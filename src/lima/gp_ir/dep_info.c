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
#include <stdio.h>
#include <stdlib.h>

/* implements helpers for the scheduling algorithm(s) */

bool lima_gp_ir_dep_info_insert(lima_gp_ir_dep_info_t* dep_info)
{
	if (!ptrset_add(&dep_info->pred->succs, dep_info))
		return false;
	if (!ptrset_add(&dep_info->succ->preds, dep_info))
		return false;
	return true;
}

void lima_gp_ir_dep_info_delete(lima_gp_ir_dep_info_t* dep_info)
{
	ptrset_remove(&dep_info->pred->succs, dep_info);
	ptrset_remove(&dep_info->succ->preds, dep_info);
	free(dep_info);
}

lima_gp_ir_dep_info_t* lima_gp_ir_dep_info_find(lima_gp_ir_node_t* pred,
												lima_gp_ir_node_t* succ)
{
	ptrset_iter_t iter = ptrset_iter_create(pred->succs);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(iter, dep_info)
	{
		if (dep_info->succ == succ)
			return dep_info;
	}
	
	return NULL;
}

static bool node_insert_child_deps(lima_gp_ir_node_t* node)
{
	lima_gp_ir_child_node_iter_t iter;
	gp_ir_node_for_each_child(node, iter)
	{
		lima_gp_ir_dep_info_t* dep_info =
			malloc(sizeof(lima_gp_ir_dep_info_t));
		if (!dep_info)
			return false;
		dep_info->pred = *iter.child;
		dep_info->succ = node;
		dep_info->is_child_dep = true;
		if (node->op == lima_gp_ir_op_store_temp)
		{
			lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
			dep_info->is_offset = (store_node->addr == *iter.child);
		}
		if (!lima_gp_ir_dep_info_insert(dep_info))
		{
			free(dep_info);
			return false;
		}
	}
	
	return true;
}

static bool is_same_reg(lima_gp_ir_reg_t* reg_a, lima_gp_ir_reg_t* reg_b)
{
	if (reg_a->phys_reg_assigned && reg_b->phys_reg_assigned &&
		reg_a->phys_reg == reg_b->phys_reg)
		return true;
	if (!reg_a->phys_reg_assigned && !reg_b->phys_reg_assigned &&
		reg_a->index == reg_b->index)
		return true;
	return false;
}

static bool insert_reg_dependencies(lima_gp_ir_load_reg_node_t* load_reg_node)
{
	lima_gp_ir_root_node_t* node = load_reg_node->node.successor;
	bool first_loop = true;
	do
	{
		if (!first_loop)
			node = gp_ir_node_next(node);
		else
			first_loop = false;
		if (node->node.op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_reg_node =
				gp_ir_node_to_store_reg(&node->node);
			if (is_same_reg(store_reg_node->reg, load_reg_node->reg) &&
				store_reg_node->mask[load_reg_node->component])
			{
				// Insert write-after-read dependency (false dependency)
				lima_gp_ir_dep_info_t* dep_info =
					malloc(sizeof(lima_gp_ir_dep_info_t));
				if (!dep_info)
					return false;
				dep_info->pred = &load_reg_node->node;
				dep_info->succ = &node->node;
				dep_info->is_child_dep = false;
				if (!lima_gp_ir_dep_info_insert(dep_info))
				{
					free(dep_info);
					return false;
				}
				
				break;
			}
		}
	} while (!gp_ir_node_is_end(node));
	
	node = load_reg_node->node.successor;
	if (gp_ir_node_is_start(node))
		return true;
	
	do
	{
		node = gp_ir_node_prev(node);
		if (node->node.op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_reg_node =
				gp_ir_node_to_store_reg(&node->node);
			if (store_reg_node->reg == load_reg_node->reg)
			{
				// Insert read-after-write dependency (true dependency)
				lima_gp_ir_dep_info_t* dep_info =
					malloc(sizeof(lima_gp_ir_dep_info_t));
				if (!dep_info)
					return false;
				dep_info->pred = &node->node;
				dep_info->succ = &load_reg_node->node;
				dep_info->is_child_dep = false;
				if (!lima_gp_ir_dep_info_insert(dep_info))
				{
					free(dep_info);
					return false;
				}
				
				break;
			}
		}
	} while (!gp_ir_node_is_start(node));
	
	return true;
}


//TODO: make this more accurate
static bool insert_temp_read_deps(lima_gp_ir_load_node_t* load_temp_node)
{
	lima_gp_ir_root_node_t* node = load_temp_node->node.successor;
	bool first_loop = true;
	do
	{
		if (!first_loop)
			node = gp_ir_node_next(node);
		else
			first_loop = false;
		if (node->node.op == lima_gp_ir_op_store_temp)
		{
			// Insert write-after-read dependency (false dependency)
			lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			dep_info->pred = &load_temp_node->node;
			dep_info->succ = &node->node;
			dep_info->is_child_dep = false;
			if (!lima_gp_ir_dep_info_insert(dep_info))
			{
				free(dep_info);
				return false;
			}
				
			break;
		}
	} while (!gp_ir_node_is_end(node));
	
	node = load_temp_node->node.successor;
	
	while (!gp_ir_node_is_start(node))
	{
		node = gp_ir_node_prev(node);
		if (node->node.op == lima_gp_ir_op_store_temp)
		{
			// Insert read-after-write dependency (true dependency)
			lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			dep_info->pred = &node->node;
			dep_info->succ = &load_temp_node->node;
			dep_info->is_child_dep = false;
			if (!lima_gp_ir_dep_info_insert(dep_info))
			{
				free(dep_info);
				return false;
			}
			
			break;
		}
	}
	
	return true;
}

static bool insert_temp_read_off_deps(lima_gp_ir_load_node_t* load_temp_node)
{
	if (!load_temp_node->offset)
		return true;
	
	lima_gp_ir_op_e off_op;
	switch (load_temp_node->off_reg)
	{
		case 0:
			off_op = lima_gp_ir_op_store_temp_load_off0;
			break;
			
		case 1:
			off_op = lima_gp_ir_op_store_temp_load_off1;
			break;
			
		case 2:
			off_op = lima_gp_ir_op_store_temp_load_off2;
			break;
			
		default:
			return false;
	}
	
	lima_gp_ir_root_node_t* node = load_temp_node->node.successor;
	bool first_loop = true;
	do
	{
		if (!first_loop)
			node = gp_ir_node_next(node);
		else
			first_loop = false;
		if (node->node.op == off_op)
		{
			// Insert write-after-read dependency (false dependency)
			lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			dep_info->pred = &load_temp_node->node;
			dep_info->succ = &node->node;
			dep_info->is_child_dep = false;
			if (!lima_gp_ir_dep_info_insert(dep_info))
			{
				free(dep_info);
				return false;
			}
			
			break;
		}
	} while (!gp_ir_node_is_end(node));
	
	node = load_temp_node->node.successor;
	
	while (!gp_ir_node_is_start(node))
	{
		node = gp_ir_node_prev(node);
		if (node->node.op == off_op)
		{
			// Insert read-after-write dependency (true dependency)
			lima_gp_ir_dep_info_t* dep_info =
			malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			dep_info->pred = &node->node;
			dep_info->succ = &load_temp_node->node;
			dep_info->is_child_dep = false;
			if (!lima_gp_ir_dep_info_insert(dep_info))
			{
				free(dep_info);
				return false;
			}
			
			break;
		}
	}
	
	return true;
}

static bool node_insert_deps_cb(lima_gp_ir_node_t* node, void* state)
{
	(void) state;
	
	if (!node_insert_child_deps(node))
		return false;
	
	if (node->op == lima_gp_ir_op_load_reg)
	{
		lima_gp_ir_load_reg_node_t* load_reg_node =
			gp_ir_node_to_load_reg(node);
		if (!insert_reg_dependencies(load_reg_node))
			return false;
	}
	
	if (node->op == lima_gp_ir_op_load_temp)
	{
		lima_gp_ir_load_node_t* load_temp_node =
			gp_ir_node_to_load(node);
		if (!insert_temp_read_deps(load_temp_node))
			return false;
		if (!insert_temp_read_off_deps(load_temp_node))
			return false;
	}
	
	return true;
}

static bool dep_exists(lima_gp_ir_node_t* pred, lima_gp_ir_node_t* succ)
{
	ptrset_iter_t iter = ptrset_iter_create(pred->succs);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(iter, dep_info)
	{
		if (dep_info->succ == succ)
			return true;
	}
	
	return false;
}

//Checks whether a dependency chain a->b->c exists
static bool indirect_dep(lima_gp_ir_node_t* pred, lima_gp_ir_node_t* succ)
{
	ptrset_iter_t iter = ptrset_iter_create(pred->succs);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(iter, dep_info)
	{
		if (dep_exists(dep_info->succ, succ))
			return true;
	}
	
	return false;
}

static bool insert_reg_write_deps(lima_gp_ir_store_reg_node_t* store_reg_node)
{

	lima_gp_ir_root_node_t* node = &store_reg_node->root_node;
	 while (!gp_ir_node_is_start(node))
	 {
		node = gp_ir_node_prev(node);
		if (node->node.op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_reg_node_2 =
				gp_ir_node_to_store_reg(&node->node);
			
			//See if both nodes write to the same register
			if (!is_same_reg(store_reg_node->reg, store_reg_node_2->reg))
				continue;
			
			//See if both nodes write to the same component
			unsigned i;
			for (i = 0; i < 4; i++)
				if (store_reg_node->mask[i] && store_reg_node_2->mask[i])
					break;
			
			if (i == 4)
				continue;
			
			//Check if there's a read between the two writes
			if (indirect_dep(&node->node, &store_reg_node->root_node.node))
				continue;
				
			
			//Add the dependency
			lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			
			dep_info->pred = &node->node;
			dep_info->succ = &store_reg_node->root_node.node;
			dep_info->is_child_dep = false;
			if (!lima_gp_ir_dep_info_insert(dep_info))
			{
				free(dep_info);
				return false;
			}
			
			break;
		}
	}
	
	return true;
}

//TODO: make this more accurate
static bool insert_temp_write_deps(lima_gp_ir_store_node_t* temp_store_node)
{
	lima_gp_ir_root_node_t* node = &temp_store_node->root_node;
	while (!gp_ir_node_is_start(node))
	{
		node = gp_ir_node_prev(node);
		if (node->node.op == lima_gp_ir_op_store_temp)
		{
			//Check if there's a read between the two writes
			if (indirect_dep(&node->node, &temp_store_node->root_node.node))
				continue;
			
			//Add the dependency
			lima_gp_ir_dep_info_t* dep_info =
			malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			
			dep_info->pred = &node->node;
			dep_info->succ = &temp_store_node->root_node.node;
			dep_info->is_child_dep = false;
			if (!lima_gp_ir_dep_info_insert(dep_info))
			{
				free(dep_info);
				return false;
			}
			
			break;
		}
	}
	
	return true;
}

static bool insert_temp_write_off_deps(lima_gp_ir_store_node_t* off_store_node)
{
	if (!gp_ir_node_is_start(&off_store_node->root_node))
	{
		lima_gp_ir_root_node_t* node = &off_store_node->root_node;
		do
		{
			node = gp_ir_node_prev(node);
			if (node->node.op == off_store_node->root_node.node.op)
			{
				//Check if there's a read between the two writes
				if (indirect_dep(&node->node, &off_store_node->root_node.node))
					continue;
				
				//Add the dependency
				lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
				if (!dep_info)
					return false;
				
				dep_info->pred = &node->node;
				dep_info->succ = &off_store_node->root_node.node;
				dep_info->is_child_dep = false;
				if (!lima_gp_ir_dep_info_insert(dep_info))
				{
					free(dep_info);
					return false;
				}
				
				break;
			}
		} while (!gp_ir_node_is_end(node));
	}
	
	return true;
}

static bool insert_root_node_deps(lima_gp_ir_root_node_t* root_node)
{
	if (!lima_gp_ir_node_dfs(&root_node->node, NULL, node_insert_deps_cb, NULL))
		return false;
	
	if (root_node->node.op == lima_gp_ir_op_store_temp)
		if (!insert_temp_write_deps(gp_ir_node_to_store(&root_node->node)))
			return false;
	if (root_node->node.op == lima_gp_ir_op_store_temp_load_off0 ||
		root_node->node.op == lima_gp_ir_op_store_temp_load_off1 ||
		root_node->node.op == lima_gp_ir_op_store_temp_load_off2)
		if (!insert_temp_write_off_deps(gp_ir_node_to_store(&root_node->node)))
			return false;
	
	if (root_node->node.op == lima_gp_ir_op_store_reg)
		if (!insert_reg_write_deps(gp_ir_node_to_store_reg(&root_node->node)))
			return false;
	
	return true;
}

static bool calc_start_nodes_cb(lima_gp_ir_node_t* node, void* state)
{
	(void) state;
	
	if (!ptrset_size(node->preds))
	{
		lima_gp_ir_block_t* block = node->successor->block;
		if (!ptrset_add(&block->start_nodes, node))
			return false;
	}
	
	return true;
}

static bool calc_start_nodes(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	
	gp_ir_block_for_each_node(block, node)
	{
		if (!lima_gp_ir_node_dfs(&node->node, NULL, calc_start_nodes_cb, NULL))
			return false;
	}
	
	return true;
}

static bool calc_end_nodes(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	
	gp_ir_block_for_each_node(block, node)
	{
		if (!ptrset_size(node->node.succs))
			if (!ptrset_add(&block->end_nodes, &node->node))
				return false;
	}
	
	return true;
}

// The binary compiler will always put a varying 0 store last
// TODO: see if this is really necessary
static bool make_varying_zero_last(lima_gp_ir_block_t* block)
{
	lima_gp_ir_node_t* node, *varying_node = NULL;
	ptrset_iter_t iter = ptrset_iter_create(block->end_nodes);
	ptrset_iter_for_each(iter, node)
	{
		if (node->op == lima_gp_ir_op_store_varying)
		{
			lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
			if (store_node->index == 0)
			{
				varying_node = node;
				break;
			}
		}
	}
	
	if (!varying_node)
		return true;
	
	iter = ptrset_iter_create(block->end_nodes);
	ptrset_iter_for_each(iter, node)
	{
		if (node != varying_node)
		{
			lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			
			dep_info->pred = node;
			dep_info->succ = varying_node;
			dep_info->is_child_dep = false;
			dep_info->is_offset = false;
			
			if (!lima_gp_ir_dep_info_insert(dep_info))
				return false;
			
			if (!ptrset_remove(&block->end_nodes, node))
				return false;
		}
	}
	
	return true;
}

static bool make_branch_last(lima_gp_ir_block_t* block)
{
	if (block->num_nodes == 0)
		return true;
	
	lima_gp_ir_root_node_t* last_node = gp_ir_block_last_node(block);
	if (last_node->node.op != lima_gp_ir_op_branch_cond &&
		last_node->node.op != lima_gp_ir_op_branch_uncond)
		return true;
	
	lima_gp_ir_node_t* branch_node = &last_node->node;
	
	lima_gp_ir_node_t* node;
	ptrset_iter_t iter = ptrset_iter_create(block->end_nodes);
	ptrset_iter_for_each(iter, node)
	{
		if (node != branch_node)
		{
			lima_gp_ir_dep_info_t* dep_info =
				malloc(sizeof(lima_gp_ir_dep_info_t));
			if (!dep_info)
				return false;
			
			dep_info->pred = node;
			dep_info->succ = branch_node;
			dep_info->is_child_dep = false;
			dep_info->is_offset = false;
			
			if (!lima_gp_ir_dep_info_insert(dep_info))
				return false;
			
			if (!ptrset_remove(&block->end_nodes, node))
				return false;
		}
	}
	
	return true;
}

bool lima_gp_ir_block_calc_dependencies(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	
	gp_ir_block_for_each_node(block, node)
	{
		if (!insert_root_node_deps(node))
			return false;
	}
	
	if (!calc_start_nodes(block))
		return false;
	
	if (!calc_end_nodes(block))
		return false;
	
	if (!make_branch_last(block))
		return false;
	
	if (!make_varying_zero_last(block))
		return false;
	
	return true;
}

bool lima_gp_ir_prog_calc_dependencies(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	
	gp_ir_prog_for_each_block(prog, block)
	{
		if (!lima_gp_ir_block_calc_dependencies(block))
			return false;
	}
	
	return true;
}

bool number_node_cb(lima_gp_ir_node_t* node, void* state)
{
	unsigned* count = state;
	
	node->index = (*count)++;
	return true;
}

bool print_dep_info_cb(lima_gp_ir_node_t* node, void* state)
{
	(void) state;
	
	printf("node %u:\n", node->index);
	printf("\top: %s\n", lima_gp_ir_op[node->op].name);
	printf("\t%u predecessors:\n", ptrset_size(node->preds));
	
	ptrset_iter_t iter = ptrset_iter_create(node->preds);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(iter, dep_info)
	{
		printf("\t\tnode %u\n", dep_info->pred->index);
	}
	
	printf("\t%u successors:\n", ptrset_size(node->succs));
	
	iter = ptrset_iter_create(node->succs);
	ptrset_iter_for_each(iter, dep_info)
	{
		printf("\t\tnode %u\n", dep_info->succ->index);
	}
	
	printf("\tmax_dist: %u\n", node->max_dist);
	
	return true;
}

void lima_gp_ir_block_print_dep_info(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	
	unsigned count = 0;
	gp_ir_block_for_each_node(block, node)
	{
		lima_gp_ir_node_dfs(&node->node, NULL, number_node_cb, (void*)&count);
	}
	
	gp_ir_block_for_each_node(block, node)
	{
		lima_gp_ir_node_dfs(&node->node, NULL, print_dep_info_cb, NULL);
		printf("\n");
	}
}

void lima_gp_ir_prog_print_dep_info(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	
	gp_ir_prog_for_each_block(prog, block)
	{
		printf("block:\n");
		lima_gp_ir_block_print_dep_info(block);
		printf("\n\n");
	}
}
