#include "vector.h"

#include <stdlib.h>
#include <string.h>

#include "bgp_utils.h"

// initializes a vector
void uint32_vector_init(UInt32Vector *vec){
	if(!vec){
		return;
	}

	vec->data = NULL;
	vec->size = 0;
	vec->capacity = 0;
}

// does nothing
void uint32_vector_free(UInt32Vector *vec){
	if(!vec){
		return;
	}

	vec->data = NULL;
	vec->size = 0;
	vec->capacity = 0;
}

// makes sure that a vector has a required capacity
static int ensure_capacity(UInt32Vector *vec, size_t required){
	if(required <= vec->capacity){
		return 0;	// already have enough space
	}

	// calculate needed spaace with a minimum of 4
	size_t new_capacity = vec->capacity == 0 ? 4 : vec->capacity;
	while(new_capacity < required){
		new_capacity *= 2;
	}

	// reallocate new data
	size_t old_size = vec->capacity * sizeof(uint32_t);
	uint32_t *new_data = (uint32_t *)bgp_realloc(vec->data, old_size, new_capacity * sizeof(uint32_t));
	if(!new_data){
		return -1;
	}

	// write over old vector data
	vec->data = new_data;
	vec->capacity = new_capacity;

	return 0;
}

// push a value to the end of a vector 
int uint32_vector_push(UInt32Vector *vec, uint32_t value){
	if(!vec){
		return -1;
	}

	// make sure theres enough space
	if(ensure_capacity(vec, vec->size + 1) != 0){
		return -1;
	}

	// add the value to the end of the vector
	vec->data[vec->size++] = value;

	return 0;
}

// push a value to a vector if it doesnt already exist
int uint32_vector_push_unique(UInt32Vector *vec, uint32_t value){
	if(!vec){
		return -1;
	}

	// check if the value already exists
	for(size_t i = 0; i < vec->size; ++i){
		if(vec->data[i] == value){
			return 0;	// exit early if it does
		}
	}

	// push to the end if it doesnt
	return uint32_vector_push(vec, value);
}

// prepend a value to a vector
int uint32_vector_prepend(UInt32Vector *vec, uint32_t value){
	if(!vec){
		return -1;
	}

	// if the vector already has values and the value is already at the start
	if(vec->size > 0 && vec->data[0] == value){
		return 0;	// just exit
	}
	// make sure the vector has enough space
	if(ensure_capacity(vec, vec->size + 1) != 0){
		return -1;
	}
	// move everything in the vector over by one
	memmove(vec->data + 1, vec->data, vec->size * sizeof(uint32_t));

	vec->data[0] = value;
	vec->size += 1;

	return 0;
}

// make a deep copy of a vector
int uint32_vector_copy(UInt32Vector *dest, const UInt32Vector *src){
	if(!dest || !src){
		return -1;
	}
	// make sure the dest has enough space
	if(ensure_capacity(dest, src->size) != 0){
		return -1;
	}

	// move over all the values
	dest->size = src->size;
	memcpy(dest->data, src->data, src->size * sizeof(uint32_t));

	return 0;
}

// pop a value off of the vector
int uint32_vector_pop(UInt32Vector *vec, uint32_t *value){
	if(!vec || vec->size == 0){
		return -1;
	}

	// decrement size and get the value at end
	vec->size -= 1;
	if(value){
		*value = vec->data[vec->size];
	}

	return 0;
}
