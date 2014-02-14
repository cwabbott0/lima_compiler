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

#include "pp_lir.h"
#include <stdlib.h>
#include <string.h>

lima_pp_lir_block_t* lima_pp_lir_block_create(void)
{
	lima_pp_lir_block_t* block =
	calloc(sizeof(lima_pp_lir_block_t), 1);
	if (!block)
		return NULL;
	
	list_init(&block->instr_list);
	block->is_end = true;
	
	return block;
}

void lima_pp_lir_block_delete(lima_pp_lir_block_t* block)
{
	if (!block)
		return;
	
	while (block->num_instrs > 0)
	{
		lima_pp_lir_scheduled_instr_t* instr = pp_lir_block_first_instr(block);
		lima_pp_lir_block_remove(instr);
	}
	
	if (block->preds)
		free(block->preds);
	
	free(block);
}

typedef struct
{
	uint32_t index;
	uint8_t allocated : 1;
} _reg_data_t;

typedef struct
{
	uint32_t num_instrs;
	
	bool is_end : 1;
	bool discard : 1;
} _block_header_t;

void* lima_pp_lir_block_export(lima_pp_lir_block_t* block, unsigned* size)
{
	void** instrs;
	unsigned* instr_sizes;
	
	instrs = malloc(sizeof(void*) * block->num_instrs);
	if (!instrs)
		return NULL;
	
	instr_sizes = malloc(sizeof(unsigned) * block->num_instrs);
	if (!instr_sizes)
	{
		free(instrs);
		return NULL;
	}
	
	*size = sizeof(_block_header_t);
	
	unsigned i = 0;
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		instrs[i] = lima_pp_lir_scheduled_instr_export(instr, instr_sizes + i);
		if (!instrs[i])
		{
			free(instrs);
			free(instr_sizes);
			return NULL;
		}
		*size += instr_sizes[i];
		i++;
	}
	
	void* data = malloc(*size);
	if (!data)
	{
		free(instrs);
		free(instr_sizes);
		return NULL;
	}
	
	_block_header_t* header = data;
	header->num_instrs = block->num_instrs;
	header->is_end = block->is_end;
	header->discard = block->discard;
	
	void* tmp = header + 1;
	for (i = 0; i < block->num_instrs; i++)
	{
		memcpy(tmp, instrs[i], instr_sizes[i]);
		free(instrs[i]);
		tmp += instr_sizes[i];
	}
	
	free(instrs);
	free(instr_sizes);
	
	return data;
}

lima_pp_lir_block_t* lima_pp_lir_block_import(void* data, unsigned* len,
											  lima_pp_lir_prog_t* prog)
{
	lima_pp_lir_block_t* block = lima_pp_lir_block_create();
	if (!block)
		return NULL;
	
	_block_header_t* header = data;
	unsigned num_instrs = header->num_instrs;
	*len = sizeof(_block_header_t);
	data = (char*) data + sizeof(_block_header_t);
	
	unsigned i;
	for (i = 0; i < num_instrs; i++)
	{
		unsigned instr_len;
		lima_pp_lir_scheduled_instr_t* instr =
			lima_pp_lir_scheduled_instr_import(data, &instr_len, prog);
		if (!instr)
		{
			lima_pp_lir_block_delete(block);
			return NULL;
		}
		
		lima_pp_lir_block_insert_end(block, instr);
		
		*len += instr_len;
		data = (char*) data + instr_len;
	}
	
	block->is_end = header->is_end;
	block->discard = header->discard;
	block->prog = prog;
	
	block->num_preds = 0;
	block->preds = NULL;
	
	return block;
}

void lima_pp_lir_block_insert_start(lima_pp_lir_block_t* block,
									lima_pp_lir_scheduled_instr_t* instr)
{
	list_add(&instr->instr_list, &block->instr_list);
	instr->block = block;
	block->num_instrs++;
}

void lima_pp_lir_block_insert_end(lima_pp_lir_block_t* block,
								  lima_pp_lir_scheduled_instr_t* instr)
{
	list_add(&instr->instr_list, block->instr_list.prev);
	instr->block = block;
	block->num_instrs++;
}

void lima_pp_lir_block_insert(lima_pp_lir_scheduled_instr_t* instr,
							  lima_pp_lir_scheduled_instr_t* before)
{
	list_add(&instr->instr_list, &before->instr_list);
	before->block->num_instrs++;
	instr->block = before->block;
}

void lima_pp_lir_block_insert_before(lima_pp_lir_scheduled_instr_t* instr,
									 lima_pp_lir_scheduled_instr_t* after)
{
	list_add(&instr->instr_list, after->instr_list.prev);
	after->block->num_instrs++;
	instr->block = after->block;
}

void lima_pp_lir_block_remove(lima_pp_lir_scheduled_instr_t* instr)
{
	list_del(&instr->instr_list);
	instr->block->num_instrs--;
	lima_pp_lir_scheduled_instr_delete(instr);
}
