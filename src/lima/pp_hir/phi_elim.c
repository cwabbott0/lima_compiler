/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
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

#include "phi_elim.h"
#include <stdlib.h>
#include <string.h>



//Splits the critical edges in prog by adding an empty basic block in-between
bool lima_pp_hir_split_crit_edges(lima_pp_hir_prog_t* prog)
{
	unsigned i, j;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		if (block->is_end || block->branch_cond == lima_pp_hir_branch_cond_always)
			continue;
		
		for (i = 0; i < 2; i++)
		{
			if (block->next[i]->num_preds < 2)
				continue;
			
			lima_pp_hir_block_t* new_block = lima_pp_hir_block_create();
			if (!new_block)
				return false;
			new_block->size = 0;
			new_block->num_preds = 1;
			new_block->preds = malloc(sizeof(lima_pp_hir_block_t**));
			if (!new_block->preds)
			{
				lima_pp_hir_block_delete(new_block);
				return false;
			}
			new_block->preds[0] = block;
			new_block->is_end = false;
			new_block->branch_cond = lima_pp_hir_branch_cond_always;
			new_block->next[0] = block->next[i];
			
			lima_pp_hir_prog_insert_end(new_block, prog);
			
			for (j = 0; j < block->next[i]->num_preds; j++)
				if (block->next[i]->preds[j] == block)
				{
					block->next[i]->preds[j] = new_block;
					break;
				}
			block->next[i] = new_block;
		}
	}
	
	return true;
}

/* Uses Sreedhar's Method I in order to prepare for phi elimination
 * Based on: 
 * http://www.tjhsst.edu/~rlatimer/papers/sreedharTranslatingOutOfStaticSingleAssignmentForm.pdf
 * Relies on copy propagation after phi elimination, since we need to do it anyways
 */

bool lima_pp_hir_convert_to_cssa(lima_pp_hir_prog_t* prog)
{
	unsigned i;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		lima_pp_hir_cmd_t* cmd_insert = NULL;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op != lima_pp_hir_op_phi)
				break;
			cmd_insert = cmd;
		}
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op != lima_pp_hir_op_phi)
				break;
			
			for (i = 0; i < cmd->num_args; i++)
			{
				if (cmd->src[i].depend == NULL)
					continue;
				
				lima_pp_hir_cmd_t* new_cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
				if (!new_cmd)
					return false;
				lima_pp_hir_cmd_t* old_cmd = cmd->src[i].depend;
				new_cmd->dst = lima_pp_hir_dest_default;
				new_cmd->dst.reg.index = prog->reg_alloc++;
				new_cmd->dst.reg.size = old_cmd->dst.reg.size;
				new_cmd->src[0] = lima_pp_hir_source_default;
				new_cmd->src[0].depend = old_cmd;
				lima_pp_hir_block_insert_end(block->preds[i], new_cmd);
				ptrset_remove(&old_cmd->cmd_uses, cmd);
				cmd->src[i].depend = new_cmd;
				ptrset_add(&new_cmd->cmd_uses, cmd);
			}
			
			lima_pp_hir_cmd_t* new_cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
			if (!new_cmd)
				return false;
			new_cmd->dst = lima_pp_hir_dest_default;
			new_cmd->dst.reg.size = cmd->dst.reg.size;
			new_cmd->dst.reg.index = prog->reg_alloc++;
			new_cmd->src[0] = lima_pp_hir_source_default;
			new_cmd->src[0].depend = cmd;
			
			lima_pp_hir_cmd_replace_uses(cmd, new_cmd);
			
			if (cmd_insert)
				lima_pp_hir_block_insert(new_cmd, cmd_insert);
			else
				lima_pp_hir_block_insert_start(block, new_cmd);
			cmd_insert = new_cmd;
		}
	}
	
	return true;
}
