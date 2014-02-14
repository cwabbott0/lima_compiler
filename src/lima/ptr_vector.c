/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "ptr_vector.h"
#include <stdlib.h>

#define INITIAL_CAPACITY 16

ptr_vector_t ptr_vector_create(void)
{
	ptr_vector_t ret;
	
	ret.elems = malloc(INITIAL_CAPACITY * sizeof(void*));
	ret.size = 0;
	ret.capacity = INITIAL_CAPACITY;
	
	return ret;
}

void ptr_vector_delete(ptr_vector_t vector)
{
	free(vector.elems);
}

bool ptr_vector_add(ptr_vector_t* vector, void* elem)
{
	if (vector->size == vector->capacity)
	{
		vector->capacity *= 2;
		vector->elems = realloc(vector->elems, vector->capacity * sizeof(void*));
		if (!vector->elems)
			return false;
	}
	
	vector->elems[vector->size] = elem;
	vector->size++;
	return true;
}
