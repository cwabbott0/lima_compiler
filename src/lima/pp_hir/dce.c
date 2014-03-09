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

#include "pp_hir.h"
#include "fixed_queue.h"

static unsigned num_cmds(lima_pp_hir_prog_t* prog)
{
	unsigned ret = 0;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		ret += block->size;
	}
	
	return ret;
}

static bool dead_code_eliminate(lima_pp_hir_prog_t* prog)
{
	fixed_queue_t work_queue = fixed_queue_create(num_cmds(prog));
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (lima_pp_hir_op_is_store(cmd->op) ||
				ptrset_size(cmd->block_uses) > 0)
				cmd->is_live = true;
			else
				cmd->is_live = false;
		}
	}
	
	pp_hir_prog_for_each_block(prog, block)
	{
		if (!block->is_end &&
			block->branch_cond != lima_pp_hir_branch_cond_always)
		{
			if (!block->reg_cond_a.is_constant)
				block->reg_cond_a.reg->is_live = true;
			if (!block->reg_cond_b.is_constant)
				block->reg_cond_b.reg->is_live = true;
		}
		
		if (block->is_end && !block->discard)
			block->output->is_live = true;
	}
	
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->is_live)
				fixed_queue_push(&work_queue, cmd);
		}
	}
	
	while (!fixed_queue_is_empty(work_queue))
	{
		lima_pp_hir_cmd_t* cmd = fixed_queue_pop(&work_queue);
		
		unsigned i;
		for (i = 0; i < cmd->num_args; i++)
		{
			if (cmd->src[i].constant)
				continue;
			
			lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
			if (!dep->is_live)
			{
				dep->is_live = true;
				fixed_queue_push(&work_queue, dep);
			}
		}
	}
	
	bool progress = false;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd, *tmp;
		pp_hir_block_for_each_cmd_safe(block, tmp, cmd)
		{
			if (!cmd->is_live)
			{
				lima_pp_hir_block_remove(block, cmd);
				progress = true;
			}
		}
	}
	
	fixed_queue_delete(work_queue);
	return progress;
}

bool lima_pp_hir_dead_code_eliminate(lima_pp_hir_prog_t* prog)
{
	bool progress = true;
	while (progress)
	{
		if (!lima_pp_hir_reg_narrow(prog))
			return false;
		
		lima_pp_hir_prog_print(prog);
		
		progress = dead_code_eliminate(prog);
	}
	
	return true;
}
