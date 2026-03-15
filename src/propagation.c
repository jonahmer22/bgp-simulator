#include "propagation.h"

#include <stdbool.h>
#include <stdlib.h>

#include "parallel.h"

// context for send_rib_entry
typedef struct SendContext{
	ASNode *neighbor;
	uint32_t sender_asn;
	RelationType relation;
} SendContext;

static void send_rib_entry(uint32_t prefix_id, void *value, void *ctx){
	(void)prefix_id;	// only needed for uint32_map_for_each compatability
	// get context and value
	SendContext *context = (SendContext *)ctx;
	Announcement *rib_entry = (Announcement *)value;
	if(!context || !context->neighbor || !context->neighbor->policy){
		return;
	}

	// create new announcement
	Announcement advertisement;
	announcement_init(&advertisement);	// initialize
	if(announcement_copy(&advertisement, rib_entry) != 0){	// copy rib_entry into advertizement
		// announcement_free(&advertisement);
		return;
	}

	// set values for advertisement
	advertisement.next_hop_asn = context->sender_asn;
	advertisement.received_relation = context->relation;
	policy_receive(context->neighbor->policy, &advertisement);
	// announcement_free(&advertisement);
}

// sends rib_entries to all neighbors using cached neighbor pointers
static void send_to_neighbors_ptrs(ASNode *sender, ASNode *const *neighbors, size_t neighbor_count, RelationType relation_type){
	if(!sender || !sender->policy || neighbor_count == 0){
		return;
	}

	const UInt32Map *rib = policy_local_rib(sender->policy);
	if(!rib || rib->size == 0){
		return;
	}

	for(size_t i = 0; i < neighbor_count; ++i){
		ASNode *neighbor = neighbors[i];
		if(!neighbor || !neighbor->policy){
			continue;
		}

		SendContext context = {.neighbor = neighbor, .sender_asn = sender->asn, .relation = relation_type};
		uint32_map_for_each(rib, send_rib_entry, &context);
	}
}

// context for the process_nodes_range function
typedef struct ProcessNodesContext{
	ASGraph *graph;
	const UInt32Vector *bucket;
} ProcessNodesContext;

// process the nodes a given range
static void process_nodes_range(size_t start, size_t end, void *ctx){
	ProcessNodesContext *context = (ProcessNodesContext *)ctx;

	// from the start to the end
	for(size_t i = start; i < end; ++i){
		// find the node in the graph
		uint32_t idx = context->bucket->data[i];
		ASNode *node = idx < context->graph->size ? context->graph->nodes[idx] : NULL;
		if(!node || !node->policy){
			continue;
		}

		// process the queue
		policy_process_queues(node->policy, node->asn);
	}
}

// process all of the nodes in a asn bucket
static void process_nodes(ASGraph *graph, const UInt32Vector *asn_bucket){
	if(!asn_bucket || asn_bucket->size == 0){
		return;	// they have to exist
	}

	// create the context and process them in parallel
	ProcessNodesContext context = {.graph = graph, .bucket = asn_bucket};
	parallel_for(asn_bucket->size, process_nodes_range, &context);
}

// context for the process_all_range function
typedef struct ProcessAllContext{
	ASGraph *graph;
	ASNode **nodes;
} ProcessAllContext;

// processes all the nodes in a range
static void process_all_range(size_t start, size_t end, void *ctx){
	ProcessAllContext *context = (ProcessAllContext *)ctx;

	// from start to end
	for(size_t i = start; i < end; ++i){
		ASNode *node = context->nodes[i];
		if(node && node->policy){
			policy_process_queues(node->policy, node->asn);
		}
	}
}

// process all nodes
static void process_all_nodes(ASGraph *graph){
	// get all of the nodes in the graph
	size_t node_count = 0;
	ASNode **nodes = as_graph_nodes(graph, &node_count);
	if(node_count == 0){
		return;	// graph cannot be empty
	}

	// create context and process them in parallel
	ProcessAllContext context = {.graph = graph, .nodes = nodes};
	parallel_for(node_count, process_all_range, &context);
}

// context for send_bucket_range function
typedef struct SendBucketContext{
	ASGraph *graph;
	const UInt32Vector *bucket;
	RelationType relation;
	bool use_providers;
} SendBucketContext;

// send to all buckets in range
static void send_bucket_range(size_t start, size_t end, void *ctx){
	SendBucketContext *context = (SendBucketContext *)ctx;

	// from start to end
	for(size_t i = start; i < end; ++i){
		// find the node in the graph
		uint32_t idx = context->bucket->data[i];
		ASNode *node = idx < context->graph->size ? context->graph->nodes[idx] : NULL;
		if(!node){
			continue;
		}

		// send to all neighbors
		if(context->use_providers){
			send_to_neighbors_ptrs(node, node->provider_ptrs, node->providers.size, context->relation);
		} else {
			send_to_neighbors_ptrs(node, node->customer_ptrs, node->customers.size, context->relation);
		}
	}
}

// send to all peers in range
static void send_peer_range(size_t start, size_t end, void *ctx){
	ProcessAllContext *context = (ProcessAllContext *)ctx;
	
	// from start to end
	for(size_t i = start; i < end; ++i){
		ASNode *node = context->nodes[i];
		if(!node){
			continue;
		}

		// send to neigbors
		send_to_neighbors_ptrs(node, node->peer_ptrs, node->peers.size, RELATION_PEER);
	}
}

// pretty self explanatory
static void propagate_upward(ASGraph *graph){
	// get ranks from the graph
	size_t rank_count = 0;
	const UInt32Vector *ranks = as_graph_ranks(graph, &rank_count);
	if(!ranks || rank_count == 0){
		return;
	}
	
	// for every index less than ranks count
	for(size_t idx = 0; idx < rank_count; ++idx){
		const UInt32Vector *bucket = &ranks[idx];
		
		SendBucketContext context = {
			.graph = graph,
			.bucket = bucket,
			.relation = RELATION_CUSTOMER,
			.use_providers = true
		};
		
		// send to all buckets in parallel
		parallel_for(bucket->size, send_bucket_range, &context);
		if(idx + 1 < rank_count){
			process_nodes(graph, &ranks[idx + 1]);
		}
	}
}

// propogate to all peers
static void propagate_peers(ASGraph *graph){
	// get all the nodes in the graph
	size_t node_count = 0;
	ASNode **nodes = as_graph_nodes(graph, &node_count);
	if(node_count == 0){
		return;
	}

	// create context and do a parallel for to send to all peers
	ProcessAllContext context = {.graph = graph, .nodes = nodes};
	parallel_for(node_count, send_peer_range, &context);
	
	// process nodes after sending
	process_all_nodes(graph);
}

// propogate downward
static void propagate_downward(ASGraph *graph){
	// get all the graph ranks
	size_t rank_count = 0;
	const UInt32Vector *ranks = as_graph_ranks(graph, &rank_count);
	if(!ranks || rank_count == 0){
		return;
	}

	// for every rank
	for(size_t idx = rank_count; idx-- > 0;){
		const UInt32Vector *bucket = &ranks[idx];

		SendBucketContext context = {
			.graph = graph,
			.bucket = bucket,
			.relation = RELATION_PROVIDER,
			.use_providers = false
		};

		// send to all buckets in parallel
		parallel_for(bucket->size, send_bucket_range, &context);
		if(idx > 0){
			process_nodes(graph, &ranks[idx - 1]);
		}
	}
}

// send in all ways, up, down, and peers
void propagation_run(ASGraph *graph){
	if(!graph){
		return;
	}

	// propogate
	propagate_upward(graph);
	propagate_peers(graph);
	propagate_downward(graph);
}
