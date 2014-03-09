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
#include <assert.h>

//Tries to move constants from old to new, creating a map used for updating
//indvidual instructions
static bool try_create_const_map(lima_pp_lir_scheduled_instr_t* new,
								 lima_pp_lir_scheduled_instr_t* old,
								 unsigned* const_map)
{
	unsigned const0_inserted = 0, const1_inserted = 0;
	unsigned i, j;
	
	for (i = 0; i < 8; i++)
		const_map[i] = 8;
	
	//First, try to find a home for const0
	
	bool const0_fit = true;
	for (i = 0; i < old->const0_size; i++)
	{
		bool found = false;
		for (j = 0; j < new->const0_size; j++)
			if (new->const0[j] == old->const0[i])
			{
				const_map[i] = j;
				found = true;
				break;
			}
		
		if (found)
			continue;
		
		if (new->const0_size + const0_inserted == 4)
		{
			//Inserting into const0 didn't work, try const1
			const0_fit = false;
			break;
		}
		
		const_map[i] = new->const0_size + const0_inserted;
		const0_inserted++;
	}
	
	if (!const0_fit)
	{
		const0_inserted = 0;
		
		for (i = 0; i < old->const0_size; i++)
		{
			bool found = false;
			for (j = 0; j < new->const1_size; j++)
				if (new->const1[j] == old->const0[i])
				{
					const_map[i] = j + 4;
					found = true;
					break;
				}
			
			if (found)
				continue;
			
			if (new->const1_size + const1_inserted == 4)
				return false;
			
			const_map[i] = new->const1_size + const1_inserted + 4;
			const1_inserted++;
		}
	}
	
	//Same thing for const1
	
	const0_fit = true;
	unsigned old_const0_inserted = const0_inserted;
	for (i = 0; i < old->const1_size; i++)
	{
		bool found = false;
		for (j = 0; j < new->const0_size; j++)
			if (new->const0[j] == old->const1[i])
			{
				const_map[i + 4] = j;
				found = true;
				break;
			}
		
		if (found)
			continue;
		
		if (new->const0_size + const0_inserted == 4)
		{
			//Inserting into const0 didn't work, try const1
			const0_fit = false;
			break;
		}
		
		const_map[i + 4] = new->const0_size + const0_inserted;
		const0_inserted++;
	}
	
	if (!const0_fit)
	{
		const0_inserted = old_const0_inserted;
		
		for (i = 0; i < old->const1_size; i++)
		{
			bool found = false;
			for (j = 0; j < new->const1_size; j++)
				if (new->const1[j] == old->const1[i])
				{
					const_map[i + 4] = j + 4;
					found = true;
					break;
				}
			
			if (found)
				continue;
			
			if (new->const1_size + const1_inserted == 4)
				return false;
			
			const_map[i + 4] = new->const1_size + const1_inserted + 4;
			const1_inserted++;
		}
	}
	
	for (i = 0; i < old->const0_size; i++)
		assert(const_map[i] < 8);
	for (i = 0; i < old->const1_size; i++)
		assert(const_map[i + 4] < 8);
	
	return true;
}

static void apply_const_map(lima_pp_lir_scheduled_instr_t* new,
							lima_pp_lir_scheduled_instr_t* old,
							unsigned* const_map)
{
	unsigned i;
	
	for (i = 0; i < old->const0_size; i++)
	{
		if (const_map[i] < 4)
		{
			new->const0[const_map[i]] = old->const0[i];
			if (const_map[i] >= new->const0_size)
				new->const0_size = const_map[i] + 1;
		}
		else
		{
			new->const1[const_map[i] - 4] = old->const0[i];
			if (const_map[i] - 4 >= new->const1_size)
				new->const1_size = const_map[i] - 3;
		}
	}
	
	for (i = 0; i < old->const1_size; i++)
	{
		if (const_map[i + 4] < 4)
		{
			new->const0[const_map[i + 4]] = old->const1[i];
			if (const_map[i + 4] >= new->const0_size)
				new->const0_size = const_map[i + 4] + 1;
		}
		else
		{
			new->const1[const_map[i + 4] - 4] = old->const1[i];
			if (const_map[i + 4] - 4 >= new->const1_size)
				new->const1_size = const_map[i + 4] - 3;
		}
	}
}

static void rewrite_consts(lima_pp_lir_instr_t* instr, unsigned* const_map)
{
	unsigned i, j;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (!instr->sources[i].pipeline ||
			(instr->sources[i].pipeline_reg != lima_pp_lir_pipeline_reg_const0 &&
			 instr->sources[i].pipeline_reg != lima_pp_lir_pipeline_reg_const1))
			continue;
		
		unsigned const_map_val;
		
		for (j = 0; j < lima_pp_lir_arg_size(instr, i); j++)
		{
			if (!lima_pp_lir_channel_used(instr, i, j))
				continue;
			
			if (instr->sources[i].pipeline_reg == lima_pp_lir_pipeline_reg_const0)
				const_map_val = const_map[instr->sources[i].swizzle[j]];
			else
				const_map_val = const_map[instr->sources[i].swizzle[j] - 4];
			
			assert(const_map_val < 8);
			
			if (const_map_val >= 4)
				const_map_val -= 4;
			
			instr->sources[i].swizzle[j] = const_map_val;
		}
		
		if (instr->sources[i].pipeline_reg == lima_pp_lir_pipeline_reg_const0)
			const_map_val = const_map[0];
		else
			const_map_val = const_map[4];
		
		if (const_map_val < 4)
			instr->sources[i].pipeline_reg = lima_pp_lir_pipeline_reg_const0;
		else
			instr->sources[i].pipeline_reg = lima_pp_lir_pipeline_reg_const1;
	}
}

static void combine_deps(lima_pp_lir_scheduled_instr_t* instr,
						 lima_pp_lir_scheduled_instr_t* other)
{
	ptrset_iter_t iter = ptrset_iter_create(other->preds);
	lima_pp_lir_scheduled_instr_t* pred;
	ptrset_iter_for_each(iter, pred)
	{
		ptrset_remove(&pred->succs, other);
		ptrset_add(&pred->succs, instr);
	}
	
	ptrset_union(&instr->preds, other->preds);
	
	iter = ptrset_iter_create(other->succs);
	lima_pp_lir_scheduled_instr_t* succ;
	ptrset_iter_for_each(iter, succ)
	{
		ptrset_remove(&succ->preds, other);
		ptrset_add(&succ->preds, instr);
	}
	
	ptrset_union(&instr->succs, other->succs);
	
	iter = ptrset_iter_create(other->min_preds);
	ptrset_iter_for_each(iter, pred)
	{
		ptrset_remove(&pred->min_succs, other);
		ptrset_add(&pred->min_succs, instr);
	}
	
	ptrset_union(&instr->min_preds, other->min_preds);
	
	iter = ptrset_iter_create(other->min_succs);
	ptrset_iter_for_each(iter, succ)
	{
		ptrset_remove(&succ->min_preds, other);
		ptrset_add(&succ->min_preds, instr);
	}
	
	ptrset_union(&instr->min_succs, other->min_succs);
	
	iter = ptrset_iter_create(other->true_preds);
	ptrset_iter_for_each(iter, pred)
	{
		ptrset_remove(&pred->true_succs, other);
		ptrset_add(&pred->true_succs, instr);
	}
	
	ptrset_union(&instr->true_preds, other->true_preds);
	
	iter = ptrset_iter_create(other->true_succs);
	ptrset_iter_for_each(iter, succ)
	{
		ptrset_remove(&succ->true_preds, other);
		ptrset_add(&succ->true_preds, instr);
	}
	
	ptrset_union(&instr->true_succs, other->true_succs);
}

static void remove_dep(lima_pp_lir_scheduled_instr_t* before,
					   lima_pp_lir_scheduled_instr_t* after)
{
	ptrset_remove(&before->succs, after);
	ptrset_remove(&after->preds, before);
	ptrset_remove(&before->true_succs, after);
	ptrset_remove(&after->true_preds, before);
	ptrset_remove(&before->min_succs, after);
	ptrset_remove(&after->min_preds, before);
}

/* tries to move before into instr, deleteing before if
 * it turns out to be empty
 */

bool lima_pp_lir_instr_combine_before(lima_pp_lir_scheduled_instr_t* before,
									  lima_pp_lir_scheduled_instr_t* instr)
{
	int i;
	
	unsigned const_map[8];
	unsigned alu_map[5];
	
	if (!try_create_const_map(instr, before, const_map))
		return false;
	
	if (before->branch_instr)
	{
		if (instr->varying_instr &&
			!lima_pp_lir_instr_can_swap(before->branch_instr,
										instr->varying_instr))
			return false;
		
		if (instr->texld_instr &&
			!lima_pp_lir_instr_can_swap(before->branch_instr,
										instr->texld_instr))
			return false;
		
		if (instr->uniform_instr &&
			!lima_pp_lir_instr_can_swap(before->branch_instr,
										instr->uniform_instr))
			return false;
		
		for (i = 0; i < 5; i++)
			if (instr->alu_instrs[i] &&
				!lima_pp_lir_instr_can_swap(before->branch_instr,
											instr->alu_instrs[i]))
				return false;
		
		if (instr->temp_store_instr &&
			!lima_pp_lir_instr_can_swap(before->branch_instr,
										instr->temp_store_instr))
			return false;
		
		if (instr->branch_instr)
			return false;
	}
	
	if (before->temp_store_instr)
	{
		if (instr->varying_instr &&
			!lima_pp_lir_instr_can_swap(before->temp_store_instr,
										instr->varying_instr))
			return false;
		
		if (instr->texld_instr &&
			!lima_pp_lir_instr_can_swap(before->temp_store_instr,
										instr->texld_instr))
			return false;
		
		if (instr->uniform_instr &&
			!lima_pp_lir_instr_can_swap(before->temp_store_instr,
										instr->uniform_instr))
			return false;
		
		for (i = 0; i < 5; i++)
			if (instr->alu_instrs[i] &&
				!lima_pp_lir_instr_can_swap(before->temp_store_instr,
											instr->alu_instrs[i]))
				return false;
		
		if (instr->temp_store_instr)
			return false;
	}
	
	unsigned cur_alu_pos = 5;
	
	for (i = 4; i >= 0; i--)
	{
		if (!before->alu_instrs[i])
			continue;
		
		if (instr->varying_instr &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[i],
										instr->varying_instr))
			return false;
		
		if (instr->texld_instr &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[i],
										instr->texld_instr))
			return false;
		
		if (instr->uniform_instr &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[i],
										instr->uniform_instr))
			return false;
		
		//Try to find a slot for the ALU instruction
		
		//First, figure out how far back we can go without dependency issues
		unsigned dep_index = 0;
		while (dep_index < cur_alu_pos &&
			   (!instr->alu_instrs[dep_index] ||
				lima_pp_lir_instr_can_swap(before->alu_instrs[i],
										  instr->alu_instrs[dep_index])))
			dep_index++;
		
		
		//Slots 0 and 1, as well as 2 and 3, run in parallel, so we maintain
		//the invariant that the two instructions cannot interfere with each
		//other (note, however, that if we combine two instructions *before*
		//register allocation, then it is possible that they will interfere
		//after register allocation, see below...)
		if (dep_index == 1 &&
			instr->alu_instrs[1] &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[i],
										instr->alu_instrs[1]))
		{
			dep_index = 0;
		}
		
		if (dep_index == 3 &&
			instr->alu_instrs[3] &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[i],
										instr->alu_instrs[3]))
		{
			dep_index = 2;
		}
		
		
		unsigned cur_index = dep_index;
		
		if (cur_index == 0)
			return false;
		
		cur_index--;
		
		//In some cases, we can have two instructions which must be excecuted
		//in parallel in slots 0 & 1 or 2 & 3, in which case they must both
		//be put in the corresponding slots of instr or else we will change
		//the meaning.
		
		if ((i == 0 || i == 1) && before->alu_instrs[0] && before->alu_instrs[1] &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[0], before->alu_instrs[1]))
		{
			if (cur_index < i || instr->alu_instrs[i])
				return false;
			
			alu_map[i] = i;
			cur_alu_pos = i;
			continue;
		}
		
		if ((i == 2 || i == 3) && before->alu_instrs[2] && before->alu_instrs[3] &&
			!lima_pp_lir_instr_can_swap(before->alu_instrs[2], before->alu_instrs[3]))
		{
			if (cur_index < i || instr->alu_instrs[i])
				return false;
			
			alu_map[i] = i;
			cur_alu_pos = i;
			continue;
		}
		
		while (true)
		{
			while (instr->alu_instrs[cur_index])
			{
				if (cur_index == 0)
					return false;
				
				cur_index--;
			}
			
			if (before->possible_alu_instr_pos[i][cur_index])
				break;
			
			if (cur_index == 0)
				return false;
			
			cur_index--;
		}
		
		alu_map[i] = cur_index;
		
		cur_alu_pos = cur_index;
	}
	
	if (before->uniform_instr)
	{
		if (instr->varying_instr &&
			!lima_pp_lir_instr_can_swap(before->uniform_instr,
										instr->varying_instr))
			return false;
		
		if (instr->texld_instr &&
			!lima_pp_lir_instr_can_swap(before->uniform_instr,
										instr->texld_instr))
			return false;
		
		if (instr->uniform_instr)
			return false;
	}
	
	if (before->texld_instr)
	{
		if (instr->varying_instr &&
			!lima_pp_lir_instr_can_swap(before->texld_instr,
										instr->varying_instr))
			return false;
		
		if (instr->texld_instr)
			return false;
	}
	
	if (before->varying_instr)
	{
		if (instr->varying_instr)
			return false;
	}
	
	apply_const_map(instr, before, const_map);
	
	if (before->branch_instr)
	{
		rewrite_consts(before->branch_instr, const_map);
		instr->branch_instr = before->branch_instr;
		before->branch_instr = NULL;
		instr->branch_instr->sched_instr = instr;
	}
	
	if (before->temp_store_instr)
	{
		rewrite_consts(before->temp_store_instr, const_map);
		instr->temp_store_instr = before->temp_store_instr;
		before->temp_store_instr = NULL;
		instr->temp_store_instr->sched_instr = instr;
	}
	
	for (i = 0; i < 5; i++)
	{
		if (before->alu_instrs[i])
		{
			rewrite_consts(before->alu_instrs[i], const_map);
			instr->alu_instrs[alu_map[i]] = before->alu_instrs[i];
			before->alu_instrs[i] = NULL;
			instr->alu_instrs[alu_map[i]]->sched_instr = instr;
		}
	}
	
	if (before->uniform_instr)
	{
			
		rewrite_consts(before->uniform_instr, const_map);
		instr->uniform_instr = before->uniform_instr;
		before->uniform_instr = NULL;
		instr->uniform_instr->sched_instr = instr;
	}
	
	if (before->texld_instr)
	{
		rewrite_consts(before->texld_instr, const_map);
		instr->texld_instr = before->texld_instr;
		before->texld_instr = NULL;
		instr->texld_instr->sched_instr = instr;
	}
	
	if (before->varying_instr)
	{
		rewrite_consts(before->varying_instr, const_map);
		instr->varying_instr = before->varying_instr;
		before->varying_instr = NULL;
		instr->varying_instr->sched_instr = instr;
	}
	
	remove_dep(before, instr);
	combine_deps(instr, before);
	
	return true;
}

/* tries to move as much of after into instr as possible, deleteing after if
 * it turns out to be empty
 */

bool lima_pp_lir_instr_combine_after(lima_pp_lir_scheduled_instr_t* after,
									 lima_pp_lir_scheduled_instr_t* instr)
{
	int i;	
	unsigned const_map[8];
	unsigned alu_map[5];
	
	if (!try_create_const_map(instr, after, const_map))
		return false;
	
	if (after->varying_instr)
	{
		if (instr->branch_instr &&
			!lima_pp_lir_instr_can_swap(instr->branch_instr,
										after->varying_instr))
			return false;
		
		if (instr->temp_store_instr &&
			!lima_pp_lir_instr_can_swap(instr->temp_store_instr,
										after->varying_instr))
			return false;
		
		for (i = 0; i < 5; i++)
			if (instr->alu_instrs[i] &&
				!lima_pp_lir_instr_can_swap(instr->alu_instrs[i],
											after->varying_instr))
				return false;
		
		if (instr->uniform_instr &&
			!lima_pp_lir_instr_can_swap(instr->uniform_instr,
										after->varying_instr))
			return false;
		
		if (instr->texld_instr &&
			!lima_pp_lir_instr_can_swap(instr->texld_instr,
										after->varying_instr))
			return false;
		
		if (instr->varying_instr)
			return false;
	}
	
	if (after->texld_instr)
	{
		if (instr->branch_instr &&
			!lima_pp_lir_instr_can_swap(instr->branch_instr,
										after->texld_instr))
			return false;
		
		if (instr->temp_store_instr &&
			!lima_pp_lir_instr_can_swap(instr->temp_store_instr,
									after->texld_instr))
			return false;
		
		for (i = 0; i < 5; i++)
			if (instr->alu_instrs[i] &&
				!lima_pp_lir_instr_can_swap(instr->alu_instrs[i],
											after->texld_instr))
				return false;
		
		if (instr->uniform_instr &&
			!lima_pp_lir_instr_can_swap(instr->uniform_instr,
										after->texld_instr))
			return false;
		
		if (instr->texld_instr)
			return false;
	}
	
	if (after->uniform_instr)
	{
		if (instr->branch_instr &&
			!lima_pp_lir_instr_can_swap(instr->branch_instr,
										after->uniform_instr))
			return false;
		
		if (instr->temp_store_instr &&
			!lima_pp_lir_instr_can_swap(instr->temp_store_instr,
										after->uniform_instr))
			return false;
		
		for (i = 0; i < 5; i++)
			if (instr->alu_instrs[i] &&
				!lima_pp_lir_instr_can_swap(instr->alu_instrs[i],
											after->uniform_instr))
				return false;
		
		if (instr->uniform_instr)
			return false;
	}
	
	int cur_alu_pos = -1;
	
	for (i = 0; i < 5; i++)
	{
		if (!after->alu_instrs[i])
			continue;
		
		if (instr->branch_instr &&
			!lima_pp_lir_instr_can_swap(instr->branch_instr,
										after->alu_instrs[i]))
			return false;
		
		if (instr->temp_store_instr &&
			!lima_pp_lir_instr_can_swap(instr->temp_store_instr,
										after->alu_instrs[i]))
			return false;
		
		//Try to find a slot for the ALU instruction
		
		//First, figure out how far forward we can go without dependency issues
		int dep_index = 4;
		while (dep_index > cur_alu_pos &&
			   (!instr->alu_instrs[dep_index] ||
				lima_pp_lir_instr_can_swap(instr->alu_instrs[dep_index],
										   after->alu_instrs[i])))
			dep_index--;
		
		if (dep_index == 0 &&
			instr->alu_instrs[0] &&
			!lima_pp_lir_instr_can_swap(instr->alu_instrs[0],
										after->alu_instrs[i]))
		{
			dep_index = 1;
		}
		
		if (dep_index == 2 &&
			instr->alu_instrs[2] &&
			!lima_pp_lir_instr_can_swap(instr->alu_instrs[2],
										after->alu_instrs[i]))
		{
			dep_index = 3;
		}
			
		
		unsigned cur_index = dep_index + 1;
		
		if (cur_index == 5)
			return false;
		
		if ((i == 0 || i == 1) && after->alu_instrs[0] && after->alu_instrs[1] &&
			!lima_pp_lir_instr_can_swap(after->alu_instrs[0], after->alu_instrs[1]))
		{
			if (cur_index > i || instr->alu_instrs[i])
				return false;
			
			alu_map[i] = i;
			cur_alu_pos = i;
			continue;
		}
		
		if ((i == 2 || i == 3) && after->alu_instrs[2] && after->alu_instrs[3] &&
			!lima_pp_lir_instr_can_swap(after->alu_instrs[2], after->alu_instrs[3]))
		{
			if (cur_index > i || instr->alu_instrs[i])
				return false;
			
			alu_map[i] = i;
			cur_alu_pos = i;
			continue;
		}
		
		while (true)
		{
			while (instr->alu_instrs[cur_index])
			{
				if (cur_index == 4)
					return false;
				
				cur_index++;
			}
			
			if (after->possible_alu_instr_pos[i][cur_index])
				break;
			
			if (cur_index == 4)
				return false;
			
			cur_index++;
		}
		
		alu_map[i] = cur_index;
		
		cur_alu_pos = cur_index;
	}
	
	if (after->temp_store_instr)
	{
		if (instr->branch_instr &&
			!lima_pp_lir_instr_can_swap(instr->branch_instr,
										after->temp_store_instr))
			return false;
		
		if (instr->temp_store_instr)
			return false;
	}
	
	if (after->branch_instr)
	{
		if (instr->branch_instr)
			return false;
	}
	
	apply_const_map(instr, after, const_map);
	
	if (after->varying_instr)
	{
		rewrite_consts(after->varying_instr, const_map);
		instr->varying_instr = after->varying_instr;
		after->varying_instr = NULL;
		instr->varying_instr->sched_instr = instr;
	}
	
	if (after->texld_instr)
	{
		rewrite_consts(after->texld_instr, const_map);
		instr->texld_instr = after->texld_instr;
		after->texld_instr = NULL;
		instr->texld_instr->sched_instr = instr;
	}
	
	if (after->uniform_instr)
	{
		rewrite_consts(after->uniform_instr, const_map);
		instr->uniform_instr = after->uniform_instr;
		after->uniform_instr = NULL;
		instr->uniform_instr->sched_instr = instr;
	}
	
	for (i = 0; i < 5; i++)
	{
		if (after->alu_instrs[i])
		{
			rewrite_consts(after->alu_instrs[i], const_map);
			instr->alu_instrs[alu_map[i]] = after->alu_instrs[i];
			after->alu_instrs[i] = NULL;
			instr->alu_instrs[alu_map[i]]->sched_instr = instr;
		}
	}
	
	if (after->temp_store_instr)
	{
		rewrite_consts(after->temp_store_instr, const_map);
		instr->temp_store_instr = after->temp_store_instr;
		after->temp_store_instr = NULL;
		instr->temp_store_instr->sched_instr = instr;
	}
	
	if (after->branch_instr)
	{
		rewrite_consts(after->branch_instr, const_map);
		instr->branch_instr = after->branch_instr;
		after->branch_instr = NULL;
		instr->branch_instr->sched_instr = instr;
	}
	
	remove_dep(instr, after);
	combine_deps(instr, after);
	
	return true;
}



/* tries to combine two instructions with no dependencies, i.e. individual
 * unscheduled instructions can go in any order
 */

bool lima_pp_lir_instr_combine_indep(lima_pp_lir_scheduled_instr_t* instr,
									 lima_pp_lir_scheduled_instr_t* other)
{
	int i;
	unsigned const_map[8];
	unsigned alu_map[5];
	
	if (!try_create_const_map(instr, other, const_map))
		return false;
	
	if (other->varying_instr)
	{
		if (instr->varying_instr)
			return false;
	}
	
	if (other->texld_instr)
	{
		if (other->texld_instr)
			return false;
	}
	
	if (other->uniform_instr)
	{
		if (instr->uniform_instr)
			return false;
	}
	
	unsigned cur_alu_pos = 5;
	
	for (i = 4; i >= 0; i--)
	{
		if (!other->alu_instrs[i])
			continue;
		
		//Try to find a slot for the ALU instruction
		
		unsigned cur_index = cur_alu_pos;
		
		if (cur_index == 0)
			return false;
		
		cur_index--;
		
		if ((i == 0 || i == 1) && other->alu_instrs[0] && other->alu_instrs[1] &&
			!lima_pp_lir_instr_can_swap(other->alu_instrs[0], other->alu_instrs[1]))
		{
			if (cur_index < i || instr->alu_instrs[i])
				return false;
			
			alu_map[i] = i;
			cur_alu_pos = i;
			continue;
		}
		
		if ((i == 2 || i == 3) && other->alu_instrs[2] && other->alu_instrs[3] &&
			!lima_pp_lir_instr_can_swap(other->alu_instrs[2], other->alu_instrs[3]))
		{
			if (cur_index < i || instr->alu_instrs[i])
				return false;
			
			alu_map[i] = i;
			cur_alu_pos = i;
			continue;
		}
		
		while (true)
		{
			while (instr->alu_instrs[cur_index])
			{
				if (cur_index == 0)
					return false;
				
				cur_index--;
			}
			
			if (other->possible_alu_instr_pos[i][cur_index])
				break;
			
			if (cur_index == 0)
				return false;
			
			cur_index--;
		}
		
		alu_map[i] = cur_index;
		
		cur_alu_pos = cur_index;
	}
	
	if (other->temp_store_instr && !instr->temp_store_instr)
	{
		if (instr->temp_store_instr)
			return false;
	}
	
	if (other->branch_instr && !instr->branch_instr)
	{
		if (instr->branch_instr)
			return false;
	}
	
	apply_const_map(instr, other, const_map);
	
	if (other->varying_instr)
	{
		rewrite_consts(other->varying_instr, const_map);
		instr->varying_instr = other->varying_instr;
		other->varying_instr = NULL;
		instr->varying_instr->sched_instr = instr;
	}
	
	if (other->texld_instr)
	{
		rewrite_consts(other->texld_instr, const_map);
		instr->texld_instr = other->texld_instr;
		other->texld_instr = NULL;
		instr->texld_instr->sched_instr = instr;
	}
	
	if (other->uniform_instr)
	{
		rewrite_consts(other->uniform_instr, const_map);
		instr->uniform_instr = other->uniform_instr;
		other->uniform_instr = NULL;
		instr->uniform_instr->sched_instr = instr;
	}
	
	for (i = 0; i < 5; i++)
	{
		if (other->alu_instrs[i])
		{
			rewrite_consts(other->alu_instrs[i], const_map);
			instr->alu_instrs[alu_map[i]] = other->alu_instrs[i];
			other->alu_instrs[i] = NULL;
			instr->alu_instrs[alu_map[i]]->sched_instr = instr;
		}
	}
	
	if (other->temp_store_instr)
	{
		rewrite_consts(other->temp_store_instr, const_map);
		instr->temp_store_instr = other->temp_store_instr;
		other->temp_store_instr = NULL;
		instr->temp_store_instr->sched_instr = instr;
	}
	
	if (other->branch_instr)
	{
		rewrite_consts(other->branch_instr, const_map);
		instr->branch_instr = other->branch_instr;
		other->branch_instr = NULL;
		instr->branch_instr->sched_instr = instr;
	}
	
	combine_deps(instr, other);
	
	return true;
}
