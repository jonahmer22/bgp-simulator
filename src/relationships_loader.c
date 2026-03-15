#include "relationships_loader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bgp_utils.h"

// parses a single line for relationships
static int parse_line(char *line, ASGraph *graph){
	if(line[0] == '#' || line[0] == '\0'){
		return 0;	// don't process comments or EOF
	}

	// split the line into its sections
	char *first = strchr(line, '|');	// first goes until '|'
	if(!first){
		return 0;
	}
	*first = '\0';
	char *second = strchr(first + 1, '|');	// second goes until '|' after the first
	if(!second){
		return 0;
	}
	*second = '\0';

	// get the strings from the file
	char *left_str = line;
	char *right_str = first + 1;
	char *rel_str = second + 1;

	// parse the strings into values
	uint32_t left = (uint32_t)strtoul(left_str, NULL, 10);
	uint32_t right = (uint32_t)strtoul(right_str, NULL, 10);
	int rel = (int)strtol(rel_str, NULL, 10);

	// add to the graph based on the relationship
	if(rel == 0){
		as_graph_add_peer(graph, left, right);
	}
	else if (rel == -1){
		as_graph_add_provider_customer(graph, left, right);
	}
	else if (rel == 1){
		as_graph_add_provider_customer(graph, right, left);
	}

	return 0;
}

// loads the relationships from a file stream
static int load_from_stream(FILE *stream, ASGraph *graph, char **error_message){
	if(!stream || !graph){
		return -1;
	}

	// create a char buffer
	char buffer[4096];
	while(fgets(buffer, sizeof(buffer), stream) != NULL){	// read the file line-by-line into that buffer
		// strip off any newline or return characters and replace with EOF
		size_t len = strlen(buffer);
		while(len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')){
			buffer[--len] = '\0';
		}

		// parse the line from
		parse_line(buffer, graph);
	}

	// if reading the file failed for some reason
	if(ferror(stream)){
		if(error_message){
			*error_message = bgp_strdup("Error reading relationships file");
		}
		return -1;
	}
	return 0;
}

// loads a file and parses it
int relationships_loader_load_file(const char *path, ASGraph *graph, char **error_message){
	if(error_message){	// clear any pre-existing error message
		*error_message = NULL;
	}

	// load the file as read only
	FILE *file = fopen(path, "r");
	if(!file){	// if loading the file failed
		if(error_message){
			char message[512];
			snprintf(message, sizeof(message), "Unable to open relationships file: %s", path);
			*error_message = bgp_strdup(message);
		}

		return -1;
	}

	// parse the file
	int result = load_from_stream(file, graph, error_message);
	fclose(file);

	// return success indicator
	return result;
}

// loads relationships directly from a string
int relationships_loader_load_string(const char *content, ASGraph *graph, char **error_message){
	if(error_message){	// clear any error message
		*error_message = NULL;
	}
	if(!content){
		return 0;
	}

	// allocate space for the string to be stored in memory
	char *copy = bgp_strdup(content);
	if(!copy){	// allocation failed
		if(error_message){
			*error_message = bgp_strdup("Out of memory while parsing relationships");
		}

		return -1;
	}

	// get the line
	char *line = strtok(copy, "\n");
	while(line){
		// strip off any return characters
		size_t len = strlen(line);
		while(len > 0 && line[len - 1] == '\r'){
			line[--len] = '\0';
		}

		// parse the line
		parse_line(line, graph);
		line = strtok(NULL, "\n");
	}

	return 0;
}
