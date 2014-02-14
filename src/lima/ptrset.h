/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott
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

#ifndef __ptrset_h__
#define __ptrset_h__

#include <stdbool.h>


/* Implements a set of pointers,
 * using an open-coded linear-probe hash-set
 */

typedef struct {
	void** elems;
	unsigned size, num_elems;
	unsigned total_size; //internal, includes deleted elements
} ptrset_t;

typedef struct {
	unsigned cur_elem;
	ptrset_t set;
} ptrset_iter_t;

bool ptrset_create(ptrset_t* set);
void ptrset_delete(ptrset_t set);
bool ptrset_copy(ptrset_t* dest, ptrset_t src);
bool ptrset_add(ptrset_t* set, void* ptr);
bool ptrset_contains(ptrset_t set, void* ptr);
bool ptrset_remove(ptrset_t* set, void* ptr);
bool ptrset_union(ptrset_t* dest, ptrset_t src);
void* ptrset_first(ptrset_t set);
void ptrset_empty(ptrset_t* set);
static inline unsigned ptrset_size(ptrset_t set)
{
	return set.size;
}

/* 
 * Note: this iterator implementation *is* safe for deletion of the current
 * element.
 */

ptrset_iter_t ptrset_iter_create(ptrset_t set);
bool ptrset_iter_next(ptrset_iter_t* iter, void** ptr);

#define ptrset_iter_for_each(iter, elem) \
	while (ptrset_iter_next(&iter, (void**)&elem))

#endif
