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

#include "pp_hir.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>



lima_pp_hir_prog_t* lima_pp_hir_prog_create(void)
{
	lima_pp_hir_prog_t* prog
		= (lima_pp_hir_prog_t*)malloc(sizeof(lima_pp_hir_prog_t));
	if (!prog) return NULL;

	list_init(&prog->block_list);
	prog->num_blocks = 0;
	prog->reg_alloc = 0;
	prog->num_arrays = 0;
	prog->arrays = NULL;
	return prog;
}

void lima_pp_hir_prog_delete(lima_pp_hir_prog_t* prog)
{
	if (!prog)
		return;
	while (prog->num_blocks > 0)
	{
		lima_pp_hir_block_t* block = pp_hir_first_block(prog);
		lima_pp_hir_prog_remove(block);
	}
	
	if (prog->arrays)
		free(prog->arrays);
	
	free(prog);
}

void lima_pp_hir_prog_insert_start(lima_pp_hir_block_t* block,
								   lima_pp_hir_prog_t* prog)
{
	list_add(&block->block_list,  &prog->block_list);
	block->prog = prog;
	prog->num_blocks++;
}

void lima_pp_hir_prog_insert_end(lima_pp_hir_block_t* block,
								 lima_pp_hir_prog_t* prog)
{
	list_add(&block->block_list, prog->block_list.prev);
	block->prog = prog;
	prog->num_blocks++;
}

void lima_pp_hir_prog_insert(lima_pp_hir_block_t* block,
							 lima_pp_hir_block_t* before)
{
	list_add(&block->block_list, &before->block_list);
	block->prog = before->prog;
	block->prog->num_blocks++;
}

void lima_pp_hir_prog_remove(lima_pp_hir_block_t* block)
{
	list_del(&block->block_list);
	block->prog->num_blocks--;
	lima_pp_hir_block_delete(block);
}

void lima_pp_hir_prog_replace(lima_pp_hir_block_t* old_block,
							  lima_pp_hir_block_t* new_block)
{
	__list_add(&new_block->block_list, old_block->block_list.prev,
			   old_block->block_list.next);
	new_block->prog = old_block->prog;
	lima_pp_hir_block_delete(old_block);
}

bool lima_pp_hir_prog_add_predecessors(lima_pp_hir_prog_t* prog)
{
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		if (block->is_end)
			continue;
		block->next[0]->num_preds++;
		if (block->branch_cond != lima_pp_hir_branch_cond_always)
			block->next[1]->num_preds++;
	}
	pp_hir_prog_for_each_block(prog, block)
	{
		block->preds = 
			malloc(block->num_preds * sizeof(lima_pp_hir_block_t*));
		if (!block->preds)
			return false;
	}
	pp_hir_prog_for_each_block(prog, block)
		block->num_preds = 0;
	pp_hir_prog_for_each_block(prog, block)
	{
		if (block->is_end)
			continue;
		block->next[0]->preds[block->next[0]->num_preds++] = block;
		if (block->branch_cond != lima_pp_hir_branch_cond_always)
			block->next[1]->preds[block->next[1]->num_preds++] = block;
	}
	
	return true;
}

bool lima_pp_hir_prog_add_array(lima_pp_hir_prog_t* prog,
								lima_pp_hir_temp_array_t array)
{
	if (prog->num_arrays == 0)
	{
		prog->arrays = malloc(sizeof(lima_pp_hir_temp_array_t));
		if (!prog->arrays)
			return false;
		
		prog->arrays[0] = array;
		prog->num_arrays = 1;
		return true;
	}
	
	lima_pp_hir_temp_array_t* narrays =
		realloc(prog->arrays,
				sizeof(lima_pp_hir_temp_array_t)*(prog->num_arrays + 1));
	
	if (!narrays)
		return false;
	
	prog->arrays = narrays;
	prog->arrays[prog->num_arrays] = array;
	prog->num_arrays++;
	return true;
}

bool lima_pp_hir_prog_remove_array(lima_pp_hir_prog_t* prog, unsigned index)
{
	unsigned i;
	
	if (prog->num_arrays == 1)
	{
		free(prog->arrays);
		prog->arrays = NULL;
		prog->num_arrays = 0;
		return true;
	}
	
	for (i = index; i < prog->num_arrays - 1; i++)
		prog->arrays[i] = prog->arrays[i + 1];
	
	lima_pp_hir_temp_array_t* narrays =
		realloc(prog->arrays,
				sizeof(lima_pp_hir_temp_array_t)*(prog->num_arrays - 1));
	
	if (!narrays)
		return false;
	
	prog->arrays = narrays;
	prog->num_arrays--;
	return true;
}


typedef struct
__attribute__((__packed__))
{
	char     ident[4]; /* ="LIR\0" */
	uint32_t version;  /* =3       */
	uint32_t num_blocks;
	uint32_t num_arrays;
} _lima_pp_hir_file_header_t;

typedef struct
{
	uint32_t start, end, alignment;
} _array_data_t;

lima_pp_hir_prog_t* lima_pp_hir_prog_import(void* data, unsigned size)
{
	unsigned pos = 0;
	
	if (size < sizeof(_lima_pp_hir_file_header_t))
		return NULL;
	
	lima_pp_hir_prog_t* prog
		= lima_pp_hir_prog_create();
	if (!prog) return NULL;

	_lima_pp_hir_file_header_t header = *((_lima_pp_hir_file_header_t*)data);
	data = (char*)data + sizeof(header);
	pos += sizeof(header);
	
	
	if (memcmp(&header.ident, "LIR\0", 4) != 0)
	{
		lima_pp_hir_prog_delete(prog);
		fprintf(stderr, "Error: Failed to import program, incorrect ident.\n");
		return NULL;
	}
	if (header.version != 3)
	{
		lima_pp_hir_prog_delete(prog);
		fprintf(stderr, "Error: Failed to import program, unsupported version.\n");
		return NULL;
	}

	uint32_t i;
	lima_pp_hir_block_t** blocks = malloc(sizeof(lima_pp_hir_block_t*)
										  * header.num_blocks);
	if (!blocks)
	{
		lima_pp_hir_prog_delete(prog);
		return NULL;
	}
	
	for (i = 0; i < header.num_blocks; i++)
	{
		unsigned block_pos;
		blocks[i]
			= lima_pp_hir_block_import(data, size - pos, &block_pos, prog);
		if (!blocks[i])
		{
			lima_pp_hir_prog_delete(prog);
			fprintf(stderr, "Error: Failed to read basic block.\n");
			return NULL;
		}
		
		data = (char*)data + block_pos;
		pos += block_pos;
		if (pos > size)
		{
			lima_pp_hir_prog_delete(prog);
			return NULL;
		}
	}
	
	for (i = 0; i < header.num_arrays; i++)
	{
		_array_data_t* array_data = data;
		lima_pp_hir_temp_array_t array;
		array.start = array_data->start;
		array.end = array_data->end;
		array.alignment = array_data->alignment;
		if (!lima_pp_hir_prog_add_array(prog, array))
			return false;
		
		data = (char*)data + sizeof(_array_data_t);
		pos += sizeof(_array_data_t);
		if (pos > size)
		{
			lima_pp_hir_prog_delete(prog);
			free(blocks);
			return NULL;
		}
	}
	
	//Rewrite indices created in lima_pp_hir_block_import() into pointers
	for (i = 0; i < header.num_blocks; i++)
	{
		lima_pp_hir_block_t* block = blocks[i];
		if (!block->is_end)
		{
			if (block->next[0])
				block->next[0] = blocks[(unsigned)block->next[0] - 1];
			if (block->next[1])
				block->next[1] = blocks[(unsigned)block->next[1] - 1];
		}
	}
	
	if (!lima_pp_hir_prog_add_predecessors(prog))
	{
		lima_pp_hir_prog_delete(prog);
		free(blocks);
		fprintf(stderr, "Error: failed to add predecessor basic blocks.\n");
		return NULL;
	}
	
	free(blocks);
	return prog;
}

void* lima_pp_hir_prog_export(lima_pp_hir_prog_t* prog, unsigned* size)
{
	if (!prog)
		return false;

	_lima_pp_hir_file_header_t header;
	memcpy(&header.ident, "LIR\0", 4);
	header.version    = 3;
	header.num_blocks = prog->num_blocks;
	header.num_arrays = prog->num_arrays;
	
	*size = 0;
	
	void* data = malloc(sizeof(_lima_pp_hir_file_header_t));
	if (!data)
		return NULL;
	
	memcpy(data, &header, sizeof(header));
	*size += sizeof(header);

	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		unsigned block_size = 0;
		void* block_data = lima_pp_hir_block_export(block, prog,
													&block_size);
		if (!block_data)
		{
			fprintf(stderr, "Error: Failed to export basic block.\n");
			return false;
		}
		
		data = realloc(data, *size + block_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, block_data, block_size);
		*size += block_size;
		free(block_data);
	}
	
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
	{
		_array_data_t array_data;
		array_data.start = prog->arrays[i].start;
		array_data.end = prog->arrays[i].end;
		array_data.alignment = prog->arrays[i].alignment;
		
		data = realloc(data, *size + sizeof(_array_data_t));
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, &array_data, sizeof(_array_data_t));
		*size += sizeof(_array_data_t);
	}

	return data;
}
