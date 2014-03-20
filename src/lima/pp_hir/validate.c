/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2014 Connor Abbott (connor@abbott.cx)
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

static void validate_cmd(lima_pp_hir_cmd_t* cmd)
{
	for (unsigned i = 0; i < cmd->num_args; i++)
	{
		if (!cmd->src[i].constant && cmd->src[i].depend)
		{
			lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
			for (unsigned j = 0; j < lima_pp_hir_arg_size(cmd, i); j++)
			{
				assert(cmd->src[i].swizzle[j] <= dep->dst.reg.size);
			}
		}
	}
}

static void validate_block(lima_pp_hir_block_t* block)
{
	lima_pp_hir_cmd_t* cmd;
	pp_hir_block_for_each_cmd(block, cmd)
	{
		validate_cmd(cmd);
	}
}

void lima_pp_hir_prog_validate(lima_pp_hir_prog_t* prog)
{
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		validate_block(block);
	}
}
