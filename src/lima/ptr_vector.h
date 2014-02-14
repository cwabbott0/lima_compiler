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

#ifndef __ptr_vector_h__
#define __ptr_vector_h__

#include <assert.h>
#include <stdbool.h>

typedef struct
{
	void** elems;
	unsigned size, capacity;
} ptr_vector_t;

static inline unsigned ptr_vector_size(ptr_vector_t vector)
{
	return vector.size;
}

static inline void* ptr_vector_get(ptr_vector_t vector, unsigned index)
{
	assert(index < vector.size);
	return vector.elems[index];
}

static inline void ptr_vector_set(ptr_vector_t vector, void* elem,
								  unsigned index)
{
	assert(index < vector.size);
	vector.elems[index] = elem;
}

static inline void ptr_vector_clear(ptr_vector_t* vector)
{
	vector->size = 0;
}

ptr_vector_t ptr_vector_create(void);
void ptr_vector_delete(ptr_vector_t vector);
bool ptr_vector_add(ptr_vector_t* vector, void* elem);



#endif
