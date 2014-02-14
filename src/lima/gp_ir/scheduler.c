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
#include "priority_queue.h"
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>

/* helpers for dealing with instruction ranges
 *
 * When scheduling nodes, we end up with a set of constraints, each constraint
 * coming from a parent node. We need to find the intersection of these 
 * constraints if it exists, and insert move node(s) if it does not.
 */

typedef struct
{
	unsigned start_instr, end_instr; // end >= start
} instr_range_t;

// Merges new into dest, returning false if the two ranges do not overlap

static bool instr_range_union(instr_range_t* dest, instr_range_t new)
{
	bool ret = false;
	
	if (dest->start_instr <= new.start_instr &&
		new.start_instr <= dest->end_instr)
	{
		dest->start_instr = new.start_instr;
		ret = true;
	}
	
	if (dest->start_instr <= new.end_instr && new.end_instr <= dest->end_instr)
	{
		dest->end_instr = new.end_instr;
		ret = true;
	}
	
	if (new.start_instr <= dest->start_instr &&
		dest->start_instr <= new.end_instr)
	{
		ret = true;
	}
	
	if (new.start_instr <= dest->end_instr && dest->end_instr <= new.end_instr)
	{
		ret = true;
	}
	
	return ret;
}

static lima_gp_ir_instr_t* get_instr(lima_gp_ir_block_t* block, unsigned num)
{
	while (num >= block->num_instrs)
	{
		lima_gp_ir_instr_t* instr = lima_gp_ir_instr_create();
		if (!instr)
			return NULL;
		
		lima_gp_ir_instr_insert_start(block, instr);
	}
	
	lima_gp_ir_instr_t* cur_instr = gp_ir_block_first_instr(block);
	unsigned cur_num = block->num_instrs - 1;
	while (cur_num != num)
	{
		cur_instr = gp_ir_instr_next(cur_instr);
		cur_num--;
	}
	
	return cur_instr;
}

static bool try_insert_node(lima_gp_ir_node_t* node, bool* success)
{
	lima_gp_ir_block_t* block = node->successor->block;
	lima_gp_ir_instr_t* instr = get_instr(block, node->sched_instr);
	if (!instr)
		return false;
	
	// for complex1, we need to make sure there's space for complex2 and complex
	// ops in the instruction before
	
	if (node->op == lima_gp_ir_op_complex1 && !gp_ir_instr_is_start(instr))
	{
		lima_gp_ir_instr_t* prev_instr = gp_ir_instr_prev(instr);
		if (prev_instr->mul_slots[0] || prev_instr->complex_slot)
		{
			*success = false;
			return true;
		}
	}
	
	//Try to insert node
	
	*success = lima_gp_ir_instr_try_insert_node(instr, node);
	
	if (*success)
	{
		printf("placed node with op %s\n", lima_gp_ir_op[node->op].name);
		printf("\tsched_instr: %u, sched_pos: %u\n", node->sched_instr,
			   node->sched_pos);
	}
	
	return true;
}


// Finds a position for node.
// Success is set to false if no position could be found for node that satisfies
// all the constraints, in which case node is still scheduled and an
// intermediate move must be inserted.

static bool try_place_node(lima_gp_ir_node_t* node, bool* success)
{
	unsigned i, j;
	
	for (i = 0; i < lima_gp_ir_op[node->op].num_sched_positions; i++)
	{
		instr_range_t range = (instr_range_t) {
			.start_instr = 0,
			.end_instr = INT_MAX
		};
		
		node->sched_pos = i;
		
		lima_gp_ir_dep_info_t* dep_info;
		ptrset_iter_t iter = ptrset_iter_create(node->succs);
		bool was_successful = true;
		ptrset_iter_for_each(iter, dep_info)
		{
			instr_range_t new_range;
			new_range.start_instr = dep_info->succ->sched_instr +
				lima_gp_ir_dep_info_get_min_dist(dep_info);
			new_range.end_instr = dep_info->succ->sched_instr +
				lima_gp_ir_dep_info_get_max_dist(dep_info);
			if (!instr_range_union(&range, new_range))
			{
				was_successful = false;
				break;
			}
		}
		
		if (was_successful)
		{
			//We found a range that satisfies the constraints, try to insert the
			//node there.
			
			for (j = range.start_instr; j <= range.end_instr; j++)
			{
				node->sched_instr = j;
				bool insert_result;
				if (!try_insert_node(node, &insert_result))
					return false;
				if (insert_result)
				{
					*success = true;
					return true;
				}
			}
		}
	}
	
	// We couldn't find a position, so relax our requirements - now we'll only
	// look for positions that satisfy some of the constraints
	
	unsigned max_instr = 0;
	for (i = 0; i < lima_gp_ir_op[node->op].num_sched_positions; i++)
	{
		instr_range_t range = (instr_range_t) {
			.start_instr = 0,
			.end_instr = INT_MAX
		};
		
		lima_gp_ir_dep_info_t* dep_info;
		ptrset_iter_t iter = ptrset_iter_create(node->succs);
		ptrset_iter_for_each(iter, dep_info)
		{
			instr_range_t new_range;
			new_range.start_instr = dep_info->succ->sched_instr +
				lima_gp_ir_dep_info_get_min_dist(dep_info);
			new_range.end_instr = dep_info->succ->sched_instr +
				lima_gp_ir_dep_info_get_max_dist(dep_info);
			if (!instr_range_union(&range, new_range))
			{
				//Choose the greatest (earliest) range & keep going
				if (new_range.start_instr > range.start_instr)
					range = new_range;
			}
		}
		
		for (j = range.start_instr; j <= range.end_instr; j++)
		{
			node->sched_instr = j;
			bool insert_result;
			if (!try_insert_node(node, &insert_result))
				return false;
			if (insert_result)
			{
				*success = false;
				return true;
			}
		}
		
		if (range.end_instr > max_instr)
			max_instr = range.end_instr;
	}
	
	// Now we've exhausted every option, and it's impossible to satisfy any of
	// the constraints. Start at max_instr and keep going backwards until
	// the insertion succeeds - it has to succeed eventually, because we'll
	// hit a newly-created, empty instruction.
	
	node->sched_instr = max_instr + 1;
	
	while (true)
	{
		for (i = 0; i < lima_gp_ir_op[node->op].num_sched_positions; i++)
		{
			node->sched_pos = i;
			
			bool insert_result;
			if (!try_insert_node(node, &insert_result))
				return false;
			if (insert_result)
			{
				*success = false;
				return true;
			}
		}
		
		node->sched_instr++;
	}
	
	return false;
}

static bool insert_move(lima_gp_ir_node_t* node, lima_gp_ir_node_t** new_node)
{
	lima_gp_ir_alu_node_t* move_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mov);
	if (!move_node)
		return false;
	
	move_node->dest_negate = false;
	move_node->children[0] = node;
	move_node->children_negate[0] = false;
	move_node->node.index = 0;
	
	// We need this first so that node does not get deleted
	lima_gp_ir_node_link(&move_node->node, node);
	
	//For each successor for which the constraints were not satisfied...
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_t iter = ptrset_iter_create(node->succs);
	ptrset_iter_for_each(iter, dep_info)
	{
		unsigned start_instr = dep_info->succ->sched_instr +
			lima_gp_ir_dep_info_get_min_dist(dep_info);
		unsigned end_instr = dep_info->succ->sched_instr +
			lima_gp_ir_dep_info_get_max_dist(dep_info);
		assert(node->sched_instr >= start_instr);
		if (node->sched_instr > end_instr)
		{
			//Move the node so it gets its input from the move node
			lima_gp_ir_child_node_iter_t child_iter;
			gp_ir_node_for_each_child(dep_info->succ, child_iter)
			{
				if (*child_iter.child == node)
					*child_iter.child = &move_node->node;
			}
			
			lima_gp_ir_node_link(dep_info->succ, &move_node->node);
			lima_gp_ir_node_unlink(dep_info->succ, node);
			
			lima_gp_ir_dep_info_t* new_dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!new_dep_info)
				return false;
			
			new_dep_info->pred = &move_node->node;
			new_dep_info->succ = dep_info->succ;
			new_dep_info->is_child_dep = true;
			new_dep_info->is_offset = dep_info->is_offset;
			if (!lima_gp_ir_dep_info_insert(new_dep_info))
				return false;
			
			lima_gp_ir_dep_info_delete(dep_info);
		}
	}
	
	lima_gp_ir_dep_info_t* new_dep_info =
	malloc(sizeof(lima_gp_ir_dep_info_t));
	if (!new_dep_info)
		return false;
	
	new_dep_info->pred = node;
	new_dep_info->succ = &move_node->node;
	new_dep_info->is_child_dep = true;
	new_dep_info->is_offset = false;
	if (!lima_gp_ir_dep_info_insert(new_dep_info))
		return false;
	
	*new_node = &move_node->node;
	
	return true;
}

static bool try_place_move_node(lima_gp_ir_node_t* node,
								lima_gp_ir_node_t* child,
								bool* success, bool* done)
{
	unsigned i, j;
	
	node->sched_pos = 0;
	
	lima_gp_ir_dep_info_t temp_dep_info = (lima_gp_ir_dep_info_t){
		.pred = child,
		.succ = node,
		.is_child_dep = true,
		.is_offset = false
	};
	
	//Calculate the constraints imposed by the child of the move node
	//Note: these constraints are not affected by where the move node is
	//scheduled, this is guarenteed by the ISA
	int start_instr = child->sched_instr -
		(int) lima_gp_ir_dep_info_get_max_dist(&temp_dep_info);
	int end_instr = child->sched_instr -
		(int) lima_gp_ir_dep_info_get_min_dist(&temp_dep_info);
	if (start_instr < 0)
		start_instr = 0;
	if (end_instr < 0)
	{
		*success = false;
		return true;
	}
	
	for (i = start_instr; i <= end_instr; i++)
	{
		node->sched_instr = i;
		for (j = 0; j < 6; j++)
		{
			node->sched_pos = j;
			*done = true;
			
			lima_gp_ir_dep_info_t* dep_info;
			ptrset_iter_t iter = ptrset_iter_create(node->succs);
			ptrset_iter_for_each(iter, dep_info)
			{
				unsigned start_instr = dep_info->succ->sched_instr +
					lima_gp_ir_dep_info_get_min_dist(dep_info);
				if (i < start_instr)
					goto break_outer; //We can't schedule moves too late
				
				unsigned end_instr = dep_info->succ->sched_instr +
					lima_gp_ir_dep_info_get_max_dist(dep_info);
				if (i > end_instr)
					*done = false; //We can schedule moves too early, but then
								   //we need to add another move
			}
			
			bool insert_result;
			if (!try_insert_node(node, &insert_result))
				return false;
			if (insert_result)
			{
				*success = true;
				return true;
			}
			
			break_outer:
			;
		}
	}
	
	*success = false;
	return true;
}

/* eliminates each move node in the set & fixes up scheduling info */

static bool undo_moves(ptrset_t* moves)
{
	lima_gp_ir_node_t* node;
	ptrset_iter_t iter = ptrset_iter_create(*moves);
	ptrset_iter_for_each(iter, node)
	{
		lima_gp_ir_instr_t* instr = get_instr(node->successor->block,
											  node->sched_instr);
		lima_gp_ir_instr_remove_alu_node(instr, node);
		/*printf("removing node:\n");
		printf("\tsched_instr: %u, sched_pos: %u\n", node->sched_instr,
			   node->sched_pos);*/
		
		lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
		lima_gp_ir_node_t* child = alu_node->children[0];
		
		ptrset_t succs;
		if (!ptrset_copy(&succs, node->succs))
			return false;
		
		lima_gp_ir_dep_info_t* dep_info;
		ptrset_iter_t succ_iter = ptrset_iter_create(succs);
		ptrset_iter_for_each(succ_iter, dep_info)
		{
			lima_gp_ir_child_node_iter_t child_iter;
			gp_ir_node_for_each_child(dep_info->succ, child_iter)
			{
				if (*child_iter.child == node)
					*child_iter.child = child;
			}
			
			lima_gp_ir_dep_info_t* new_dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!new_dep_info)
				return false;
			
			new_dep_info->pred = child;
			new_dep_info->succ = dep_info->succ;
			new_dep_info->is_child_dep = true;
			new_dep_info->is_offset = dep_info->is_offset;
			if (!lima_gp_ir_dep_info_insert(new_dep_info))
				return false;
			
			lima_gp_ir_node_link(dep_info->succ, child);
			lima_gp_ir_node_unlink(dep_info->succ, node);
		}
		
		ptrset_delete(succs);
		ptrset_remove(moves, node);
	}
	
	return true;
}

/* tries to thread move nodes between node and its parents */

static bool try_thread_move_nodes(lima_gp_ir_node_t* node,
								  ptrset_t* moves_inserted, bool* success)
{
	lima_gp_ir_node_t* cur_node = node;
	
	while (true)
	{
		lima_gp_ir_node_t* move_node;
		if (!insert_move(cur_node, &move_node))
			return false;
		
		ptrset_add(moves_inserted, (void*)move_node);
		
		bool done, result;
		if (!try_place_move_node(move_node, cur_node, &result, &done))
			return false;
		
		if (!result)
		{
			*success = false;
			return true;
		}
		
		if (done)
		{
			*success = true;
			return true;
		}
		
		cur_node = move_node;
	}
	
	return false;
}

/* tries to schedule a node, placing it and then inserting any intermediate
 * moves necessary.
 *
 * If scheduling fails, sets success to false and doesn't clean up the inserted
 * moves (but adds them to the moves_inserted structure). Used internally by
 * sched_reg_reads() and by try_schedule_node().
 */

static bool try_schedule_node_impl(lima_gp_ir_node_t* node,
								   ptrset_t* moves_inserted, bool* success)
{
	bool result;
	if (!try_place_node(node, &result))
		return false;
	
	if (result)
	{
		*success = true;
		return true;
	}
	
	return try_thread_move_nodes(node, moves_inserted, success);
}

/* Spilling to registers */

static bool is_scheduled_alu(lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_load_uniform:
		case lima_gp_ir_op_load_temp:
		case lima_gp_ir_op_load_attribute:
		case lima_gp_ir_op_load_reg:
			return false;
			
		default:
			break;
	}
	
	return true;
}

static bool is_store_dep(lima_gp_ir_dep_info_t* dep_info)
{
	switch (dep_info->succ->op)
	{
		case lima_gp_ir_op_store_reg:
		case lima_gp_ir_op_store_varying:
			return true;
			
		case lima_gp_ir_op_store_temp:
			return !dep_info->is_offset;
			
		default:
			break;
	}
	
	return false;
}

/* Makes the given node output its result to a register, and then has all the
 * parents/successors that can't be scheduled directly read from the same
 * register.
 */

static bool insert_reg(lima_gp_ir_node_t* node,
					   lima_gp_ir_node_t** new_store_node,
					   ptrset_t* new_load_nodes,
					   ptrset_t* new_move_nodes,
					   lima_gp_ir_node_t** new_move_node,
					   lima_gp_ir_reg_t** new_reg)
{
	lima_gp_ir_root_node_t* old_successor = node->successor;
	
	lima_gp_ir_alu_node_t* move_node;
	
	if (!is_scheduled_alu(node))
	{
		move_node = lima_gp_ir_alu_node_create(lima_gp_ir_op_mov);
		if (!move_node)
			return false;
		
		move_node->dest_negate = false;
		move_node->children[0] = node;
		move_node->children_negate[0] = false;
		move_node->node.index = 0;
		
		lima_gp_ir_dep_info_t* dep_info = malloc(sizeof(lima_gp_ir_dep_info_t));
		if (!dep_info)
		{
			lima_gp_ir_node_delete(&move_node->node);
			return false;
		}
		
		dep_info->pred = node;
		dep_info->succ = &move_node->node;
		dep_info->is_child_dep = true;
		dep_info->is_offset = false;
		if (!lima_gp_ir_dep_info_insert(dep_info))
		{
			lima_gp_ir_node_delete(&move_node->node);
			free(dep_info);
			return false;
		}
		
		*new_move_node = &move_node->node;
		
		lima_gp_ir_node_link(&move_node->node, node);
	}
	else
		*new_move_node = NULL;
	
	lima_gp_ir_reg_t* reg = lima_gp_ir_reg_create(node->successor->block->prog);
	if (!reg)
		return false;
	reg->size = 1;
	*new_reg = reg;
	
	lima_gp_ir_store_reg_node_t* store_reg_node =
		lima_gp_ir_store_reg_node_create();
	if (!store_reg_node)
		return false;
	
	store_reg_node->reg = reg;
	store_reg_node->mask[0] = true;
	store_reg_node->mask[1] = false;
	store_reg_node->mask[2] = false;
	store_reg_node->mask[3] = false;
	if (is_scheduled_alu(node))
		store_reg_node->children[0] = node;
	else
		store_reg_node->children[0] = &move_node->node;
	store_reg_node->root_node.node.index = 0;
	
	lima_gp_ir_block_insert_before(&store_reg_node->root_node,
								   node->successor);
	
	lima_gp_ir_dep_info_t* dep_info = malloc(sizeof(lima_gp_ir_dep_info_t));
	if (!dep_info)
		return false;
	
	dep_info->pred = store_reg_node->children[0];
	dep_info->succ = &store_reg_node->root_node.node;
	dep_info->is_child_dep = true;
	dep_info->is_offset = false;
	if (!lima_gp_ir_dep_info_insert(dep_info))
	{
		free(dep_info);
		return false;
	}
	
	lima_gp_ir_node_link(&store_reg_node->root_node.node,
						 store_reg_node->children[0]);
	
	*new_store_node = &store_reg_node->root_node.node;
	
	ptrset_t succs;
	if (!ptrset_copy(&succs, node->succs))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(succs);
	ptrset_iter_for_each(iter, dep_info)
	{
		if (dep_info->succ == &store_reg_node->root_node.node ||
			(!is_scheduled_alu(node) && dep_info->succ == &move_node->node))
			continue;
		
		unsigned distance = node->sched_instr - dep_info->succ->sched_instr;
		if (distance <= lima_gp_ir_dep_info_get_max_dist(dep_info))
		{
			//This node can be scheduled directly, so don't make it use a
			//register load
			continue;
		}
		
		lima_gp_ir_load_reg_node_t* load_reg_node
		= lima_gp_ir_load_reg_node_create();
		if (!load_reg_node)
			return false;
		
		load_reg_node->reg = reg;
		load_reg_node->component = 0;
		load_reg_node->offset = NULL;
		load_reg_node->node.index = 0;
		
		lima_gp_ir_node_t* child_node = &load_reg_node->node;
		if (is_store_dep(dep_info))
		{
			lima_gp_ir_alu_node_t* move_node =
				lima_gp_ir_alu_node_create(lima_gp_ir_op_mov);
			if (!move_node)
			{
				lima_gp_ir_node_delete(&load_reg_node->node);
				return false;
			}
			
			child_node = &move_node->node;
			
			move_node->dest_negate = false;
			move_node->children[0] = &load_reg_node->node;
			move_node->children_negate[0] = false;
			move_node->node.index = 0;
			
			lima_gp_ir_dep_info_t* new_dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!new_dep_info)
			{
				lima_gp_ir_node_delete(&move_node->node);
				lima_gp_ir_node_delete(&load_reg_node->node);
				return false;
			}
			
			new_dep_info->pred = &load_reg_node->node;
			new_dep_info->succ = &move_node->node;
			new_dep_info->is_child_dep = true;
			new_dep_info->is_offset = false;
			if (!lima_gp_ir_dep_info_insert(new_dep_info))
			{
				lima_gp_ir_node_delete(&move_node->node);
				lima_gp_ir_node_delete(&load_reg_node->node);
				free(dep_info);
				return false;
			}
			
			lima_gp_ir_node_link(&move_node->node, &load_reg_node->node);
			
			ptrset_add(new_move_nodes, &move_node->node);
		}
		
		lima_gp_ir_child_node_iter_t child_iter;
		gp_ir_node_for_each_child(dep_info->succ, child_iter)
		{
			if (*child_iter.child == node)
				*child_iter.child = child_node;
		}
		
		lima_gp_ir_dep_info_t* new_dep_info =
			malloc(sizeof(lima_gp_ir_dep_info_t));
		if (!new_dep_info)
			return false;
		
		new_dep_info->pred = child_node;
		new_dep_info->succ = dep_info->succ;
		new_dep_info->is_child_dep = true;
		new_dep_info->is_offset = dep_info->is_offset;
		if (!lima_gp_ir_dep_info_insert(new_dep_info))
		{
			free(new_dep_info);
			return false;
		}
		
		lima_gp_ir_node_link(dep_info->succ, child_node);
		lima_gp_ir_node_unlink(dep_info->succ, node);
		
		//lima_gp_ir_dep_info_delete(dep_info);
		
		new_dep_info = malloc(sizeof(lima_gp_ir_dep_info_t));
		if (!new_dep_info)
			return false;
		
		new_dep_info->pred = &store_reg_node->root_node.node;
		new_dep_info->succ = &load_reg_node->node;
		new_dep_info->is_child_dep = false;
		new_dep_info->is_offset = false;
		if (!lima_gp_ir_dep_info_insert(new_dep_info))
		{
			free(new_dep_info);
			return false;
		}
		
		ptrset_add(new_load_nodes, &load_reg_node->node);
	}
	
	ptrset_delete(succs);
	
	return lima_gp_ir_liveness_compute_node(old_successor,
		store_reg_node->root_node.live_phys_after, false);
}

static bool sched_reg_reads(ptrset_t load_reg_nodes, ptrset_t move_nodes,
							ptrset_t* moves_inserted,
							bool* success)
{
	//Schedule moves first
	//We know that they must be scheduled in the same instruction as their
	//parent nodes, which are store nodes
	ptrset_iter_t iter = ptrset_iter_create(move_nodes);
	lima_gp_ir_node_t* move_node;
	ptrset_iter_for_each(iter, move_node)
	{
		lima_gp_ir_node_t* parent = ptrset_first(move_node->parents);
		
		move_node->sched_instr = parent->sched_instr;
		unsigned i;
		for (i = 0; i < 6; i++)
		{
			move_node->sched_pos = i;
			bool result;
			if (!try_insert_node(move_node, &result))
				return false;
			if (result)
				break;
		}
		
		if (i == 6)
		{
			*success = false;
			return true;
		}
	}
	
	iter = ptrset_iter_create(load_reg_nodes);
	lima_gp_ir_node_t* load_reg_node;
	ptrset_iter_for_each(iter, load_reg_node)
	{
		bool result;
		if (!try_schedule_node_impl(load_reg_node, moves_inserted, &result))
			return false;
		if (!result)
		{
			*success = false;
			return true;
		}
	}
	
	*success = true;
	return true;
}

static bool sched_reg_write(lima_gp_ir_node_t* node,
							lima_gp_ir_node_t* move_node,
							lima_gp_ir_node_t* child_node,
							ptrset_t* moves_inserted, bool* success)
{
	unsigned end_instr = child_node->sched_instr;
	unsigned start_instr = 0;
	
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_t iter = ptrset_iter_create(node->succs);
	ptrset_iter_for_each(iter, dep_info)
	{
		unsigned new_start_instr = dep_info->succ->sched_instr +
			lima_gp_ir_dep_info_get_min_dist(dep_info);
		if (new_start_instr > start_instr)
			start_instr = new_start_instr;
	}
	
	node->sched_pos = 0; //Only one possible position for writes...
	
	for (node->sched_instr = end_instr; node->sched_instr >= start_instr;
		 node->sched_instr--)
	{
		bool result;
		if (!try_insert_node(node, &result))
			return false;
		if (!result)
			continue;
		
		if (move_node)
		{
			move_node->sched_instr = node->sched_instr;
			unsigned i;
			for (i = 0; i < 6; i++)
			{
				move_node->sched_pos = i;
				bool result;
				if (!try_insert_node(move_node, &result))
					return false;
				if (result)
					break;
			}
			
			if (i == 6)
			{
				assert(0);
				return false;
			}
		}
		
		lima_gp_ir_dep_info_t temp_dep_info = {
			.succ = move_node ? move_node : node,
			.pred = child_node,
			.is_child_dep = true,
			.is_offset = true
		};
		
		unsigned end_instr = temp_dep_info.succ->sched_instr +
			lima_gp_ir_dep_info_get_max_dist(&temp_dep_info);
		
		if (child_node->sched_instr > end_instr)
		{
			//The child node and the store node are too far apart,
			//Need to thread move nodes
			bool result;
			if (!try_thread_move_nodes(child_node, moves_inserted, &result))
				return false;
			if (!result)
			{
				*success = false;
				return true;
			}
		}
		
		*success = true;
		return true;
	}
	
	*success = false;
	return true;
}

static bool try_schedule_reg(lima_gp_ir_node_t* node, bitset_t free_regs,
							 bool* success)
{
	lima_gp_ir_node_t* reg_store_node, *move_node;
	lima_gp_ir_reg_t* reg;
	ptrset_t load_reg_nodes;
	if (!ptrset_create(&load_reg_nodes))
		return false;

	ptrset_t move_nodes;
	if (!ptrset_create(&move_nodes))
	{
		ptrset_delete(load_reg_nodes);
		return false;
	}

	ptrset_t moves_inserted;
	if (!ptrset_create(&moves_inserted))
	{
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		return false;
	}
	
	if (!insert_reg(node, &reg_store_node, &load_reg_nodes, &move_nodes,
					&move_node, &reg))
	{
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		ptrset_delete(moves_inserted);
		return false;
	}
	
	if (!lima_gp_ir_regalloc_scalar_fast(reg, free_regs))
	{
		*success = false;
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		ptrset_delete(moves_inserted);
		assert(0);
		return true;
	}
	
	bool result;
	if (!sched_reg_reads(load_reg_nodes, move_nodes, &moves_inserted, &result))
	{
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		ptrset_delete(moves_inserted);
		return false;
	}
	if (!result)
	{
		undo_moves(&moves_inserted);
		*success = false;
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		ptrset_delete(moves_inserted);
		return true;
	}
	
	if (!sched_reg_write(reg_store_node, move_node, node, &moves_inserted,
						 &result))
	{
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		ptrset_delete(moves_inserted);
		return false;
	}
	if (!result)
	{
		undo_moves(&moves_inserted);
		*success = false;
		ptrset_delete(load_reg_nodes);
		ptrset_delete(move_nodes);
		ptrset_delete(moves_inserted);
		return true;
	}
	
	ptrset_delete(load_reg_nodes);
	ptrset_delete(move_nodes);
	ptrset_delete(moves_inserted);
	
	*success = true;
	return true;
}

static bool try_schedule_node(lima_gp_ir_node_t* node, bitset_t free_regs,
							  ptrset_t* new_moves_inserted, bool* success)
{
	assert(node->op != lima_gp_ir_op_const);
	
	ptrset_t moves_inserted;
	if (!ptrset_create(&moves_inserted))
		return false;
	
	if (!try_schedule_node_impl(node, &moves_inserted, success))
	{
		ptrset_delete(moves_inserted);
		return false;
	}
	
	if (!*success)
	{
		if (!undo_moves(&moves_inserted))
		{
			ptrset_delete(moves_inserted);
			return false;
		}
		
		if (!try_schedule_reg(node, free_regs, success))
		{
			ptrset_delete(moves_inserted);
			return false;
		}
	}
	else
		ptrset_union(new_moves_inserted, moves_inserted);
		
	
	ptrset_delete(moves_inserted);
	return true;
}

static bool compare_nodes(void* elem1, void* elem2)
{
	lima_gp_ir_node_t* node1 = (lima_gp_ir_node_t*) elem1;
	lima_gp_ir_node_t* node2 = (lima_gp_ir_node_t*) elem2;
	
	//We must schedule complex2 and complex ops directly after complex1 -
	//we already reserved space for them in try_insert_node()
	
	if (node1->op == lima_gp_ir_op_complex1  ||
		node1->op == lima_gp_ir_op_exp2_impl ||
		node1->op == lima_gp_ir_op_log2_impl ||
		node1->op == lima_gp_ir_op_rcp_impl  ||
		node1->op == lima_gp_ir_op_rsqrt_impl)
		return true;
	
	if (node2->op == lima_gp_ir_op_complex1  ||
		node2->op == lima_gp_ir_op_exp2_impl ||
		node2->op == lima_gp_ir_op_log2_impl ||
		node2->op == lima_gp_ir_op_rcp_impl  ||
		node2->op == lima_gp_ir_op_rsqrt_impl)
		return false;
	
	// Next up - loads
	// If we scheduled them according to the heuristic below, they would
	// always be last - but that's clearly not a very good idea,
	// as we would not want to spill the result of a load into a register
	
	if (node1->op == lima_gp_ir_op_load_uniform   ||
		node1->op == lima_gp_ir_op_load_temp      ||
		node1->op == lima_gp_ir_op_load_attribute ||
		node1->op == lima_gp_ir_op_load_reg)
		return true;
	
	if (node2->op == lima_gp_ir_op_load_uniform   ||
		node2->op == lima_gp_ir_op_load_temp      ||
		node2->op == lima_gp_ir_op_load_attribute ||
		node2->op == lima_gp_ir_op_load_reg)
		return false;
	
	//Use a heuristic based on the node's maximum distance from the beginning
	int node1_value = node1->max_dist, node2_value = node2->max_dist;
	if (node1->op == lima_gp_ir_op_store_reg ||
		node1->op == lima_gp_ir_op_store_temp ||
		node1->op == lima_gp_ir_op_store_varying)
		node1_value -= 1;
	if (node2->op == lima_gp_ir_op_store_reg ||
		node2->op == lima_gp_ir_op_store_temp ||
		node2->op == lima_gp_ir_op_store_varying)
		node2_value -= 1;
	
	
	
	return node1_value > node2_value;
}

static bool succs_processed(lima_gp_ir_node_t* node, ptrset_t processed)
{
	ptrset_iter_t node_iter = ptrset_iter_create(node->succs);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(node_iter, dep_info)
	{
		if (!ptrset_contains(processed, dep_info->succ))
			return false;
	}
	
	return true;
}

static bool number_node_cb(lima_gp_ir_node_t* node, void* state)
{
	unsigned* count = state;
	
	node->index = (*count)++;
	return true;
}

static bool schedule_block(lima_gp_ir_block_t* block, ptrset_t* moves_inserted,
						   bool* success)
{
	unsigned count = 0;
	lima_gp_ir_root_node_t* root_node;
	gp_ir_block_for_each_node(block, root_node)
	{
		lima_gp_ir_node_dfs(&root_node->node, NULL, number_node_cb,
							(void*)&count);
	}
	
	priority_queue_t* queue = priority_queue_create(compare_nodes);
	if (!queue)
		return false;
	
	ptrset_t processed_nodes;
	if (!ptrset_create(&processed_nodes))
	{
		priority_queue_delete(queue);
		return false;
	}
	
	bitset_t free_regs = lima_gp_ir_regalloc_get_free_regs(block);
	
	ptrset_iter_t iter = ptrset_iter_create(block->end_nodes);
	lima_gp_ir_node_t* node;
	ptrset_iter_for_each(iter, node)
	{
		priority_queue_push(queue, (void*)node);
	}
	
	while (priority_queue_num_elems(queue))
	{
		lima_gp_ir_node_t* node = priority_queue_pull(queue);
		
		bool result;
		if (!try_schedule_node(node, free_regs, moves_inserted, &result))
			return false;
		
		if (!result)
		{
			*success = false;
			return true;
		}
		
		printf("processed node %u\n", node->index);
		
		ptrset_add(&processed_nodes, node);
		
		//Check if any predecessors are now processable
		lima_gp_ir_dep_info_t* dep_info;
		iter = ptrset_iter_create(node->preds);
		ptrset_iter_for_each(iter, dep_info)
			if (succs_processed(dep_info->pred, processed_nodes))
				priority_queue_push(queue, (void*)dep_info->pred);
	}
	
	priority_queue_delete(queue);
	ptrset_delete(processed_nodes);
	bitset_delete(free_regs);
	
	*success = true;
	return true;
}

static void delete_instrs(lima_gp_ir_block_t* block)
{
	while (block->num_instrs > 0)
		lima_gp_ir_instr_delete(gp_ir_block_first_instr(block));
}

bool lima_gp_ir_schedule_block(lima_gp_ir_block_t* block)
{
	ptrset_t moves_inserted;
	if (!ptrset_create(&moves_inserted))
		return false;
	
	bool result = false;
	while (!result)
	{
		if (!lima_gp_ir_block_calc_crit_path(block))
			return false;
		if (!schedule_block(block, &moves_inserted, &result))
			return false;
		if (!result)
		{
			if (!undo_moves(&moves_inserted))
				return false;
			
			ptrset_empty(&moves_inserted);
			
			delete_instrs(block);
			printf("\nrestarting...\n\n");
		}
	}
	
	ptrset_delete(moves_inserted);
	return true;
}

bool lima_gp_ir_schedule_prog(lima_gp_ir_prog_t* prog)
{
	if (!lima_gp_ir_liveness_compute_prog(prog, false))
		return false;
	
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		if (!lima_gp_ir_schedule_block(block))
			return false;
	}
	
	return true;
}
