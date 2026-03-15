#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../arena.h"

static double timespec_to_seconds(const struct timespec *ts_start, const struct timespec *ts_end){
	const double sec = (double)(ts_end->tv_sec - ts_start->tv_sec);
	const double nsec = (double)(ts_end->tv_nsec - ts_start->tv_nsec) / 1e9;
	return sec + nsec;
}

int main(void){
	const size_t iterations = 200000;
	const size_t alloc_size = 128;

	struct timespec start, end;

	Arena *arena = arenaLocalInit();
	if(!arena){
		fprintf(stderr, "arenaLocalInit failed\n");
		return 1;
	}

	if(clock_gettime(CLOCK_MONOTONIC, &start) != 0){
		perror("clock_gettime");
		return 1;
	}

	for(size_t i = 0; i < iterations; ++i){
		unsigned char *ptr = (unsigned char*)arenaLocalAlloc(arena, alloc_size);
		if(!ptr){
			fprintf(stderr, "arenaLocalAlloc returned NULL\n");
			return 1;
		}
		ptr[0] = (unsigned char)(i & 0xFF);
		ptr[alloc_size - 1] = (unsigned char)((i >> 8) & 0xFF);
	}

	if(clock_gettime(CLOCK_MONOTONIC, &end) != 0){
		perror("clock_gettime");
		return 1;
	}

	const double arena_seconds = timespec_to_seconds(&start, &end);

	arenaLocalDestroy(arena);

	if(clock_gettime(CLOCK_MONOTONIC, &start) != 0){
		perror("clock_gettime");
		return 1;
	}

	unsigned char **ptrs = (unsigned char**)malloc(iterations * sizeof(unsigned char*));
	if(!ptrs){
		fprintf(stderr, "malloc pointer array failed\n");
		return 1;
	}

	for(size_t i = 0; i < iterations; ++i){
		unsigned char *ptr = (unsigned char*)malloc(alloc_size);
		if(!ptr){
			fprintf(stderr, "malloc failed at iteration %zu\n", i);
			return 1;
		}
		ptr[0] = (unsigned char)(i & 0xFF);
		ptr[alloc_size - 1] = (unsigned char)((i >> 8) & 0xFF);
		ptrs[i] = ptr;
	}

	for(size_t i = 0; i < iterations; ++i){
		free(ptrs[i]);
	}
	free(ptrs);

	if(clock_gettime(CLOCK_MONOTONIC, &end) != 0){
		perror("clock_gettime");
		return 1;
	}

	const double malloc_seconds = timespec_to_seconds(&start, &end);

	printf("Arena allocator elapsed: %.9f seconds\n", arena_seconds);
	printf("malloc/free elapsed: %.9f seconds\n", malloc_seconds);
	printf("{\"arena_time\": %.9f, \"malloc_time\": %.9f}\n", arena_seconds, malloc_seconds);
	return 0;
}
