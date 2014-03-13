/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Codethink (http://www.codethink.co.uk), Connor Abbott (connor@abbott.cx)
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



#include "pp_hir.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>



static inline double _sign(double x)
{
	if (x == 0.0)
		return 0.0;
	return (x < 0.0 ? -1.0 : 1.0);
}

static inline double _fract(double x)
{
	return (x - floor(x));
}

static inline double _sat(double x)
{
	return (x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x));
}

static inline double _int(double x)
{
	return (double)((int) x);
}

static inline double _pos(double x)
{
	return x < 0.0 ? 0.0 : x;
}

static inline double _min(double x, double y)
	{ return (x < y ? x : y); }
static inline double _max(double x, double y)
	{ return (x > y ? x : y); }
static inline double _gt(double x, double y)
	{ return (x > y ? 0.0 : 1.0); }
static inline double _ge(double x, double y)
	{ return (x >= y ? 0.0 : 1.0); }
static inline double _eq(double x, double y)
	{ return (x == y ? 0.0 : 1.0); }
static inline double _ne(double x, double y)
	{ return (x != y ? 0.0 : 1.0); }
static inline double _not(double x)
	{ return (x == 0.0 ? 1.0 : 0.0); }

static inline double _lrp(double x, double y, double t)
	{ return x * (1 - t) + y * t; }




static lima_pp_hir_vec4_t mov_cfold(lima_pp_hir_vec4_t* args)
{
	return args[0];
}

static lima_pp_hir_vec4_t add_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x + args[1].x,
		.y = args[0].y + args[1].y,
		.z = args[0].z + args[1].z,
		.w = args[0].w + args[1].w,
	};
}

static lima_pp_hir_vec4_t sub_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x - args[1].x,
		.y = args[0].y - args[1].y,
		.z = args[0].z - args[1].z,
		.w = args[0].w - args[1].w,
	};
}

static lima_pp_hir_vec4_t neg_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = -args[0].x,
		.y = -args[0].y,
		.z = -args[0].z,
		.w = -args[0].w,
	};
}



static lima_pp_hir_vec4_t mul_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x * args[1].x,
		.y = args[0].y * args[1].y,
		.z = args[0].z * args[1].z,
		.w = args[0].w * args[1].w,
	};
}

static lima_pp_hir_vec4_t rcp_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = 1.0 / args[0].x,
		.y = 1.0 / args[0].y,
		.z = 1.0 / args[0].z,
		.w = 1.0 / args[0].w,
	};
}

static lima_pp_hir_vec4_t div_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x / args[1].x,
		.y = args[0].y / args[1].y,
		.z = args[0].z / args[1].z,
		.w = args[0].w / args[1].w,
	};
}


//the derivative of a constant is 0
static lima_pp_hir_vec4_t deriv_cfold(lima_pp_hir_vec4_t* args)
{
	(void) args;
	
	return (lima_pp_hir_vec4_t){
		.x = 0.0,
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}


static lima_pp_hir_vec4_t sum3_cfold(lima_pp_hir_vec4_t* args)
{
	double sum = (args[0].x + args[0].y + args[0].z);
	return (lima_pp_hir_vec4_t){
		.x = sum,
		.y = sum,
		.z = sum,
		.w = sum,
	};
}

static lima_pp_hir_vec4_t sum4_cfold(lima_pp_hir_vec4_t* args)
{
	double sum = (args[0].x + args[0].y + args[0].z + args[0].w);
	return (lima_pp_hir_vec4_t){
		.x = sum,
		.y = sum,
		.z = sum,
		.w = sum,
	};
}


static lima_pp_hir_vec4_t normalize2_cfold(lima_pp_hir_vec4_t* args)
{
	double factor = 1.0 / sqrt(args[0].x*args[0].x + args[0].y*args[0].y);
	return (lima_pp_hir_vec4_t){
		.x = args[0].x * factor,
		.y = args[0].y * factor,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t normalize3_cfold(lima_pp_hir_vec4_t* args)
{
	double factor = 1.0 / sqrt(args[0].x*args[0].x + args[0].y*args[0].y +
							   args[0].z*args[0].z);
	return (lima_pp_hir_vec4_t){
		.x = args[0].x * factor,
		.y = args[0].y * factor,
		.z = args[0].z * factor,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t normalize4_cfold(lima_pp_hir_vec4_t* args)
{
	double factor = 1.0 / sqrt(args[0].x*args[0].x + args[0].y*args[0].y +
							   args[0].z*args[0].z + args[0].w*args[0].w);
	return (lima_pp_hir_vec4_t){
		.x = args[0].x * factor,
		.y = args[0].y * factor,
		.z = args[0].z * factor,
		.w = args[0].w * factor,
	};
}


static lima_pp_hir_vec4_t sin_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = sin(args[0].x),
		.y = sin(args[0].y),
		.z = sin(args[0].z),
		.w = sin(args[0].w),
	};
}

static lima_pp_hir_vec4_t cos_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = cos(args[0].x),
		.y = cos(args[0].y),
		.z = cos(args[0].z),
		.w = cos(args[0].w),
	};
}

static lima_pp_hir_vec4_t tan_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = tan(args[0].x),
		.y = tan(args[0].y),
		.z = tan(args[0].z),
		.w = tan(args[0].w),
	};
}

static lima_pp_hir_vec4_t asin_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = asin(args[0].x),
		.y = asin(args[0].y),
		.z = asin(args[0].z),
		.w = asin(args[0].w),
	};
}

static lima_pp_hir_vec4_t acos_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = acos(args[0].x),
		.y = acos(args[0].y),
		.z = acos(args[0].z),
		.w = acos(args[0].w),
	};
}

static lima_pp_hir_vec4_t atan_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = atan(args[0].x),
		.y = atan(args[0].y),
		.z = atan(args[0].z),
		.w = atan(args[0].w),
	};
}

static lima_pp_hir_vec4_t atan2_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = atan2(args[0].x, args[1].x),
		.y = atan2(args[0].y, args[1].y),
		.z = atan2(args[0].z, args[1].z),
		.w = atan2(args[0].w, args[1].w),
	};
}



static lima_pp_hir_vec4_t pow_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = pow(args[0].x, args[1].x),
		.y = pow(args[0].y, args[1].y),
		.z = pow(args[0].z, args[1].z),
		.w = pow(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t exp_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = pow(M_E, args[0].x),
		.y = pow(M_E, args[0].y),
		.z = pow(M_E, args[0].z),
		.w = pow(M_E, args[0].w),
	};
}

static lima_pp_hir_vec4_t log_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = log(args[0].x),
		.y = log(args[0].y),
		.z = log(args[0].z),
		.w = log(args[0].w),
	};
}

static lima_pp_hir_vec4_t exp2_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = pow(2.0, args[0].x),
		.y = pow(2.0, args[0].y),
		.z = pow(2.0, args[0].z),
		.w = pow(2.0, args[0].w),
	};
}

static lima_pp_hir_vec4_t log2_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = log2(args[0].x),
		.y = log2(args[0].y),
		.z = log2(args[0].z),
		.w = log2(args[0].w),
	};
}

static lima_pp_hir_vec4_t sqrt_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = sqrt(args[0].x),
		.y = sqrt(args[0].y),
		.z = sqrt(args[0].z),
		.w = sqrt(args[0].w),
	};
}

static lima_pp_hir_vec4_t rsqrt_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = 1.0 / sqrt(args[0].x),
		.y = 1.0 / sqrt(args[0].y),
		.z = 1.0 / sqrt(args[0].z),
		.w = 1.0 / sqrt(args[0].w),
	};
}



static lima_pp_hir_vec4_t abs_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = fabs(args[0].x),
		.y = fabs(args[0].y),
		.z = fabs(args[0].z),
		.w = fabs(args[0].w),
	};
}

static lima_pp_hir_vec4_t sign_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _sign(args[0].x),
		.y = _sign(args[0].y),
		.z = _sign(args[0].z),
		.w = _sign(args[0].w),
	};
}

static lima_pp_hir_vec4_t floor_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = floor(args[0].x),
		.y = floor(args[0].y),
		.z = floor(args[0].z),
		.w = floor(args[0].w),
	};
}

static lima_pp_hir_vec4_t ceil_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = ceil(args[0].x),
		.y = ceil(args[0].y),
		.z = ceil(args[0].z),
		.w = ceil(args[0].w),
	};
}

static lima_pp_hir_vec4_t fract_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _fract(args[0].x),
		.y = _fract(args[0].y),
		.z = _fract(args[0].z),
		.w = _fract(args[0].w),
	};
}

static lima_pp_hir_vec4_t mod_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = fmod(args[0].x, args[1].x),
		.y = fmod(args[0].y, args[1].y),
		.z = fmod(args[0].z, args[1].z),
		.w = fmod(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t min_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _min(args[0].x, args[1].x),
		.y = _min(args[0].y, args[1].y),
		.z = _min(args[0].z, args[1].z),
		.w = _min(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t max_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _max(args[0].x, args[1].x),
		.y = _max(args[0].y, args[1].y),
		.z = _max(args[0].z, args[1].z),
		.w = _max(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t dot2_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x*args[1].x + args[0].y*args[1].y,
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t dot3_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x*args[1].x + args[0].y*args[1].y + args[0].z*args[1].z,
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t dot4_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = args[0].x*args[1].x + args[0].y*args[1].y + args[0].z*args[1].z +
			 args[0].w*args[1].w,
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t lrp_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _lrp(args[0].x, args[1].x, args[2].x),
		.y = _lrp(args[0].y, args[1].y, args[2].y),
		.z = _lrp(args[0].z, args[1].z, args[2].z),
		.w = _lrp(args[0].w, args[1].w, args[2].w),
	};
}

static lima_pp_hir_vec4_t gt_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _gt(args[0].x, args[1].x),
		.y = _gt(args[0].y, args[1].y),
		.z = _gt(args[0].z, args[1].z),
		.w = _gt(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t ge_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _ge(args[0].x, args[1].x),
		.y = _ge(args[0].y, args[1].y),
		.z = _ge(args[0].z, args[1].z),
		.w = _ge(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t eq_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _eq(args[0].x, args[1].x),
		.y = _eq(args[0].y, args[1].y),
		.z = _eq(args[0].z, args[1].z),
		.w = _eq(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t ne_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _ne(args[0].x, args[1].x),
		.y = _ne(args[0].y, args[1].y),
		.z = _ne(args[0].z, args[1].z),
		.w = _ne(args[0].w, args[1].w),
	};
}

static lima_pp_hir_vec4_t any2_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _ne(args[0].x + args[0].y, 0.0),
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t any3_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _ne(args[0].x + args[0].y + args[0].z, 0.0),
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t any4_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _ne(args[0].x + args[0].y + args[0].z + args[0].w, 0.0),
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t all2_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _eq(args[0].x + args[0].y, 2.0),
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t all3_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _eq(args[0].x + args[0].y + args[0].z, 3.0),
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t all4_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _eq(args[0].x + args[0].y + args[0].z + args[0].w, 4.0),
		.y = 0.0,
		.z = 0.0,
		.w = 0.0,
	};
}

static lima_pp_hir_vec4_t all_eq2_cfold(lima_pp_hir_vec4_t* args)
{
	lima_pp_hir_vec4_t eq = eq_cfold(args);
	return all2_cfold(&eq);
}

static lima_pp_hir_vec4_t all_eq3_cfold(lima_pp_hir_vec4_t* args)
{
	lima_pp_hir_vec4_t eq = eq_cfold(args);
	return all3_cfold(&eq);
}

static lima_pp_hir_vec4_t all_eq4_cfold(lima_pp_hir_vec4_t* args)
{
	lima_pp_hir_vec4_t eq = eq_cfold(args);
	return all4_cfold(&eq);
}

static lima_pp_hir_vec4_t any_ne2_cfold(lima_pp_hir_vec4_t* args)
{
	lima_pp_hir_vec4_t ne = ne_cfold(args);
	return any2_cfold(&ne);
}

static lima_pp_hir_vec4_t any_ne3_cfold(lima_pp_hir_vec4_t* args)
{
	lima_pp_hir_vec4_t ne = ne_cfold(args);
	return any3_cfold(&ne);
}

static lima_pp_hir_vec4_t any_ne4_cfold(lima_pp_hir_vec4_t* args)
{
	lima_pp_hir_vec4_t ne = ne_cfold(args);
	return any4_cfold(&ne);
}

static lima_pp_hir_vec4_t not_cfold(lima_pp_hir_vec4_t* args)
{
	return (lima_pp_hir_vec4_t){
		.x = _not(args[0].x),
		.y = _not(args[0].y),
		.z = _not(args[0].z),
		.w = _not(args[0].w),
	};
}


lima_pp_hir_vec4_t (*lima_pp_hir_cfold[])(lima_pp_hir_vec4_t* args) =
{
	mov_cfold,

	neg_cfold,
	add_cfold,
	sub_cfold,
	
	deriv_cfold,
	deriv_cfold,

	mul_cfold,
	rcp_cfold,
	div_cfold,

	NULL,
	NULL,

	sum3_cfold,
	sum4_cfold,
	
	normalize2_cfold,
	normalize3_cfold,
	normalize4_cfold,
	
	NULL,

	sin_cfold,
	cos_cfold,
	tan_cfold,
	asin_cfold,
	acos_cfold,
	
	atan_cfold,
	atan2_cfold,
	NULL,
	NULL,
	NULL,

	pow_cfold,
	exp_cfold,
	log_cfold,
	exp2_cfold,
	log2_cfold,
	sqrt_cfold,
	rsqrt_cfold,

	abs_cfold,
	sign_cfold,
	floor_cfold,
	ceil_cfold,
	fract_cfold,
	mod_cfold,
	min_cfold,
	max_cfold,
	
	dot2_cfold,
	dot3_cfold,
	dot4_cfold,
	
	lrp_cfold,
	
	gt_cfold,
	ge_cfold,
	eq_cfold,
	ne_cfold,
	any2_cfold,
	any3_cfold,
	any4_cfold,
	all2_cfold,
	all3_cfold,
	all4_cfold,
	all_eq2_cfold,
	all_eq3_cfold,
	all_eq4_cfold,
	any_ne2_cfold,
	any_ne3_cfold,
	any_ne4_cfold,
	not_cfold,
	
	NULL,
	
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static lima_pp_hir_vec4_t output_mod(lima_pp_hir_vec4_t in,
									 lima_pp_outmod_e outmod)
{
	lima_pp_hir_vec4_t out;
	switch (outmod)
	{
		case lima_pp_outmod_clamp_positive:
			out.x = _pos(in.x);
			out.y = _pos(in.y);
			out.z = _pos(in.z);
			out.w = _pos(in.w);
			break;
			
		case lima_pp_outmod_clamp_fraction:
			out.x = _sat(in.x);
			out.y = _sat(in.y);
			out.z = _sat(in.z);
			out.w = _sat(in.w);
			break;
			
		case lima_pp_outmod_round:
			out.x = _int(in.x);
			out.y = _int(in.y);
			out.z = _int(in.z);
			out.w = _int(in.w);
			break;
			
		default:
			out = in;
			break;
	}
	
	return out;
}

unsigned lima_pp_hir_prog_cfold(lima_pp_hir_prog_t* prog)
{
	if (!prog)
		return 0;

	unsigned c = 0;
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
	{
		lima_pp_hir_cmd_t* cmd;
		pp_hir_block_for_each_cmd(block, cmd)
		{
			unsigned o = cmd->op;
			if (o >= lima_pp_hir_op_count
				|| !lima_pp_hir_cfold[o])
				continue;
			
			if (cmd->op == lima_pp_hir_op_mov &&
				!cmd->src[0].negate && !cmd->src[0].absolute &&
				cmd->dst.modifier == lima_pp_outmod_none)
				continue;

			unsigned arg_count
				= cmd->num_args;
			lima_pp_hir_vec4_t args[arg_count];

			unsigned a;
			for (a = 0; (a < arg_count)
				 && cmd->src[a].constant; a++)
			{
				memcpy(&args[a], cmd->src[a].depend, sizeof(lima_pp_hir_vec4_t));
				if (cmd->src[a].absolute)
				{
					args[a].x = fabs(args[a].x);
					args[a].y = fabs(args[a].y);
					args[a].z = fabs(args[a].z);
					args[a].w = fabs(args[a].w);
				}
				if (cmd->src[a].negate)
				{
					args[a].x = -args[a].x;
					args[a].y = -args[a].y;
					args[a].z = -args[a].z;
					args[a].w = -args[a].w;
				}
			}
			if (a < arg_count)
				continue;


			lima_pp_hir_cmd_t* mov_cmd
				= lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
			if (!mov_cmd) continue;

			lima_pp_hir_vec4_t* fold
				= (lima_pp_hir_vec4_t*)malloc(sizeof(lima_pp_hir_vec4_t));
			if (!fold)
			{
				lima_pp_hir_cmd_delete(mov_cmd);
				continue;
			}
			*fold = lima_pp_hir_cfold[o](args);
			*fold = output_mod(*fold, cmd->dst.modifier);
			mov_cmd->dst = cmd->dst;
			mov_cmd->dst.modifier = lima_pp_outmod_none;
			mov_cmd->src[0].constant = true;
			mov_cmd->src[0].depend = fold;
		
			lima_pp_hir_cmd_replace_uses(cmd, mov_cmd);
			lima_pp_hir_block_replace(cmd, mov_cmd);

			c++;
		}
	}

	return c;
}
