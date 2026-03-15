#include "cycle_checker.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bgp_utils.h"
#include "parallel.h"

#define CYCLE_PARALLEL_THRESHOLD 2048

// struct to keep track of task metadata
typedef struct CycleTask{
	const ASGraph *graph;
	CycleDirection direction;
	int *state;
	char *message;
	int result;
} CycleTask;

static char *build_cycle_message(const UInt32Vector *stack, uint32_t start){
	// try to find start in the stack
	size_t start_index = 0;
	bool found = false;
	for(; start_index < stack->size; ++start_index){
		if(stack->data[start_index] == start){
			found = true;
			break;	// exit early
		}
	}
	if(!found){
		start_index = 0;	// if it wasnt found just start from top
	}

	// allocate the message memory
	size_t elements = stack->size - start_index + 1;
	size_t buffer_size = elements * 16;
	char *buffer = (char *)bgp_alloc(buffer_size);

	// build the message
	size_t offset = 0;
	for(size_t i = start_index; i < stack->size; ++i){	// from start to end of the stack
		// move over data to the buffer via formatted string
		int written = snprintf(buffer + offset, buffer_size - offset, "%u -> ", stack->data[i]);
		if(written < 0 || (size_t)written >= buffer_size - offset){
			return NULL;	// there was an error or oob access
		}
		offset += (size_t)written;	// shift the offset
	}
	// add the start element in
	int written = snprintf(buffer + offset, buffer_size - offset, "%u", start);
	if(written < 0 || (size_t)written >= buffer_size - offset){
		return NULL;	// there was an error
	}

	// return formatted string
	return buffer;
}

// return providers or customers based on direction
static const UInt32Vector *neighbors_for_direction(const ASNode *node, CycleDirection direction){
	if(direction == CYCLE_DIRECTION_PROVIDER_TO_CUSTOMER){
		return &node->customers;
	}
	return &node->providers;
}

// perform DFS to check for cycles
static int dfs_cycle(const ASGraph *graph, ASNode *node, CycleDirection direction, int *state, UInt32Vector *stack, char **cycle_message){
	size_t index = node->graph_index;
	state[index] = 1;	// mark the start as visited
	uint32_vector_push(stack, node->asn);	// push the starting asn onto the stack

	// get neighbors
	const UInt32Vector *neighbors = neighbors_for_direction(node, direction);
	for(size_t i = 0; i < neighbors->size; ++i){	// for every neighbor
		// get the node
		ASNode *neighbor = as_graph_find((ASGraph *)graph, neighbors->data[i]);
		if(!neighbor){
			continue;
		}

		// get it's index
		size_t neighbor_index = neighbor->graph_index;
		if(state[neighbor_index] == 0){	// if it hasn't been visited
			// run dfs from that node
			if(dfs_cycle(graph, neighbor, direction, state, stack, cycle_message) != 0){
				return -1;
			}
			// if there was a cycle message made
			if(*cycle_message){
				return 0;	// exit
			}
		}
		// if the neighbor was already visited
		else if(state[neighbor_index] == 1){
			// build the cycle message
			*cycle_message = build_cycle_message(stack, neighbor->asn);
			return 0;
		}
	}
	uint32_vector_pop(stack, NULL);
	state[index] = 2;

	return 0;
}

// executes a cycle task
static void run_cycle_task(void *arg){
	CycleTask *task = (CycleTask *)arg;
	
	// get nodes and create a stack
	size_t node_count = 0;
	ASNode **nodes = as_graph_nodes(task->graph, &node_count);
	UInt32Vector stack;
	uint32_vector_init(&stack);

	// set state memory to 0s
	memset(task->state, 0, node_count * sizeof(int));

	// for every node
	for(size_t i = 0; i < node_count; ++i){
		if(task->state[nodes[i]->graph_index] != 0){
			continue;	// skip non-zeros
		}

		char *cycle_message = NULL;

		// check for cycles using dfs from that node
		if(dfs_cycle(task->graph, nodes[i], task->direction, task->state, &stack, &cycle_message) != 0){
			task->result = -1;
			// uint32_vector_free(&stack);
			return;
		}
		// if there was a cycle (there would be a message) and return failure
		if(cycle_message){
			task->message = cycle_message;
			task->result = -1;
			// uint32_vector_free(&stack);
			return;
		}
	}

	// uint32_vector_free(&stack);
	task->result = 0;
}

static int run_cycle_direction(const ASGraph *graph, CycleDirection direction, int *state, char **message){
	// get nodes and make a stack
	size_t node_count = 0;
	ASNode **nodes = as_graph_nodes(graph, &node_count);
	UInt32Vector stack;
	uint32_vector_init(&stack);

	// set state to 0s
	memset(state, 0, node_count * sizeof(int));

	// for every node
	for(size_t i = 0; i < node_count; ++i){
		if(state[nodes[i]->graph_index] != 0){
			continue;	// skip non-zeros
		}
		// run dfs from every node
		if(dfs_cycle(graph, nodes[i], direction, state, &stack, message) != 0){
			// uint32_vector_free(&stack);
			return -1;	// return failure
		}
		// if there was a message it failed in a recursive call
		if(*message){
			// uint32_vector_free(&stack);
			return -1;	// return failure
		}
	}

	// uint32_vector_free(&stack);
	return 0;	// return norm
}

// makes sure that the entire graph is acyclic
int cycle_checker_ensure_acyclic(const ASGraph *graph, char **error_message){
	if(error_message){
		*error_message = NULL;	// clear error message
	}
	if(!graph){	 // make sure there is a graph
		return 0;
	}

	// get node count
	size_t node_count = 0;
	as_graph_nodes(graph, &node_count);
	if(node_count == 0){
		return 0;
	}

	// get memory for 2 arrays of ints for every node
	int *state_provider = (int *)bgp_alloc(node_count * sizeof(int));
	int *state_customer = (int *)bgp_alloc(node_count * sizeof(int));

	// need to search both up and down to make sure there are no cycles at all
	// providers direction search with provider states
	CycleTask provider_task = {
		.graph = graph,
		.direction = CYCLE_DIRECTION_PROVIDER_TO_CUSTOMER,
		.state = state_provider,
		.message = NULL,
		.result = 0
	};
	// customers direction search with customer states
	CycleTask customer_task = {
		.graph = graph,
		.direction = CYCLE_DIRECTION_CUSTOMER_TO_PROVIDER,
		.state = state_customer,
		.message = NULL,
		.result = 0
	};
	// if there are less nodes than the parallel threshold
	if(node_count < CYCLE_PARALLEL_THRESHOLD){
		// run cycle detection in both directions
		if(run_cycle_direction(graph, CYCLE_DIRECTION_PROVIDER_TO_CUSTOMER, state_provider, &provider_task.message) != 0){
			provider_task.result = -1;
		}
		if(!provider_task.message){	// make sure the p->c check didnt fail
			if(run_cycle_direction(graph, CYCLE_DIRECTION_CUSTOMER_TO_PROVIDER, state_customer, &customer_task.message) != 0){
				customer_task.result = -1;
			}
		}
	}
	else{	// do the things in parallel for a speedup
		parallel_run(run_cycle_task, &provider_task, run_cycle_task, &customer_task);
	}

	// check for fail states
	CycleTask *failed = NULL;
	if(provider_task.result != 0){	// anything but 0 is fail
		failed = &provider_task;
	}
	else if(customer_task.result != 0){	// same for this
		failed = &customer_task;
	}

	// if we failed and have a message 
	if(failed && failed->message){
		// add a prefix to show which direction had a cycle
		const char *prefix = (failed->direction == CYCLE_DIRECTION_PROVIDER_TO_CUSTOMER) ? "Provider cycle detected: " : "Customer cycle detected: ";
		// add the prefix to the string
		size_t prefix_len = strlen(prefix);
		size_t message_len = strlen(failed->message);
		char *full = (char *)bgp_alloc(prefix_len + message_len + 1);
		memcpy(full, prefix, prefix_len);
		memcpy(full + prefix_len, failed->message, message_len + 1);
		
		// set the error message
		if(error_message){
			*error_message = full;
		}

		return -1;
	}
	// if we still failed but didn;t have a message
	if(provider_task.result != 0 || customer_task.result != 0){
		return -1;
	}

	// normal exit
	return 0;
}
#undef CYCLE_PARALLEL_THRESHOLD
