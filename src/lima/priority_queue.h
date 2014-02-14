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

#ifndef __priority_queue_h__
#define __priority_queue_h__

#include <stdbool.h>

//Implementation of a priority queue using a binary heap

typedef bool (*compare_cb)(void* elem1, void* elem2);

typedef struct {
	void **elems;
	unsigned num_elems;
	compare_cb compare_gt;
} priority_queue_t;

priority_queue_t *priority_queue_create(compare_cb compare_gt);
void priority_queue_delete(priority_queue_t *queue);
unsigned priority_queue_num_elems(priority_queue_t *queue);
bool priority_queue_push(priority_queue_t *queue, void *elem);
void *priority_queue_pull(priority_queue_t *queue);
void *priority_queue_peek(priority_queue_t* queue);


#endif
