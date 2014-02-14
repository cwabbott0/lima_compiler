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

#include "priority_queue.h"
#include <stdio.h>
#include <stdlib.h>

priority_queue_t *priority_queue_create(compare_cb compare_gt)
{
	priority_queue_t *queue = malloc(sizeof(priority_queue_t));
	if (!queue)
		return NULL;
	
	queue->elems = NULL;
	queue->num_elems = 0;
	queue->compare_gt = compare_gt;
	return queue;
}

void priority_queue_delete(priority_queue_t *queue)
{
	if(queue->elems)
		free(queue->elems);
	free(queue);
}

unsigned priority_queue_num_elems(priority_queue_t *queue)
{
	return queue->num_elems;
}

static bool is_power_of_two(unsigned num)
{
	return !(num & (num - 1));
}

bool priority_queue_push(priority_queue_t *queue, void *elem)
{
	if(queue->num_elems == 0)
	{
		queue->num_elems = 1;
		queue->elems = malloc(sizeof(void*));
		if(!queue->elems)
			return false;
		queue->elems[0] = elem;
		return true;
	}
	
	if(is_power_of_two(queue->num_elems))
	{
		queue->elems = realloc(queue->elems,
							   queue->num_elems * 2 * sizeof(void*));
		if(!queue->elems)
			return false;
	}
	
	queue->elems[queue->num_elems] = elem;
	
	unsigned cur_elem = queue->num_elems;
	while(cur_elem != 0)
	{
		unsigned parent = (cur_elem - 1) / 2;
		if(!queue->compare_gt(queue->elems[cur_elem], queue->elems[parent]))
			break;
		
		void *temp = queue->elems[parent];
		queue->elems[parent] = queue->elems[cur_elem];
		queue->elems[cur_elem] = temp;
		cur_elem = parent;
	}
	
	queue->num_elems++;
	return true;
}

void *priority_queue_pull(priority_queue_t *queue)
{
	if(queue->num_elems == 0)
		return NULL;
	
	void *ret = queue->elems[0];
	
	if(queue->num_elems == 1)
	{
		free(queue->elems);
		queue->elems = NULL;
		queue->num_elems--;
		return ret;
	}
	
	queue->elems[0] = queue->elems[queue->num_elems-1];
	
	unsigned cur_elem = 0;
	while (true)
	{
		unsigned smallest = cur_elem;
		unsigned left = 2*cur_elem + 1;
		unsigned right = 2*cur_elem + 2;
		if(left < queue->num_elems - 1 &&
		   queue->compare_gt(queue->elems[left], queue->elems[smallest]))
		   smallest = left;
		if(right < queue->num_elems - 1 &&
		   queue->compare_gt(queue->elems[right], queue->elems[smallest]))
		   smallest = right;
		
		if(smallest == cur_elem)
			break;
		
		void *temp = queue->elems[smallest];
		queue->elems[smallest] = queue->elems[cur_elem];
		queue->elems[cur_elem] = temp;
		cur_elem = smallest;
	}
	
	queue->num_elems--;
	if(is_power_of_two(queue->num_elems))
	{
		queue->elems = realloc(queue->elems, queue->num_elems * sizeof(void*));
		if(!queue->elems)
			return NULL;
	}

	return ret;
}

void *priority_queue_peek(priority_queue_t* queue)
{
	if(queue->num_elems == 0)
		return NULL;
	return queue->elems[0];
}
