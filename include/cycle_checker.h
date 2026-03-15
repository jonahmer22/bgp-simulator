#ifndef CYCLE_CHECKER_H
#define CYCLE_CHECKER_H

#include "as_graph.h"

// basic enum to keep track of which direction to check for cycle in
typedef enum{
	CYCLE_DIRECTION_PROVIDER_TO_CUSTOMER,
	CYCLE_DIRECTION_CUSTOMER_TO_PROVIDER
} CycleDirection;

// does what it says
int cycle_checker_ensure_acyclic(const ASGraph *graph, char **error_message);

#endif
