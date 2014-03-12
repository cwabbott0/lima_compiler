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

#include "pp_lir.h"
#include <assert.h>

static unsigned uniform_load_width(lima_pp_hir_op_e op)
{
	switch (op)
	{
		case lima_pp_hir_op_loadu_one:
		case lima_pp_hir_op_loadu_one_off:
			return 1;
		case lima_pp_hir_op_loadu_two:
		case lima_pp_hir_op_loadu_two_off:
			return 2;
		case lima_pp_hir_op_loadu_four:
		case lima_pp_hir_op_loadu_four_off:
			return 4;
		default:
			break;
	}
	assert(0);
	return 0;
}

//Given a pipeline register, finds a move instruction with it as a source,
//guarenteeing that the move found is the only ALU instruction
static lima_pp_lir_instr_t* find_single_pipeline_move(
	lima_pp_lir_scheduled_instr_t* instr, lima_pp_lir_pipeline_reg_e in,
	unsigned width)
{
	lima_pp_lir_instr_t* ret = NULL;
	
	unsigned i, j;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
		{
			if (ret)
				return NULL;
			
			if (instr->alu_instrs[i]->op != lima_pp_hir_op_mov)
				return NULL;
			
			if (!instr->alu_instrs[i]->sources[0].pipeline)
				return NULL;
			
			if (instr->alu_instrs[i]->sources[0].pipeline_reg != in)
				return NULL;
			
			if (instr->alu_instrs[i]->sources[0].absolute ||
				instr->alu_instrs[i]->sources[0].negate)
				return NULL;
			
			if (instr->alu_instrs[i]->dest.pipeline)
				return NULL;
			
			if (instr->alu_instrs[i]->dest.reg->precolored)
				return NULL;
			
			if (instr->alu_instrs[i]->dest.modifier != lima_pp_outmod_none)
				return NULL;
			
			for (j = 0; j < width; j++)
				if (!instr->alu_instrs[i]->dest.mask[j] ||
					instr->alu_instrs[i]->sources[0].swizzle[j] != j)
					return NULL;
			
			ret = instr->alu_instrs[i];
		}
	
	return ret;
}

static unsigned alu_instr_pos(lima_pp_lir_scheduled_instr_t* instr,
							  lima_pp_lir_instr_t* alu_instr)
{
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i] == alu_instr)
			return i;
	
	assert(0);
	return 0;
}

static void remove_instr(lima_pp_lir_scheduled_instr_t* instr)
{
	ptrset_iter_t iter = ptrset_iter_create(instr->preds);
	lima_pp_lir_scheduled_instr_t* pred;
	ptrset_iter_for_each(iter, pred)
	{
		ptrset_remove(&pred->succs, instr);
		ptrset_union(&pred->succs, instr->succs);
	}
	
	iter = ptrset_iter_create(instr->succs);
	lima_pp_lir_scheduled_instr_t* succ;
	ptrset_iter_for_each(iter, succ)
	{
		ptrset_remove(&succ->preds, instr);
		ptrset_union(&succ->preds, instr->preds);
	}
	
	iter = ptrset_iter_create(instr->true_preds);
	ptrset_iter_for_each(iter, pred)
	{
		ptrset_remove(&pred->true_succs, instr);
		ptrset_union(&pred->true_succs, instr->true_succs);
	}
	
	iter = ptrset_iter_create(instr->true_succs);
	ptrset_iter_for_each(iter, succ)
	{
		ptrset_remove(&succ->true_preds, instr);
		ptrset_union(&succ->true_preds, instr->true_preds);
	}
	
	iter = ptrset_iter_create(instr->min_preds);
	ptrset_iter_for_each(iter, pred)
	{
		ptrset_remove(&pred->min_succs, instr);
		ptrset_union(&pred->min_succs, instr->min_succs);
	}
	
	iter = ptrset_iter_create(instr->min_succs);
	ptrset_iter_for_each(iter, succ)
	{
		ptrset_remove(&succ->min_preds, instr);
		ptrset_union(&succ->min_preds, instr->min_preds);
	}
	
	lima_pp_lir_block_remove(instr);
}

static lima_pp_lir_instr_t* copy_uniform_instr(lima_pp_lir_instr_t* orig)
{
	lima_pp_lir_instr_t* new = lima_pp_lir_instr_create();
	if (!new)
		return NULL;
	
	new->op = orig->op;
	new->dest = orig->dest;
	new->load_store_index = orig->load_store_index;
	if (lima_pp_hir_op[orig->op].args == 1)
	{
		lima_pp_lir_reg_t* reg = orig->sources[0].reg;
		new->sources[0].reg = reg;
		new->sources[0].swizzle[0] = orig->sources[0].swizzle[0];
		ptrset_add(&reg->uses, new);
	}
	
	return new;
}

static void reg_to_pipeline_reg(lima_pp_lir_instr_t* instr,
								lima_pp_lir_reg_t* reg,
								lima_pp_lir_pipeline_reg_e pipeline_reg)
{
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].pipeline ||
			instr->sources[i].reg != reg)
			continue;
		
		instr->sources[i].reg = NULL;
		instr->sources[i].pipeline = true;
		instr->sources[i].pipeline_reg = pipeline_reg;
		ptrset_remove(&reg->uses, instr);
	}
}

static void delete_reg(lima_pp_lir_reg_t* reg, lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
		if (prog->regs[i] == reg)
		{
			lima_pp_lir_prog_delete_reg(prog, i);
			lima_pp_lir_reg_delete(reg);
			break;
		}
}

//Finds instructions of the form:
//^uniform = loadu
//register = mov ^uniform
//And tries to move the uniform load into uses of the register
static bool peephole_uniform(lima_pp_lir_scheduled_instr_t* instr)
{
	if (!instr->uniform_instr)
		return true;
	
	if (instr->uniform_instr->op != lima_pp_hir_op_loadu_one &&
		instr->uniform_instr->op != lima_pp_hir_op_loadu_one_off &&
		instr->uniform_instr->op != lima_pp_hir_op_loadu_two &&
		instr->uniform_instr->op != lima_pp_hir_op_loadu_two_off &&
		instr->uniform_instr->op != lima_pp_hir_op_loadu_four &&
		instr->uniform_instr->op != lima_pp_hir_op_loadu_four_off)
		return true;
	
	if (instr->varying_instr ||
		instr->texld_instr ||
		instr->temp_store_instr ||
		instr->branch_instr)
		return true;
	
	lima_pp_lir_reg_t* offset = NULL;
	if (lima_pp_hir_op[instr->uniform_instr->op].args == 1)
		offset = instr->uniform_instr->sources[0].reg;
	
	lima_pp_lir_instr_t* move = find_single_pipeline_move(
		instr,
		lima_pp_lir_pipeline_reg_uniform,
		uniform_load_width(instr->uniform_instr->op));
	
	if (!move)
		return true;
	
	lima_pp_lir_reg_t* reg = move->dest.reg;
	if (ptrset_size(reg->defs) > 1)
		return true;
	
	unsigned i = 0;
	while (instr->alu_instrs[i] != move)
		i++;
	
	for (i++; i < 5; i++)
		if (instr->alu_instrs[i] && !instr->alu_instrs[i]->dest.pipeline &&
			instr->alu_instrs[i]->dest.reg == offset)
		{
			return true;
		}
	
	if (instr->temp_store_instr &&
		lima_pp_hir_op[instr->temp_store_instr->op].has_dest &&
		!instr->temp_store_instr->dest.pipeline &&
		 instr->temp_store_instr->dest.reg == offset)
	{
		return true;
	}
	
	lima_pp_lir_instr_t* use_instr;
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	ptrset_iter_for_each(iter, use_instr)
	{
		lima_pp_lir_scheduled_instr_t* use = use_instr->sched_instr;
		
		if (offset && !ptrset_contains(instr->min_succs, use))
			continue;
		
		if (use->uniform_instr)
			continue;
		
		if (use_instr == use->varying_instr)
			continue;
		
		if (use->varying_instr &&
			!use->varying_instr->dest.pipeline &&
			 use->varying_instr->dest.reg == offset)
			continue;
		
		if (use_instr == use->texld_instr)
			continue;
		
		use->uniform_instr = copy_uniform_instr(instr->uniform_instr);
		if (!use->uniform_instr)
			return false;
		use->uniform_instr->sched_instr = use;
		
		for (i = 0; i < 5; i++)
		{
			if (!use->alu_instrs[i])
				continue;
			
			if (use->alu_instrs[i] == use_instr)
			{
				reg_to_pipeline_reg(use_instr, reg,
									lima_pp_lir_pipeline_reg_uniform);
				break;
			}
		}
		
		if (use->temp_store_instr == use_instr)
		{
			reg_to_pipeline_reg(use_instr, reg,
								lima_pp_lir_pipeline_reg_uniform);
			continue;
		}
		
		if (use->branch_instr == use_instr)
		{
			reg_to_pipeline_reg(use_instr, reg,
								lima_pp_lir_pipeline_reg_uniform);
		}
	}
	
	if (ptrset_size(reg->uses) == 0)
	{
		delete_reg(reg, instr->block->prog);
		
		remove_instr(instr);
	}
	
	return true;
}

static lima_pp_lir_instr_t* copy_varying_instr(lima_pp_lir_instr_t* instr)
{
	lima_pp_lir_instr_t* ret = lima_pp_lir_instr_create();
	if (!ret)
		return NULL;
	
	memcpy(ret->dest.mask, instr->dest.mask, 4 * sizeof(bool));
	ret->op = instr->op;
	ret->load_store_index = instr->load_store_index;
	return ret;
}

static void instr_replace_uses(lima_pp_lir_instr_t* instr,
							   lima_pp_lir_reg_t* old,
							   lima_pp_lir_reg_t* new)
{
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
		if (!instr->sources[i].pipeline &&
			instr->sources[i].reg == old)
		{
			instr->sources[i].reg = new;
			ptrset_remove(&old->uses, instr);
			ptrset_add(&new->uses, instr);
		}
}

static void sched_instr_replace_uses(lima_pp_lir_scheduled_instr_t* instr,
									 lima_pp_lir_reg_t* old,
									 lima_pp_lir_reg_t* new)
{
	if (instr->varying_instr)
		instr_replace_uses(instr->varying_instr, old, new);
	
	if (instr->texld_instr)
		instr_replace_uses(instr->texld_instr, old, new);
	
	if (instr->uniform_instr)
		instr_replace_uses(instr->uniform_instr, old, new);
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			instr_replace_uses(instr->alu_instrs[i], old, new);
	
	if (instr->temp_store_instr)
		instr_replace_uses(instr->temp_store_instr, old, new);
	
	if (instr->branch_instr)
		instr_replace_uses(instr->branch_instr, old, new);
}

static bool is_proj_or_cube(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_texld_2d_proj_z
	|| op == lima_pp_hir_op_texld_2d_proj_z_off
	|| op == lima_pp_hir_op_texld_2d_proj_z_lod
	|| op == lima_pp_hir_op_texld_2d_proj_z_off_lod
	|| op == lima_pp_hir_op_texld_2d_proj_w
	|| op == lima_pp_hir_op_texld_2d_proj_w_off
	|| op == lima_pp_hir_op_texld_2d_proj_w_lod
	|| op == lima_pp_hir_op_texld_2d_proj_w_off_lod
	|| op == lima_pp_hir_op_texld_cube
	|| op == lima_pp_hir_op_texld_cube_off
	|| op == lima_pp_hir_op_texld_cube_lod
	|| op == lima_pp_hir_op_texld_cube_off_lod;
}

static bool has_texload_use(lima_pp_lir_scheduled_instr_t* instr,
							lima_pp_lir_reg_t* reg)
{
	if (!instr->texld_instr || !instr->varying_instr)
		return false;
	
	if (instr->varying_instr->op != lima_pp_hir_op_mov)
		return false;
	
	if (!instr->varying_instr->dest.pipeline)
		return false;
	
	if (instr->varying_instr->sources[0].reg != reg)
		return false;
	
	if (instr->varying_instr->sources[0].negate ||
		instr->varying_instr->sources[0].absolute)
		return false;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		if (instr->varying_instr->dest.mask[i] &&
			instr->varying_instr->sources[0].swizzle[i] != i)
			return false;
	
	return true;
}

static bool instr_has_use(lima_pp_lir_instr_t* instr,
						  lima_pp_lir_reg_t* reg)
{
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
		if (!instr->sources[i].pipeline &&
			instr->sources[i].reg == reg)
			return true;
	
	return false;
}

//if has_texload_use() returns true, tells if there are any other uses of the
//register

static bool has_non_texload_use(lima_pp_lir_scheduled_instr_t* instr,
								lima_pp_lir_reg_t* reg)
{
	if (instr->texld_instr && instr_has_use(instr->texld_instr, reg))
		return true;
	
	if (instr->uniform_instr && instr_has_use(instr->uniform_instr, reg))
		return true;
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i] && instr_has_use(instr->alu_instrs[i], reg))
			return true;
	
	if (instr->temp_store_instr && instr_has_use(instr->temp_store_instr, reg))
		return true;
	
	if (instr->branch_instr && instr_has_use(instr->branch_instr, reg))
		return true;
	
	return false;
}


//Splits up the varying definition & moves it before each use, taking care of
//cases where it's used directly as a texture coordinate. 
static bool peephole_varying(lima_pp_lir_scheduled_instr_t* instr,
							 bool* progress)
{
	*progress = false;
	
	if (!instr->varying_instr || instr->varying_instr->dest.pipeline ||
		instr->varying_instr->op == lima_pp_hir_op_mov ||
		instr->varying_instr->op == lima_pp_hir_op_normalize3)
		return true;
	
	lima_pp_lir_reg_t* reg = instr->varying_instr->dest.reg;
	if (ptrset_size(reg->defs) > 1)
		return true;
	
	//To help make sure we don't run this pass twice when unneccesary, bail out
	//if there are any other instructions in the scheduled instruction other
	//than the varying load instruction... the linear->scheduled code will
	//always put varying instructions by themselves, so if there's anything else
	//than we know this pass has been run already
	
	if (instr->texld_instr ||
		instr->uniform_instr ||
		instr->temp_store_instr ||
		instr->branch_instr)
		return true;
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			return true;
	
	if (lima_pp_hir_op[instr->varying_instr->op].args == 1)
	{
		//Thinking about the case where there's an offset makes my brain hurt,
		//and apparently the binary compiler doesn't bother either so let's
		//just bail out... even if we bail out if the # of def's for the offset
		//register is > 1, then we still have to deal with the fact that the
		//varying instr will have a predecessor, meaning that when we move
		//it into the uses, we could introduce a triangle of dependencies which
		//would be a Bad Thing (tm) for other passes
		
		return true;
	}
	
	//Will this pass actually do anything?
	
	if (ptrset_size(instr->succs) == 0)
		return true;
	
	if (ptrset_size(instr->succs) == 1)
	{
		lima_pp_lir_scheduled_instr_t* use = ptrset_first(instr->succs);
		
		if (!has_texload_use(use, reg) ||
			(is_proj_or_cube(use->texld_instr->op) &&
			 has_non_texload_use(use, reg)))
		{
			return true;
		}
	}
	
	//Try to combine uses, in order to not hinder scheduling... if we push the
	//varying definition down to two instructions, which could be combined
	//together, then we could hinder scheduling down the line since the
	//scheduler won't be able to combine those instructions
	
	ptrset_t unprocessed;
	if (!ptrset_copy(&unprocessed, instr->min_succs))
		return false;
	
	while (ptrset_size(unprocessed))
	{
		lima_pp_lir_scheduled_instr_t* use = ptrset_first(unprocessed);
		
		lima_pp_lir_scheduled_instr_t* other;
		ptrset_iter_t iter = ptrset_iter_create(instr->succs);
		ptrset_iter_for_each(iter, other)
		{
			if (other == use)
				continue;
			
			if (ptrset_contains(instr->min_succs, other))
			{
				if (lima_pp_lir_instr_combine_indep(use, other))
				{
					lima_pp_lir_block_remove(other);
					ptrset_remove(&unprocessed, other);
				}
			}
			else if (ptrset_contains(use->min_succs, other))
			{
				if (lima_pp_lir_instr_combine_after(other, use))
					lima_pp_lir_block_remove(other);
			}
		}
		
		ptrset_remove(&unprocessed, use);
	}
	
	ptrset_delete(unprocessed);
	
	//Now, move the varying definition into each instruction in instr->succs
	lima_pp_lir_scheduled_instr_t* use;
	ptrset_iter_t iter = ptrset_iter_create(instr->succs);
	ptrset_iter_for_each(iter, use)
	{
		if (has_texload_use(use, reg) &&
			(!is_proj_or_cube(use->texld_instr->op) ||
			 !has_non_texload_use(use, reg)))
		{
			lima_pp_lir_instr_delete(use->varying_instr);
			use->varying_instr = copy_varying_instr(instr->varying_instr);
			if (!use->varying_instr)
				return false;
			
			use->varying_instr->sched_instr = use;
			
			if (has_non_texload_use(use, reg))
			{
				lima_pp_lir_reg_t* new_reg = lima_pp_lir_reg_create();
				if (!new_reg)
					return false;
				
				new_reg->index = instr->block->prog->reg_alloc++;
				new_reg->precolored = false;
				new_reg->size = reg->size;
				new_reg->beginning = true;
				
				
				if (!lima_pp_lir_prog_append_reg(instr->block->prog, new_reg))
				{
					lima_pp_lir_reg_delete(new_reg);
					return false;
				}
				
				use->varying_instr->dest.pipeline = false;
				use->varying_instr->dest.reg = new_reg;
				ptrset_add(&new_reg->defs, use->varying_instr);
				
				sched_instr_replace_uses(use, reg, new_reg);
			}
			else
			{
				use->varying_instr->dest.pipeline = true;
				use->varying_instr->dest.pipeline_reg =
					lima_pp_lir_pipeline_reg_discard;
			}
		}
		else
		{
			lima_pp_lir_instr_t* varying_instr =
				copy_varying_instr(instr->varying_instr);
			if (!varying_instr)
				return false;
			
			lima_pp_lir_reg_t* new_reg = lima_pp_lir_reg_create();
			if (!new_reg)
			{
				lima_pp_lir_instr_delete(varying_instr);
				return false;
			}
			
			new_reg->index = instr->block->prog->reg_alloc++;
			new_reg->precolored = false;
			new_reg->size = reg->size;
			new_reg->beginning = true;
			
			if (!lima_pp_lir_prog_append_reg(instr->block->prog, new_reg))
			{
				lima_pp_lir_reg_delete(new_reg);
				lima_pp_lir_instr_delete(varying_instr);
				return false;
			}
			
			varying_instr->dest.pipeline = false;
			varying_instr->dest.reg = new_reg;
			ptrset_add(&new_reg->defs, varying_instr);
			
			sched_instr_replace_uses(use, reg, new_reg);
			
			if (use->varying_instr)
			{
				lima_pp_lir_scheduled_instr_t* new_def =
					lima_pp_lir_scheduled_instr_create();
				if (!new_def)
				{
					lima_pp_lir_instr_delete(varying_instr);
					return false;
				}
				
				new_def->varying_instr = varying_instr;
				new_def->varying_instr->sched_instr = new_def;
				
				lima_pp_lir_block_insert_before(new_def, use);
				
				ptrset_add(&use->preds, new_def);
				ptrset_add(&new_def->succs, use);
				ptrset_add(&use->true_preds, new_def);
				ptrset_add(&new_def->true_succs, use);
				ptrset_add(&use->min_preds, new_def);
				ptrset_add(&new_def->min_succs, use);
			}
			else
			{
				use->varying_instr = varying_instr;
				use->varying_instr->sched_instr = use;
			}
		}
	}
	
	delete_reg(reg, instr->block->prog);
	remove_instr(instr);
	
	*progress = true;
	
	return true;
}

//Moves uses of a texld instruction into the instruction, hopefully replacing
//uses of the register with pipeline registers so that we can delete the
//move instruction and register
static bool peephole_texture(lima_pp_lir_scheduled_instr_t* instr)
{
	if (!instr->texld_instr)
		return true;
	
	if (instr->temp_store_instr ||
		instr->branch_instr)
		return true;
	
	lima_pp_lir_instr_t* move =
		find_single_pipeline_move(instr, lima_pp_lir_pipeline_reg_sampler, 4);
	if (!instr)
		return true;
	
	lima_pp_lir_reg_t* reg = move->dest.reg;
	
	ptrset_t sched_uses;
	if (!ptrset_create(&sched_uses))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	lima_pp_lir_instr_t* use_instr;
	ptrset_iter_for_each(iter, use_instr)
	{
		lima_pp_lir_scheduled_instr_t* sched_use = use_instr->sched_instr;
		if (sched_use == instr)
			continue;
		
		ptrset_add(&sched_uses, sched_use);
	}

	iter = ptrset_iter_create(sched_uses);
	lima_pp_lir_scheduled_instr_t* use;
	ptrset_iter_for_each(iter, use)
	{
		if (!ptrset_contains(instr->min_succs, use))
			continue;
		
		unsigned move_instr_pos;
		bool move_instr_possible[5];
		bool removed_move = false;
		if (ptrset_size(sched_uses) == 1 &&
			(!use->temp_store_instr ||
			 !ptrset_contains(reg->uses, use->temp_store_instr)) &&
			(!use->branch_instr ||
			 !ptrset_contains(reg->uses, use->branch_instr)) &&
			(!instr->temp_store_instr ||
			 !ptrset_contains(reg->uses, instr->temp_store_instr)) &&
			(!instr->branch_instr ||
			 !ptrset_contains(reg->uses, instr->branch_instr)))
		{
			//Optimization: if there's only one use, then if we can merge it
			//successfully then we don't need the move instruction, so
			//temporarily remove it and only add it back in if merging the
			//instructions doesn't work
			
			move_instr_pos = alu_instr_pos(instr, move);
			unsigned i;
			for (i = 0; i < 5; i++)
				move_instr_possible[i] =
					instr->possible_alu_instr_pos[move_instr_pos][i];
			
			instr->alu_instrs[move_instr_pos] = NULL;
			for (i = 0; i < 5; i++)
				instr->possible_alu_instr_pos[move_instr_pos][i] = false;
			
			removed_move = true;
		}
		
		if (lima_pp_lir_instr_combine_after(use, instr))
		{
			lima_pp_lir_block_remove(use);
			ptrset_remove(&sched_uses, use);
			
			unsigned i;
			for (i = 0; i < 5; i++)
				if (ptrset_contains(reg->uses, instr->alu_instrs[i]))
					reg_to_pipeline_reg(instr->alu_instrs[i], reg,
										lima_pp_lir_pipeline_reg_sampler);
			
			if (removed_move)
			{
				lima_pp_lir_instr_delete(move);
				delete_reg(reg, instr->block->prog);
			}
			
			continue;
		}
		
		if (removed_move)
		{
			//Undo the optimization since it didn't work
			
			instr->alu_instrs[move_instr_pos] = move;
			unsigned i;
			for (i = 0; i < 5; i++)
				instr->possible_alu_instr_pos[move_instr_pos][i] =
					move_instr_possible[i];
		}
	}
	
	ptrset_delete(sched_uses);
	
	return true;
}

//Checks to make sure an instruction has only one ALU instruction and nothing
//else, returning that instruction
static lima_pp_lir_instr_t* get_single_alu_instr(
	lima_pp_lir_scheduled_instr_t* instr, unsigned* pos)
{
	if (instr->varying_instr)
		return NULL;
	
	if (instr->texld_instr)
		return NULL;
	
	if (instr->uniform_instr)
		return NULL;
	
	lima_pp_lir_instr_t* ret = NULL;
	unsigned i;
	for (i = 0; i < 5; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		if (ret)
			return NULL;
		else
		{
			ret = instr->alu_instrs[i];
			*pos = i;
		}
	}
	
	if (!ret)
		return NULL;
	
	if (instr->temp_store_instr)
		return NULL;
	
	if (instr->branch_instr)
		return NULL;
	
	return ret;
}

static void pin_alu_instr(lima_pp_lir_instr_t* instr, unsigned old_pos,
						  unsigned new_pos)
{
	lima_pp_lir_scheduled_instr_t* sched_instr = instr->sched_instr;
	
	sched_instr->alu_instrs[old_pos] = NULL;
	sched_instr->alu_instrs[new_pos] = instr;
	
	unsigned i;
	for (i = 0; i < 5; i++)
		sched_instr->possible_alu_instr_pos[old_pos][i] = false;
	
	for (i = 0; i < new_pos; i++)
		sched_instr->possible_alu_instr_pos[new_pos][i] = false;
	
	sched_instr->possible_alu_instr_pos[new_pos][new_pos] = true;
	
	for (i++; i < 5; i++)
		sched_instr->possible_alu_instr_pos[new_pos][i] = false;
}

static bool peephole_mul_add(lima_pp_lir_scheduled_instr_t* instr,
							 bool* progress)
{
	unsigned mul_pos;
	
	*progress = false;
	
	lima_pp_lir_instr_t* mul_instr = get_single_alu_instr(instr, &mul_pos);
	if (!mul_instr)
		return true;
	
	if (mul_instr->op == lima_pp_hir_op_mov)
		return true;
	
	bool mul_scalar =
		instr->possible_alu_instr_pos[mul_pos][lima_pp_lir_alu_scalar_mul];
	bool mul_vector =
		instr->possible_alu_instr_pos[mul_pos][lima_pp_lir_alu_vector_mul];
	
	if (!mul_scalar && !mul_vector)
		return true;
	
	assert(!mul_instr->dest.pipeline);
	
	lima_pp_lir_reg_t* reg = mul_instr->dest.reg;
	
	lima_pp_lir_scheduled_instr_t* succ;
	ptrset_iter_t iter = ptrset_iter_create(instr->min_succs);
	ptrset_iter_for_each(iter, succ)
	{
		unsigned add_pos;
		lima_pp_lir_instr_t* add_instr = get_single_alu_instr(succ, &add_pos);
		if (!add_instr)
			continue;
		
		if (add_instr->op == lima_pp_hir_op_mov)
			continue;
		
		if (!ptrset_contains(reg->uses, add_instr))
			continue;
		
		bool add_scalar =
			succ->possible_alu_instr_pos[add_pos][lima_pp_lir_alu_scalar_add];
		bool add_vector =
			succ->possible_alu_instr_pos[add_pos][lima_pp_lir_alu_vector_add];
		
		if (!(add_scalar && mul_scalar) && !(add_vector && mul_vector))
			continue;
		
		bool scalar = add_scalar && mul_scalar;
		
		//Swap inputs if neccessary
		
		if (add_instr->sources[0].reg != reg)
		{
			if (!lima_pp_hir_op[add_instr->op].commutative)
				continue;
			
			lima_pp_lir_source_t temp = add_instr->sources[0];
			add_instr->sources[0] = add_instr->sources[1];
			add_instr->sources[1] = temp;
			assert(add_instr->sources[0].reg == reg);
		}
		
		pin_alu_instr(mul_instr, mul_pos, scalar ? lima_pp_lir_alu_scalar_mul :
													lima_pp_lir_alu_vector_mul);
		
		pin_alu_instr(add_instr, add_pos, scalar ? lima_pp_lir_alu_scalar_add :
													lima_pp_lir_alu_vector_add);
		
		if (lima_pp_lir_instr_combine_after(succ, instr))
		{
			lima_pp_lir_block_remove(succ);
			add_instr->sources[0].reg = NULL;
			add_instr->sources[0].pipeline = true;
			add_instr->sources[0].pipeline_reg =
				scalar ? lima_pp_lir_pipeline_reg_fmul :
						  lima_pp_lir_pipeline_reg_vmul;
			
			if (lima_pp_hir_op[add_instr->op].args < 2 ||
				add_instr->sources[1].reg != reg)
				ptrset_remove(&reg->uses, add_instr);
			
			if (ptrset_size(reg->uses) == 0)
			{
				mul_instr->dest.reg = NULL;
				mul_instr->dest.pipeline = true;
				mul_instr->dest.pipeline_reg = lima_pp_lir_pipeline_reg_discard;
				ptrset_remove(&reg->defs, mul_instr);
				if (ptrset_size(reg->defs) == 0)
				{
					delete_reg(reg, instr->block->prog);
				}
			}
			
			*progress = true;
			return true;
		}
		else
			return true;
	}
	
	return true;
}

static lima_pp_lir_instr_t* find_discard_move(lima_pp_lir_scheduled_instr_t* instr,
											  unsigned* pos)
{
	unsigned i, j;
	for (i = 0; i < 5; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		if (instr->alu_instrs[i]->op != lima_pp_hir_op_mov)
			continue;
		
		if (instr->alu_instrs[i]->sources[0].pipeline)
			continue;
		
		if (instr->alu_instrs[i]->sources[0].absolute ||
			instr->alu_instrs[i]->sources[0].negate)
			continue;
		
		if (!instr->alu_instrs[i]->dest.pipeline)
			continue;
		
		if (instr->alu_instrs[i]->dest.pipeline_reg !=
			lima_pp_lir_pipeline_reg_discard)
			continue;
		
		lima_pp_lir_reg_t* src = instr->alu_instrs[i]->sources[0].reg;
		if (src->precolored)
			continue;
		
		if (instr->alu_instrs[i]->dest.modifier != lima_pp_outmod_none)
			continue;
		
		for (j = 0; j < 4; j++)
			if (!instr->alu_instrs[i]->dest.mask[j] ||
				instr->alu_instrs[i]->sources[0].swizzle[j] != j)
				continue;
		
		*pos = i;
		return instr->alu_instrs[i];
	}
	
	return NULL;
}

static bool is_alu_instr(lima_pp_lir_instr_t* instr)
{
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->sched_instr->alu_instrs[i] == instr)
			return true;
	
	return false;
}

//Detects patterns of the form:
//%some_reg = ...
//^discard = %some_reg
//And, if possible, removes some_reg, assigning to ^discard directly.
//Helps selects, where linear_to_scheduled will produce something like the above
static bool peephole_discard_move(lima_pp_lir_scheduled_instr_t* instr,
								   bool* progress)
{
	*progress = false;
	
	unsigned move_pos;
	lima_pp_lir_instr_t* move = find_discard_move(instr, &move_pos);
	
	if (!move)
		return true;
	
	lima_pp_lir_reg_t* reg = move->sources[0].reg;
	
	if (ptrset_size(reg->defs) > 1)
		return true;
	
	lima_pp_lir_instr_t* def = ptrset_first(reg->defs);
	
	if (!is_alu_instr(def))
		return true;
	
	if (!ptrset_contains(instr->min_preds, def->sched_instr))
		return true;
	
	unsigned def_pos = alu_instr_pos(def->sched_instr, def);
	if (!def->sched_instr->possible_alu_instr_pos[def_pos][move_pos])
		return true;
	
	unsigned i;
	for (i = def_pos + 1; i < 5; i++)
		if (def->sched_instr->alu_instrs[i] &&
			!lima_pp_lir_instr_can_swap(def, def->sched_instr->alu_instrs[i]))
			return true;
	
	if (def->sched_instr->temp_store_instr &&
		!lima_pp_lir_instr_can_swap(def, def->sched_instr->temp_store_instr))
		return true;
	
	if (def->sched_instr->branch_instr &&
		!lima_pp_lir_instr_can_swap(def, def->sched_instr->branch_instr))
		return true;
	
	if (instr->varying_instr &&
		!lima_pp_lir_instr_can_swap(def, instr->varying_instr))
		return true;
	
	if (instr->texld_instr &&
		!lima_pp_lir_instr_can_swap(def, instr->texld_instr))
		return true;
	
	if (instr->uniform_instr &&
		!lima_pp_lir_instr_can_swap(def, instr->uniform_instr))
		return true;
	
	for (i = 0; i < move_pos; i++)
		if (instr->alu_instrs[i] &&
			!lima_pp_lir_instr_can_swap(def, instr->alu_instrs[i]))
			return true;
	
	for (i = 0; i < 5; i++)
	{
		instr->possible_alu_instr_pos[move_pos][i] =
			def->sched_instr->possible_alu_instr_pos[def_pos][i];
		
		def->sched_instr->possible_alu_instr_pos[def_pos][i] = false;
	}
	
	def->sched_instr->alu_instrs[def_pos] = NULL;
	
	if (lima_pp_lir_sched_instr_is_empty(def->sched_instr))
	{
		lima_pp_lir_instr_combine_before(def->sched_instr, instr);
		lima_pp_lir_block_remove(def->sched_instr);
	}
	
	def->sched_instr = instr;
	ptrset_remove(&reg->uses, move);
	lima_pp_lir_instr_delete(move);
	instr->alu_instrs[move_pos] = def;
	if (ptrset_size(reg->uses) == 0)
	{
		def->dest.reg = NULL;
		def->dest.pipeline = true;
		def->dest.pipeline_reg = lima_pp_lir_pipeline_reg_discard;
		delete_reg(reg, instr->block->prog);
	}
	
	*progress = true;
	return true;
}

static bool peephole_block(lima_pp_lir_block_t* block)
{
	bool progress = true;
	lima_pp_lir_scheduled_instr_t* instr;
	while (progress)
	{
		progress = false;
		pp_lir_block_for_each_instr(block, instr)
		{
			if (!peephole_discard_move(instr, &progress))
				return false;
			
			if (progress)
				break;
		}
	}
	
	progress = true;
	while (progress)
	{
		progress = false;
		pp_lir_block_for_each_instr(block, instr)
		{
			if (!peephole_mul_add(instr, &progress))
				return false;
			
			if (progress)
				break;
		}
	}
	
	lima_pp_lir_calc_min_dep_info(block);
	
	lima_pp_lir_scheduled_instr_t* tmp;
	pp_lir_block_for_each_instr_safe(block, tmp, instr)
	{
		if (!peephole_uniform(instr))
			return false;
	}
	
	lima_pp_lir_calc_min_dep_info(block);
	
	progress = true;
	while (progress)
	{
		progress = false;
		pp_lir_block_for_each_instr(block, instr)
		{
			if (!peephole_varying(instr, &progress))
				return false;
			
			if (progress)
				break;
		}
	}
	
	lima_pp_lir_calc_min_dep_info(block);
	
	pp_lir_block_for_each_instr(block, instr)
	{
		if (!peephole_texture(instr))
			return false;
	}
	
	return true;
}

bool lima_pp_lir_peephole(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
		if (!peephole_block(prog->blocks[i]))
			return false;
	
	return true;
}
