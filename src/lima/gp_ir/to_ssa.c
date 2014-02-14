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

/*
 * Conversion to SSA
 *
 * See "Efficiently Computing Static Single Assignment Form and the Control
 * Dependence Graph" by Cytron et. al. for details.
 */


// Inserts phi nodes reg = phi(reg, reg, ..., reg) wherever necessary,
// as described in section 5.1 of the paper.
// We need to insert phi nodes in the "iterated dominance frontier" of the set
// of all basic blocks where reg is defined. Here we use a simple algorithm
// based on operations on ptrset's, instead of the worklist algorithm described
// in the paper.
static bool insert_phi_nodes(lima_gp_ir_reg_t* reg)
{
	ptrset_t blocks;
	if (!ptrset_create(&blocks))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(reg->defs);
	lima_gp_ir_node_t* node;
	ptrset_iter_for_each(iter, node)
	{
		ptrset_union(&blocks, node->successor->block->dominance_frontier);
	}
	
	ptrset_t new_blocks;
	if (!ptrset_copy(&new_blocks, blocks))
	{
		ptrset_delete(blocks);
		return false;
	}
	
	while (true)
	{
		iter = ptrset_iter_create(blocks);
		lima_gp_ir_block_t* block;
		ptrset_iter_for_each(iter, block)
		{
			ptrset_union(&new_blocks, block->dominance_frontier);
		}
		
		if (ptrset_size(new_blocks) == ptrset_size(blocks))
			break;
		
		ptrset_union(&blocks, new_blocks);
	}
	
	//blocks now contains the iterated dominance frontier
	
	ptrset_delete(new_blocks);
	
	iter = ptrset_iter_create(blocks);
	lima_gp_ir_block_t* block;
	ptrset_iter_for_each(iter, block)
	{
		lima_gp_ir_phi_node_t* phi_node =
			lima_gp_ir_phi_node_create(block->num_preds);
		if (!phi_node)
		{
			ptrset_delete(blocks);
			return false;
		}
		
		unsigned i;
		for (i = 0; i < block->num_preds; i++)
		{
			phi_node->sources[i].reg = reg;
			phi_node->sources[i].pred = block->preds[i];
		}
		
		phi_node->dest = reg;
		lima_gp_ir_block_insert_phi(block, phi_node);
	}
	
	ptrset_delete(blocks);
	
	return true;
}

// Register renaming pass
// Based on section 5.2 of the paper

// Register renaming state
typedef struct
{
	// Represents a set of stacks, one for each register, holding the registers
	// that are replacing it
	lima_gp_ir_reg_t*** reg_stack;
	
	//The current top of the stack
	int* reg_stack_index;
	
	//The number of entries in reg_stack and reg_stack_index
	unsigned num_entries;
} reg_rename_state_t;

static bool reg_replace_node(lima_gp_ir_node_t* node, void* _state)
{
	reg_rename_state_t* state = _state;
	
	if (node->op != lima_gp_ir_op_load_reg)
		return true;
	
	lima_gp_ir_load_reg_node_t* load_node = gp_ir_node_to_load_reg(node);
	int index = load_node->reg->index;
	assert(state->reg_stack_index[index] != -1);
	lima_gp_ir_reg_t* new_reg =
		state->reg_stack[index][state->reg_stack_index[index]];
	
	ptrset_remove(&load_node->reg->uses, node);
	load_node->reg = new_reg;
	ptrset_add(&load_node->reg->uses, node);
	
	return true;
}

static void update_phi_uses(lima_gp_ir_block_t* succ, lima_gp_ir_block_t* pred,
							reg_rename_state_t* state)
{
	unsigned pred_index = 0;
	for (pred_index = 0; pred_index < succ->num_preds; pred_index++)
		if (succ->preds[pred_index] == pred)
			break;
	
	assert(pred_index != succ->num_preds);
	
	ptrset_iter_t iter = ptrset_iter_create(succ->phi_nodes);
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_for_each(iter, phi_node)
	{
		lima_gp_ir_reg_t* reg = phi_node->sources[pred_index].reg;
		int index = reg->index;
		assert(state->reg_stack_index[index] != -1);
		
		lima_gp_ir_reg_t* new_reg =
			state->reg_stack[index][state->reg_stack_index[index]];
		
		ptrset_remove(&reg->uses, &phi_node->node);
		ptrset_add(&new_reg->uses, &phi_node->node);
		phi_node->sources[pred_index].reg = new_reg;
	}
}

static bool reg_rename_before(lima_gp_ir_block_t* block, void* _state)
{
	reg_rename_state_t* state = _state;
	
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
	ptrset_iter_for_each(iter, phi_node)
	{
		lima_gp_ir_reg_t* new_reg = lima_gp_ir_reg_create(block->prog);
		if (!new_reg)
			return false;
		new_reg->size = phi_node->dest->size;
		
		int index = ++state->reg_stack_index[phi_node->dest->index];
		state->reg_stack[phi_node->dest->index][index] = new_reg;
		
		ptrset_remove(&phi_node->dest->defs, &phi_node->node);
		phi_node->dest = new_reg;
		ptrset_add(&phi_node->dest->defs, &phi_node->node);
		
	}
	
	lima_gp_ir_root_node_t* node;
	gp_ir_block_for_each_node(block, node)
	{
		if (!lima_gp_ir_node_dfs(&node->node, NULL, reg_replace_node, _state))
			return false;
		
		if (node->node.op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_node =
				gp_ir_node_to_store_reg(&node->node);
			
			lima_gp_ir_reg_t* new_reg = lima_gp_ir_reg_create(block->prog);
			if (!new_reg)
				return false;
			new_reg->size = store_node->reg->size;
			
			int index = ++state->reg_stack_index[store_node->reg->index];
			state->reg_stack[store_node->reg->index][index] = new_reg;
			
			ptrset_remove(&store_node->reg->defs, &node->node);
			store_node->reg = new_reg;
			ptrset_add(&store_node->reg->defs, &node->node);
		}
	}
	
	if (block != gp_ir_prog_last_block(block->prog))
	{
		lima_gp_ir_block_t* next_block = gp_ir_block_next(block);
		if (block->num_nodes > 0)
		{
			lima_gp_ir_root_node_t* last_node = gp_ir_block_last_node(block);
			if (last_node->node.op == lima_gp_ir_op_branch_uncond ||
				last_node->node.op == lima_gp_ir_op_branch_cond)
			{
				lima_gp_ir_branch_node_t* branch =
					gp_ir_node_to_branch(&last_node->node);
				update_phi_uses(branch->dest, block, state);
				if (last_node->node.op == lima_gp_ir_op_branch_cond)
				{
					update_phi_uses(next_block, block, state);
				}
			}
			else
			{
				update_phi_uses(next_block, block, state);
			}
		}
		else
		{
			update_phi_uses(next_block, block, state);
		}
	}
	
	return true;
}

static bool reg_rename_after(lima_gp_ir_block_t* block, void* _state)
{
	reg_rename_state_t* state = _state;
	
	lima_gp_ir_root_node_t* node;
	gp_ir_block_for_each_node_reverse(block, node)
	{
		if (node->node.op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_node =
				gp_ir_node_to_store_reg(&node->node);
			lima_gp_ir_reg_t* reg = store_node->reg;
			
			unsigned i;
			for (i = 0; i < state->num_entries; i++)
			{
				if (state->reg_stack_index[i] != -1 &&
					state->reg_stack[i][state->reg_stack_index[i]] == reg)
				{
					state->reg_stack_index[i]--;
					break;
				}
			}
		}
	}
	
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
	ptrset_iter_for_each(iter, phi_node)
	{
		unsigned i;
		for (i = 0; i < state->num_entries; i++)
		{
			lima_gp_ir_reg_t* reg = phi_node->dest;
			if (state->reg_stack_index[i] != -1 &&
				state->reg_stack[i][state->reg_stack_index[i]] == reg)
			{
				state->reg_stack_index[i]--;
				break;
			}
		}
	}
	
	return true;
}

static void cleanup_regs(lima_gp_ir_prog_t* prog)
{
	// First pass - delete any unused regs
	lima_gp_ir_reg_t* reg, *temp;
	gp_ir_prog_for_each_reg_safe(prog, reg, temp)
	{
		if (ptrset_size(reg->uses) == 0 &&
			ptrset_size(reg->defs) == 0)
			lima_gp_ir_reg_delete(reg);
	}
	
	// Second pass - re-index remaining regs to avoid wasting space in later
	// analyses
	unsigned index = 0;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		reg->index = index++;
	}
	prog->reg_alloc = index;
}

bool lima_gp_ir_convert_to_ssa(lima_gp_ir_prog_t* prog)
{
	if (!lima_gp_ir_calc_dominance(prog))
		return false;
	
	lima_gp_ir_reg_t* reg;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		if (!insert_phi_nodes(reg))
			return false;
	}
	
	//Create reg renaming state
	reg_rename_state_t state;
	state.num_entries = prog->reg_alloc;
	state.reg_stack_index = malloc(sizeof(int) * prog->reg_alloc);
	if (!state.reg_stack_index)
		return false;
	
	unsigned i;
	for (i = 0; i < state.num_entries; i++)
		state.reg_stack_index[i] = -1;
	
	state.reg_stack = calloc(sizeof(lima_gp_ir_reg_t**), prog->reg_alloc);
	if (!state.reg_stack)
	{
		free(state.reg_stack_index);
		return false;
	}
	
	gp_ir_prog_for_each_reg(prog, reg)
	{
		state.reg_stack[reg->index] =
			malloc(sizeof(lima_gp_ir_reg_t*) * ptrset_size(reg->defs));
		if (!state.reg_stack[reg->index])
		{
			free(state.reg_stack);
			free(state.reg_stack_index);
			return false;
		}
	}
	
	bool ret = lima_gp_ir_dom_tree_dfs(prog, reg_rename_before,
									   reg_rename_after, (void*)&state);
	
	if (ret)
		cleanup_regs(prog);
	
	for (i = 0; i < state.num_entries; i++)
		if (state.reg_stack[i])
			free(state.reg_stack[i]);
	free(state.reg_stack);
	free(state.reg_stack_index);
	
	return ret;
}
