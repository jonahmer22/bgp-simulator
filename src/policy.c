#include "policy.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "bgp_utils.h"

static inline void spin_init(SpinLock *lock){
	lock->flag = (atomic_flag)ATOMIC_FLAG_INIT;
}

static inline void spin_lock(SpinLock *lock){
	while(atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire)){
#if defined(__aarch64__)
		__asm__ __volatile__("yield");
#else
		__asm__ __volatile__("pause");
#endif
	}
}

static inline void spin_unlock(SpinLock *lock){
	atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

// decide which one to go first
static bool prefer_first(const Announcement *lhs, const Announcement *rhs){
	// get the relation from the announcement
	int lhs_pref = relation_preference(lhs->received_relation);
	int rhs_pref = relation_preference(rhs->received_relation);

	// if they arent equal
	if(lhs_pref != rhs_pref){
		return lhs_pref > rhs_pref;	// does lhs have higher preference than rhs
	}
	if(lhs->as_path.size != rhs->as_path.size){	// if the path is not the same size
		return lhs->as_path.size < rhs->as_path.size;	// does lhs have a smaller path than rhs
	}

	return lhs->next_hop_asn < rhs->next_hop_asn;	// is lhs's next hop smaller than rhs's
}

// initializes an announcement list
static void announcement_list_init(AnnouncementList *list){
	list->items = NULL;
	list->size = 0;
	list->capacity = 0;
}

// does nothing
static void announcement_list_free(AnnouncementList *list){
	if(!list){
		return;
	}

	// for every item in the list
	for(size_t i = 0; i < list->size; ++i){
		announcement_free(&list->items[i]);	// free it
	}

	// null values
	list->items = NULL;
	list->size = 0;
	list->capacity = 0;
}

static int announcement_list_push_copy(AnnouncementList *list, const Announcement *src){
	if(list->size == list->capacity){	// if the list is full
		size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;	// expand it with a base of 4
		size_t old_bytes = list->capacity * sizeof(Announcement);

		// realloc to new size
		Announcement *new_items = (Announcement *)bgp_realloc(list->items, old_bytes, new_capacity * sizeof(Announcement));
		if(!new_items){
			return -1;
		}

		// copy over old values
		list->items = new_items;
		list->capacity = new_capacity;
	}

	// assign and initialize a announcement
	Announcement *dest = &list->items[list->size++];
	announcement_init(dest);
	if(announcement_copy(dest, src) != 0){
		return -1;	// copy failed return failure
	}

	return 0;
}

// nothing
static void received_entry_free(void *value){
	ReceivedEntry *entry = (ReceivedEntry *)value;
	if(!entry){
		return;
	}
	announcement_list_free(&entry->announcements);
}
static void rib_entry_free(void *value){
	Announcement *entry = (Announcement *)value;
	if(!entry){
		return;
	}
	announcement_free(entry);
}

// creates a policy and initializes it
Policy *policy_create(bool drop_invalid){
	Policy *policy = (Policy *)bgp_alloc(sizeof(Policy));	// alloc the space
	memset(policy, 0, sizeof(Policy));	// set to all 0s by default

	policy->drop_invalid = drop_invalid;	// set drop_invalid bool
	spin_init(&policy->lock);
	// initialize maps
	uint32_map_init(&policy->local_rib);
	uint32_map_init(&policy->received);

	return policy;
}

// does nothing
void policy_destroy(Policy *policy){
	if(!policy){
		return;
	}

	uint32_map_clear(&policy->received, received_entry_free);
	uint32_map_clear(&policy->local_rib, rib_entry_free);
	uint32_map_free(&policy->received);
	uint32_map_free(&policy->local_rib);
}

// adds an entry to a policies recieved map
static ReceivedEntry *get_or_create_received_entry(Policy *policy, uint32_t prefix_id){
	ReceivedEntry *entry = (ReceivedEntry *)uint32_map_get(&policy->received, prefix_id);
	if(entry){
		return entry;
	}

	// alloc the space for it
	entry = (ReceivedEntry *)bgp_alloc(sizeof(ReceivedEntry));
	memset(entry, 0, sizeof(ReceivedEntry));	// set to 0s by default (now that I think of it, all of these memsets have to have some form of overhead, I might remove them and just hope the memory is blank)
	announcement_list_init(&entry->announcements);	// init the list
	entry->best_index = SIZE_MAX;
	entry->has_best = false;

	// add to the map
	if(uint32_map_set(&policy->received, prefix_id, entry) != 0){
		// received_entry_free(entry);
		return NULL;	// setting in  the map failed
	}

	return entry;
}

void policy_receive(Policy *policy, const Announcement *announcement){
	if(!policy || !announcement){
		return;	// stuff has to exist (i wonder if removing all of these and forgoing safety would help with speed too)
	}
	spin_lock(&policy->lock);
	// if both policy and the rov are invalid then exit
	if(policy->drop_invalid && announcement->rov_invalid){
		spin_unlock(&policy->lock);
		return;
	}

	// add the entry into recieved map
	ReceivedEntry *entry = get_or_create_received_entry(policy, announcement->prefix_id);
	if(!entry){
		spin_unlock(&policy->lock);
		return;
	}

	// add announcement to the list
	size_t insert_index = entry->announcements.size;
	if(announcement_list_push_copy(&entry->announcements, announcement) == 0){
		if(!entry->has_best){
			entry->best_index = insert_index;
			entry->has_best = true;
		} else {
			Announcement *current_best = &entry->announcements.items[entry->best_index];
			Announcement *candidate = &entry->announcements.items[insert_index];
			if(prefer_first(candidate, current_best)){
				entry->best_index = insert_index;
			}
		}
	}
	spin_unlock(&policy->lock);
}

// see whether an annnouncement contains an asn
static bool announcement_contains_asn(const Announcement *announcement, uint32_t asn){
	for(size_t i = 0; i < announcement->as_path.size; ++i){	// for the length announcement path
		if(announcement->as_path.data[i] == asn){	// if the asn is in the data
			return true;	// true
		}
	}

	// didnt find it
	return false;
}

// decide which one to go first
// context for processig a revieved entry
typedef struct ProcessContext{
	Policy *policy;
	uint32_t self_asn;
} ProcessContext;

// processes a recieved entry, idk what else to say about it
static void process_received_entry(uint32_t prefix_id, void *value, void *ctx){
	// get the entry and context
	ReceivedEntry *entry = (ReceivedEntry *)value;
	ProcessContext *context = (ProcessContext *)ctx;
	if(!entry || !context){
		return;	// they have to exist
	}

	// initialize an announcent
	Announcement best;
	announcement_init(&best);
	bool has_best = false;
	// fast path: try cached best if it doesn't loop
	if(entry->has_best && entry->best_index < entry->announcements.size){
		Announcement *cached = &entry->announcements.items[entry->best_index];
		if(!announcement_contains_asn(cached, context->self_asn)){
			if(announcement_copy(&best, cached) == 0){
				has_best = true;
			}
		}
	}
	// fall back to full scan if no valid cached best
	if(!has_best){
		for(size_t i = 0; i < entry->announcements.size; ++i){	// for every announcement in the entry
			Announcement *candidate = &entry->announcements.items[i];	// get the announcement
			if(announcement_contains_asn(candidate, context->self_asn)){
				continue;	// if it contains self_asn then skip
			}

			// if there isnt a best of the candidate is prefered over best
			if(!has_best || prefer_first(candidate, &best)){
				// announcement_free(&best);
				announcement_init(&best);	// init best (wipes all values)
				if(announcement_copy(&best, candidate) != 0){	// copy over candidate into best
					continue;
				}
				has_best = true;	// set has_best to true
			}
		}
	}
	if(!has_best){
		// announcement_free(&best);
		return;	// if no best was found in the entries then return
	}
	// prepend self_asn to best
	announcement_prepend_asn(&best, context->self_asn);

	// get an existing announcement from prefix_id
		Announcement *existing = (Announcement *)uint32_map_get(&context->policy->local_rib, prefix_id);
		if(existing){	// if one does exist
			if(!prefer_first(&best, existing)){
				// announcement_free(&best);
				return;	// best isn't prefered
			}
			// announcement_free(existing);
			announcement_init(existing);	// wipe existing
			if(announcement_copy(existing, &best) != 0){	// copy and free best into existing
				// announcement_free(&best);
				return;
			}
		}
		else{	// if existing doesnt exist
			// allocate the memory needed
		Announcement *stored = (Announcement *)bgp_alloc(sizeof(Announcement));
		// 0 it all out
			memset(stored, 0, sizeof(Announcement));
			announcement_init(stored);	// initizlialize the memory
			if(announcement_copy(stored, &best) != 0){	// copy over best into stored
				// rib_entry_free(stored);
				// announcement_free(&best);
				return;
			}
			if(uint32_map_set(&context->policy->local_rib, prefix_id, stored) != 0){	// add stored to the prefix_id
				// rib_entry_free(stored);
				// announcement_free(&best);
				return;
			}
		}
		// announcement_free(&best);
	}

// processes over the entire recieved map
void policy_process_queues(Policy *policy, uint32_t self_asn){
	if(!policy){
		return;
	}

	spin_lock(&policy->lock);
	// create context
	ProcessContext context = {.policy = policy, .self_asn = self_asn};
	uint32_map_for_each(&policy->received, process_received_entry, &context);	// apply to every item in the hashmap
	uint32_map_clear(&policy->received, received_entry_free);	// this one is like the only purposefull "free" needed so that future calls to this function do not do extra work
	spin_unlock(&policy->lock);
}

// add a policy origin
void policy_add_origin(Policy *policy, const Announcement *announcement, uint32_t self_asn){
	if(!policy || !announcement){
		return;
	}

	spin_lock(&policy->lock);
	// create and store announcement in stored
	Announcement stored;
	announcement_init(&stored);
	if(announcement_copy(&stored, announcement) != 0){
		spin_unlock(&policy->lock);
		return;
	}

	// prepend self_asn to announcement
	announcement_prepend_asn(&stored, self_asn);
	stored.next_hop_asn = self_asn;	// set the next hop to be self_asn

	// check for an existing announcement
	Announcement *existing = (Announcement *)uint32_map_get(&policy->local_rib, stored.prefix_id);
		if(existing){	// if there is one
			// announcement_free(existing);
			announcement_init(existing);	// wipe it
			if(announcement_copy(existing, &stored) != 0){	// copy stored into it
				// announcement_free(&stored);
				spin_unlock(&policy->lock);
				return;
			}
		}
		else{	// if there wasnt one
			// allocate the memory needed
		Announcement *copy = (Announcement *)bgp_alloc(sizeof(Announcement));
		memset(copy, 0, sizeof(Announcement));	// 0s by default
			announcement_init(copy);	// initialize the announcement

			if(announcement_copy(copy, &stored) != 0){	// copy over stored
				// rib_entry_free(copy);
				// announcement_free(&stored);
				spin_unlock(&policy->lock);
				return;
			}

			// add the copy to the map
			if(uint32_map_set(&policy->local_rib, stored.prefix_id, copy) != 0){
				// rib_entry_free(copy);
			}
		}
	// announcement_free(&stored);
	spin_unlock(&policy->lock);
}

// returns the local_rib of a policy
const UInt32Map *policy_local_rib(const Policy *policy){
	if(!policy){	// has to exist
		return NULL;
	}

	return &policy->local_rib;
}
