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
#include "../pp/lima_pp.h"
#include "hfloat.h"
#include <assert.h>

static bool is_scalar_temp_load(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_loadt_one ||
		op == lima_pp_hir_op_loadt_one_off;
}

static bool is_scalar_temp_store(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_storet_one ||
		op == lima_pp_hir_op_storet_one_off;
}

static bool is_vector_temp_load(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_loadt_four ||
		op == lima_pp_hir_op_loadt_four_off;
}

static bool is_vector_temp_store(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_storet_four ||
		op == lima_pp_hir_op_storet_four_off;
}

static void offset_temporaries(lima_pp_lir_prog_t* prog)
{
	unsigned offset = 0x10000 - prog->temp_alloc;
	
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_scheduled_instr_t* instr;
		pp_lir_block_for_each_instr(prog->blocks[i], instr)
		{
			if (instr->uniform_instr)
			{
				if (is_scalar_temp_load(instr->uniform_instr->op))
					instr->uniform_instr->load_store_index += offset * 4;
				if (is_vector_temp_load(instr->uniform_instr->op))
					instr->uniform_instr->load_store_index += offset;
			}
			
			if (instr->temp_store_instr)
			{
				if (is_scalar_temp_store(instr->temp_store_instr->op))
					instr->temp_store_instr->load_store_index += offset * 4;
				if (is_vector_temp_store(instr->temp_store_instr->op))
					instr->temp_store_instr->load_store_index += offset;
			}
		}
	}
}

typedef struct {
	unsigned size, start;
	lima_pp_instruction_t* instrs;
	unsigned dest1, dest2;
	bool discard;
} pp_asm_block_t;

static unsigned get_mask(bool* mask)
{
	unsigned ret = 0, i;
	for (i = 0; i < 4; i++)
		if (mask[i])
			ret |= (1 << i);
	return ret;
}

//assumes the mask has only 1 component, returns which one
static unsigned get_dest_component(bool* mask)
{
	unsigned i;
	for (i = 0; i < 4; i++)
		if (mask[i])
			return i;
	
	assert(0);
	return 0;
}

static unsigned get_swizzle(unsigned* swizzle)
{
	unsigned ret = 0, i;
	for (i = 0; i < 4; i++)
		ret |= (swizzle[i] & 3) << (i * 2);
	return ret;
}

static unsigned get_source(lima_pp_lir_source_t src)
{
	if (src.pipeline)
	{
		switch (src.pipeline_reg)
		{
			case lima_pp_lir_pipeline_reg_const0:
				return lima_pp_vec4_reg_constant0;
			case lima_pp_lir_pipeline_reg_const1:
				return lima_pp_vec4_reg_constant1;
			case lima_pp_lir_pipeline_reg_sampler:
				return lima_pp_vec4_reg_texture;
			case lima_pp_lir_pipeline_reg_uniform:
				return lima_pp_vec4_reg_uniform;
			default:
				break;
		}
	}
	
	lima_pp_lir_reg_t* src_reg = src.reg;
	return src_reg->index;
}

static void emit_varying_instr(lima_pp_lir_instr_t* instr,
							   lima_pp_field_varying_t* field, bool is_texture_cube,
							   bool proj_z, bool proj_w)
{
	field->imm.mask = get_mask(instr->dest.mask);
	if (instr->dest.pipeline)
	{
		switch (instr->dest.pipeline_reg)
		{
			case lima_pp_lir_pipeline_reg_discard:
				field->imm.dest = lima_pp_vec4_reg_discard;
				break;
			default:
				assert(0);
				break;
		}
	}
	else
		field->imm.dest = instr->dest.reg->index;
	
	switch (instr->op)
	{
		case lima_pp_hir_op_mov:
		case lima_pp_hir_op_normalize3:
			if (proj_z)
				field->reg.perspective = 2;
			else if (proj_w)
				field->reg.perspective = 3;
			else if (is_texture_cube)
			{
				field->reg.source_type = 2;
				field->reg.perspective = 1;
			}
			else if (instr->op == lima_pp_hir_op_normalize3)
			{
				field->reg.source_type = 2;
				field->reg.perspective = 2;
				field->reg.normalize = true;
			}
			else
				field->reg.source_type = 1;
			
			field->reg.swizzle = get_swizzle(instr->sources[0].swizzle);
			field->reg.negate = instr->sources[0].negate;
			field->reg.absolute = instr->sources[0].absolute;
			
			assert(!instr->sources[0].pipeline);
			lima_pp_lir_reg_t* src_reg = instr->sources[0].reg;
			field->reg.source = src_reg->index;
			break;
			
		case lima_pp_hir_op_loadv_one:
		case lima_pp_hir_op_loadv_one_off:
		case lima_pp_hir_op_loadv_two:
		case lima_pp_hir_op_loadv_two_off:
		case lima_pp_hir_op_loadv_three:
		case lima_pp_hir_op_loadv_three_off:
		case lima_pp_hir_op_loadv_four:
		case lima_pp_hir_op_loadv_four_off:
		{
			if (is_texture_cube)
				field->imm.source_type = 2;
			else
			{
				field->imm.source_type = 0;
				if (proj_z)
					field->imm.perspective = 2;
				else if (proj_w)
					field->imm.perspective = 3;
				else
					field->imm.perspective = 0;
			}
			
			switch (instr->op)
			{
				case lima_pp_hir_op_loadv_one:
				case lima_pp_hir_op_loadv_one_off:
					field->imm.alignment = 0;
					break;
				case lima_pp_hir_op_loadv_two:
				case lima_pp_hir_op_loadv_two_off:
					field->imm.alignment = 1;
					break;
				case lima_pp_hir_op_loadv_three:
				case lima_pp_hir_op_loadv_three_off:
				case lima_pp_hir_op_loadv_four:
				case lima_pp_hir_op_loadv_four_off:
					field->imm.alignment = 3;
					break;
				default:
					break;
			}
			switch (instr->op)
			{
				case lima_pp_hir_op_loadv_one:
				case lima_pp_hir_op_loadv_two:
				case lima_pp_hir_op_loadv_three:
				case lima_pp_hir_op_loadv_four:
					field->imm.offset_vector = 15;
					break;
					
				case lima_pp_hir_op_loadv_one_off:
				case lima_pp_hir_op_loadv_two_off:
				case lima_pp_hir_op_loadv_three_off:
				case lima_pp_hir_op_loadv_four_off:
					field->imm.offset_vector = get_source(instr->sources[0]);
					field->imm.offset_scalar = instr->sources[0].swizzle[0];
					break;
				
				default:
					break;
			}
			field->imm.index = instr->load_store_index;
			break;
		}
			
		case lima_pp_hir_op_frag_coord_impl:
			field->imm.source_type = 2;
			field->imm.perspective = 3;
			break;
			
		case lima_pp_hir_op_point_coord_impl:
			field->imm.source_type = 3;
			field->imm.perspective = 0;
			break;
			
		case lima_pp_hir_op_front_facing:
			field->imm.source_type = 3;
			field->imm.perspective = 1;
			break;
			
		default:
			assert(0);
			break;
	}
}

static void emit_sampler_instr(lima_pp_lir_instr_t* instr,
								  lima_pp_field_sampler_t* field)
{
	unsigned index, swizzle;
	switch (instr->op)
	{
		case lima_pp_hir_op_texld_2d:
		case lima_pp_hir_op_texld_2d_proj_z:
		case lima_pp_hir_op_texld_2d_proj_w:
		case lima_pp_hir_op_texld_cube:
			field->lod_bias_en = false;
			field->offset_en = false;
			break;
			
		case lima_pp_hir_op_texld_2d_off:
		case lima_pp_hir_op_texld_2d_proj_z_off:
		case lima_pp_hir_op_texld_2d_proj_w_off:
		case lima_pp_hir_op_texld_cube_off:
			index = get_source(instr->sources[1]);
			swizzle = instr->sources[1].swizzle[0];
			field->index_offset = 4 * index + swizzle;
			
			field->lod_bias_en = false;
			field->offset_en = true;
			break;
			
		case lima_pp_hir_op_texld_2d_lod:
		case lima_pp_hir_op_texld_2d_proj_z_lod:
		case lima_pp_hir_op_texld_2d_proj_w_lod:
		case lima_pp_hir_op_texld_cube_lod:
			index = get_source(instr->sources[1]);
			swizzle = instr->sources[1].swizzle[0];
			field->lod_bias = 4 * index + swizzle;
			
			field->lod_bias_en = true;
			field->offset_en = false;
			break;
			
		case lima_pp_hir_op_texld_2d_off_lod:
		case lima_pp_hir_op_texld_2d_proj_z_off_lod:
		case lima_pp_hir_op_texld_2d_proj_w_off_lod:
		case lima_pp_hir_op_texld_cube_off_lod:
			index = get_source(instr->sources[1]);
			swizzle = instr->sources[1].swizzle[0];
			field->index_offset = 4 * index + swizzle;
			
			index = get_source(instr->sources[2]);
			swizzle = instr->sources[2].swizzle[0];
			field->lod_bias = 4 * index + swizzle;
			
			field->lod_bias_en = true;
			field->offset_en = true;
			break;
			
		default:
			assert(0);
			break;
	}
	
	switch (instr->op)
	{
		case lima_pp_hir_op_texld_2d:
		case lima_pp_hir_op_texld_2d_lod:
		case lima_pp_hir_op_texld_2d_off:
		case lima_pp_hir_op_texld_2d_off_lod:
		case lima_pp_hir_op_texld_2d_proj_z:
		case lima_pp_hir_op_texld_2d_proj_z_lod:
		case lima_pp_hir_op_texld_2d_proj_z_off:
		case lima_pp_hir_op_texld_2d_proj_z_off_lod:
		case lima_pp_hir_op_texld_2d_proj_w:
		case lima_pp_hir_op_texld_2d_proj_w_lod:
		case lima_pp_hir_op_texld_2d_proj_w_off:
		case lima_pp_hir_op_texld_2d_proj_w_off_lod:
			field->type = lima_pp_sampler_type_2d;
			break;
			
		case lima_pp_hir_op_texld_cube:
		case lima_pp_hir_op_texld_cube_lod:
		case lima_pp_hir_op_texld_cube_off:
		case lima_pp_hir_op_texld_cube_off_lod:
			field->type = lima_pp_sampler_type_cube;
			break;
			
		default:
			assert(0);
			break;
	}
	
	field->index = instr->load_store_index;
	field->unknown_2 = 0x39001;
}

static void emit_uniform_instr(lima_pp_lir_instr_t* instr,
								  lima_pp_field_uniform_t* field)
{
	switch (instr->op)
	{
		case lima_pp_hir_op_loadu_one:
		case lima_pp_hir_op_loadu_one_off:
		case lima_pp_hir_op_loadu_four:
		case lima_pp_hir_op_loadu_four_off:
			field->source = lima_pp_uniform_src_uniform;
			break;
		case lima_pp_hir_op_loadt_one:
		case lima_pp_hir_op_loadt_one_off:
		case lima_pp_hir_op_loadt_four:
		case lima_pp_hir_op_loadt_four_off:
			field->source = lima_pp_uniform_src_temporary;
			break;
		default:
			assert(0);
			break;
	}
	
	switch (instr->op)
	{
		case lima_pp_hir_op_loadu_four:
		case lima_pp_hir_op_loadu_four_off:
		case lima_pp_hir_op_loadt_four:
		case lima_pp_hir_op_loadt_four_off:
			field->alignment = true;
			break;
		case lima_pp_hir_op_loadu_one:
		case lima_pp_hir_op_loadu_one_off:
		case lima_pp_hir_op_loadt_one:
		case lima_pp_hir_op_loadt_one_off:
			field->alignment = false;
			break;
		default:
			assert(0);
			break;
	}
	
	
	unsigned index, swizzle;
	switch (instr->op) {
		case lima_pp_hir_op_loadu_one:
		case lima_pp_hir_op_loadu_four:
		case lima_pp_hir_op_loadt_one:
		case lima_pp_hir_op_loadt_four:
			field->offset_en = false;
			break;
			
		case lima_pp_hir_op_loadu_one_off:
		case lima_pp_hir_op_loadu_four_off:
		case lima_pp_hir_op_loadt_one_off:
		case lima_pp_hir_op_loadt_four_off:
			index = get_source(instr->sources[0]);
			swizzle = instr->sources[0].swizzle[0];
			field->offset_reg = 4 * index + swizzle;
			field->offset_en = true;
			break;
			
		default:
			assert(0);
			break;
	}

	field->index = instr->load_store_index;
}

static unsigned shift_to_op(int shift)
{
	assert(shift >= -3 && shift <= 3);
	return shift < 0 ? shift + 8 : shift;
}

static void emit_vec4_mul_instr(lima_pp_lir_instr_t* instr,
								   lima_pp_field_vec4_mul_t* field)
{
	switch (instr->op)
	{
		case lima_pp_hir_op_mul:
			field->op = (lima_pp_vec4_mul_op_e) shift_to_op(instr->shift);
			break;
		case lima_pp_hir_op_not:
			field->op = lima_pp_vec4_mul_op_not;
			break;
		case lima_pp_hir_op_ne:
			field->op = lima_pp_vec4_mul_op_neq;
			break;
		case lima_pp_hir_op_gt:
			field->op = lima_pp_vec4_mul_op_lt;
			break;
		case lima_pp_hir_op_ge:
			field->op = lima_pp_vec4_mul_op_le;
			break;
		case lima_pp_hir_op_eq:
			field->op = lima_pp_vec4_mul_op_eq;
			break;
		case lima_pp_hir_op_min:
			field->op = lima_pp_vec4_mul_op_min;
			break;
		case lima_pp_hir_op_max:
			field->op = lima_pp_vec4_mul_op_max;
			break;
		case lima_pp_hir_op_mov:
			field->op = lima_pp_vec4_mul_op_mov;
			break;
		default:
			assert(0);
			break;
	}
	
	//TODO: swap inputs in asm?

	field->arg1_source = get_source(instr->sources[0]);
	field->arg1_swizzle = get_swizzle(instr->sources[0].swizzle);
	field->arg1_negate = instr->sources[0].negate;
	field->arg1_absolute = instr->sources[0].absolute;
	if (lima_pp_hir_op[instr->op].args == 2)
	{
		field->arg0_source = get_source(instr->sources[1]);
		field->arg0_swizzle = get_swizzle(instr->sources[1].swizzle);
		field->arg0_negate = instr->sources[1].negate;
		field->arg0_absolute = instr->sources[1].absolute;
	}
	
	if (instr->dest.pipeline)
		field->mask = 0;
	else
	{
		field->dest = instr->dest.reg->index;
		field->mask = get_mask(instr->dest.mask);
	}
	
	field->dest_modifier = instr->dest.modifier;
}

static void emit_float_mul_instr(lima_pp_lir_instr_t* instr,
								    lima_pp_field_float_mul_t* field)
{
	switch (instr->op)
	{
		case lima_pp_hir_op_mul:
			field->op = (lima_pp_float_mul_op_e) shift_to_op(instr->shift);
			break;
		case lima_pp_hir_op_not:
			field->op = lima_pp_float_mul_op_not;
			break;
		case lima_pp_hir_op_ne:
			field->op = lima_pp_float_mul_op_neq;
			break;
		case lima_pp_hir_op_gt:
			field->op = lima_pp_float_mul_op_lt;
			break;
		case lima_pp_hir_op_ge:
			field->op = lima_pp_float_mul_op_le;
			break;
		case lima_pp_hir_op_eq:
			field->op = lima_pp_float_mul_op_eq;
			break;
		case lima_pp_hir_op_min:
			field->op = lima_pp_float_mul_op_min;
			break;
		case lima_pp_hir_op_max:
			field->op = lima_pp_float_mul_op_max;
			break;
		case lima_pp_hir_op_mov:
			field->op = lima_pp_float_mul_op_mov;
			break;
		default:
			assert(0);
			break;
	}
	
	unsigned dest_component = get_dest_component(instr->dest.mask);
	unsigned src0_component = instr->sources[0].swizzle[dest_component];
	unsigned src1_component = instr->sources[1].swizzle[dest_component];
	field->arg1_source = get_source(instr->sources[0]) * 4 + src0_component;
	field->arg1_absolute = instr->sources[0].absolute;
	field->arg1_negate = instr->sources[0].negate;
	if (lima_pp_hir_op[instr->op].args == 2)
	{
		field->arg0_source = get_source(instr->sources[1]) * 4 + src1_component;
		field->arg0_absolute = instr->sources[1].absolute;
		field->arg0_negate = instr->sources[1].negate;
	}
	
	if (instr->dest.pipeline)
		field->output_en = false;
	else
	{
		field->output_en = true;
		field->dest = instr->dest.reg->index * 4 + dest_component;
	}
	
	field->dest_modifier = instr->dest.modifier;
}

static void emit_vec4_acc_instr(lima_pp_lir_instr_t* instr,
								   lima_pp_field_vec4_acc_t* field)
{
	//TODO: swap inputs in asm?
	
	if (instr->sources[0].pipeline &&
		instr->sources[0].pipeline_reg == lima_pp_lir_pipeline_reg_vmul)
		field->mul_in = true;
	else
		field->arg1_source = get_source(instr->sources[0]);
	
	field->arg1_swizzle = get_swizzle(instr->sources[0].swizzle);
	field->arg1_negate = instr->sources[0].negate;
	field->arg1_absolute = instr->sources[0].absolute;
	if (lima_pp_hir_op[instr->op].args > 1)
	{
		field->arg0_source = get_source(instr->sources[1]);
		field->arg0_swizzle = get_swizzle(instr->sources[1].swizzle);
		field->arg0_negate = instr->sources[1].negate;
		field->arg0_absolute = instr->sources[1].absolute;
	}
	
	if (instr->op == lima_pp_hir_op_ddx || instr->op == lima_pp_hir_op_ddy)
	{
		field->arg0_source = get_source(instr->sources[0]);
		field->arg0_swizzle = get_swizzle(instr->sources[0].swizzle);
		field->arg0_negate = !instr->sources[0].negate;
		field->arg0_absolute = instr->sources[0].absolute;
	}
	
	field->dest = instr->dest.reg->index;
	field->mask = get_mask(instr->dest.mask);
	
	field->dest_modifier = instr->dest.modifier;
	
	switch (instr->op)
	{
		case lima_pp_hir_op_add:
			field->op = (lima_pp_vec4_acc_op_e) shift_to_op(instr->shift);
			break;
		case lima_pp_hir_op_fract:
			field->op = lima_pp_vec4_acc_op_fract;
			break;
		case lima_pp_hir_op_ne:
			field->op = lima_pp_vec4_acc_op_neq;
			break;
		case lima_pp_hir_op_gt:
			field->op = lima_pp_vec4_acc_op_lt;
			break;
		case lima_pp_hir_op_ge:
			field->op = lima_pp_vec4_acc_op_le;
			break;
		case lima_pp_hir_op_eq:
			field->op = lima_pp_vec4_acc_op_eq;
			break;
		case lima_pp_hir_op_floor:
			field->op = lima_pp_vec4_acc_op_floor;
			break;
		case lima_pp_hir_op_ceil:
			field->op = lima_pp_vec4_acc_op_ceil;
			break;
		case lima_pp_hir_op_min:
			field->op = lima_pp_vec4_acc_op_min;
			break;
		case lima_pp_hir_op_max:
			field->op = lima_pp_vec4_acc_op_max;
			break;
		case lima_pp_hir_op_sum3:
			field->op = lima_pp_vec4_acc_op_sum3;
			break;
		case lima_pp_hir_op_sum4:
			field->op = lima_pp_vec4_acc_op_sum;
			break;
		case lima_pp_hir_op_ddx:
			field->op = lima_pp_vec4_acc_op_dFdx;
			break;
		case lima_pp_hir_op_ddy:
			field->op = lima_pp_vec4_acc_op_dFdy;
			break;
		case lima_pp_hir_op_select:
			field->op = lima_pp_vec4_acc_op_sel;
			break;
		case lima_pp_hir_op_mov:
			field->op = lima_pp_vec4_acc_op_mov;
			break;
		default:
			assert(0);
			break;
	}
}

static void emit_float_acc_instr(lima_pp_lir_instr_t* instr,
								    lima_pp_field_float_acc_t* field)
{
	unsigned dest_component = get_dest_component(instr->dest.mask);
	unsigned src0_component = instr->sources[0].swizzle[dest_component];
	unsigned src1_component = instr->sources[1].swizzle[dest_component];
	
	if (instr->sources[0].pipeline &&
		instr->sources[0].pipeline_reg == lima_pp_lir_pipeline_reg_fmul)
		field->mul_in = true;
	else
		field->arg1_source = get_source(instr->sources[0]) * 4 + src0_component;
	
	field->arg1_absolute = instr->sources[0].absolute;
	field->arg1_negate = instr->sources[0].negate;
	if (lima_pp_hir_op[instr->op].args == 2)
	{
		field->arg0_source = get_source(instr->sources[1]) * 4 + src1_component;
		field->arg0_absolute = instr->sources[1].absolute;
		field->arg0_negate = instr->sources[1].negate;
	}
	
	if (instr->op == lima_pp_hir_op_ddx || instr->op == lima_pp_hir_op_ddy)
	{
		field->arg0_source = get_source(instr->sources[0]) * 4 + src0_component;
		field->arg0_absolute = instr->sources[0].absolute;
		field->arg0_negate = !instr->sources[0].negate;
	}

	field->output_en = true;
	field->dest = instr->dest.reg->index * 4 + dest_component;
	
	field->dest_modifier = instr->dest.modifier;
	
	switch (instr->op)
	{
		case lima_pp_hir_op_add:
			field->op = (lima_pp_float_acc_op_e) shift_to_op(instr->shift);
			break;
		case lima_pp_hir_op_fract:
			field->op = lima_pp_float_acc_op_fract;
			break;
		case lima_pp_hir_op_ne:
			field->op = lima_pp_float_acc_op_neq;
			break;
		case lima_pp_hir_op_gt:
			field->op = lima_pp_float_acc_op_lt;
			break;
		case lima_pp_hir_op_ge:
			field->op = lima_pp_float_acc_op_le;
			break;
		case lima_pp_hir_op_eq:
			field->op = lima_pp_float_acc_op_eq;
			break;
		case lima_pp_hir_op_floor:
			field->op = lima_pp_float_acc_op_floor;
			break;
		case lima_pp_hir_op_ceil:
			field->op = lima_pp_float_acc_op_ceil;
			break;
		case lima_pp_hir_op_min:
			field->op = lima_pp_float_acc_op_min;
			break;
		case lima_pp_hir_op_max:
			field->op = lima_pp_float_acc_op_max;
			break;
		case lima_pp_hir_op_ddx:
			field->op = lima_pp_float_acc_op_dFdx;
			break;
		case lima_pp_hir_op_ddy:
			field->op = lima_pp_float_acc_op_dFdy;
			break;
		case lima_pp_hir_op_mov:
			field->op = lima_pp_float_acc_op_mov;
			break;
		default:
			assert(0);
			break;
	}
}

static void emit_combine_instr(lima_pp_lir_instr_t* instr,
								  lima_pp_field_combine_t* field)
{
	switch (instr->op)
	{
		case lima_pp_hir_op_rcp:
		case lima_pp_hir_op_mov:
		case lima_pp_hir_op_sqrt:
		case lima_pp_hir_op_rsqrt:
		case lima_pp_hir_op_exp2:
		case lima_pp_hir_op_log2:
		case lima_pp_hir_op_sin_lut:
		case lima_pp_hir_op_cos_lut:
		{
			field->scalar.dest_vec = false;
			field->scalar.arg1_en = false;
			unsigned dest_component = get_dest_component(instr->dest.mask);
			unsigned src_component = instr->sources[0].swizzle[dest_component];
			switch (instr->op)
			{
				case lima_pp_hir_op_rcp:
					field->scalar.op = lima_pp_combine_scalar_op_rcp;
					break;
				case lima_pp_hir_op_mov:
					field->scalar.op = lima_pp_combine_scalar_op_mov;
					break;
				case lima_pp_hir_op_sqrt:
					field->scalar.op = lima_pp_combine_scalar_op_sqrt;
					break;
				case lima_pp_hir_op_rsqrt:
					field->scalar.op = lima_pp_combine_scalar_op_rsqrt;
					break;
				case lima_pp_hir_op_exp2:
					field->scalar.op = lima_pp_combine_scalar_op_exp2;
					break;
				case lima_pp_hir_op_log2:
					field->scalar.op = lima_pp_combine_scalar_op_log2;
					break;
				case lima_pp_hir_op_sin_lut:
					field->scalar.op = lima_pp_combine_scalar_op_sin;
					break;
				case lima_pp_hir_op_cos_lut:
					field->scalar.op = lima_pp_combine_scalar_op_cos;
					break;
				default:
					assert(0);
					break;
			}
			field->scalar.arg0_absolute = instr->sources[0].absolute;
			field->scalar.arg0_negate = instr->sources[0].negate;
			field->scalar.arg0_src = get_source(instr->sources[0]) * 4 + src_component;
			field->scalar.dest = instr->dest.reg->index * 4 + dest_component;
			field->scalar.dest_modifier = instr->dest.modifier;
			break;
		}
			
		case lima_pp_hir_op_mul:
			assert(!instr->sources[1].absolute);
			assert(!instr->sources[1].negate);
			assert(instr->dest.modifier == lima_pp_outmod_none);
			field->vector.dest_vec = true;
			field->vector.arg1_en = true;
			field->vector.arg1_swizzle = get_swizzle(instr->sources[1].swizzle);
			field->vector.arg1_source = get_source(instr->sources[1]);
			field->scalar.arg0_absolute = instr->sources[0].absolute;
			field->scalar.arg0_negate = instr->sources[0].negate;
			field->scalar.arg0_src = get_source(instr->sources[0]) * 4 +
				instr->sources[0].swizzle[0];
			field->vector.mask = get_mask(instr->dest.mask);
			field->vector.dest = instr->dest.reg->index;
			break;
		
		case lima_pp_hir_op_atan2_pt1:
			field->scalar.arg1_src = get_source(instr->sources[1]) * 4 +
				instr->sources[1].swizzle[0];
			field->scalar.arg1_absolute = instr->sources[1].absolute;
			field->scalar.arg1_negate = instr->sources[1].negate;
			/* fallthrough */
			
		case lima_pp_hir_op_atan_pt1:
			field->vector.dest_vec = true;
			field->vector.arg1_en = false;
			field->scalar.arg0_src = get_source(instr->sources[0]) * 4 +
				instr->sources[1].swizzle[0];
			field->scalar.arg0_absolute = instr->sources[0].absolute;
			field->scalar.arg0_negate = instr->sources[0].negate;
			
			if (instr->op == lima_pp_hir_op_atan_pt1)
				field->scalar.op = lima_pp_combine_scalar_op_atan;
			else
				field->scalar.op = lima_pp_combine_scalar_op_atan2;
			
			field->vector.mask = get_mask(instr->dest.mask);
			field->vector.dest = instr->dest.reg->index;
			break;
			
		case lima_pp_hir_op_atan_pt2:
			field->vector.dest_vec = false;
			field->vector.arg1_en = true;
			field->scalar.dest = instr->dest.reg->index * 4
				+ get_dest_component(instr->dest.mask);
			field->vector.arg1_source = get_source(instr->sources[0]);
			field->vector.arg1_swizzle = get_swizzle(instr->sources[0].swizzle);
			break;
			
		default:
			assert(0);
			break;
	}
}

static void emit_temp_write_instr(lima_pp_lir_instr_t* instr,
									 lima_pp_field_temp_write_t* field)
{
	if (instr->op == lima_pp_hir_op_fb_color ||
		instr->op == lima_pp_hir_op_fb_depth)
	{
		if (instr->op == lima_pp_hir_op_fb_color)
			field->fb_read.source = true;
		else
			field->fb_read.source = false;
		field->fb_read.unknown_0 = 0x7;
		field->fb_read.dest = instr->dest.reg->index;
		field->fb_read.unknown_1 = 0x2;
		return;
	}
	
	field->temp_write.dest = 3;
	
	unsigned source = get_source(instr->sources[0]);
	switch (instr->op)
	{
		case lima_pp_hir_op_storet_four:
		case lima_pp_hir_op_storet_four_off:
			field->temp_write.alignment = true;
			field->temp_write.source = source * 4;
			break;
		case lima_pp_hir_op_storet_one:
		case lima_pp_hir_op_storet_one_off:
			field->temp_write.alignment = false;
			field->temp_write.source = source * 4 + instr->sources[0].swizzle[0];
			break;
		default:
			assert(0);
			break;
	}
	
	unsigned index, swizzle;
	switch (instr->op)
	{
		case lima_pp_hir_op_storet_one:
		case lima_pp_hir_op_storet_four:
			field->temp_write.offset_en = false;
			break;
			
		case lima_pp_hir_op_storet_one_off:
		case lima_pp_hir_op_storet_four_off:
			index = get_source(instr->sources[1]);
			swizzle = instr->sources[1].swizzle[0];
			field->temp_write.offset_reg = 4 * index + swizzle;
			field->temp_write.offset_en = true;
			
		default:
			assert(0);
			break;
	}
	
	field->temp_write.index = instr->load_store_index;
}

static void emit_branch_instr(lima_pp_lir_instr_t* instr,
							  lima_pp_field_branch_t* field)
{
	if (instr->op != lima_pp_hir_op_branch)
	{
		unsigned arg0_chan = instr->sources[0].swizzle[0];
		unsigned arg1_chan = instr->sources[1].swizzle[0];
		unsigned arg0_index = get_source(instr->sources[0]);
		unsigned arg1_index = get_source(instr->sources[1]);
		field->branch.arg0_source = arg0_index * 4 + arg0_chan;
		field->branch.arg1_source = arg1_index * 4 + arg1_chan;
	}
	
	switch (instr->op)
	{
		case lima_pp_hir_op_branch:
			field->branch.cond_gt = true;
			field->branch.cond_eq = true;
			field->branch.cond_lt = true;
			break;
		case lima_pp_hir_op_branch_gt:
			field->branch.cond_gt = true;
			field->branch.cond_eq = false;
			field->branch.cond_lt = false;
			break;
		case lima_pp_hir_op_branch_eq:
			field->branch.cond_gt = false;
			field->branch.cond_eq = true;
			field->branch.cond_lt = false;
			break;
		case lima_pp_hir_op_branch_ge:
			field->branch.cond_gt = true;
			field->branch.cond_eq = true;
			field->branch.cond_lt = false;
			break;
		case lima_pp_hir_op_branch_lt:
			field->branch.cond_gt = false;
			field->branch.cond_eq = false;
			field->branch.cond_lt = true;
			break;
		case lima_pp_hir_op_branch_ne:
			field->branch.cond_gt = true;
			field->branch.cond_eq = false;
			field->branch.cond_lt = true;
			break;
		case lima_pp_hir_op_branch_le:
			field->branch.cond_gt = false;
			field->branch.cond_eq = true;
			field->branch.cond_lt = true;
			break;
		default:
			assert(0);
			break;
	}
}

static void emit_const(double* orig, lima_pp_vec4_t* asm_const)
{
	asm_const->x = ogt_hfloat_from_float(orig[0]);
	asm_const->y = ogt_hfloat_from_float(orig[1]);
	asm_const->z = ogt_hfloat_from_float(orig[2]);
	asm_const->w = ogt_hfloat_from_float(orig[3]);
}

static bool is_texture_cube(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_texld_cube
	|| op == lima_pp_hir_op_texld_cube_off
	|| op == lima_pp_hir_op_texld_cube_lod
	|| op == lima_pp_hir_op_texld_cube_off_lod;
}

static bool is_proj_z(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_texld_2d_proj_z
	|| op == lima_pp_hir_op_texld_2d_proj_z_off
	|| op == lima_pp_hir_op_texld_2d_proj_z_lod
	|| op == lima_pp_hir_op_texld_2d_proj_z_off_lod;
}

static bool is_proj_w(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_texld_2d_proj_w
	|| op == lima_pp_hir_op_texld_2d_proj_w_off
	|| op == lima_pp_hir_op_texld_2d_proj_w_lod
	|| op == lima_pp_hir_op_texld_2d_proj_w_off_lod;
}

static void emit_sched_instr(lima_pp_lir_scheduled_instr_t* instr,
								lima_pp_instruction_t *asm_instr)
{
	if (instr->const0_size)
	{
		asm_instr->control.fields |= (1 << lima_pp_field_vec4_const_0);
		unsigned i;
		
		for (i = instr->const0_size; i < 4; i++)
			instr->const0[i] = 0;
		emit_const(instr->const0, &asm_instr->const0);
	}
	if (instr->const1_size)
	{
		asm_instr->control.fields |= (1 << lima_pp_field_vec4_const_1);
		unsigned i;
		
		for (i = instr->const1_size; i < 4; i++)
			instr->const1[i] = 0;
		emit_const(instr->const1, &asm_instr->const1);
	}
	
	if (instr->varying_instr)
	{
		bool tex_cube = instr->texld_instr &&
			is_texture_cube(instr->texld_instr->op);
		bool proj_z = instr->texld_instr &&
			is_proj_z(instr->texld_instr->op);
		bool proj_w = instr->texld_instr &&
			is_proj_w(instr->texld_instr->op);
		
		asm_instr->control.fields |= (1 << lima_pp_field_varying);
		emit_varying_instr(instr->varying_instr, &asm_instr->varying,
						   tex_cube, proj_z, proj_w);
	}

	if (instr->texld_instr)
	{
		asm_instr->control.fields |= (1 << lima_pp_field_sampler);
		asm_instr->control.sync = true;
		emit_sampler_instr(instr->texld_instr, &asm_instr->sampler);
	}
	
	if (instr->uniform_instr)
	{
		asm_instr->control.fields |= (1 << lima_pp_field_uniform);
		emit_uniform_instr(instr->uniform_instr, &asm_instr->uniform);
	}
	
	if (instr->alu_instrs[lima_pp_lir_alu_vector_add])
	{
		lima_pp_lir_instr_t* add_instr =
			instr->alu_instrs[lima_pp_lir_alu_vector_add];
		asm_instr->control.fields |= (1 << lima_pp_field_vec4_acc);
		emit_vec4_acc_instr(add_instr, &asm_instr->vec4_acc);
		
		if (add_instr->op == lima_pp_hir_op_ddx ||
			add_instr->op == lima_pp_hir_op_ddy)
		{
			asm_instr->control.sync = true;
		}
	}
				
	if (instr->alu_instrs[lima_pp_lir_alu_vector_mul])
	{
		asm_instr->control.fields |= (1 << lima_pp_field_vec4_mul);
		emit_vec4_mul_instr(instr->alu_instrs[lima_pp_lir_alu_vector_mul],
							&asm_instr->vec4_mul);
	}
				
	if (instr->alu_instrs[lima_pp_lir_alu_scalar_add])
	{
		lima_pp_lir_instr_t* add_instr =
			instr->alu_instrs[lima_pp_lir_alu_scalar_add];
		asm_instr->control.fields |= (1 << lima_pp_field_float_acc);
		emit_float_acc_instr(add_instr, &asm_instr->float_acc);
		
		if (add_instr->op == lima_pp_hir_op_ddx ||
			add_instr->op == lima_pp_hir_op_ddy)
		{
			asm_instr->control.sync = true;
		}
	}
				
	if (instr->alu_instrs[lima_pp_lir_alu_scalar_mul])
	{
		asm_instr->control.fields |= (1 << lima_pp_field_float_mul);
		emit_float_mul_instr(instr->alu_instrs[lima_pp_lir_alu_scalar_mul],
							 &asm_instr->float_mul);
	}
				
	if (instr->alu_instrs[lima_pp_lir_alu_combine])
	{
		asm_instr->control.fields |= (1 << lima_pp_field_combine);
		emit_combine_instr(instr->alu_instrs[lima_pp_lir_alu_combine],
						   &asm_instr->combine);
	}
	
	if (instr->temp_store_instr)
	{
		asm_instr->control.fields |= (1 << lima_pp_field_temp_write);
		emit_temp_write_instr(instr->temp_store_instr, &asm_instr->temp_write);
	}
	
	if (instr->branch_instr)
	{
		asm_instr->control.fields |= (1 << lima_pp_field_branch);
		emit_branch_instr(instr->branch_instr, &asm_instr->branch);
	}
}

static pp_asm_block_t* emit_block(lima_pp_lir_block_t* block, unsigned block_num)
{
	pp_asm_block_t* ret = malloc(sizeof(pp_asm_block_t));
	
	ret->size = block->num_instrs;
	ret->instrs = calloc(ret->size, sizeof(lima_pp_instruction_t));

	if (block->is_end)
	{
		assert(ret->size >= 1);
		if (block->discard)
		{
			ret->instrs[ret->size - 1].control.fields |= (1 << lima_pp_field_branch);
			ret->instrs[ret->size - 1].branch.discard.word0 = LIMA_PP_DISCARD_WORD0;
			ret->instrs[ret->size - 1].branch.discard.word1 = LIMA_PP_DISCARD_WORD1;
			ret->instrs[ret->size - 1].branch.discard.word2 = LIMA_PP_DISCARD_WORD2;
		}
		ret->instrs[ret->size - 1].control.stop = true;
	}
	
	if (block->is_end && block->discard)
		ret->discard = true;
	else
		ret->discard = false;
	
	unsigned i = 0;
	
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		emit_sched_instr(instr, &ret->instrs[i]);
		i++;
	}
	
	if (block->num_instrs >= 1)
	{
		lima_pp_lir_scheduled_instr_t* last_instr = pp_lir_block_last_instr(block);
		if (last_instr->branch_instr)
		{
			ret->dest1 = last_instr->branch_instr->branch_dest;
		}
		
		if (block->num_instrs >= 2)
		{
			last_instr = pp_lir_block_prev_instr(last_instr);
			if (last_instr->branch_instr)
			{
				ret->dest2 = last_instr->branch_instr->branch_dest;
			}
		}
	}
	
	return ret;
}


//Returns the total size of the program
static unsigned schedule_instrs(pp_asm_block_t** blocks, unsigned num_blocks)
{
	unsigned i, j, offset = 0;
	lima_pp_ctrl_t* last_ctrl = NULL;
	
	for (i = 0; i < num_blocks; i++)
	{
		pp_asm_block_t* block = blocks[i];
		
		block->start = offset;
		for (j = 0; j < block->size; j++)
		{
			lima_pp_instruction_t* instr = &block->instrs[j];
			if (j == block->size - 1 && (i == num_blocks - 1 || block->discard))
				instr->control.prefetch = false;
			else
				instr->control.prefetch = true;
			lima_pp_instruction_calc_size(&instr->control);
			if (last_ctrl)
				last_ctrl->next_count = instr->control.count;
			last_ctrl = &instr->control;
			offset += instr->control.count;
		}
	}
	
	return offset;
}

static void resolve_branch_dests(pp_asm_block_t** blocks, unsigned num_blocks)
{
	unsigned i, j;
	
	for (i = 0; i < num_blocks; i++)
	{
		pp_asm_block_t* block = blocks[i];
		
		if (block->discard)
			continue;
		
		if (block->size >= 2 &&
			block->instrs[block->size - 2].control.fields & (1 << lima_pp_field_branch))
		{
			unsigned offset = block->start;
			for (j = 0; j < block->size - 2; j++)
				offset += block->instrs[j].control.count;
			pp_asm_block_t* dest_block = blocks[block->dest2];
			block->instrs[block->size - 2].branch.branch.target =
				(signed) dest_block->start - offset;
		}
		if (block->size >= 1 &&
			block->instrs[block->size - 1].control.fields & (1 << lima_pp_field_branch))
		{
			unsigned offset = block->start;
			for (j = 0; j < block->size - 1; j++)
				offset += block->instrs[j].control.count;
			pp_asm_block_t* dest_block = blocks[block->dest1];
			block->instrs[block->size - 1].branch.branch.target =
				(signed) dest_block->start - offset;
		}
	}
}

static void dump_asm(pp_asm_block_t** blocks, unsigned num_blocks)
{
	unsigned i, j;
	for (i = 0; i < num_blocks; i++)
		for (j = 0; j < blocks[i]->size; j++)
			lima_pp_instruction_print(&blocks[i]->instrs[j], true, 0);
}

void* lima_pp_lir_codegen(lima_pp_lir_prog_t* prog, unsigned* code_size)
{
	offset_temporaries(prog);
	
	pp_asm_block_t** blocks = malloc(sizeof(pp_asm_block_t*) * prog->num_blocks);
	
	if (!blocks)
		return NULL;
	
	unsigned i, j;
	
	for (i = 0; i < prog->num_blocks; i++)
		blocks[i] = emit_block(prog->blocks[i], i);
	
	unsigned size = schedule_instrs(blocks, prog->num_blocks);
	resolve_branch_dests(blocks, prog->num_blocks);
	
	/*dump_asm(blocks, prog->num_blocks);*/
	
	uint32_t* code = malloc(size * sizeof(uint32_t));
	if (!code)
	{
		for (i = 0; i < prog->num_blocks; i++)
		{
			free(blocks[i]->instrs);
			free(blocks[i]);
		}
		free(blocks);
		return NULL;
	}
	
	unsigned offset = 0;
	for (i = 0; i < prog->num_blocks; i++)
	{
		pp_asm_block_t* block = blocks[i];
		for (j = 0; j < block->size; j++)
		{
			lima_pp_instruction_t* instr = block->instrs + j;
			lima_pp_instruction_encode(instr, code + offset);
			offset += instr->control.count;
		}
	}
	
	for (i = 0; i < prog->num_blocks; i++)
	{
		free(blocks[i]->instrs);
		free(blocks[i]);
	}
	free(blocks);
	
	*code_size = size * sizeof(uint32_t);
	
	return code;
}
