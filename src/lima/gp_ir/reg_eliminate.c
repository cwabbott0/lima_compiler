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
 * Register Elimination
 *
 * In this pass, the goal is to replace register reads/writes to code where
 * the result is passed directly from one node to another. This allows us to
 * convert traditional 3-address code, as well as tree-based IR's, to our IR
 * by first converting directly and then using this optimization. It is also
 * used to cleanup the result of if-conversion.
 *
 * Assuming SSA form, we know that as long as an expression doesn't have any
 * side-effects or depend upon side-effecting nodes (i.e. temporary
 * reads/writes), we can move it as far down in its basic block as we want.
 * We exploit that fact by conceptually moving the definition to right before
 * the first corresponding use; since we define the register and then
 * immediately use it, it is then equivalent to simply passing the result
 * directly to the use and bypassing the register. We do this for each use in
 * the same basic block as the definition, and then delete each register and
 * corresponding definition with no more uses.
 */

static bool has_temp_read_cb(lima_gp_ir_node_t* node, void* _state)
{
	bool* has_temp_read = (bool*)_state;
	
	if (node->op == lima_gp_ir_op_load_temp)
		*has_temp_read = true;
	
	return true;
}

static bool has_temp_read(lima_gp_ir_node_t* node)
{
	bool ret = false;
	if (!lima_gp_ir_node_dfs(node, NULL, has_temp_read_cb, (void*)&ret))
		return false;
	
	return ret;
}

//Check whether we can move the definition to the use
static bool can_move(lima_gp_ir_node_t* def, lima_gp_ir_node_t* use)
{
	if (!has_temp_read(def))
		return true;
	
	lima_gp_ir_root_node_t* node;
	for (node = def->successor; node != use->successor;
		 node = gp_ir_node_next(node))
	{
		if (node->node.op == lima_gp_ir_op_store_temp)
			return false;
	}
	
	return true;
}

static bool eliminate_reg(lima_gp_ir_reg_t* reg)
{
	assert(ptrset_size(reg->defs) == 1);
	
	lima_gp_ir_node_t* def = ptrset_first(reg->defs);
	
	//Can't eliminate register if it's defined by a phi node
	if (def->op == lima_gp_ir_op_phi)
		return true;
	
	assert(def->op == lima_gp_ir_op_store_reg);
	
	lima_gp_ir_store_reg_node_t* store_reg = gp_ir_node_to_store_reg(def);
	
	lima_gp_ir_node_t* use;
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	ptrset_iter_for_each(iter, use)
	{
		if (use->op == lima_gp_ir_op_phi)
			continue;
		
		if (use->successor->block != def->successor->block)
			continue;
		
		assert(use->op == lima_gp_ir_op_load_reg);
		
		//Find the actual definition corresponding to the channel being used
		lima_gp_ir_load_reg_node_t* load_reg = gp_ir_node_to_load_reg(use);
		
		assert(store_reg->mask[load_reg->component]);
		
		lima_gp_ir_node_t* actual_def = store_reg->children[load_reg->component];
		
		if (!can_move(actual_def, use))
			continue;
		
		//Make all uses of the register take the actual definition instead
		if (!lima_gp_ir_node_replace(use, actual_def))
			return false;
	}
	
	if (ptrset_size(reg->uses) == 0)
	{
		lima_gp_ir_block_remove(&store_reg->root_node);
		lima_gp_ir_reg_delete(reg);
	}
	
	return true;
}

static void cleanup_regs(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_reg_t* reg;
	unsigned i = 0;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		reg->index = i++;
	}
	
	prog->reg_alloc = i;
}

bool lima_gp_ir_reg_eliminate(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_reg_t* reg, *temp;
	
	gp_ir_prog_for_each_reg_safe(prog, reg, temp)
	{
		if (!eliminate_reg(reg))
			return false;
	}
	
	cleanup_regs(prog);
	return true;
}
