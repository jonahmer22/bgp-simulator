#include "announcement.h"

#include <stdio.h>

#include "bgp_utils.h"

// initializes an announcement
inline void announcement_init(Announcement *announcement){
	if(!announcement){	// im not going to keep commenting this, its just making sure that arguements exist
		return;
	}

	// set everything to 0 or whatever makes sense
	announcement->prefix_id = 0;
	uint32_vector_init(&announcement->as_path);
	announcement->next_hop_asn = 0;
	announcement->received_relation = RELATION_ORIGIN;
	announcement->rov_invalid = false;
}

// doesnt actually free anything just nulls all values
void announcement_free(Announcement *announcement){
	if(!announcement){
		return;
	}

	uint32_vector_free(&announcement->as_path);
}

// assigns a given prefix ID to an announcement
void announcement_set_prefix_id(Announcement *announcement, uint32_t prefix_id){
	if(!announcement){
		return;
	}

	announcement->prefix_id = prefix_id;
}

// pushes an asn to the announcement path
int announcement_push_asn(Announcement *announcement, uint32_t asn){
	if(!announcement){
		return -1;
	}

	return uint32_vector_push(&announcement->as_path, asn);
}

// prepends an asn to the announcement path
int announcement_prepend_asn(Announcement *announcement, uint32_t asn){
	if(!announcement){
		return -1;
	}

	return uint32_vector_prepend(&announcement->as_path, asn);
}

// coppies an announcement
inline int announcement_copy(Announcement *dest, const Announcement *src){
	if(!dest || !src){
		return -1;
	}

	// copy all the items over
	dest->prefix_id = src->prefix_id;
	if(uint32_vector_copy(&dest->as_path, &src->as_path) != 0){
		return -1;	// copy failed
	}
	dest->next_hop_asn = src->next_hop_asn;
	dest->received_relation = src->received_relation;
	dest->rov_invalid = src->rov_invalid;

	return 0;
}

// convert the as_path to a string and return the buffer
char *announcement_as_path_string(const Announcement *announcement){
	if(!announcement){
		return NULL;
	}

	// alocate the correct buffer size
	size_t max_chars = announcement->as_path.size * 11 + 4;
	if(max_chars < 8){
		max_chars = 8;
	}
	char *buffer = (char *)bgp_alloc(max_chars);

	// add all characters to the buffer
	size_t offset = 0;
	buffer[offset++] = '(';	// start with (
	for(size_t i = 0; i < announcement->as_path.size; ++i){	// add each asn in the path to the string
		if(i > 0){
			buffer[offset++] = ',';
			buffer[offset++] = ' ';
		}
		int written = snprintf(buffer + offset, max_chars - offset, "%u", announcement->as_path.data[i]);
		offset += (size_t)written;
	}
	if(announcement->as_path.size == 1){
		buffer[offset++] = ',';
	}
	buffer[offset++] = ')';	// end with )
	buffer[offset] = '\0'; 	// add EOF

	return buffer;
}

// cast relation to int
int inline relation_preference(RelationType relation){
	return (int)relation;
}

// returns a string representation of relationship
const char *relation_to_string(RelationType relation){
	switch(relation){
		case RELATION_ORIGIN:
			return "ORIGIN";
		case RELATION_CUSTOMER:
			return "CUSTOMER";
		case RELATION_PEER:
			return "PEER";
		case RELATION_PROVIDER:
			return "PROVIDER";
	}
	return "UNKNOWN";
}
