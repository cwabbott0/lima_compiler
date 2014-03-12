/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Codethink (http://www.codethink.co.uk), Connor Abbott (connor@abbott.cx)
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
#include <stdlib.h>
#include <math.h>

static bool simple_ub_xform(
	lima_pp_hir_cmd_t* cmd,
	lima_pp_hir_op_e op0, lima_pp_hir_op_e op1)
{
	lima_pp_hir_cmd_t* c[2];
	c[0] = lima_pp_hir_cmd_create(op0);
	c[1] = lima_pp_hir_cmd_create(op1);
	if (!c[0] || !c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		return false;
	}

	lima_pp_hir_reg_t ireg = { { cmd->block->prog->reg_alloc++, 0 } };

	c[0]->dst     = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	c[0]->src[0]  = lima_pp_hir_source_copy(cmd->src[1]);

	c[1]->dst           = cmd->dst;
	c[1]->src[0]        = lima_pp_hir_source_copy(cmd->src[0]);
	c[1]->src[1]        = lima_pp_hir_source_default;
	c[1]->src[1].depend = c[0];

	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool scalarize_xform(lima_pp_hir_cmd_t* cmd)
{
	unsigned i, j;
	
	if (cmd->dst.reg.size == 0)
		return false;
	
	lima_pp_hir_cmd_t** c = malloc(sizeof(lima_pp_hir_cmd_t*)
								   * (cmd->dst.reg.size + 1));
	if (!c)
		return false;
	
	lima_pp_hir_cmd_t* combine = lima_pp_hir_combine_create(cmd->dst.reg.size + 1);
	combine->dst = lima_pp_hir_dest_default;
	combine->dst.reg = cmd->dst.reg;
	
	for (i = 0; i <= cmd->dst.reg.size; i++)
	{
		c[i] = lima_pp_hir_cmd_create(cmd->op);
		if (!c[i])
			return false;
		
		c[i]->dst.modifier = cmd->dst.modifier;
		
		lima_pp_hir_reg_t ireg
			= { { cmd->block->prog->reg_alloc++, 0 } };
		c[i]->dst.reg = ireg;
		
		for (j = 0; j < cmd->num_args; j++)
		{
			c[i]->src[j] = lima_pp_hir_source_copy(cmd->src[j]);
			c[i]->src[j].swizzle[0] = cmd->src[j].swizzle[i];
		}
		
		combine->src[i] = lima_pp_hir_source_default;
		combine->src[i].depend = c[i];
	}
	
	lima_pp_hir_cmd_replace_uses(cmd, combine);
	lima_pp_hir_block_replace(cmd, c[0]);
	for (i = 1; i <= combine->dst.reg.size; i++)
		lima_pp_hir_block_insert(c[i], c[i-1]);
	lima_pp_hir_block_insert(combine, c[combine->dst.reg.size]);
	
	free(c);
	return true;
}

static bool sub_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c
		= lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	if (!c) return false;
	c->dst           = cmd->dst;
	c->src[0]        = lima_pp_hir_source_copy(cmd->src[0]);
	c->src[1]        = lima_pp_hir_source_copy(cmd->src[1]);
	c->src[1].negate = !cmd->src[1].negate;
	
	lima_pp_hir_cmd_replace_uses(cmd, c);
	lima_pp_hir_block_replace(cmd, c);
	return true;
}

static bool neg_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
	if (!c)
		return false;
	
	c->dst = cmd->dst;
	c->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c->src[0].negate = !cmd->src[0].negate;
	
	lima_pp_hir_cmd_replace_uses(cmd, c);
	lima_pp_hir_block_replace(cmd, c);
	return true;
}

static bool abs_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
	if (!c)
		return false;
	
	c->dst = cmd->dst;
	c->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c->src[0].absolute = !cmd->src[0].absolute;
	
	lima_pp_hir_cmd_replace_uses(cmd, c);
	lima_pp_hir_block_replace(cmd, c);
	return true;
}

static bool sign_xform(lima_pp_hir_cmd_t* cmd)
{
	double* const0 = malloc(sizeof(double) * 4);
	if (!const0)
		return false;
	const0[0] = const0[1] = const0[2] = const0[3] = 0.0;
	
	double* const1 = malloc(sizeof(double) * 4);
	if (!const1)
	{
		free(const0);
		return false;
	}
	const1[0] = const1[1] = const1[2] = const1[3] = 0.0;
	
	lima_pp_hir_cmd_t* c[3];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_gt);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_gt);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	if (!c[0] || !c[1] || !c[2])
	{
		free(const0);
		free(const1);
		return false;
	}
	
	lima_pp_hir_reg_t ireg[2];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_default;
	c[0]->src[1].constant = true;
	c[0]->src[1].depend = const0;
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].constant = true;
	c[1]->src[0].depend = const1;
	c[1]->src[1] = lima_pp_hir_source_copy(cmd->src[0]);
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[0];
	c[2]->src[1] = lima_pp_hir_source_default;
	c[2]->src[1].depend = c[1];
	c[2]->src[1].negate = true;
	c[2]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[2]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	return true;
}

static bool mod_xform(lima_pp_hir_cmd_t* cmd)
{
	/* from the ESSL spec:
	 * mod(x, y) = x - y*floor(x/y)
	 *
	 * but also,
	 *
	 * fract(x) = x - floor(x) is implemented in hw
	 *
	 * and therefore,
	 *
	 *   mod(x, y)
	 * = x - y*floor(x/y)
	 * = y(x/y - floor(x/y))
	 * = y*fract(x/y)
	 *
	 * implementing mod() this way (using fract()) saves us an instruction.
	 * Clever, Mali guys, clever...
	 */
	
	lima_pp_hir_cmd_t* c[3];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_div);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_fract);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	
	if (!c[0] || !c[1] || !c[2])
		return false;
	
	lima_pp_hir_reg_t ireg[2];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[1]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_copy(cmd->src[1]);
	c[2]->src[1] = lima_pp_hir_source_default;
	c[2]->src[1].depend = c[1];
	c[2]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[2]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	return true;
}

static bool div_xform(lima_pp_hir_cmd_t* cmd)
{
	return simple_ub_xform(
		cmd,
		lima_pp_hir_op_rcp,
		lima_pp_hir_op_mul);
}

static bool normalize_xform(lima_pp_hir_cmd_t* cmd)
{
	//normalize(x) = x * rsqrt(dot(x, x))
	
	lima_pp_hir_cmd_t* c[3];
	
	if (cmd->op == lima_pp_hir_op_normalize2)
		c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_dot2);
	else
		c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_dot4);
	
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_rsqrt);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	if (!c[0] || !c[1] || !c[2])
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		lima_pp_hir_cmd_delete(c[2]);
		return false;
	}
	
	lima_pp_hir_reg_t ireg[2];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, 0 } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, 0 } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[2]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[2]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	return true;
}


static bool const_mul_xform(lima_pp_hir_cmd_t* cmd, lima_pp_hir_op_e new_op,
							double factor)
{
	lima_pp_hir_cmd_t* c[2];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(new_op);
	lima_pp_hir_vec4_t* vfactor
	= (lima_pp_hir_vec4_t*)malloc(sizeof(lima_pp_hir_vec4_t));
	if (!c[0] || !c[1] || !vfactor)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		free(vfactor);
		return false;
	}

	vfactor->x = factor;
	vfactor->y = factor;
	vfactor->z = factor;
	vfactor->w = factor;
	
	lima_pp_hir_reg_t ireg
	= { { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->dst     = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	c[0]->src[0]  = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1]  = lima_pp_hir_source_default;
	c[0]->src[1].constant = true;
	c[0]->src[1].depend   = vfactor;
	
	c[1]->dst           = cmd->dst;
	c[1]->src[0]        = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool sin_xform(lima_pp_hir_cmd_t* cmd)
{
	return const_mul_xform(cmd, lima_pp_hir_op_sin_lut, 0.5 / M_PI);
}

static bool cos_xform(lima_pp_hir_cmd_t* cmd)
{
	return const_mul_xform(cmd, lima_pp_hir_op_cos_lut, 0.5 / M_PI);
}

static bool tan_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[5];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_cos_lut);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_sin_lut);
	c[3] = lima_pp_hir_cmd_create(lima_pp_hir_op_rcp);
	c[4] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	lima_pp_hir_vec4_t* vfactor
		= (lima_pp_hir_vec4_t*)malloc(
			sizeof(lima_pp_hir_vec4_t));
	if (!c[0] || !c[1]
		|| !c[2] || !c[3]
		|| !c[4] || !vfactor)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		lima_pp_hir_cmd_delete(c[2]);
		lima_pp_hir_cmd_delete(c[3]);
		lima_pp_hir_cmd_delete(c[4]);
		free(vfactor);
		return false;
	}

	double factor = (0.5 / M_PI);
	vfactor->x = factor;
	vfactor->y = factor;
	vfactor->z = factor;
	vfactor->w = factor;

	lima_pp_hir_reg_t ireg[4];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[2] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[3] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };

	c[0]->dst     = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	c[0]->src[0]  = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1]  = lima_pp_hir_source_default;
	c[0]->src[1].constant = true;
	c[0]->src[1].depend   = vfactor;

	c[1]->dst     = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	c[1]->src[0]  = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];

	c[2]->dst     = lima_pp_hir_dest_default;
	c[2]->dst.reg = ireg[2];
	c[2]->src[0]  = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[0];

	c[3]->dst     = lima_pp_hir_dest_default;
	c[3]->dst.reg = ireg[3];
	c[3]->src[0]  = lima_pp_hir_source_default;
	c[3]->src[0].depend = c[1];

	c[4]->dst           = cmd->dst;
	c[4]->src[0]        = lima_pp_hir_source_default;
	c[4]->src[0].depend = c[2];
	c[4]->src[1]        = lima_pp_hir_source_default;
	c[4]->src[1].depend = c[3];

	lima_pp_hir_cmd_replace_uses(cmd, c[4]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	lima_pp_hir_block_insert(c[3], c[2]);
	lima_pp_hir_block_insert(c[4], c[3]);
	return true;
}

static bool asin_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[4];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_sqrt);
	c[3] = lima_pp_hir_cmd_create(lima_pp_hir_op_atan2);
	lima_pp_hir_vec4_t* vconst
	= (lima_pp_hir_vec4_t*)malloc(sizeof(lima_pp_hir_vec4_t));
	if (!c[0] || !c[1]
		|| !c[2] || !c[3]
		|| !vconst)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		lima_pp_hir_cmd_delete(c[2]);
		lima_pp_hir_cmd_delete(c[3]);
		free(vconst);
		return false;
	}
	
	vconst->x = 1.0;
	vconst->y = 1.0;
	vconst->z = 1.0;
	vconst->w = 1.0;
	
	lima_pp_hir_reg_t ireg[3];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[2] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].constant = true;
	c[1]->src[0].depend = vconst;
	c[1]->src[1] = lima_pp_hir_source_default;
	c[1]->src[1].depend = c[0];
	c[1]->src[1].negate = true;
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[1];
	c[2]->dst = lima_pp_hir_dest_default;
	c[2]->dst.reg = ireg[2];
	
	c[3]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[3]->src[1] = lima_pp_hir_source_default;
	c[3]->src[1].depend = c[2];
	c[3]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[3]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	lima_pp_hir_block_insert(c[3], c[2]);
	return true;
}

static bool acos_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[4];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_sqrt);
	c[3] = lima_pp_hir_cmd_create(lima_pp_hir_op_atan2);
	lima_pp_hir_vec4_t* vconst
	= (lima_pp_hir_vec4_t*)malloc(sizeof(lima_pp_hir_vec4_t));
	if (!c[0] || !c[1]
		|| !c[2] || !c[3]
		|| !vconst)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		lima_pp_hir_cmd_delete(c[2]);
		lima_pp_hir_cmd_delete(c[3]);
		free(vconst);
		return false;
	}
	
	vconst->x = 1.0;
	vconst->y = 1.0;
	vconst->z = 1.0;
	vconst->w = 1.0;
	
	lima_pp_hir_reg_t ireg[3];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[2] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].constant = true;
	c[1]->src[0].depend = vconst;
	c[1]->src[1] = lima_pp_hir_source_default;
	c[1]->src[1].depend = c[0];
	c[1]->src[1].negate = true;
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[1];
	c[2]->dst = lima_pp_hir_dest_default;
	c[2]->dst.reg = ireg[2];
	
	c[3]->src[0] = lima_pp_hir_source_default;
	c[3]->src[0].depend = c[2];
	c[3]->src[1] = lima_pp_hir_source_copy(cmd->src[0]);
	c[3]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[3]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	lima_pp_hir_block_insert(c[3], c[2]);
	return true;
}

static bool atan_xform(lima_pp_hir_cmd_t* cmd)
{
	if (cmd->dst.reg.size > 0)
		return scalarize_xform(cmd);
	
	/* convert into atan_pt1 and atan_pt2 */
	lima_pp_hir_cmd_t* c[2];
	
	lima_pp_hir_reg_t ireg = { { cmd->block->prog->reg_alloc++, 2 } };
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_atan_pt1);
	if (!c[0])
		return false;
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_atan_pt2);
	if (!c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		return false;
	}
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool atan2_xform(lima_pp_hir_cmd_t* cmd)
{
	if (cmd->dst.reg.size > 0)
		return scalarize_xform(cmd);
	
	/* vec3 %temp1 = atan2_pt1 a, b;
	 * float %temp2 = %temp1.x * %temp1.y;
	 * vec3 %temp3 = combine %temp2, %temp1.yz;
	 * float %result = atan_pt2 %temp3;
	 */
	
	lima_pp_hir_cmd_t* c[4];
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_atan2_pt1);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[2] = lima_pp_hir_combine_create(2);
	c[3] = lima_pp_hir_cmd_create(lima_pp_hir_op_atan_pt2);
	
	if (!c[0] || !c[1] || !c[2] || !c[3])
		return false;
	
	lima_pp_hir_reg_t ireg[3];
	ireg[0] = (lima_pp_hir_reg_t){ { cmd->block->prog->reg_alloc++, 2 } };
	ireg[1] = (lima_pp_hir_reg_t){ { cmd->block->prog->reg_alloc++, 0 } };
	ireg[2] = (lima_pp_hir_reg_t){ { cmd->block->prog->reg_alloc++, 2 } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[1]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->src[0].swizzle[0] = 0;
	c[1]->src[1] = lima_pp_hir_source_default;
	c[1]->src[1].depend = c[0];
	c[1]->src[1].swizzle[0] = 1;
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[1];
	c[2]->src[1] = lima_pp_hir_source_default;
	c[2]->src[1].depend = c[0];
	c[2]->src[1].swizzle[0] = 1;
	c[2]->src[1].swizzle[1] = 2;
	c[2]->dst = lima_pp_hir_dest_default;
	c[2]->dst.reg = ireg[2];
	
	c[3]->src[0] = lima_pp_hir_source_default;
	c[3]->src[0].depend = c[2];
	c[3]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[3]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	lima_pp_hir_block_insert(c[3], c[2]);
	return true;
}

static bool pow_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[3];
	
	/* x^y = exp2(log2(x) * y) */
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_log2);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_exp2);
	
	if (!c[0] || !c[1] || !c[2])
		return false;
	
	lima_pp_hir_reg_t ireg[2];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->src[1] = lima_pp_hir_source_copy(cmd->src[1]);
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[1];
	c[2]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[2]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	return true;
}

static bool exp_xform(lima_pp_hir_cmd_t* cmd)
{
	return const_mul_xform(cmd, lima_pp_hir_op_exp2,
						   1.44269504088896); /* = log2(e) */
}

static bool log_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[2];
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_log2);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	lima_pp_hir_vec4_t* vfactor
	= (lima_pp_hir_vec4_t*)malloc(sizeof(lima_pp_hir_vec4_t));
	if (!c[0] || !c[1] || !vfactor)
		return false;
	
	double factor = 0.693147180559946; /* = 1 / log2(e) */
	vfactor->x = factor;
	vfactor->y = factor;
	vfactor->z = factor;
	vfactor->w = factor;
	
	lima_pp_hir_reg_t ireg
	= { { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->dst     = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	c[0]->src[0]  = lima_pp_hir_source_copy(cmd->src[0]);
	
	c[1]->dst             = cmd->dst;
	c[1]->src[0]          = lima_pp_hir_source_default;
	c[1]->src[0].depend   = c[0];
	c[1]->src[1]          = lima_pp_hir_source_default;
	c[1]->src[1].constant = true;
	c[1]->src[1].depend   = vfactor;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static lima_pp_hir_cmd_t* sum_cmd(lima_pp_hir_source_t src, lima_pp_hir_dest_t dst,
								  unsigned num_components)
{
	lima_pp_hir_cmd_t* cmd = NULL;
	switch (num_components)
	{
		case 2:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
			break;
		case 3:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sum3);
			break;
		case 4:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sum4);
			break;
		default:
			break;
	}
	
	if (!cmd)
		return NULL;
	
	if (num_components == 2)
	{
		cmd->src[0] = lima_pp_hir_source_copy(src);
		cmd->src[1] = lima_pp_hir_source_copy(src);
		cmd->src[1].swizzle[0] = src.swizzle[1];
		cmd->dst = dst;
	}
	else
	{
		cmd->src[0] = lima_pp_hir_source_copy(src);
		cmd->dst = dst;
	}
	
	return cmd;
}

static bool dot2_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[2];
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	
	if (!c[0] || !c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		return false;
	}
	
	lima_pp_hir_reg_t ireg = { { cmd->block->prog->reg_alloc++, 1 } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[1]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->src[0].swizzle[0] = 0;
	c[1]->src[1] = lima_pp_hir_source_default;
	c[1]->src[1].depend = c[0];
	c[1]->src[1].swizzle[0] = 1;
	c[1]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool dot3_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[2];
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_sum3);
	
	if (!c[0] || !c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		return false;
	}
	
	lima_pp_hir_reg_t ireg = { { cmd->block->prog->reg_alloc++, 2 } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[1]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->dst = cmd->dst;

	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool dot4_xform(lima_pp_hir_cmd_t* cmd)
{
	lima_pp_hir_cmd_t* c[2];
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_sum4);
	
	if (!c[0] || !c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		return false;
	}
	
	lima_pp_hir_reg_t ireg = { { cmd->block->prog->reg_alloc++, 3 } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[0]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[1]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg;
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool lrp_xform(lima_pp_hir_cmd_t* cmd)
{
	/* %temp1 = mul %y, %t;
	 * %temp2 = sub 1, %t;
	 * %temp3 = mul %temp2, %x;
	 * %out = add %temp1, %temp3;
	 *
	 * The first two can be done in one cycle, and the second two can be done
	 * in one cycle, meaning the lerp can be done in 2 cycles.
	 */
	
	lima_pp_hir_cmd_t* c[4];
	
	c[0] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	c[2] = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
	c[3] = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
	
	if (!c[0] || !c[1] || !c[2] || !c[3])
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		lima_pp_hir_cmd_delete(c[2]);
		lima_pp_hir_cmd_delete(c[3]);
		return false;
	}
	
	lima_pp_hir_reg_t ireg[3];
	ireg[0] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[1] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	ireg[2] = (lima_pp_hir_reg_t)
		{ { cmd->block->prog->reg_alloc++, cmd->dst.reg.size } };
	
	c[0]->src[0] = lima_pp_hir_source_copy(cmd->src[1]);
	c[0]->src[1] = lima_pp_hir_source_copy(cmd->src[2]);
	c[0]->dst = lima_pp_hir_dest_default;
	c[0]->dst.reg = ireg[0];
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].constant = true;
	double* constant = malloc(4 * sizeof(double));
	if (!constant)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		lima_pp_hir_cmd_delete(c[2]);
		lima_pp_hir_cmd_delete(c[3]);
		return false;
	}
	constant[0] = constant[1] = constant[2] = constant[3] = 1.0;
	c[1]->src[0].depend = constant;
	c[1]->src[1] = lima_pp_hir_source_copy(cmd->src[2]);
	c[1]->src[1].negate = !c[1]->src[1].negate;
	c[1]->dst = lima_pp_hir_dest_default;
	c[1]->dst.reg = ireg[1];
	
	c[2]->src[0] = lima_pp_hir_source_default;
	c[2]->src[0].depend = c[1];
	c[2]->src[1] = lima_pp_hir_source_copy(cmd->src[0]);
	c[2]->dst = lima_pp_hir_dest_default;
	c[2]->dst.reg = ireg[2];
	
	c[3]->src[0] = lima_pp_hir_source_default;
	c[3]->src[0].depend = c[0];
	c[3]->src[1] = lima_pp_hir_source_default;
	c[3]->src[1].depend = c[2];
	c[3]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[3]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	lima_pp_hir_block_insert(c[2], c[1]);
	lima_pp_hir_block_insert(c[3], c[2]);
	return true;
}

static bool any_xform(lima_pp_hir_cmd_t* cmd, unsigned num_components)
{
	lima_pp_hir_cmd_t* c[2];
	
	lima_pp_hir_dest_t dst = lima_pp_hir_dest_default;
	dst.reg = (lima_pp_hir_reg_t) { { cmd->block->prog->reg_alloc++, 0 } };
	c[0] = sum_cmd(cmd->src[0], dst, num_components);
	if (!c[0])
		return false;
	
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_ne);
	if (!c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		return false;
	}
	
	double* constant = malloc(4 * sizeof(double));
	if (!constant)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		return false;
	}
	constant[0] = constant[1] = constant[2] = constant[3] = 0.0;
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->src[1] = lima_pp_hir_source_default;
	c[1]->src[1].constant = true;
	c[1]->src[1].depend = constant;
	c[1]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool all_xform(lima_pp_hir_cmd_t* cmd, unsigned num_components)
{
	lima_pp_hir_cmd_t* c[2];
	
	lima_pp_hir_dest_t dst = lima_pp_hir_dest_default;
	dst.reg = (lima_pp_hir_reg_t) { { cmd->block->prog->reg_alloc++, 0 } };
	c[0] = sum_cmd(cmd->src[0], dst, num_components);
	if (!c[0])
		return false;
	
	c[1] = lima_pp_hir_cmd_create(lima_pp_hir_op_eq);
	if (!c[1])
	{
		lima_pp_hir_cmd_delete(c[0]);
		return false;
	}
	
	double* constant = malloc(4 * sizeof(double));
	if (!constant)
	{
		lima_pp_hir_cmd_delete(c[0]);
		lima_pp_hir_cmd_delete(c[1]);
		return false;
	}
	constant[0] = constant[1] = constant[2] = constant[3] = (double) num_components;
	
	c[1]->src[0] = lima_pp_hir_source_default;
	c[1]->src[0].depend = c[0];
	c[1]->src[1] = lima_pp_hir_source_default;
	c[1]->src[1].constant = true;
	c[1]->src[1].depend = constant;
	c[1]->dst = cmd->dst;
	
	lima_pp_hir_cmd_replace_uses(cmd, c[1]);
	lima_pp_hir_block_replace(cmd, c[0]);
	lima_pp_hir_block_insert(c[1], c[0]);
	return true;
}

static bool any2_xform(lima_pp_hir_cmd_t* cmd)
{
	return any_xform(cmd, 2);
}

static bool any3_xform(lima_pp_hir_cmd_t* cmd)
{
	return any_xform(cmd, 3);
}

static bool any4_xform(lima_pp_hir_cmd_t* cmd)
{
	return any_xform(cmd, 4);
}

static bool all2_xform(lima_pp_hir_cmd_t* cmd)
{
	return all_xform(cmd, 2);
}

static bool all3_xform(lima_pp_hir_cmd_t* cmd)
{
	return all_xform(cmd, 3);
}

static bool all4_xform(lima_pp_hir_cmd_t* cmd)
{
	return all_xform(cmd, 4);
}

bool (*lima_pp_hir_xform[])(lima_pp_hir_cmd_t* cmd) =
{
	NULL,

	neg_xform,
	NULL,
	sub_xform,
	
	NULL,
	NULL,

	NULL,
	scalarize_xform,
	div_xform,

	scalarize_xform,
	scalarize_xform,

	NULL,
	NULL,
	
	normalize_xform,
	NULL,
	normalize_xform,
	
	NULL,

	sin_xform,
	cos_xform,
	tan_xform,
	asin_xform,
	acos_xform,
	
	atan_xform,
	atan2_xform,
	NULL,
	NULL,
	NULL,

	pow_xform,
	exp_xform,
	log_xform,
	scalarize_xform,
	scalarize_xform,
	scalarize_xform,
	scalarize_xform,

	abs_xform,
	sign_xform,
	NULL,
	NULL,
	NULL,
	mod_xform,
	NULL,
	NULL,

	dot2_xform,
	dot3_xform,
	dot4_xform,
	
	lrp_xform,

	NULL,
	NULL,
	NULL,
	NULL,
	any2_xform,
	any3_xform,
	any4_xform,
	all2_xform,
	all3_xform,
	all4_xform,
	NULL,
	
	NULL,
	
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};




unsigned lima_pp_hir_prog_xform(lima_pp_hir_prog_t* prog)
{
	if (!prog)
		return 0;

	unsigned c = 0;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd, *tmp_cmd;
		pp_hir_block_for_each_cmd_safe(block, tmp_cmd, cmd)
		{
			unsigned o = cmd->op;
			if (o >= lima_pp_hir_op_count)
				continue;
			if (lima_pp_hir_xform[o])
			{
				if (lima_pp_hir_xform[o](cmd))
					c++;
			}
		}
	}

	return c;
}
