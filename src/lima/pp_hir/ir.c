/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013
 *   Codethink (http://www.codethink.co.uk)
 *   Connor Abbott
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



#include "../pp_lir/pp_lir.h"
#include "phi_elim.h"
#include "xform.h"
#include "cfold.h"
#include <stdlib.h>



const lima_pp_hir_op_t lima_pp_hir_op[] =
{
	{
		.name = "mov",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},

	{
		.name = "neg",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "add",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "sub",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	
	{
		.name = "ddx",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "ddy",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},

	{
		.name = "mul",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "rcp",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "div",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},

	{
		.name = "sin_lut",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "cos_lut",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},

	{
		.name = "sum3",
		.args = 1,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "sum4",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	
	{
		.name = "normalize2",
		.args = 1,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "normalize3",
		.args = 1,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 3,
		.output_modifiers = false,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "normalize4",
		.args = 1,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	
	{
		.name = "select",
		.args = 3,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 1},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, true}
	},

	{
		.name = "sin",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "cos",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "tan",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "asin",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "acos",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	
	{
		.name = "atan",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "atan2",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "atan_pt1",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 3,
		.output_modifiers = false,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "atan2_pt1",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 3,
		.output_modifiers = false,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "atan_pt2",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},

	{
		.name = "pow",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "exp",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "log",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "exp2",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "log2",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "sqrt",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "rsqrt",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},

	{
		.name = "abs",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "sign",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "floor",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "ceil",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "fract",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "mod",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "min",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "max",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	
	{
		.name = "dot2",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 2, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "dot3",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 3, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "dot4",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 4, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	
	{
		.name = "lrp",
		.args = 3,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 1,
		.output_modifiers = true,
		.input_modifiers = {true, true, true}
	},

	{
		.name = "gt",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "ge",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "eq",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "ne",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, true, false}
	},
	{
		.name = "any2",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "any3",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "any4",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "all2",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "all3",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "all4",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "all_eq2",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 2, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "all_eq3",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 3, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "all_eq4",
		.args = 2,
		.commutative = true,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 4, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "any_ne2",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 2, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "any_ne3",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 3, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "any_ne4",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 4, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	{
		.name = "not",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = true,
		.input_modifiers = {true, false, false}
	},
	
	{
		.name = "phi",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	
	{
		.name = "combine",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	
	{
		.name = "loadu_1",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadu_1_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadu_2",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadu_2_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadu_4",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadu_4_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_1",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_1_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_2",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_2_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_3",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 3,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_3_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 3,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_4",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadv_4_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadt_1",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadt_1_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadt_2",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadt_2_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadt_4",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "loadt_4_off",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = true,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "storet_1",
		.args = 1,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "storet_1_off",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "storet_2",
		.args = 1,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {2, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "storet_2_off",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {2, 1, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "storet_4",
		.args = 1,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {4, 0, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "storet_4_off",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {4, 1, 0},
		.is_horizantal = false,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	
	{
		.name = "frag_coord",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "frag_coord_impl",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "point_coord",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "point_coord_impl",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 2,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "front_facing",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	
	{
		.name = "fb_color",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "fb_depth",
		.args = 0,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = false,
		.dest_size = 1,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	
	{
		.name = "texld_2d",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_off",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_lod",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_off_lod",
		.args = 3,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {2, 1, 1},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_z",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_z_off",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_z_lod",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_z_off_lod",
		.args = 3,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 1, 1},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_w",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_w_off",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_w_lod",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_2d_proj_w_off_lod",
		.args = 3,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {4, 1, 1},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_cube",
		.args = 1,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 0, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_cube_off",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_cube_lod",
		.args = 2,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 1, 0},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "texld_cube_off_lod",
		.args = 3,
		.commutative = false,
		.has_dest = true,
		.dest_beginning = false,
		.arg_sizes = {3, 1, 1},
		.is_horizantal = true,
		.dest_size = 4,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	
	{
		.name = "branch",
		.args = 0,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {0, 0, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "branch_gt",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "branch_eq",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "branch_ge",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "branch_lt",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "branch_ne",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	},
	{
		.name = "branch_le",
		.args = 2,
		.commutative = false,
		.has_dest = false,
		.dest_beginning = false,
		.arg_sizes = {1, 1, 0},
		.is_horizantal = true,
		.dest_size = 0,
		.output_modifiers = false,
		.input_modifiers = {false, false, false}
	}
};

bool lima_pp_hir_op_is_texld(lima_pp_hir_op_e op)
{
	return 	op == lima_pp_hir_op_texld_2d
	|| op == lima_pp_hir_op_texld_2d_off
	|| op == lima_pp_hir_op_texld_2d_lod
	|| op == lima_pp_hir_op_texld_2d_off_lod
	|| op == lima_pp_hir_op_texld_2d_proj_z
	|| op == lima_pp_hir_op_texld_2d_proj_z_off
	|| op == lima_pp_hir_op_texld_2d_proj_z_lod
	|| op == lima_pp_hir_op_texld_2d_proj_z_off_lod
	|| op == lima_pp_hir_op_texld_2d_proj_w
	|| op == lima_pp_hir_op_texld_2d_proj_w_off
	|| op == lima_pp_hir_op_texld_2d_proj_w_lod
	|| op == lima_pp_hir_op_texld_2d_proj_w_off_lod
	|| op == lima_pp_hir_op_texld_cube;

}

bool lima_pp_hir_op_is_load(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_loadu_one
	|| op == lima_pp_hir_op_loadu_one_off
	|| op == lima_pp_hir_op_loadu_two
	|| op == lima_pp_hir_op_loadu_two_off
	|| op == lima_pp_hir_op_loadu_four
	|| op == lima_pp_hir_op_loadu_four_off
	|| op == lima_pp_hir_op_loadv_one
	|| op == lima_pp_hir_op_loadv_one_off
	|| op == lima_pp_hir_op_loadv_two
	|| op == lima_pp_hir_op_loadv_two_off
	|| op == lima_pp_hir_op_loadv_three
	|| op == lima_pp_hir_op_loadv_three_off
	|| op == lima_pp_hir_op_loadv_four
	|| op == lima_pp_hir_op_loadv_four_off
	|| op == lima_pp_hir_op_loadt_one
	|| op == lima_pp_hir_op_loadt_one_off
	|| op == lima_pp_hir_op_loadt_two
	|| op == lima_pp_hir_op_loadt_two_off
	|| op == lima_pp_hir_op_loadt_four
	|| op == lima_pp_hir_op_loadt_four_off
	|| lima_pp_hir_op_is_texld(op);
}

bool lima_pp_hir_op_is_store(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_storet_one
	|| op == lima_pp_hir_op_storet_one_off
	|| op == lima_pp_hir_op_storet_two
	|| op == lima_pp_hir_op_storet_two_off
	|| op == lima_pp_hir_op_storet_four
	|| op == lima_pp_hir_op_storet_four_off;
}

bool lima_pp_hir_op_is_load_store(lima_pp_hir_op_e op)
{
	return lima_pp_hir_op_is_load(op) || lima_pp_hir_op_is_store(op);
}

bool lima_pp_hir_op_is_branch(lima_pp_hir_op_e op)
{
	return op == lima_pp_hir_op_branch
	|| op == lima_pp_hir_op_branch_gt
	|| op == lima_pp_hir_op_branch_eq
	|| op == lima_pp_hir_op_branch_ge
	|| op == lima_pp_hir_op_branch_lt
	|| op == lima_pp_hir_op_branch_ne
	|| op == lima_pp_hir_op_branch_le;
}

/*static ogt_ir_program_t* ogt_lower_to_lir(ogt_ir_t* dest_ir, ogt_ir_program_t* source)
{
	if (dest_ir != ogt_ir_lima_pp_lir)
		return NULL;
	
	ogt_ir_program_t* dest = calloc(sizeof(ogt_ir_program_t), 1);
	if (!dest)
		return NULL;
	
	dest->symbol_table = essl_program_create();
	unsigned i;
	for (i = 0; i < source->symbol_table->symbol_count; i++)
	{
		essl_symbol_t* new_sym = essl_symbol_copy(source->symbol_table->symbol[i]);
		if (!new_sym || !essl_program_add_symbol(dest->symbol_table, new_sym))
		{
			essl_program_delete(dest->symbol_table);
			free(dest);
			return NULL;
		}
	}
	
	if (!lima_pp_hir_calc_dominance(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	if (!lima_pp_hir_temp_to_reg(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	if (!lima_pp_hir_compress_temp_arrays(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	if (!lima_pp_hir_dead_code_eliminate(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	lima_pp_hir_propagate_copies(source->prog);
	
	lima_pp_hir_prog_cfold(source->prog);
	
	unsigned c;
	do
	{
		c = lima_pp_hir_prog_xform(source->prog);
	} while (c != 0);
	
	lima_pp_hir_prog_print(source->prog);
	
	if (!lima_pp_hir_split_crit_edges(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	if (!lima_pp_hir_convert_to_cssa(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	if (!lima_pp_hir_prog_reorder(source->prog))
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	lima_pp_hir_prog_print(source->prog);
	
	dest->prog = lima_pp_lir_convert(source->prog);
	if (!dest->prog)
	{
		essl_program_delete(dest->symbol_table);
		free(dest);
		return NULL;
	}
	
	dest->ir = ogt_ir_lima_pp_lir;
	
	return dest;
}*/
