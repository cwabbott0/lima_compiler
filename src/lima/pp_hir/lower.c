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

#include "../pp_lir/pp_lir.h"
#include <stdlib.h>
#include <string.h>


static bool add_regs(lima_pp_hir_prog_t* prog, lima_pp_lir_prog_t* frag_prog)
{
	unsigned i;
	lima_pp_lir_reg_t* reg;
	
	for (i = 0; i < 6; i++)
	{
		reg = lima_pp_lir_reg_create();
		if (!reg)
		{
			fprintf(stderr, "Error: failed to allocate new register\n");
			return false;
		}
		reg->size = 4;
		reg->index = i;
		reg->precolored = true;
		reg->beginning = true;
		lima_pp_lir_prog_append_reg(frag_prog, reg);
	}
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (!lima_pp_hir_op[cmd->op].has_dest)
				continue;
			
			reg = lima_pp_lir_reg_create();
			if (!reg)
			{
				fprintf(stderr, "Error: failed to allocate new register\n");
				return false;
			}
			reg->size = cmd->dst.reg.size + 1;
			reg->index = cmd->dst.reg.index;
			reg->precolored = false;
			reg->beginning = false;
			lima_pp_lir_prog_append_reg(frag_prog, reg);
		}
	}
	return true;
}

static int block_get_index(lima_pp_hir_block_t* block, lima_pp_hir_prog_t* prog)
{
	lima_pp_hir_block_t* iter_block;
	int i = 0;
	pp_hir_prog_for_each_block(prog, iter_block)
	{
		if (iter_block == block)
			return i;
		i++;
	}

	fprintf(stderr, "Error: could not find index of basic block");
	return -1;
}

static lima_pp_lir_reg_t* get_reg(lima_pp_lir_prog_t* prog, unsigned index)
{
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
		if (prog->regs[i]->index == index && !prog->regs[i]->precolored)
			return prog->regs[i];
	
	fprintf(stderr, "Error: could not find register with index %u\n", index);
	return NULL;
}

static unsigned get_reg_index(lima_pp_lir_prog_t* prog, unsigned index)
{
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
		if (prog->regs[i]->index == index && !prog->regs[i]->precolored)
			return i;
	
	return 0;
}

static bool append_instr(lima_pp_lir_block_t* block,
						 lima_pp_lir_instr_t* instr)
{
	lima_pp_lir_scheduled_instr_t* sched_instr =
		lima_pp_lir_instr_to_sched_instr(instr);
	if (!sched_instr)
		return false;
	
	assert(instr->sched_instr);
	
	lima_pp_lir_block_insert_end(block, sched_instr);
	return true;
}

static lima_pp_lir_instr_t* convert_instr(lima_pp_lir_prog_t* prog,
										   lima_pp_hir_cmd_t* cmd)
{
	unsigned i, j;
	lima_pp_lir_instr_t* instr = lima_pp_lir_instr_create();
	if (!instr)
	{
		fprintf(stderr, "Error: failed to allocate new instruction\n");
		return NULL;
	}
	
	instr->op = cmd->op;
	instr->shift = cmd->shift;
	if (lima_pp_hir_op[instr->op].has_dest)
	{
		instr->dest.modifier = cmd->dst.modifier;
		instr->dest.reg = get_reg(prog, cmd->dst.reg.index);
		if (!instr->dest.reg)
		{
			lima_pp_lir_instr_delete(instr);
			return NULL;
		}
		ptrset_add(&instr->dest.reg->defs, instr);
		for (i = 0; i < instr->dest.reg->size; i++)
			instr->dest.mask[i] = true;
		for (; i < 4; i++)
			instr->dest.mask[i] = false;
	}
	
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		instr->sources[i].absolute = cmd->src[i].absolute;
		instr->sources[i].negate = cmd->src[i].negate;
		for (j = 0; j < 4; j++)
			instr->sources[i].swizzle[j] = cmd->src[i].swizzle[j];

		if (cmd->src[i].constant)
		{
			instr->sources[i].constant = true;
			instr->sources[i].reg = malloc(4 * sizeof(double));
			if (!instr->sources[i].reg)
			{
				fprintf(stderr, "Error: failed to allocate constant\n");
				lima_pp_lir_instr_delete(instr);
				return NULL;
			}
			memcpy(instr->sources[i].reg, cmd->src[i].depend, 4 * sizeof(double));
		}
		else
		{
			lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
			instr->sources[i].constant = false;
			instr->sources[i].reg = get_reg(prog, dep->dst.reg.index);
			if (!instr->sources[i].reg)
			{
				lima_pp_lir_instr_delete(instr);
				return NULL;
			}
			lima_pp_lir_reg_t* reg = instr->sources[i].reg;
			ptrset_add(&reg->uses, instr);
		}
	}
	
	if (lima_pp_hir_op_is_load_store(cmd->op))
		instr->load_store_index = cmd->load_store_index;
	
	return instr;
}

//Lower a combine op into a series of write-masked moves
static bool convert_combine(lima_pp_lir_block_t* block,
							lima_pp_hir_cmd_t* cmd)
{
	lima_pp_lir_reg_t* dest_reg = get_reg(block->prog, cmd->dst.reg.index);
	if (!dest_reg)
		return false;

	unsigned i, j, pos = 0;
	for (i = 0; i < cmd->num_args && pos <= cmd->dst.reg.size; i++)
	{
		if (cmd->src[i].constant)
		{
			fprintf(stderr,
					"Error: constant sources to combine commands are not yet supported\n");
			return false;
		}
		
		lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
		lima_pp_lir_instr_t* instr = lima_pp_lir_instr_create();
		
		instr->op = lima_pp_hir_op_mov;
		instr->dest.reg = dest_reg;
		instr->dest.modifier = cmd->dst.modifier;
		for (j = 0; j < pos; j++)
			instr->dest.mask[j] = false;
		for (; j <= pos + dep->dst.reg.size && j <= cmd->dst.reg.size && j < 4; j++)
			instr->dest.mask[j] = true;
		for (; j < 4; j++)
			instr->dest.mask[j] = false;
		ptrset_add(&dest_reg->defs, instr);

		lima_pp_lir_reg_t* src_reg = get_reg(block->prog, dep->dst.reg.index);
		instr->sources[0].reg = src_reg;
		if (!instr->sources[0].reg)
		{
			lima_pp_lir_instr_delete(instr);
			return false;
		}
		ptrset_add(&src_reg->uses, instr);
		instr->sources[0].absolute = cmd->src[i].absolute;
		instr->sources[0].negate = cmd->src[i].negate;
		for (j = 0; j < pos; j++)
			instr->sources[0].swizzle[j] = cmd->src[i].swizzle[0];
		for (; j <= pos + dep->dst.reg.size && j < 4; j++)
			instr->sources[0].swizzle[j] = cmd->src[i].swizzle[j - pos];
		for (; j < 4; j++)
			instr->sources[0].swizzle[j] = cmd->src[i].swizzle[0];
		
		if (!append_instr(block, instr))
			return false;

		pos += dep->dst.reg.size + 1;
	}
	
	return true;
}


//Creates branch instrs from branch info in hir block
static bool convert_branch(lima_pp_lir_prog_t* frag_prog,
						   lima_pp_hir_prog_t* prog,
						   lima_pp_lir_block_t* frag_block,
						   lima_pp_hir_block_t* block)
{
	if (block->is_end)
		return true;
	
	lima_pp_hir_block_t* next_block;
	if (prog->num_blocks > 1 && block != pp_hir_last_block(block))
		next_block = pp_hir_next_block(block);
	else
		next_block = NULL;
	
	if (block->branch_cond == lima_pp_hir_branch_cond_always)
	{
		if (block->next[0] == next_block)
			return true;
		
		int next_index = block_get_index(block->next[0], prog);
		if (next_index < 0)
			return false;
		
		lima_pp_lir_instr_t* branch_instr = lima_pp_lir_instr_create();
		branch_instr->op = lima_pp_hir_op_branch;
		branch_instr->branch_dest = (unsigned) next_index;

		return append_instr(frag_block, branch_instr);
	}
	
	lima_pp_lir_instr_t* branch_instr = lima_pp_lir_instr_create();
	
	if (block->next[0] == next_block)
	{
		int next_index = block_get_index(block->next[1], prog);
		if (next_index < 0)
			return false;
		
		branch_instr->branch_dest = (unsigned) next_index;
		
		switch (block->branch_cond)
		{
			case lima_pp_hir_branch_cond_gt:
				branch_instr->op = lima_pp_hir_op_branch_le;
				break;
			case lima_pp_hir_branch_cond_eq:
				branch_instr->op = lima_pp_hir_op_branch_ne;
				break;
			case lima_pp_hir_branch_cond_ge:
				branch_instr->op = lima_pp_hir_op_branch_lt;
				break;
			case lima_pp_hir_branch_cond_lt:
				branch_instr->op = lima_pp_hir_op_branch_ge;
				break;
			case lima_pp_hir_branch_cond_ne:
				branch_instr->op = lima_pp_hir_op_branch_eq;
				break;
			case lima_pp_hir_branch_cond_le:
				branch_instr->op = lima_pp_hir_op_branch_gt;
				break;
			default:
				break;
		}
	}
	else
	{
		int next_index = block_get_index(block->next[0], prog);
		if (next_index < 0)
			return false;
		
		branch_instr->branch_dest = (unsigned) next_index;
		
		switch (block->branch_cond)
		{
			case lima_pp_hir_branch_cond_gt:
				branch_instr->op = lima_pp_hir_op_branch_gt;
				break;
			case lima_pp_hir_branch_cond_eq:
				branch_instr->op = lima_pp_hir_op_branch_eq;
				break;
			case lima_pp_hir_branch_cond_ge:
				branch_instr->op = lima_pp_hir_op_branch_ge;
				break;
			case lima_pp_hir_branch_cond_lt:
				branch_instr->op = lima_pp_hir_op_branch_lt;
				break;
			case lima_pp_hir_branch_cond_ne:
				branch_instr->op = lima_pp_hir_op_branch_ne;
				break;
			case lima_pp_hir_branch_cond_le:
				branch_instr->op = lima_pp_hir_op_branch_le;
				break;
			default:
				break;
		}
	}
	
	if (block->reg_cond_a.is_constant)
	{
		double* constant = malloc(sizeof(double) * 4);
		if (!constant)
		{
			lima_pp_lir_instr_delete(branch_instr);
			return false;
		}
		constant[0] = block->reg_cond_a.constant;
		constant[1] = constant[2] = constant[3] = 0.;
		branch_instr->sources[0].reg = constant;
		branch_instr->sources[0].constant = true;
	}
	else
	{
		branch_instr->sources[0].constant = false;
		lima_pp_lir_reg_t* reg = get_reg(frag_prog,
										 block->reg_cond_a.reg->dst.reg.index);
		branch_instr->sources[0].reg = reg;
		ptrset_add(&reg->uses, branch_instr);
		if (!branch_instr->sources[0].reg)
		{
			lima_pp_lir_instr_delete(branch_instr);
			return false;
		}
	}
	
	if (block->reg_cond_b.is_constant)
	{
		double* constant = malloc(sizeof(double) * 4);
		if (!constant)
		{
			lima_pp_lir_instr_delete(branch_instr);
			return false;
		}
		constant[0] = block->reg_cond_b.constant;
		constant[1] = constant[2] = constant[3] = 0.;
		branch_instr->sources[1].reg = constant;
		branch_instr->sources[1].constant = true;
	}
	else
	{
		branch_instr->sources[1].constant = false;
		lima_pp_lir_reg_t* reg = get_reg(frag_prog,
										 block->reg_cond_b.reg->dst.reg.index);
		branch_instr->sources[1].reg = reg;
		ptrset_add(&reg->uses, branch_instr);
		if (!branch_instr->sources[1].reg)
		{
			lima_pp_lir_instr_delete(branch_instr);
			return false;
		}
	}
	
	branch_instr->sources[0].pipeline = branch_instr->sources[1].pipeline = false;
	branch_instr->sources[0].absolute = branch_instr->sources[0].negate =
	branch_instr->sources[1].absolute = branch_instr->sources[1].negate = false;
	branch_instr->sources[0].swizzle[0] = branch_instr->sources[1].swizzle[0] = 0;
	
	if (!append_instr(frag_block, branch_instr))
		return false;
	
	if (block->next[1] != next_block)
	{
		int next_index = block_get_index(block->next[1], prog);
		if (next_index < 0)
			return false;
		
		branch_instr = lima_pp_lir_instr_create();
		branch_instr->op = lima_pp_hir_op_branch;
		branch_instr->branch_dest = (unsigned) next_index;
		
		if (!append_instr(frag_block, branch_instr))
			return false;
	}
	
	return true;
}

static lima_pp_lir_block_t* convert_block(lima_pp_lir_prog_t* frag_prog,
										   lima_pp_hir_prog_t* prog,
										   lima_pp_hir_block_t* block)
{
	unsigned i;
	
	lima_pp_lir_block_t* ret = lima_pp_lir_block_create();
	if (!ret)
		return NULL;
	
	ret->prog = frag_prog;
	
	lima_pp_hir_cmd_t* cmd;
	pp_hir_block_for_each_cmd(block, cmd)
	{
		if (cmd->op == lima_pp_hir_op_phi)
			continue;
		if (cmd->op == lima_pp_hir_op_combine)
		{
			if (!convert_combine(ret, cmd))
			{
				lima_pp_lir_block_delete(ret);
				return NULL;
			}
		}
		else
		{
			lima_pp_lir_instr_t* instr = convert_instr(frag_prog, cmd);
			if (!instr || !append_instr(ret, instr))
			{
				lima_pp_lir_block_delete(ret);
				return NULL;
			}
		}
	}
	
	ret->is_end = block->is_end;
	if (block->is_end)
	{
		ret->num_succs = 0;
		ret->discard = block->discard;
		lima_pp_lir_instr_t* output_instr = lima_pp_lir_instr_create();
		if (!output_instr)
		{
			fprintf(stderr, "Error: failed to allocate output instruction\n");
			lima_pp_lir_block_delete(ret);
			return NULL;
		}

		output_instr->op = lima_pp_hir_op_mov;

		if (ret->discard)
			output_instr->sources[0].constant = true;
		else
			output_instr->sources[0].constant = false;
		output_instr->sources[0].pipeline = false;
		output_instr->sources[0].absolute = false;
		output_instr->sources[0].negate = false;
		for (i = 0; i < 4; i++)
			output_instr->sources[0].swizzle[i] = i;
		if (ret->discard)
		{
			double* constant = malloc(4 * sizeof(double));
			if (!constant)
			{
				lima_pp_lir_block_delete(ret);
				return NULL;
			}
			for (i = 0; i < 4; i++)
				constant[i] = 0.;
			output_instr->sources[0].reg = constant;
			
		}
		else
			output_instr->sources[0].reg = get_reg(frag_prog,
												   block->output->dst.reg.index);
	
		output_instr->dest.reg = frag_prog->regs[0];
		for (i = 0; i < 4; i++)
			output_instr->dest.mask[i] = true;
		output_instr->dest.modifier = lima_pp_outmod_none;

		if (!append_instr(ret, output_instr))
		{
			lima_pp_lir_block_delete(ret);
			return NULL;
		}
		
		ptrset_add(&frag_prog->regs[0]->defs, output_instr);
		
		if (!ret->discard)
		{
			lima_pp_lir_reg_t* reg = output_instr->sources[0].reg;
			ptrset_add(&reg->uses, output_instr);
		}
	}
	else
	{
		if (!convert_branch(frag_prog, prog,
							ret, block))
		{
			fprintf(stderr, "Error: failed to create branch instructions\n");
			lima_pp_lir_block_delete(ret);
			return NULL;
		}
		
		ret->succs[0] = block_get_index(block->next[0], prog);
		if (block->branch_cond != lima_pp_hir_branch_cond_always)
		{
			ret->succs[1] = block_get_index(block->next[1], prog);
			ret->num_succs = 2;
		}
		else
			ret->num_succs = 1;
	}
	
	ret->num_preds = block->num_preds;
	ret->preds = malloc(ret->num_preds * sizeof(unsigned));
	if (!ret->preds)
	{
		fprintf(stderr, "Error: failed to allocate basic block predicates\n");
		lima_pp_lir_block_delete(ret);
		return NULL;
	}
	for (i = 0; i < ret->num_preds; i++)
	{
		int pred_index = block_get_index(block->preds[i], prog);
		if (pred_index < 0)
		{
			lima_pp_lir_block_delete(ret);
			return NULL;
		}
		ret->preds[i] = pred_index;
	}
	
	return ret;
}

static void replace_register(lima_pp_lir_reg_t* old_reg, unsigned old_index,
							 lima_pp_lir_reg_t* new_reg, lima_pp_lir_prog_t* prog)
{
	unsigned i;
	ptrset_iter_t iter;
	lima_pp_lir_instr_t* instr;
	
	iter = ptrset_iter_create(old_reg->defs);
	ptrset_iter_for_each(iter, instr)
	{
		instr->dest.reg = new_reg;
	}
	
	iter = ptrset_iter_create(old_reg->uses);
	ptrset_iter_for_each(iter, instr)
		for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
			if (instr->sources[i].reg == old_reg)
				instr->sources[i].reg = new_reg;
	
	ptrset_union(&new_reg->defs, old_reg->defs);
	ptrset_union(&new_reg->uses, old_reg->uses);
	
	lima_pp_lir_prog_delete_reg(prog, old_index);
	lima_pp_lir_reg_delete(old_reg);
}

//Replaces all sources and destinations of each phi node with a common resource,
//Which in this case is simply the destination register
static void replace_phi_nodes(lima_pp_lir_prog_t* frag_prog, lima_pp_hir_prog_t* prog)
{
	unsigned i;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			if (cmd->op != lima_pp_hir_op_phi)
				break;
			
			lima_pp_lir_reg_t* new_reg = get_reg(frag_prog, cmd->dst.reg.index);
			for (i = 0; i < cmd->num_args; i++)
			{
				lima_pp_hir_cmd_t* depend = cmd->src[i].depend;
				unsigned old_index = get_reg_index(frag_prog, 
												   depend->dst.reg.index);
				replace_register(frag_prog->regs[old_index], old_index, 
								 new_reg, frag_prog);
			}
		}
	}
}

//Check if the beginning flag needs to be set for a register
static void check_beginning(lima_pp_lir_reg_t* reg)
{
	lima_pp_lir_instr_t* instr;
	
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	ptrset_iter_for_each(iter, instr)
	{
		if (lima_pp_hir_op_is_store(instr->op))
		{
			reg->beginning = true;
			return;
		}
	}
	
	iter = ptrset_iter_create(reg->defs);
	ptrset_iter_for_each(iter, instr)
	{
		if (lima_pp_hir_op[instr->op].dest_beginning)
		{
			reg->beginning = true;
			break;
		}
	}
}

lima_pp_lir_prog_t* lima_pp_lir_convert(lima_pp_hir_prog_t* prog)
{
	lima_pp_lir_prog_t* ret = lima_pp_lir_prog_create();
	if (!ret)
		return NULL;
	
	if (!add_regs(prog, ret))
		return false;
	ret->reg_alloc = prog->reg_alloc;
	ret->temp_alloc = prog->temp_alloc;
	
	ret->num_blocks = prog->num_blocks;
	ret->blocks = calloc(sizeof(lima_pp_lir_block_t*) * ret->num_blocks, 1);
	if (!ret->blocks)
	{
		lima_pp_lir_prog_delete(ret);
		return NULL;
	}
	
	unsigned i = 0;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		ret->blocks[i] = convert_block(ret, prog, block);
		if (!ret->blocks[i])
		{
			lima_pp_lir_prog_delete(ret);
			return NULL;
		}
		ret->blocks[i]->prog = ret;
		i++;
	}
	
	replace_phi_nodes(ret, prog);
	
	for (i = 0; i < ret->num_regs; i++)
		check_beginning(ret->regs[i]);
	
	return ret;
}
