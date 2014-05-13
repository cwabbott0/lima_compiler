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

#include "symbols.h"
#include <stdint.h>
#include <stdlib.h>

typedef enum {
	mbs_symbol_type_float        = 1,
	mbs_symbol_type_int          = 2,
	mbs_symbol_type_bool         = 3,
	mbs_symbol_type_matrix       = 4,
	mbs_symbol_type_sampler2d    = 5,
	mbs_symbol_type_sampler_cube = 6,
	mbs_symbol_type_struct       = 8,
} mbs_symbol_type_e;

static const mbs_symbol_type_e types[lima_num_symbol_types] = {
	[lima_symbol_float] = mbs_symbol_type_float,
	[lima_symbol_vec2]  = mbs_symbol_type_float,
	[lima_symbol_vec3]  = mbs_symbol_type_float,
	[lima_symbol_vec4]  = mbs_symbol_type_float,
	[lima_symbol_int]   = mbs_symbol_type_int,
	[lima_symbol_ivec2] = mbs_symbol_type_int,
	[lima_symbol_ivec3] = mbs_symbol_type_int,
	[lima_symbol_ivec4] = mbs_symbol_type_int,
	[lima_symbol_bool]  = mbs_symbol_type_bool,
	[lima_symbol_bvec2] = mbs_symbol_type_bool,
	[lima_symbol_bvec3] = mbs_symbol_type_bool,
	[lima_symbol_bvec4] = mbs_symbol_type_bool,
	[lima_symbol_mat2]  = mbs_symbol_type_matrix,
	[lima_symbol_mat3]  = mbs_symbol_type_matrix,
	[lima_symbol_mat4]  = mbs_symbol_type_matrix,
	[lima_symbol_sampler2d] = mbs_symbol_type_sampler2d,
	[lima_symbol_sampler_cube] = mbs_symbol_type_sampler_cube,
	[lima_symbol_struct] = mbs_symbol_type_struct
};

typedef enum {
	mbs_symbol_precision_low = 1,
	mbs_symbol_precision_medium = 2,
	mbs_symbol_precision_high = 3
} mbs_symbol_precision_e;

static const mbs_symbol_precision_e precisions[lima_num_precisions] = {
	[lima_precision_low] = mbs_symbol_precision_low,
	[lima_precision_medium] = mbs_symbol_precision_medium,
	[lima_precision_high] = mbs_symbol_precision_high
};

static const unsigned component_counts[lima_num_symbol_types] = {
	[lima_symbol_float] = 1,
	[lima_symbol_bool] = 1,
	[lima_symbol_int] = 1,
	[lima_symbol_vec2]  = 2,
	[lima_symbol_ivec2] = 2,
	[lima_symbol_bvec2] = 2,
	[lima_symbol_mat2]  = 2,
	[lima_symbol_vec3]  = 3,
	[lima_symbol_ivec3] = 3,
	[lima_symbol_bvec3] = 3,
	[lima_symbol_mat3]  = 3,
	[lima_symbol_vec4]  = 4,
	[lima_symbol_ivec4] = 4,
	[lima_symbol_bvec4] = 4,
	[lima_symbol_mat4]  = 4,
	
	[lima_symbol_sampler2d] = 2,
	[lima_symbol_sampler_cube] = 3,
};

static const unsigned num_rows[lima_num_symbol_types] = {
	[lima_symbol_float] = 1,
	[lima_symbol_bool] = 1,
	[lima_symbol_int] = 1,
	[lima_symbol_vec2]  = 1,
	[lima_symbol_ivec2] = 1,
	[lima_symbol_bvec2] = 1,
	[lima_symbol_vec3]  = 1,
	[lima_symbol_ivec3] = 1,
	[lima_symbol_bvec3] = 1,
	[lima_symbol_vec4]  = 1,
	[lima_symbol_ivec4] = 1,
	[lima_symbol_bvec4] = 1,
	
	[lima_symbol_mat2]  = 2,
	[lima_symbol_mat3]  = 3,
	[lima_symbol_mat4]  = 4,
	
	[lima_symbol_struct] = 1,
	[lima_symbol_sampler2d] = 1,
	[lima_symbol_sampler_cube] = 1,
};

typedef struct {
	uint8_t unknown_0; //=0x00
	uint8_t type;
	uint16_t component_count;
	uint16_t component_size;
	uint16_t array_entries;
	uint16_t stride;
	uint8_t  unknown_1; //=0x10
	uint8_t  precision;
	uint32_t invariant; //=0x00000000
	uint16_t offset;
	uint16_t parent_index; //=0xFFFF if not part of a structure
} mbs_uniform_t;

//added to newer compilers, this seems to always be the same
static const uint32_t vidx_blob[9] = {
	0x52445449,
	0x00000004,
	0xFFFFFFFF,
	0x56555949,
	0x00000004,
	0xFFFFFFFF,
	0x44524749,
	0x00000004,
	0x00000001,
};

static bool mbs_uniform_export(mbs_chunk_t* uniform_table, lima_symbol_t* symbol,
							   int parent_index, unsigned* cur_index)
{
	mbs_chunk_t* chunk = mbs_chunk_create("VUNI");
	if (!chunk)
		return false;
	
	mbs_chunk_t* name = mbs_chunk_string(symbol->name);
	if (!name)
	{
		mbs_chunk_delete(chunk);
		return false;
	}
	
	if (!mbs_chunk_append(chunk, name))
	{
		mbs_chunk_delete(chunk);
		mbs_chunk_delete(name);
		return false;
	}
	
	mbs_uniform_t uniform = {
		.unknown_0 = 0,
		.type = types[symbol->type],
		.component_size = symbol->stride / num_rows[symbol->type],
		.array_entries = symbol->array_elems,
		.stride = symbol->stride,
		.unknown_1 = 0x10,
		.precision = precisions[symbol->precision],
		.invariant = 0,
		.offset = symbol->offset,
		.parent_index = (parent_index == -1) ? 0xFFFF : parent_index
	};
	
	if (symbol->type == lima_symbol_struct)
		uniform.component_count = symbol->num_children;
	else
		uniform.component_count = component_counts[symbol->type];
	
	if (!mbs_chunk_append_data(chunk, &uniform, sizeof(mbs_uniform_t)))
	{
		mbs_chunk_delete(chunk);
		return false;
	}
	
	mbs_chunk_t* vidx_chunk = mbs_chunk_create("VIDX");
	if (!vidx_chunk)
	{
		mbs_chunk_delete(chunk);
		return false;
	}
	
	if (!mbs_chunk_append_data(vidx_chunk, (void*) vidx_blob, 9 * 4))
	{
		mbs_chunk_delete(chunk);
		mbs_chunk_delete(vidx_chunk);
		return false;
	}
	
	if (!mbs_chunk_append(chunk, vidx_chunk))
	{
		mbs_chunk_delete(chunk);
		mbs_chunk_delete(vidx_chunk);
		return false;
	}
	
	if (symbol->array_const)
	{
		mbs_chunk_t* vini_chunk = mbs_chunk_create("VINI");
		if (!vini_chunk)
		{
			mbs_chunk_delete(chunk);
			return false;
		}
		
		uint32_t count = component_counts[symbol->type] * num_rows[symbol->type];
		if (!mbs_chunk_append_data(vini_chunk, &count, sizeof(uint32_t)) ||
			!mbs_chunk_append_data(vini_chunk, symbol->array_const,
								   count * sizeof(float)))
		{
			mbs_chunk_delete(chunk);
			mbs_chunk_delete(vini_chunk);
			return false;
		}
		
		if (!mbs_chunk_append(chunk, vini_chunk))
		{
			mbs_chunk_delete(chunk);
			mbs_chunk_delete(vini_chunk);
			return false;
		}
	}
	
	if (!mbs_chunk_append(uniform_table, chunk))
	{
		mbs_chunk_delete(chunk);
		return false;
	}
	
	if (symbol->type == lima_symbol_struct)
	{
		unsigned new_parent_idx = *cur_index;
		for (unsigned i = 0; i < symbol->num_children; i++)
		{
			if (!mbs_uniform_export(uniform_table, symbol->children[i],
									(int) new_parent_idx, cur_index))
			{
				return false;
			}
		}
	}
	
	(*cur_index)++;
	return true;
}

static unsigned _get_num_symbols(lima_symbol_t* symbol)
{
	if (symbol->type == lima_symbol_struct)
	{
		unsigned ret = 1; //one for the struct itself
		for (unsigned i = 0; i < symbol->num_children; i++)
			ret += _get_num_symbols(symbol->children[i]);
		return ret;
	}
	
	return 1;
}

static unsigned get_num_symbols(lima_symbol_table_t* table)
{
	unsigned ret = 0;
	for (unsigned i = 0; i < table->num_symbols; i++)
		ret += _get_num_symbols(table->symbols[i]);
	
	return ret;
}

mbs_chunk_t* lima_export_uniform_table(lima_shader_symbols_t* symbols)
{
	mbs_chunk_t* uniform_table = mbs_chunk_create("SUNI");
	if (!uniform_table)
		return NULL;
	
	uint32_t num_symbols = get_num_symbols(&symbols->uniform_table);
	if (!mbs_chunk_append_data(uniform_table, &num_symbols, 4))
	{
		mbs_chunk_delete(uniform_table);
		return NULL;
	}
	
	uint32_t size = symbols->uniform_table.total_size;
	//for some reason, the blob aligns uniform size to 4, so do that here too
	size = (size + 3) & ~3;
	if (!mbs_chunk_append_data(uniform_table, &size, 4))
	{
		mbs_chunk_delete(uniform_table);
		return NULL;
	}
	
	for (unsigned i = 0; i < symbols->uniform_table.num_symbols; i++)
	{
		unsigned cur_index = 0;
		if (!mbs_uniform_export(uniform_table,
								symbols->uniform_table.symbols[i], -1,
								&cur_index))
		{
			mbs_chunk_delete(uniform_table);
			return NULL;
		}
	}
	
	return uniform_table;
}

typedef struct {
	uint8_t unknown_0; //=0x00
	uint8_t type;
	uint16_t component_count;
	uint16_t component_size;
	uint16_t array_entries;
	uint16_t stride;
	uint8_t  unknown_1; //=0x10, except 0x18 when used as texcoord
	uint8_t  precision;
	uint32_t invariant;
	uint16_t offset;
	uint16_t parent_index; //=0xFFFF
} mbs_varying_t;

static const unsigned varying_strides[lima_num_symbol_types] = {
	[lima_symbol_float] = 1,
	[lima_symbol_vec2] = 2,
	[lima_symbol_vec3] = 4,
	[lima_symbol_vec4] = 4,
	[lima_symbol_mat2] = 4,
	[lima_symbol_mat3] = 12,
	[lima_symbol_mat4] = 16
};

static const unsigned varying_sizes[lima_num_symbol_types] = {
	[lima_symbol_float] = 1,
	[lima_symbol_vec2] = 2,
	[lima_symbol_vec3] = 4,
	[lima_symbol_vec4] = 4,
	[lima_symbol_mat2] = 2,
	[lima_symbol_mat3] = 4,
	[lima_symbol_mat4] = 4
};

static mbs_chunk_t* mbs_varying_export(lima_symbol_t* symbol)
{
	mbs_chunk_t* chunk = mbs_chunk_create("VVAR");
	if (!chunk)
		return NULL;
	
	mbs_chunk_t* name = mbs_chunk_string(symbol->name);
	if (!name)
	{
		mbs_chunk_delete(chunk);
		return false;
	}
	
	if (!mbs_chunk_append(chunk, name))
	{
		mbs_chunk_delete(chunk);
		mbs_chunk_delete(name);
		return false;
	}
	
	mbs_varying_t varying = {
		.unknown_0 = 0,
		.type = types[symbol->type],
		.component_count = component_counts[symbol->type],
		.component_size = varying_sizes[symbol->type],
		.array_entries = symbol->array_elems,
		.stride = varying_strides[symbol->type],
		.unknown_1 = 0x10, //TODO handle this properly
		.precision = precisions[symbol->precision],
		.invariant = 0, //TODO
		.offset = symbol->used ? symbol->offset : 0xFFFF,
		.parent_index = 0xFFFF //varyings are never part of structures
	};
	
	if (!mbs_chunk_append_data(chunk, &varying, sizeof(mbs_varying_t)))
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	return chunk;
}

mbs_chunk_t* lima_export_varying_table(lima_shader_symbols_t* symbols)
{
	mbs_chunk_t* varying_table = mbs_chunk_create("SVAR");
	if (!varying_table)
		return NULL;
	
	uint32_t num_symbols = symbols->varying_table.num_symbols;
	if (!mbs_chunk_append_data(varying_table, &num_symbols, 4))
	{
		mbs_chunk_delete(varying_table);
		return NULL;
	}
	
	for (unsigned i = 0; i < symbols->varying_table.num_symbols; i++)
	{
		lima_symbol_t* varying = symbols->varying_table.symbols[i];
		mbs_chunk_t* chunk = mbs_varying_export(varying);
		if (!chunk || !mbs_chunk_append(varying_table, chunk))
		{
			mbs_chunk_delete(varying_table);
			mbs_chunk_delete(chunk);
			return NULL;
		}
	}
	
	return varying_table;
}

typedef struct __attribute__((packed)) {
	uint8_t unknown_0; //=0x00
	uint8_t type;
	uint16_t component_count;
	uint16_t component_size;
	uint16_t array_entries;
	uint16_t stride;
	uint8_t  unknown_1; //=0x10
	uint8_t  precision;
	uint16_t unknown_2; //=0x0000
	uint16_t offset;
} mbs_attribute_t;

static mbs_chunk_t* mbs_attribute_export(lima_symbol_t* symbol)
{
	mbs_chunk_t* chunk = mbs_chunk_create("VATT");
	if (!chunk)
		return NULL;
	
	mbs_chunk_t* name = mbs_chunk_string(symbol->name);
	if (!name)
	{
		mbs_chunk_delete(chunk);
		return false;
	}
	
	if (!mbs_chunk_append(chunk, name))
	{
		mbs_chunk_delete(chunk);
		mbs_chunk_delete(name);
		return false;
	}
	
	mbs_attribute_t attribute = {
		.unknown_0 = 0,
		.type = types[symbol->type],
		.component_count = component_counts[symbol->type],
		.component_size = 4,
		.array_entries = 0,
		.stride = symbol->stride,
		.unknown_1 = 0x10,
		.precision = precisions[symbol->precision],
		.unknown_2 = 0,
		.offset = symbol->offset,
	};
	
	if (!mbs_chunk_append_data(chunk, &attribute, sizeof(mbs_attribute_t)))
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	return chunk;
}

mbs_chunk_t* lima_export_attribute_table(lima_shader_symbols_t* symbols)
{
	mbs_chunk_t* attribute_table = mbs_chunk_create("SATT");
	if (!attribute_table)
		return NULL;
	
	uint32_t num_symbols = symbols->attribute_table.num_symbols;
	if (!mbs_chunk_append_data(attribute_table, &num_symbols, 4))
	{
		mbs_chunk_delete(attribute_table);
		return NULL;
	}
	
	for (unsigned i = 0; i < symbols->attribute_table.num_symbols; i++)
	{
		lima_symbol_t* attribute = symbols->attribute_table.symbols[i];
		mbs_chunk_t* chunk = mbs_attribute_export(attribute);
		if (!chunk || !mbs_chunk_append(attribute_table, chunk))
		{
			mbs_chunk_delete(attribute_table);
			mbs_chunk_delete(chunk);
			return NULL;
		}
	}
	
	return attribute_table;
}
