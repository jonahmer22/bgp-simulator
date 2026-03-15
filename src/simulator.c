#include "simulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "announcements_loader.h"
#include "as_graph.h"
#include "bgp_utils.h"
#include "cycle_checker.h"
#include "parallel.h"
#include "propagation.h"
#include "relationships_loader.h"
#include "ribs_writer.h"
#include "rov_loader.h"
#include "string_pool.h"

// note: sorry if the function comments seem useless, but they seem kinda self explanatory to me

// add polict and remove any existing
static void add_policy_to_node(ASNode *node, bool use_rov){
	if(!node){	// node has to exist
		return;
	}

	// destroy old policy
	if(node->policy){
		policy_destroy(node->policy);
	}

	// add new one
	node->policy = policy_create(use_rov);
}

// insert a callback into graph
static void rov_insert_callback(uint32_t key, void *value, void *ctx){
	(void)value;
	ASGraph *graph = (ASGraph *)ctx;
	as_graph_get_or_create(graph, key);
}

// holds nodes and a rovset (and an int for error)
typedef struct AssignPoliciesContext{
	ASNode **nodes;
	size_t count;
	const ROVSet *rov_set;
	int error;
} AssignPoliciesContext;

static void assign_policies_range(size_t start, size_t end, void *ctx){
	AssignPoliciesContext *context = (AssignPoliciesContext *)ctx;

	// for every node in our range
	for(size_t i = start; i < end; ++i){
		ASNode *node = context->nodes[i];	// capture a pointer to the node
		bool use_rov = context->rov_set && rov_set_contains(context->rov_set, node->asn);	// bool for if rov_set exists and the asn is in the rov_set
		add_policy_to_node(node, use_rov);	// add the policy
		// something went wrong
		if(!node->policy){
			context->error = 1;
		}
	}
}

// assign given policies to the graph
static int assign_policies(ASGraph *graph, const ROVSet *rov_set){
	// get a list of ASNode *
	size_t node_count = 0;
	ASNode **nodes = as_graph_nodes(graph, &node_count);

	// assign the propper policies to all nodes
	AssignPoliciesContext context = {.nodes = nodes, .count = node_count, .rov_set = rov_set, .error = 0};
	parallel_for(node_count, assign_policies_range, &context);	// speedup note, this is easily parallelizable

	return context.error ? -1 : 0;	// error.
}

// holds the graph, a seed list, and error int
typedef struct InstallOriginsContext{
	ASGraph *graph;
	const OriginSeedList *seeds;
	int error;
} InstallOriginsContext;

// installs announcements on a range
static void install_range(size_t start, size_t end, void *ctx){
	InstallOriginsContext *context = (InstallOriginsContext *)ctx;

	// for every seed in our range
	for(size_t i = start; i < end; ++i){
		const OriginSeed *seed = &context->seeds->items[i];	// get the seed
		ASNode *node = as_graph_find(context->graph, seed->origin_asn);	// find the relevant node
		if(!node || !node->policy){	// make sure the node exists
			context->error = 1;
			continue;
		}

		// prep the announcement
		Announcement announcement;
		announcement_init(&announcement);
		announcement_set_prefix_id(&announcement, seed->prefix_id);
		announcement_push_asn(&announcement, seed->origin_asn);
		announcement.next_hop_asn = seed->origin_asn;
		announcement.received_relation = RELATION_ORIGIN;
		announcement.rov_invalid = seed->rov_invalid;

		// set it in place
		policy_add_origin(node->policy, &announcement, seed->origin_asn);

		// "free" it, note here, all of these free functions do nothing, they used to but I switched everything to the arena or stack so there is no freeing actually going on whenever you see these, just nulling values
		// announcement_free(&announcement);	// on an optimization note I should probably get rid of all of these
	}
}

static int install_origin_announcements(ASGraph *graph, const OriginSeedList *seeds, const ROVSet *rov_set){
	(void)rov_set;
	InstallOriginsContext context = {.graph = graph, .seeds = seeds, .error = 0};

	// install all the seeds in parallel
	parallel_for(seeds->size, install_range, &context);

	return context.error ? -1 : 0;
}

// pretty much does what it says on the tin
int run_simulation(const char *relationships_path, const char *announcements_path, const char *rov_asns_path, const char *output_path, char **error_message){
	// start up everything as a clean slate
	if(error_message){	// reset any error messages
		*error_message = NULL;
	}
	bgp_arena_reset();	// reset the arenas (im using mine from in /deps/arena/ but I wrapped it to play really well as a per-thread kinda thing)
	string_pool_reset();	// reset the string pool
	ASGraph graph;	// make a new graph on the stack
	as_graph_init(&graph);	// initialize it
	OriginSeedList seeds;	// make new seeds on the stack
	origin_seed_list_init(&seeds);	// initilize the seeds
	ROVSet rov_set;	// make a new set of rovs on the set
	rov_set_init(&rov_set);	// initialize the rov set
	char *local_error = NULL;	// create a new char * for any errors that happen

	// ====================
	// Initialize the graph
	// ====================

	// make sure all the options exist in case for some reason havent already exited
	if(!relationships_path || !announcements_path || !output_path){
		if(error_message){
			*error_message = bgp_strdup("Missing required input paths");
		}
		goto failure;	// hear me out, I know this is "bad" but it's quicker than makign a failure function to do the same thing or pasting it everywhere
	}

	// load all the relationships into the graph
	if(relationships_loader_load_file(relationships_path, &graph, &local_error) != 0){
		// we got here from some error
		if(error_message){
			*error_message = local_error;	// set the error message to what we recieved
		}
		goto failure;
	}

	// if we hace rov-asns
	if(rov_asns_path && rov_asns_path[0] != '\0'){
		// load into a set
		if(rov_loader_load_file(rov_asns_path, &rov_set, &local_error) != 0){
			if(error_message){
				*error_message = local_error;	// set error
			}
			goto failure;
		}

		// make hashmap entry for ever rov in the set
		uint32_map_for_each(&rov_set.entries, rov_insert_callback, &graph);
	}

	// load announcements into seeds and graph
	if(announcements_loader_load_file(announcements_path, &graph, &seeds, &local_error) != 0){
		if(error_message){
			*error_message = local_error;
		}
		goto failure;
	}

	// set graph policies based on rov_set
	if(assign_policies(&graph, &rov_set) != 0){
		if(error_message){
			*error_message = bgp_strdup("Failed to assign policies");
		}
		goto failure;
	}

	// set up the starting announcements
	if(install_origin_announcements(&graph, &seeds, &rov_set) != 0){
		if(error_message){
			*error_message = bgp_strdup("Failed to seed announcements");
		}
		goto failure;
	}

	// ===========
	// Run the sim
	// ===========

	as_graph_compute_propagation_ranks(&graph);
	propagation_run(&graph);

	// ==============
	// output and end
	// ==============

	// writes the output into csv with asn, prefix, as_path
	if(ribs_writer_write_csv(&graph, output_path, &local_error) != 0){
		if(error_message){
			*error_message = local_error;
		}
		goto failure;
	}

	// Note: since "free functions" do nothing after switching over to arena and letting all memory leak
	// by commenting them out it gets rid of nulling overhead

	// cleanup (just nulling values)
	// origin_seed_list_free(&seeds);
	// rov_set_free(&rov_set);
	// as_graph_free(&graph);
	return 0;

failure:
	// free all objects
	// origin_seed_list_free(&seeds);
	// rov_set_free(&rov_set);
	// as_graph_free(&graph);
	return -1;	// return error
}
