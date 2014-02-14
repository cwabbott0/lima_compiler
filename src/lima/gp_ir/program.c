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

#include "gp_ir.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

lima_gp_ir_prog_t* lima_gp_ir_prog_create(void)
{
	lima_gp_ir_prog_t* prog = malloc(sizeof(lima_gp_ir_prog_t));
	if (!prog)
		return NULL;
	
	list_init(&prog->block_list);
	list_init(&prog->reg_list);
	prog->num_blocks = prog->reg_alloc = 0;
	
	return prog;
}

void lima_gp_ir_prog_delete(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block, *temp_block;
	gp_ir_prog_for_each_block_safe(prog, block, temp_block)
	{
		lima_gp_ir_block_delete(block);
	}
	
	lima_gp_ir_reg_t* reg, *temp_reg;
	gp_ir_prog_for_each_reg_safe(prog, reg, temp_reg)
	{
		lima_gp_ir_reg_delete(reg);
	}
	free(prog);
}

void lima_gp_ir_prog_insert_start(lima_gp_ir_prog_t* prog,
								  lima_gp_ir_block_t* block)
{
	prog->num_blocks++;
	block->prog = prog;
	list_add(&block->block_list, prog->block_list.prev);
}

void lima_gp_ir_prog_insert_end(lima_gp_ir_prog_t* prog,
								lima_gp_ir_block_t* block)
{
	prog->num_blocks++;
	block->prog = prog;
	list_add(&block->block_list, &prog->block_list);
}

void lima_gp_ir_prog_insert(lima_gp_ir_block_t* block,
							lima_gp_ir_block_t* before)
{
	before->prog->num_blocks++;
	block->prog = before->prog;
	list_add(&block->block_list, &before->block_list);
}

void lima_gp_ir_prog_remove(lima_gp_ir_block_t* block)
{
	block->prog->num_blocks--;
	list_del(&block->block_list);
	lima_gp_ir_block_delete(block);
}

static void block_insert_preds(lima_gp_ir_block_t* block,
							   lima_gp_ir_block_t* pred)
{
	unsigned i;
	for (i = 0; i < block->num_preds; i++)
		if (!block->preds[i])
		{
			block->preds[i] = pred;
			break;
		}
}

bool lima_gp_ir_prog_calc_preds(lima_gp_ir_prog_t* prog)
{
	//First, free/invalidate any predecessors calculated before
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		if (block->preds)
		{
			free(block->preds);
			block->preds = NULL;
		}
		block->num_preds = 0;
	}
	
	//Calculate the number of predecessors for each block
	gp_ir_prog_for_each_block(prog, block)
	{
		bool next = true; //If the block can fall through to the next block
		if (gp_ir_block_is_last(block))
			next = false;
		if (!gp_ir_block_is_empty(block))
		{
			lima_gp_ir_root_node_t* last_node = gp_ir_block_last_node(block);
			if (last_node->node.op == lima_gp_ir_op_branch_cond ||
				last_node->node.op == lima_gp_ir_op_branch_uncond)
			{
				lima_gp_ir_branch_node_t* branch_node =
					gp_ir_node_to_branch(&last_node->node);
				
				branch_node->dest->num_preds++;
				
				if (last_node->node.op == lima_gp_ir_op_branch_uncond)
					next = false;
					
			}
		}
		
		if (next)
		{
			lima_gp_ir_block_t* next_block = gp_ir_block_next(block);
			next_block->num_preds++;
		}
	}
	
	//Allocate predecessors
	gp_ir_prog_for_each_block(prog, block)
	{
		if (block->num_preds)
		{
			block->preds =
				calloc(block->num_preds, sizeof(lima_gp_ir_block_t*));
			if (!block->preds)
				return false;
		}
	}
	
	//Fill out predecessor information
	gp_ir_prog_for_each_block(prog, block)
	{
		bool next = true; //If the block can fall through to the next block
		if (gp_ir_block_is_last(block))
			next = false;
		
		if (!gp_ir_block_is_empty(block))
		{
			lima_gp_ir_root_node_t* last_node = gp_ir_block_last_node(block);
			if (last_node->node.op == lima_gp_ir_op_branch_cond ||
				last_node->node.op == lima_gp_ir_op_branch_uncond)
			{
				lima_gp_ir_branch_node_t* branch_node =
					gp_ir_node_to_branch(&last_node->node);
				
				block_insert_preds(branch_node->dest, block);
				
				if (last_node->node.op == lima_gp_ir_op_branch_uncond)
					next = false;
				
			}
		}
		
		if (next)
		{
			lima_gp_ir_block_t* next_block = gp_ir_block_next(block);
			block_insert_preds(next_block, block);
		}
	}
	
	return true;
}

bool lima_gp_ir_prog_print(lima_gp_ir_prog_t* prog, unsigned tabs,
						   bool print_liveness)
{
	printf("(temp_alloc %u)\n\n", prog->temp_alloc);
	
	lima_gp_ir_block_t* block;
	unsigned index = 0;
	gp_ir_prog_for_each_block(prog, block)
		block->index = index++;
	
	gp_ir_prog_for_each_block(prog, block)
		if (!lima_gp_ir_block_print(block, tabs, print_liveness))
			return false;
	
	return true;
}

static unsigned calc_num_regs(lima_gp_ir_prog_t* prog)
{
	unsigned ret = 0;
	lima_gp_ir_reg_t* reg;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		ret++;
	}
	return ret;
}

typedef struct
{
	uint32_t index, size;
	uint32_t phys_reg, phys_reg_offset;
	bool phys_reg_assigned;
} reg_data_t;

static void* export_regs(lima_gp_ir_prog_t* prog, unsigned* size)
{
	unsigned num_regs = calc_num_regs(prog);
	*size = sizeof(uint32_t) + num_regs * sizeof(reg_data_t);
	void* data = malloc(*size);
	if (!data)
		return NULL;
	
	*(uint32_t*)data = num_regs;
	reg_data_t* reg_data = (reg_data_t*)((char*)data + sizeof(uint32_t));
	lima_gp_ir_reg_t* reg;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		reg_data->index = reg->index;
		reg_data->size = reg->size;
		reg_data->phys_reg = reg->phys_reg;
		reg_data->phys_reg_offset = reg->phys_reg_offset;
		reg_data->phys_reg_assigned = reg->phys_reg_assigned;
		reg_data = (reg_data_t*)((char*)reg_data + sizeof(reg_data_t));
	}
	
	return data;
}

static bool import_regs(lima_gp_ir_prog_t* prog, void* data, unsigned* size)
{
	unsigned num_regs = *(uint32_t*)data;
	
	*size = sizeof(uint32_t) + num_regs * sizeof(reg_data_t);
	
	reg_data_t* reg_data = (reg_data_t*)((char*)data + sizeof(uint32_t));
	unsigned i;
	for (i = 0; i < num_regs; i++)
	{
		lima_gp_ir_reg_t* reg = lima_gp_ir_reg_create(prog);
		prog->reg_alloc--;
		if (!reg)
			return false;
		
		reg->index = reg_data->index;
		reg->size = reg_data->size;
		reg->phys_reg = reg_data->phys_reg;
		reg->phys_reg_offset = reg_data->phys_reg_offset;
		reg->phys_reg_assigned = reg_data->phys_reg_assigned;
		if (reg->index >= prog->reg_alloc)
			prog->reg_alloc = reg->index + 1;
		reg_data = (reg_data_t*)((char*)reg_data + sizeof(reg_data_t));
	}
	
	return true;
}

static void index_blocks(lima_gp_ir_prog_t* prog)
{
	unsigned index = 0;
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		block->index = index++;
	}
}

static void* export_blocks(lima_gp_ir_prog_t* prog, unsigned* size)
{
	void** block_data = malloc(sizeof(void*) * prog->num_blocks);
	if (!block_data)
		return NULL;
	
	unsigned* block_sizes = malloc(sizeof(unsigned) * prog->num_blocks);
	if (!block_sizes)
	{
		free(block_data);
		return NULL;
	}
	
	index_blocks(prog);
	
	lima_gp_ir_block_t* block;
	unsigned i = 0;
	gp_ir_prog_for_each_block(prog, block)
	{
		block_data[i] = lima_gp_ir_block_export(block, block_sizes + i);
		if (!block_data[i])
		{
			free(block_data);
			free(block_sizes);
			return NULL;
		}
		i++;
	}
	
	*size = sizeof(uint32_t);
	for (i = 0; i < prog->num_blocks; i++)
		*size += block_sizes[i];
	
	void* data = malloc(*size);
	if (!data)
	{
		free(block_data);
		free(block_sizes);
		return NULL;
	}
	
	*(uint32_t*)data = prog->num_blocks;
	
	void* pos = (char*)data + sizeof(uint32_t);
	for (i = 0; i < prog->num_blocks; i++)
	{
		memcpy(pos, block_data[i], block_sizes[i]);
		free(block_data[i]);
		pos = (char*)pos + block_sizes[i];
	}
	
	free(block_data);
	free(block_sizes);
	
	return data;
}

static bool import_blocks(lima_gp_ir_prog_t* prog, void* data, unsigned* size)
{
	unsigned num_blocks = *(uint32_t*)data;
	
	*size = sizeof(uint32_t);
	
	unsigned i;
	for (i = 0; i < num_blocks; i++)
	{
		lima_gp_ir_block_t* block = lima_gp_ir_block_create();
		if (!block)
			return false;
		block->index = i;
		lima_gp_ir_prog_insert_end(prog, block);
	}
	
	lima_gp_ir_block_t* block;
	void* pos = (char*)data + sizeof(uint32_t);
	gp_ir_prog_for_each_block(prog, block)
	{
		unsigned block_size;
		if (!lima_gp_ir_block_import(block, pos, &block_size))
			return false;
		
		*size += block_size;
		pos = (char*)pos + block_size;
	}
	
	return true;
}

typedef struct {
	uint32_t temp_alloc;
} prog_header_t;

void* lima_gp_ir_prog_export(lima_gp_ir_prog_t* prog, unsigned* size)
{
	unsigned reg_size;
	void* reg_data = export_regs(prog, &reg_size);
	if (!reg_data)
		return NULL;
	
	unsigned block_size;
	void* block_data = export_blocks(prog, &block_size);
	if (!block_data)
	{
		free(reg_data);
		return NULL;
	}
	
	*size = reg_size + block_size + sizeof(prog_header_t);
	
	void* data = malloc(*size);
	if (!data)
	{
		free(block_data);
		free(reg_data);
		return NULL;
	}
	
	prog_header_t* header = (prog_header_t*)data;
	header->temp_alloc = prog->temp_alloc;
	
	memcpy((char*)data + sizeof(prog_header_t), reg_data, reg_size);
	memcpy((char*)data + sizeof(prog_header_t) + reg_size,
		   block_data, block_size);
	
	free(reg_data);
	free(block_data);
	
	return data;
}

lima_gp_ir_prog_t* lima_gp_ir_prog_import(void* data, unsigned* size)
{
	lima_gp_ir_prog_t* prog = lima_gp_ir_prog_create();
	if (!prog)
		return NULL;
	
	prog_header_t* header = (prog_header_t*)data;
	prog->temp_alloc = header->temp_alloc;
	
	data = (void*)(header + 1);
	
	unsigned reg_size;
	if (!import_regs(prog, data, &reg_size))
	{
		lima_gp_ir_prog_delete(prog);
		return NULL;
	}
	
	unsigned block_size;
	if (!import_blocks(prog, (char*)data + reg_size, &block_size))
	{
		lima_gp_ir_prog_delete(prog);
		return NULL;
	}
	
	*size = reg_size + block_size + sizeof(prog_header_t);
	
	return prog;
}
