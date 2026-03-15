#include "announcements_loader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bgp_utils.h"
#include "string_pool.h"

// initializes the seed list as null and empty
void origin_seed_list_init(OriginSeedList *list){
	list->items = NULL;
	list->size = 0;
	list->capacity = 0;
}

// "frees" the list like others doesnt actually free
void origin_seed_list_free(OriginSeedList *list){
	if(!list){
		return;
	}

	list->items = NULL;
	list->size = 0;
	list->capacity = 0;
}

// push a seed to the origin list
static int origin_seed_list_push(OriginSeedList *list, const OriginSeed *seed){
	if(list->size == list->capacity){	// if the list is full
		size_t new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;	// double size with a bottom of 16
		size_t old_bytes = list->capacity * sizeof(OriginSeed);
		OriginSeed *new_items = (OriginSeed *)bgp_realloc(list->items, old_bytes, new_capacity * sizeof(OriginSeed));	// realloc to new size
		if(!new_items){
			return -1;	// for some reason the realloc failed
		}

		// copy over
		list->items = new_items;
		list->capacity = new_capacity;
	}

	// add the new seed
	list->items[list->size] = *seed;
	list->size += 1;

	return 0;
}

// parses a bool from a "string"
static bool parse_bool(const char *value){
	if(!value){
		return false;
	}

	// skip whitespace
	while(isspace((unsigned char)*value)){
		value++;
	}
	if(*value == '\0'){
		return false;
	}

	// detect a true value: 1, true, or yes
	if(*value == '1'){
		return true;
	}
	if(strncasecmp(value, "true", 4) == 0 || strncasecmp(value, "yes", 3) == 0){
		return true;
	}

	return false;
}

// parse csv into its fields
static size_t split_csv(char *line, char **fields, size_t max_fields){
	size_t count = 0;
	char *ptr = line;

	while(*ptr && count < max_fields){	// for each field
		fields[count++] = ptr;
		while(*ptr && *ptr != ','){	// keep going until we hit a comma
			ptr++;
		}

		// replace the next char with a eof
		if(*ptr == ','){
			*ptr++ = '\0';
		}
	}

	return count;
}

// trim off any spaces from a "string" char*
static void trim(char *value){
	// trim off leading spaces
	char *start = value;
	while(*start && isspace((unsigned char)*start)){
		start++;
	}
	// trim off trailing spaces
	char *end = start + strlen(start);
	while(end > start && isspace((unsigned char)end[-1])){
		*--end = '\0';
	}

	// move over the char * to the start of the char buffer
	if(start != value){
		// optimization note, this also moves over the EOF char so then it occupies the same memory
		// technically wastefull of memory and can leave over anything that was left, but doesnt use anymore memory and quicker
		memmove(value, start, strlen(start) + 1);
	}
}

// load announcements into the seeds and graph
static int load_from_stream(FILE *stream, ASGraph *graph, OriginSeedList *seeds, char **error_message){
	char buffer[4096];
	bool header_checked = false;

	// read each line
	while(fgets(buffer, sizeof(buffer), stream)){
		size_t len = strlen(buffer);	// get the length

		// fill the end of the line with EOF 
		while(len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')){
			buffer[--len] = '\0';
		}
		if(buffer[0] == '\0'){
			continue;
		}
		if(!header_checked){	// skip the first line with the labels
			header_checked = true;
			// make sure it actually was the header
			if(strstr(buffer, "asn") && strstr(buffer, "prefix")){
				continue;
			}
		}

		// parse the fields from the line
		char *line = buffer;
		char *fields[4] = {0};
		size_t field_count = split_csv(line, fields, 4);	// get the number of fields and split
		if(field_count >= 2){	// make sure there is enough
			OriginSeed seed = {0};
			seed.origin_asn = (uint32_t)strtoul(fields[0], NULL, 10);	// cast the seed (1st column as uint32)
			// trim and set prefix_id
			trim(fields[1]);
			seed.prefix_id = string_pool_intern(fields[1]);
			if(field_count >= 3){	// get the 3rd column boolean
				trim(fields[2]);
				seed.rov_invalid = parse_bool(fields[2]);
			}

			// add to the graph and seed list
			as_graph_get_or_create(graph, seed.origin_asn);
			origin_seed_list_push(seeds, &seed);
		}
	}

	// if for some reason reading with fgets failed
	if(ferror(stream)){
		if(error_message){
			*error_message = bgp_strdup("Error reading announcements file");
		}
		return -1;
	}

	// no error
	return 0;
}

// loads announcements from a file at a given path
int announcements_loader_load_file(const char *path, ASGraph *graph, OriginSeedList *seeds, char **error_message){
	if(error_message){
		*error_message = NULL;
	}

	// open the file as read only
	FILE *file = fopen(path, "r");
	if(!file){	// make sure we actually read the file
		if(error_message){
			char message[512];
			snprintf(message, sizeof(message), "Unable to open announcements file: %s", path);
			*error_message = bgp_strdup(message);
		}

		return -1;
	}

	// run the loader from the input file
	int result = load_from_stream(file, graph, seeds, error_message);
	fclose(file);

	return result;	// indicate success (or failure)
}

// does the same as the load_file, but instead from just a raw char * and not reading the file using fgets
int announcements_loader_load_string(const char *content, ASGraph *graph, OriginSeedList *seeds, char **error_message){
	if(error_message){
		*error_message = NULL;
	}
	if(!content){
		return 0;
	}

	// make a copy of the file
	char *copy = bgp_strdup(content);
	if(!copy){
		if(error_message){
			*error_message = bgp_strdup("Out of memory while parsing announcements");
		}
		return -1;
	}

	// parse the string
	bool header_checked = false;
	char *line = strtok(copy, "\n");
	while(line){
		// strip off any returns characters or anything
		size_t len = strlen(line);
		while(len > 0 && line[len - 1] == '\r'){
			line[--len] = '\0';
		}

		// if there is a line left
		if(line[0] != '\0'){
			if(!header_checked){	// skip the header
				header_checked = true;
				// make sure it was the header we skipped and if so go onto next iter of while
				if(strstr(line, "asn") && strstr(line, "prefix")){
					line = strtok(NULL, "\n");
					continue;
				}
			}

		// seperate it into its fields
		char *fields[4] = {0};
		size_t field_count = split_csv(line, fields, 4);
			if(field_count >= 2){	// this is all the same as from load_from_stream()
				OriginSeed seed = {0};
				seed.origin_asn = (uint32_t)strtoul(fields[0], NULL, 10);

				trim(fields[1]);
				seed.prefix_id = string_pool_intern(fields[1]);
				if(field_count >= 3){
					trim(fields[2]);
					seed.rov_invalid = parse_bool(fields[2]);
				}

				as_graph_get_or_create(graph, seed.origin_asn);
				origin_seed_list_push(seeds, &seed);
			}
		}
		line = strtok(NULL, "\n");
	}
	return 0;
}
