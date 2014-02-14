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

#include "fixed_queue.h"
#include <stdlib.h>

fixed_queue_t fixed_queue_create(unsigned max_elems)
{
	fixed_queue_t ret;
	ret.size = max_elems + 1;
	ret.elems = calloc(ret.size, sizeof(void*));
	ret.front_index = ret.back_index = 0;
	return ret;
}

void fixed_queue_delete(fixed_queue_t queue)
{
	free(queue.elems);
}

bool fixed_queue_is_empty(fixed_queue_t queue)
{
	return queue.front_index == queue.back_index;
}

void fixed_queue_push(fixed_queue_t* queue, void* elem)
{
	queue->elems[queue->front_index] = elem;
	queue->front_index++;
	if (queue->front_index == queue->size)
		queue->front_index = 0;
}

void* fixed_queue_pop(fixed_queue_t* queue)
{
	void* ret = queue->elems[queue->back_index];
	queue->back_index++;
	if (queue->back_index == queue->size)
		queue->back_index = 0;
	return ret;
}
