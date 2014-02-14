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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

lima_pp_lir_prog_t *lima_pp_lir_prog_create(void)
{
	 return calloc(sizeof(lima_pp_lir_prog_t), 1);
}

void lima_pp_lir_prog_delete(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
		if (prog->blocks[i])
			lima_pp_lir_block_delete(prog->blocks[i]);
	free(prog->blocks);
	for (i = 0; i < prog->num_regs; i++)
		if (prog->regs[i])
			lima_pp_lir_reg_delete(prog->regs[i]);
	free(prog->regs);
	free(prog);
}

typedef struct
{
	uint32_t num_blocks, num_regs, temp_alloc;
} _prog_header_t;

typedef struct
{
	uint32_t index;
	uint8_t size;
	bool precolored : 1;
	bool beginning : 1;
} _reg_data_t;

static void* reg_export(lima_pp_lir_reg_t* reg, void* data)
{
	_reg_data_t* reg_data = data;
	reg_data->index = reg->index;
	reg_data->precolored = reg->precolored;
	reg_data->size = reg->size;
	reg_data->beginning = reg->beginning;
	return reg_data + 1;
}

void* lima_pp_lir_prog_export(lima_pp_lir_prog_t* prog, unsigned* size)
{
	void** block_data = malloc(prog->num_blocks * sizeof(void*));
	unsigned* block_size = malloc(prog->num_blocks * sizeof(unsigned));
	if (!block_data || !block_size)
		return NULL;
	
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		block_data[i] = lima_pp_lir_block_export(prog->blocks[i], block_size + i);
		if (!block_data[i])
		{
			free(block_data);
			free(block_size);
			return NULL;
		}
	}
	
	*size = sizeof(_prog_header_t) + prog->num_regs * sizeof(_reg_data_t);
	for (i = 0; i < prog->num_blocks; i++)
		*size += block_size[i];
	
	void* data = malloc(*size);
	if (!data)
	{
		free(block_data);
		free(block_size);
		return NULL;
	}
	
	_prog_header_t* header = data;
	header->num_blocks = prog->num_blocks;
	header->num_regs = prog->num_regs;
	header->temp_alloc = prog->temp_alloc;
	
	
	void* tmp = header + 1;
	for (i = 0; i < prog->num_regs; i++)
		tmp = reg_export(prog->regs[i], tmp);
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		memcpy(tmp, block_data[i], block_size[i]);
		tmp += block_size[i];
		free(block_data[i]);
	}
	
	free(block_data);
	free(block_size);
	
	return data;
}

static bool calc_predecessors(lima_pp_lir_prog_t* prog)
{
	unsigned i, j;
	for (i = 0; i < prog->num_blocks; i++)
		prog->blocks[i]->num_preds = 0;
	
	unsigned* cur_pred = calloc(sizeof(unsigned) * prog->num_blocks, 1);
	if (!cur_pred)
		return false;
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		if (block->is_end)
			continue;
		
		for (j = 0; j < block->num_succs; j++)
			prog->blocks[block->succs[j]]->num_preds++;
	}
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		prog->blocks[i]->preds = malloc(prog->blocks[i]->num_preds * sizeof(unsigned));
		if (!prog->blocks[i]->preds)
			return false;
	}
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		
		for (j = 0; j < block->num_succs; j++)
			prog->blocks[block->succs[j]]->preds[cur_pred[block->succs[j]]++] = i;
	}
	
	free(cur_pred);
	return true;
}

static void block_calc_successors(lima_pp_lir_block_t* block, unsigned i)
{
	if (block->is_end)
	{
		block->num_succs = 0;
		return;
	}
	
	if (block->num_instrs == 0)
	{
		block->num_succs = 1;
		block->succs[0] = i + 1;
		return;
	}
	
	lima_pp_lir_scheduled_instr_t* last_instr =
		pp_lir_block_last_instr(block);
	lima_pp_lir_instr_t* instr = last_instr->branch_instr;
	if (instr)
	{
		block->succs[0] = instr->branch_dest;
		if (instr->op == lima_pp_hir_op_branch)
		{
			if (block->num_instrs >= 2)
			{
				last_instr = pp_lir_block_prev_instr(last_instr);
				instr = last_instr->branch_instr;
				if (instr)
				{
					block->succs[1] = instr->branch_dest;
					block->num_succs = 2;
				}
				else
					block->num_succs = 1;
			}
			else
				block->num_succs = 1;
		}
		else
		{
			block->succs[1] = i + 1;
			block->num_succs = 2;
		}
	}
	else
	{
		block->num_succs = 1;
		block->succs[0] = i + 1;
	}
	
}

lima_pp_lir_prog_t* lima_pp_lir_prog_import(void* data, unsigned size)
{
	_prog_header_t* header = data;
	unsigned i, pos = sizeof(_prog_header_t);
	
	if (pos > size)
		return NULL;
	
	lima_pp_lir_prog_t* prog = lima_pp_lir_prog_create();
	if (!prog)
		return NULL;
	
	prog->num_blocks = header->num_blocks;
	prog->num_regs = header->num_regs;
	prog->temp_alloc = header->temp_alloc;
	
	prog->blocks = calloc(sizeof(lima_pp_lir_block_t*) * prog->num_blocks, 1);
	if (!prog->blocks)
	{
		lima_pp_lir_prog_delete(prog);
		return NULL;
	}
	
	prog->regs = calloc(sizeof(lima_pp_lir_reg_t*) * prog->num_regs, 1);
	if (!prog->regs)
	{
		lima_pp_lir_prog_delete(prog);
		return NULL;
	}
	
	data = header + 1;
	
	/* import registers */
	
	prog->reg_alloc = 0;
	
	for (i = 0; i < prog->num_regs; i++)
	{
		_reg_data_t* reg_data = data;
		
		prog->regs[i] = lima_pp_lir_reg_create();
		if (!prog->regs[i])
		{
			lima_pp_lir_prog_delete(prog);
			return NULL;
		}
		
		prog->regs[i]->index = reg_data->index;
		prog->regs[i]->precolored = reg_data->precolored;
		prog->regs[i]->size = reg_data->size;
		prog->regs[i]->beginning = reg_data->beginning;
		
		data = reg_data + 1;
		pos += sizeof(_reg_data_t);
		if (pos > size)
		{
			lima_pp_lir_prog_delete(prog);
			return NULL;
		}
		
		if (!prog->regs[i]->precolored && prog->regs[i]->index >= prog->reg_alloc)
			prog->reg_alloc = prog->regs[i]->index + 1;
	}
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		unsigned block_len;
		prog->blocks[i] = lima_pp_lir_block_import(data, &block_len, prog);
		if (!prog->blocks[i])
		{
			lima_pp_lir_prog_delete(prog);
			return NULL;
		}
		prog->blocks[i]->prog = prog;
		
		data += block_len;
		pos += block_len;
		if (pos > size)
		{
			lima_pp_lir_prog_delete(prog);
			return NULL;
		}
	}
	
	for (i = 0; i < prog->num_blocks; i++)
		block_calc_successors(prog->blocks[i], i);
	
	if (!calc_predecessors(prog))
	{
		lima_pp_lir_prog_delete(prog);
		return NULL;
	}
	
	return prog;
}

bool lima_pp_lir_prog_append_reg(lima_pp_lir_prog_t* prog, lima_pp_lir_reg_t* reg)
{
	if (prog->num_regs == 0) {
		prog->regs = malloc(sizeof(lima_pp_lir_reg_t*));
		prog->regs[0] = reg;
		prog->num_regs = 1;
		return true;
	}
	
	lima_pp_lir_reg_t **nregs = 
		realloc(prog->regs, (prog->num_regs + 1) * sizeof(lima_pp_lir_reg_t*));
	if (!nregs)
		return false;
	
	nregs[prog->num_regs++] = reg;
	prog->regs = nregs;
	reg->prog = prog;
	return true;
}

bool lima_pp_lir_prog_delete_reg(lima_pp_lir_prog_t* prog, unsigned index)
{
	if (index >= prog->num_regs)
		return false;
	
	if (prog->num_regs == 1) {
		free(prog->regs);
		prog->regs = NULL;
		prog->num_regs = 0;
		return true;
	}
	
	lima_pp_lir_reg_t* last_reg = prog->regs[prog->num_regs - 1];
	
	lima_pp_lir_reg_t** nregs =
		realloc(prog->regs, (prog->num_regs - 1) * sizeof(lima_pp_lir_reg_t*));
	if (!nregs)
		return false;
	
	prog->num_regs--;

	unsigned i;
	for (i = index; i < prog->num_regs - 1; i++)
		nregs[i] = nregs[i+1];
	if (index != prog->num_regs)
		nregs[prog->num_regs - 1] = last_reg;

	prog->regs = nregs;
	return true;
}

lima_pp_lir_reg_t* lima_pp_lir_prog_find_reg(lima_pp_lir_prog_t* prog,
											 unsigned index, bool precolored)
{
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
	{
		if (prog->regs[i]->index == index && prog->regs[i]->precolored == precolored)
			return prog->regs[i];
	}
	
	return NULL;
}
