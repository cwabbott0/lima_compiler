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

/* Similar to the code in lima_pp_lir/phi_elim.c */

static lima_gp_ir_root_node_t* create_copy(lima_gp_ir_reg_t* dst,
										   lima_gp_ir_reg_t* src)
{
	lima_gp_ir_store_reg_node_t* store_node = lima_gp_ir_store_reg_node_create();
	if (!store_node)
		return NULL;
	
	store_node->reg = dst;
	
	unsigned i;
	for (i = 0; i < src->size; i++)
	{
		lima_gp_ir_load_reg_node_t* load_node = lima_gp_ir_load_reg_node_create();
		if (!load_node)
		{
			lima_gp_ir_node_delete(&store_node->root_node.node);
			return NULL;
		}
		
		load_node->reg = src;
		load_node->component = i;
		
		store_node->mask[i] = true;
		store_node->children[i] = &load_node->node;
		
		lima_gp_ir_node_link(&store_node->root_node.node, &load_node->node);
	}
	
	return &store_node->root_node;
}

static void insert_end(lima_gp_ir_block_t* block, lima_gp_ir_root_node_t* node)
{
	if (block->num_nodes > 0)
	{
		lima_gp_ir_root_node_t* last_node = gp_ir_block_last_node(block);
		if (last_node->node.op == lima_gp_ir_op_branch_cond ||
			last_node->node.op == lima_gp_ir_op_branch_uncond)
		{
			lima_gp_ir_block_insert_before(node, last_node);
			return;
		}
	}
	
	lima_gp_ir_block_insert_end(block, node);
}

static bool phi_node_insert_copies(lima_gp_ir_phi_node_t* node)
{
	lima_gp_ir_reg_t* new_reg = lima_gp_ir_reg_create(node->block->prog);
	if (!new_reg)
		return false;
	new_reg->size = node->dest->size;
	
	lima_gp_ir_root_node_t* copy = create_copy(node->dest, new_reg);
	if (!copy)
		return false;
	
	lima_gp_ir_block_insert_start(node->block, copy);
	
	ptrset_remove(&node->dest->defs, &node->node);
	node->dest = new_reg;
	ptrset_add(&node->dest->defs, &node->node);
	
	unsigned i;
	for (i = 0; i < node->num_sources; i++)
	{
		new_reg = lima_gp_ir_reg_create(node->block->prog);
		if (!new_reg)
			return false;
		new_reg->size = node->sources[i].reg->size;
		
		copy = create_copy(new_reg, node->sources[i].reg);
		if (!copy)
			return false;
		
		insert_end(node->sources[i].pred, copy);
		
		ptrset_remove(&node->sources[i].reg->uses, &node->node);
		node->sources[i].reg = new_reg;
		ptrset_add(&node->sources[i].reg->uses, &node->node);
	}
	
	return true;
}

static bool insert_copies(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_phi_node_t* node;
		ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
		ptrset_iter_for_each(iter, node)
		{
			if (!phi_node_insert_copies(node))
				return false;
		}
	}
	
	return true;
}

static void replace_reg(lima_gp_ir_reg_t* new, lima_gp_ir_reg_t* old)
{
	lima_gp_ir_node_t* use;
	ptrset_iter_t iter = ptrset_iter_create(old->uses);
	ptrset_iter_for_each(iter, use)
	{
		if (use->op == lima_gp_ir_op_load_reg)
		{
			lima_gp_ir_load_reg_node_t* load_node = gp_ir_node_to_load_reg(use);
			load_node->reg = new;
		}
		else
		{
			lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(use);
			unsigned i;
			for (i = 0; i < phi_node->num_sources; i++)
			{
				if (phi_node->sources[i].reg == old)
				{
					phi_node->sources[i].reg = new;
					break;
				}
			}
		}
		
		ptrset_add(&new->uses, use);
		ptrset_remove(&old->uses, use);
	}
	
	lima_gp_ir_node_t* def;
	iter = ptrset_iter_create(old->defs);
	ptrset_iter_for_each(iter, def)
	{
		if (def->op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_node =
				gp_ir_node_to_store_reg(def);
			store_node->reg = new;
		}
		else
		{
			lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(def);
			phi_node->dest = new;
		}
		
		ptrset_add(&new->defs, def);
		ptrset_remove(&old->defs, def);
	}
	
	lima_gp_ir_reg_delete(old);
}

static void eliminate_phi(lima_gp_ir_phi_node_t* node, lima_gp_ir_reg_t* reg)
{
	replace_reg(reg, node->dest);
	
	unsigned i;
	for (i = 0; i < node->num_sources; i++)
		replace_reg(reg, node->sources[i].reg);
	
	lima_gp_ir_block_remove_phi(node->block, node);
	
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	lima_gp_ir_node_t* use;
	ptrset_iter_for_each(iter, use)
	{
		if (use->op == lima_gp_ir_op_phi)
		{
			lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(use);
			eliminate_phi(phi_node, reg);
		}
	}
	
	lima_gp_ir_node_t* def;
	iter = ptrset_iter_create(reg->defs);
	ptrset_iter_for_each(iter, def)
	{
		if (def->op == lima_gp_ir_op_phi)
		{
			lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(def);
			eliminate_phi(phi_node, reg);
		}
	}
}

static bool eliminate_phi_nodes(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		while (ptrset_size(block->phi_nodes) > 0)
		{
			lima_gp_ir_phi_node_t* phi_node = ptrset_first(block->phi_nodes);

			lima_gp_ir_reg_t* reg = lima_gp_ir_reg_create(prog);
			reg->size = phi_node->dest->size;
			if (!reg)
				return false;
			
			eliminate_phi(phi_node, reg);
		}
	}
	
	return true;
}

bool lima_gp_ir_eliminate_phi_nodes(lima_gp_ir_prog_t* prog)
{
	if (!insert_copies(prog))
		return false;
	
	lima_gp_ir_prog_print(prog, 0, false);
	
	return eliminate_phi_nodes(prog);
}
