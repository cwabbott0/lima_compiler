/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *   Connor Abbott
 *
 * Copyright (c) 2013
 *   Codethink (http://www.codethink.co.uk)
 *   Connor Abbott (connor@abbott.cx)
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



#ifndef __ogt_arch_lima_pp_h__
#define __ogt_arch_lima_pp_h__

#include <stdint.h>
#include <stdbool.h>
#include "hfloat.h"



typedef struct
__attribute__((__packed__))
{
	ogt_hfloat_t x, y, z, w;
} lima_pp_vec4_t;

typedef enum
{
	lima_pp_outmod_none           = 0,
	lima_pp_outmod_clamp_fraction = 1,
	lima_pp_outmod_clamp_positive = 2,
	lima_pp_outmod_round          = 3,
} lima_pp_outmod_e;

/*static const lima_pp_vec4_t
	gl_mali_FragCoordScale = { 0 };
static const lima_pp_vec4_t
	gl_mali_PointCoordScaleBias = { 0 };*/



/* Control */

typedef union
__attribute__((__packed__))
{
	struct
	__attribute__((__packed__))
	{
		unsigned count      :  5;
		bool     stop       :  1;
		bool     sync       :  1;
		unsigned fields     : 12;
		unsigned next_count :  6;
		bool     prefetch   :  1;
		unsigned unknown    :  6;
	};
	uint32_t mask;
} lima_pp_ctrl_t;

typedef enum
{
	lima_pp_field_varying      =  0,
	lima_pp_field_sampler      =  1,
	lima_pp_field_uniform      =  2,
	lima_pp_field_vec4_mul     =  3,
	lima_pp_field_float_mul    =  4,
	lima_pp_field_vec4_acc     =  5,
	lima_pp_field_float_acc    =  6,
	lima_pp_field_combine      =  7,
	lima_pp_field_temp_write   =  8,
	lima_pp_field_branch       =  9,
	lima_pp_field_vec4_const_0 = 10,
	lima_pp_field_vec4_const_1 = 11,
	lima_pp_field_count        = 12,
} lima_pp_field_e;

extern const char* lima_pp_field_name[];
extern unsigned    lima_pp_field_size[];



/* Data Inputs */

typedef enum
{
	lima_pp_vec4_reg_frag_color =  0,
	lima_pp_vec4_reg_constant0  = 12,
	lima_pp_vec4_reg_constant1  = 13,
	lima_pp_vec4_reg_texture    = 14,
	lima_pp_vec4_reg_uniform    = 15,
	lima_pp_vec4_reg_discard    = 15,
} lima_pp_vec4_reg_e;

typedef union
__attribute__((__packed__))
{
	struct
	__attribute__((__packed__))
	{
		unsigned           perspective   :  2;
		unsigned           source_type   :  2;
		unsigned           unknown_0     :  1; /* = 0 */
		unsigned           alignment     :  2;
		unsigned           unknown_1     :  3; /* = 00 0 */
		unsigned           offset_vector :  4;
		unsigned           unknown_2     :  2; /* = 00 */
		unsigned           offset_scalar :  2;
		unsigned           index         :  6;
		lima_pp_vec4_reg_e dest          :  4;
		unsigned           mask          :  4;
		unsigned           unknown_3     :  2; /* = 00 */
	} imm;
	struct
	__attribute__((__packed__))
	{
		unsigned           perspective :  2;
		unsigned           source_type :  2; /* = 01 */
		unsigned           unknown_0   :  2; /* = 00 */
		bool               normalize   :  1;
		unsigned           unknown_1   :  3;
		lima_pp_vec4_reg_e source      :  4;
		bool               negate      :  1;
		bool               absolute    :  1;
		unsigned           swizzle     :  8;
		lima_pp_vec4_reg_e dest        :  4;
		unsigned           mask        :  4;
		unsigned           unknown_2   :  2; /* = 00 */
	} reg;
} lima_pp_field_varying_t;

typedef enum
{
	lima_pp_sampler_type_2d   = 0x00,
	lima_pp_sampler_type_cube = 0x1F,
} lima_pp_sampler_type_e;

typedef struct
__attribute__((__packed__))
{
	unsigned               lod_bias     :  6;
	unsigned               index_offset :  6;
	unsigned               unknown_0    :  6; /* = 000000 */
	bool                   lod_bias_en  :  1;
	unsigned               unknown_1    :  5; /* = 00000 */
	lima_pp_sampler_type_e type         :  5;
	bool                   offset_en    :  1;
	unsigned               index        : 12;
	unsigned               unknown_2    : 20; /* = 0011 1001 0000 0000 0001 */
} lima_pp_field_sampler_t;

typedef enum
{
	lima_pp_uniform_src_uniform   = 0,
	lima_pp_uniform_src_temporary = 3,
} lima_pp_uniform_src_e;

typedef struct
__attribute__((__packed__))
{
	lima_pp_uniform_src_e source     :  2;
	unsigned              unknown_0  :  8; /* = 00 0000 00 */
	unsigned              alignment  :  2; /* 0: float, 1: vec2, 2: vec4 */
	unsigned              unknown_1  :  6; /* = 00 0000 */
	unsigned              offset_reg :  6;
	bool                  offset_en  :  1;
	unsigned              index      : 16;
} lima_pp_field_uniform_t;







/* Vector Pipe */

typedef enum
{
	lima_pp_vec4_mul_op_not = 0x08, /* Logical Not */
	lima_pp_vec4_mul_op_neq = 0x0C, /* Not Equal */
	lima_pp_vec4_mul_op_lt  = 0x0D, /* Less Than */
	lima_pp_vec4_mul_op_le  = 0x0E, /* Less than or Equal */
	lima_pp_vec4_mul_op_eq  = 0x0F, /* Equal */
	lima_pp_vec4_mul_op_min = 0x10, /* Minimum */
	lima_pp_vec4_mul_op_max = 0x11, /* Maximum */
	lima_pp_vec4_mul_op_mov = 0x1F, /* Passthrough, result = arg1 */
} lima_pp_vec4_mul_op_e;

typedef struct
__attribute__((__packed__))
{
	lima_pp_vec4_reg_e    arg1_source   : 4;
	unsigned              arg1_swizzle  : 8;
	bool                  arg1_absolute : 1;
	bool                  arg1_negate   : 1;
	lima_pp_vec4_reg_e    arg0_source   : 4;
	unsigned              arg0_swizzle  : 8;
	bool                  arg0_absolute : 1;
	bool                  arg0_negate   : 1;
	unsigned              dest          : 4;
	unsigned              mask          : 4;
	lima_pp_outmod_e      dest_modifier : 2;
	lima_pp_vec4_mul_op_e op            : 5;
} lima_pp_field_vec4_mul_t;

typedef enum
{
	lima_pp_vec4_acc_op_add   = 0x00,
	lima_pp_vec4_acc_op_fract = 0x04, /* Fract? */
	lima_pp_vec4_acc_op_neq   = 0x08, /* Not Equal */
	lima_pp_vec4_acc_op_lt    = 0x09, /* Less-Than */
	lima_pp_vec4_acc_op_le    = 0x0A, /* Less-than or Equal */
	lima_pp_vec4_acc_op_eq    = 0x0B, /* Equal */
	lima_pp_vec4_acc_op_floor = 0x0C,
	lima_pp_vec4_acc_op_ceil  = 0x0D,
	lima_pp_vec4_acc_op_min   = 0x0E,
	lima_pp_vec4_acc_op_max   = 0x0F,
	lima_pp_vec4_acc_op_sum3  = 0x10, /* Sum3, result.w = (arg1.x + arg1.y + arg1.z) */
	lima_pp_vec4_acc_op_sum   = 0x11, /* Sum, result.w = (arg1.x + arg1.y + arg1.z + arg1.w) */
	lima_pp_vec4_acc_op_dFdx  = 0x14,
	lima_pp_vec4_acc_op_dFdy  = 0x15,
	lima_pp_vec4_acc_op_sel   = 0x17, /* result = (^fmul ? arg0 : arg1) */
	lima_pp_vec4_acc_op_mov   = 0x1F, /* Passthrough, result = arg1 */
} lima_pp_vec4_acc_op_e;

typedef struct
__attribute__((__packed__))
{
	lima_pp_vec4_reg_e    arg1_source   : 4;
	unsigned              arg1_swizzle  : 8;
	bool                  arg1_absolute : 1;
	bool                  arg1_negate   : 1;
	lima_pp_vec4_reg_e    arg0_source   : 4;
	unsigned              arg0_swizzle  : 8;
	bool                  arg0_absolute : 1;
	bool                  arg0_negate   : 1;
	unsigned              dest          : 4;
	unsigned              mask          : 4;
	lima_pp_outmod_e      dest_modifier : 2;
	lima_pp_vec4_acc_op_e op            : 5;
	bool                  mul_in        : 1; /* whether to get arg1 from multiply unit below */
} lima_pp_field_vec4_acc_t;



/* Float (Scalar) Pipe */

typedef enum
{
	lima_pp_float_mul_op_not = 0x08, /* Logical Not */
	lima_pp_float_mul_op_neq = 0x0C, /* Not Equal */
	lima_pp_float_mul_op_lt  = 0x0D, /* Less Than */
	lima_pp_float_mul_op_le  = 0x0E, /* Less than or Equal */
	lima_pp_float_mul_op_eq  = 0x0F, /* Equal */
	lima_pp_float_mul_op_min = 0x10, /* Minimum */
	lima_pp_float_mul_op_max = 0x11, /* Maximum */
	lima_pp_float_mul_op_mov = 0x1F, /* Passthrough, result = arg1 */
} lima_pp_float_mul_op_e;

typedef struct
__attribute__((__packed__))
{
	unsigned         arg1_source   : 6;
	bool             arg1_absolute : 1;
	bool             arg1_negate   : 1;
	unsigned         arg0_source   : 6;
	bool             arg0_absolute : 1;
	bool             arg0_negate   : 1;
	unsigned         dest          : 6;
	bool             output_en     : 1; /* Set to 0 when outputting directly to float_acc below. */
	lima_pp_outmod_e dest_modifier : 2;
	unsigned           op            : 5;
} lima_pp_field_float_mul_t;

typedef enum
{
	lima_pp_float_acc_op_add   = 0x00,
	lima_pp_float_acc_op_fract = 0x04,
	lima_pp_float_acc_op_neq   = 0x08, /* Not Equal */
	lima_pp_float_acc_op_lt    = 0x09, /* Less-Than */
	lima_pp_float_acc_op_le    = 0x0A, /* Less-than or Equal */
	lima_pp_float_acc_op_eq    = 0x0B, /* Equal */
	lima_pp_float_acc_op_floor = 0x0C,
	lima_pp_float_acc_op_ceil  = 0x0D,
	lima_pp_float_acc_op_min   = 0x0E,
	lima_pp_float_acc_op_max   = 0x0F,
	lima_pp_float_acc_op_dFdx  = 0x14,
	lima_pp_float_acc_op_dFdy  = 0x15,
	lima_pp_float_acc_op_mov   = 0x1F, /* Passthrough, result = arg1 */
} lima_pp_float_acc_op_e;

typedef struct
__attribute__((__packed__))
{
	unsigned               arg1_source   : 6;
	bool                   arg1_absolute : 1;
	bool                   arg1_negate   : 1;
	unsigned               arg0_source   : 6;
	bool                   arg0_absolute : 1;
	bool                   arg0_negate   : 1;
	unsigned               dest          : 6;
	bool                   output_en     : 1; /* Always true */
	lima_pp_outmod_e       dest_modifier : 2;
	lima_pp_float_acc_op_e op            : 5;
	bool                   mul_in        : 1; /* Get arg1 from float_mul above. */
} lima_pp_field_float_acc_t;



/* Temporary Write / Framebuffer Read */

typedef union
__attribute__((__packed__))
{
	struct
	__attribute__((__packed__))
	{
		unsigned dest       :  2; /* = 11 */
		unsigned unknown_0  :  2; /* = 00 */
		unsigned source     :  6;
		unsigned alignment  :  2; /* 0: float, 1: vec2, 2: vec4 */
		unsigned unknown_2  :  6; /* = 00 0000 */
		unsigned offset_reg :  6;
		bool     offset_en  :  1;
		unsigned index      : 16;
	} temp_write;
	struct
	__attribute__((__packed__))
	{
		bool     source    :  1; /* 0 = fb_depth, 1 = fb_color */
		unsigned unknown_0 :  5; /* = 00 111 */
		unsigned dest      :  4;
		unsigned unknown_1 : 31; /* = 0 0000 ... 10 */
	} fb_read;
} lima_pp_field_temp_write_t;



/* Result combiner */

typedef enum
{
	lima_pp_combine_scalar_op_rcp   = 0, /* Reciprocal */
	lima_pp_combine_scalar_op_mov   = 1, /* No Operation */
	lima_pp_combine_scalar_op_sqrt  = 2, /* Square-Root */
	lima_pp_combine_scalar_op_rsqrt = 3, /* Inverse Square-Root */
	lima_pp_combine_scalar_op_exp2  = 4, /* Binary Exponent */
	lima_pp_combine_scalar_op_log2  = 5, /* Binary Logarithm */
	lima_pp_combine_scalar_op_sin   = 6, /* Sine   (Scaled LUT) */
	lima_pp_combine_scalar_op_cos   = 7, /* Cosine (Scaled LUT) */
	lima_pp_combine_scalar_op_atan  = 8, /* Arc Tangent Part 1 */
	lima_pp_combine_scalar_op_atan2 = 9, /* Arc Tangent 2 Part 1 */
} lima_pp_combine_scalar_op_e;

typedef union
__attribute__((__packed__))
{
	struct
	__attribute__((__packed__))
	{
		bool                        dest_vec      : 1;
		bool                        arg1_en       : 1;
		lima_pp_combine_scalar_op_e op            : 4;
		bool                        arg1_absolute : 1;
		bool                        arg1_negate   : 1;
		unsigned                    arg1_src      : 6;
		bool                        arg0_absolute : 1;
		bool                        arg0_negate   : 1;
		unsigned                    arg0_src      : 6;
		lima_pp_outmod_e            dest_modifier : 2;
		unsigned                    dest          : 6;
	} scalar;
	struct
	__attribute__((__packed__))
	{
		bool     dest_vec     : 1;
		bool     arg1_en      : 1;
		unsigned arg1_swizzle : 8;
		unsigned arg1_source  : 4;
		unsigned padding_0    : 8;
		unsigned mask         : 4;
		unsigned dest         : 4;
	} vector;
} lima_pp_field_combine_t;



/* Branch/Control Flow */

#define LIMA_PP_DISCARD_WORD0 0x007F0003
#define LIMA_PP_DISCARD_WORD1 0x00000000
#define LIMA_PP_DISCARD_WORD2 0x000

typedef union
__attribute__((__packed__))
{
		struct
	__attribute__((__packed__))
	{
		unsigned unknown_0   :  4; /* = 0000 */
		unsigned arg1_source :  6;
		unsigned arg0_source :  6;
		bool     cond_gt     :  1;
		bool     cond_eq     :  1;
		bool     cond_lt     :  1;
		unsigned unknown_1   : 22; /* = 0 0000 0000 0000 0000 0000 0 */
		  signed target      : 27;
		unsigned unknown_2   :  5; /* = 0 0011 */
	} branch;
	struct
	__attribute__((__packed__))
	{
		unsigned word0 : 32;
		unsigned word1 : 32;
		unsigned word2 : 9;
	} discard;
} lima_pp_field_branch_t;



typedef struct
{
	const char* name;
	const char* symbol;
	bool arg0, arg1;
} lima_pp_asm_op_t;

extern lima_pp_asm_op_t lima_pp_vec4_mul_asm_op[];
extern lima_pp_asm_op_t lima_pp_vec4_acc_asm_op[];
extern lima_pp_asm_op_t lima_pp_float_mul_asm_op[];
extern lima_pp_asm_op_t lima_pp_float_acc_asm_op[];
extern lima_pp_asm_op_t lima_pp_combine_asm_op[];

typedef struct
{
	lima_pp_ctrl_t             control;
	lima_pp_field_varying_t    varying;
	lima_pp_field_sampler_t    sampler;
	lima_pp_field_uniform_t    uniform;
	lima_pp_field_vec4_mul_t   vec4_mul;
	lima_pp_field_float_mul_t  float_mul;
	lima_pp_field_vec4_acc_t   vec4_acc;
	lima_pp_field_float_acc_t  float_acc;
	lima_pp_field_combine_t    combine;
	lima_pp_field_temp_write_t temp_write;
	lima_pp_field_branch_t     branch;
	lima_pp_vec4_t             const0;
	lima_pp_vec4_t             const1;
} lima_pp_instruction_t;



extern void lima_pp_instruction_calc_size(
	lima_pp_ctrl_t* control);
extern void     lima_pp_instruction_encode(
	lima_pp_instruction_t* inst, uint32_t* output);
extern void     lima_pp_instruction_decode(
	uint32_t* source, lima_pp_instruction_t* output);
extern void     lima_pp_instruction_print(
	lima_pp_instruction_t* code, bool verbose, unsigned tabs);

#endif
