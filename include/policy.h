#ifndef POLICY_H
#define POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "announcement.h"
#include "hash_map.h"

typedef struct SpinLock{
	atomic_flag flag;
} SpinLock;

// basic list of announcement
typedef struct AnnouncementList{
	Announcement *items;
	size_t size;
	size_t capacity;
} AnnouncementList;

// wrapper for announcement list
typedef struct ReceivedEntry{
	AnnouncementList announcements;
	size_t best_index;
	bool has_best;
} ReceivedEntry;

// keeps track of a policies information like what it may have recieved and whether or not to drop invalid
typedef struct Policy{
	bool drop_invalid;
	SpinLock lock;	// guards received/local_rib during parallel sends
	UInt32Map local_rib;
	UInt32Map received;
} Policy;

// create and destroy
Policy *policy_create(bool drop_invalid);
void policy_destroy(Policy *policy);

// basic policy functions
void policy_receive(Policy *policy, const Announcement *announcement);
void policy_process_queues(Policy *policy, uint32_t self_asn);
void policy_add_origin(Policy *policy, const Announcement *announcement, uint32_t self_asn);
const UInt32Map *policy_local_rib(const Policy *policy);

#endif
