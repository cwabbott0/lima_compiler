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
 * Implements dominance calculations required for calculating SSA,
 * based on "A Simple, Fast Dominance Algorithm" by Cooper, Harvey, and
 * Kennedy (can be found online at
 * http://www.hipersoft.rice.edu/grads/publications/dom14.pdf).
 *
 * Note: we assume that the list of basic blocks is already in reverse post
 * order.
 */

// We use the block index for quickly comparing the order two basic blocks,
// but we can't assume it's already initialized so we need to do that first.
static void index_prog(lima_gp_ir_prog_t* prog)
{
	unsigned i = 0;
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		block->index = i++;
	}
}


// Finds the closest common parent of block_1 and block_2 in the dominator tree
// Equivalent to intersect() in the paper
static lima_gp_ir_block_t* intersect(lima_gp_ir_block_t* block_1,
									 lima_gp_ir_block_t* block_2)
{
	while (block_1 != block_2)
	{
		while (block_1->index > block_2->index)
			block_1 = block_1->imm_dominator;
		while (block_2->index > block_1->index)
			block_2 = block_2->imm_dominator;
	}
	
	return block_1;
}

static void calc_dominance(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		block->imm_dominator = NULL;
	}
	
	block = gp_ir_prog_first_block(prog);
	block->imm_dominator = block;
	
	bool changed = true;
	while (changed)
	{
		changed = false;
		gp_ir_prog_for_each_block(prog, block)
		{
			if (block == gp_ir_prog_first_block(prog))
				continue;
			
			lima_gp_ir_block_t* new_idom = NULL;
			
			unsigned i;
			for (i = 0; i < block->num_preds; i++)
			{
				if (!block->preds[i]->imm_dominator)
					continue;
				
				if (new_idom)
					new_idom = intersect(new_idom, block->preds[i]);
				else
					new_idom = block->preds[i];
			}
			
			assert(new_idom);
			
			if (new_idom != block->imm_dominator)
			{
				block->imm_dominator = new_idom;
				changed = true;
			}
		}
	}
	
	gp_ir_prog_for_each_block(prog, block)
	{
		ptrset_empty(&block->dom_tree_children);
	}
	
	gp_ir_prog_for_each_block(prog, block)
	{
		if (block->imm_dominator && block->imm_dominator != block)
			ptrset_add(&block->imm_dominator->dom_tree_children, block);
	}
}

static void calc_dominance_frontier(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		ptrset_empty(&block->dominance_frontier);
	}
	
	gp_ir_prog_for_each_block(prog, block)
	{
		if (block->num_preds < 2)
			continue;
		
		unsigned i;
		for (i = 0; i < block->num_preds; i++)
		{
			lima_gp_ir_block_t* runner = block->preds[i];
			while (runner != block->imm_dominator)
			{
				ptrset_add(&runner->dominance_frontier, block);
				runner = runner->imm_dominator;
			}
		}
	}
}

bool lima_gp_ir_calc_dominance(lima_gp_ir_prog_t* prog)
{
	if (!lima_gp_ir_prog_calc_preds(prog))
		return false;
	
	index_prog(prog);
	calc_dominance(prog);
	calc_dominance_frontier(prog);
	return true;
}

//TODO: make this not recursive
static bool dom_tree_traverse(lima_gp_ir_block_t* block,
							  lima_gp_ir_dom_tree_traverse_cb preorder,
							  lima_gp_ir_dom_tree_traverse_cb postorder,
							  void* state)
{
	if (preorder && !preorder(block, state))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(block->dom_tree_children);
	lima_gp_ir_block_t* cur_block;
	ptrset_iter_for_each(iter, cur_block)
	{
		if (!dom_tree_traverse(cur_block, preorder, postorder, state))
			return false;
	}
	
	if (postorder && !postorder(block, state))
		return false;
	
	return true;
}

bool lima_gp_ir_dom_tree_dfs(lima_gp_ir_prog_t* prog,
							 lima_gp_ir_dom_tree_traverse_cb preorder,
							 lima_gp_ir_dom_tree_traverse_cb postorder,
							 void* state)
{
	if (prog->num_blocks == 0)
		return true;
	
	return dom_tree_traverse(gp_ir_prog_first_block(prog), preorder, postorder,
							 state);
}
