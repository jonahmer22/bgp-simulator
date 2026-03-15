#include "bgp_utils.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// most of this file is a wrapper around my arena in /deps/arena/ to make it more multithreaded friendly
#include "arena.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define THREAD_LOCAL _Thread_local
#else
#define THREAD_LOCAL __thread
#endif

// centralized place to store arenas (should be 1 per thread)
typedef struct ArenaRegistry{
	pthread_mutex_t mutex;	// the one mutex actually needed
	// list of arenas basically
	Arena **arenas;
	size_t count;
	size_t capacity;
} ArenaRegistry;

// create a global arena registry
static ArenaRegistry g_registry = {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0};
// give each thread its own arena
static THREAD_LOCAL Arena *tls_arena = NULL;
static THREAD_LOCAL size_t tls_arena_index = (size_t)-1;
static pthread_key_t g_arena_key;
static pthread_once_t g_arena_key_once = PTHREAD_ONCE_INIT;

static void cleanup_thread_arena(void *ptr);
static void make_arena_key(void){
	pthread_key_create(&g_arena_key, cleanup_thread_arena);
}

// register a new arena
static void register_arena(Arena *arena){
	pthread_mutex_lock(&g_registry.mutex);

	// try to reuse a NULL slot if a thread exited
	size_t slot = g_registry.count;
	for(size_t i = 0; i < g_registry.count; ++i){
		if(g_registry.arenas[i] == NULL){
			slot = i;
			break;
		}
	}

	if(slot == g_registry.count && g_registry.count == g_registry.capacity){	// out of space
		size_t new_capacity = g_registry.capacity == 0 ? 8 : g_registry.capacity * 2;	// make more space with a floor of 8
		// this is the only time system allocators are used (that I remember)
		Arena **new_list = (Arena **)realloc(g_registry.arenas, new_capacity * sizeof(Arena *));
		if(!new_list){	// the one system alloc failed
			pthread_mutex_unlock(&g_registry.mutex);
			abort();
		}

		// write new data over old
		g_registry.arenas = new_list;
		g_registry.capacity = new_capacity;
	}

	// assign an index to the arena and give it a pointer in the list
	tls_arena_index = slot;
	g_registry.arenas[slot] = arena;
	if(slot == g_registry.count){
		g_registry.count++;
	}

	pthread_mutex_unlock(&g_registry.mutex);
}

// returns the arena for the thread (or creates it)
static Arena *get_thread_arena(void){
	if(!tls_arena){	// if it doesnt exist
		pthread_once(&g_arena_key_once, make_arena_key);
		tls_arena = arenaLocalInit();	// make it
		if(!tls_arena){	// if it failed
			abort();	// panick
		}
		// add to registry
		register_arena(tls_arena);
		pthread_setspecific(g_arena_key, tls_arena);
	}

	// return the old or just created arena
	return tls_arena;
}

// resets *ALL* the arenas
void bgp_arena_reset(void){
	pthread_mutex_lock(&g_registry.mutex);

	// for every arena
	for(size_t i = 0; i < g_registry.count; ++i){
		if(g_registry.arenas[i]){
			arenaLocalReset(g_registry.arenas[i]);	// perform a local reset
		}
	}

	pthread_mutex_unlock(&g_registry.mutex);
}

static void cleanup_thread_arena(void *ptr){
	(void)ptr;	// intentionally leak thread arenas so collected data remains valid after thread exit
}

// allocate to the threads arena
void *bgp_alloc(size_t size){
	Arena *arena = get_thread_arena();	// get the appropriate arena
	void *ptr = arenaLocalAlloc(arena, size ? size : 1);	// use arenaLocalAlloc() (should be mmap backed if the system supports it)
	if(!ptr){
		abort();	// allocation failed
	}

	return ptr;
}

// duplicates a string in the arena, good for like strdup("Hello, World!")
char *bgp_strdup(const char *input){
	if(!input){
		return NULL;
	}

	// allocate a string in the arena
	Arena *arena = get_thread_arena();
	size_t len = strlen(input);
	char *copy = (char *)arenaLocalAlloc(arena, len + 1);
	if(!copy){
		abort();
	}

	// move over the memory to the arena
	memcpy(copy, input, len + 1);

	return copy;	// return char *
}

// allocates memory and copies over values at *ptr into new arena memory
// honestly a little misleading because std realloc frees the old stuff but here that doesn't happen, but idc tbh, quicker to leak than free
void *bgp_realloc(void *ptr, size_t old_size, size_t new_size){
	// get the arena and allocate newsize buffer
	Arena *arena = get_thread_arena();
	void *result = arenaLocalAlloc(arena, new_size ? new_size : 1);
	if(!result){
		abort();
	}

	// if there was old data to copy do so
	if(ptr && old_size > 0){
		size_t copy_size = old_size < new_size ? old_size : new_size;
		memcpy(result, ptr, copy_size);
	}

	return result;
}
