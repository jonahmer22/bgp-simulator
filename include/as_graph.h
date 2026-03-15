#ifndef AS_GRAPH_H
#define AS_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "as_node.h"
#include "hash_map.h"

// main struct, "the big one" (ironically not the biggest struct like lines wise)
// holds the entire graph of ASNodes
typedef struct ASGraph{
	ASNode **nodes;
	size_t size;
	size_t capacity;
	UInt32Map index;
	UInt32Vector *rank_buckets;
	size_t rank_count;
} ASGraph;

// init and destroy
void as_graph_init(ASGraph *graph);
void as_graph_free(ASGraph *graph);

// creation and fetching
ASNode *as_graph_get_or_create(ASGraph *graph, uint32_t asn);
ASNode *as_graph_find(ASGraph *graph, uint32_t asn);
const ASNode *as_graph_find_const(const ASGraph *graph, uint32_t asn);

// adding nodes in a relationship
void as_graph_add_provider_customer(ASGraph *graph, uint32_t provider, uint32_t customer);
void as_graph_add_peer(ASGraph *graph, uint32_t left, uint32_t right);

// compute and fetch ranks
void as_graph_compute_propagation_ranks(ASGraph *graph);
const UInt32Vector *as_graph_ranks(const ASGraph *graph, size_t *rank_count);

// returns all the nodes in the graph in the form of a list
ASNode **as_graph_nodes(const ASGraph *graph, size_t *count);

#endif
