/* Author(s):
 *   Connor Abbott
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

#include "pp_hir/pp_hir.h"
#include "shader.h"

void fill_fs_info(lima_pp_hir_prog_t* prog, lima_shader_info_t* info)
{
	info->fs.stack_size = 1;
	info->fs.stack_offset = 1;
	info->fs.has_discard = false;
	info->fs.reads_color = false;
	info->fs.writes_color = true;
	info->fs.reads_depth = false;
	info->fs.writes_depth = false;
	info->fs.reads_stencil = false;
	info->fs.writes_stencil = false;
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op == lima_pp_hir_op_fb_color)
				info->fs.reads_color = true;
			if (cmd->op == lima_pp_hir_op_fb_depth)
				info->fs.reads_depth = true;
		}
		
		if (block->is_end && block->discard)
			info->fs.has_discard = true;
	}
}
