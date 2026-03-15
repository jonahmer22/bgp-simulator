#ifndef ANNOUNCEMENTS_LOADER_H
#define ANNOUNCEMENTS_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "as_graph.h"

// info for the ASNode that an announcement starts at
typedef struct OriginSeed{
	uint32_t origin_asn;
	uint32_t prefix_id;
	bool rov_invalid;
} OriginSeed;

// list of seeds, keeps track of all the starting announcements
typedef struct OriginSeedList{
	OriginSeed *items;
	size_t size;
	size_t capacity;
} OriginSeedList;

// init and destroy
void origin_seed_list_init(OriginSeedList *list);
void origin_seed_list_free(OriginSeedList *list);

// loaders
int announcements_loader_load_file(const char *path, ASGraph *graph, OriginSeedList *seeds, char **error_message);
int announcements_loader_load_string(const char *content, ASGraph *graph, OriginSeedList *seeds, char **error_message);

#endif
