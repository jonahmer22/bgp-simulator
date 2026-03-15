#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../arena.h"

typedef struct RouteNode{
	char prefix[16];
	unsigned int as_path[6];
	struct RouteNode *next;
} RouteNode;

static double timespec_to_seconds(const struct timespec *start, const struct timespec *end){
	const double sec = (double)(end->tv_sec - start->tv_sec);
	const double nsec = (double)(end->tv_nsec - start->tv_nsec) / 1e9;
	return sec + nsec;
}

static unsigned int hash_prefix(unsigned int seed, size_t idx){
	return seed * 2654435761u + (unsigned int)idx * 977u;
}

static void fill_route(RouteNode *node, unsigned int seed, size_t idx){
	unsigned int hash = hash_prefix(seed, idx);
	snprintf(node->prefix, sizeof(node->prefix), "10.%u.%u.%u/24",
			 (hash >> 16) & 0xFF, (hash >> 8) & 0xFF, hash & 0xFF);
	for(size_t i = 0; i < 6; ++i){
		node->as_path[i] = hash_prefix(seed + (unsigned int)i, idx + i) & 0xFFFF;
	}
	node->next = NULL;
}

static unsigned long long walk_routes(RouteNode *head){
	unsigned long long checksum = 0;
	for(RouteNode *node = head; node != NULL; node = node->next){
		checksum += (unsigned long long)node->as_path[0];
		checksum ^= (unsigned long long)node->prefix[0];
		checksum = (checksum << 7) | (checksum >> (57));
	}
	return checksum;
}

int main(int argc, char **argv){
	if(argc < 2){
		fprintf(stderr, "usage: %s [arena|malloc]\n", argv[0]);
		return 1;
	}

	const size_t route_count = 50000;
	const unsigned int seed = 1337u;
	struct timespec start, end;
	RouteNode *head = NULL;
	RouteNode **tail = &head;
	unsigned long long checksum = 0;

	if(strcmp(argv[1], "arena") == 0){
		Arena *arena = arenaLocalInit();
		if(!arena){
			fprintf(stderr, "arenaLocalInit failed\n");
			return 1;
		}

		if(clock_gettime(CLOCK_MONOTONIC, &start) != 0){
			perror("clock_gettime");
			return 1;
		}

		for(size_t i = 0; i < route_count; ++i){
			RouteNode *node = (RouteNode*)arenaLocalAlloc(arena, sizeof(RouteNode));
			if(!node){
				fprintf(stderr, "arenaLocalAlloc failed\n");
				return 1;
			}
			fill_route(node, seed, i);
			*tail = node;
			tail = &node->next;
		}

		checksum = walk_routes(head);

		if(clock_gettime(CLOCK_MONOTONIC, &end) != 0){
			perror("clock_gettime");
			return 1;
		}

		arenaLocalDestroy(arena);
	} else{
		if(clock_gettime(CLOCK_MONOTONIC, &start) != 0){
			perror("clock_gettime");
			return 1;
		}

		for(size_t i = 0; i < route_count; ++i){
			RouteNode *node = (RouteNode*)malloc(sizeof(RouteNode));
			if(!node){
				fprintf(stderr, "malloc failed\n");
				return 1;
			}
			fill_route(node, seed, i);
			*tail = node;
			tail = &node->next;
		}

		checksum = walk_routes(head);

		if(clock_gettime(CLOCK_MONOTONIC, &end) != 0){
			perror("clock_gettime");
			return 1;
		}

		for(RouteNode *node = head; node != NULL;){
			RouteNode *next = node->next;
			free(node);
			node = next;
		}
	}

	const double seconds = timespec_to_seconds(&start, &end);
	printf("%s path elapsed: %.9f seconds\n", argv[1], seconds);
	printf("{\"mode\": \"%s\", \"seconds\": %.9f, \"checksum\": %llu}\n",
		   argv[1], seconds, checksum);
	return 0;
}
