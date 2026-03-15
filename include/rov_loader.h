#ifndef ROV_LOADER_H
#define ROV_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hash_map.h"

// wrapper for hashmap, but functions make it act like a set
typedef struct ROVSet{
	UInt32Map entries;
} ROVSet;

// create and destroy
void rov_set_init(ROVSet *set);
void rov_set_free(ROVSet *set);

// add and check includes
int rov_set_insert(ROVSet *set, uint32_t asn);
bool rov_set_contains(const ROVSet *set, uint32_t asn);

// load from file or directly from a string
int rov_loader_load_file(const char *path, ROVSet *set, char **error_message);
int rov_loader_load_string(const char *content, ROVSet *set, char **error_message);

#endif
