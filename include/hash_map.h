#ifndef HASH_MAP_H
#define HASH_MAP_H

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

// elements in the hashmap
typedef struct UInt32MapEntry{
	uint32_t key;
	void *value;
	unsigned long hash;
	struct UInt32MapEntry *next;
} UInt32MapEntry;

// basic hashmap that uses buckets to handle collisions
typedef struct UInt32Map{
	UInt32MapEntry **buckets;
	size_t capacity;
	size_t size;
	UInt32MapEntry *free_entries;	// for easy access to "dead" or "gravestone" entries (allows for quicker allocations in case of collisions)
} UInt32Map;

// creation and destruction
void uint32_map_init(UInt32Map *map);
void uint32_map_free(UInt32Map *map);

// addition and fetching of values
int uint32_map_set(UInt32Map *map, uint32_t key, void *value);
void *uint32_map_get(const UInt32Map *map, uint32_t key);

// applies a function to every element in the hash
typedef void (*UInt32MapIterFn)(uint32_t key, void *value, void *ctx);
void uint32_map_for_each(const UInt32Map *map, UInt32MapIterFn fn, void *ctx);

// frees an entire map along with all of its elements (function not included ™)
typedef void (*UInt32MapFreeFn)(void *value);
void uint32_map_clear(UInt32Map *map, UInt32MapFreeFn value_free);

#endif
