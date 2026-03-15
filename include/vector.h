#ifndef VECTOR_H
#define VECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// basic vector struct, acts like a list, but backed by arena and realloc
// think of it as a c replacement for C++ vectors
typedef struct UInt32Vector{
	uint32_t *data;
	size_t size;
	size_t capacity;
} UInt32Vector;

// create and destroy
void uint32_vector_init(UInt32Vector *vec);
void uint32_vector_free(UInt32Vector *vec);

// add values to the list
int uint32_vector_push(UInt32Vector *vec, uint32_t value);
int uint32_vector_push_unique(UInt32Vector *vec, uint32_t value);
int uint32_vector_prepend(UInt32Vector *vec, uint32_t value);
// remove values
int uint32_vector_pop(UInt32Vector *vec, uint32_t *value);

// make a deep copy of a list
int uint32_vector_copy(UInt32Vector *dest, const UInt32Vector *src);

#endif
