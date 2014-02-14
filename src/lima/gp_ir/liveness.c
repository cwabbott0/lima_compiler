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
#include "fixed_queue.h"

typedef struct
{
	bitset_t live_regs;
	bool virt;
} liveness_compute_state_t;

static bool liveness_compute_node_cb(lima_gp_ir_node_t* node, void* _state)
{
	if (node->op != lima_gp_ir_op_load_reg)
		return true;
	
	liveness_compute_state_t* state = (liveness_compute_state_t*)_state;
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	lima_gp_ir_reg_t* reg = load_reg_node->reg;
	if ((reg->phys_reg_assigned && state->virt) ||
		(!reg->phys_reg_assigned && !state->virt))
		return true;
	
	if (state->virt)
	{
		bitset_set(state->live_regs, 4 * reg->index + load_reg_node->component,
				   true);
	}
	else
	{
		bitset_set(state->live_regs,
				   4 * reg->phys_reg + reg->phys_reg_offset +
				   load_reg_node->component, true);
	}
	
	return true;
}

bool lima_gp_ir_liveness_compute_node(lima_gp_ir_root_node_t* node,
									  bitset_t live_before, bool virt)
{
	if (virt)
		bitset_copy(&live_before, node->live_virt_after);
	else
		bitset_copy(&live_before, node->live_phys_after);
	
	if (node->node.op == lima_gp_ir_op_store_reg)
	{
		lima_gp_ir_store_reg_node_t* store_reg_node =
			gp_ir_node_to_store_reg(&node->node);
		lima_gp_ir_reg_t* reg = store_reg_node->reg;
		if (virt && !reg->phys_reg_assigned)
		{
			unsigned i;
			for (i = 0; i < 4; i++)
				if (store_reg_node->mask[i])
				{
					bitset_set(live_before, 4 * reg->index + i, false);
				}
		}
		if (!virt && reg->phys_reg_assigned)
		{
			unsigned i;
			for (i = 0; i < 4; i++)
				if (store_reg_node->mask[i])
				{
					bitset_set(live_before,
							   4 * reg->phys_reg + reg->phys_reg_offset + i,
							   false);
				}
		}
	}
	
	liveness_compute_state_t state;
	state.live_regs = live_before;
	state.virt = virt;
	return lima_gp_ir_node_dfs(&node->node, NULL, liveness_compute_node_cb,
							   (void*)&state);
}

bool lima_gp_ir_liveness_compute_block(lima_gp_ir_block_t* block, bool virt,
									   bool* changed)
{
	if (block->num_nodes == 0)
	{
		*changed = true;
		return true;
	}
	
	unsigned num_regs;
	if (virt)
		num_regs = block->prog->reg_alloc;
	else
		num_regs = 16;
	
	bitset_t live_beginning = bitset_create(num_regs * 4);
	lima_gp_ir_root_node_t* node;
	gp_ir_block_for_each_node_reverse(block, node)
	{
		bitset_t live_before;
		if (gp_ir_node_is_start(node))
			live_before = live_beginning;
		else
		{
			lima_gp_ir_root_node_t* prev = gp_ir_node_prev(node);
			live_before = virt ? prev->live_virt_after :
			                     prev->live_phys_after;
		}
		if (!lima_gp_ir_liveness_compute_node(node, live_before, virt))
			return false;
	}
	
	*changed = !bitset_equal(live_beginning, virt ? block->live_virt_before :
							                        block->live_phys_before);
	if (*changed)
		bitset_copy(virt ? &block->live_virt_before : &block->live_phys_before,
					live_beginning);
	
	bitset_delete(live_beginning);
	
	return true;
}

static void prog_create_liveness(lima_gp_ir_prog_t* prog)
{
	unsigned num_regs = prog->reg_alloc;
	
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_root_node_t* node;
		gp_ir_block_for_each_node(block, node)
		{
			bitset_delete(node->live_virt_after);
			node->live_virt_after = bitset_create(num_regs * 4);
		}
		bitset_delete(block->live_virt_before);
		block->live_virt_before = bitset_create(num_regs * 4);
	}
}

static bitset_t get_block_live_after(lima_gp_ir_block_t* block, bool virt)
{
	if (block->num_nodes == 0)
	{
		if (virt)
			return block->live_virt_before;
		else
			return block->live_phys_before;
	}
	
	lima_gp_ir_root_node_t* last_node = gp_ir_block_last_node(block);
	if (virt)
		return last_node->live_virt_after;
	
	return last_node->live_phys_after;
}

static void compress_regs(lima_gp_ir_prog_t* prog)
{
	unsigned i = 0;
	lima_gp_ir_reg_t* reg;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		reg->index = i++;
	}
	
	prog->reg_alloc = i;
}

bool lima_gp_ir_liveness_compute_prog(lima_gp_ir_prog_t* prog, bool virt)
{
	if (!lima_gp_ir_prog_calc_preds(prog))
		return false;
	
	if (virt)
	{
		compress_regs(prog);
		prog_create_liveness(prog);
	}
	
	fixed_queue_t work_queue = fixed_queue_create(prog->num_blocks);
	
	if (prog->num_blocks >= 1)
		fixed_queue_push(&work_queue, (void*)gp_ir_prog_last_block(prog));
	
	while (!fixed_queue_is_empty(work_queue))
	{
		lima_gp_ir_block_t* block =
			(lima_gp_ir_block_t*)fixed_queue_pop(&work_queue);
		
		bool changed;
		if (!lima_gp_ir_liveness_compute_block(block, virt, &changed))
		{
			fixed_queue_delete(work_queue);
			return false;
		}
		
		if (!changed)
			continue;
		
		unsigned i;
		for (i = 0; i < block->num_preds; i++)
		{
			lima_gp_ir_block_t* pred = block->preds[i];
			bitset_t live_after = get_block_live_after(pred, virt);
			if (virt)
				bitset_union(&live_after, block->live_virt_before);
			else
				bitset_union(&live_after, block->live_phys_before);
			fixed_queue_push(&work_queue, (void*)pred);
		}
	}
	
	fixed_queue_delete(work_queue);
	return true;
}
