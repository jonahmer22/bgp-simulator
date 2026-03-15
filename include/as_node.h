#ifndef AS_NODE_H
#define AS_NODE_H

#include <stddef.h>
#include <stdint.h>

#include "policy.h"
#include "vector.h"

// each ASN gets one of these in a graph
// holds all info: peers, providers, customers, policy, index, and rank
typedef struct ASNode{
	uint32_t asn;
	UInt32Vector providers;
	UInt32Vector customers;
	UInt32Vector peers;
	struct ASNode **provider_ptrs;
	struct ASNode **customer_ptrs;
	struct ASNode **peer_ptrs;
	Policy *policy;
	size_t propagation_rank;
	size_t graph_index;
} ASNode;

// create and destroy
void as_node_init(ASNode *node, uint32_t asn);
void as_node_free(ASNode *node);

#endif
