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

using namespace ir_builder;

namespace {

class ir_lower_scalar_arg_visitor : public ir_hierarchical_visitor
{
public:
	ir_lower_scalar_arg_visitor()
	{
	}
	
	virtual ir_visitor_status visit_leave(ir_expression*);
};
	
}; /* private namespace */

ir_visitor_status ir_lower_scalar_arg_visitor::visit_leave(ir_expression* ir)
{
	switch (ir->operation)
	{
		case ir_binop_add:
		case ir_binop_sub:
		case ir_binop_mul:
		case ir_binop_div:
		case ir_binop_mod:
		case ir_binop_min:
		case ir_binop_max:
		case ir_binop_pow:
			if (ir->operands[0]->type->is_scalar()
				&& ir->operands[1]->type->is_vector())
			{
				ir->operands[0] = swizzle(ir->operands[0], 0,
										  ir->operands[1]->type->vector_elements);
			}
			
			if (ir->operands[1]->type->is_scalar()
				&& ir->operands[0]->type->is_vector())
			{
				ir->operands[1] = swizzle(ir->operands[1], 0,
										  ir->operands[0]->type->vector_elements);
			}
			break;
			
		case ir_triop_lrp:
			if (ir->operands[2]->type == glsl_type::float_type
				&& ir->operands[0]->type->is_vector())
			{
				ir->operands[2] = swizzle(ir->operands[2], 0,
										  ir->operands[0]->type->vector_elements);
			}
			break;
			
		default:
			break;
	}
	
	return visit_continue;
}

void lima_lower_scalar_args(exec_list* ir)
{
	ir_lower_scalar_arg_visitor v;
	v.run(ir);
}
