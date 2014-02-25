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

#ifndef __SYMBOLS_H__
#define __SYMBOLS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "shader.h"
#include "mbs/mbs.h"

/* symbol types */

typedef enum {
	lima_symbol_float,
	lima_symbol_vec2,
	lima_symbol_vec3,
	lima_symbol_vec4,
	lima_symbol_mat2,
	lima_symbol_mat3,
	lima_symbol_mat4,
	//sentinel representing the last symbol type legal for varyings and attributes
	lima_last_vary_attr_type = lima_symbol_mat4,
	
	//All these symbol types are legal for uniforms only
	lima_symbol_int,
	lima_symbol_ivec2,
	lima_symbol_ivec3,
	lima_symbol_ivec4,
	lima_symbol_bool,
	lima_symbol_bvec2,
	lima_symbol_bvec3,
	lima_symbol_bvec4,
	lima_symbol_sampler2d,
	lima_symbol_sampler_cube,
	lima_symbol_struct,
	lima_num_symbol_types
} lima_symbol_type_e;

/* symbol precision */

typedef enum {
	lima_precision_low,
	lima_precision_medium,
	lima_precision_high,
	lima_num_precisions
} lima_symbol_precision_e;

typedef struct lima_symbol_s {
	lima_symbol_type_e type;
	lima_symbol_precision_e precision;
	char* name;
	unsigned array_elems; /* 0 = no array, 1 = array of 1 element */
	
	/* values output by the packing algorithm */
	unsigned offset; /* in terms of one float */
	unsigned stride; /* in terms of one float */
	
	bool used; /* unused varyings don't need to occupy space */
	
	/* for uniforms only, specifies a value to initialize it to by the driver */
	float* array_const;
	
	/* for structures only */
	unsigned num_children;
	struct lima_symbol_s** children;
} lima_symbol_t;

lima_symbol_t* lima_symbol_create(lima_symbol_type_e type,
								  lima_symbol_precision_e precision,
								  const char* name,
								  unsigned array_elems);

lima_symbol_t* lima_struct_create(const char* name, unsigned num_children,
								  lima_symbol_t** children,
								  unsigned array_elems);

lima_symbol_t* lima_const_create(unsigned index, lima_symbol_type_e type,
								 unsigned array_elems, float* const_array);

void lima_symbol_delete(lima_symbol_t* symbol);

typedef struct {
	unsigned num_symbols, symbol_capacity;
	lima_symbol_t** symbols;
	
	unsigned total_size;
} lima_symbol_table_t;

bool lima_symbol_table_init(lima_symbol_table_t* table);

void lima_symbol_table_delete(lima_symbol_table_t* table);

bool lima_symbol_table_add(lima_symbol_table_t* table, lima_symbol_t* symbol);

lima_symbol_t* lima_symbol_table_find(lima_symbol_table_t* table,
									  const char* name);

typedef struct lima_shader_symbols_s {
	lima_symbol_table_t attribute_table, varying_table, uniform_table;
	unsigned cur_uniform_index, cur_const_index; /* for inserting constants */
} lima_shader_symbols_t;

bool lima_shader_symbols_init(lima_shader_symbols_t* symbols);

void lima_shader_symbols_delete(lima_shader_symbols_t* symbols);

bool lima_shader_symbols_add_varying(lima_shader_symbols_t* symbols,
									 lima_symbol_t* symbol);

bool lima_shader_symbols_add_attribute(lima_shader_symbols_t* symbols,
									   lima_symbol_t* symbol);

bool lima_shader_symbols_add_uniform(lima_shader_symbols_t* symbols,
									 lima_symbol_t* symbol);

/* convenience method for inserting constants in the GP backend - returns the
 * index of the created constant
 */

unsigned lima_shader_symbols_add_const(lima_shader_symbols_t* symbols,
									   lima_symbol_t* symbol);

void lima_shader_symbols_print(lima_shader_symbols_t* symbols);

bool lima_shader_symbols_pack(lima_shader_symbols_t* symbols,
							  lima_shader_stage_e stage);

mbs_chunk_t* lima_export_varying_table(lima_shader_symbols_t* symbols);
mbs_chunk_t* lima_export_attribute_table(lima_shader_symbols_t* symbols);
mbs_chunk_t* lima_export_uniform_table(lima_shader_symbols_t* symbols);

#ifdef __cplusplus
}
#endif

#endif /* __SYMBOLS_H__ */

