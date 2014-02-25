/* Author(s):
 *   Connor Abbott
 *
 * Copyright (c) 2014 Connor Abbott (connor@abbott.cx)
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

#ifndef __MBS_H__
#define __MBS_H__

#include <stdbool.h>

typedef struct {
	char ident[4];
	unsigned size;
	char* data;
} mbs_chunk_t;

mbs_chunk_t* mbs_chunk_create(char* ident);
void mbs_chunk_delete(mbs_chunk_t* chunk);

//creates a chunk of type STRI
mbs_chunk_t* mbs_chunk_string(const char* string);

//insert one chunk inside another - deletes the chunk being inserted
bool mbs_chunk_append(mbs_chunk_t* chunk, mbs_chunk_t* append);

//insert binary data inside a chunk
bool mbs_chunk_append_data(mbs_chunk_t* chunk, void* data, unsigned size);

//returns the total amount of bytes needed to hold the exported chunk
unsigned mbs_chunk_size(mbs_chunk_t* chunk);

//exports a chunk to an allocated piece of memory
void mbs_chunk_export(mbs_chunk_t* chunk, void* data);

#endif
