#ifndef RIBS_WRITER_H
#define RIBS_WRITER_H

#include "as_graph.h"

// writes the outpus csv
int ribs_writer_write_csv(const ASGraph *graph, const char *output_path, char **error_message);

#endif
