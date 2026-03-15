#include "hash_map.h"

#include <string.h>

#include "bgp_utils.h"

// constants
static const size_t kInitialCapacity = 128;
static const float kMaxLoadFactor = 0.75f;

// basic hash function (gutted because I remembered that I could've just used the value of the uint)
static inline unsigned long hash_uint32(uint32_t value){
	// value = ((value >> 16) ^ value) * 0x45d9f3b;
	// value = ((value >> 16) ^ value) * 0x45d9f3b;
	// value = (value >> 16) ^ value;
	return (unsigned long)value;
}

// doubles the capacity with a floor of 128
static size_t next_capacity(size_t capacity){
	return capacity == 0 ? kInitialCapacity : capacity * 2;
}

// resizes the map to a new capacity
static int uint32_map_resize(UInt32Map *map, size_t new_capacity){
	// allocate new buckets
	UInt32MapEntry **new_buckets = (UInt32MapEntry **)bgp_alloc(new_capacity * sizeof(UInt32MapEntry *));
	memset(new_buckets, 0, new_capacity * sizeof(UInt32MapEntry *));	// 0 out all the values
	// move over entries from the map to new_bockets
	for(size_t i = 0; i < map->capacity; ++i){
		UInt32MapEntry *entry = map->buckets ? map->buckets[i] : NULL;
		while(entry){	// while there are entries
			UInt32MapEntry *next = entry->next;
			size_t idx = entry->hash & (new_capacity - 1);	// equivielent to a %
			entry->next = new_buckets[idx];
			new_buckets[idx] = entry;
			entry = next;
		}
	}

	// copy over new values
	map->buckets = new_buckets;
	map->capacity = new_capacity;

	return 0;
}

// initialize a map
void uint32_map_init(UInt32Map *map){
	map->buckets = NULL;
	map->capacity = 0;
	map->size = 0;
	map->free_entries = NULL;
}

// useless
void uint32_map_free(UInt32Map *map){
	if(!map){
		return;
	}
	map->buckets = NULL;
	map->capacity = 0;
	map->size = 0;
	map->free_entries = NULL;
}

// set a value for a key in a map
int uint32_map_set(UInt32Map *map, uint32_t key, void *value){
	if(!map){	// the map has to exist
		return -1;
	}

	// make sure there is enough room
	if(map->capacity == 0){	// initialize if empty
		if(uint32_map_resize(map, kInitialCapacity) != 0){
			return -1;
		}
	}
	else if((float)(map->size + 1) / (float)map->capacity > kMaxLoadFactor){	// add more if above load factor
		if(uint32_map_resize(map, next_capacity(map->capacity)) != 0){
			return -1;
		}
	}

	// get the entry the value will go into
	unsigned long hash = hash_uint32(key);
	size_t idx = hash & (map->capacity - 1);
	UInt32MapEntry *entry = map->buckets[idx];

	// search for the entry in the map for the key
	while(entry){
		if(entry->key == key){	// found it
			entry->value = value;
			return 0;
		}
		entry = entry->next;	// didnt find, move onto next
	}
	// couldnt find it in map so add it to a any free entries
	if(map->free_entries){
		entry = map->free_entries;
		map->free_entries = entry->next;
	}
	else{	// have to allocate a new one if there are no left over free entries
		entry = (UInt32MapEntry *)bgp_alloc(sizeof(UInt32MapEntry));
	}

	// assign entry values
	entry->key = key;
	entry->value = value;
	entry->hash = hash;
	entry->next = map->buckets[idx];

	// add it to the map and increment size
	map->buckets[idx] = entry;
	map->size += 1;

	return 0;
}

// fetches the value for a key
void *uint32_map_get(const UInt32Map *map, uint32_t key){
	if(!map || map->capacity == 0){
		return NULL;	// map didnt exist
	}

	// get the entry
	unsigned long hash = hash_uint32(key);
	size_t idx = hash & (map->capacity - 1);
	const UInt32MapEntry *entry = map->buckets[idx];
	// search for the key in the entry
	while(entry){
		if(entry->key == key){
			return entry->value;	// found and return
		}
		entry = entry->next;
	}

	// didnt find it
	return NULL;
}

// applies a function to every item in the map
void uint32_map_for_each(const UInt32Map *map, UInt32MapIterFn fn, void *ctx){
	if(!map || !fn){
		return;
	}
	for(size_t i = 0; i < map->capacity; ++i){	// for every item in the map
		UInt32MapEntry *entry = map->buckets ? map->buckets[i] : NULL;
		while(entry){	// for everything in the entry
			fn(entry->key, entry->value, ctx);	// apply the function
			entry = entry->next;
		}
	}
}

// doesnt do anything anymore
void uint32_map_clear(UInt32Map *map, UInt32MapFreeFn value_free){
	if(!map){
		return;
	}

	for(size_t i = 0; i < map->capacity; ++i){
		UInt32MapEntry *entry = map->buckets ? map->buckets[i] : NULL;
		while(entry){
			UInt32MapEntry *next = entry->next;
			
			if(value_free){
				value_free(entry->value);
			}
			
			entry->next = map->free_entries;
			map->free_entries = entry;
			entry = next;
		}
		
		if(map->buckets){
			map->buckets[i] = NULL;
		}
	}
	
	map->size = 0;
}
