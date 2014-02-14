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

#include "pp_hir.h"
#include <assert.h>
#include "bitset.h"

/* Converts loads/stores to temporaries to moves to/from registers in SSA form,
 * for all temporary addresses which are not indirectly addressed.
 */

static unsigned get_max_temp_index(lima_pp_hir_prog_t* prog)
{
	unsigned ret = 0;
	
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
	{
		unsigned index = prog->arrays[i].end;
		if (index > ret)
			ret = index;
	}
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op != lima_pp_hir_op_loadt_four &&
				cmd->op != lima_pp_hir_op_loadt_four_off &&
				cmd->op != lima_pp_hir_op_storet_four &&
				cmd->op != lima_pp_hir_op_storet_four_off)
					continue;
			
			unsigned index = cmd->load_store_index;
			if (index > ret)
				ret = index;
		}
	}
	
	return ret;
}

//Returns a bitset where an element is true if it's part of an array
static bitset_t get_array_set(lima_pp_hir_prog_t* prog, unsigned max_index)
{
	bitset_t ret = bitset_create(max_index + 1);
	
	unsigned i, j;
	for (i = 0; i < prog->num_arrays; i++)
	{
		unsigned start = prog->arrays[i].start, end = prog->arrays[i].end;
		
		for (j = start; j <= end; j++)
			bitset_set(ret, j, true);
	}
	
	return ret;
}

static unsigned get_max_non_array_index(bitset_t array_set, unsigned max_index)
{
	unsigned i;
	
	for (i = max_index; i > 0; i--)
		if (!bitset_get(array_set, i))
			return i;
	
	return 0;
}

typedef struct
{
	lima_pp_hir_cmd_t*** reg_stack;
	int* reg_stack_index;
	ptrset_t* phi_nodes;
	bitset_t array_set;
	unsigned num_entries;
} reg_rename_state_t;

static void calc_defs(lima_pp_hir_prog_t* prog, unsigned num_entries,
					  unsigned* num_defs, ptrset_t* def_blocks,
					  bitset_t array_set)
{
	memset(num_defs, 0, num_entries * sizeof(unsigned));
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op != lima_pp_hir_op_storet_four)
				continue;
			
			unsigned index = cmd->load_store_index;
			
			if (bitset_get(array_set, index))
				continue;
			
			num_defs[index]++;
			ptrset_add(&def_blocks[index], block);
		}
	}
}

static bool init_state(reg_rename_state_t* state, unsigned max_index,
					   unsigned* num_defs, bitset_t array_set)
{
	state->num_entries = max_index + 1;
	state->array_set = array_set;
	
	state->reg_stack_index = malloc(sizeof(int) * state->num_entries);
	if (!state->reg_stack_index)
		return false;
	
	unsigned i;
	for (i = 0; i < state->num_entries; i++)
		state->reg_stack_index[i] = -1;
	
	state->phi_nodes = malloc(sizeof(ptrset_t) * state->num_entries);
	if (!state->phi_nodes)
	{
		free(state->reg_stack_index);
		bitset_delete(state->array_set);
		
		return false;
	}
	
	for (i = 0; i < state->num_entries; i++)
	{
		if (!ptrset_create(&state->phi_nodes[i]))
		{
			free(state->reg_stack_index);
			free(state->phi_nodes);
			bitset_delete(state->array_set);
			return false;
		}
	}

	state->reg_stack = malloc(sizeof(lima_pp_hir_cmd_t**) * state->num_entries);
	if (!state->reg_stack)
	{
		free(state->reg_stack_index);
		free(state->phi_nodes);
		bitset_delete(state->array_set);
		return false;
	}
	
	for (i = 0; i < state->num_entries; i++)
	{
		state->reg_stack[i] = malloc(sizeof(lima_pp_hir_cmd_t*) * num_defs[i]);
		if (!state->reg_stack[i])
		{
			free(state->reg_stack);
			free(state->reg_stack_index);
			for (i = 0; i < state->num_entries; i++)
				ptrset_delete(state->phi_nodes[i]);
			free(state->phi_nodes);
			bitset_delete(state->array_set);
			return false;
		}
	}
	
	return true;
}

static void delete_state(reg_rename_state_t* state)
{
	free(state->reg_stack_index);
	
	unsigned i;
	for (i = 0; i < state->num_entries; i++)
		ptrset_delete(state->phi_nodes[i]);
	
	free(state->phi_nodes);
	
	for (i = 0; i < state->num_entries; i++)
		free(state->reg_stack[i]);
	
	free(state->reg_stack);
	
	bitset_delete(state->array_set);
}

static bool calc_iter_dom_frontier(ptrset_t def_blocks, ptrset_t* blocks)
{
	if (!ptrset_create(blocks))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(def_blocks);
	lima_pp_hir_block_t* block;
	ptrset_iter_for_each(iter, block)
	{
		ptrset_union(blocks, block->dominance_frontier);
	}
	
	ptrset_t new_blocks;
	if (!ptrset_copy(&new_blocks, *blocks))
	{
		ptrset_delete(*blocks);
		return false;
	}
	
	while (true)
	{
		iter = ptrset_iter_create(*blocks);
		lima_pp_hir_block_t* block;
		ptrset_iter_for_each(iter, block)
		{
			ptrset_union(&new_blocks, block->dominance_frontier);
		}
		
		if (ptrset_size(new_blocks) == ptrset_size(*blocks))
			break;
		
		ptrset_union(blocks, new_blocks);
	}
	
	ptrset_delete(new_blocks);
	return true;
}

static bool insert_phi_nodes(reg_rename_state_t* state, ptrset_t* def_blocks)
{
	unsigned i;
	for (i = 0; i < state->num_entries; i++)
	{
		ptrset_t blocks;
		if (!calc_iter_dom_frontier(def_blocks[i], &blocks))
			return false;
		
		lima_pp_hir_block_t* block;
		ptrset_iter_t iter = ptrset_iter_create(blocks);
		ptrset_iter_for_each(iter, block)
		{
			lima_pp_hir_cmd_t* phi_node =
				lima_pp_hir_phi_create(block->num_preds);
			
			if (!phi_node)
			{
				ptrset_delete(blocks);
				return false;
			}
			
			lima_pp_hir_block_insert_start(block, phi_node);
			
			ptrset_add(&state->phi_nodes[i], phi_node);
		}
		
		ptrset_delete(blocks);
	}
	
	return true;
}

static unsigned get_phi_index(reg_rename_state_t* state,
							  lima_pp_hir_cmd_t* phi_node)
{
	unsigned i;
	for (i = 0; i < state->num_entries; i++)
	{
		if (ptrset_contains(state->phi_nodes[i], phi_node))
			return i;
	}
	
	assert(0);
	return 0;
}

static void update_phi_uses(reg_rename_state_t* state,
							lima_pp_hir_block_t* pred,
							lima_pp_hir_block_t* succ)
{
	unsigned pred_index = 0;
	for (pred_index = 0; pred_index < succ->num_preds; pred_index++)
		if (succ->preds[pred_index] == pred)
			break;
	
	assert(pred_index != succ->num_preds);
	
	lima_pp_hir_cmd_t* cmd;
	pp_hir_block_for_each_cmd(succ, cmd)
	{
		if (cmd->op != lima_pp_hir_op_phi)
			break;
		
		unsigned reg_index = get_phi_index(state, cmd);
		lima_pp_hir_cmd_t* dep =
			state->reg_stack[reg_index][state->reg_stack_index[reg_index]];
		ptrset_add(&dep->cmd_uses, cmd);
		cmd->src[pred_index].depend = dep;
	}
}

static bool reg_rename_before(lima_pp_hir_block_t* block, void* _state)
{
	reg_rename_state_t* state = _state;
	
	lima_pp_hir_cmd_t* cmd, *tmp_cmd;
	pp_hir_block_for_each_cmd_safe(block, tmp_cmd, cmd)
	{
		switch (cmd->op)
		{
			case lima_pp_hir_op_phi:
			{
				cmd->dst.reg.index = block->prog->reg_alloc++;
				
				unsigned orig_index = get_phi_index(state, cmd);
				int index = ++state->reg_stack_index[orig_index];
				state->reg_stack[orig_index][index] = cmd;
				break;
			}
			
			case lima_pp_hir_op_loadt_four:
			{
				unsigned orig_index = cmd->load_store_index;
				
				if (bitset_get(state->array_set, orig_index))
					break;
				
				lima_pp_hir_cmd_t* new_cmd =
					lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
				if (!new_cmd)
					return false;
				
				new_cmd->src[0].depend =
					state->reg_stack[orig_index][state->reg_stack_index[orig_index]];
				new_cmd->dst.reg.index = cmd->dst.reg.index;
				
				lima_pp_hir_cmd_replace_uses(cmd, new_cmd);
				lima_pp_hir_block_replace(cmd, new_cmd);
				break;
			}
			
			case lima_pp_hir_op_storet_four:
			{
				unsigned orig_index = cmd->load_store_index;
				
				if (bitset_get(state->array_set, orig_index))
					break;
				
				lima_pp_hir_cmd_t* new_cmd =
					lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
				if (!new_cmd)
					return false;
				
				new_cmd->src[0] = cmd->src[0];
				
				int index = ++state->reg_stack_index[orig_index];
				state->reg_stack[orig_index][index] = new_cmd;
				new_cmd->dst.reg.index = block->prog->reg_alloc++;
				
				lima_pp_hir_block_replace(cmd, new_cmd);
				break;
			}
				
			default:
				break;
		}
	}
	
	if (!block->is_end)
	{
		update_phi_uses(state, block, block->next[0]);
		if (block->branch_cond != lima_pp_hir_branch_cond_always)
			update_phi_uses(state, block, block->next[1]);
	}
	
	return true;
}

static bool reg_rename_after(lima_pp_hir_block_t* block, void* _state)
{
	reg_rename_state_t* state = _state;
	
	lima_pp_hir_cmd_t* cmd;
	pp_hir_block_for_each_cmd_reverse(block, cmd)
	{
		unsigned i;
		for (i = 0; i < state->num_entries; i++)
		{
			if (state->reg_stack_index[i] != -1 &&
				state->reg_stack[i][state->reg_stack_index[i]] == cmd)
			{
				state->reg_stack_index[i]--;
				break;
			}
		}
	}
	
	return true;
}

bool lima_pp_hir_temp_to_reg(lima_pp_hir_prog_t* prog)
{
	unsigned max_index = get_max_temp_index(prog);
	bitset_t array_set = get_array_set(prog, max_index);
	max_index = get_max_non_array_index(array_set, max_index);
	
	unsigned* num_defs = malloc(sizeof(unsigned) * (max_index + 1));
	if (!num_defs)
	{
		bitset_delete(array_set);
		return false;
	}
	
	ptrset_t* def_blocks = malloc(sizeof(ptrset_t) * (max_index + 1));
	if (!def_blocks)
	{
		bitset_delete(array_set);
		free(num_defs);
		return false;
	}
	
	unsigned i;
	for (i = 0; i <= max_index; i++)
	{
		if (!ptrset_create(&def_blocks[i]))
		{
			bitset_delete(array_set);
			free(num_defs);
			free(def_blocks);
			return false;
		}
	}
	
	calc_defs(prog, max_index + 1, num_defs, def_blocks, array_set);
	
	reg_rename_state_t state;
	if (!init_state(&state, max_index, num_defs, array_set))
	{
		free(num_defs);
		for (i = 0; i <= max_index; i++)
			ptrset_delete(def_blocks[i]);
		free(def_blocks);
		return false;
	}
	
	free(num_defs);
	
	if (!insert_phi_nodes(&state, def_blocks))
	{
		for (i = 0; i <= max_index; i++)
			ptrset_delete(def_blocks[i]);
		free(def_blocks);
		delete_state(&state);
		return false;
	}
	
	for (i = 0; i <= max_index; i++)
		ptrset_delete(def_blocks[i]);
	free(def_blocks);
	
	bool ret = lima_pp_hir_dom_tree_dfs(prog, reg_rename_before,
										reg_rename_after, (void*)&state);
	
	delete_state(&state);
	return ret;
}

