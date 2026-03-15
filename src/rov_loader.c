#include "rov_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bgp_utils.h"

// initialize a ROVSet
void rov_set_init(ROVSet *set){
	if(!set){
		return;
	}

	uint32_map_init(&set->entries);
}

// does nothing
void rov_set_free(ROVSet *set){
	if(!set){
		return;
	}

	uint32_map_free(&set->entries);
}

// insert a asn into the ROVSet
int rov_set_insert(ROVSet *set, uint32_t asn){
	if(!set){
		return -1;
	}

	return uint32_map_set(&set->entries, asn, (void *)1);
}

// says whether or not the rov_set contains a asn
bool rov_set_contains(const ROVSet *set, uint32_t asn){
	if(!set){
		return false;
	}

	return uint32_map_get(&set->entries, asn) != NULL;
}

// trims a line of all spaces, newlines, returns, or tabs
static void trim_line(char *line){
	// strip the line of all newline from the back
	size_t len = strlen(line);
	while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')){
		line[--len] = '\0';
	}

	// trim all the starting whitespace
	char *start = line;
	while(*start == ' ' || *start == '\t'){
		start++;
	}

	// copy over the memory if needed (only if leading trim)
	if(start != line){
		memmove(line, start, strlen(start) + 1);
	}
}

// loads rovs from a filestream
static int load_from_stream(FILE *stream, ROVSet *set, char **error_message){
	char buffer[512];	// basic buffer
	while(fgets(buffer, sizeof(buffer), stream)){	// get all the lines in the file
		trim_line(buffer);	// trim the line
		if(buffer[0] == '\0' || buffer[0] == '#'){
			continue;	// skip comments and EOF
		}

		// get the asn from the string
		uint32_t asn = (uint32_t)strtoul(buffer, NULL, 10);
		rov_set_insert(set, asn);	// insert the asn into the rov_set
	}

	// if there was an error reading the file
	if(ferror(stream)){
		if(error_message){
			*error_message = bgp_strdup("Error reading ROV ASN file");
		}

		return -1;
	}

	return 0;
}

// loads rovs from file path
int rov_loader_load_file(const char *path, ROVSet *set, char **error_message){
	if(error_message){	// clear any error message
		*error_message = NULL;
	}

	// open the file as read only
	FILE *file = fopen(path, "r");
	if(!file){	// if there was an error opening the file
		if(error_message){
			char message[512];
			snprintf(message, sizeof(message), "Unable to open ROV ASN list: %s", path);
			*error_message = bgp_strdup(message);
		}

		return -1;
	}

	// load the rovs
	int result = load_from_stream(file, set, error_message);
	fclose(file);	// close the file

	return result;
}

// load rovs directly from a string
int rov_loader_load_string(const char *content, ROVSet *set, char **error_message){
	if(error_message){	// clear any error message
		*error_message = NULL;
	}
	if(!content){
		return 0;
	}

	// copy the string
	char *copy = bgp_strdup(content);
	if(!copy){
		if(error_message){
			*error_message = bgp_strdup("Out of memory while parsing ROV ASNs");
		}

		return -1;
	}

	// get the line until any new lines
	char *line = strtok(copy, "\n");
	while(line){
		trim_line(line);	// trim the line
		if(line[0] != '\0' && line[0] != '#'){	// skip comment and eofs
			// parse the asn and add it to the set
			uint32_t asn = (uint32_t)strtoul(line, NULL, 10);
			rov_set_insert(set, asn);
		}

		line = strtok(NULL, "\n");
	}

	return 0;
}
