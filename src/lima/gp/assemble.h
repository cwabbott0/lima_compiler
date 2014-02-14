/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *
 * Copyright (c) 2013 Codethink (http://www.codethink.co.uk)
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



#ifndef __lima_gp_assemble_h__
#define __lima_gp_assemble_h__

#include "lima_gp.h"

#include <stdint.h>
#include <stdbool.h>



typedef enum
{
	lima_gp_fu_multiply,
	lima_gp_fu_accumulate,
	lima_gp_fu_pass,
	lima_gp_fu_complex,
	lima_gp_fu_uniform,
	lima_gp_fu_temporary,
	lima_gp_fu_attribute,
	lima_gp_fu_register,
	lima_gp_fu_store,
} lima_gp_fu_e;

typedef struct
{
	lima_gp_fu_e unit;
	unsigned     index;
} lima_gp_fu_t;

typedef enum
{
	lima_gp_reg_x,
	lima_gp_reg_y,
	lima_gp_reg_z,
	lima_gp_reg_w,
	lima_gp_reg_out,
	lima_gp_reg_unused,
	lima_gp_reg_ident,
	lima_gp_reg_addr,

	lima_gp_reg_count
} lima_gp_reg_e;

typedef struct
{
	lima_gp_fu_t  unit;
	lima_gp_reg_e reg;
	unsigned      time;
	bool          neg;
} lima_gp_src_t;

typedef enum
{
	lima_gp_op_nop,
	lima_gp_op_pass,
	lima_gp_op_neg,

	lima_gp_op_sub,
	lima_gp_op_abs,
	lima_gp_op_nabs,

	lima_gp_op_add,
	lima_gp_op_floor,
	lima_gp_op_sign,
	lima_gp_op_ge,
	lima_gp_op_lt,
	lima_gp_op_gt,
	lima_gp_op_le,
	lima_gp_op_min,
	lima_gp_op_max,

	lima_gp_op_mul,
	lima_gp_op_complex1,
	lima_gp_op_complex2,
	lima_gp_op_select,

	lima_gp_op_exp2,
	lima_gp_op_log2,
	lima_gp_op_rsqrt,
	lima_gp_op_rcp,
	lima_gp_op_temp_store_addr,
	lima_gp_op_temp_load_addr_0,
	lima_gp_op_temp_load_addr_1,
	lima_gp_op_temp_load_addr_2,

	lima_gp_op_clamp,

	lima_gp_op_load,
	lima_gp_op_store_register,
	lima_gp_op_store_varying,
	lima_gp_op_store_temporary,

	lima_gp_op_count
} lima_gp_op_e;

typedef struct
{
	lima_gp_op_e op;
	bool           neg;
} lima_gp_op_t;

static const lima_gp_instruction_t lima_gp_instruction_default =
{
	.mul0_src0           = lima_gp_src_unused,
	.mul0_src1           = lima_gp_src_unused,
	.mul1_src0           = lima_gp_src_unused,
	.mul1_src1           = lima_gp_src_unused,
	.mul0_neg            = false,
	.mul1_neg            = false,
	.acc0_src0           = lima_gp_src_unused,
	.acc0_src1           = lima_gp_src_unused,
	.acc1_src0           = lima_gp_src_unused,
	.acc1_src1           = lima_gp_src_unused,
	.acc0_src0_neg       = false,
	.acc0_src1_neg       = false,
	.acc1_src0_neg       = false,
	.acc1_src1_neg       = false,
	.load_addr           = 0,
	.load_offset         = lima_gp_load_off_none,
	.register0_addr      = 0,
	.register0_attribute = false,
	.register1_addr      = 0,
	.store0_temporary    = false,
	.store1_temporary    = false,
	.branch              = false,
	.branch_target_lo    = false,
	.store0_src_x        = lima_gp_store_src_none,
	.store0_src_y        = lima_gp_store_src_none,
	.store1_src_z        = lima_gp_store_src_none,
	.store1_src_w        = lima_gp_store_src_none,
	.acc_op              = lima_gp_acc_op_add,
	.complex_op          = lima_gp_complex_op_nop,
	.store0_addr         = 0,
	.store0_varying      = false,
	.store1_addr         = 0,
	.store1_varying      = false,
	.mul_op              = lima_gp_mul_op_mul,
	.pass_op             = lima_gp_pass_op_pass,
	.complex_src         = lima_gp_src_unused,
	.pass_src            = lima_gp_src_unused,
	.unknown_1           = 0,
	.branch_target       = 0,
};

typedef struct
{
	lima_gp_instruction_t inst;
	lima_gp_instruction_t mask;
} lima_gp_instruction_partial_t;



extern bool lima_gp_instruction_partial_merge(
	lima_gp_instruction_partial_t* dst,
	lima_gp_instruction_partial_t a,
	lima_gp_instruction_partial_t b);

extern bool lima_gp_assemble_field(
	lima_gp_fu_t unit, lima_gp_op_t op,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_src_t* c, lima_gp_src_t* d,
	lima_gp_instruction_partial_t* inst);
extern bool lima_gp_assemble_field_load(
	unsigned address, lima_gp_src_t* offset,
	lima_gp_instruction_partial_t* partial);
extern bool lima_gp_assemble_field_store(
	unsigned unit, lima_gp_op_t op, unsigned addr,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_instruction_partial_t* partial);
extern bool lima_gp_assemble_field_register(
	unsigned unit, unsigned index, bool attribute,
	lima_gp_instruction_partial_t* partial);
extern bool lima_gp_assemble_field_branch(unsigned target,
	lima_gp_instruction_partial_t* inst);

#endif
