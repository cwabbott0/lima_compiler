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

#include "symbols/symbols.h"
#define _GNU_SOURCE /* for asprintf on linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

lima_symbol_t* lima_symbol_create(lima_symbol_type_e type,
								  lima_symbol_precision_e precision,
								  const char* name,
								  unsigned array_elems)
{
	lima_symbol_t* symbol = malloc(sizeof(lima_symbol_t));
	if (!symbol)
		return NULL;
	
	symbol->type = type;
	symbol->precision = precision;
	symbol->name = strdup(name);
	if (!symbol->name)
	{
		free(symbol);
		return NULL;
	}
	symbol->array_elems = array_elems;
	symbol->offset = 0;
	symbol->stride = 0;
	symbol->used = true;
	symbol->array_const = NULL;
	symbol->num_children = 0;
	symbol->children = NULL;
	
	return symbol;
}

lima_symbol_t* lima_struct_create(const char* name, unsigned num_children,
								  lima_symbol_t** children,
								  unsigned array_elems)
{
	lima_symbol_t* symbol = malloc(sizeof(lima_symbol_t));
	if (!symbol)
		return NULL;
	
	symbol->type = lima_symbol_struct;
	symbol->precision = lima_precision_high; /* not important */
	symbol->name = strdup(name);
	if (!symbol->name)
		goto err_mem;
	symbol->array_elems = array_elems;
	symbol->offset = 0;
	symbol->stride = 0;
	symbol->used = true;
	symbol->array_const = NULL;
	symbol->num_children = num_children;
	
	symbol->children = malloc(sizeof(lima_symbol_t*) * num_children);
	if (!symbol->children)
		goto err_mem2;
	
	memcpy(symbol->children, children, num_children * sizeof(lima_symbol_t*));
	
	return symbol;
	
	err_mem2:
	
	free(symbol->name);
	
	err_mem:
	
	free(symbol);
	
	return NULL;
}

static unsigned const_size(lima_symbol_type_e type)
{
	switch (type)
	{
		case lima_symbol_float:
			return 1;
		case lima_symbol_vec2:
			return 2;
		case lima_symbol_vec3:
			return 3;
		case lima_symbol_vec4:
			return 4;
		case lima_symbol_mat2:
			return 4;
		case lima_symbol_mat3:
			return 9;
		case lima_symbol_mat4:
			return 16;
		default:
			assert(0);
	}
	
	return 0;
}

lima_symbol_t* lima_const_create(unsigned index, lima_symbol_type_e type,
								 unsigned array_elems, float* const_array)
{
	//this only works for arrays of vectors of floats, that's ok though since
	//this is only used internally by the GP backend
	assert(type < lima_last_vary_attr_type);
	
	lima_symbol_t* symbol = malloc(sizeof(lima_symbol_t));
	if (!symbol)
		return NULL;
	
	if (asprintf(&symbol->name, "?__maligp2_constant_%03u", index) == -1 ||
		symbol->name == NULL)
		goto err_mem;
	
	symbol->type = type;
	symbol->precision = lima_precision_high; /* not important */
	symbol->array_elems = array_elems;
	symbol->offset = 0;
	symbol->stride = 0;
	symbol->used = true;
	
	unsigned size = (array_elems ? array_elems : 1) * const_size(type);
	symbol->array_const = malloc(size * sizeof(float));
	if (!symbol->array_const)
		goto err_mem2;
	
	memcpy(symbol->array_const, const_array, size * sizeof(float));
	
	return symbol;
	
	err_mem2:
	
	free(symbol->name);
	
	err_mem:
	
	free(symbol);
	return NULL;
}

void lima_symbol_delete(lima_symbol_t* symbol)
{
	if (symbol->type == lima_symbol_struct)
	{
		for (unsigned i = 0; i < symbol->num_children; i++)
			lima_symbol_delete(symbol->children[i]);
		free(symbol->children);
	}
	
	if (symbol->array_const)
		free(symbol->array_const);
	
	free(symbol->name);
	free(symbol);
}

#define INITIAL_CAPACITY 4

bool lima_symbol_table_init(lima_symbol_table_t* table)
{
	table->num_symbols = 0;
	table->total_size = 0;
	table->symbol_capacity = INITIAL_CAPACITY;
	table->symbols = malloc(INITIAL_CAPACITY * sizeof(lima_symbol_t*));
	
	return !!table->symbols;
}

void lima_symbol_table_delete(lima_symbol_table_t* table)
{
	for (unsigned i = 0; i < table->num_symbols; i++)
		lima_symbol_delete(table->symbols[i]);
	free(table->symbols);
}

bool lima_symbol_table_add(lima_symbol_table_t* table, lima_symbol_t* symbol)
{
	table->num_symbols++;
	if (table->num_symbols > table->symbol_capacity)
	{
		table->symbol_capacity *= 2;
		table->symbols = realloc(table->symbols,
								 table->symbol_capacity * sizeof(lima_symbol_t*));
		if (!table->symbols)
			return false;
	}
	
	table->symbols[table->num_symbols - 1] = symbol;
	return true;
}

lima_symbol_t* lima_symbol_table_find(lima_symbol_table_t* table,
									  const char* name)
{
	for (unsigned i = 0; i < table->num_symbols; i++)
	{
		if (strcmp(name, table->symbols[i]->name) == 0)
			return table->symbols[i];
	}
	
	return NULL;
}

static bool key_equals(const void* a, const void* b)
{
	const float* f_a = (float*) a;
	const float* f_b = (float*) b;
	
	return f_a == f_b;
}

static inline uint32_t hash_float(float f)
{
	return _mesa_hash_data(&f, sizeof(float));
}

bool lima_shader_symbols_init(lima_shader_symbols_t* symbols)
{
	if (!lima_symbol_table_init(&symbols->attribute_table))
		return false;
	
	if (!lima_symbol_table_init(&symbols->varying_table))
		goto err_mem;
	
	if (!lima_symbol_table_init(&symbols->uniform_table))
		goto err_mem2;
	
	if (!lima_symbol_table_init(&symbols->temporary_table))
		goto err_mem3;
	
	symbols->constants = _mesa_hash_table_create(NULL, key_equals);
	if (!symbols->constants)
		goto err_mem4;
	
	symbols->cur_uniform_index = 0;
	symbols->cur_const_index = 0;
	return true;
	
	err_mem4:
	lima_symbol_table_delete(&symbols->temporary_table);
	
	err_mem3:
	lima_symbol_table_delete(&symbols->uniform_table);
	
	err_mem2:
	lima_symbol_table_delete(&symbols->varying_table);
	
	err_mem:
	lima_symbol_table_delete(&symbols->attribute_table);
	return false;
}

void lima_shader_symbols_delete(lima_shader_symbols_t* symbols)
{
	lima_symbol_table_delete(&symbols->varying_table);
	lima_symbol_table_delete(&symbols->attribute_table);
	lima_symbol_table_delete(&symbols->uniform_table);
	lima_symbol_table_delete(&symbols->temporary_table);
	_mesa_hash_table_destroy(symbols->constants, NULL);
}

bool lima_shader_symbols_add_varying(lima_shader_symbols_t* symbols,
									 lima_symbol_t* symbol)
{
	/* From page 32 of the GLSL ES 1.0 spec, section 4.3.5 "Varying":
	 *
	 * "The varying qualifier can be used only with the data types float, vec2,
	 * vec3, vec4, mat2, mat3, and mat4, or arrays of these. Structures cannot
	 * be varying."
	 */
	assert(symbol->type <= lima_last_vary_attr_type);
	
	return lima_symbol_table_add(&symbols->varying_table, symbol);
}

bool lima_shader_symbols_add_attribute(lima_shader_symbols_t* symbols,
									   lima_symbol_t* symbol)
{
	/* From page 30 of the GLSL ES 1.0 spec, section 4.3.3 "Attribute":
	 *
	 * "The attribute qualifier can be used only with the data types float, vec2,
	 * vec3, vec4, mat2, mat3, and mat4. Attribute variables cannot be declared
	 * as arrays or structures."
	 */
	assert(symbol->type <= lima_last_vary_attr_type);
	assert(symbol->array_elems == 0);
	
	return lima_symbol_table_add(&symbols->attribute_table, symbol);
}

bool lima_shader_symbols_add_uniform(lima_shader_symbols_t* symbols,
									 lima_symbol_t* symbol)
{
	/* From page 30 of the GLSL ES 1.0 spec, section 4.3.4 "Uniform":
	 *
	 * "The uniform qualifier can be used with any of the basic data types, or when
	 * declaring a variable whose type is a structure, or an array of any of these."
	 *
	 * This means we can have arrays of structures of arrays of...
	 */
	
	return lima_symbol_table_add(&symbols->uniform_table, symbol);
}

bool lima_shader_symbols_add_temporary(lima_shader_symbols_t* symbols,
									   lima_symbol_t* symbol)
{
	return lima_symbol_table_add(&symbols->temporary_table, symbol);
}

unsigned lima_shader_symbols_add_const(lima_shader_symbols_t* symbols,
									   float constant)
{
	struct hash_entry* entry = _mesa_hash_table_search(symbols->constants,
													   hash_float(constant),
													   &constant);
	
	if (entry)
	{
		lima_symbol_t* symbol = (lima_symbol_t*) entry->data;
		return symbol->offset;
	}
	
	lima_symbol_t* symbol = lima_const_create(symbols->cur_const_index,
											  lima_symbol_float,
											  0, &constant);
	
	symbols->cur_const_index++;
	
	if (!symbol)
		return 0;
	
	symbol->offset = symbols->cur_uniform_index++;
	symbol->stride = 4;
	
	lima_shader_symbols_add_uniform(symbols, symbol);
	
	_mesa_hash_table_insert(symbols->constants, hash_float(constant),
							symbol->array_const, symbol);
	
	return symbol->offset;
}

unsigned lima_shader_symbols_add_clamp_const(lima_shader_symbols_t* symbols,
											 float const1, float const2)
{
	float constants[2] = {const1, const2};
	
	lima_symbol_t* symbol = lima_const_create(symbols->cur_const_index,
											  lima_symbol_vec2,
											  0, constants);
	
	if (!symbol)
		return 0;
	
	symbols->cur_const_index++;
	
	//round cur_uniform_index to multiple of 4
	symbols->cur_uniform_index = (symbols->cur_uniform_index + 3) & ~3;
	
	symbol->offset = symbols->cur_uniform_index;
	symbols->cur_uniform_index += 2;
	
	symbol->stride = 4;
	
	lima_shader_symbols_add_uniform(symbols, symbol);
	return symbol->offset / 4;
}

static void print_tabs(unsigned tabs)
{
	for (unsigned i = 0; i < tabs; i++)
		printf("\t");
}

static const char* symbol_strings[lima_num_symbol_types] = {
	[lima_symbol_float]  = "float",
	[lima_symbol_vec2]   = "vec2",
	[lima_symbol_vec3]   = "vec3",
	[lima_symbol_vec4]   = "vec4",
	[lima_symbol_mat2]   = "mat2",
	[lima_symbol_mat3]   = "mat3",
	[lima_symbol_mat4]   = "mat4",
	[lima_symbol_int]    = "int",
	[lima_symbol_ivec2]  = "ivec2",
	[lima_symbol_ivec3]  = "ivec3",
	[lima_symbol_ivec4]  = "ivec4",
	[lima_symbol_bool]   = "bool",
	[lima_symbol_bvec2]  = "bvec2",
	[lima_symbol_bvec3]  = "bvec3",
	[lima_symbol_bvec4]  = "bvec4",
	[lima_symbol_sampler2d] = "sampler2D",
	[lima_symbol_sampler_cube] = "samplerCube",
	[lima_symbol_struct] = "struct"
};

static const char* precision_strings[3] = {
	[lima_precision_low] = "lowp",
	[lima_precision_high] = "highp",
	[lima_precision_medium] = "mediump"
};

static void print_symbol(lima_symbol_t* symbol, unsigned tabs)
{
	print_tabs(tabs);
	printf("%s %s ", precision_strings[symbol->precision],
		   symbol_strings[symbol->type]);
	if (symbol->type == lima_symbol_struct)
	{
		printf("{\n");
		for (unsigned i = 0; i < symbol->num_children; i++)
			print_symbol(symbol->children[i], tabs + 1);
		printf("} ");
	}
	
	printf("%s", symbol->name);
	if (symbol->array_elems)
		printf("[%u]", symbol->array_elems);
	printf("; //offset = %u, stride = %u", symbol->offset, symbol->stride);
	if (!symbol->used)
		printf(", unused");
	printf("\n");
}

static void print_table(lima_symbol_table_t* table, const char* prefix)
{
	for (unsigned i = 0; i < table->num_symbols; i++)
	{
		printf("%s ", prefix);
		print_symbol(table->symbols[i], 0);
	}
}

void lima_shader_symbols_print(lima_shader_symbols_t* symbols)
{
	print_table(&symbols->attribute_table, "attribute");
	print_table(&symbols->varying_table, "varying");
	print_table(&symbols->uniform_table, "uniform");
	print_table(&symbols->temporary_table, "");
}
