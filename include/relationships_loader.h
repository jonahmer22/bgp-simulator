#ifndef RELATIONSHIPS_LOADER_H
#define RELATIONSHIPS_LOADER_H

#include "as_graph.h"

// loads relationships from a file or directly from a string
int relationships_loader_load_file(const char *path, ASGraph *graph, char **error_message);
int relationships_loader_load_string(const char *content, ASGraph *graph, char **error_message);

#endif
