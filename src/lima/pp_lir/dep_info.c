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
#include "fixed_queue.h"

static bool is_temp_load(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_loadt_one ||
	op == lima_pp_hir_op_loadt_one_off ||
	op == lima_pp_hir_op_loadt_four ||
	op == lima_pp_hir_op_loadt_four_off;
}

static bool is_temp_store(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_storet_one ||
	op == lima_pp_hir_op_storet_one_off ||
	op == lima_pp_hir_op_storet_four ||
	op == lima_pp_hir_op_storet_four_off;
}

bool lima_pp_lir_instr_can_swap(lima_pp_lir_instr_t* before,
								lima_pp_lir_instr_t* after)
{
	unsigned i, j;
	if (lima_pp_hir_op[before->op].has_dest && !before->dest.pipeline)
	{
		//Check for read-after-write dependencies
		for (i = 0; i < lima_pp_hir_op[after->op].args; i++)
			if (!after->sources[i].pipeline &&
				after->sources[i].reg == before->dest.reg)
			{
				for (j = 0; j < lima_pp_lir_arg_size(after, i); j++)
					if (before->dest.mask[after->sources[i].swizzle[j]])
						return false;
			}
		
		//Check for write-after-write dependencies
		if (lima_pp_hir_op[after->op].has_dest && !before->dest.pipeline &&
			after->dest.reg == before->dest.reg)
		{
			for (i = 0; i < 4; i++)
				if (before->dest.mask[i] && after->dest.mask[i])
					return false;
		}
	}
	
	if (lima_pp_hir_op[after->op].has_dest && !after->dest.pipeline)
	{
		//Check for write-after-read dependencies
		for (i = 0; i < lima_pp_hir_op[before->op].args; i++)
			if (!before->sources[i].pipeline &&
				before->sources[i].reg == after->dest.reg)
			{
				for (j = 0; j < lima_pp_lir_arg_size(before, i); j++)
					if (after->dest.mask[before->sources[i].swizzle[j]])
						return false;
			}
	}
	
	//Check for temporary dependencies
	
	if (is_temp_store(before->op) &&
		(is_temp_load(after->op) || is_temp_store(after->op)))
		return false;
	
	if (is_temp_store(after->op) &&
		(is_temp_load(before->op) || is_temp_store(before->op)))
		return false;
	
	return true;
}


static void update_reg_write_regs(bitset_t read_regs, bitset_t write_regs,
								  lima_pp_lir_instr_t* instr)
{
	unsigned i, j;
	
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].constant || instr->sources[i].pipeline)
			continue;
		
		lima_pp_lir_reg_t* reg = instr->sources[i].reg;
		unsigned index = reg->index;
		if (!reg->precolored)
			index += 6;
		
		for (j = 0; j < lima_pp_lir_arg_size(instr, i); j++)
		{
			if (!lima_pp_lir_channel_used(instr, i, j))
				continue;
			
			unsigned chan = instr->sources[i].swizzle[j];
			if (!bitset_get(write_regs, 4*index + chan))
				bitset_set(read_regs, 4*index + chan, true);
		}
	}
	
	if (lima_pp_hir_op[instr->op].has_dest && !instr->dest.pipeline)
	{
		unsigned index = instr->dest.reg->index;
		if (!instr->dest.reg->precolored)
			index += 6;
		
		for (i = 0; i < instr->dest.reg->size; i++)
			if (instr->dest.mask[i])
			{
				bitset_set(write_regs, 4*index + i, true);
			}
			
	}
}

static void calc_read_write_regs(lima_pp_lir_scheduled_instr_t* instr)
{
	instr->read_regs = bitset_create((instr->block->prog->reg_alloc + 6) * 4);
	instr->write_regs = bitset_create((instr->block->prog->reg_alloc + 6) * 4);
	
	if (instr->varying_instr)
		update_reg_write_regs(instr->read_regs, instr->write_regs,
							  instr->varying_instr);
	
	if (instr->texld_instr)
		update_reg_write_regs(instr->read_regs, instr->write_regs,
							  instr->texld_instr);
	
	if (instr->uniform_instr)
		update_reg_write_regs(instr->read_regs, instr->write_regs,
							  instr->uniform_instr);
	
	unsigned i;
	for (i = 0; i < 5; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		update_reg_write_regs(instr->read_regs, instr->write_regs,
							  instr->alu_instrs[i]);
	}
	
	if (instr->temp_store_instr)
		update_reg_write_regs(instr->read_regs, instr->write_regs,
							  instr->temp_store_instr);
	
	if (instr->branch_instr)
		update_reg_write_regs(instr->read_regs, instr->write_regs,
							  instr->branch_instr);
}

static void delete_read_write_regs(lima_pp_lir_scheduled_instr_t* instr)
{
	bitset_delete(instr->read_regs);
	bitset_delete(instr->write_regs);
}

static void add_dep(lima_pp_lir_scheduled_instr_t* before,
					lima_pp_lir_scheduled_instr_t* after)
{
	ptrset_add(&before->succs, after);
	ptrset_add(&after->preds, before);
	ptrset_add(&before->min_succs, after);
	ptrset_add(&after->min_preds, before);
}

static void add_true_dep(lima_pp_lir_scheduled_instr_t* before,
						 lima_pp_lir_scheduled_instr_t* after)
{
	ptrset_add(&before->true_succs, after);
	ptrset_add(&after->true_preds, before);
	add_dep(before, after);
}

static void add_dep_temp_load(lima_pp_lir_scheduled_instr_t* instr)
{
	lima_pp_lir_scheduled_instr_t* cur_instr = instr;
	while (cur_instr != pp_lir_block_last_instr(instr->block))
	{
		cur_instr = pp_lir_block_next_instr(cur_instr);
		
		if (cur_instr->temp_store_instr &&
			is_temp_store(cur_instr->temp_store_instr->op))
		{
			add_dep(instr, cur_instr);
			break;
		}
	}
}

static void add_dep_temp_store(lima_pp_lir_scheduled_instr_t* instr)
{
	lima_pp_lir_scheduled_instr_t* cur_instr = instr;
	while (cur_instr != pp_lir_block_last_instr(instr->block))
	{
		cur_instr = pp_lir_block_next_instr(cur_instr);
		
		if (cur_instr->temp_store_instr &&
			is_temp_store(cur_instr->temp_store_instr->op))
		{
			add_dep(instr, cur_instr);
			break;
		}
		
		if (cur_instr->uniform_instr &&
			is_temp_load(cur_instr->uniform_instr->op))
		{
			add_dep(instr, cur_instr);
		}
	}
}

static void add_dep_instr(lima_pp_lir_scheduled_instr_t* instr)
{
	lima_pp_lir_block_t* block = instr->block;
	
	if (instr == pp_lir_block_last_instr(block))
		return;
	
	if (instr->temp_store_instr && is_temp_store(instr->temp_store_instr->op))
		add_dep_temp_store(instr);
	else if (instr->uniform_instr && is_temp_load(instr->uniform_instr->op))
		add_dep_temp_load(instr);
	
	bitset_t read_regs = bitset_new(instr->read_regs);
	bitset_t write_regs = bitset_new(instr->write_regs);
	
	lima_pp_lir_scheduled_instr_t* cur_instr = pp_lir_block_next_instr(instr);
	while (!bitset_empty(instr->read_regs) || !bitset_empty(instr->write_regs))
	{
		//Read-after-write (true)
		bitset_t temp = bitset_new(instr->write_regs);
		bitset_disjunction(&temp, cur_instr->read_regs);
		if (!bitset_empty(temp))
			add_true_dep(instr, cur_instr);
		bitset_delete(temp);
		
		//Write-after-read
		bitset_subtract(&read_regs, cur_instr->write_regs);
		if (!bitset_equal(instr->read_regs, read_regs))
		{
			bitset_copy(&instr->read_regs, read_regs);
			add_dep(instr, cur_instr);
		}
		
		//Write-after-write
		bitset_subtract(&write_regs, cur_instr->write_regs);
		if (!bitset_equal(instr->write_regs, write_regs))
		{
			bitset_copy(&instr->write_regs, write_regs);
			add_dep(instr, cur_instr);
		}
		
		if (cur_instr == pp_lir_block_last_instr(block))
			break;
		
		cur_instr = pp_lir_block_next_instr(cur_instr);
	}
	
	bitset_delete(read_regs);
	bitset_delete(write_regs);
	
	//If we have a branch instruction, then we need to make sure we run
	//after all other instructions
	
	if (instr->branch_instr)
	{
		lima_pp_lir_scheduled_instr_t* other;
		pp_lir_block_for_each_instr(block, other)
		{
			if (other == instr)
				break;
			
			if (ptrset_size(other->succs) == 0)
			{
				add_dep(other, instr);
			}
		}
	}
}

static void remove_true_dep(lima_pp_lir_scheduled_instr_t* before,
							lima_pp_lir_scheduled_instr_t* after)
{
	ptrset_remove(&before->min_succs, after);
	ptrset_remove(&after->min_preds, before);
}

static void remove_edges(lima_pp_lir_scheduled_instr_t* start_instr,
						 lima_pp_lir_scheduled_instr_t* cur_instr)
{
	if (cur_instr->visited)
		return;
	
	ptrset_iter_t iter = ptrset_iter_create(cur_instr->succs);
	lima_pp_lir_scheduled_instr_t* child;
	ptrset_iter_for_each(iter, child)
	{
		remove_true_dep(start_instr, child);
		remove_edges(start_instr, child);
	}
	
	cur_instr->visited = true;
}

/* Calculates the transitive reduction of the dataflow graph, which contains
 * the minimum necessary links. This has the property that if there is a link
 * A -> B, then there is no other way to get from A to B, and therefore it is
 * safe to merge A and B, combining their successors & predecessors. Note that
 * the combined node, however, may have extra successors/predecessors, meaning
 * that this function must be called again before using min_preds and min_succs
 * of the combined node.
 */

void lima_pp_lir_calc_min_dep_info(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		lima_pp_lir_scheduled_instr_t* temp;
		pp_lir_block_for_each_instr(block, temp)
		{
			temp->visited = false;
		}
		
		ptrset_iter_t iter = ptrset_iter_create(instr->succs);
		lima_pp_lir_scheduled_instr_t* succ;
		ptrset_iter_for_each(iter, succ)
		{
			remove_edges(instr, succ);
		}
	}
}

static void calc_dep_info_block(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		calc_read_write_regs(instr);
	}
	
	pp_lir_block_for_each_instr(block, instr)
	{
		add_dep_instr(instr);
	}
	
	pp_lir_block_for_each_instr(block, instr)
	{
		delete_read_write_regs(instr);
	}
	
	lima_pp_lir_calc_min_dep_info(block);
		
}

void lima_pp_lir_calc_dep_info(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		calc_dep_info_block(block);
	}
}

static void delete_dep_info_instr(lima_pp_lir_scheduled_instr_t* instr)
{
	ptrset_empty(&instr->succs);
	ptrset_empty(&instr->preds);
	ptrset_empty(&instr->true_succs);
	ptrset_empty(&instr->true_preds);
	ptrset_empty(&instr->min_succs);
	ptrset_empty(&instr->min_preds);
}

static void delete_dep_info_block(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		delete_dep_info_instr(instr);
	}
}

void lima_pp_lir_delete_dep_info(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
		delete_dep_info_block(prog->blocks[i]);
}
