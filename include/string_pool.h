#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <stdint.h>

// interestingly I learned this concept from the Crafting Interpreters textbook I read in my free time, speeds up string processing in language runtimes a lot
// should allow for writing big output csv files really quickly, or really just using any strings quickly

// resets the string pool
void string_pool_reset(void);

// add and get strings from the pool
uint32_t string_pool_intern(const char *value);
const char *string_pool_get(uint32_t id);

// returns the size of the string pool
uint32_t string_pool_size(void);

#endif
