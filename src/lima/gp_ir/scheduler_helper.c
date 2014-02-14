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
#include <assert.h>
#include <limits.h>

static unsigned get_min_dist_alu(lima_gp_ir_dep_info_t* dep_info)
{
	switch (dep_info->pred->op)
	{
		case lima_gp_ir_op_load_uniform:
		case lima_gp_ir_op_load_temp:
		case lima_gp_ir_op_load_attribute:
		case lima_gp_ir_op_load_reg:
			return 0;
			
		case lima_gp_ir_op_complex1:
			return 2;
			
		default:
			break;
	}
	
	return 1;
}

unsigned lima_gp_ir_dep_info_get_min_dist(lima_gp_ir_dep_info_t* dep_info)
{
	if (dep_info->is_child_dep)
	{
		switch (dep_info->succ->op)
		{
			case lima_gp_ir_op_store_temp:
				if (dep_info->is_offset)
					return get_min_dist_alu(dep_info);
				else
					return 0;
				
			case lima_gp_ir_op_store_reg:
			case lima_gp_ir_op_store_varying:
				return 0;
				
			case lima_gp_ir_op_mov:
			case lima_gp_ir_op_mul:
			case lima_gp_ir_op_select:
			case lima_gp_ir_op_complex1:
			case lima_gp_ir_op_complex2:
			case lima_gp_ir_op_add:
			case lima_gp_ir_op_floor:
			case lima_gp_ir_op_sign:
			case lima_gp_ir_op_ge:
			case lima_gp_ir_op_lt:
			case lima_gp_ir_op_min:
			case lima_gp_ir_op_max:
			case lima_gp_ir_op_neg:
			case lima_gp_ir_op_clamp_const:
			case lima_gp_ir_op_preexp2:
			case lima_gp_ir_op_postlog2:
			case lima_gp_ir_op_exp2_impl:
			case lima_gp_ir_op_log2_impl:
			case lima_gp_ir_op_rcp_impl:
			case lima_gp_ir_op_rsqrt_impl:
			case lima_gp_ir_op_branch_cond:
			case lima_gp_ir_op_store_temp_load_off0:
			case lima_gp_ir_op_store_temp_load_off1:
			case lima_gp_ir_op_store_temp_load_off2:
				return get_min_dist_alu(dep_info);
				
			default:
				assert(0);
		}
	}
	else
	{
		if (dep_info->pred->op == lima_gp_ir_op_store_temp &&
			dep_info->succ->op == lima_gp_ir_op_load_temp)
			return 4;
		else if (dep_info->pred->op == lima_gp_ir_op_store_reg &&
				 dep_info->succ->op == lima_gp_ir_op_load_reg)
			return 3;
		else if ((dep_info->pred->op == lima_gp_ir_op_store_temp_load_off0 ||
				  dep_info->pred->op == lima_gp_ir_op_store_temp_load_off1 ||
				  dep_info->pred->op == lima_gp_ir_op_store_temp_load_off2) &&
				 dep_info->succ->op == lima_gp_ir_op_load_uniform)
			return 4;
		else
			return 1;
	}
	
	return 0;
}

static bool is_sched_complex(lima_gp_ir_node_t* node)
{
	if (node->op == lima_gp_ir_op_exp2_impl  ||
		node->op == lima_gp_ir_op_log2_impl  ||
		node->op == lima_gp_ir_op_rcp_impl   ||
		node->op == lima_gp_ir_op_rsqrt_impl ||
		node->op == lima_gp_ir_op_store_temp_load_off0 ||
		node->op == lima_gp_ir_op_store_temp_load_off1 ||
		node->op == lima_gp_ir_op_store_temp_load_off2)
		return true;
	
	if (node->op == lima_gp_ir_op_mov && node->sched_pos == 4)
		return true;
	
	return false;
}

static unsigned get_max_dist_alu(lima_gp_ir_dep_info_t* dep_info)
{
	if (dep_info->pred->op == lima_gp_ir_op_load_uniform ||
		dep_info->pred->op == lima_gp_ir_op_load_temp)
		return 0;
	if (dep_info->pred->op == lima_gp_ir_op_load_attribute)
		return 1;
	if (dep_info->pred->op == lima_gp_ir_op_load_reg)
	{
		if (dep_info->pred->sched_pos == 0)
			return 0;
		else
			return 1;
	}
	if (dep_info->succ->op == lima_gp_ir_op_complex1)
		return 1;
	if (is_sched_complex(dep_info->pred))
		return 1;
	return 2;
}

unsigned lima_gp_ir_dep_info_get_max_dist(lima_gp_ir_dep_info_t* dep_info)
{
	if (dep_info->is_child_dep)
	{
		switch (dep_info->succ->op)
		{
			case lima_gp_ir_op_store_temp:
				if (dep_info->is_offset)
					return get_max_dist_alu(dep_info);
				else
					return 0;
				
			case lima_gp_ir_op_store_reg:
			case lima_gp_ir_op_store_varying:
				return 0;
				
			case lima_gp_ir_op_mov:
			case lima_gp_ir_op_mul:
			case lima_gp_ir_op_select:
			case lima_gp_ir_op_complex1:
			case lima_gp_ir_op_complex2:
			case lima_gp_ir_op_add:
			case lima_gp_ir_op_floor:
			case lima_gp_ir_op_sign:
			case lima_gp_ir_op_ge:
			case lima_gp_ir_op_lt:
			case lima_gp_ir_op_min:
			case lima_gp_ir_op_max:
			case lima_gp_ir_op_neg:
			case lima_gp_ir_op_clamp_const:
			case lima_gp_ir_op_preexp2:
			case lima_gp_ir_op_postlog2:
			case lima_gp_ir_op_exp2_impl:
			case lima_gp_ir_op_log2_impl:
			case lima_gp_ir_op_rcp_impl:
			case lima_gp_ir_op_rsqrt_impl:
			case lima_gp_ir_op_branch_cond:
			case lima_gp_ir_op_store_temp_load_off0:
			case lima_gp_ir_op_store_temp_load_off1:
			case lima_gp_ir_op_store_temp_load_off2:
				return get_max_dist_alu(dep_info);
				
			default:
				assert(0);
		}
	}
	
	return INT_MAX >> 2; //Don't want to overflow...
}

static bool preds_processed(lima_gp_ir_node_t* node, ptrset_t processed)
{
	ptrset_iter_t node_iter = ptrset_iter_create(node->preds);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(node_iter, dep_info)
	{
		if (!ptrset_contains(processed, dep_info->pred))
			return false;
	}
	
	return true;
}

bool lima_gp_ir_block_calc_crit_path(lima_gp_ir_block_t* block)
{
	ptrset_t processing_nodes;
	if (!ptrset_copy(&processing_nodes, block->start_nodes))
		return false;
	
	ptrset_t processed_nodes;
	if (!ptrset_create(&processed_nodes))
	{
		ptrset_delete(processing_nodes);
		return false;
	}
	
	while (ptrset_size(processing_nodes))
	{
		//Pop off a node
		lima_gp_ir_node_t* node = ptrset_first(processing_nodes);
		ptrset_remove(&processing_nodes, (void*)node);
		
		//Process it
		node->max_dist = 0;
		lima_gp_ir_dep_info_t* dep_info;
		ptrset_iter_t iter = ptrset_iter_create(node->preds);
		ptrset_iter_for_each(iter, dep_info)
		{
			unsigned dist = dep_info->pred->max_dist +
				lima_gp_ir_dep_info_get_min_dist(dep_info);
			if (dist > node->max_dist)
				node->max_dist = dist;
		}
		ptrset_add(&processed_nodes, node);
		
		//Check if any successors are now processable
		iter = ptrset_iter_create(node->succs);
		ptrset_iter_for_each(iter, dep_info)
			if (preds_processed(dep_info->succ, processed_nodes))
				ptrset_add(&processing_nodes, dep_info->succ);
	}
	
	ptrset_delete(processing_nodes);
	ptrset_delete(processed_nodes);
	
	return true;
}

bool lima_gp_ir_prog_calc_crit_path(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	
	gp_ir_prog_for_each_block(prog, block)
	{
		if (!lima_gp_ir_block_calc_crit_path(block))
			return false;
	}
	
	return true;
}
