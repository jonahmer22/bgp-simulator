#ifndef PARALLEL_H
#define PARALLEL_H

#include <stddef.h>

// splits up a range into 2 and does it in parallel
typedef void (*ParallelRangeFn)(size_t start, size_t end, void *ctx);
void parallel_for(size_t count, ParallelRangeFn fn, void *ctx);

// just does 2 functions at the same time (the void * becomes the ctx if you couldnt tell)
void parallel_run(void (*fn1)(void *), void *ctx1, void (*fn2)(void *), void *ctx2);

#endif
