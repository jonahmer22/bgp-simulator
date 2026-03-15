#include "string_pool.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bgp_utils.h"

// struct of string pools
typedef struct StringPool{
	char **entries;
	size_t count;
	size_t capacity;
	struct Bucket{
		unsigned long hash;
		uint32_t id;
		char *value;
		int occupied;
	} *buckets;
	size_t bucket_count;
	size_t bucket_used;
} StringPool;

// global string pool
static StringPool g_pool;
static uint32_t g_empty_id = UINT32_MAX;

// basic string hashing function
static unsigned long hash_string(const char *str){
	unsigned long hash = 1469598103934665603ull;

	while(*str){
		hash ^= (unsigned char)(*str++);
		hash *= 1099511628211ull;
	}

	return hash;
}

// make sure that there is enough space in the pool
static void ensure_entry_capacity(StringPool *pool){
	if(pool->count < pool->capacity){
		return;
	}

	// calculate the new max size with a floor of 64
	size_t new_capacity = pool->capacity == 0 ? 64 : pool->capacity * 2;
	size_t old_bytes = pool->capacity * sizeof(char *);
	// realloc space
	char **new_entries = (char **)bgp_realloc(pool->entries, old_bytes, new_capacity * sizeof(char *));

	// assign new values to pool
	pool->entries = new_entries;
	pool->capacity = new_capacity;
}

// clears a pool (not usefull as far as im aware)
static void string_pool_clear(StringPool *pool){
	pool->entries = NULL;
	pool->count = 0;
	pool->capacity = 0;
	pool->buckets = NULL;
	pool->bucket_count = 0;
	pool->bucket_used = 0;
}

// resizes buckets to a new_capacity
static void buckets_resize(StringPool *pool, size_t new_capacity){
	// makes buckets of new_capacity
	struct Bucket *new_buckets = (struct Bucket *)bgp_alloc(new_capacity * sizeof(struct Bucket));
	memset(new_buckets, 0, new_capacity * sizeof(struct Bucket));	// zero out values of new_buckets

	// for every bucket
	for(size_t i = 0; i < pool->bucket_count; ++i){
		// get the bucket
		struct Bucket *bucket = &pool->buckets[i];
		if(!bucket->occupied){
			continue;
		}

		// get the index and find the next unoccupied bucket
		size_t idx = bucket->hash & (new_capacity - 1);
		while(new_buckets[idx].occupied){
			idx = (idx + 1) & (new_capacity - 1);
		}

		// assign bucket
		new_buckets[idx] = *bucket;
	}

	// add new values
	pool->buckets = new_buckets;
	pool->bucket_count = new_capacity;
	pool->bucket_used = pool->count;
}

// resets the string_pool
void string_pool_reset(void){
	g_empty_id = UINT32_MAX;
	string_pool_clear(&g_pool);
}

// returns the size of the string pool
uint32_t string_pool_size(void){
	return (uint32_t)g_pool.count;
}

// intern strings into the pool
uint32_t string_pool_intern(const char *value){
	if(!value || !*value){	// if there is no pointer or value at the pointer
		// assign empty_id to a effectively unreachable value
		if(g_empty_id != UINT32_MAX){
			return g_empty_id;
		}

		// make sure there is enough space in the pool
		ensure_entry_capacity(&g_pool);
		char *copy = bgp_strdup("");	// make a copy of an empty string on the heap
		uint32_t id = (uint32_t)g_pool.count++;	// give it an id
		g_pool.entries[id] = copy;	// place it somewhere in the pool
		if(g_pool.bucket_count == 0){
			buckets_resize(&g_pool, 128);
		}	// get the hash of of the string
		unsigned long hash = hash_string(copy);
		size_t idx = hash & (g_pool.bucket_count - 1);
		// find the next unoccupied bucket
		while(g_pool.buckets[idx].occupied){
			idx = (idx + 1) & (g_pool.bucket_count - 1);
		}

		// set the metadata for the bucket
		g_pool.buckets[idx].occupied = 1;
		g_pool.buckets[idx].hash = hash;
		g_pool.buckets[idx].id = id;
		g_pool.buckets[idx].value = copy;
		g_pool.bucket_used += 1;

		// set the empty_id to id
		g_empty_id = id;

		return id;
	}

	// if the bucket count is zero or bucket usage is over threshold
	if(g_pool.bucket_count == 0 || (double)(g_pool.bucket_used + 1) / (double)g_pool.bucket_count > 0.7){
		size_t new_capacity = g_pool.bucket_count == 0 ? 256 : g_pool.bucket_count * 2;	// double the size with a minimum of 256
		buckets_resize(&g_pool, new_capacity);
	}

	// get the new hash and index
	unsigned long hash = hash_string(value);
	size_t idx = hash & (g_pool.bucket_count - 1);
	while(g_pool.buckets[idx].occupied){	// find the next unoccupied bucket
		if(g_pool.buckets[idx].hash == hash && strcmp(g_pool.buckets[idx].value, value) == 0){
			return g_pool.buckets[idx].id;
		}

		idx = (idx + 1) & (g_pool.bucket_count - 1);
	}

	// make sure there is enough space
	ensure_entry_capacity(&g_pool);
	char *copy = bgp_strdup(value);	// copy the string
	uint32_t id = (uint32_t)g_pool.count++;	// increase the count

	// 
	g_pool.entries[id] = copy;
	g_pool.buckets[idx].occupied = 1;
	g_pool.buckets[idx].hash = hash;
	g_pool.buckets[idx].id = id;
	g_pool.buckets[idx].value = copy;
	g_pool.bucket_used += 1;

	return id;
}

// gets the string from the pool at a given id
const char *string_pool_get(uint32_t id){
	if(id >= g_pool.count){
		return "";	// if oob just return a empty string
	}

	// return the string in pool entries at id
	return g_pool.entries[id];
}
