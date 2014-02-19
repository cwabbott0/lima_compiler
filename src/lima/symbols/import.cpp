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

#include "ir.h"
#include "symbols/symbols.h"
#include "shader/shader_internal.h"

/* import varying/attribute/uniform symbols from GLSL IR into lima */

/*
 * Note that vertex shaders must include all varyings in the table, even
 * unused ones, so we make a second pass over the original (un-linked) shader
 * where we find varyings that were optimized out, marking them as unused so
 * that varying packing doesn't allocate space for them.
 */

namespace {

class symbol_convert_visitor : public ir_hierarchical_visitor
{
public:
	symbol_convert_visitor(lima_shader_symbols_t* symbols,
						   lima_shader_stage_e stage)
		: symbols(symbols), stage(stage), unused(false)
	{
	}
	
	ir_visitor_status visit(ir_variable* ir);
	
	lima_shader_symbols_t* symbols;
	lima_shader_stage_e stage;
	bool unused;
};

}; /* end private namespace */

/* find the symbol type for a given glsl type */
static lima_symbol_type_e convert_type(const struct glsl_type* type)
{
	switch (type->base_type) {
		case GLSL_TYPE_SAMPLER:
			switch (type->sampler_dimensionality)
		{
			case GLSL_SAMPLER_DIM_2D:
				return lima_symbol_sampler2d;
			case GLSL_SAMPLER_DIM_CUBE:
				return lima_symbol_sampler_cube;
			default:
				assert(0);
		}
			
		case GLSL_TYPE_FLOAT:
			switch (type->matrix_columns)
		{
			case 2:
				assert(type->vector_elements == 2);
				return lima_symbol_mat2;
			case 3:
				assert(type->vector_elements == 3);
				return lima_symbol_mat3;
			case 4:
				assert(type->vector_elements == 4);
				return lima_symbol_mat4;
			case 1:
				switch (type->vector_elements)
			{
				case 1:
					return lima_symbol_float;
				case 2:
					return lima_symbol_vec2;
				case 3:
					return lima_symbol_vec3;
				case 4:
					return lima_symbol_vec4;
				default:
					assert(0);
			}
				
			default:
				assert(0);
		}
			
		case GLSL_TYPE_INT:
			switch (type->vector_elements)
		{
			case 1:
				return lima_symbol_int;
			case 2:
				return lima_symbol_ivec2;
			case 3:
				return lima_symbol_ivec3;
			case 4:
				return lima_symbol_ivec4;
			default:
				assert(0);
		}
			
		case GLSL_TYPE_BOOL:
			switch (type->vector_elements)
		{
			case 1:
				return lima_symbol_bool;
			case 2:
				return lima_symbol_bvec2;
			case 3:
				return lima_symbol_bvec3;
			case 4:
				return lima_symbol_bvec4;
			default:
				assert(0);
		}
			
		default:
			assert(0);
	}
	
	return lima_symbol_float;
}

/* given a glsl_type, produce a symbol with the given name and array size */
static lima_symbol_t* convert_symbol(const struct glsl_type* type,
									 const char* name, unsigned array_size)
{
	if (type->base_type == GLSL_TYPE_ARRAY)
	{
		//We don't support arrays of arrays
		assert(array_size == 0);
		return convert_symbol(type->fields.array, name, type->length);
	}
	else if (type->base_type == GLSL_TYPE_STRUCT)
	{
		lima_symbol_t** children =
			(lima_symbol_t**) malloc(type->length * sizeof(lima_symbol_t*));
		if (!children)
			return NULL;
		
		for (unsigned i = 0; i < type->length; i++)
		{
			children[i] = convert_symbol(type->fields.structure[i].type,
										 type->fields.structure[i].name,
										 0);
			if (!children[i])
			{
				free(children);
				return NULL;
			}
		}
		
		return lima_struct_create(name, type->length, children, array_size);
	}
	
	//We have a base type, just convert it over
	return lima_symbol_create(convert_type(type),
							  lima_precision_high, /* TODO support precision in mesa */
							  name, array_size);
}

ir_visitor_status symbol_convert_visitor::visit(ir_variable* ir)
{
	if (ir->data.mode != ir_var_shader_in &&
		ir->data.mode != ir_var_shader_out &&
		ir->data.mode != ir_var_uniform)
		return visit_continue;
	
	if (this->stage == lima_shader_stage_fragment &&
		ir->data.mode == ir_var_shader_out)
		return visit_continue;
	
	if (this->unused && (ir->data.mode != ir_var_shader_out ||
		lima_symbol_table_find(&this->symbols->varying_table, ir->name)))
		return visit_continue;
	
	lima_symbol_t* symbol = convert_symbol(ir->type, ir->name, 0);
	symbol->used = !this->unused;
	
	switch (ir->data.mode)
	{
		case ir_var_shader_in:
			if (this->stage == lima_shader_stage_fragment)
				lima_shader_symbols_add_varying(this->symbols, symbol);
			else
				lima_shader_symbols_add_attribute(this->symbols, symbol);
			break;
			
		case ir_var_shader_out:
			lima_shader_symbols_add_varying(this->symbols, symbol);
			break;
			
		case ir_var_uniform:
			lima_shader_symbols_add_uniform(this->symbols, symbol);
			break;
			
		default:
			assert(0);
	}
	
	return visit_continue;
}


void lima_convert_symbols(lima_shader_t* shader)
{
	symbol_convert_visitor v(&shader->symbols, shader->stage);
	v.run(shader->linked_shader->ir);
	if (shader->stage == lima_shader_stage_vertex)
	{
		v.unused = true;
		v.run(shader->shader->ir);
	}
}

