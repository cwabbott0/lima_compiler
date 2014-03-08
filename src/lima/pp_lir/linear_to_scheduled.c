#include "pp_lir.h"


// Note: due to constant folding, we can assume each instruction never takes
// more than two constant arguments.
static void _convert_constants(lima_pp_lir_instr_t* instr,
							   lima_pp_lir_scheduled_instr_t* scheduled_instr)
{
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].constant)
		{
			unsigned size = lima_pp_lir_arg_size(instr, i);

			if (!scheduled_instr->const0_size)
			{
				scheduled_instr->const0_size = size;
				memcpy(scheduled_instr->const0, instr->sources[i].reg,
					   size * sizeof(double));
				instr->sources[i].pipeline_reg = lima_pp_lir_pipeline_reg_const0;
			}
			else
			{
				scheduled_instr->const1_size = size;
				memcpy(scheduled_instr->const1, instr->sources[i].reg,
					   size * sizeof(double));
				instr->sources[i].pipeline_reg = lima_pp_lir_pipeline_reg_const1;
			}

			instr->sources[i].pipeline = true;
			instr->sources[i].constant = false;
			free(instr->sources[i].reg);
			instr->sources[i].reg = NULL;
		}
	}
}

// Convert from inline constants to ^const0 and ^const1
static void convert_constants(lima_pp_lir_scheduled_instr_t* instr)
{
	if (instr->varying_instr)
		_convert_constants(instr->varying_instr, instr);
	
	if (instr->texld_instr)
		_convert_constants(instr->texld_instr, instr);
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			_convert_constants(instr->alu_instrs[i], instr);
	
	if (instr->branch_instr)
		_convert_constants(instr->branch_instr, instr);
}

static bool is_scalar(lima_pp_lir_instr_t* instr)
{
	unsigned num_components = 0, i;
	for (i = 0; i < 4; i++)
		if (instr->dest.mask[i])
			num_components++;
	
	return num_components == 1;
}

static bool is_combine_normal(lima_pp_lir_instr_t* instr)
{
	if (instr->dest.modifier != lima_pp_outmod_none)
		return false;
	
	if (instr->sources[1].absolute || instr->sources[1].negate)
		return false;
	
	unsigned component = instr->sources[0].swizzle[0];
	unsigned i;
	for (i = 1; i < instr->dest.reg->size; i++)
		if (instr->sources[0].swizzle[i] != component)
			return false;
	
	return true;
}

static bool is_combine_swapped(lima_pp_lir_instr_t* instr)
{
	if (instr->dest.modifier != lima_pp_outmod_none)
		return false;
	
	if (instr->sources[0].absolute || instr->sources[0].negate)
		return false;
	
	unsigned component = instr->sources[1].swizzle[0];
	unsigned i;
	for (i = 1; i < instr->dest.reg->size; i++)
		if (instr->sources[1].swizzle[i] != component)
			return false;
	
	return true;
}

static bool is_combine(lima_pp_lir_instr_t* instr)
{
	if (is_combine_normal(instr))
		return true;
	
	if (is_combine_swapped(instr))
	{
		lima_pp_lir_source_t temp = instr->sources[0];
		instr->sources[0] = instr->sources[1];
		instr->sources[1] = temp;
		return true;
	}
	
	return false;
}


/* Convert from the linear to scheduled type in a one-to-one manner. */

lima_pp_lir_scheduled_instr_t* lima_pp_lir_instr_to_sched_instr(
	lima_pp_lir_instr_t* instr)
{
	lima_pp_lir_scheduled_instr_t* ret = lima_pp_lir_scheduled_instr_create();
	if (!ret)
		return NULL;
	
	instr->sched_instr = ret;
	
	unsigned i;
	
	switch (instr->op)
	{
		/* ALU slots */
		case lima_pp_hir_op_mov:
		{
			bool scalar = is_scalar(instr);
			unsigned pos;
			if (scalar)
			{
				pos = lima_pp_lir_alu_scalar_add;
				ret->alu_instrs[lima_pp_lir_alu_scalar_add] = instr;
			}
			else
			{
				pos = lima_pp_lir_alu_vector_add;
				ret->alu_instrs[lima_pp_lir_alu_vector_add] = instr;
			}

			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_mul] = true;
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_add] = true;
			if (scalar)
			{
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_mul] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_add] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_combine] = true;
			}
			break;
		}
			
		case lima_pp_hir_op_add:
		case lima_pp_hir_op_ddx:
		case lima_pp_hir_op_ddy:
		case lima_pp_hir_op_fract:
		case lima_pp_hir_op_floor:
		case lima_pp_hir_op_ceil:
		{
			bool scalar = is_scalar(instr);
			unsigned pos;
			if (scalar)
			{
				pos = lima_pp_lir_alu_scalar_add;
				ret->alu_instrs[lima_pp_lir_alu_scalar_add] = instr;
			}
			else
			{
				pos = lima_pp_lir_alu_vector_add;
				ret->alu_instrs[lima_pp_lir_alu_vector_add] = instr;
			}
			
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_add] = true;
			if (scalar)
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_add] = true;
			break;
		}
			
		case lima_pp_hir_op_sum3:
		case lima_pp_hir_op_sum4:
		{
			ret->alu_instrs[lima_pp_lir_alu_vector_add] = instr;
			ret->possible_alu_instr_pos[lima_pp_lir_alu_vector_add][lima_pp_lir_alu_vector_add]
				= true;
			break;
		}
			
		case lima_pp_hir_op_mul:
		{
			if (is_scalar(instr))
			{
				ret->alu_instrs[lima_pp_lir_alu_scalar_mul] = instr;
				ret->possible_alu_instr_pos[lima_pp_lir_alu_scalar_mul][lima_pp_lir_alu_vector_mul]
					= true;
				ret->possible_alu_instr_pos[lima_pp_lir_alu_scalar_mul][lima_pp_lir_alu_scalar_mul]
					= true;
				if (is_combine(instr))
					ret->possible_alu_instr_pos[lima_pp_lir_alu_scalar_mul][lima_pp_lir_alu_combine]
						= true;
			}
			else if (is_combine(instr))
			{
				ret->alu_instrs[lima_pp_lir_alu_combine] = instr;
				ret->possible_alu_instr_pos[lima_pp_lir_alu_combine][lima_pp_lir_alu_combine]
					= true;
				ret->possible_alu_instr_pos[lima_pp_lir_alu_combine][lima_pp_lir_alu_vector_mul]
					= true;
			}
			else
			{
				ret->alu_instrs[lima_pp_lir_alu_vector_mul] = instr;
				ret->possible_alu_instr_pos[lima_pp_lir_alu_vector_mul][lima_pp_lir_alu_vector_mul]
					= true;
			}
			break;
		}
			
		case lima_pp_hir_op_gt:
		case lima_pp_hir_op_ge:
		case lima_pp_hir_op_eq:
		case lima_pp_hir_op_ne:
		case lima_pp_hir_op_min:
		case lima_pp_hir_op_max:
		{
			bool scalar = is_scalar(instr);
			unsigned pos;
			if (scalar)
			{
				pos = lima_pp_lir_alu_scalar_add;
				ret->alu_instrs[lima_pp_lir_alu_scalar_add] = instr;
			}
			else
			{
				pos = lima_pp_lir_alu_vector_add;
				ret->alu_instrs[lima_pp_lir_alu_vector_add] = instr;
			}
			
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_mul] = true;
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_add] = true;
			if (scalar)
			{
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_mul] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_add] = true;
			}
			break;
		}
			
		case lima_pp_hir_op_select:
		{
			lima_pp_lir_instr_t* new_instr = lima_pp_lir_instr_create();
			if (!new_instr)
				return NULL;
			
			new_instr->sched_instr = ret;
			
			new_instr->op = lima_pp_hir_op_mov;
			
			new_instr->sources[0] = instr->sources[2];
			new_instr->dest.mask[0] = true;
			new_instr->dest.mask[1] = false;
			new_instr->dest.mask[2] = false;
			new_instr->dest.mask[3] = false;
			new_instr->dest.pipeline = true;
			new_instr->dest.pipeline_reg = lima_pp_lir_pipeline_reg_discard;
			
			instr->sources[2].reg = NULL;
			instr->sources[2].constant = false;
			instr->sources[2].pipeline = true;
			instr->sources[2].pipeline_reg = lima_pp_lir_pipeline_reg_fmul;
			
			if (!new_instr->sources[0].constant)
			{
				lima_pp_lir_reg_t* reg = new_instr->sources[0].reg;
				ptrset_remove(&reg->uses, instr);
				ptrset_add(&reg->uses, new_instr);
			}
			
			ret->alu_instrs[lima_pp_lir_alu_vector_add] = instr;
			ret->alu_instrs[lima_pp_lir_alu_scalar_mul] = new_instr;
			ret->possible_alu_instr_pos[lima_pp_lir_alu_vector_add][lima_pp_lir_alu_vector_add]
				= true;
			ret->possible_alu_instr_pos[lima_pp_lir_alu_scalar_mul][lima_pp_lir_alu_scalar_mul]
				= true;
			
			break;
		}
			
		case lima_pp_hir_op_rcp:
		case lima_pp_hir_op_sin_lut:
		case lima_pp_hir_op_cos_lut:
		case lima_pp_hir_op_exp2:
		case lima_pp_hir_op_log2:
		case lima_pp_hir_op_sqrt:
		case lima_pp_hir_op_rsqrt:
		case lima_pp_hir_op_atan_pt1:
		case lima_pp_hir_op_atan2_pt1:
		case lima_pp_hir_op_atan_pt2:
		{
			ret->alu_instrs[lima_pp_lir_alu_combine] = instr;
			ret->possible_alu_instr_pos[lima_pp_lir_alu_combine][lima_pp_lir_alu_combine] = true;
			break;
		}


		/* varying load slot */
		case lima_pp_hir_op_loadv_one:
		case lima_pp_hir_op_loadv_one_off:
		case lima_pp_hir_op_loadv_two:
		case lima_pp_hir_op_loadv_two_off:
		case lima_pp_hir_op_loadv_three:
		case lima_pp_hir_op_loadv_three_off:
		case lima_pp_hir_op_loadv_four:
		case lima_pp_hir_op_loadv_four_off:
		case lima_pp_hir_op_frag_coord_impl:
		case lima_pp_hir_op_point_coord_impl:
		case lima_pp_hir_op_front_facing:
		case lima_pp_hir_op_normalize3:
			ret->varying_instr = instr;
			break;
		
		/* uniform load slot */
		case lima_pp_hir_op_loadu_one:
		case lima_pp_hir_op_loadu_one_off:
		case lima_pp_hir_op_loadu_two:
		case lima_pp_hir_op_loadu_two_off:
		case lima_pp_hir_op_loadu_four:
		case lima_pp_hir_op_loadu_four_off:
		case lima_pp_hir_op_loadt_one:
		case lima_pp_hir_op_loadt_one_off:
		case lima_pp_hir_op_loadt_two:
		case lima_pp_hir_op_loadt_two_off:
		case lima_pp_hir_op_loadt_four:
		case lima_pp_hir_op_loadt_four_off:
		{
			lima_pp_lir_instr_t* new_instr = lima_pp_lir_instr_create();
			if (!new_instr)
				return NULL;
			
			new_instr->sched_instr = ret;

			new_instr->op = lima_pp_hir_op_mov;

			new_instr->sources[0].constant = false;
			new_instr->sources[0].pipeline = true;
			new_instr->sources[0].absolute = false;
			new_instr->sources[0].negate = false;
			new_instr->sources[0].pipeline_reg = lima_pp_lir_pipeline_reg_uniform;

			unsigned i;
			for (i = 0; i < instr->dest.reg->size; i++)
			{
				new_instr->sources[0].swizzle[i] = i;
				new_instr->dest.mask[i] = true;
			}
			for (; i < 4; i++)
				new_instr->dest.mask[i] = false;
			
			bool scalar = instr->dest.reg->size == 1;

			new_instr->dest.modifier = lima_pp_outmod_none;
			new_instr->dest.reg = instr->dest.reg;

			instr->dest.reg = NULL;
			instr->dest.pipeline = true;
			instr->dest.pipeline_reg = lima_pp_lir_pipeline_reg_uniform;

			ret->uniform_instr = instr;
			
			unsigned pos;
			if (scalar)
			{
				pos = lima_pp_lir_alu_scalar_add;
				ret->alu_instrs[lima_pp_lir_alu_scalar_add] = new_instr;
			}
			else
			{
				pos = lima_pp_lir_alu_vector_add;
				ret->alu_instrs[lima_pp_lir_alu_vector_add] = new_instr;
			}
			
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_mul] = true;
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_add] = true;
			if (scalar)
			{
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_mul] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_add] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_combine] = true;
			}
			
			ptrset_remove(&new_instr->dest.reg->defs, instr);
			ptrset_add(&new_instr->dest.reg->defs, new_instr);
			break;
		}
		
		/* texture sampler slot */
		case lima_pp_hir_op_texld_2d:
		case lima_pp_hir_op_texld_2d_off:
		case lima_pp_hir_op_texld_2d_lod:
		case lima_pp_hir_op_texld_2d_off_lod:
		case lima_pp_hir_op_texld_2d_proj_z:
		case lima_pp_hir_op_texld_2d_proj_z_off:
		case lima_pp_hir_op_texld_2d_proj_z_lod:
		case lima_pp_hir_op_texld_2d_proj_z_off_lod:
		case lima_pp_hir_op_texld_2d_proj_w:
		case lima_pp_hir_op_texld_2d_proj_w_off:
		case lima_pp_hir_op_texld_2d_proj_w_lod:
		case lima_pp_hir_op_texld_2d_proj_w_off_lod:
		case lima_pp_hir_op_texld_cube:
		case lima_pp_hir_op_texld_cube_off:
		case lima_pp_hir_op_texld_cube_lod:
		case lima_pp_hir_op_texld_cube_off_lod:
		{
			lima_pp_lir_instr_t* coord_instr = lima_pp_lir_instr_create();
			if (!coord_instr)
				return NULL;
			
			coord_instr->sched_instr = ret;
			
			coord_instr->op = lima_pp_hir_op_mov;

			unsigned coord_size = lima_pp_hir_op[instr->op].arg_sizes[0];

			coord_instr->sources[0] = instr->sources[0];
			
			coord_instr->dest.reg = NULL;
			coord_instr->dest.pipeline = true;
			coord_instr->dest.pipeline_reg = lima_pp_lir_pipeline_reg_discard;
			coord_instr->dest.modifier = lima_pp_outmod_none;
			
			for (i = 0; i < coord_size; i++)
				coord_instr->dest.mask[i] = true;
			for (; i < 4; i++)
				coord_instr->dest.mask[i] = false;
			
			ret->varying_instr = coord_instr;
			
			lima_pp_lir_reg_t* reg = instr->sources[0].reg;
			ptrset_remove(&reg->uses, instr);
			ptrset_add(&reg->uses, coord_instr);
			
			instr->sources[0].reg = NULL;
			instr->sources[0].pipeline = true;
			
			//doesn't matter which one
			instr->sources[0].pipeline_reg = lima_pp_lir_pipeline_reg_discard;
			
			lima_pp_lir_instr_t* new_instr = lima_pp_lir_instr_create();
			
			if (!new_instr)
				return false;

			new_instr->sched_instr = ret;
			
			new_instr->op = lima_pp_hir_op_mov;
			
			new_instr->sources[0].constant = false;
			new_instr->sources[0].pipeline = true;
			new_instr->sources[0].absolute = false;
			new_instr->sources[0].negate = false;
			new_instr->sources[0].pipeline_reg = lima_pp_lir_pipeline_reg_sampler;

			for (i = 0; i < instr->dest.reg->size; i++)
			{
				new_instr->sources[0].swizzle[i] = i;
				new_instr->dest.mask[i] = true;
			}
			for (; i < 4; i++)
				new_instr->dest.mask[i] = false;
			
			bool scalar = instr->dest.reg->size == 1;
			
			new_instr->dest.modifier = lima_pp_outmod_none;
			new_instr->dest.reg = instr->dest.reg;
			
			ptrset_remove(&new_instr->dest.reg->defs, instr);
			ptrset_add(&new_instr->dest.reg->defs, new_instr);
			
			instr->dest.reg = NULL;
			instr->dest.pipeline = true;
			instr->dest.pipeline_reg = lima_pp_lir_pipeline_reg_sampler;
			
			ret->texld_instr = instr;
			
			unsigned pos;
			if (scalar)
			{
				pos = lima_pp_lir_alu_scalar_add;
				ret->alu_instrs[lima_pp_lir_alu_scalar_add] = new_instr;
			}
			else
			{
				pos = lima_pp_lir_alu_vector_add;
				ret->alu_instrs[lima_pp_lir_alu_vector_add] = new_instr;
			}
			
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_mul] = true;
			ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_vector_add] = true;
			if (scalar)
			{
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_mul] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_scalar_add] = true;
				ret->possible_alu_instr_pos[pos][lima_pp_lir_alu_combine] = true;
			}
			
			break;
		}
			
		/* temp write slot */
		case lima_pp_hir_op_storet_one:
		case lima_pp_hir_op_storet_one_off:
		case lima_pp_hir_op_storet_two:
		case lima_pp_hir_op_storet_two_off:
		case lima_pp_hir_op_storet_four:
		case lima_pp_hir_op_storet_four_off:
		case lima_pp_hir_op_fb_color:
		case lima_pp_hir_op_fb_depth:
			ret->temp_store_instr = instr;
			break;
			
		case lima_pp_hir_op_branch:
		case lima_pp_hir_op_branch_gt:
		case lima_pp_hir_op_branch_eq:
		case lima_pp_hir_op_branch_ge:
		case lima_pp_hir_op_branch_lt:
		case lima_pp_hir_op_branch_ne:
		case lima_pp_hir_op_branch_le:
			ret->branch_instr = instr;
			break;
		
		default:
			return NULL;
			
	}
	
	convert_constants(ret);
	lima_pp_lir_instr_compress_consts(ret);
	
	return ret;
}
