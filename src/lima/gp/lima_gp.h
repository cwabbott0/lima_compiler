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



#ifndef __ogt_arch_lima_gp_h__
#define __ogt_arch_lima_gp_h__

#include <stdint.h>
#include <stdbool.h>



typedef enum
{
	lima_gp_acc_op_add   = 0,
	lima_gp_acc_op_floor = 1,
	lima_gp_acc_op_sign  = 2,
	lima_gp_acc_op_ge    = 4,
	lima_gp_acc_op_lt    = 5,
	lima_gp_acc_op_min   = 6,
	lima_gp_acc_op_max   = 7,
} lima_gp_acc_op_e;

typedef enum
{
	lima_gp_complex_op_nop   = 0,
	lima_gp_complex_op_exp2  = 2,
	lima_gp_complex_op_log2  = 3,
	lima_gp_complex_op_rsqrt = 4,
	lima_gp_complex_op_rcp   = 5,
	lima_gp_complex_op_pass  = 9,
	lima_gp_complex_op_temp_store_addr  = 12,
	lima_gp_complex_op_temp_load_addr_0 = 13,
	lima_gp_complex_op_temp_load_addr_1 = 14,
	lima_gp_complex_op_temp_load_addr_2 = 15,
} lima_gp_complex_op_e;

typedef enum
{
	lima_gp_mul_op_mul      = 0,
	lima_gp_mul_op_complex1 = 1,
	lima_gp_mul_op_complex2 = 3,
	lima_gp_mul_op_select   = 4,
} lima_gp_mul_op_e;

typedef enum
{
	lima_gp_pass_op_pass     = 2,
	lima_gp_pass_op_preexp2  = 4,
	lima_gp_pass_op_postlog2 = 5,
	lima_gp_pass_op_clamp    = 6,
} lima_gp_pass_op_e;



typedef enum
{
	lima_gp_src_attrib_x     =  0,
	lima_gp_src_attrib_y     =  1,
	lima_gp_src_attrib_z     =  2,
	lima_gp_src_attrib_w     =  3,
	lima_gp_src_register_x   =  4,
	lima_gp_src_register_y   =  5,
	lima_gp_src_register_z   =  6,
	lima_gp_src_register_w   =  7,
	lima_gp_src_unknown_0    =  8,
	lima_gp_src_unknown_1    =  9,
	lima_gp_src_unknown_2    = 10,
	lima_gp_src_unknown_3    = 11,
	lima_gp_src_load_x       = 12,
	lima_gp_src_load_y       = 13,
	lima_gp_src_load_z       = 14,
	lima_gp_src_load_w       = 15,
	lima_gp_src_p1_acc_0     = 16,
	lima_gp_src_p1_acc_1     = 17,
	lima_gp_src_p1_mul_0     = 18,
	lima_gp_src_p1_mul_1     = 19,
	lima_gp_src_p1_pass      = 20,
	lima_gp_src_unused       = 21,
	lima_gp_src_ident        = 22,
	lima_gp_src_p1_complex   = 22,
	lima_gp_src_p2_pass      = 23,
	lima_gp_src_p2_acc_0     = 24,
	lima_gp_src_p2_acc_1     = 25,
	lima_gp_src_p2_mul_0     = 26,
	lima_gp_src_p2_mul_1     = 27,
	lima_gp_src_p1_attrib_x  = 28,
	lima_gp_src_p1_attrib_y  = 29,
	lima_gp_src_p1_attrib_z  = 30,
	lima_gp_src_p1_attrib_w  = 31,
} lima_gp_src_e;

typedef enum
{
	lima_gp_store_src_acc_0   = 0,
	lima_gp_store_src_acc_1   = 1,
	lima_gp_store_src_mul_0   = 2,
	lima_gp_store_src_mul_1   = 3,
	lima_gp_store_src_pass    = 4,
	lima_gp_store_src_unknown = 5,
	lima_gp_store_src_complex = 6,
	lima_gp_store_src_none    = 7,
} lima_gp_store_src_e;

typedef enum
{
	lima_gp_load_off_ld_addr_0 = 1,
	lima_gp_load_off_ld_addr_1 = 2,
	lima_gp_load_off_ld_addr_2 = 3,
	lima_gp_load_off_none      = 7,
} lima_gp_load_off_t;

typedef struct
__attribute__((__packed__))
{
	lima_gp_src_e        mul0_src0           : 5;
	lima_gp_src_e        mul0_src1           : 5;
	lima_gp_src_e        mul1_src0           : 5;
	lima_gp_src_e        mul1_src1           : 5;
	bool                 mul0_neg            : 1;
	bool                 mul1_neg            : 1;
	lima_gp_src_e        acc0_src0           : 5;
	lima_gp_src_e        acc0_src1           : 5;
	lima_gp_src_e        acc1_src0           : 5;
	lima_gp_src_e        acc1_src1           : 5;
	bool                 acc0_src0_neg       : 1;
	bool                 acc0_src1_neg       : 1;
	bool                 acc1_src0_neg       : 1;
	bool                 acc1_src1_neg       : 1;
	unsigned             load_addr           : 9;
	lima_gp_load_off_t   load_offset         : 3;
	unsigned             register0_addr      : 4;
	bool                 register0_attribute : 1;
	unsigned             register1_addr      : 4;
	bool                 store0_temporary    : 1;
	bool                 store1_temporary    : 1;
	bool                 branch              : 1;
	bool                 branch_target_lo    : 1;
	lima_gp_store_src_e  store0_src_x        : 3;
	lima_gp_store_src_e  store0_src_y        : 3;
	lima_gp_store_src_e  store1_src_z        : 3;
	lima_gp_store_src_e  store1_src_w        : 3;
	lima_gp_acc_op_e     acc_op              : 3;
	lima_gp_complex_op_e complex_op          : 4;
	unsigned             store0_addr         : 4;
	bool                 store0_varying      : 1;
	unsigned             store1_addr         : 4;
	bool                 store1_varying      : 1;
	lima_gp_mul_op_e     mul_op              : 3;
	lima_gp_pass_op_e    pass_op             : 3;
	lima_gp_src_e        complex_src         : 5;
	lima_gp_src_e        pass_src            : 5;
	unsigned             unknown_1           : 4; /* 12: tmp_st, 13: branch */
	unsigned             branch_target       : 8;
} lima_gp_instruction_t;



/*typedef struct
{
	essl_program_t* symbol_table;
	ogt_link_map_t* link_map;

	unsigned               size;
	lima_gp_instruction_t* code;

	unsigned attrib_prefetch;
} lima_gp_program_t;*/



extern const char* lima_gp_acc_op_name[];
extern const char* lima_gp_acc_op_sym[];
extern const char* lima_gp_complex_op_name[];
extern const char* lima_gp_mul_op_name[];
extern const char* lima_gp_pass_op_name[];
extern const char* lima_gp_control_op_name[];
extern const char* lima_gp_src_name[];
extern const char* lima_gp_store_src_name[];
extern const char* lima_gp_load_off_name[];



extern void lima_gp_instruction_print_explicit(
	lima_gp_instruction_t* code, unsigned tabs);
/*extern void lima_gp_instruction_print_verbose(
	lima_gp_instruction_t* code, ogt_link_map_t* map, unsigned tabs);
extern void lima_gp_instruction_print_decompile(
	lima_gp_instruction_t* code, unsigned offset, ogt_link_map_t* map,
	unsigned tabs);*/

#endif
