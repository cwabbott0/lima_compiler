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

#include "ptrset.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#define INITIAL_NUM_ELEMS 16
#define MAX_LOAD 0.7f

#define EMPTY_ELEM NULL
#define DELETED_ELEM ((void*)1)

static unsigned get_hash(void* ptr, unsigned num_elems)
{
	unsigned long num = (unsigned long) ptr;
	return (num >> 4) % num_elems;
}
bool ptrset_create(ptrset_t* set)
{	
	set->elems = calloc(INITIAL_NUM_ELEMS, sizeof(void*));
	if (!set->elems)
		return false;
	
	set->size = 0;
	set->total_size = 0;
	set->num_elems = INITIAL_NUM_ELEMS;
	return true;
}

void ptrset_delete(ptrset_t set)
{
	free(set.elems);
}

bool ptrset_copy(ptrset_t* dest, ptrset_t src)
{
	dest->elems = calloc(src.num_elems, sizeof(void*));
	if (!dest->elems)
		return false;
	
	dest->num_elems = src.num_elems;
	dest->size = src.size;
	dest->total_size = src.total_size;
	
	memcpy(dest->elems, src.elems, src.num_elems * sizeof(void*));
	
	return true;
}

static void elems_add(void** elems, unsigned num_elems, void* ptr)
{
	unsigned hash = get_hash(ptr, num_elems);
	while (elems[hash] != EMPTY_ELEM)
	{
		hash = (hash + 1) % num_elems;
	}
	
	elems[hash] = ptr;
}

static bool ptrset_expand(ptrset_t* set, unsigned new_num_elems)
{
	void** new_elems = calloc(new_num_elems, sizeof(void*));
	if (!new_elems)
		return false;
	
	unsigned i;
	for (i = 0; i < set->num_elems; i++)
	{
		if (set->elems[i] == EMPTY_ELEM || set->elems[i] == DELETED_ELEM)
			continue;
		
		elems_add(new_elems, new_num_elems, set->elems[i]);
	}
	
	free(set->elems);
	set->num_elems = new_num_elems;
	set->elems = new_elems;
	set->total_size = set->size;
	return true;
}

bool ptrset_add(ptrset_t* set, void* ptr)
{
	if ((float)set->total_size / set->num_elems > MAX_LOAD)
		if (!ptrset_expand(set, set->num_elems * 2))
			return false;
	
	unsigned hash = get_hash(ptr, set->num_elems);
	unsigned insert_point;
	bool inserted = false;
	while (set->elems[hash] != EMPTY_ELEM)
	{
		if (set->elems[hash] == ptr)
			return true;
		
		if (set->elems[hash] == DELETED_ELEM && !inserted)
		{
			insert_point = hash;
			inserted = true;
		}
		
		hash = (hash + 1) % set->num_elems;
	}
	
	if (!inserted)
	{
		set->total_size++;
		insert_point = hash;
	}
	
	set->elems[insert_point] = ptr;
	set->size++;
	
	assert(set->size <= set->total_size);
	
	return true;
}

bool ptrset_contains(ptrset_t set, void* ptr)
{
	unsigned hash = get_hash(ptr, set.num_elems);
	while (set.elems[hash] != EMPTY_ELEM)
	{
		if (set.elems[hash] == ptr)
			return true;
		
		hash = (hash + 1) % set.num_elems;
	}
	
	return false;
}

bool ptrset_remove(ptrset_t* set, void* ptr)
{
	unsigned hash = get_hash(ptr, set->num_elems);
	while (set->elems[hash] != EMPTY_ELEM)
	{
		if (set->elems[hash] == ptr)
		{
			set->elems[hash] = DELETED_ELEM;
			set->size--;
			return true;
		}
		
		hash = (hash + 1) % set->num_elems;
	}
	
	return false;
}

bool ptrset_union(ptrset_t* dest, ptrset_t src)
{
	unsigned i;
	for (i = 0; i < src.num_elems; i++)
	{
		if (src.elems[i] == EMPTY_ELEM || src.elems[i] == DELETED_ELEM)
			continue;
		
		if (!ptrset_add(dest, src.elems[i]))
			return false;
	}
	
	return true;
}

void* ptrset_first(ptrset_t set)
{
	unsigned i;
	for (i = 0; i < set.num_elems; i++)
	{
		if (set.elems[i] == EMPTY_ELEM || set.elems[i] == DELETED_ELEM)
			continue;
		
		return set.elems[i];
	}
	
	return NULL;
}

void ptrset_empty(ptrset_t* set)
{
	memset(set->elems, 0, set->num_elems * sizeof(void*));
	set->size = 0;
}

static void update_iter_initial(ptrset_iter_t* iter)
{
	unsigned i;
	for (i = 0; i < iter->set.num_elems; i++)
	{
		if (iter->set.elems[i] != EMPTY_ELEM &&
			iter->set.elems[i] != DELETED_ELEM)
		{
			iter->cur_elem = i;
			return;
		}
	}
	
	iter->cur_elem = iter->set.num_elems;
	return;
}

ptrset_iter_t ptrset_iter_create(ptrset_t set)
{
	ptrset_iter_t iter;
	iter.cur_elem = 0;
	iter.set = set;
	update_iter_initial(&iter);
	return iter;
}

static void update_iter(ptrset_iter_t* iter)
{
	assert(iter->cur_elem != iter->set.num_elems);
	
	unsigned i;
	for (i = iter->cur_elem + 1; i < iter->set.num_elems; i++)
	{
		if (iter->set.elems[i] != EMPTY_ELEM &&
			iter->set.elems[i] != DELETED_ELEM)
		{
			iter->cur_elem = i;
			return;
		}
	}
	iter->cur_elem = iter->set.num_elems;
}

bool ptrset_iter_next(ptrset_iter_t* iter, void** ptr)
{
	if (iter->cur_elem == iter->set.num_elems)
		return false;

	*ptr = iter->set.elems[iter->cur_elem];
	update_iter(iter);
	return true;
}
