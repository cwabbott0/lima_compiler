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

typedef lima_gp_ir_node_t* (*lower_cb)(lima_gp_ir_node_t* orig);

//abs(x) = max(x, -x)
static lima_gp_ir_node_t* lower_abs(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_max);
	
	if (!node)
		return NULL;
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	node->children[0] = node->children[1] = orig_alu->children[0];
	node->children_negate[1] = true;
	lima_gp_ir_node_link(&node->node, node->children[0]);
	
	return &node->node;
}

//not(x) = 1.0 - x
static lima_gp_ir_node_t* lower_not(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_max);
	
	if (!node)
		return NULL;
	
	lima_gp_ir_const_node_t* const_one = lima_gp_ir_const_node_create();
	
	if (!const_one)
	{
		lima_gp_ir_node_delete(&node->node);
		return NULL;
	}
	
	const_one->constant = 1.0f;
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	node->children[0] = &const_one->node;
	node->children[1] = orig_alu->children[0];
	node->children_negate[1] = true;
	lima_gp_ir_node_link(&node->node, &const_one->node);
	lima_gp_ir_node_link(&node->node, orig_alu->children[0]);
	
	return &node->node;
}

//x / y = x * (1 / y)
static lima_gp_ir_node_t* lower_div(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* mul =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	
	if (!mul)
		return NULL;
	
	lima_gp_ir_alu_node_t* rcp =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_rcp);
	
	if (!rcp)
	{
		lima_gp_ir_node_delete(&mul->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	rcp->children[0] = orig_alu->children[1];
	lima_gp_ir_node_link(&rcp->node, orig_alu->children[1]);
	
	mul->children[0] = orig_alu->children[0];
	mul->children[1] = &rcp->node;
	lima_gp_ir_node_link(&mul->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&mul->node, &rcp->node);
	
	return &mul->node;
}

//mod(x, y) = y * fract(x/y)
static lima_gp_ir_node_t* lower_mod(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* div =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_div);
	
	if (!div)
		return NULL;
	
	lima_gp_ir_alu_node_t* fract =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_fract);
	
	if (!fract)
	{
		lima_gp_ir_node_delete(&div->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* mul =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	
	if (!mul)
	{
		lima_gp_ir_node_delete(&div->node);
		lima_gp_ir_node_delete(&fract->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	div->children[0] = orig_alu->children[0];
	div->children[1] = orig_alu->children[1];
	lima_gp_ir_node_link(&div->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&div->node, orig_alu->children[1]);
	
	fract->children[0] = &div->node;
	lima_gp_ir_node_link(&fract->node, &div->node);
	
	mul->children[0] = &fract->node;
	mul->children[1] = orig_alu->children[1];
	lima_gp_ir_node_link(&mul->node, &fract->node);
	lima_gp_ir_node_link(&mul->node, orig_alu->children[1]);
	
	return &mul->node;
}

//lrp(x, y, t) = y * t + x * (1 - t)
static lima_gp_ir_node_t* lower_lrp(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* mul1 =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	
	if (!mul1)
		return NULL;
	
	lima_gp_ir_alu_node_t* mul2 =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	
	if (!mul2)
	{
		lima_gp_ir_node_delete(&mul1->node);
		return NULL;
	}
	
	lima_gp_ir_const_node_t* one = lima_gp_ir_const_node_create();
	
	if (!one)
	{
		lima_gp_ir_node_delete(&mul1->node);
		lima_gp_ir_node_delete(&mul2->node);
		return NULL;
	}
	
	one->constant = 1.0f;
	
	lima_gp_ir_alu_node_t* sub =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	
	if (!sub)
	{
		lima_gp_ir_node_delete(&mul1->node);
		lima_gp_ir_node_delete(&mul2->node);
		lima_gp_ir_node_delete(&one->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* add =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	
	if (!add)
	{
		lima_gp_ir_node_delete(&mul1->node);
		lima_gp_ir_node_delete(&mul2->node);
		lima_gp_ir_node_delete(&one->node);
		lima_gp_ir_node_delete(&sub->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	mul1->children[0] = orig_alu->children[1];
	mul1->children[1] = orig_alu->children[2];
	lima_gp_ir_node_link(&mul1->node, orig_alu->children[1]);
	lima_gp_ir_node_link(&mul1->node, orig_alu->children[2]);
	
	sub->children[0] = &one->node;
	sub->children[1] = orig_alu->children[2];
	sub->children_negate[1] = true;
	lima_gp_ir_node_link(&sub->node, &one->node);
	lima_gp_ir_node_link(&sub->node, orig_alu->children[2]);
	
	mul2->children[0] = orig_alu->children[0];
	mul2->children[1] = &sub->node;
	lima_gp_ir_node_link(&mul2->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&mul2->node, &sub->node);
	
	add->children[0] = &mul1->node;
	add->children[1] = &mul2->node;
	lima_gp_ir_node_link(&add->node, &mul1->node);
	lima_gp_ir_node_link(&add->node, &mul2->node);
	
	return &add->node;
}

static lima_gp_ir_node_t* lower_complex(lima_gp_ir_node_t* child,
										lima_gp_ir_op_e impl_op)
{
	lima_gp_ir_alu_node_t* complex2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_complex2);
	if (!complex2_node)
		return NULL;
	
	complex2_node->children[0] = child;
	lima_gp_ir_node_link(&complex2_node->node, child);
	
	lima_gp_ir_alu_node_t* impl_node =
		lima_gp_ir_alu_node_create(impl_op);
	if (!impl_node)
	{
		lima_gp_ir_node_delete(&complex2_node->node);
		return NULL;
	}
	
	impl_node->children[0] = child;
	lima_gp_ir_node_link(&impl_node->node, child);
	
	lima_gp_ir_alu_node_t* complex1_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_complex1);
	if (!complex1_node)
	{
		lima_gp_ir_node_delete(&complex2_node->node);
		lima_gp_ir_node_delete(&impl_node->node);
		return NULL;
	}
	
	complex1_node->children[0] = &impl_node->node;
	complex1_node->children[1] = &complex2_node->node;
	complex1_node->children[2] = child;
	lima_gp_ir_node_link(&complex1_node->node, &impl_node->node);
	lima_gp_ir_node_link(&complex1_node->node, &complex2_node->node);
	lima_gp_ir_node_link(&complex1_node->node, child);
	
	return &complex1_node->node;
}

static lima_gp_ir_node_t* lower_exp2(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_alu_node_t* preexp2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_preexp2);
	if (!preexp2_node)
		return NULL;
	
	preexp2_node->children[0] = child;
	lima_gp_ir_node_link(&preexp2_node->node, child);
	
	lima_gp_ir_node_t* ret = lower_complex(&preexp2_node->node,
										   lima_gp_ir_op_exp2_impl);
	if (!ret)
	{
		lima_gp_ir_node_delete(&preexp2_node->node);
		return NULL;
	}
	
	return ret;
}

static lima_gp_ir_node_t* lower_log2(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_alu_node_t* postlog2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_postlog2);
	if (!postlog2_node)
		return NULL;
	
	lima_gp_ir_node_t* ret = lower_complex(child, lima_gp_ir_op_log2_impl);
	
	if (!ret)
	{
		lima_gp_ir_node_delete(&postlog2_node->node);
		return NULL;
	}
	
	postlog2_node->children[0] = ret;
	lima_gp_ir_node_link(&postlog2_node->node, ret);
	
	return &postlog2_node->node;
}

static lima_gp_ir_node_t* lower_rcp(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	return lower_complex(child, lima_gp_ir_op_rcp_impl);
}

static lima_gp_ir_node_t* lower_rsqrt(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	return lower_complex(child, lima_gp_ir_op_rsqrt_impl);
}


//ceil(x) = -floor(-x)
static lima_gp_ir_node_t* lower_ceil(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_alu_node_t* floor_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_floor);
	if (!floor_node)
		return NULL;
	
	lima_gp_ir_alu_node_t* neg_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_neg);
	if (!neg_node)
	{
		lima_gp_ir_node_delete(&floor_node->node);
		return NULL;
	}
	
	floor_node->children[0] = child;
	floor_node->children_negate[0] = !orig_alu->children_negate[0];
	lima_gp_ir_node_link(&floor_node->node, child);
	
	neg_node->children[0] = &floor_node->node;
	lima_gp_ir_node_link(&neg_node->node, &floor_node->node);
	
	return &neg_node->node;
}

//fract(x) = x - floor(x)
static lima_gp_ir_node_t* lower_fract(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_alu_node_t* floor_node =
	lima_gp_ir_alu_node_create(lima_gp_ir_op_floor);
	if (!floor_node)
		return NULL;
	
	lima_gp_ir_alu_node_t* sub_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!sub_node)
	{
		lima_gp_ir_node_delete(&floor_node->node);
		return NULL;
	}
	
	floor_node->children[0] = child;
	floor_node->children_negate[0] = orig_alu->children_negate[0];
	lima_gp_ir_node_link(&floor_node->node, child);
	
	sub_node->children[0] = child;
	sub_node->children_negate[0] = orig_alu->children_negate[0];
	sub_node->children[1] = &floor_node->node;
	sub_node->children_negate[1] = true;
	lima_gp_ir_node_link(&sub_node->node, child);
	lima_gp_ir_node_link(&sub_node->node, &floor_node->node);
	
	return &sub_node->node;
}

//exp(x) = exp2(log2(e)*x)
static lima_gp_ir_node_t* lower_exp(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_const_node_t* log2e = lima_gp_ir_const_node_create();
	if (!log2e)
		return NULL;
	
	log2e->constant = M_LOG2E;
	
	lima_gp_ir_alu_node_t* mul_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!mul_node)
	{
		lima_gp_ir_node_delete(&log2e->node);
		return NULL;
	}
	
	mul_node->children[0] = child;
	mul_node->children[1] = &log2e->node;
	lima_gp_ir_node_link(&mul_node->node, child);
	lima_gp_ir_node_link(&mul_node->node, &log2e->node);
	
	lima_gp_ir_alu_node_t* exp2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_exp2);
	if (!exp2_node)
	{
		lima_gp_ir_node_delete(&log2e->node);
		lima_gp_ir_node_delete(&mul_node->node);
		return NULL;
	}
	
	exp2_node->children[0] = &mul_node->node;
	lima_gp_ir_node_link(&exp2_node->node, &mul_node->node);
	
	return &exp2_node->node;
}

//ln(x) = log2(x)/log2(e) = log2(x)*ln(2)
static lima_gp_ir_node_t* lower_log(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_const_node_t* ln2 = lima_gp_ir_const_node_create();
	if (!ln2)
		return NULL;
	
	ln2->constant = M_LN2;
	
	lima_gp_ir_alu_node_t* log2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_log2);
	if (!log2_node)
	{
		lima_gp_ir_node_delete(&ln2->node);
		return NULL;
	}
	
	log2_node->children[0] = child;
	lima_gp_ir_node_link(&log2_node->node, child);
	
	lima_gp_ir_alu_node_t* mul_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!mul_node)
	{
		lima_gp_ir_node_delete(&ln2->node);
		lima_gp_ir_node_delete(&log2_node->node);
		return NULL;
	}
	
	mul_node->children[0] = &log2_node->node;
	mul_node->children[1] = &ln2->node;
	lima_gp_ir_node_link(&mul_node->node, &log2_node->node);
	lima_gp_ir_node_link(&mul_node->node, &ln2->node);
	
	return &mul_node->node;
}

//pow(x, y) = exp2(y*log2(x))
static lima_gp_ir_node_t* lower_pow(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* base = orig_alu->children[0];
	lima_gp_ir_node_t* exponent = orig_alu->children[1];
	
	lima_gp_ir_alu_node_t* log2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_log2);
	if (!log2_node)
		return NULL;
	
	lima_gp_ir_alu_node_t* mul_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!mul_node)
	{
		lima_gp_ir_node_delete(&log2_node->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* exp2_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_exp2);
	if (!mul_node)
	{
		lima_gp_ir_node_delete(&log2_node->node);
		lima_gp_ir_node_delete(&mul_node->node);
		return NULL;
	}
	
	log2_node->children[0] = base;
	lima_gp_ir_node_link(&log2_node->node, base);
	
	mul_node->children[0] = exponent;
	mul_node->children[1] = &log2_node->node;
	lima_gp_ir_node_link(&mul_node->node, exponent);
	lima_gp_ir_node_link(&mul_node->node, &log2_node->node);
	
	exp2_node->children[0] = &mul_node->node;
	lima_gp_ir_node_link(&exp2_node->node, &mul_node->node);
	
	return &exp2_node->node;
}

//sqrt(x) = 1/rsqrt(x)
//As to why the blob doesn't do x*rsqrt(x) with exception for x = 0, don't ask
//me...
static lima_gp_ir_node_t* lower_sqrt(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_alu_node_t* rsqrt_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_rsqrt);
	if (!rsqrt_node)
		return NULL;
	
	rsqrt_node->children[0] = child;
	lima_gp_ir_node_link(&rsqrt_node->node, child);
	
	lima_gp_ir_alu_node_t* rcp_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_rcp);
	if (!rcp_node)
	{
		lima_gp_ir_node_delete(&rsqrt_node->node);
		return NULL;
	}
	
	rcp_node->children[0] = &rsqrt_node->node;
	lima_gp_ir_node_link(&rcp_node->node, &rsqrt_node->node);
	
	return &rcp_node->node;
}

/* trigonometric functions */

/* sin(x)
 *
 * Strategy:
 *
 * The function:
 * f(x) = 2*pi*abs(x/(2*pi) - floor(x/(2*pi) + 3/4) + 1/4) - pi/2
 * maps all the possible inputs for sin(x) into the range [-pi/2, pi/2],
 * guarenteeing (by the fact that sin is odd and has a period of 2*pi) that
 * sin(x) = sin(f(x)). We feed the result of this into a seventh-degree Taylor
 * series. Because the largest value of x being fed into the series is pi/2,
 * the error is at most:
 * (pi/2)^9/9! < 2^(-12)
 * Although the ESSL 1.0 standard doesn't define a required precision for
 * transcendental functions (section 10.30), this should give us enough
 * precision for any real-world application (and is the same number of terms
 * as what the binary driver uses).
 *
 * An optimization is to rewrite f(x) as:
 * f(x) = 2*pi*(abs(x/(2*pi) - floor(x/(2*pi) + 3/4) + 1/4) - 1/4)
 * The 2*pi can then be absorbed into the constant coefficients, saving a
 * multiply. Thus, each coefficient is:
 * coefficients[n] = (-1)^n*(2*pi)^(2n+1)/(2n+1)!
 *
 * cos(x)
 *
 * Strategy:
 *
 * We use the same Taylor series, but we use a different input function:
 * f(x) = 2*pi*abs(x/(2*pi) + floor(-x/(2*pi)) + 1/2) - pi/2
 * which is a result of substituting pi/2 - x into the original function and
 * simplifying.
 */

static const float sin_coefficients[] = {
	6.28318530717959,  // = 2*pi
	-41.3417022403998, // = (2*pi)^3/3!
	81.605249276075,   // = (2*pi)^5/5!
	-76.7058597530614  // = (2*pi)^7/7!
};

#define NUM_SIN_TERMS sizeof(sin_coefficients)/sizeof(sin_coefficients[0])

/* builds the Taylor series for sin(2*pi*x) */

static lima_gp_ir_node_t* build_sin_series(lima_gp_ir_node_t* input)
{
	lima_gp_ir_alu_node_t* square_alu =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!square_alu)
		return NULL;
	
	square_alu->children[0] = input;
	square_alu->children[1] = input;
	lima_gp_ir_node_link(&square_alu->node, input);
	
	lima_gp_ir_node_t* square = &square_alu->node;
	
	lima_gp_ir_node_t* cur_x_term = input;
	lima_gp_ir_node_t* cur_sum = NULL;
	unsigned i = 0;
	while (true)
	{
		lima_gp_ir_const_node_t* const_term = lima_gp_ir_const_node_create();
		if (!const_term)
			return NULL;
		const_term->constant = sin_coefficients[i];
		
		lima_gp_ir_alu_node_t* term =
			lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
		if (!term)
		{
			lima_gp_ir_node_delete(&const_term->node);
			return NULL;
		}
		
		term->children[0] = &const_term->node;
		term->children[1] = cur_x_term;
		lima_gp_ir_node_link(&term->node, &const_term->node);
		lima_gp_ir_node_link(&term->node, cur_x_term);
		
		if (cur_sum == NULL)
			cur_sum = &term->node;
		else
		{
			lima_gp_ir_alu_node_t* next_sum =
				lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
			if (!next_sum)
			{
				lima_gp_ir_node_delete(&term->node);
				return NULL;
			}
			
			next_sum->children[0] = &term->node;
			next_sum->children[1] = cur_sum;
			lima_gp_ir_node_link(&next_sum->node, &term->node);
			lima_gp_ir_node_link(&next_sum->node, cur_sum);
			
			cur_sum = &next_sum->node;
		}
		
		if (i == NUM_SIN_TERMS - 1)
			break;
		
		lima_gp_ir_alu_node_t* next_x_term =
			lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
		if (!next_x_term)
			return NULL;
		
		next_x_term->children[0] = cur_x_term;
		next_x_term->children[1] = square;
		lima_gp_ir_node_link(&next_x_term->node, cur_x_term);
		lima_gp_ir_node_link(&next_x_term->node, square);
		
		cur_x_term = &next_x_term->node;
		i++;
	}
	
	return cur_sum;
}

/* builds f(x) = abs(x/(2*pi) - floor(x/(2*pi) + 3/4) + 1/4) - 1/4 */

static lima_gp_ir_node_t* build_sin_input(lima_gp_ir_node_t* input)
{
	lima_gp_ir_const_node_t* two_pi = lima_gp_ir_const_node_create();
	if (!two_pi)
		return NULL;
	
	two_pi->constant = 1.0 / (2.0 * M_PI);
	
	lima_gp_ir_const_node_t* one_fourth = lima_gp_ir_const_node_create();
	if (!one_fourth)
	{
		lima_gp_ir_node_delete(&two_pi->node);
		return NULL;
	}
	
	one_fourth->constant = .25;
	
	lima_gp_ir_const_node_t* three_fourths = lima_gp_ir_const_node_create();
	if (!three_fourths)
	{
		lima_gp_ir_node_delete(&two_pi->node);
		lima_gp_ir_node_delete(&one_fourth->node);
		return NULL;
	}
	
	three_fourths->constant = .75;
	
	// = x/(2*pi)
	lima_gp_ir_alu_node_t* x_over_two_pi =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!x_over_two_pi)
	{
		lima_gp_ir_node_delete(&two_pi->node);
		lima_gp_ir_node_delete(&one_fourth->node);
		lima_gp_ir_node_delete(&three_fourths->node);
		return NULL;
	}
	
	x_over_two_pi->children[0] = input;
	x_over_two_pi->children[1] = &two_pi->node;
	lima_gp_ir_node_link(&x_over_two_pi->node, input);
	lima_gp_ir_node_link(&x_over_two_pi->node, &two_pi->node);
	
	// = x/(2*pi) + 3/4
	lima_gp_ir_alu_node_t* inner_floor =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!inner_floor)
	{
		lima_gp_ir_node_delete(&x_over_two_pi->node);
		lima_gp_ir_node_delete(&one_fourth->node);
		lima_gp_ir_node_delete(&three_fourths->node);
		return NULL;
	}
	
	inner_floor->children[0] = &x_over_two_pi->node;
	inner_floor->children[1] = &three_fourths->node;
	lima_gp_ir_node_link(&inner_floor->node, &x_over_two_pi->node);
	lima_gp_ir_node_link(&inner_floor->node, &three_fourths->node);
	
	// = floor(x/(2*pi) + 3/4)
	lima_gp_ir_alu_node_t* floor =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_floor);
	if (!floor)
	{
		lima_gp_ir_node_delete(&inner_floor->node);
		lima_gp_ir_node_delete(&one_fourth->node);
		return NULL;
	}
	
	floor->children[0] = &inner_floor->node;
	lima_gp_ir_node_link(&floor->node, &inner_floor->node);
	
	// = x/(2*pi) - floor(x/(2*pi) + 3/4)
	lima_gp_ir_alu_node_t* sum_one =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!sum_one)
	{
		lima_gp_ir_node_delete(&floor->node);
		lima_gp_ir_node_delete(&one_fourth->node);
		return NULL;
	}
	
	sum_one->children[0] = &x_over_two_pi->node;
	sum_one->children[1] = &floor->node;
	sum_one->children_negate[1] = true;
	lima_gp_ir_node_link(&sum_one->node, &x_over_two_pi->node);
	lima_gp_ir_node_link(&sum_one->node, &floor->node);
	
	// = x/(2*pi) - floor(x/(2*pi) + 3/4) + 1/4
	lima_gp_ir_alu_node_t* sum_two =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!sum_two)
	{
		lima_gp_ir_node_delete(&sum_one->node);
		lima_gp_ir_node_delete(&one_fourth->node);
		return NULL;
	}
	
	sum_two->children[0] = &sum_one->node;
	sum_two->children[1] = &one_fourth->node;
	lima_gp_ir_node_link(&sum_two->node, &sum_one->node);
	lima_gp_ir_node_link(&sum_two->node, &one_fourth->node);
	
	// = abs(x/(2*pi) - floor(x/(2*pi) + 3/4) + 1/4)
	lima_gp_ir_alu_node_t* abs = lima_gp_ir_alu_node_create(lima_gp_ir_op_max);
	if (!abs)
	{
		lima_gp_ir_node_delete(&sum_two->node);
		return NULL;
	}
	
	abs->children[0] = &sum_two->node;
	abs->children[1] = &sum_two->node;
	abs->children_negate[1] = true;
	lima_gp_ir_node_link(&abs->node, &sum_two->node);
	
	// = abs(x/(2*pi) - floor(x/(2*pi) + 3/4) + 1/4) - 1/4
	lima_gp_ir_alu_node_t* result =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!result)
	{
		lima_gp_ir_node_delete(&abs->node);
		return NULL;
	}
	
	result->children[0] = &abs->node;
	result->children[1] = &one_fourth->node;
	result->children_negate[1] = true;
	lima_gp_ir_node_link(&result->node, &abs->node);
	lima_gp_ir_node_link(&result->node, &one_fourth->node);
	
	return &result->node;
}

/* builds f(x) = abs(x/(2*pi) + floor(-x/(2*pi)) + 1/2) - 1/4 */

static lima_gp_ir_node_t* build_cos_input(lima_gp_ir_node_t* input)
{
	lima_gp_ir_const_node_t* two_pi = lima_gp_ir_const_node_create();
	if (!two_pi)
		return NULL;
	
	two_pi->constant = 1.0 / (2.0 * M_PI);
	
	lima_gp_ir_const_node_t* one_half = lima_gp_ir_const_node_create();
	if (!one_half)
	{
		lima_gp_ir_node_delete(&two_pi->node);
		return NULL;
	}
	
	one_half->constant = .5;
	
	lima_gp_ir_const_node_t* neg_one_fourth = lima_gp_ir_const_node_create();
	if (!neg_one_fourth)
	{
		lima_gp_ir_node_delete(&two_pi->node);
		lima_gp_ir_node_delete(&one_half->node);
		return NULL;
	}
	
	neg_one_fourth->constant = -0.25;
	
	// = x/(2*pi)
	lima_gp_ir_alu_node_t* x_over_two_pi =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!x_over_two_pi)
	{
		lima_gp_ir_node_delete(&two_pi->node);
		lima_gp_ir_node_delete(&one_half->node);
		lima_gp_ir_node_delete(&neg_one_fourth->node);
		return NULL;
	}
	
	x_over_two_pi->children[0] = input;
	x_over_two_pi->children[1] = &two_pi->node;
	lima_gp_ir_node_link(&x_over_two_pi->node, input);
	lima_gp_ir_node_link(&x_over_two_pi->node, &two_pi->node);
	
	// = floor(-x/(2*pi))
	lima_gp_ir_alu_node_t* floor =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_floor);
	if (!floor)
	{
		lima_gp_ir_node_delete(&x_over_two_pi->node);
		lima_gp_ir_node_delete(&one_half->node);
		lima_gp_ir_node_delete(&neg_one_fourth->node);
		return NULL;
	}
	
	floor->children[0] = &x_over_two_pi->node;
	floor->children_negate[0] = true;
	lima_gp_ir_node_link(&floor->node, &x_over_two_pi->node);
	
	// = x/(2*pi) + floor(-x/(2*pi))
	lima_gp_ir_alu_node_t* sum_one =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!sum_one)
	{
		lima_gp_ir_node_delete(&floor->node);
		lima_gp_ir_node_delete(&one_half->node);
		lima_gp_ir_node_delete(&neg_one_fourth->node);
		return NULL;
	}
	
	sum_one->children[0] = &x_over_two_pi->node;
	sum_one->children[1] = &floor->node;
	lima_gp_ir_node_link(&sum_one->node, &x_over_two_pi->node);
	lima_gp_ir_node_link(&sum_one->node, &floor->node);

	// = x/(2*pi) + floor(-x/(2*pi)) + 1/2
	lima_gp_ir_alu_node_t* sum_two =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!sum_two)
	{
		lima_gp_ir_node_delete(&sum_one->node);
		lima_gp_ir_node_delete(&one_half->node);
		lima_gp_ir_node_delete(&neg_one_fourth->node);
		return NULL;
	}
	
	sum_two->children[0] = &sum_one->node;
	sum_two->children[1] = &one_half->node;
	lima_gp_ir_node_link(&sum_two->node, &sum_one->node);
	lima_gp_ir_node_link(&sum_two->node, &one_half->node);
	
	// = abs(x/(2*pi) + floor(-x/(2*pi)) + 1/2)
	lima_gp_ir_alu_node_t* abs = lima_gp_ir_alu_node_create(lima_gp_ir_op_max);
	if (!abs)
	{
		lima_gp_ir_node_delete(&sum_two->node);
		lima_gp_ir_node_delete(&neg_one_fourth->node);
		return NULL;
	}
	
	abs->children[0] = &sum_two->node;
	abs->children[1] = &sum_two->node;
	abs->children_negate[1] = true;
	lima_gp_ir_node_link(&abs->node, &sum_two->node);
	
	// = abs(x/(2*pi) + floor(-x/(2*pi)) + 1/2) - 1/4
	lima_gp_ir_alu_node_t* result =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_add);
	if (!result)
	{
		lima_gp_ir_node_delete(&abs->node);
		lima_gp_ir_node_delete(&neg_one_fourth->node);
		return NULL;
	}
	
	result->children[0] = &abs->node;
	result->children[1] = &neg_one_fourth->node;
	lima_gp_ir_node_link(&result->node, &abs->node);
	lima_gp_ir_node_link(&result->node, &neg_one_fourth->node);
	
	return &result->node;
}

static lima_gp_ir_node_t* lower_sin(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_node_t* input = build_sin_input(child);
	if (!input)
		return NULL;
	
	lima_gp_ir_node_t* result = build_sin_series(input);
	if (!result)
	{
		lima_gp_ir_node_delete(input);
		return NULL;
	}
	
	return result;
}

static lima_gp_ir_node_t* lower_cos(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_node_t* input = build_cos_input(child);
	if (!input)
		return NULL;
	
	lima_gp_ir_node_t* result = build_sin_series(input);
	if (!result)
	{
		lima_gp_ir_node_delete(input);
		return NULL;
	}
	
	return result;
}

/* tan(x) = sin(x) / cos(x) */

static lima_gp_ir_node_t* lower_tan(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	lima_gp_ir_node_t* child = orig_alu->children[0];
	
	lima_gp_ir_alu_node_t* sin_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_sin);
	if (!sin_node)
		return NULL;
	
	sin_node->children[0] = child;
	lima_gp_ir_node_link(&sin_node->node, child);
	
	lima_gp_ir_alu_node_t* cos_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_cos);
	if (!cos_node)
	{
		lima_gp_ir_node_delete(&sin_node->node);
		return NULL;
	}
	
	cos_node->children[0] = child;
	lima_gp_ir_node_link(&cos_node->node, child);
	
	lima_gp_ir_alu_node_t* rcp_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_rcp);
	if (!rcp_node)
	{
		lima_gp_ir_node_delete(&sin_node->node);
		lima_gp_ir_node_delete(&cos_node->node);
		return NULL;
	}
	
	rcp_node->children[0] = &cos_node->node;
	lima_gp_ir_node_link(&rcp_node->node, &cos_node->node);
	
	lima_gp_ir_alu_node_t* mul_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	if (!mul_node)
	{
		lima_gp_ir_node_delete(&rcp_node->node);
		lima_gp_ir_node_delete(&sin_node->node);
		return NULL;
	}
	
	mul_node->children[0] = &sin_node->node;
	mul_node->children[1] = &rcp_node->node;
	lima_gp_ir_node_link(&mul_node->node, &sin_node->node);
	lima_gp_ir_node_link(&mul_node->node, &rcp_node->node);
	
	return &mul_node->node;
}

//eq(x, y) = min(x >= y, y >= x)
static lima_gp_ir_node_t* lower_eq(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* ge1 =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_ge);
	
	if (!ge1)
		return NULL;
	
	lima_gp_ir_alu_node_t* ge2 =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_ge);
	
	if (!ge2)
	{
		lima_gp_ir_node_delete(&ge1->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* min =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_min);
	
	if (!min)
	{
		lima_gp_ir_node_delete(&ge1->node);
		lima_gp_ir_node_delete(&ge2->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	ge1->children[0] = orig_alu->children[0];
	ge1->children[1] = orig_alu->children[1];
	lima_gp_ir_node_link(&ge1->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&ge1->node, orig_alu->children[1]);
	
	ge2->children[0] = orig_alu->children[1];
	ge2->children[1] = orig_alu->children[0];
	lima_gp_ir_node_link(&ge2->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&ge2->node, orig_alu->children[1]);
	
	min->children[0] = &ge1->node;
	min->children[1] = &ge2->node;
	lima_gp_ir_node_link(&min->node, &ge1->node);
	lima_gp_ir_node_link(&min->node, &ge2->node);
	
	return &min->node;
}

//ne(x, y) = max(x < y, y < x)
static lima_gp_ir_node_t* lower_ne(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* lt1 =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_lt);
	
	if (!lt1)
		return NULL;
	
	lima_gp_ir_alu_node_t* lt2 =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_lt);
	
	if (!lt2)
	{
		lima_gp_ir_node_delete(&lt1->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* max =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_max);
	
	if (!max)
	{
		lima_gp_ir_node_delete(&lt1->node);
		lima_gp_ir_node_delete(&lt2->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	lt1->children[0] = orig_alu->children[0];
	lt1->children[1] = orig_alu->children[1];
	lima_gp_ir_node_link(&lt1->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&lt1->node, orig_alu->children[1]);
	
	lt2->children[0] = orig_alu->children[1];
	lt2->children[1] = orig_alu->children[0];
	lima_gp_ir_node_link(&lt1->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&lt1->node, orig_alu->children[1]);
	
	max->children[0] = &lt1->node;
	max->children[1] = &lt2->node;
	lima_gp_ir_node_link(&max->node, &lt1->node);
	lima_gp_ir_node_link(&max->node, &lt2->node);
	
	return &max->node;
}

//f2b(x) = ne(x, 0.0)
static lima_gp_ir_node_t* lower_f2b(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_const_node_t* zero = lima_gp_ir_const_node_create();
	
	if (!zero)
		return NULL;
	
	zero->constant = 0.0f;
	
	lima_gp_ir_alu_node_t* ne = lima_gp_ir_alu_node_create(lima_gp_ir_op_ne);
	
	if (!ne)
	{
		lima_gp_ir_node_delete(&zero->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	ne->children[0] = orig_alu->children[0];
	ne->children[1] = &zero->node;
	lima_gp_ir_node_link(&ne->node, orig_alu->children[0]);
	lima_gp_ir_node_link(&ne->node, &zero->node);
	
	return &ne->node;
}

//f2i(x) = sign(x) * floor(abs(x))
static lima_gp_ir_node_t* lower_f2i(lima_gp_ir_node_t* orig)
{
	lima_gp_ir_alu_node_t* sign =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_sign);
	
	if (!sign)
		return NULL;
	
	lima_gp_ir_alu_node_t* floor =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_floor);
	
	if (!floor)
	{
		lima_gp_ir_node_delete(&sign->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* abs = lima_gp_ir_alu_node_create(lima_gp_ir_op_abs);
	
	if (!abs)
	{
		lima_gp_ir_node_delete(&sign->node);
		lima_gp_ir_node_delete(&floor->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* mul = lima_gp_ir_alu_node_create(lima_gp_ir_op_mul);
	
	if (!abs)
	{
		lima_gp_ir_node_delete(&sign->node);
		lima_gp_ir_node_delete(&floor->node);
		lima_gp_ir_node_delete(&abs->node);
		return NULL;
	}
	
	lima_gp_ir_alu_node_t* orig_alu = gp_ir_node_to_alu(orig);
	
	abs->children[0] = orig_alu->children[0];
	lima_gp_ir_node_link(&abs->node, orig_alu->children[0]);
	
	floor->children[0] = &abs->node;
	lima_gp_ir_node_link(&floor->node, &abs->node);
	
	sign->children[0] = orig_alu->children[0];
	lima_gp_ir_node_link(&sign->node, orig_alu->children[0]);
	
	mul->children[0] = &sign->node;
	mul->children[1] = &floor->node;
	lima_gp_ir_node_link(&mul->node, &sign->node);
	lima_gp_ir_node_link(&mul->node, &floor->node);
	
	return &mul->node;
}

typedef struct
{
	lower_cb cb;
	lima_gp_ir_op_e op;
} lower_cb_info_t;

static const lower_cb_info_t lower_cb_info[] = {
	{ lower_abs,   lima_gp_ir_op_abs   },
	{ lower_not,   lima_gp_ir_op_not   },
	{ lower_div,   lima_gp_ir_op_div   },
	{ lower_mod,   lima_gp_ir_op_mod   },
	{ lower_lrp,   lima_gp_ir_op_lrp   },
	{ lower_exp2,  lima_gp_ir_op_exp2  },
	{ lower_log2,  lima_gp_ir_op_log2  },
	{ lower_rcp,   lima_gp_ir_op_rcp   },
	{ lower_rsqrt, lima_gp_ir_op_rsqrt },
	{ lower_ceil,  lima_gp_ir_op_ceil  },
	{ lower_fract, lima_gp_ir_op_fract },
	{ lower_exp,   lima_gp_ir_op_exp   },
	{ lower_log,   lima_gp_ir_op_log   },
	{ lower_pow,   lima_gp_ir_op_pow   },
	{ lower_sqrt,  lima_gp_ir_op_sqrt  },
	{ lower_sin,   lima_gp_ir_op_sin   },
	{ lower_cos,   lima_gp_ir_op_cos   },
	{ lower_tan,   lima_gp_ir_op_tan   },
	{ lower_eq,    lima_gp_ir_op_eq    },
	{ lower_ne,    lima_gp_ir_op_ne    },
	{ lower_f2b,   lima_gp_ir_op_f2b   },
	{ lower_f2i,   lima_gp_ir_op_f2i   }
};

#define NUM_LOWER_CALLBACKS sizeof(lower_cb_info)/sizeof(lower_cb_info_t)

static lower_cb get_lower_cb(lima_gp_ir_op_e op)
{
	unsigned i;
	for (i = 0; i < NUM_LOWER_CALLBACKS; i++)
	{
		if (lower_cb_info[i].op == op)
			return lower_cb_info[i].cb;
	}
	
	return NULL;
}

static bool lower_node_cb(lima_gp_ir_node_t* node, void* state)
{
	bool* has_lowered = (bool*) state;
	
	lower_cb cb = get_lower_cb(node->op);
	if (!cb)
		return true;
	
	lima_gp_ir_node_t* new_node = cb(node);
	if (!new_node)
		return false;
	
	if (!lima_gp_ir_node_replace(node, new_node))
		return false;
	
	*has_lowered = true;
	return true;
}

//Store nodes cannot use certain nodes directly, so we have to insert a move
//node between those nodes and the store itself.
static bool lower_store_child(lima_gp_ir_node_t* node,
							  lima_gp_ir_node_t* store_node)
{
	//NOTE: this list of opcodes must match up with the opcodes allowed in
	//get_store_input() in codegen.c
	if (node->op == lima_gp_ir_op_mov         ||
		node->op == lima_gp_ir_op_mul         ||
		node->op == lima_gp_ir_op_select      ||
		node->op == lima_gp_ir_op_complex1    ||
		node->op == lima_gp_ir_op_complex2    ||
		node->op == lima_gp_ir_op_add         ||
		node->op == lima_gp_ir_op_floor       ||
		node->op == lima_gp_ir_op_sign        ||
		node->op == lima_gp_ir_op_ge          ||
		node->op == lima_gp_ir_op_lt          ||
		node->op == lima_gp_ir_op_min         ||
		node->op == lima_gp_ir_op_max         ||
		node->op == lima_gp_ir_op_neg         ||
		node->op == lima_gp_ir_op_clamp_const ||
		node->op == lima_gp_ir_op_preexp2     ||
		node->op == lima_gp_ir_op_postlog2    ||
		node->op == lima_gp_ir_op_exp2_impl   ||
		node->op == lima_gp_ir_op_log2_impl   ||
		node->op == lima_gp_ir_op_rcp_impl    ||
		node->op == lima_gp_ir_op_rsqrt_impl)
	{
		return true;
	}
	
	//This opcode cannot be used directly, insert a move
	lima_gp_ir_alu_node_t* mov_node =
		lima_gp_ir_alu_node_create(lima_gp_ir_op_mov);
	if (!mov_node)
		return false;
	
	mov_node->children[0] = node;
	lima_gp_ir_node_link(&mov_node->node, node);
	
	lima_gp_ir_node_replace_child(store_node, node, &mov_node->node);
	return true;
}

bool lima_gp_ir_lower_root_node(lima_gp_ir_root_node_t* node)
{
	bool has_lowered = true;
	while (has_lowered) //Keep going until we reach a fixed point
	{
		has_lowered = false;
		if (!lima_gp_ir_node_dfs(&node->node, NULL, lower_node_cb,
								 (void*)&has_lowered))
			return false;
	}
	
	if (node->node.op == lima_gp_ir_op_branch_uncond)
	{
		//Convert unconditional branches to conditional branches
		lima_gp_ir_const_node_t* cond = lima_gp_ir_const_node_create();
		if (!cond)
			return false;
		
		cond->constant = 1.0;
		
		node->node.op = lima_gp_ir_op_branch_cond;
		
		lima_gp_ir_branch_node_t* branch_node =
			gp_ir_node_to_branch(&node->node);
		branch_node->condition = &cond->node;
		lima_gp_ir_node_link(&node->node, &cond->node);
	}
	
	if (node->node.op == lima_gp_ir_op_store_temp ||
		node->node.op == lima_gp_ir_op_store_varying)
	{
		lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(&node->node);
		unsigned i;
		for (i = 0; i < 4; i++)
		{
			if (!store_node->mask[i])
				continue;
			if (!lower_store_child(store_node->children[i], &node->node))
				return false;
		}
	}
	
	if (node->node.op == lima_gp_ir_op_store_reg)
	{
		lima_gp_ir_store_reg_node_t* store_node =
			gp_ir_node_to_store_reg(&node->node);
		unsigned i;
		for (i = 0; i < 4; i++)
		{
			if (!store_node->mask[i])
				continue;
			if (!lower_store_child(store_node->children[i], &node->node))
				return false;
		}
	}
	
	return true;
}

bool lima_gp_ir_lower_block(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node;
	
	gp_ir_block_for_each_node(block, node)
	{
		if (!lima_gp_ir_lower_root_node(node))
			return false;
	}
	
	return true;
}

bool lima_gp_ir_lower_prog(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_block_t* block;
	
	gp_ir_prog_for_each_block(prog, block)
	{
		if (!lima_gp_ir_lower_block(block))
			return false;
	}
	
	return true;
}

