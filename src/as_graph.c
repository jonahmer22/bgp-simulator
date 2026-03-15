#include "as_graph.h"

#include <string.h>

#include "bgp_utils.h"

static int ensure_node_capacity(ASGraph *graph, size_t required){
	if(required <= graph->capacity){
		return 0;	// if there already is enough space then exit
	}

	// get the old capacity (base value of 16) and double it until large enough
	size_t new_capacity = graph->capacity == 0 ? 16 : graph->capacity;
	while(new_capacity < required){
		new_capacity *= 2;
	}

	// copy all of the old nodes into new_nodes
	size_t old_bytes = graph->capacity * sizeof(ASNode *);
	ASNode **new_nodes = (ASNode **)bgp_realloc(graph->nodes, old_bytes, new_capacity * sizeof(ASNode *));
	// copy new_nodes and new_capacity into the graph
	graph->nodes = new_nodes;
	graph->capacity = new_capacity;

	return 0;
}

// like all "frees" this does nothing and I might remove them (I just did by commenting out the frees at the end of simulator.c)
static void free_ranks(ASGraph *graph){
	if(!graph->rank_buckets){
		return;
	}

	for(size_t i = 0; i < graph->rank_count; ++i){
		uint32_vector_free(&graph->rank_buckets[i]);
	}

	graph->rank_buckets = NULL;
	graph->rank_count = 0;
}

// initializes a graph struct
void as_graph_init(ASGraph *graph){
	graph->nodes = NULL;
	graph->size = 0;
	graph->capacity = 0;
	uint32_map_init(&graph->index);
	graph->rank_buckets = NULL;
	graph->rank_count = 0;
}

// does nothing (I cant be bothered to go remove them from everywhere in the code, just know that these now do nothing)
void as_graph_free(ASGraph *graph){
	if(!graph){
		return;
	}

	for(size_t i = 0; i < graph->size; ++i){
		as_node_free(graph->nodes[i]);
	}

	graph->nodes = NULL;
	graph->size = 0;
	graph->capacity = 0;
	uint32_map_free(&graph->index);
	free_ranks(graph);
}

// find or creates an asn in the graph then returns it
ASNode *as_graph_get_or_create(ASGraph *graph, uint32_t asn){
	ASNode *existing = (ASNode *)uint32_map_get(&graph->index, asn);	// try to find it
	if(existing){
		return existing;	// return existing node in graph
	}

	// since we got here we have to add one
	if(ensure_node_capacity(graph, graph->size + 1) != 0){
		return NULL;
	}

	// allocate the new node
	ASNode *node = (ASNode *)bgp_alloc(sizeof(ASNode));

	// set defaults
	memset(node, 0, sizeof(ASNode));
	as_node_init(node, asn);
	graph->nodes[graph->size++] = node;
	node->graph_index = graph->size - 1;
	uint32_map_set(&graph->index, asn, node);

	// return new node in graph
	return node;
}

// finds a node in graph via hashmap lookup
ASNode *as_graph_find(ASGraph *graph, uint32_t asn){
	return (ASNode *)uint32_map_get(&graph->index, asn);
}
const ASNode *as_graph_find_const(const ASGraph *graph, uint32_t asn){	// same as last but just const-ed
	return (const ASNode *)uint32_map_get(&graph->index, asn);
}

// add 2 asns to the graph in a provider / customer relationship
void as_graph_add_provider_customer(ASGraph *graph, uint32_t provider, uint32_t customer){
	// create both nodes in the graph
	ASNode *provider_node = as_graph_get_or_create(graph, provider);
	ASNode *customer_node = as_graph_get_or_create(graph, customer);
	if(!provider_node || !customer_node){	// make sure they both exist
		return;
	}

	// add them to eachothers vectors as provider->customer and customer->provider
	uint32_vector_push_unique(&provider_node->customers, customer);
	uint32_vector_push_unique(&customer_node->providers, provider);
}

// add 2 asns to the graph in a perr relationship
void as_graph_add_peer(ASGraph *graph, uint32_t left, uint32_t right){
	// create both nodes
	ASNode *left_node = as_graph_get_or_create(graph, left);
	ASNode *right_node = as_graph_get_or_create(graph, right);
	if(!left_node || !right_node){	// make sure they exist
		return;
	}

	// add them to eachothers peers vectors
	uint32_vector_push_unique(&left_node->peers, right);
	uint32_vector_push_unique(&right_node->peers, left);
}

// adds an ASNode to a queue
static void enqueue(ASNode ***queue, size_t *tail, size_t *capacity, ASNode *node){
	if(*tail >= *capacity){	// if there is not enough space
		// calculate new space as double prev with a floor of 16
		size_t new_capacity = (*capacity == 0) ? 16 : (*capacity * 2);
		size_t old_bytes = (*capacity) * sizeof(ASNode *);

		// allocate new memory space and copy over old data with realloc
		ASNode **new_queue = (ASNode **)bgp_realloc(*queue, old_bytes, new_capacity * sizeof(ASNode *));

		// assign new values over the queue
		*queue = new_queue;
		*capacity = new_capacity;
	}

	// add the node to the tail and increment
	(*queue)[(*tail)++] = node;
}

// computes the propogation ranks of a graph
void as_graph_compute_propagation_ranks(ASGraph *graph){
	// free_ranks(graph);	// im just going to comment these out anywhere I see them from now on
	if(graph->size == 0){	// make sure there actually is a graph
		return;
	}
	
	// make a queue
	ASNode **queue = NULL;
	size_t head = 0;
	size_t tail = 0;
	size_t capacity = 0;
	
	// initialize all graph nodes to be rank 0
	// queue up every node if it has no customers
	for(size_t i = 0; i < graph->size; ++i){
		ASNode *node = graph->nodes[i];

		node->propagation_rank = 0;

		if(node->customers.size == 0){
			enqueue(&queue, &tail, &capacity, node);
		}
	}

	// find the max rank by going bottom up like from the graph
	size_t max_rank = 0;
	while(head < tail){	// for every node in the queue
		ASNode *node = queue[head++];	// get the next node
		size_t next_rank = node->propagation_rank + 1;

		// for every provider
		for(size_t i = 0; i < node->providers.size; ++i){
			// get the provider
			ASNode *provider = as_graph_find(graph, node->providers.data[i]);
			if(!provider){
				continue;
			}
			// if the child has a larger rand than the provider
			if(next_rank > provider->propagation_rank){
				provider->propagation_rank = next_rank;	// set the providers rank to the childs
				if(next_rank > max_rank){	// if this is the max assign it
					max_rank = next_rank;
				}
				// queue up the provider
				enqueue(&queue, &tail, &capacity, provider);
			}
		}
	}

	// save the max rank and make buckets
	graph->rank_count = max_rank + 1;
	graph->rank_buckets = (UInt32Vector *)bgp_alloc(graph->rank_count * sizeof(UInt32Vector));
	memset(graph->rank_buckets, 0, graph->rank_count * sizeof(UInt32Vector));

	// initialize the vector of each bucker
	for(size_t i = 0; i < graph->rank_count; ++i){
		uint32_vector_init(&graph->rank_buckets[i]);
	}
	// for every node
	for(size_t i = 0; i < graph->size; ++i){
		ASNode *node = graph->nodes[i];
		if(node->propagation_rank >= graph->rank_count){	// if propogation is >= thank the max rank
			node->propagation_rank = graph->rank_count - 1;	// set the prop rank to max rank - 1
		}

		// add the dense graph index to the appropriate rank bucket
		uint32_vector_push(&graph->rank_buckets[node->propagation_rank], (uint32_t)node->graph_index);
	}

	// cache neighbor pointers once to avoid repeated hash lookups during propagation
	for(size_t i = 0; i < graph->size; ++i){
		ASNode *node = graph->nodes[i];
		if(node->providers.size){
			node->provider_ptrs = (ASNode **)bgp_alloc(node->providers.size * sizeof(ASNode *));
			for(size_t j = 0; j < node->providers.size; ++j){
				node->provider_ptrs[j] = as_graph_find(graph, node->providers.data[j]);
			}
		}
		if(node->customers.size){
			node->customer_ptrs = (ASNode **)bgp_alloc(node->customers.size * sizeof(ASNode *));
			for(size_t j = 0; j < node->customers.size; ++j){
				node->customer_ptrs[j] = as_graph_find(graph, node->customers.data[j]);
			}
		}
		if(node->peers.size){
			node->peer_ptrs = (ASNode **)bgp_alloc(node->peers.size * sizeof(ASNode *));
			for(size_t j = 0; j < node->peers.size; ++j){
				node->peer_ptrs[j] = as_graph_find(graph, node->peers.data[j]);
			}
		}
	}
}

// returns the ranks of the graph
const UInt32Vector *as_graph_ranks(const ASGraph *graph, size_t *rank_count){
	if(rank_count){
		*rank_count = graph ? graph->rank_count : 0;
	}
	return graph ? graph->rank_buckets : NULL;
}

// returns the nodes of the graph
ASNode **as_graph_nodes(const ASGraph *graph, size_t *count){
	if(count){
		*count = graph ? graph->size : 0;
	}
	return graph ? graph->nodes : NULL;
}
