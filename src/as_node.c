#include "as_node.h"

#include <stdlib.h>

// initialize
void as_node_init(ASNode *node, uint32_t asn){
    if(!node){
        return;
    }

    node->asn = asn;
	uint32_vector_init(&node->providers);
	uint32_vector_init(&node->customers);
	uint32_vector_init(&node->peers);
	node->provider_ptrs = NULL;
	node->customer_ptrs = NULL;
	node->peer_ptrs = NULL;
	node->policy = NULL;
	node->propagation_rank = 0;
	node->graph_index = 0;
}

// "free"
void as_node_free(ASNode *node){
    if(!node){
        return;
    }

    uint32_vector_free(&node->providers);
    uint32_vector_free(&node->customers);
    uint32_vector_free(&node->peers);
    policy_destroy(node->policy);
    node->policy = NULL;
}
