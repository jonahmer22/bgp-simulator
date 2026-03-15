#ifndef ANNOUNCEMENTS_H
#define ANNOUNCEMENTS_H

#include <stdbool.h>
#include <stdint.h>

#include "vector.h"

// keeps track of the role of an announcement
typedef enum{
	RELATION_PROVIDER = 0,
	RELATION_PEER = 1,
	RELATION_CUSTOMER = 2,
	RELATION_ORIGIN = 3
} RelationType;

// contails all pertinent info for announcement and its behavior
typedef struct Announcement{
	uint32_t prefix_id;
	UInt32Vector as_path;	// path the announcement took
	uint32_t next_hop_asn;
	RelationType received_relation;
	bool rov_invalid;
} Announcement;

// announcement behavior
void announcement_init(Announcement *announcement);
void announcement_free(Announcement *announcement);
void announcement_set_prefix_id(Announcement *announcement, uint32_t prefix_id);
int announcement_push_asn(Announcement *announcement, uint32_t asn);
int announcement_prepend_asn(Announcement *announcement, uint32_t asn);
int announcement_copy(Announcement *dest, const Announcement *src);
char *announcement_as_path_string(const Announcement *announcement);

// retrieving relation info
int relation_preference(RelationType relation);
const char *relation_to_string(RelationType relation);

#endif
