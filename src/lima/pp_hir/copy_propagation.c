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
#include <assert.h>

/* Tries to replace all uses of a move with the source, and if successful
 * deletes the move. Note that moves in pp_hir can have input modifiers and
 * output modifiers; in that case, we have to propagate the output modifier
 * to the original command, although that only works if it has no other uses,
 * supports output modifiers, and doesn't have an output modifier currently.
 * It also won't work if there are input modifiers, since those are supposed
 * to happen before the output modifiers and if we propagate the output modifier
 * earlier than that won't happen. Input modifiers and swizzles are easier to
 * propagate; when replacing uses, we need to check that the use we're replacing
 * supports input modifiers, and if so than we can modify them appropriately.
 * Ditto for swizzles and not being a store.
 */

//Tries to propagate the output modifier, if any, to the source of the move
static bool try_propagate_outmod(lima_pp_hir_cmd_t* move)
{
	if (move->dst.modifier == lima_pp_outmod_none)
		return true; //Nothing to do here
	
	if (move->src[0].absolute || move->src[0].negate)
		return false;
	
	lima_pp_hir_cmd_t* source = move->src[0].depend;
	if (ptrset_size(source->cmd_uses) > 1 || ptrset_size(source->block_uses) > 0)
		return false;
	
	if (!lima_pp_hir_op[source->op].output_modifiers)
		return false;
	
	if (source->dst.modifier != lima_pp_outmod_none)
		return false;
	
	source->dst.modifier = move->dst.modifier;
	move->dst.modifier = lima_pp_outmod_none;
	return true;
}

static bool try_replace_use(lima_pp_hir_cmd_t* move, lima_pp_hir_cmd_t* use,
							unsigned arg, bool ident_swizzle)
{
	lima_pp_hir_cmd_t* source = move->src[0].depend;
	
	if ((move->src[0].absolute || move->src[0].negate) &&
		!lima_pp_hir_input_modifier(use->op, arg))
		return false;
	
	if (lima_pp_hir_op_is_store(use->op) && arg == 0 && !ident_swizzle)
		return false; //Store inputs can't be swizzled
	
	if (use->op == lima_pp_hir_op_combine &&
		source->dst.reg.size != move->dst.reg.size)
		return false;
	
	//Replace use
	
	if (move->src[0].negate && !use->src[arg].absolute)
		use->src[arg].negate = !use->src[arg].negate;
	
	use->src[arg].absolute = use->src[arg].absolute || move->src[0].absolute;
	
	unsigned i;
	for (i = 0; i < lima_pp_hir_arg_size(use, arg); i++)
	{
		use->src[arg].swizzle[i] = move->src[0].swizzle[use->src[arg].swizzle[i]];
	}
	
	use->src[arg].depend = source;
	
	ptrset_remove(&move->cmd_uses, use);
	ptrset_add(&source->cmd_uses, use);
	
	return true;
}

static bool try_replace_block_use(lima_pp_hir_cmd_t* move,
								  lima_pp_hir_block_t* block)
{
	if (move->src[0].absolute || move->src[0].negate)
		return false;
	
	lima_pp_hir_cmd_t* source = move->src[0].depend;
	
	if (block->is_end)
	{
		if (!block->discard && block->output == move)
		{
			block->output = source;
			ptrset_remove(&move->block_uses, block);
			ptrset_add(&source->block_uses, block);
		}
	}
	else if (block->branch_cond != lima_pp_hir_branch_cond_always)
	{
		if (block->reg_cond_a.reg == move)
		{
			block->reg_cond_a.reg = source;
			ptrset_remove(&move->block_uses, block);
			ptrset_add(&source->block_uses, block);
		}
		if (block->reg_cond_b.reg == move)
		{
			block->reg_cond_b.reg = source;
			ptrset_remove(&move->block_uses, block);
			ptrset_add(&source->block_uses, block);
		}
	}
	
	return true;
}

static bool try_replace_uses(lima_pp_hir_cmd_t* move)
{
	if (!try_propagate_outmod(move))
		return false;
	
	bool ident_swizzle = true;
	unsigned i;
	for (i = 0; i <= move->dst.reg.size; i++)
		if (move->src[0].swizzle[i] != i)
		{
			ident_swizzle = false;
			break;
		}
	
	bool success = true;
	
	lima_pp_hir_cmd_t* use;
	ptrset_iter_t iter = ptrset_iter_create(move->cmd_uses);
	ptrset_iter_for_each(iter, use)
	{
		unsigned arg;
		for (arg = 0; arg < use->num_args; arg++)
		{
			if (use->src[arg].constant || use->src[arg].depend != move)
				continue;
			
			success = success && try_replace_use(move, use, arg, ident_swizzle);
		}
	}
	
	lima_pp_hir_block_t* block;
	iter = ptrset_iter_create(move->block_uses);
	ptrset_iter_for_each(iter, block)
	{
		if (!ident_swizzle)
		{
			success = false;
			break;
		}
		
		success = success && try_replace_block_use(move, block);
	}
	
	return success;
}

void lima_pp_hir_propagate_copies(lima_pp_hir_prog_t* prog)
{
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd, *tmp;
		pp_hir_block_for_each_cmd_safe(block, tmp, cmd)
		{
			if (cmd->op != lima_pp_hir_op_mov ||
				cmd->src[0].constant)
				continue;
			
			if (try_replace_uses(cmd))
			{
				assert(ptrset_size(cmd->cmd_uses) == 0);
				assert(ptrset_size(cmd->block_uses) == 0);
				lima_pp_hir_block_remove(block, cmd);
			}
		}
	}
}
