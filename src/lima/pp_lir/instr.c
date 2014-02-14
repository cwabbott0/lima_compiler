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

#include "pp_lir.h"
#include <stdlib.h>
#include <string.h>


lima_pp_lir_instr_t* lima_pp_lir_instr_create(void)
{
	return calloc(sizeof(lima_pp_lir_instr_t), 1);
}

void lima_pp_lir_instr_delete(lima_pp_lir_instr_t *instr)
{
	free(instr);
}

lima_pp_lir_scheduled_instr_t* lima_pp_lir_scheduled_instr_create(void)
{
	lima_pp_lir_scheduled_instr_t* instr =
		calloc(sizeof(lima_pp_lir_scheduled_instr_t), 1);
	if (!instr)
		return NULL;
	
	if (!ptrset_create(&instr->preds))
	{
		free(instr);
		return NULL;
	}
	
	if (!ptrset_create(&instr->succs))
	{
		ptrset_delete(instr->preds);
		free(instr);
		return NULL;
	}
	
	if (!ptrset_create(&instr->min_preds))
	{
		ptrset_delete(instr->preds);
		ptrset_delete(instr->succs);
		free(instr);
		return NULL;
	}
	
	if (!ptrset_create(&instr->min_succs))
	{
		ptrset_delete(instr->preds);
		ptrset_delete(instr->succs);
		ptrset_delete(instr->min_preds);
		free(instr);
		return NULL;
	}
	
	if (!ptrset_create(&instr->true_preds))
	{
		ptrset_delete(instr->preds);
		ptrset_delete(instr->succs);
		ptrset_delete(instr->min_preds);
		ptrset_delete(instr->min_succs);
		free(instr);
		return NULL;
	}
	
	if (!ptrset_create(&instr->true_succs))
	{
		ptrset_delete(instr->preds);
		ptrset_delete(instr->succs);
		ptrset_delete(instr->min_preds);
		ptrset_delete(instr->min_succs);
		ptrset_delete(instr->true_preds);
		free(instr);
		return NULL;
	}
	
	return instr;
}

void lima_pp_lir_scheduled_instr_delete(lima_pp_lir_scheduled_instr_t *instr)
{
	unsigned i;
	if (instr->varying_instr)
		lima_pp_lir_instr_delete(instr->varying_instr);
	if (instr->texld_instr)
		lima_pp_lir_instr_delete(instr->texld_instr);
	if (instr->uniform_instr)
		lima_pp_lir_instr_delete(instr->uniform_instr);
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			lima_pp_lir_instr_delete(instr->alu_instrs[i]);
	if (instr->temp_store_instr)
		lima_pp_lir_instr_delete(instr->temp_store_instr);
	if (instr->branch_instr)
		lima_pp_lir_instr_delete(instr->branch_instr);
	
	ptrset_delete(instr->preds);
	ptrset_delete(instr->succs);
	ptrset_delete(instr->min_preds);
	ptrset_delete(instr->min_succs);
	ptrset_delete(instr->true_preds);
	ptrset_delete(instr->true_succs);
	
	free(instr);
}

typedef struct
{
	uint32_t index;
	bool precolored : 1;
} _reg_data_t;

typedef struct
{
	uint32_t reg;
} _pipeline_reg_data_t;

typedef struct
{
	double data[4];
} _vec4_const_data_t;

typedef struct
{
	unsigned mask : 4;
	unsigned modifier : 2;
	bool pipeline : 1;
} _dest_header_t;

static void* dest_export(lima_pp_lir_dest_t dest, unsigned* size)
{
	*size = sizeof(_dest_header_t);
	
	if (dest.pipeline)
		*size += sizeof(_pipeline_reg_data_t);
	else
		*size += sizeof(_reg_data_t);
	
	void* data = malloc(*size);
	if (!data)
		return NULL;
	
	_dest_header_t* header = data;
	
	header->mask = 0;
	unsigned i;
	for (i = 0; i < 4; i++)
		if (dest.mask[i])
			header->mask |= 1 << i;
	
	header->modifier = dest.modifier;
	header->pipeline = dest.pipeline;
	
	if (dest.pipeline)
	{
		_pipeline_reg_data_t* pipeline_data = (_pipeline_reg_data_t*)(header + 1);
		pipeline_data->reg = dest.pipeline_reg;
	}
	else
	{
		_reg_data_t* reg_data = (_reg_data_t*)(header + 1);
		reg_data->index = dest.reg->index;
		reg_data->precolored = dest.reg->precolored;
	}
	
	return data;
}

static void* dest_import(lima_pp_lir_dest_t* dest, lima_pp_lir_prog_t* prog,
						 void* data, unsigned* len)
{
	_dest_header_t* header = data;
	dest->pipeline = header->pipeline;
	dest->modifier = header->modifier;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		dest->mask[i] = (header->mask >> i) & 1;
	
	data = header + 1;
	*len += sizeof(*header);
	
	if (dest->pipeline)
	{
		_pipeline_reg_data_t* pipeline_data = data;
		dest->pipeline_reg = pipeline_data->reg;
		data = pipeline_data + 1;
		*len += sizeof(*pipeline_data);
	}
	else
	{
		_reg_data_t* reg_data = data;
		dest->reg = lima_pp_lir_prog_find_reg(prog, reg_data->index, reg_data->precolored);
		if (!dest->reg)
			return NULL;
		data = reg_data + 1;
		*len += sizeof(*reg_data);
	}
	
	return data;
}

typedef struct
{
	unsigned swizzle : 8;
	bool constant : 1;
	bool pipeline : 1;
	bool absolute : 1;
	bool negate : 1;
} _src_header_t;

static void* source_export(lima_pp_lir_source_t src, unsigned* size)
{
	*size = sizeof(_src_header_t);
	if (src.constant)
		*size += sizeof(_vec4_const_data_t);
	else if (src.pipeline)
		*size += sizeof(_pipeline_reg_data_t);
	else
		*size += sizeof(_reg_data_t);
	
	void* data = malloc(*size);
	if (!data)
		return NULL;
	
	_src_header_t* header = data;
	header->swizzle = 0;
	unsigned i;
	for (i = 0; i < 4; i++)
		header->swizzle |= src.swizzle[i] << (i * 2);
	
	header->constant = src.constant;
	header->pipeline = src.pipeline;
	header->absolute = src.absolute;
	header->negate = src.negate;
	
	if (src.constant)
	{
		_vec4_const_data_t* vec4_data = (_vec4_const_data_t*)(header + 1);
		memcpy(vec4_data, src.reg, 4 * sizeof(double));
	}
	else if (src.pipeline)
	{
		_pipeline_reg_data_t* reg_data = (_pipeline_reg_data_t*)(header + 1);
		reg_data->reg = src.pipeline_reg;
	}
	else
	{
		_reg_data_t* reg_data = (_reg_data_t*)(header + 1);
		lima_pp_lir_reg_t* reg = src.reg;
		reg_data->index = reg->index;
		reg_data->precolored = reg->precolored;
	}
	
	return data;
}

static void* source_import(lima_pp_lir_source_t* src, lima_pp_lir_prog_t* prog,
						   void* data, unsigned* len)
{
	_src_header_t* header = data;
	data = header + 1;
	*len += sizeof(*header);
	
	unsigned i;
	for (i = 0; i < 4; i++)
		src->swizzle[i] = (header->swizzle >> (i * 2)) & 3;
	
	src->constant = header->constant;
	src->pipeline = header->pipeline;
	src->absolute = header->absolute;
	src->negate = header->negate;
	
	if (src->constant)
	{
		_vec4_const_data_t* vec4_data = data;
		src->reg = malloc(4 * sizeof(double));
		if (!src->reg)
			return NULL;
		
		memcpy(src->reg, vec4_data, 4 * sizeof(double));
		
		data = vec4_data + 1;
		*len += sizeof(*vec4_data);
	}
	else if (src->pipeline)
	{
		_pipeline_reg_data_t* reg_data = data;
		src->pipeline_reg = reg_data->reg;
		
		data = reg_data + 1;
		*len += sizeof(*reg_data);
	}
	else
	{
		_reg_data_t* reg_data = data;
		src->reg = lima_pp_lir_prog_find_reg(prog, reg_data->index, reg_data->precolored);
		if (!src->reg)
			return NULL;
		
		data = reg_data + 1;
		*len += sizeof(*reg_data);
	}
	
	return data;
}

static void add_defs_and_uses(lima_pp_lir_instr_t* instr)
{
	if (lima_pp_hir_op[instr->op].has_dest && !instr->dest.pipeline)
		ptrset_add(&instr->dest.reg->defs, instr);
	
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (!instr->sources[i].constant &&
			!instr->sources[i].pipeline)
		{
			lima_pp_lir_reg_t* reg = instr->sources[i].reg;
			ptrset_add(&reg->uses, instr);
		}
	}
}

typedef struct
{
	uint32_t op;
	uint32_t load_store_index;
	uint32_t branch_dest;
	int32_t  shift;
} _instr_header_t;

void* lima_pp_lir_instr_export(lima_pp_lir_instr_t* instr, unsigned* size)
{
	*size = sizeof(_instr_header_t);
	void* data = malloc(*size);
	
	_instr_header_t* header = data;
	header->op = instr->op;
	if (lima_pp_hir_op_is_load_store(instr->op))
		header->load_store_index = instr->load_store_index;
	if (lima_pp_hir_op_is_branch(instr->op))
		header->branch_dest = instr->branch_dest;
	header->shift = instr->shift;
	
	if (lima_pp_hir_op[instr->op].has_dest)
	{
		unsigned dest_size;
		void* dest_data = dest_export(instr->dest, &dest_size);
		if (!dest_data)
		{
			free(data);
			return NULL;
		}
		
		data = realloc(data, *size + dest_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, dest_data, dest_size);
		*size += dest_size;
		free(dest_data);
	}
	
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		unsigned src_size;
		void* src_data = source_export(instr->sources[i], &src_size);
		if (!src_data)
		{
			free(data);
			return NULL;
		}
		
		data = realloc(data, *size + src_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, src_data, src_size);
		*size += src_size;
		free(src_data);
	}
	
	return data;
}

lima_pp_lir_instr_t* lima_pp_lir_instr_import(
	void* data, unsigned* len, lima_pp_lir_prog_t* prog)
{
	*len = sizeof(_instr_header_t);
	_instr_header_t* header = data;
	data = header + 1;
	
	lima_pp_lir_instr_t* instr = lima_pp_lir_instr_create();
	if (!instr)
		return NULL;
	
	instr->op = header->op;
	if (lima_pp_hir_op_is_load_store(instr->op))
		instr->load_store_index = header->load_store_index;
	if (lima_pp_hir_op_is_branch(instr->op))
		instr->branch_dest = header->branch_dest;
	instr->shift = header->shift;
	
	if (lima_pp_hir_op[instr->op].has_dest)
	{
		data = dest_import(&instr->dest, prog, data, len);
		if (!data)
		{
			lima_pp_lir_instr_delete(instr);
			return NULL;
		}
	}
	
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		data = source_import(&instr->sources[i], prog, data, len);
		if (!data)
		{
			lima_pp_lir_instr_delete(instr);
			return NULL;
		}
	}
	
	add_defs_and_uses(instr);
	
	return instr;
}

typedef struct
{
	unsigned const0_size : 3; /* 0-4 */
	unsigned const1_size : 3;
	unsigned num_alu_instrs : 3; /* 0-5 */
	bool alu_instr0 : 1;
	bool alu_instr1 : 1;
	bool alu_instr2 : 1;
	bool alu_instr3 : 1;
	bool alu_instr4 : 1;
	unsigned alu_instr0_possible : 5;
	unsigned alu_instr1_possible : 5;
	unsigned alu_instr2_possible : 5;
	unsigned alu_instr3_possible : 5;
	unsigned alu_instr4_possible : 5;
	bool varying : 1;
	bool texld : 1;
	bool uniform : 1;
	bool temp_store : 1;
	bool branch : 1;
} _sched_instr_header_t;

void* lima_pp_lir_scheduled_instr_export(
	lima_pp_lir_scheduled_instr_t* instr, unsigned* size)
{
	*size = sizeof(_sched_instr_header_t);
	void* data = malloc(*size);
	if (!data)
		return NULL;
	
	_sched_instr_header_t* header = data;
	header->const0_size = instr->const0_size;
	header->const1_size = instr->const1_size;
	header->varying = !!(instr->varying_instr);
	header->texld = !!(instr->texld_instr);
	header->uniform = !!(instr->uniform_instr);
	header->temp_store = !!(instr->temp_store_instr);
	header->branch = !!(instr->branch_instr);
	
	header->alu_instr0 = !!(instr->alu_instrs[0]);
	header->alu_instr1 = !!(instr->alu_instrs[1]);
	header->alu_instr2 = !!(instr->alu_instrs[2]);
	header->alu_instr3 = !!(instr->alu_instrs[3]);
	header->alu_instr4 = !!(instr->alu_instrs[4]);
	
	header->alu_instr0_possible =
		instr->possible_alu_instr_pos[0][0] |
		instr->possible_alu_instr_pos[0][1] << 1 |
		instr->possible_alu_instr_pos[0][2] << 2 |
		instr->possible_alu_instr_pos[0][3] << 3 |
		instr->possible_alu_instr_pos[0][4] << 4;
	
	header->alu_instr1_possible =
		instr->possible_alu_instr_pos[1][0] |
		instr->possible_alu_instr_pos[1][1] << 1 |
		instr->possible_alu_instr_pos[1][2] << 2 |
		instr->possible_alu_instr_pos[1][3] << 3 |
		instr->possible_alu_instr_pos[1][4] << 4;
	
	header->alu_instr2_possible =
		instr->possible_alu_instr_pos[2][0] |
		instr->possible_alu_instr_pos[2][1] << 1 |
		instr->possible_alu_instr_pos[2][2] << 2 |
		instr->possible_alu_instr_pos[2][3] << 3 |
		instr->possible_alu_instr_pos[2][4] << 4;
	
	header->alu_instr3_possible =
		instr->possible_alu_instr_pos[3][0] |
		instr->possible_alu_instr_pos[3][1] << 1 |
		instr->possible_alu_instr_pos[3][2] << 2 |
		instr->possible_alu_instr_pos[3][3] << 3 |
		instr->possible_alu_instr_pos[3][4] << 4;
	
	header->alu_instr4_possible =
		instr->possible_alu_instr_pos[4][0] |
		instr->possible_alu_instr_pos[4][1] << 1 |
		instr->possible_alu_instr_pos[4][2] << 2 |
		instr->possible_alu_instr_pos[4][3] << 3 |
		instr->possible_alu_instr_pos[4][4] << 4;
	
	*size += (instr->const0_size + instr->const1_size) * sizeof(double);
	data = realloc(data, *size);
	if (!data)
		return NULL;
	
	double* const_data = data + sizeof(_sched_instr_header_t);
	unsigned i;
	for (i = 0; i < instr->const0_size; i++)
	{
		*const_data = instr->const0[i];
		const_data++;
	}
	
	for (i = 0; i < instr->const1_size; i++)
	{
		*const_data = instr->const1[i];
		const_data++;
	}
	
	if (instr->varying_instr)
	{
		unsigned instr_size;
		void* instr_data = lima_pp_lir_instr_export(instr->varying_instr, &instr_size);
		if (!instr_data)
		{
			free(data);
			return NULL;
		}
		data = realloc(data, *size + instr_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, instr_data, instr_size);
		*size += instr_size;
		free(instr_data);
	}
	
	if (instr->texld_instr)
	{
		unsigned instr_size;
		void* instr_data = lima_pp_lir_instr_export(instr->texld_instr, &instr_size);
		if (!instr_data)
		{
			free(data);
			return NULL;
		}
		data = realloc(data, *size + instr_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, instr_data, instr_size);
		*size += instr_size;
		free(instr_data);
	}
	
	if (instr->uniform_instr)
	{
		unsigned instr_size;
		void* instr_data = lima_pp_lir_instr_export(instr->uniform_instr, &instr_size);
		if (!instr_data)
		{
			free(data);
			return NULL;
		}
		data = realloc(data, *size + instr_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, instr_data, instr_size);
		*size += instr_size;
		free(instr_data);
	}
	
	for (i = 0; i < 5; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		unsigned instr_size;
		void* instr_data = lima_pp_lir_instr_export(instr->alu_instrs[i], &instr_size);
		if (!instr_data)
		{
			free(data);
			return NULL;
		}
		data = realloc(data, *size + instr_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, instr_data, instr_size);
		*size += instr_size;
		free(instr_data);
	}
	
	if (instr->temp_store_instr)
	{
		unsigned instr_size;
		void* instr_data = lima_pp_lir_instr_export(instr->temp_store_instr, &instr_size);
		if (!instr_data)
		{
			free(data);
			return NULL;
		}
		data = realloc(data, *size + instr_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, instr_data, instr_size);
		*size += instr_size;
		free(instr_data);
	}
	
	if (instr->branch_instr)
	{
		unsigned instr_size;
		void* instr_data = lima_pp_lir_instr_export(instr->branch_instr, &instr_size);
		if (!instr_data)
		{
			free(data);
			return NULL;
		}
		data = realloc(data, *size + instr_size);
		if (!data)
			return NULL;
		
		memcpy((char*)data + *size, instr_data, instr_size);
		*size += instr_size;
		free(instr_data);
	}
	
	return data;
}

lima_pp_lir_scheduled_instr_t* lima_pp_lir_scheduled_instr_import(
	void* data, unsigned* len, lima_pp_lir_prog_t* prog)
{
	unsigned i;
	
	_sched_instr_header_t* header = data;
	data = header + 1;
	*len = sizeof(*header);
	
	lima_pp_lir_scheduled_instr_t* instr = lima_pp_lir_scheduled_instr_create();
	if (!instr)
		return NULL;
	
	instr->const0_size = header->const0_size;
	instr->const1_size = header->const1_size;
	
	for (i = 0; i < 5; i++)
		instr->possible_alu_instr_pos[0][i] =
			(header->alu_instr0_possible >> i) & 1;
	
	for (i = 0; i < 5; i++)
		instr->possible_alu_instr_pos[1][i] =
			(header->alu_instr1_possible >> i) & 1;
	
	for (i = 0; i < 5; i++)
		instr->possible_alu_instr_pos[2][i] =
			(header->alu_instr2_possible >> i) & 1;
	
	for (i = 0; i < 5; i++)
		instr->possible_alu_instr_pos[3][i] =
			(header->alu_instr3_possible >> i) & 1;
	
	for (i = 0; i < 5; i++)
		instr->possible_alu_instr_pos[4][i] =
			(header->alu_instr4_possible >> i) & 1;
	
	bool alu_instrs[5];
	alu_instrs[0] = header->alu_instr0;
	alu_instrs[1] = header->alu_instr1;
	alu_instrs[2] = header->alu_instr2;
	alu_instrs[3] = header->alu_instr3;
	alu_instrs[4] = header->alu_instr4;
	
	double* const_data = data;
	for (i = 0; i < header->const0_size; i++)
	{
		instr->const0[i] = *const_data;
		const_data += 1;
		*len += sizeof(double);
	}
	
	for (i = 0; i < header->const1_size; i++)
	{
		instr->const1[i] = *const_data;
		const_data += 1;
		*len += sizeof(double);
	}
	
	data = const_data;
	
	unsigned instr_size;
	
	if (header->varying)
	{
		instr->varying_instr = lima_pp_lir_instr_import(data, &instr_size, prog);
		if (!instr->varying_instr)
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return NULL;
		}
		instr->varying_instr->sched_instr = instr;
		data = (char*)data + instr_size;
		*len += instr_size;
	}
	
	if (header->texld)
	{
		instr->texld_instr = lima_pp_lir_instr_import(data, &instr_size, prog);
		if (!instr->texld_instr)
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return NULL;
		}
		instr->texld_instr->sched_instr = instr;
		data = (char*)data + instr_size;
		*len += instr_size;
	}
	
	if (header->uniform)
	{
		instr->uniform_instr = lima_pp_lir_instr_import(data, &instr_size, prog);
		if (!instr->uniform_instr)
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return NULL;
		}
		instr->uniform_instr->sched_instr = instr;
		data = (char*)data + instr_size;
		*len += instr_size;
	}
	
	for (i = 0; i < 5; i++)
	{
		if (!alu_instrs[i])
			continue;
		
		instr->alu_instrs[i] = lima_pp_lir_instr_import(data, &instr_size, prog);
		if (!instr->alu_instrs[i])
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return NULL;
		}
		instr->alu_instrs[i]->sched_instr = instr;
		data = (char*)data + instr_size;
		*len += instr_size;
	}
	
	if (header->temp_store)
	{
		instr->temp_store_instr = lima_pp_lir_instr_import(data, &instr_size, prog);
		if (!instr->temp_store_instr)
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return NULL;
		}
		instr->temp_store_instr->sched_instr = instr;
		data = (char*)data + instr_size;
		*len += instr_size;
	}
	
	if (header->branch)
	{
		instr->branch_instr = lima_pp_lir_instr_import(data, &instr_size, prog);
		if (!instr->branch_instr)
		{
			lima_pp_lir_scheduled_instr_delete(instr);
			return NULL;
		}
		instr->branch_instr->sched_instr = instr;
		data = (char*)data + instr_size;
		*len += instr_size;
	}
	
	return instr;
}

bool lima_pp_lir_sched_instr_is_empty(lima_pp_lir_scheduled_instr_t* instr)
{
	if (instr->varying_instr)
		return false;
	
	if (instr->texld_instr)
		return false;
	
	if (instr->uniform_instr)
		return false;
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			return false;
	
	if (instr->temp_store_instr)
		return false;
	
	if (instr->branch_instr)
		return false;
	
	return true;
}
