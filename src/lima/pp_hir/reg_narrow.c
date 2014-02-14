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


//Gets rid of any extra sources for a combine that aren't actually being used
static bool narrow_combine(lima_pp_hir_cmd_t* cmd)
{
	//Figure out how many sources we actually need
	unsigned cur_index = 0, i;
	for (i = 0; cur_index <= cmd->dst.reg.size; i++)
	{
		lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
		cur_index += dep->dst.reg.size + 1;
	}
	
	unsigned num_sources = i;
	
	if (num_sources == cmd->num_args)
		return true; //Nothing to do here
	
	lima_pp_hir_cmd_t* new_cmd;
	if (num_sources == 1)
	{
		//This combine is really just a move now, lower it
		new_cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
	}
	else
		new_cmd = lima_pp_hir_combine_create(num_sources);
	
	if (!new_cmd)
		return false;
	
	new_cmd->dst = cmd->dst;
	for (i = 0; i < num_sources; i++)
		new_cmd->src[i] = cmd->src[i];
	
	lima_pp_hir_cmd_replace_uses(cmd, new_cmd);
	lima_pp_hir_block_replace(cmd, new_cmd);
	return true;
}

static unsigned get_num_channels_used(lima_pp_hir_cmd_t* cmd, unsigned src)
{
	if (cmd->op == lima_pp_hir_op_combine)
	{
		//For combines, this hack prevents any narrowing since that could
		//change the semantics of the combine
		lima_pp_hir_cmd_t* dep = cmd->src[src].depend;
		return dep->dst.reg.size + 1;
	}
	
	unsigned ret = 0, i;
	for (i = 0; i < lima_pp_hir_arg_size(cmd, src); i++)
		if (cmd->src[src].swizzle[i] >= ret)
			ret = cmd->src[src].swizzle[i] + 1;
	
	return ret;
}

static unsigned get_total_channels_used(lima_pp_hir_cmd_t* cmd)
{
	unsigned ret = 1;
	
	if (lima_pp_hir_op[cmd->op].dest_size)
		ret = lima_pp_hir_op[cmd->op].dest_size;
	
	ptrset_iter_t iter = ptrset_iter_create(cmd->block_uses);
	lima_pp_hir_block_t* block;
	ptrset_iter_for_each(iter, block)
	{
		if (block->is_end && !block->discard && block->output == cmd)
			return 4; //Outputs use all 4 channels
	}
	
	lima_pp_hir_cmd_t* use;
	iter = ptrset_iter_create(cmd->cmd_uses);
	ptrset_iter_for_each(iter, use)
	{
		unsigned src;
		for (src = 0; src < use->num_args; src++)
		{
			if (use->src[src].constant || use->src[src].depend != cmd)
				continue;
			
			unsigned num_channels = get_num_channels_used(use, src);
			if (num_channels > ret)
				ret = num_channels;
		}
	}
	
	return ret;
}

bool lima_pp_hir_reg_narrow(lima_pp_hir_prog_t* prog)
{
	bool progress = true;
	while (progress)
	{
		progress = false;
		
		lima_pp_hir_block_t* block;
		pp_hir_prog_for_each_block(prog, block)
		{
			lima_pp_hir_cmd_t* cmd, *tmp;
			pp_hir_block_for_each_cmd_safe(block, tmp, cmd)
			{
				if (!lima_pp_hir_op[cmd->op].has_dest)
					continue;
				
				unsigned num_channels = get_total_channels_used(cmd);
				if (num_channels <= cmd->dst.reg.size)
				{
					cmd->dst.reg.size = num_channels - 1;
					if (cmd->op == lima_pp_hir_op_combine &&
						!narrow_combine(cmd))
						return false;
					progress = true;
				}
			}
		}
	}
	
	return true;
}
