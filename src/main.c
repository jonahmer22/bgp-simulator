#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simulator.h"

// basic struct to hold input and output file paths for the program
typedef struct Options{
	const char *relationships_path;
	const char *announcements_path;
	const char *rov_asns_path;
	const char *output_path;
} Options;

// print basic usage
static void print_usage(void){
	// basic usage list
	printf("Usage: bgp_simulator --relationships <file> --announcements <file> --rov-asns <file> [--output <file>]\n");
}

// parse the given args into an options struct
static int parse_args(int argc, char **argv, Options *options){
	// set the defaults for options
	options->relationships_path = NULL;
	options->announcements_path = NULL;
	options->rov_asns_path = NULL;
	options->output_path = "ribs.csv";	// default for output is ribs.csv (can be overwritten with --output)

	// for every parsed arg
	for(int i = 1; i < argc; ++i){
		// if there is a specified relationship
		if(strcmp(argv[i], "--relationships") == 0 && i+1 < argc){
			options->relationships_path = argv[++i];	// get the value and increment the index
		}
		// if there is a given announcements
		else if(strcmp(argv[i], "--announcements") == 0 && i+1 < argc){
			options->announcements_path = argv[++i];
		}
		// if we hit rov-asns
		else if(strcmp(argv[i], "--rov-asns") == 0 && i+1 < argc){
			options->rov_asns_path = argv[++i];
		}
		// optionally given an output file name
		else if(strcmp(argv[i], "--output") == 0 && i+1 < argc){
			options->output_path = argv[++i];
		}
		// help
		else if(strcmp(argv[i], "--help") == 0){
			print_usage();
			return 1;
		}
		else{	// someone is putting random stuff in
			fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
			return -1;	// exit with an error
		}
	}

	// make sure that we got all the needed args
	if(!options->relationships_path){
		fprintf(stderr, "--relationships is required\n");
		return -1;	// exit with an error
	}
	if(!options->announcements_path){
		fprintf(stderr, "--announcements is required\n");
		return -1;
	}
	if(!options->rov_asns_path){
		fprintf(stderr, "--rov-asns is required\n");
		return -1;
	}

	// exit with no errors
	return 0;
}

// ok
int main(int argc, char **argv){
	// parse the args
	Options options;
	int parse_result = parse_args(argc, argv, &options);
	if(parse_result > 0){	// if they used --help
		return 0;
	}
	if(parse_result < 0){	// there was an error with parsing
		print_usage();	// show usage
		return 1;
	}

	// run the simulation
	// char * for errors that the simulator might "return", I used this a lot and it's kinda nice it basically replaces catch + throw
	char *error_message = NULL;
	if(run_simulation(options.relationships_path, options.announcements_path, options.rov_asns_path, options.output_path, &error_message) != 0){
		// something went wrong
		if(error_message){	// is there an error_message
			fprintf(stderr, "Simulation failed: %s\n", error_message);
		}
		else{	// idk went wrong if we got here
			fprintf(stderr, "Simulation failed\n");
		}

		return 1;
	}

	// simulation succeeds if we got here
	printf("Simulation complete. Output written to %s\n", options.output_path);
	return 0;
}
