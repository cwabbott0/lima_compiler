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

#include "gp_ir.h"
#include <assert.h>

/* search for simple if-statements and if-else-statements in a program.
 * 
 * We assume that an if-statement consists of the following basic blocks:
 *
 * entry:
 * ...
 * branch to end if (condition)
 * 
 * if:
 * ...
 *
 * end:
 * ...
 *
 * And an if-else-statement:
 *
 * entry
 * ...
 * branch to else if (condition)
 *
 * if:
 * ...
 * branch to end
 *
 * else:
 * ...
 *
 * end:
 * ...
 *
 */

static bool is_if(lima_gp_ir_block_t* entry)
{
	if (gp_ir_block_is_last(entry))
		return false;
	
	lima_gp_ir_block_t* if_block = gp_ir_block_next(entry);
	if (gp_ir_block_is_last(if_block))
		return false;
	
	if (if_block->num_preds != 1)
		return false;
	
	lima_gp_ir_block_t* end = gp_ir_block_next(if_block);
	
	if (end->num_preds != 2)
		return false;
	
	if (gp_ir_block_is_empty(entry))
		return false;
	
	lima_gp_ir_root_node_t* last = gp_ir_block_last_node(entry);
	if (last->node.op != lima_gp_ir_op_branch_cond)
		return false;
	
	lima_gp_ir_branch_node_t* branch = gp_ir_node_to_branch(&last->node);
	if (branch->dest != end)
		return false;
	
	if (!gp_ir_block_is_empty(if_block))
	{
		last = gp_ir_block_last_node(if_block);
		if (last->node.op == lima_gp_ir_op_branch_cond ||
			last->node.op == lima_gp_ir_op_branch_uncond)
			return false;
	}
	
	return true;
}

static bool is_if_else(lima_gp_ir_block_t* entry)
{
	if (gp_ir_block_is_last(entry))
		return false;
	
	lima_gp_ir_block_t* if_block = gp_ir_block_next(entry);
	if (gp_ir_block_is_last(if_block))
		return false;
	
	if (if_block->num_preds != 1)
		return false;
	
	lima_gp_ir_block_t* else_block = gp_ir_block_next(if_block);
	if (gp_ir_block_is_last(else_block))
		return false;
	
	if (else_block->num_preds != 1)
		return false;
	
	lima_gp_ir_block_t* end = gp_ir_block_next(else_block);
	
	if (end->num_preds != 2)
		return false;
	
	if (gp_ir_block_is_empty(entry))
		return false;
	
	lima_gp_ir_root_node_t* last = gp_ir_block_last_node(entry);
	if (last->node.op != lima_gp_ir_op_branch_cond)
		return false;
	
	lima_gp_ir_branch_node_t* branch = gp_ir_node_to_branch(&last->node);
	if (branch->dest != else_block)
		return false;
	
	if (gp_ir_block_is_empty(if_block))
		return false;
	
	last = gp_ir_block_last_node(if_block);
	if (last->node.op != lima_gp_ir_op_branch_uncond)
		return false;
	
	branch = gp_ir_node_to_branch(&last->node);
	if (branch->dest != end)
		return false;
	
	if (!gp_ir_block_is_empty(else_block))
	{
		last = gp_ir_block_last_node(else_block);
		if (last->node.op == lima_gp_ir_op_branch_cond ||
			last->node.op == lima_gp_ir_op_branch_uncond)
			return false;
	}
	
	return true;
}

static bool has_side_effects(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	gp_ir_block_for_each_node(block, node)
	{
		if (node->node.op == lima_gp_ir_op_store_temp ||
			node->node.op == lima_gp_ir_op_store_varying ||
			node->node.op == lima_gp_ir_op_store_temp_load_off0 ||
			node->node.op == lima_gp_ir_op_store_temp_load_off1 ||
			node->node.op == lima_gp_ir_op_store_temp_load_off2)
			return true;
	}
	
	return false;
}

//Creates a register that holds the condition of the branch
static lima_gp_ir_reg_t* create_condition_reg(lima_gp_ir_block_t* entry)
{
	assert(!gp_ir_block_is_empty(entry));
	lima_gp_ir_root_node_t* last = gp_ir_block_last_node(entry);
	assert(last->node.op == lima_gp_ir_op_branch_cond);
	lima_gp_ir_branch_node_t* branch = gp_ir_node_to_branch(&last->node);
	
	lima_gp_ir_reg_t* reg = lima_gp_ir_reg_create(entry->prog);
	if (!reg)
		return NULL;
	
	reg->size = 1;
	
	lima_gp_ir_store_reg_node_t* store_node = lima_gp_ir_store_reg_node_create();
	if (!store_node)
	{
		lima_gp_ir_reg_delete(reg);
		return NULL;
	}
	
	store_node->reg = reg;
	
	lima_gp_ir_block_insert_before(&store_node->root_node, last);

	store_node->mask[0] = true;
	store_node->children[0] = branch->condition;
	lima_gp_ir_node_link(&store_node->root_node.node, branch->condition);
	
	return reg;
}

//Rewrites phi nodes in the exit block to use selects instead
//cond_reg must be true if pred1 was taken, false if pred2 was taken
static bool rewrite_phi_nodes(lima_gp_ir_block_t* exit,
							  lima_gp_ir_reg_t* cond_reg,
							  lima_gp_ir_block_t* pred1,
							  lima_gp_ir_block_t* pred2)
{
	unsigned i;
	
	lima_gp_ir_load_reg_node_t* cond = lima_gp_ir_load_reg_node_create();
	if (!cond)
		return false;
	
	cond->reg = cond_reg;
	cond->component = 0;
	
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_t iter = ptrset_iter_create(exit->phi_nodes);
	ptrset_iter_for_each(iter, phi_node)
	{
		lima_gp_ir_reg_t* pred1_reg, *pred2_reg;
		assert(phi_node->num_sources == 2);
		if (phi_node->sources[0].pred == pred1)
		{
			pred1_reg = phi_node->sources[0].reg;
			pred2_reg = phi_node->sources[1].reg;
		}
		else
		{
			pred1_reg = phi_node->sources[1].reg;
			pred2_reg = phi_node->sources[0].reg;
		}
		
		lima_gp_ir_store_reg_node_t* store_node =
			lima_gp_ir_store_reg_node_create();
		if (!store_node)
			return false;
		
		store_node->reg = phi_node->dest;
		
		lima_gp_ir_block_insert_start(exit, &store_node->root_node);
		
		for (i = 0; i < phi_node->dest->size; i++)
		{
			lima_gp_ir_alu_node_t* select_node =
				lima_gp_ir_alu_node_create(lima_gp_ir_op_select);
			if (!select_node)
			{
				lima_gp_ir_node_delete(&store_node->root_node.node);
				return false;
			}
			
			lima_gp_ir_load_reg_node_t* pred1_load =
				lima_gp_ir_load_reg_node_create();
			if (!pred1_load)
			{
				lima_gp_ir_node_delete(&store_node->root_node.node);
				lima_gp_ir_node_delete(&select_node->node);
				return false;
			}
			
			lima_gp_ir_load_reg_node_t* pred2_load =
				lima_gp_ir_load_reg_node_create();
			if (!pred2_load)
			{
				lima_gp_ir_node_delete(&store_node->root_node.node);
				lima_gp_ir_node_delete(&select_node->node);
				lima_gp_ir_node_delete(&pred2_load->node);
				return false;
			}
			
			pred1_load->reg = pred1_reg;
			pred1_load->component = i;
			
			pred2_load->reg = pred2_reg;
			pred2_load->component = i;
			
			select_node->children[0] = &cond->node;
			lima_gp_ir_node_link(&select_node->node, &cond->node);
			
			select_node->children[1] = &pred1_load->node;
			lima_gp_ir_node_link(&select_node->node, &pred1_load->node);
			
			select_node->children[2] = &pred2_load->node;
			lima_gp_ir_node_link(&select_node->node, &pred2_load->node);
			
			store_node->children[i] = &select_node->node;
			lima_gp_ir_node_link(&store_node->root_node.node,
								 &select_node->node);
			
			store_node->mask[i] = true;
		}
		
		lima_gp_ir_block_remove_phi(exit, phi_node);
	}
	
	return true;
}

//Really hacky way to merge two basic blocks into one
//Moves all the nodes in block2 to the end of block1
//Rather than actually copy the nodes, we simply move them and update the lists
//directly, which is a lot simpler than copying and faster but more hacky.
static void merge_blocks(lima_gp_ir_block_t* block1, lima_gp_ir_block_t* block2)
{
	if (!gp_ir_block_is_empty(block1))
	{
		lima_gp_ir_root_node_t* last = gp_ir_block_last_node(block1);
		if (last->node.op == lima_gp_ir_op_branch_cond ||
			last->node.op == lima_gp_ir_op_branch_uncond)
			lima_gp_ir_block_remove(last);
	}
	
	while (!gp_ir_block_is_empty(block2))
	{
		lima_gp_ir_root_node_t* node = gp_ir_block_first_node(block2);
		list_del(&node->node_list);
		block2->num_nodes--;
		node->block = block1;
		list_add(&node->node_list, block1->node_list.prev);
		block1->num_nodes++;
	}
	
	lima_gp_ir_prog_remove(block2);
}

static bool convert_if(lima_gp_ir_block_t* entry, bool has_else)
{
	lima_gp_ir_block_t* if_block = gp_ir_block_next(entry);
	lima_gp_ir_block_t* else_block = NULL, *exit;
	
	if (has_else)
	{
		else_block = gp_ir_block_next(if_block);
		exit = gp_ir_block_next(else_block);
	}
	else
		exit = gp_ir_block_next(if_block);
	
	if (ptrset_size(exit->phi_nodes) > 0)
	{
		lima_gp_ir_reg_t* cond_reg = create_condition_reg(entry);
		if (!cond_reg)
			return false;
		
		if (has_else)
		{
			if (!rewrite_phi_nodes(exit, cond_reg, else_block, if_block))
				return false;
		}
		else
		{
			if (!rewrite_phi_nodes(exit, cond_reg, entry, if_block))
				return false;
		}
	}
	
	merge_blocks(entry, if_block);
	if (has_else)
		merge_blocks(entry, else_block);
	merge_blocks(entry, exit);
	
	return true;
}

static bool convert_if_pass(lima_gp_ir_prog_t* prog, bool* changed)
{
	*changed = false;
	
	if (prog->num_blocks == 0)
		return true;
	
	bool has_else;
	lima_gp_ir_block_t* entry;
	gp_ir_prog_for_each_block(prog, entry)
	{
		if (!is_if(entry) && !is_if_else(entry))
			continue;
		
		has_else = is_if_else(entry);
		
		lima_gp_ir_block_t* if_block = gp_ir_block_next(entry);
		if (has_side_effects(if_block))
			continue;
		
		if (has_else && has_side_effects(gp_ir_block_next(if_block)))
			continue;
		
		if (!convert_if(entry, has_else))
			return false;
		
		*changed = true;
	}
	
	return true;
}

bool lima_gp_ir_if_convert(lima_gp_ir_prog_t* prog)
{
	bool changed = true;
	while (changed)
	{
		if (!convert_if_pass(prog, &changed))
			return false;
	}
	
	return true;
}
