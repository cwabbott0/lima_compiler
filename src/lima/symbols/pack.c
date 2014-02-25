/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2014 Connor Abbott
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
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

/*
 * Here, we implement the varying, attribute, and uniform packing rules.
 * Attributes aren't packed, and GP uniforms are packed based on the original
 * GLSL ES packing rules, but everything else is packed based on a simple
 * algorithm where we maintain a pointer to the current position, aligning it
 * and adding to it the size of the element.
 */

/*
 * Standard packing
 *
 * Implement the GLSL ES rules for the order to pack; we use this order for
 * symbols not part of a struct, even if we aren't using the rest of the GLSL
 * ES rules. We call the callback function to actually do the packing.
 */

typedef bool (*pack_cb)(lima_symbol_t* symbol, void* data);

//lower is a higher order, see section 7 of Appenidx A of the GLSL ES 1.0 spec
static const int type_pack_order[lima_num_symbol_types] = {
	[lima_symbol_sampler2d] = 0,
	[lima_symbol_sampler_cube] = 1,
	[lima_symbol_struct] = 2,
	[lima_symbol_mat4] = 3,
	[lima_symbol_mat2] = 4,
	[lima_symbol_vec4] = 5,
	[lima_symbol_ivec4] = 6,
	[lima_symbol_bvec4] = 7,
	[lima_symbol_mat3] = 8,
	[lima_symbol_vec3] = 9,
	[lima_symbol_ivec3] = 10,
	[lima_symbol_bvec3] = 11,
	[lima_symbol_vec2] = 12,
	[lima_symbol_ivec2] = 13,
	[lima_symbol_bvec2] = 14,
	[lima_symbol_float] = 15,
	[lima_symbol_int] = 16,
	[lima_symbol_bool] = 17,
};

static int pack_compare(const void* elem1, const void* elem2)
{
	const lima_symbol_t* symbol1 = *(lima_symbol_t**) elem1;
	const lima_symbol_t* symbol2 = *(lima_symbol_t**) elem2;
	
	if (symbol1->type != symbol2->type)
		return type_pack_order[symbol1->type] - type_pack_order[symbol2->type];
	
	if (symbol1->array_elems != symbol2->array_elems)
		return (int) symbol2->array_elems - (int) symbol1->array_elems;
	
	//lastly, sort by the name... this isn't necessary per the spec, but it
	//increases the chance that the fragment and vertex shader varyings will
	//match, and it's done by the binary compler.
	return strcmp(symbol1->name, symbol2->name);
}

static bool pack_table(lima_symbol_table_t* table, pack_cb pack, void* data)
{
	lima_symbol_t** symbols = malloc(table->num_symbols * sizeof(lima_symbol_t*));
	memcpy(symbols, table->symbols, table->num_symbols * sizeof(lima_symbol_t*));
	qsort(symbols, table->num_symbols, sizeof(lima_symbol_t*), pack_compare);
	
	for (unsigned i = 0; i < table->num_symbols; i++)
	{
		lima_symbol_t* symbol = symbols[i];
		if (!symbol->used)
			continue; //skip unused symbols
		
		if (!pack(symbol, data))
		{
			free(symbols);
			return false;
		}
	}
	
	free(symbols);
	return true;
}

/*
 * Standard packing
 *
 * Implements the GLSL ES 1.0 rules for packing varyings and uniforms. We only
 * use this for packing GP uniforms, though, since they are the only type of
 * symbols which can be accessed solely with alignment of 4.
 */

typedef struct {
	//for the standard algortihm, we can allocate variables from the lowest
	//and highest row, so we need the lower and upper bound of free space in
	//each column
	
	unsigned free_low[4], free_high[4];
} std_pack_state_t;

static const unsigned num_components[lima_num_symbol_types] = {
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
	
	[lima_symbol_sampler2d] = 1,
	[lima_symbol_sampler_cube] = 1,
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
};

#ifndef MAX2
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define MIN2(a, b) ((a) > (b) ? (b) : (a))
#endif

static bool pack_std(lima_symbol_t* symbol, void* data)
{
	std_pack_state_t* state = (std_pack_state_t*) data;
	
	if (symbol->type == lima_symbol_struct)
	{
		//align all the free space before to a vec4 so we have a consistent
		//offset for the structure
		unsigned offset = 0;
		for (unsigned i = 0; i < 4; i++)
			if (state->free_low[i] > offset)
				offset = state->free_low[i];
		
		for (unsigned i = 0; i < 4; i++)
		{
			if (state->free_high[i] < offset)
				return false;
			state->free_low[i] = offset;
		}
		
		//pack each of the members, in their original order (the binary compiler
		//does this...)
		for (unsigned i = 0; i < symbol->num_children; i++)
		{
			if (!pack_std(symbol->children[i], data))
				return false;
		}
		
		//align all the free space again
		unsigned end_offset = 0;
		for (unsigned i = 0; i < 4; i++)
			if (state->free_low[i] > end_offset)
				end_offset = state->free_low[i];
		
		unsigned size = end_offset - offset;
		
		unsigned extra_array_size =
			size * (symbol->array_elems ? symbol->array_elems - 1 : 0);
		
		for (unsigned i = 0; i < 4; i++)
		{
			if (state->free_high[i] < end_offset + extra_array_size)
				return false;
			state->free_low[i] = end_offset + extra_array_size;
		}
		
		symbol->offset = offset * 4;
		symbol->stride = size * 4;
		return true;
	}
	
	symbol->stride = 4 * num_rows[symbol->type];
	
	unsigned my_num_components = num_components[symbol->type];
	
	unsigned my_num_rows =
		num_rows[symbol->type] * (symbol->array_elems ? symbol->array_elems : 1);
	
	if (my_num_components != 1)
	{
		//try to align it to the 1st column
		unsigned low_pos = 0, high_pos = UINT_MAX;
		for (unsigned i = 0; i < my_num_components; i++)
		{
			if (state->free_low[i] > low_pos)
				low_pos = state->free_low[i];
			if (state->free_high[i] < high_pos)
				high_pos = state->free_high[i];
		}
		
		if (low_pos + my_num_rows <= high_pos)
		{
			symbol->offset = 4 * low_pos;
			for (unsigned i = 0; i < my_num_components; i++)
				state->free_low[i] = low_pos + my_num_rows;
			return true;
		}
		else
		{
			if (my_num_components != 2)
				return false;
			
			//for 2-component symbols, try to pack using "the highest
			//numbered row and the lowest numbered column where the variable
			//will fit."
			
			for (unsigned col = 0; col < 3; col++)
			{
				unsigned low_pos = MAX2(state->free_low[col],
										state->free_low[col + 1]);
				unsigned high_pos = MIN2(state->free_high[col],
										 state->free_high[col + 1]);
				
				if (high_pos - my_num_rows >= low_pos)
				{
					symbol->offset = 4 * (high_pos - my_num_rows) + col;
					state->free_high[col] = high_pos - my_num_rows;
					state->free_high[col + 1] = high_pos - my_num_rows;
					return true;
				}
			}
			
			return false;
		}
	}
	else
	{
		/*
		 * "1 component variables (i.e. floats and arrays of floats) have their
		 * own packing rule. They are packed in order of size, largest first.
		 * Each variable is placed in the column that leaves the least amount of
		 * space in the column and aligned to the lowest available rows within
		 * that column. During this phase of packing, space will be available in
		 * up to 4 columns. The space within each column is always contiguous."
		 *
		 * Basically, this amounts to finding the column with the least amount
		 * of space (free_high[i] - free_low[i]) while still containing enough
		 * to hold the array, and then putting the array in the low part of
		 * that column.
		 */
		
		int column = -1;
		unsigned space_left = UINT_MAX;
		for (unsigned i = 0; i < 4; i++)
		{
			unsigned new_space_left = state->free_high[i] - state->free_low[i];
			if (new_space_left < my_num_rows)
				continue;
			
			if (new_space_left < space_left)
			{
				space_left = new_space_left;
				column = i;
			}
		}
		
		if (column == -1)
			return false;
		
		symbol->offset = 4 * state->free_low[column] + column;
		state->free_low[column] += my_num_rows;
	}
	
	return true;
}

static bool pack_table_std(lima_symbol_table_t* table, unsigned num_vec4s)
{
	std_pack_state_t state;
	state.free_low[0] = state.free_low[1] = state.free_low[2]
		= state.free_low[3] = 0;
	state.free_high[0] = state.free_high[1] = state.free_high[2]
		= state.free_high[3] = num_vec4s;
	
	if (pack_table(table, pack_std, &state))
	{
		//determine total size
		
		for (unsigned i = 0; i < 4; i++)
			if (state.free_high[i] != num_vec4s)
			{
				table->total_size = num_vec4s;
				return true;
			}
		
		table->total_size = 0;
		for (unsigned i = 0; i < 4; i++)
			if (state.free_low[i] > table->total_size)
				table->total_size = state.free_low[i];
		
		return true;
	}
	
	return false;
}

/*
 * Alignment-based packing
 *
 * This is the algorithm for things that access members with a stride of 1, 2,
 * or 4 - basically any type of variable except for GP uniforms. We simply hold
 * one piece of state, the current position, and then to allocate a variable we
 * align the current position to the variable's alignment and then increment
 * the current position by the size of the variable.
 */

/* alignments for each type */

static const unsigned alignments[lima_num_symbol_types] = {
	[lima_symbol_float] = 1,
	[lima_symbol_int] = 1,
	[lima_symbol_bool] = 1,
	[lima_symbol_vec2] =  2,
	[lima_symbol_ivec2] = 2,
	[lima_symbol_bvec2] = 2,
	[lima_symbol_vec3]  = 4,
	[lima_symbol_ivec3] = 4,
	[lima_symbol_bvec3] = 4,
	[lima_symbol_vec4]  = 4,
	[lima_symbol_ivec4] = 4,
	[lima_symbol_bvec4] = 4,
	[lima_symbol_mat2] = 2,
	[lima_symbol_mat3] = 4,
	[lima_symbol_mat4] = 4,
	[lima_symbol_sampler2d] = 1,
	[lima_symbol_sampler_cube] = 1
};

/* total size for each type */

static const unsigned sizes[lima_num_symbol_types] = {
	[lima_symbol_float] = 1,
	[lima_symbol_int] = 1,
	[lima_symbol_bool] = 1,
	[lima_symbol_vec2] =  2,
	[lima_symbol_ivec2] = 2,
	[lima_symbol_bvec2] = 2,
	[lima_symbol_vec3]  = 4,
	[lima_symbol_ivec3] = 4,
	[lima_symbol_bvec3] = 4,
	[lima_symbol_vec4]  = 4,
	[lima_symbol_ivec4] = 4,
	[lima_symbol_bvec4] = 4,
	[lima_symbol_mat2] = 4,
	[lima_symbol_mat3] = 12,
	[lima_symbol_mat4] = 16,
	[lima_symbol_sampler2d] = 1,
	[lima_symbol_sampler_cube] = 1
};

static unsigned get_alignment(lima_symbol_t* symbol)
{
	if (symbol->type == lima_symbol_struct)
	{
		unsigned alignment = 1;
		for (unsigned i = 0; i < symbol->num_children; i++)
		{
			unsigned new_alignment = get_alignment(symbol->children[i]);
			if (new_alignment > alignment)
				alignment = new_alignment;
		}
		
		return alignment;
	}
	
	return alignments[symbol->type];
}

typedef struct {
	unsigned pos;
} align_pack_state_t;

#define ALIGN(n, align) (((n) + (align) - 1) - ((n) + (align) - 1) % (align))

static bool pack_align(lima_symbol_t* symbol, void* data)
{
	align_pack_state_t* state = (align_pack_state_t*) data;
	unsigned alignment = get_alignment(symbol);
	
	state->pos = ALIGN(state->pos, alignment);
	symbol->offset = state->pos;
	
	if (symbol->type == lima_symbol_struct)
	{
		unsigned old_pos = state->pos;
		state->pos = 0;
		
		for (unsigned i = 0; i < symbol->num_children; i++)
		{
			if (!pack_align(symbol->children[i], data))
				return false;
		}
		
		state->pos = ALIGN(state->pos, alignment);
		symbol->stride = state->pos;
		
		state->pos = old_pos;
	}
	else
		symbol->stride = sizes[symbol->type];
	
	unsigned array_elems = symbol->array_elems ? symbol->array_elems : 1;
	state->pos += symbol->stride * array_elems;
	
	return true;
}

static bool pack_table_align(lima_symbol_table_t* table, unsigned size)
{
	align_pack_state_t state;
	state.pos = 0;
	pack_table(table, pack_align, &state);
	table->total_size = state.pos;
	return table->total_size <= size;
}

/* 
 * Attribute packing
 *
 * According to the spec and the binary compiler, there isn't much to do here...
 * each element occupies a separate vec4, and there can be no arrays or
 * structures.
 */

typedef struct {
	unsigned pos;
} attr_pack_state_t;

static bool pack_attr(lima_symbol_t* symbol, void* data)
{
	attr_pack_state_t* state = (attr_pack_state_t*) data;
	
	assert(symbol->array_elems == 0);
	assert(symbol->type != lima_symbol_struct);
	
	unsigned my_num_rows = num_rows[symbol->type];
	symbol->offset = 4 * state->pos;
	symbol->stride = 4 * my_num_rows;
	state->pos += my_num_rows;
	return true;
}

static bool pack_table_attr(lima_symbol_table_t* table, unsigned num_vec4s)
{
	attr_pack_state_t state;
	state.pos = 0;
	pack_table(table, pack_attr, &state);
	table->total_size = 4 * state.pos;
	return state.pos <= num_vec4s;
}

bool lima_shader_symbols_pack(lima_shader_symbols_t* symbols,
							  lima_shader_stage_e stage)
{
	if (!pack_table_attr(&symbols->attribute_table, 16))
		return false;
	
	if (!pack_table_align(&symbols->varying_table, 64))
		return false;
	
	if (stage == lima_shader_stage_vertex)
	{
		if (!pack_table_std(&symbols->uniform_table, 304))
			return false;
	}
	else
	{
		if (!pack_table_align(&symbols->uniform_table, 65536))
			return false;
	}
	
	return true;
}
