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

#include "pp_hir.h"
#include <stdlib.h>
#include <assert.h>

/* compress_temp_arrays.c
 *
 * Removes holes in the temporary address space left from temporary-to-register
 * conversion, while converting arrays to scalar arrays if possible. Also, since
 * this is the last time we touch temporaries before the conversion to pp_lir,
 * we set temp_alloc here for register allocation etc. in pp_lir.
 *
 * Note: as a precondition, we require that all temporaries are part of arrays
 * (since temp->reg should have removed all non-array temporaries) and that
 * all arrays have an alignment of 4 (since this pass converts some of them
 * to an alignment of 1 and 2).
 *
 * This pass should be run before register narrowing, since it can open up
 * opportunities for that pass.
 */


//Given an index accessed, return the array that contains that index.

unsigned get_array_index(lima_pp_hir_prog_t* prog, unsigned index)
{
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
	{
		if (index >= prog->arrays[i].start && index <= prog->arrays[i].end)
			return i;
	}
	
	assert(0);
	return 0;
}

static unsigned src_width(lima_pp_hir_cmd_t* cmd, unsigned src)
{
	if (cmd->op == lima_pp_hir_op_combine)
	{
		//For combines, this hack prevents any narrowing since that could
		//change the semantics of the combine
		return 4;
	}
	
	unsigned i, ret = 1;
	for (i = 0; i < lima_pp_hir_arg_size(cmd, src); i++)
		if (cmd->src[src].swizzle[i] + 1 > ret)
			ret = cmd->src[src].swizzle[i] + 1;
	
	return ret;
}

static unsigned dest_width(lima_pp_hir_cmd_t* cmd)
{	
	ptrset_iter_t iter = ptrset_iter_create(cmd->block_uses);
	lima_pp_hir_block_t* block;
	ptrset_iter_for_each(iter, block)
	{
		if (block->is_end && !block->discard && block->output == cmd)
			return 1; //Outputs use all 4 channels
	}
	
	unsigned ret = 1;
	lima_pp_hir_cmd_t* use;
	iter = ptrset_iter_create(cmd->cmd_uses);
	ptrset_iter_for_each(iter, use)
	{
		unsigned src;
		for (src = 0; src < use->num_args; src++)
		{
			if (use->src[src].constant || use->src[src].depend != cmd)
				continue;
			
			unsigned new_width = src_width(use, src);
			if (new_width > ret)
				ret = new_width;
		}
	}
	
	return ret;
}

static unsigned* get_array_width(lima_pp_hir_prog_t* prog)
{
	unsigned* ret = malloc(prog->num_arrays * sizeof(unsigned));
	if (!ret)
		return NULL;
	
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
		ret[i] = 1;
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op != lima_pp_hir_op_loadt_four &&
				cmd->op != lima_pp_hir_op_loadt_four_off)
				continue;
			
			unsigned index = get_array_index(prog, cmd->load_store_index);
			unsigned new_width = dest_width(cmd);
			if (ret[index] < new_width)
				ret[index] = new_width;
		}
	}
	
	return ret;
}

static int* calc_array_offsets(lima_pp_hir_prog_t* prog, unsigned* width)
{
	int* ret = malloc(prog->num_arrays * sizeof(int));
	if (!ret)
		return NULL;
	
	unsigned index = 0;
	
	//first assign arrays of width 3 and 4 (alignment 4)...
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
	{
		if (width[i] < 3)
			continue;
		
		unsigned length = prog->arrays[i].end - prog->arrays[i].start + 1;
		ret[i] = (int)index - prog->arrays[i].start;
		index += length;
	}
	
	//... and then of width 2...
	index *= 2;
	
	for (i = 0; i < prog->num_arrays; i++)
	{
		if (width[i] != 2)
			continue;
		
		unsigned length = prog->arrays[i].end - prog->arrays[i].start + 1;
		ret[i] = index - prog->arrays[i].start;
		index += length;
	}
	
	//... and then of width 1
	index *= 2;
	
	for (i = 0; i < prog->num_arrays; i++)
	{
		if (width[i] != 1)
			continue;
		
		unsigned length = prog->arrays[i].end - prog->arrays[i].start + 1;
		ret[i] = index - prog->arrays[i].start;
		index += length;
	}
	
	prog->temp_alloc = (index + 3) / 4;
	
	return ret;
}

static void rewrite_program(lima_pp_hir_prog_t* prog, int* offsets,
							unsigned* width)
{
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op == lima_pp_hir_op_loadt_four ||
				cmd->op == lima_pp_hir_op_loadt_four_off ||
				cmd->op == lima_pp_hir_op_storet_four ||
				cmd->op == lima_pp_hir_op_storet_four_off)
			{
				unsigned array = get_array_index(prog, cmd->load_store_index);
				cmd->load_store_index += offsets[array];
				if (width[array] == 1)
				{
					switch (cmd->op)
					{
						case lima_pp_hir_op_loadt_four:
							cmd->op = lima_pp_hir_op_loadt_one;
							cmd->dst.reg.size = 0;
							break;
							
						case lima_pp_hir_op_loadt_four_off:
							cmd->op = lima_pp_hir_op_loadt_one_off;
							cmd->dst.reg.size = 0;
							break;
							
						case lima_pp_hir_op_storet_four:
							cmd->op = lima_pp_hir_op_storet_one;
							break;
							
						case lima_pp_hir_op_storet_four_off:
							cmd->op = lima_pp_hir_op_storet_one_off;
							break;
							
						default:
							assert(0);
							break;
					}
				}
				else if (width[array] == 2)
				{
					switch (cmd->op)
					{
						case lima_pp_hir_op_loadt_four:
							cmd->op = lima_pp_hir_op_loadt_two;
							cmd->dst.reg.size = 1;
							break;
							
						case lima_pp_hir_op_loadt_four_off:
							cmd->op = lima_pp_hir_op_loadt_two_off;
							cmd->dst.reg.size = 1;
							break;
							
						case lima_pp_hir_op_storet_four:
							cmd->op = lima_pp_hir_op_storet_two;
							break;
							
						case lima_pp_hir_op_storet_four_off:
							cmd->op = lima_pp_hir_op_storet_one_off;
							break;
							
						default:
							assert(0);
							break;
					}
				}
			}
		}
	}
}

static void rewrite_arrays(lima_pp_hir_prog_t* prog, int* offsets,
						   unsigned* width)
{
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
	{
		prog->arrays[i].start += offsets[i];
		prog->arrays[i].end += offsets[i];
		if (width[i] == 1)
			prog->arrays[i].alignment = lima_pp_hir_align_one;
		else if (width[i] == 2)
			prog->arrays[i].alignment = lima_pp_hir_align_two;
	}
}

bool lima_pp_hir_compress_temp_arrays(lima_pp_hir_prog_t* prog)
{
	unsigned* width = get_array_width(prog);
	if (!width)
		return false;
	
	int* offsets = calc_array_offsets(prog, width);
	if (!offsets)
	{
		free(width);
		return false;
	}
	
	rewrite_program(prog, offsets, width);
	rewrite_arrays(prog, offsets, width);
	
	free(width);
	free(offsets);
	return true;
}

