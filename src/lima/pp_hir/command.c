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
#include <string.h>


static lima_pp_hir_cmd_t* create_cmd(unsigned num_args)
{
	lima_pp_hir_cmd_t* cmd;
	if (num_args > 0)
	{
		cmd = (lima_pp_hir_cmd_t*)malloc(sizeof(lima_pp_hir_cmd_t)
									 + ((num_args - 1)
									 * sizeof(lima_pp_hir_source_t)));
	}
	else
	{
		cmd = (lima_pp_hir_cmd_t*)malloc(sizeof(lima_pp_hir_cmd_t));
	}
	if (!cmd)
		return NULL;
	
	cmd->dst = lima_pp_hir_dest_default;
	cmd->num_args = num_args;
	cmd->block = NULL;
	cmd->shift = 0;
	
	unsigned i;
	for (i = 0; i < num_args; i++)
		cmd->src[i] = lima_pp_hir_source_default;
	
	if (!ptrset_create(&cmd->cmd_uses))
	{
		free(cmd);
		return NULL;
	}
	
	if (!ptrset_create(&cmd->block_uses))
	{
		ptrset_delete(cmd->cmd_uses);
		free(cmd);
		return NULL;
	}
	
	return cmd;
}

lima_pp_hir_cmd_t* lima_pp_hir_cmd_create(lima_pp_hir_op_e op)
{
	if (op > lima_pp_hir_op_count)
		return NULL;

	lima_pp_hir_cmd_t* cmd = create_cmd(lima_pp_hir_op[op].args);
	if (!cmd)
		return NULL;
	
	cmd->op = op;
	return cmd;
}

lima_pp_hir_cmd_t* lima_pp_hir_phi_create(unsigned num_args)
{
	if (num_args < 2)
		return NULL;
	
	lima_pp_hir_cmd_t* cmd = create_cmd(num_args);
	if (!cmd)
		return NULL;
	
	cmd->op = lima_pp_hir_op_phi;
	return cmd;
}

lima_pp_hir_cmd_t* lima_pp_hir_combine_create(unsigned num_args)
{
	if (num_args < 2 || num_args > 4)
		return NULL;
	
	lima_pp_hir_cmd_t* cmd = create_cmd(num_args);
	if (!cmd)
		return NULL;
	
	cmd->op = lima_pp_hir_op_combine;
	return cmd;
}

void lima_pp_hir_cmd_delete(lima_pp_hir_cmd_t* cmd)
{
	if (!cmd)
		return;

	unsigned i;
	for (i = 0; i < lima_pp_hir_op[cmd->op].args; i++)
	{
		if (cmd->src[i].constant)
			free(cmd->src[i].depend);
	}
	
	lima_pp_hir_cmd_t* use;
	ptrset_iter_t iter = ptrset_iter_create(cmd->cmd_uses);
	ptrset_iter_for_each(iter, use)
	{
		for (i = 0; i < use->num_args; i++)
		{
			if (use->src[i].constant)
				continue;
			if (use->src[i].depend == cmd)
				use->src[i].depend = NULL;
		}
	}
	
	lima_pp_hir_block_t* block;
	iter = ptrset_iter_create(cmd->block_uses);
	ptrset_iter_for_each(iter, block)
	{
		if (block->is_end)
		{
			if (!block->discard && block->output == cmd)
				block->output = NULL;
		}
		else
		{
			if (block->branch_cond != lima_pp_hir_branch_cond_always)
			{
				if (!block->reg_cond_a.is_constant &&
					block->reg_cond_a.reg == cmd)
					block->reg_cond_a.reg = NULL;
				if (!block->reg_cond_b.is_constant &&
					block->reg_cond_b.reg == cmd)
					block->reg_cond_b.reg = NULL;
			}
		}
	}
	
	ptrset_delete(cmd->cmd_uses);
	ptrset_delete(cmd->block_uses);

	free(cmd);
}



lima_pp_hir_source_t lima_pp_hir_source_copy(lima_pp_hir_source_t src)
{
	if (!src.constant)
		return src;
	lima_pp_hir_source_t ret = src;
	ret.depend = malloc(sizeof(lima_pp_hir_vec4_t));
	if (ret.depend)
		memcpy(ret.depend, src.depend, sizeof(lima_pp_hir_vec4_t));
	return ret;
}

void lima_pp_hir_cmd_replace_uses(lima_pp_hir_cmd_t* old_cmd,
								  lima_pp_hir_cmd_t* new_cmd)
{
	lima_pp_hir_cmd_t* cmd;
	ptrset_iter_t iter = ptrset_iter_create(old_cmd->cmd_uses);
	ptrset_iter_for_each(iter, cmd)
	{
		unsigned i;
		for (i = 0; i < cmd->num_args; i++)
		{
			if (cmd->src[i].constant)
				continue;
			if (cmd->src[i].depend == old_cmd)
				cmd->src[i].depend = new_cmd;
		}
	}
	
	lima_pp_hir_block_t* block;
	iter = ptrset_iter_create(old_cmd->block_uses);
	ptrset_iter_for_each(iter, block)
	{
		if (block->is_end)
		{
			if (!block->discard && block->output == old_cmd)
				block->output = new_cmd;
		}
		else
		{
			if (block->branch_cond != lima_pp_hir_branch_cond_always)
			{
				if (!block->reg_cond_a.is_constant &&
					block->reg_cond_a.reg == old_cmd)
					block->reg_cond_a.reg = new_cmd;
				if (!block->reg_cond_b.is_constant &&
					block->reg_cond_b.reg == old_cmd)
					block->reg_cond_b.reg = new_cmd;
			}
		}
	}
	
	ptrset_union(&new_cmd->cmd_uses, old_cmd->cmd_uses);
	ptrset_union(&new_cmd->block_uses, old_cmd->block_uses);
	ptrset_empty(&old_cmd->cmd_uses);
	ptrset_empty(&old_cmd->block_uses);
}



typedef enum
{
	_lima_pp_hir_file_src_type_normal,
	_lima_pp_hir_file_src_type_constant,
} _lima_pp_hir_file_src_type_e;

typedef struct
__attribute__((__packed__))
{
	unsigned reg      : 32;
	unsigned swizzle  :  8;
	bool     absolute :  1;
	bool     negate   :  1;
	unsigned type     :  6;
	unsigned reserved : 16;
} _lima_pp_hir_file_src_t;

typedef struct
__attribute__((__packed__))
{
	double x, y, z, w;
} _lima_pp_hir_file_vec4_t;

static lima_pp_hir_cmd_t* _lima_pp_hir_find_dep(lima_pp_hir_prog_t* prog, 
										lima_pp_hir_block_t* block, unsigned index)
{
	if (!prog)
		return false;
	
	lima_pp_hir_cmd_t* cmd;
	lima_pp_hir_block_t* iter_block;
	pp_hir_prog_for_each_block(prog, iter_block)
		pp_hir_block_for_each_cmd(iter_block, cmd)
			if (lima_pp_hir_op[cmd->op].has_dest && cmd->dst.reg.index == index)
				return cmd;

	pp_hir_block_for_each_cmd(block, cmd)
	{
		if (lima_pp_hir_op[cmd->op].has_dest && cmd->dst.reg.index == index)
			return cmd;
	}
	return NULL;
}

static bool import_src(void* data, unsigned size, unsigned* pos,
					   lima_pp_hir_prog_t* prog, lima_pp_hir_block_t* block,
					   lima_pp_hir_source_t* src)
{
	*pos = 0;
	
	*pos += sizeof(_lima_pp_hir_file_src_t);
	if (*pos > size)
		return false;
	
	_lima_pp_hir_file_src_t header = *((_lima_pp_hir_file_src_t*)data);
	data = (char*)data + sizeof(header);
	
	lima_pp_hir_cmd_t* dep = NULL;
	if (header.type
		== _lima_pp_hir_file_src_type_constant)
	{
		*pos += sizeof(_lima_pp_hir_file_vec4_t);
		if (*pos > size)
			return false;
		
		_lima_pp_hir_file_vec4_t v = *((_lima_pp_hir_file_vec4_t*)data);
		data = (char*)data + sizeof(v);
		if (src)
		{
			dep = malloc(sizeof(v));
			if (!dep) return false;
			memcpy(dep, &v, sizeof(v));
		}
	} else {
		dep = _lima_pp_hir_find_dep(prog, block, header.reg);
		if (!dep)
			return false;
	}

	if (src)
	{
		src->constant = (header.type
			== _lima_pp_hir_file_src_type_constant);
		src->depend = dep;
		unsigned i;
		for (i = 0; i < 4; i++)
			src->swizzle[i] = (header.swizzle >> (i << 1)) & 3;
		src->absolute = header.absolute;
		src->negate   = header.negate;
	}
	return true;
}

typedef struct
__attribute__((__packed__))
{
	lima_pp_hir_reg_t reg;
	struct
	__attribute__((__packed__))
	{
		lima_pp_outmod_e modifier : 4;
	};
	uint8_t reserved[3];
} _lima_pp_hir_file_dst_t;

typedef struct
__attribute__((__packed__))
{
	uint32_t            op;
	_lima_pp_hir_file_dst_t dst;
	uint32_t            args;
	uint32_t            load_store_index;
	int32_t             shift;
} _lima_pp_hir_file_cmd_t;

lima_pp_hir_cmd_t* lima_pp_hir_cmd_import(void* data, unsigned size, unsigned* pos,
										  lima_pp_hir_prog_t* prog,
										  lima_pp_hir_block_t* block)
{
	*pos = 0;
	
	*pos += sizeof(_lima_pp_hir_file_cmd_t);
	if (*pos > size)
		return NULL;
	
	_lima_pp_hir_file_cmd_t header = *((_lima_pp_hir_file_cmd_t*)data);
	data = (char*)data + sizeof(header);

	lima_pp_hir_cmd_t* cmd;
	if (header.op == lima_pp_hir_op_phi)
		cmd = lima_pp_hir_phi_create(header.args);
	else if (header.op == lima_pp_hir_op_combine)
		cmd = lima_pp_hir_combine_create(header.args);
	else
		cmd = lima_pp_hir_cmd_create(header.op);
	if (!cmd) return NULL;
	cmd->shift = header.shift;

	cmd->dst.reg = header.dst.reg;
	unsigned i;
	cmd->dst.modifier = header.dst.modifier;

	for (i = 0; i < cmd->num_args; i++)
	{
		unsigned cmd_pos;
		if (!import_src(data, size - *pos, &cmd_pos, prog, block, &cmd->src[i]))
		{
			lima_pp_hir_cmd_delete(cmd);
			fprintf(stderr, "Error: Failed to import command, couldn't read source %u\n", i);
			return NULL;
		}
		
		*pos += cmd_pos;
		data = (char*)data + cmd_pos;
	}
	
	if (lima_pp_hir_op_is_load_store(header.op))
		cmd->load_store_index = header.load_store_index;

	return cmd;
}

void* lima_pp_hir_cmd_export(lima_pp_hir_cmd_t* cmd, unsigned* size)
{
	if (!cmd)
		return NULL;
	
	*size = 0;

	_lima_pp_hir_file_cmd_t header;
	header.op       = cmd->op;
	header.args     = cmd->num_args;
	header.shift    = cmd->shift;
	header.dst.reg  = cmd->dst.reg;
	header.dst.modifier = cmd->dst.modifier;
	header.dst.reserved[0] = 0;
	header.dst.reserved[1] = 0;
	header.dst.reserved[2] = 0;
	
	if (lima_pp_hir_op_is_load_store(cmd->op))
		header.load_store_index = cmd->load_store_index;

	void* data = malloc(sizeof(header));
	if (!data)
		return NULL;
	
	memcpy(data, &header, sizeof(header));
	*size += sizeof(header);
	

	unsigned i;
	for (i = 0; i < cmd->num_args; i++)
	{
		if (!cmd->src[i].depend)
			return NULL;

		_lima_pp_hir_file_src_t source;
		if (cmd->src[i].constant)
		{
			source.reg  = 0;
			source.type = _lima_pp_hir_file_src_type_constant;
		} else {
			source.reg  = ((lima_pp_hir_cmd_t*)cmd->src[i].depend)->dst.reg.index;
			source.type = _lima_pp_hir_file_src_type_normal;
		}
		source.absolute = cmd->src[i].absolute;
		source.negate   = cmd->src[i].negate;
		source.swizzle = 0;
		unsigned j;
		for (j = 0; j < 4; j++)
			source.swizzle |= (cmd->src[i].swizzle[j] << (j << 1));
		source.reserved = 0;

		data = realloc(data, *size + sizeof(source));
		if (!data)
			return NULL;
		memcpy((char*)data + *size, &source, sizeof(source));
		*size += sizeof(source);

		if (cmd->src[i].constant)
		{
			_lima_pp_hir_file_vec4_t v;
			v.x = ((lima_pp_hir_vec4_t*)cmd->src[i].depend)->x;
			v.y = ((lima_pp_hir_vec4_t*)cmd->src[i].depend)->y;
			v.z = ((lima_pp_hir_vec4_t*)cmd->src[i].depend)->z;
			v.w = ((lima_pp_hir_vec4_t*)cmd->src[i].depend)->w;
			data = realloc(data, *size + sizeof(v));
			if (!data)
				return NULL;
			memcpy((char*)data + *size, &v, sizeof(v));
			*size += sizeof(v);
		}
	}

	return data;
}
