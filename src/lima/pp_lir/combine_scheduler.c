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

#include "scheduler.h"

static void calc_max_dist(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		instr->max_dist = 0;
		
		if (ptrset_size(instr->preds) != 0)
		{
			ptrset_iter_t iter = ptrset_iter_create(instr->preds);
			lima_pp_lir_scheduled_instr_t* pred;
			ptrset_iter_for_each(iter, pred)
			{
				if (pred->max_dist > instr->max_dist)
					instr->max_dist = pred->max_dist;
				
				instr->max_dist++;
			}
		}
	}
}

static bool sched_priority(void* _instr1, void* _instr2)
{
	lima_pp_lir_scheduled_instr_t* instr1 = _instr1;
	lima_pp_lir_scheduled_instr_t* instr2 = _instr2;
	
	return instr1->max_dist >= instr2->max_dist;
}

static bool sched_insert(lima_pp_lir_scheduled_instr_t* instr)
{
	lima_pp_lir_scheduled_instr_t* latest_succ = NULL;
	
	lima_pp_lir_scheduled_instr_t* succ;
	ptrset_iter_t iter = ptrset_iter_create(instr->succs);
	ptrset_iter_for_each(iter, succ)
	{
		if (!latest_succ || succ->index > latest_succ->index)
			latest_succ = succ;
	}
	
	if (latest_succ &&
		lima_pp_lir_instr_combine_before(instr, latest_succ))
	{
		lima_pp_lir_scheduled_instr_delete(instr);
		return true;
	}
	
	if (instr->block->num_instrs == 0)
	{
		sched_insert_start(instr);
		return true;
	}
	
	lima_pp_lir_scheduled_instr_t* cur_instr = latest_succ;
	
	while (cur_instr != pp_lir_block_first_instr(instr->block))
	{
		if (cur_instr)
			cur_instr = pp_lir_block_prev_instr(cur_instr);
		else
			cur_instr = pp_lir_block_last_instr(instr->block);
		
		if (lima_pp_lir_instr_combine_indep(cur_instr, instr))
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return true;
		}
	}
	
	sched_insert_start(instr);
	return true;
}

bool lima_pp_lir_combine_schedule_block(lima_pp_lir_block_t* block)
{
	calc_max_dist(block);
	
	return lima_pp_lir_schedule_block(block, sched_priority, sched_insert);
}

bool lima_pp_lir_combine_schedule_prog(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		if (!lima_pp_lir_combine_schedule_block(prog->blocks[i]))
			return false;
	}
	
	return true;
}

