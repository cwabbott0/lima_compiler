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
#include "priority_queue.h"

bool lima_pp_lir_schedule_block(lima_pp_lir_block_t* block,
								sched_priority_cb sched_priority,
								sched_insert_cb sched_insert)
{
	priority_queue_t* work_queue =
		priority_queue_create(sched_priority);
	
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		if (ptrset_size(instr->succs) == 0)
			priority_queue_push(work_queue, instr);
		
		instr->visited = false;
	}
	
	//Here's the hacky part... we delete the instructions from the list,
	//the sched_insert() function should re-insert them
	while (block->num_instrs > 0)
	{
		list_del(block->instr_list.next);
		block->num_instrs--;
	}
	
	while (priority_queue_num_elems(work_queue) != 0)
	{
		instr = priority_queue_pull(work_queue);
		
		//Make a copy of instr->preds, since instr may be deleted by
		//sched_insert() and we need it in the loop later on
		ptrset_t preds;
		if (!ptrset_copy(&preds, instr->preds))
		{
			priority_queue_delete(work_queue);
			return false;
		}
		
		instr->visited = true;
		
		if (!sched_insert(instr))
		{
			priority_queue_delete(work_queue);
			ptrset_delete(preds);
			return false;
		}
		
		ptrset_iter_t iter = ptrset_iter_create(preds);
		lima_pp_lir_scheduled_instr_t* pred;
		ptrset_iter_for_each(iter, pred)
		{
			bool succs_visited = true;
			ptrset_iter_t iter2 = ptrset_iter_create(pred->succs);
			lima_pp_lir_scheduled_instr_t* succ;
			ptrset_iter_for_each(iter2, succ)
			{
				if (!succ->visited)
				{
					succs_visited = false;
					break;
				}
			}
			
			if (succs_visited)
			{
				priority_queue_push(work_queue, pred);
			}
		}
		
		ptrset_delete(preds);
	}
	
	priority_queue_delete(work_queue);
	return true;
}

bool lima_pp_lir_schedule_prog(lima_pp_lir_prog_t* prog,
							   sched_priority_cb sched_priority,
							   sched_insert_cb sched_insert)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
		if (!lima_pp_lir_schedule_block(prog->blocks[i], sched_priority,
										sched_insert))
			return false;
	
	return true;
}
