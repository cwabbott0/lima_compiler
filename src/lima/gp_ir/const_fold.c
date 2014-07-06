/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott
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

#include "gp_ir.h"
#include <math.h>

typedef float (*fold_op_cb)(float* args);

static float fold_mov(float* args)
{
	return args[0];
}

static float fold_mul(float* args)
{
	return args[0] * args[1];
}

static float fold_select(float* args)
{
	return (args[0] == 0.0f) ? args[2] : args[1];
}

static float fold_add(float* args)
{
	return args[0] + args[1];
}

static float fold_floor(float* args)
{
	return floor(args[0]);
}

static float fold_sign(float* args)
{
	if (args[0] == 0.0f)
		return 0.0f;
	if (args[0] > 0.0f)
		return 1.0f;
	return -1.0f;
}

static float fold_ge(float* args)
{
	return args[0] >= args[1] ? 1.0f : 0.0f;
}

static float fold_lt(float* args)
{
	return args[0] < args[1] ? 1.0f : 0.0f;
}

static float fold_min(float* args)
{
	return fminf(args[0], args[1]);
}

static float fold_max(float* args)
{
	return fmaxf(args[0], args[1]);
}

static float fold_neg(float* args)
{
	return -args[0];
}

static float fold_abs(float* args)
{
	return fabsf(args[0]);
}

static float fold_not(float* args)
{
	return 1.0f - args[0];
}

static float fold_div(float* args)
{
	return args[0] / args[1];
}

static float fold_mod(float* args)
{
	float temp = args[0] / args[1];
	return args[1] * (temp - floorf(temp));
}

static float fold_lrp(float* args)
{
	return args[1] * args[2] + args[0] * (1 - args[2]);
}

static float fold_exp2(float* args)
{
	return exp2f(args[0]);
}

static float fold_log2(float *args)
{
	return log2f(args[0]);
}

static float fold_rcp(float* args)
{
	return 1.0f / args[0];
}

static float fold_rsqrt(float* args)
{
	return 1.0f / sqrtf(args[0]);
}

static float fold_ceil(float* args)
{
	return ceilf(args[0]);
}

static float fold_fract(float* args)
{
	return args[0] - floorf(args[0]);
}

static float fold_exp(float* args)
{
	return expf(args[0]);
}

static float fold_log(float* args)
{
	return logf(args[0]);
}

static float fold_pow(float* args)
{
	return powf(args[0], args[1]);
}

static float fold_sqrt(float* args)
{
	return sqrtf(args[0]);
}

static float fold_sin(float* args)
{
	return sinf(args[0]);
}

static float fold_cos(float* args)
{
	return cosf(args[0]);
}

static float fold_tan(float* args)
{
	return tanf(args[0]);
}

static float fold_eq(float* args)
{
	return (args[0] == args[1]) ? 1.0f : 0.0f;
}

static float fold_ne(float* args)
{
	return (args[0] != args[1]) ? 1.0f : 0.0f;
}

static float fold_f2b(float* args)
{
	return (args[0] == 0.0) ? 1.0f : 0.0;
}

static float fold_f2i(float* args)
{
	return fold_sign(args) * floorf(fabsf(args[0]));
}

typedef struct
{
	fold_op_cb cb;
	lima_gp_ir_op_e op;
} fold_op_info_t;

static const fold_op_info_t fold_ops[] = {
	{ fold_mov,    lima_gp_ir_op_mov    },
	{ fold_mul,    lima_gp_ir_op_mul    },
	{ fold_select, lima_gp_ir_op_select },
	{ fold_add,    lima_gp_ir_op_add    },
	{ fold_floor,  lima_gp_ir_op_floor  },
	{ fold_sign,   lima_gp_ir_op_sign   },
	{ fold_ge,     lima_gp_ir_op_ge     },
	{ fold_lt,     lima_gp_ir_op_lt     },
	{ fold_min,    lima_gp_ir_op_min    },
	{ fold_max,    lima_gp_ir_op_max    },
	{ fold_neg,    lima_gp_ir_op_neg    },
	{ fold_abs,    lima_gp_ir_op_abs    },
	{ fold_not,    lima_gp_ir_op_not    },
	{ fold_div,    lima_gp_ir_op_div    },
	{ fold_mod,    lima_gp_ir_op_mod    },
	{ fold_lrp,    lima_gp_ir_op_lrp    },
	{ fold_exp2,   lima_gp_ir_op_exp2   },
	{ fold_log2,   lima_gp_ir_op_log2   },
	{ fold_rcp,    lima_gp_ir_op_rcp    },
	{ fold_rsqrt,  lima_gp_ir_op_rsqrt  },
	{ fold_ceil,   lima_gp_ir_op_ceil   },
	{ fold_fract,  lima_gp_ir_op_fract  },
	{ fold_exp,    lima_gp_ir_op_exp    },
	{ fold_log,    lima_gp_ir_op_log    },
	{ fold_pow,    lima_gp_ir_op_pow    },
	{ fold_sqrt,   lima_gp_ir_op_sqrt   },
	{ fold_sin,    lima_gp_ir_op_sin    },
	{ fold_cos,    lima_gp_ir_op_cos    },
	{ fold_tan,    lima_gp_ir_op_tan    },
	{ fold_eq,     lima_gp_ir_op_eq     },
	{ fold_ne,     lima_gp_ir_op_ne     },
	{ fold_f2b,    lima_gp_ir_op_f2b    },
	{ fold_f2i,    lima_gp_ir_op_f2i    },
};

#define NUM_FOLD_OPS sizeof(fold_ops)/sizeof(fold_op_info_t)

static fold_op_cb get_fold_op(lima_gp_ir_op_e op)
{
	unsigned i;
	for (i = 0; i < NUM_FOLD_OPS; i++)
	{
		if (fold_ops[i].op == op)
			return fold_ops[i].cb;
	}
	
	return NULL;
}

static bool fold_alu_node(lima_gp_ir_alu_node_t* node, float* result)
{
	float args[3];
	unsigned i;
	
	fold_op_cb fold_op = get_fold_op(node->node.op);
	
	if (!fold_op)
		return false;
	
	for (i = 0; i < lima_gp_ir_alu_node_num_children(node->node.op); i++)
	{
		lima_gp_ir_node_t* child = node->children[i];
		
		if (child->op == lima_gp_ir_op_const)
		{
			lima_gp_ir_const_node_t* const_node = gp_ir_node_to_const(child);
			args[i] = const_node->constant;
		}
		else
			return false;
		
		if (node->children_negate[i])
			args[i] = -args[i];
	}
	
	*result = fold_op(args);
	if (node->dest_negate)
		*result = -*result;
	
	return true;
}

static bool fold_clamp_const_node(lima_gp_ir_clamp_const_node_t* node,
								  float* result)
{
	if (!node->is_inline_const)
		return false;
	
	lima_gp_ir_node_t* child = node->child;
	float arg;
	
	if (child->op == lima_gp_ir_op_const)
	{
		lima_gp_ir_const_node_t* const_node = gp_ir_node_to_const(child);
		arg = const_node->constant;
	}
	else
		return false;
	
	if (arg > node->high)
		arg = node->high;
	if (arg < node->low)
		arg = node->low;
	
	*result = arg;
	return true;
}

static bool fold_node(lima_gp_ir_node_t* node, float* result)
{
	switch (lima_gp_ir_op[node->op].type)
	{
		case lima_gp_ir_node_type_alu:
			return fold_alu_node(gp_ir_node_to_alu(node), result);
			
		case lima_gp_ir_node_type_clamp_const:
			return fold_clamp_const_node(gp_ir_node_to_clamp_const(node), result);
			
		default:
			break;
	}
	
	return false;
}

static bool const_fold_cb(lima_gp_ir_node_t* node, void* state)
{
	(void) state;
	
	float constant;
	if (!fold_node(node, &constant))
		return true;
	
	lima_gp_ir_const_node_t* const_node = lima_gp_ir_const_node_create();
	if (!const_node)
		return false;
	
	const_node->constant = constant;
	
	if (!lima_gp_ir_node_replace(node, &const_node->node))
		return false;
	
	return true;
}

bool lima_gp_ir_const_fold_root_node(lima_gp_ir_root_node_t* node)
{
	return lima_gp_ir_node_dfs(&node->node, NULL, const_fold_cb, NULL);
}

bool lima_gp_ir_const_fold_block(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	
	gp_ir_block_for_each_node(block, node)
	{
		if (!lima_gp_ir_const_fold_root_node(node))
			return false;
	}
	
	return true;
}

bool lima_gp_ir_const_fold_prog(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	
	gp_ir_prog_for_each_block(prog, block)
	{
		if (!lima_gp_ir_const_fold_block(block))
			return false;
	}
	
	return true;
}
