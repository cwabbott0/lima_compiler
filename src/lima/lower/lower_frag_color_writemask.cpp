/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
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

#include "lower/lower.h"
#include "ir_builder.h"
#include <string.h>

using namespace ir_builder;

namespace {
	
class ir_lower_writemask_visitor : public ir_hierarchical_visitor
{
public:
	ir_lower_writemask_visitor()
	{
		this->partial_write = true;
		for (unsigned i = 0; i < 4; i++)
			this->components[i] = NULL;
		this->frag_color = NULL;
	}
	
	virtual ir_visitor_status visit_enter(ir_assignment*);
	virtual ir_visitor_status visit_enter(ir_return*);
	virtual ir_visitor_status visit_leave(ir_function_signature*);
	virtual ir_visitor_status visit(ir_variable*);
	
private:
	//whether the most recent write to gl_FragColor was partial
	//(i.e. not all 4 components)
	bool partial_write;
	ir_rvalue* components[4];
	ir_variable* frag_color;
	void handle_return(ir_instruction* insert_after);
};
	
}; /* private namespace */

void lima_lower_frag_color_writemask(exec_list* ir)
{
	ir_lower_writemask_visitor v;
	v.run(ir);
}

ir_visitor_status ir_lower_writemask_visitor::visit_enter(ir_assignment* ir)
{
	ir_dereference_variable* deref_var = ir->lhs->as_dereference_variable();
	
	if (!deref_var)
		return visit_continue;
	
	ir_variable* var = deref_var->var;
	
	if (strcmp(var->name, "gl_FragColor") != 0)
		return visit_continue;
	
	if (ir->write_mask != 0xF)
	{
		assert(this->partial_write);
		
		void* mem_ctx = ralloc_parent(ir);
		
		unsigned num_components = 0;
		for (unsigned i = 0; i < 4; i++)
			if (ir->write_mask & (1 << i))
				num_components++;
		
		ir_variable* temp_var =
			new(mem_ctx) ir_variable(glsl_type::vec(num_components),
									 "wrmask_temp", ir_var_temporary_ssa);
		
		ir->lhs = new(mem_ctx) ir_dereference_variable(temp_var);
		
		num_components = 0;
		for (unsigned i = 0; i < 4; i++)
			if (ir->write_mask & (1 << i))
			{
				components[i] = swizzle_component(temp_var, num_components);
				num_components++;
			}
		
		ir->write_mask = (1 << num_components) - 1;
	}
	else
		this->partial_write = false;
	
	return visit_continue;
}

ir_visitor_status ir_lower_writemask_visitor::visit_enter(ir_return* ir)
{
	this->handle_return((ir_instruction*) ir->get_prev());
	return visit_continue;
}

ir_visitor_status ir_lower_writemask_visitor::visit_leave(ir_function_signature* ir)
{
	this->handle_return((ir_instruction*) ir->body.get_tail());
	return visit_continue;
}

ir_visitor_status ir_lower_writemask_visitor::visit(ir_variable* var)
{
	if (strcmp(var->name, "gl_FragColor") == 0)
		this->frag_color = var;
	
	return visit_continue;
}

void ir_lower_writemask_visitor::handle_return(ir_instruction* insert_after)
{
	if (!this->partial_write)
		return; //nothing to do here
	
	void* mem_ctx = ralloc_parent(insert_after);
	
	ir_rvalue* args[4];
	
	for (unsigned i = 0; i < 4; i++)
	{
		if (this->components[i])
			args[i] = this->components[i];
		else
			args[i] = new(mem_ctx) ir_constant(0.0f);
	}
	
	if (!this->frag_color)
	{
		this->frag_color = new(mem_ctx) ir_variable(glsl_type::vec(4),
													"gl_FragColor",
													ir_var_shader_out);
	}
	
	ir_expression* ir = new(mem_ctx) ir_expression(ir_quadop_vector,
												   this->frag_color->type,
												   args[0], args[1], args[2],
												   args[3]);
	
	insert_after->insert_after(assign(this->frag_color, ir));
}
