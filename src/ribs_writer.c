#include "ribs_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bgp_utils.h"
#include "parallel.h"
#include "string_pool.h"

// contains all the data for the 3 columns of the output ribs.csv file
typedef struct RibRow{
	uint32_t asn;
	uint32_t prefix_id;
	const char *prefix_str;
	char *path;
} RibRow;

// basically a list of rows
typedef struct RibTable{
	RibRow *rows;
	size_t size;
	size_t capacity;
} RibTable;

// initializes a rib table
static void rib_table_init(RibTable *table){
	table->rows = NULL;
	table->size = 0;
	table->capacity = 0;
}

// pushes a row to a rib table; takes ownership of path
static int rib_table_push(RibTable *table, uint32_t asn, uint32_t prefix_id, char *path){
	if(table->size == table->capacity){	// if the table is at max capacity
		// get the new capacity with a floor of 32
		size_t new_capacity = table->capacity == 0 ? 32 : table->capacity * 2;
		size_t old_bytes = table->capacity * sizeof(RibRow);

		// realloc enough memory for new_capacity
		RibRow *new_rows = (RibRow *)bgp_realloc(table->rows, old_bytes, new_capacity * sizeof(RibRow));

		// assign new values to table
		table->rows = new_rows;
		table->capacity = new_capacity;
	}

	// add the row and increment size
	RibRow *row = &table->rows[table->size++];
	// set the values of the row
	row->asn = asn;
	row->prefix_id = prefix_id;
	row->prefix_str = string_pool_get(prefix_id);
	row->path = path;

	return 0;
}

// context for the collect_rib_entry function
typedef struct CollectContext{
	RibTable *table;
	uint32_t asn;
} CollectContext;

// collects a rib entry besed off of an announcement
static void collect_rib_entry(uint32_t prefix_id, void *value, void *ctx){
	// get context
	CollectContext *context = (CollectContext *)ctx;
	Announcement *announcement = (Announcement *)value;

	// get the path of the announcement
	char *path_str = announcement_as_path_string(announcement);
	if(!path_str || !context || !context->table){
		return;
	}

	// push the value to the table
	rib_table_push(context->table, context->asn, prefix_id, path_str);
}

static int rib_row_compare(const RibRow *lhs, const RibRow *rhs){
	// compare left hand and right hand side
	if(lhs->asn < rhs->asn){
		return -1;
	}
	if(lhs->asn > rhs->asn){
		return 1;
	}

	// allocate strings for lhs and rhs prefixes
	const char *lhs_prefix = lhs->prefix_str;
	const char *rhs_prefix = rhs->prefix_str;

	return strcmp(lhs_prefix, rhs_prefix);
}

static int rib_row_compare_qsort(const void *lhs_ptr, const void *rhs_ptr){
	return rib_row_compare((const RibRow *)lhs_ptr, (const RibRow *)rhs_ptr);
}

// task info for collect_nodes_range function
typedef struct CollectTask{
	ASNode **nodes;
	size_t start;
	size_t end;
	RibTable table;
} CollectTask;

// collects rib entries for all nodes in a range
static void collect_nodes_range(CollectTask *task){
	rib_table_init(&task->table);

	// from start to end
	for(size_t i = task->start; i < task->end; ++i){
		// get the node for each iteration
		ASNode *node = task->nodes[i];
		if(!node || !node->policy){
			continue;
		}

		// get the rib from each node
		const UInt32Map *rib = policy_local_rib(node->policy);
		if(!rib){
			continue;
		}

		// make context and execute function on all entries in the rib map
		CollectContext context = {.table = &task->table, .asn = node->asn};
		uint32_map_for_each(rib, collect_rib_entry, &context);
	}
}

// just a wrapper for the collect_nodes_range function
static void collect_task_runner(void *arg){
	collect_nodes_range((CollectTask *)arg);
}

// merges 2 tables together
static void merge_tables(RibTable *dest, RibTable *a, RibTable *b){
	size_t total = a->size + b->size;

	// allocate the size of rows needed
	dest->rows = (RibRow *)bgp_alloc(total * sizeof(RibRow));
	dest->capacity = total;
	dest->size = total;

	// iterate over tables and add rows in order
	size_t i = 0, j = 0, k = 0;
	while(i < a->size && j < b->size){
		RibRow *target = rib_row_compare(&a->rows[i], &b->rows[j]) <= 0 ? &a->rows[i++] : &b->rows[j++];
		dest->rows[k++] = *target;
	}
	while(i < a->size){
		dest->rows[k++] = a->rows[i++];
	}
	while(j < b->size){
		dest->rows[k++] = b->rows[j++];
	}
}

// writes the output to a csv file based off of a given graph
int ribs_writer_write_csv(const ASGraph *graph, const char *output_path, char **error_message){
	if(error_message){	// reset any error message that might be present
		*error_message = NULL;
	}
	if(!graph || !output_path){
		if(error_message){
			*error_message = bgp_strdup("Invalid graph or output path");
		}	// things have to exist to do something
		return -1;
	}

	// get all the ndoes in the graph
	size_t node_count = 0;
	ASNode **nodes = as_graph_nodes(graph, &node_count);

	// create tasks and final table
	CollectTask task_a = {.nodes = nodes, .start = 0, .end = node_count};
	CollectTask task_b = {.nodes = nodes, .start = node_count, .end = node_count};
	
	// initializes the final table
	RibTable final_table;
	rib_table_init(&final_table);

	// if its large enough to be parallelized
	if(node_count >= 1024){
		// find the way to split them
		task_a.end = node_count / 2;
		task_b.start = task_a.end;

		// run the tasks in parallel
		parallel_run(collect_task_runner, &task_a, collect_task_runner, &task_b);
		if(task_a.table.size > 1){
			qsort(task_a.table.rows, task_a.table.size, sizeof(RibRow), rib_row_compare_qsort);
		}
		if(task_b.table.size > 1){
			qsort(task_b.table.rows, task_b.table.size, sizeof(RibRow), rib_row_compare_qsort);
		}

		// merge the final tables into the final one
		merge_tables(&final_table, &task_a.table, &task_b.table);
	}
	else{	// do it single threaded if not
		collect_nodes_range(&task_a);
		if(task_a.table.size > 1){
			qsort(task_a.table.rows, task_a.table.size, sizeof(RibRow), rib_row_compare_qsort);
		}
		final_table = task_a.table;
	}

	// open the output file as write only
	FILE *output = fopen(output_path, "w");
	if(!output){	// if for some reason we cant open it
		if(error_message){
			char message[512];
			snprintf(message, sizeof(message), "Unable to open output file: %s", output_path);
			*error_message = bgp_strdup(message);
		}

		return -1;
	}

	// add the top header to it
	fprintf(output, "asn,prefix,as_path\n");
	for(size_t i = 0; i < final_table.size; ++i){	// add every row to the output table
		fprintf(output, "%u,%s,\"%s\"\n", final_table.rows[i].asn, string_pool_get(final_table.rows[i].prefix_id), final_table.rows[i].path);
	}
	fclose(output);	// close the file

	return 0;
}
