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

#include "scheduler.h"
#include "fixed_queue.h"
#include <stdlib.h>

/* implements a pre-RA register pressure sensitive scheduler as described in
 * the paper "Register-Sensitive Selection, Duplication, and Sequencing of
 * Instructions" (note that we only implement the sequencing part)
 */

static void cut_fan_out_nodes(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		if (ptrset_size(instr->true_succs) < 2)
			continue;
		
		ptrset_iter_t iter = ptrset_iter_create(instr->true_succs);
		lima_pp_lir_scheduled_instr_t* succ;
		ptrset_iter_for_each(iter, succ)
		{
			ptrset_remove(&succ->true_preds, instr);
		}
		
		ptrset_empty(&instr->true_succs);
	}
}

static int compare(const void* a, const void* b)
{
	return *(unsigned*)a - *(unsigned*)b;
}

static bool calc_reg_pressure(lima_pp_lir_scheduled_instr_t* instr)
{
	unsigned num_children = ptrset_size(instr->true_preds);
	
	if (num_children == 0)
	{
		instr->reg_pressure = 0;
		return true;
	}
	
	unsigned* child_reg_pressure = malloc(num_children * sizeof(unsigned));
	if (!child_reg_pressure)
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(instr->true_preds);
	lima_pp_lir_scheduled_instr_t* child;
	unsigned i = 0;
	ptrset_iter_for_each(iter, child)
	{
		if (!calc_reg_pressure(child))
		{
			free(child_reg_pressure);
			return false;
		}
		
		child_reg_pressure[i] = child->reg_pressure;
		i++;
	}
	
	qsort(child_reg_pressure, num_children, sizeof(unsigned), compare);
	
	instr->reg_pressure = 0;
	for (i = 0; i < num_children; i++)
	{
		unsigned temp = child_reg_pressure[i] + num_children - i - 1;
		if (temp > instr->reg_pressure)
			instr->reg_pressure = temp;
	}
	
	free(child_reg_pressure);
	return true;
}

static bool calc_reg_pressure_block(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		if (ptrset_size(instr->true_succs) != 0)
			continue;
		
		if (!calc_reg_pressure(instr))
			return false;
	}
	
	return true;
}

//Note: this function is similar to the one in combine_scheduler.c, except
//we can't assume the ordering of the instructions is correct since the
//peephole optimizations may have left the instructions in the wrong order.

static void calc_max_dist(lima_pp_lir_block_t* block)
{
	fixed_queue_t work_queue = fixed_queue_create(block->num_instrs);
	
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		if (ptrset_size(instr->preds) == 0)
		{
			fixed_queue_push(&work_queue, instr);
		}
		instr->visited = false;
		instr->max_dist = 0;
	}
	
	while (!fixed_queue_is_empty(work_queue))
	{
		instr = fixed_queue_pop(&work_queue);
		
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
		
		instr->visited = true;
		
		ptrset_iter_t iter = ptrset_iter_create(instr->succs);
		lima_pp_lir_scheduled_instr_t* succ;
		ptrset_iter_for_each(iter, succ)
		{
			bool preds_visited = true;
			ptrset_iter_t iter2 = ptrset_iter_create(succ->preds);
			lima_pp_lir_scheduled_instr_t* pred;
			ptrset_iter_for_each(iter2, pred)
			{
				if (!pred->visited)
				{
					preds_visited = false;
					break;
				}
			}
			
			if (preds_visited)
			{
				fixed_queue_push(&work_queue, succ);
			}
		}
	}
	
	fixed_queue_delete(work_queue);
}

static bool sched_priority(void* _instr1, void* _instr2)
{
	lima_pp_lir_scheduled_instr_t* instr1 = _instr1;
	lima_pp_lir_scheduled_instr_t* instr2 = _instr2;
	
	
	/* first up: index of parent in the fan-in tree. We want the node with the
	 * smallest parent index. Note that not having a parent is equivalent to
	 * a parent index of infinity.
	 */
	
	if (ptrset_size(instr1->true_succs) == 0 &&
		ptrset_size(instr2->true_succs) == 1)
		return false;
	
	if (ptrset_size(instr2->true_succs) == 0 &&
		ptrset_size(instr1->true_succs) == 1)
		return true;
	
	if (ptrset_size(instr1->true_succs) == 1 &&
		ptrset_size(instr2->true_succs) == 1)
	{
		lima_pp_lir_scheduled_instr_t* instr1_parent =
			ptrset_first(instr1->true_succs);
		
		lima_pp_lir_scheduled_instr_t* instr2_parent =
			ptrset_first(instr2->true_succs);
		
		if (instr1_parent->index > instr2_parent->index)
			return true;
		
		if (instr1_parent->index < instr2_parent->index)
			return false;
	}
	
	/* Next up is register pressure. We want to schedule the node with the
	 * lowest register pressure first.
	 */
	
	if (instr1->reg_pressure > instr2->reg_pressure)
		return false;
	
	if (instr1->reg_pressure < instr2->reg_pressure)
		return true;
	
	/* Finally, choose the node with the largest max_dist (critical path
	 * heuristic)
	 */
	
	return instr1->max_dist >= instr2->max_dist;
}

static bool sched_insert(lima_pp_lir_scheduled_instr_t* instr)
{
	sched_insert_start(instr);
	return true;
}

bool lima_pp_lir_reg_pressure_schedule_block(lima_pp_lir_block_t* block)
{
	cut_fan_out_nodes(block);
	
	if (!calc_reg_pressure_block(block))
		return false;
	
	calc_max_dist(block);
	
	return lima_pp_lir_schedule_block(block, sched_priority, sched_insert);
}

bool lima_pp_lir_reg_pressure_schedule_prog(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		if (!lima_pp_lir_reg_pressure_schedule_block(prog->blocks[i]))
			return false;
	}
	
	return true;
}
