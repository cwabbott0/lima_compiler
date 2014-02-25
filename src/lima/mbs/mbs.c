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

#include "mbs.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
	char ident[4];
	uint32_t size;
} mbs_header_t;

mbs_chunk_t* mbs_chunk_create(char* ident)
{
	mbs_chunk_t* chunk = malloc(sizeof(mbs_chunk_t));
	if (!chunk)
		return NULL;
	
	memcpy(&chunk->ident, ident, 4);
	chunk->data = NULL;
	chunk->size = 0;
	
	return chunk;
}

void mbs_chunk_delete(mbs_chunk_t* chunk)
{
	free(chunk->data);
	free(chunk);
}

mbs_chunk_t* mbs_chunk_string(const char* string)
{
	mbs_chunk_t* chunk = mbs_chunk_create("STRI");
	if (!chunk)
		return NULL;
	
	unsigned size = strlen(string) + 1;
	//round up to nearest multiple of 4
	unsigned aligned_size = (size + 3) & ~3;
	chunk->data = calloc(aligned_size, 1);
	if (!chunk->data)
	{
		free(chunk);
		return NULL;
	}
	chunk->size = aligned_size;
	
	memcpy(chunk->data, string, size);
	
	return chunk;
}

bool mbs_chunk_append(mbs_chunk_t* chunk, mbs_chunk_t* append)
{
	chunk->data = realloc(chunk->data,
						  chunk->size + sizeof(mbs_header_t) + append->size);
	if (!chunk->data)
		return false;
	
	mbs_chunk_export(append, chunk->data + chunk->size);
	chunk->size += sizeof(mbs_header_t) + append->size;
	mbs_chunk_delete(append);
	return true;
}

bool mbs_chunk_append_data(mbs_chunk_t* chunk, void* data, unsigned size)
{
	chunk->data = realloc(chunk->data, chunk->size + size);
	
	if (!chunk->data)
		return false;
	
	memcpy(chunk->data + chunk->size, data, size);
	chunk->size += size;
	return true;
}

unsigned mbs_chunk_size(mbs_chunk_t* chunk)
{
	return chunk->size + sizeof(mbs_header_t);
}

void mbs_chunk_export(mbs_chunk_t* chunk, void* data)
{
	mbs_header_t header;
	memcpy(&header.ident, &chunk->ident, 4);
	header.size = chunk->size;
	memcpy(data, &header, sizeof(mbs_header_t));
	memcpy((char*) data + sizeof(mbs_header_t), chunk->data, chunk->size);
}
