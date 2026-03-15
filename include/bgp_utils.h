#ifndef BGP_UTILS_H
#define BGP_UTILS_H

#include <stddef.h>

// memory management (wrapper for arena that makes it more multithreaded friendly)
// see /deps/arena/ or my https://github.com/jonahmer22/arena/ for the actual memory code
char *bgp_strdup(const char *input);
void *bgp_alloc(size_t size);
void *bgp_realloc(void *ptr, size_t old_size, size_t new_size);
void bgp_arena_reset(void);

#endif
