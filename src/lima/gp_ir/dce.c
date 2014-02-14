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
#include "fixed_queue.h"

/* Dead Code Elimination
 *
 * Assumes SSA form.
 */

static void mark_all_dead(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_phi_node_t* phi_node;
		ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
		ptrset_iter_for_each(iter, phi_node)
		{
			phi_node->is_dead = true;
		}
		
		lima_gp_ir_root_node_t* root_node;
		gp_ir_block_for_each_node(block, root_node)
		{
			root_node->is_dead = true;
		}
	}
}

static void add_reg_to_queue(lima_gp_ir_reg_t* reg, fixed_queue_t* queue)
{
	lima_gp_ir_node_t* def = ptrset_first(reg->defs);
	bool add_to_queue = false;
	
	if (def->op == lima_gp_ir_op_phi)
	{
		lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(def);
		if (phi_node->is_dead)
		{
			phi_node->is_dead = false;
			add_to_queue = true;
		}
	}
	else
	{
		lima_gp_ir_store_reg_node_t* store_reg = gp_ir_node_to_store_reg(def);
		if (store_reg->root_node.is_dead)
		{
			store_reg->root_node.is_dead = false;
			add_to_queue = true;
		}
	}
	
	if (add_to_queue)
		fixed_queue_push(queue, (void*)def);
}

//TODO: make this not dumb
//Adds the register write corresponding to any read in node or its children
//to the queue.
static void process_node(lima_gp_ir_node_t* node, fixed_queue_t* queue)
{
	if (node->op == lima_gp_ir_op_load_reg)
	{
		lima_gp_ir_load_reg_node_t* load_reg = gp_ir_node_to_load_reg(node);
		add_reg_to_queue(load_reg->reg, queue);
	}
	else
	{
		lima_gp_ir_child_node_iter_t iter;
		gp_ir_node_for_each_child(node, iter)
		{
			process_node(*iter.child, queue);
		}
	}
}

static void process_phi(lima_gp_ir_phi_node_t* node, fixed_queue_t* queue)
{
	unsigned i;
	for (i = 0; i < node->num_sources; i++)
	{
		add_reg_to_queue(node->sources[i].reg, queue);
	}
}

static void init_queue(lima_gp_ir_prog_t* prog, fixed_queue_t* queue)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_root_node_t* node;
		gp_ir_block_for_each_node(block, node)
		{
			if (node->node.op == lima_gp_ir_op_store_temp ||
				node->node.op == lima_gp_ir_op_store_varying ||
				node->node.op == lima_gp_ir_op_store_temp_load_off0 ||
				node->node.op == lima_gp_ir_op_store_temp_load_off1 ||
				node->node.op == lima_gp_ir_op_store_temp_load_off2 ||
				node->node.op == lima_gp_ir_op_branch_uncond ||
				node->node.op == lima_gp_ir_op_branch_cond)
			{
				node->is_dead = false;
				fixed_queue_push(queue, (void*)&node->node);
			}
		}
	}
}

static unsigned get_total_nodes(lima_gp_ir_prog_t* prog)
{
	unsigned ret = 0;
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		ret += ptrset_size(block->phi_nodes);
		lima_gp_ir_root_node_t* node;
		gp_ir_block_for_each_node(block, node)
		{
			ret++;
		}
	}
	
	return ret;
}

static void delete_dead_nodes(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_phi_node_t* phi_node;
		ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
		ptrset_iter_for_each(iter, phi_node)
		{
			if (phi_node->is_dead)
				lima_gp_ir_block_remove_phi(block, phi_node);
		}
		
		lima_gp_ir_root_node_t* root_node, *temp;
		gp_ir_block_for_each_node_safe(block, root_node, temp)
		{
			if (root_node->is_dead)
				lima_gp_ir_block_remove(root_node);
		}
	}
}

static void cleanup_regs(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_reg_t* reg, *temp;
	gp_ir_prog_for_each_reg_safe(prog, reg, temp)
	{
		if (ptrset_size(reg->defs) == 0 && ptrset_size(reg->uses) == 0)
			lima_gp_ir_reg_delete(reg);
	}
	
	unsigned i = 0;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		reg->index = i++;
	}
	
	prog->reg_alloc = i;
}

bool lima_gp_ir_dead_code_eliminate(lima_gp_ir_prog_t* prog)
{
	mark_all_dead(prog);
	
	fixed_queue_t queue = fixed_queue_create(get_total_nodes(prog));
	
	init_queue(prog, &queue);
	
	while (!fixed_queue_is_empty(queue))
	{
		lima_gp_ir_node_t* node = (lima_gp_ir_node_t*)fixed_queue_pop(&queue);
		if (node->op == lima_gp_ir_op_phi)
			process_phi(gp_ir_node_to_phi(node), &queue);
		else
			process_node(node, &queue);
	}
	
	delete_dead_nodes(prog);
	cleanup_regs(prog);
	fixed_queue_delete(queue);
	return true;
}
