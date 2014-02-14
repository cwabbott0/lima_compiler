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

#include "pp_lir.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "fixed_queue.h"

static void liveness_init_instr(lima_pp_lir_instr_t* instr, unsigned size)
{
	instr->live_in = bitset_create(size);
	instr->live_out = bitset_create(size);
}

static void liveness_init_sched_instr(lima_pp_lir_scheduled_instr_t* instr,
									  unsigned size)
{
	unsigned i;
	
	if (instr->varying_instr)
		liveness_init_instr(instr->varying_instr, size);
	
	if (instr->texld_instr)
		liveness_init_instr(instr->texld_instr, size);

	if (instr->uniform_instr)
		liveness_init_instr(instr->uniform_instr, size);
	
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			liveness_init_instr(instr->alu_instrs[i], size);

	if (instr->temp_store_instr)
		liveness_init_instr(instr->temp_store_instr, size);

	if (instr->branch_instr)
		liveness_init_instr(instr->branch_instr, size);
	
	instr->live_in = bitset_create(size);
	instr->live_out = bitset_create(size);
}

bool lima_pp_lir_liveness_init(lima_pp_lir_prog_t* prog)
{
	unsigned i, j;
	unsigned size = (prog->reg_alloc + 1) * 4;
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		
		block->live_in = bitset_create(size);
		block->live_out = bitset_create(size);
		
		if (block->is_end)
		{
			for (j = 0; j < 4; j++)
				bitset_set(block->live_out, j, true); 
		}
		
		lima_pp_lir_scheduled_instr_t* instr;
		pp_lir_block_for_each_instr(block, instr)
		{
			liveness_init_sched_instr(instr, size);
		}
	}
	
	return true;
}

static void instr_liveness_delete(lima_pp_lir_instr_t* instr)
{
	bitset_delete(instr->live_in);
	bitset_delete(instr->live_out);
}

static void scheduled_instr_liveness_delete(lima_pp_lir_scheduled_instr_t* instr)
{
	unsigned i;
	
	bitset_delete(instr->live_in);
	bitset_delete(instr->live_out);
	
	if (instr->varying_instr)
		instr_liveness_delete(instr->varying_instr);
	if (instr->texld_instr)
		instr_liveness_delete(instr->texld_instr);
	if (instr->uniform_instr)
		instr_liveness_delete(instr->uniform_instr);
	
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			instr_liveness_delete(instr->alu_instrs[i]);
	
	if (instr->temp_store_instr)
		instr_liveness_delete(instr->temp_store_instr);
	
	if (instr->branch_instr)
		instr_liveness_delete(instr->branch_instr);
}

void lima_pp_lir_liveness_delete(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		
		bitset_delete(block->live_in);
		bitset_delete(block->live_out);
		
		lima_pp_lir_scheduled_instr_t* instr;
		pp_lir_block_for_each_instr(block, instr)
		{
			scheduled_instr_liveness_delete(instr);
		}
	}
}

static unsigned get_index(lima_pp_lir_reg_t* reg)
{
	if (reg->precolored)
	{
		assert(reg->index == 0);
		return 0;
	}
	return reg->index + 1;
}

// Make anything that's written to dead
static void liveness_calc_write(lima_pp_lir_instr_t* instr,
								bitset_t cur_live)
{
	if (lima_pp_hir_op[instr->op].has_dest && !instr->dest.pipeline)
	{
		unsigned i;
		for (i = 0; i < instr->dest.reg->size; i++)
		{
			if (!instr->dest.mask[i])
				continue;
			
				bitset_set(cur_live,
						   4 * get_index(instr->dest.reg) + i, false);
		}
	}
}

// Make anything that's read from live
static void liveness_calc_read(lima_pp_lir_instr_t* instr,
							   bitset_t cur_live)
{
	unsigned i, j;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].constant || instr->sources[i].pipeline)
			continue;
		lima_pp_lir_reg_t* source_reg = instr->sources[i].reg;
		for (j = 0; j < lima_pp_lir_arg_size(instr, i); j++)
		{
			if (!lima_pp_lir_channel_used(instr, i, j))
				continue;
			
			bitset_set(cur_live,
					   4 * get_index(source_reg) + instr->sources[i].swizzle[j],
					   true);
		}
	}

}

/* returns true if the liveness information changed */

bool lima_pp_lir_liveness_calc_instr(lima_pp_lir_instr_t* instr)
{
	bitset_t old_live_in = bitset_new(instr->live_in);
	
	bitset_copy(&instr->live_in, instr->live_out);
	
	liveness_calc_write(instr, instr->live_in);
	liveness_calc_read(instr, instr->live_in);
	
	bool ret = !bitset_equal(instr->live_in, old_live_in);
	
	bitset_delete(old_live_in);
	
	return ret;
}

static void _liveness_calc_scheduled_instr(lima_pp_lir_instr_t* instr,
										   bitset_t* cur_live)
{
	bitset_copy(&instr->live_out, *cur_live);
	
	lima_pp_lir_liveness_calc_instr(instr);
	
	bitset_copy(cur_live, instr->live_in);
}

bool lima_pp_lir_liveness_calc_scheduled_instr(lima_pp_lir_scheduled_instr_t* instr)
{
	bitset_t cur_live = bitset_new(instr->live_out);
	bitset_t old_live_in = bitset_new(instr->live_in);
	
	if (instr->branch_instr)
		_liveness_calc_scheduled_instr(instr->branch_instr, &cur_live);
	
	if (instr->temp_store_instr)
		_liveness_calc_scheduled_instr(instr->temp_store_instr, &cur_live);
	
	
	if (instr->alu_instrs[4])
		_liveness_calc_scheduled_instr(instr->alu_instrs[4], &cur_live);
	
	unsigned i;
	for (i = 2; i < 4; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		bitset_copy(&instr->alu_instrs[i]->live_out, cur_live);
	}
	
	for (i = 2; i < 4; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		liveness_calc_write(instr->alu_instrs[i], cur_live);
	}
	
	for (i = 2; i < 4; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		liveness_calc_read(instr->alu_instrs[i], cur_live);
	}
	
	for (i = 2; i < 4; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		bitset_copy(&instr->alu_instrs[i]->live_in, cur_live);
	}
	
	for (i = 0; i < 2; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		bitset_copy(&instr->alu_instrs[i]->live_out, cur_live);
	}
	
	for (i = 0; i < 2; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		liveness_calc_write(instr->alu_instrs[i], cur_live);
	}
	
	for (i = 0; i < 2; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		liveness_calc_read(instr->alu_instrs[i], cur_live);
	}
	
	for (i = 0; i < 2; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		bitset_copy(&instr->alu_instrs[i]->live_in, cur_live);
	}
	
	if (instr->uniform_instr)
		_liveness_calc_scheduled_instr(instr->uniform_instr, &cur_live);
	
	if (instr->texld_instr)
		_liveness_calc_scheduled_instr(instr->texld_instr, &cur_live);
	
	if (instr->varying_instr)
		_liveness_calc_scheduled_instr(instr->varying_instr, &cur_live);

	bitset_copy(&instr->live_in, cur_live);
	
	bool ret = !bitset_equal(instr->live_in, old_live_in);
	
	bitset_delete(cur_live);
	bitset_delete(old_live_in);
	
	return ret;
}

static bool liveness_calc_block(lima_pp_lir_block_t* block)
{	
	lima_pp_lir_scheduled_instr_t* instr, *last = NULL;
	
	pp_lir_block_for_each_instr_reverse(block, instr)
	{
		if (last)
		{
			bitset_copy(&instr->live_out, last->live_in);
		}
		
		if (!lima_pp_lir_liveness_calc_scheduled_instr(instr))
			return false;
		
		last = instr;
	}
	
	return true;
}

bool lima_pp_lir_liveness_calc_block(lima_pp_lir_block_t* block)
{
	if (block->num_instrs > 0)
	{
		lima_pp_lir_scheduled_instr_t* last_instr =
			pp_lir_block_last_instr(block);
		bitset_copy(&last_instr->live_out, block->live_out);
	}
	else
	{
		// If this block is empty, then the set of live-in registers
		// is the same as the set of live-out registers
		if (bitset_equal(block->live_in, block->live_out))
			return false;
		
		bitset_copy(&block->live_in, block->live_out);
		return true;
	}
	
	if (!liveness_calc_block(block))
		return false;
	
	lima_pp_lir_scheduled_instr_t* first_instr = pp_lir_block_first_instr(block);
	bitset_copy(&block->live_in, first_instr->live_in);
	
	return true;
}

void lima_pp_lir_liveness_calc_prog(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	fixed_queue_t work_queue = fixed_queue_create(prog->num_blocks);

	for (i = 0; i < prog->num_blocks; i++)
		if (prog->blocks[i]->is_end)
			fixed_queue_push(&work_queue, (void*)prog->blocks[i]);
	
	while (!fixed_queue_is_empty(work_queue))
	{
		lima_pp_lir_block_t* block =
			(lima_pp_lir_block_t*)fixed_queue_pop(&work_queue);
		
		// Each block's set of live-out variables is the union of the live-in
		// variables of each of its succesor blocks.

		for (i = 0; i < block->num_succs; i++)
			bitset_union(&block->live_out,
						 prog->blocks[block->succs[i]]->live_in);
		
		if (lima_pp_lir_liveness_calc_block(block))
			for (i = 0; i < block->num_preds; i++)
				fixed_queue_push(&work_queue,
								 (void*)prog->blocks[block->preds[i]]);
	}
	
	fixed_queue_delete(work_queue);
}
