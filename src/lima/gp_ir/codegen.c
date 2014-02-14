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

#include "scheduler.h"
#include "../gp/lima_gp.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	lima_gp_instruction_t* instrs;
	unsigned num_instrs, start_instr;
	bool has_branch;
	unsigned branch_dest; // Refers to a basic block, must be fixed up
} codegen_block_t;

typedef struct
{
	codegen_block_t* blocks;
	unsigned num_blocks;
} codegen_prog_t;

static lima_gp_src_e get_alu_input(lima_gp_ir_node_t* parent,
								   lima_gp_ir_node_t* child)
{
	switch (child->op)
	{
		case lima_gp_ir_op_mov:
		{
			if (child->sched_instr == parent->sched_instr + 1)
			{
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p1_acc_0;
					case 1:
						return lima_gp_src_p1_acc_1;
					case 2:
						return lima_gp_src_p1_mul_1;
					case 3:
						return lima_gp_src_p1_mul_0;
					case 4:
						return lima_gp_src_p1_complex;
					case 5:
						return lima_gp_src_p1_pass;
					default:
						assert(0);
				}
			}
			else
			{
				assert(child->sched_instr == parent->sched_instr + 2);
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p2_acc_0;
					case 1:
						return lima_gp_src_p2_acc_1;
					case 2:
						return lima_gp_src_p2_mul_1;
					case 3:
						return lima_gp_src_p2_mul_0;
					case 5:
						return lima_gp_src_p2_pass;
					default:
						assert(0);
				}
			}
		}
			
		case lima_gp_ir_op_neg:
		{
			if (child->sched_instr == parent->sched_instr + 1)
			{
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p1_acc_0;
					case 1:
						return lima_gp_src_p1_acc_1;
					case 2:
						return lima_gp_src_p1_mul_1;
					case 3:
						return lima_gp_src_p1_mul_0;
					default:
						assert(0);
				}
			}
			else
			{
				assert(child->sched_instr == parent->sched_instr + 2);
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p2_acc_0;
					case 1:
						return lima_gp_src_p2_acc_1;
					case 2:
						return lima_gp_src_p2_mul_1;
					case 3:
						return lima_gp_src_p2_mul_0;
					default:
						assert(0);
				}
			}
		}
			
		case lima_gp_ir_op_mul:
		{
			if (child->sched_instr == parent->sched_instr + 1)
			{
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p1_mul_1;
					case 1:
						return lima_gp_src_p1_mul_0;
					default:
						assert(0);
				}
			}
			else
			{
				assert(child->sched_instr == parent->sched_instr + 2);
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p2_mul_1;
					case 1:
						return lima_gp_src_p2_mul_0;
					default:
						assert(0);
				}
			}
		}
			
		case lima_gp_ir_op_select:
		case lima_gp_ir_op_complex1:
		case lima_gp_ir_op_complex2:
		{
			if (child->sched_instr == parent->sched_instr + 1)
				return lima_gp_src_p1_mul_0;
			else
			{
				assert(child->sched_instr == parent->sched_instr + 2);
				return lima_gp_src_p2_mul_0;
			}
		}
			
		case lima_gp_ir_op_add:
		case lima_gp_ir_op_floor:
		case lima_gp_ir_op_sign:
		case lima_gp_ir_op_ge:
		case lima_gp_ir_op_lt:
		case lima_gp_ir_op_min:
		case lima_gp_ir_op_max:
		{
			if (child->sched_instr == parent->sched_instr + 1)
			{
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p1_acc_0;
					case 1:
						return lima_gp_src_p1_acc_1;
					default:
						assert(0);
				}
			}
			else
			{
				assert(child->sched_instr == parent->sched_instr + 2);
				switch (child->sched_pos)
				{
					case 0:
						return lima_gp_src_p2_acc_0;
					case 1:
						return lima_gp_src_p2_acc_1;
					default:
						assert(0);
				}
			}
		}
			
		case lima_gp_ir_op_clamp_const:
		case lima_gp_ir_op_preexp2:
		case lima_gp_ir_op_postlog2:
		{
			if (child->sched_instr == parent->sched_instr + 1)
				return lima_gp_src_p1_pass;
			else
			{
				assert(child->sched_instr == parent->sched_instr + 2);
				return lima_gp_src_p2_pass;
			}
		}
			
		case lima_gp_ir_op_exp2_impl:
		case lima_gp_ir_op_log2_impl:
		case lima_gp_ir_op_rcp_impl:
		case lima_gp_ir_op_rsqrt_impl:
		{
			assert(child->sched_instr == parent->sched_instr + 1);
			return lima_gp_src_p1_complex;
		}
			
		case lima_gp_ir_op_load_reg:
		{
			lima_gp_src_e base;
			if (child->sched_pos == 0)
			{
				base = lima_gp_src_register_x;
				assert(child->sched_instr == parent->sched_instr);
			}
			else
			{
				if (child->sched_instr == parent->sched_instr)
					base = lima_gp_src_attrib_x;
				else
				{
					assert(child->sched_instr == parent->sched_instr + 1);
					base = lima_gp_src_p1_attrib_x;
				}
			}
			
			lima_gp_ir_load_reg_node_t* load_reg_node =
				gp_ir_node_to_load_reg(child);
			return base + load_reg_node->reg->phys_reg_offset +
				load_reg_node->component;
		}
			
		case lima_gp_ir_op_load_uniform:
		{
			lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(child);
			return lima_gp_src_load_x + load_node->component;
		}
			
		case lima_gp_ir_op_load_attribute:
		{
			lima_gp_src_e base;
			if (child->sched_instr == parent->sched_instr)
				base = lima_gp_src_attrib_x;
			else
			{
				assert(child->sched_instr == parent->sched_instr + 1);
				base = lima_gp_src_p1_attrib_x;
			}
			
			lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(child);
			return base + load_node->component;
		}
			
		default:
			assert(0);
	}
	
	return lima_gp_src_unused; //Should never get here
}

static void emit_mul_slot_zero(lima_gp_instruction_t* instr,
							   lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_mul:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul0_src1 = get_alu_input(node, alu_node->children[1]);
			if (instr->mul0_src1 == lima_gp_src_p1_complex)
			{
				//Will get confused with lima_gp_src_ident, so need to swap
				//inputs
				lima_gp_src_e temp = instr->mul0_src0;
				instr->mul0_src0 = instr->mul0_src1;
				instr->mul0_src1 = temp;
			}
			instr->mul0_neg = alu_node->dest_negate;
			if (alu_node->children_negate[0])
				instr->mul0_neg = !instr->mul0_neg;
			if (alu_node->children_negate[1])
				instr->mul0_neg = !instr->mul0_neg;
			instr->mul_op = lima_gp_mul_op_mul;
			break;
		}
			
		case lima_gp_ir_op_mov:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul0_src1 = lima_gp_src_ident;
			instr->mul0_neg = false;
			instr->mul_op = lima_gp_mul_op_mul;
			break;
		}
			
		case lima_gp_ir_op_neg:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul0_src1 = lima_gp_src_ident;
			instr->mul0_neg = true;
			instr->mul_op = lima_gp_mul_op_mul;
			break;
		}
			
		case lima_gp_ir_op_select:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul0_src1 = get_alu_input(node, alu_node->children[0]);
			instr->mul0_src0 = get_alu_input(node, alu_node->children[1]);
			instr->mul0_neg = false;
			instr->mul_op = lima_gp_mul_op_select;
			break;
		}
			
		case lima_gp_ir_op_complex1:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul0_src1 = get_alu_input(node, alu_node->children[1]);
			instr->mul0_neg = false;
			instr->mul_op = lima_gp_mul_op_complex1;
			break;
		}
			
		case lima_gp_ir_op_complex2:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul0_src1 = instr->mul0_src0;
			instr->mul0_neg = false;
			instr->mul_op = lima_gp_mul_op_complex2;
			break;
		}
			
		default:
			assert(0);
	}
}

static void emit_mul_slot_one(lima_gp_instruction_t* instr,
							  lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_mul:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul1_src1 = get_alu_input(node, alu_node->children[1]);
			if (instr->mul1_src1 == lima_gp_src_p1_complex)
			{
				//Will get confused with lima_gp_src_ident, so need to swap
				//inputs
				lima_gp_src_e temp = instr->mul1_src0;
				instr->mul1_src0 = instr->mul1_src1;
				instr->mul1_src1 = temp;
			}
			instr->mul1_neg = alu_node->dest_negate;
			if (alu_node->children_negate[0])
				instr->mul1_neg = !instr->mul1_neg;
			if (alu_node->children_negate[1])
				instr->mul1_neg = !instr->mul1_neg;
			break;
		}
			
		case lima_gp_ir_op_mov:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul1_src1 = lima_gp_src_ident;
			instr->mul1_neg = false;
			break;
		}
			
		case lima_gp_ir_op_neg:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul1_src1 = lima_gp_src_ident;
			instr->mul1_neg = true;
			break;
		}
			
		case lima_gp_ir_op_select:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul1_src0 = get_alu_input(node, alu_node->children[2]);
			instr->mul1_src1 = lima_gp_src_unused;
			instr->mul1_neg = false;
			break;
		}
			
		case lima_gp_ir_op_complex1:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->mul1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->mul1_src1 = get_alu_input(node, alu_node->children[2]);
			instr->mul1_neg = false;
			break;
		}
			
		default:
			assert(0);
	}
}

static void emit_add_slot_zero(lima_gp_instruction_t* instr,
							   lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_add:
		case lima_gp_ir_op_ge:
		case lima_gp_ir_op_lt:
		case lima_gp_ir_op_min:
		case lima_gp_ir_op_max:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->acc0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->acc0_src1 = get_alu_input(node, alu_node->children[1]);
			instr->acc0_src0_neg = alu_node->children_negate[0];
			instr->acc0_src1_neg = alu_node->children_negate[1];
			switch (node->op)
			{
				case lima_gp_ir_op_add:
					instr->acc_op = lima_gp_acc_op_add;
					if (instr->acc0_src1 == lima_gp_src_p1_complex)
					{
						//Swap sources
						instr->acc0_src1 = instr->acc0_src0;
						instr->acc0_src0 = lima_gp_src_p1_complex;
						bool temp = instr->acc0_src0_neg;
						instr->acc0_src0_neg = instr->acc0_src1_neg;
						instr->acc0_src1_neg = temp;
					}
					break;
				case lima_gp_ir_op_ge:
					instr->acc_op = lima_gp_acc_op_ge;
					break;
				case lima_gp_ir_op_lt:
					instr->acc_op = lima_gp_acc_op_lt;
					break;
				case lima_gp_ir_op_min:
					instr->acc_op = lima_gp_acc_op_min;
					break;
				case lima_gp_ir_op_max:
					instr->acc_op = lima_gp_acc_op_max;
					break;
				default:
					break;
			}
			break;
		}
			
		case lima_gp_ir_op_neg:
			instr->acc0_src0_neg = true;
			/* fallthrough */
		case lima_gp_ir_op_mov:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->acc0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->acc0_src1 = lima_gp_src_ident;
			instr->acc0_src1_neg = true;
			instr->acc_op = lima_gp_acc_op_add;
			break;
		}
			
		case lima_gp_ir_op_floor:
		case lima_gp_ir_op_sign:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->acc0_src0 = get_alu_input(node, alu_node->children[0]);
			instr->acc0_src1 = lima_gp_src_unused;
			instr->acc0_src0_neg = alu_node->children_negate[0];
			instr->acc0_src1_neg = false;
			if (node->op == lima_gp_ir_op_floor)
				instr->acc_op = lima_gp_acc_op_floor;
			else
				instr->acc_op = lima_gp_acc_op_sign;
			break;
		}
			
		default:
			assert(0);
	}
}

static void emit_add_slot_one(lima_gp_instruction_t* instr,
							  lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_add:
		case lima_gp_ir_op_ge:
		case lima_gp_ir_op_lt:
		case lima_gp_ir_op_min:
		case lima_gp_ir_op_max:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->acc1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->acc1_src1 = get_alu_input(node, alu_node->children[1]);
			instr->acc1_src0_neg = alu_node->children_negate[0];
			instr->acc1_src1_neg = alu_node->children_negate[1];
			if (node->op == lima_gp_ir_op_add &&
				instr->acc1_src1 == lima_gp_src_p1_complex)
			{
				//Swap sources
				instr->acc1_src1 = instr->acc1_src0;
				instr->acc1_src0 = lima_gp_src_p1_complex;
				bool temp = instr->acc1_src0_neg;
				instr->acc1_src0_neg = instr->acc1_src1_neg;
				instr->acc1_src1_neg = temp;
			}
			break;
		}
			
		case lima_gp_ir_op_neg:
			instr->acc1_src0_neg = true;
			/* fallthrough */
		case lima_gp_ir_op_mov:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->acc1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->acc1_src1 = lima_gp_src_ident;
			instr->acc1_src1_neg = true;
			break;
		}
			
		case lima_gp_ir_op_floor:
		case lima_gp_ir_op_sign:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->acc1_src0 = get_alu_input(node, alu_node->children[0]);
			instr->acc1_src1 = lima_gp_src_unused;
			instr->acc1_src0_neg = alu_node->children_negate[0];
			instr->acc1_src1_neg = false;
			break;
		}
			
		default:
			assert(0);
	}
}

static void emit_complex_slot(lima_gp_instruction_t* instr,
							  lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_mov:
		case lima_gp_ir_op_exp2_impl:
		case lima_gp_ir_op_log2_impl:
		case lima_gp_ir_op_rcp_impl:
		case lima_gp_ir_op_rsqrt_impl:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->complex_src = get_alu_input(node, alu_node->children[0]);
			switch (node->op)
			{
				case lima_gp_ir_op_mov:
					instr->complex_op = lima_gp_complex_op_pass;
					break;
				case lima_gp_ir_op_exp2_impl:
					instr->complex_op = lima_gp_complex_op_exp2;
					break;
				case lima_gp_ir_op_log2_impl:
					instr->complex_op = lima_gp_complex_op_log2;
					break;
				case lima_gp_ir_op_rcp_impl:
					instr->complex_op = lima_gp_complex_op_rcp;
					break;
				case lima_gp_ir_op_rsqrt_impl:
					instr->complex_op = lima_gp_complex_op_rsqrt;
					break;
				default:
					break;
			}
			break;
		}
			
		case lima_gp_ir_op_store_temp_load_off0:
		case lima_gp_ir_op_store_temp_load_off1:
		case lima_gp_ir_op_store_temp_load_off2:
		{
			lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
			instr->complex_src = get_alu_input(node, store_node->children[0]);
			switch (node->op)
			{
				case lima_gp_ir_op_store_temp_load_off0:
					instr->complex_op = lima_gp_complex_op_temp_load_addr_0;
					break;
				case lima_gp_ir_op_store_temp_load_off1:
					instr->complex_op = lima_gp_complex_op_temp_load_addr_1;
					break;
				case lima_gp_ir_op_store_temp_load_off2:
					instr->complex_op = lima_gp_complex_op_temp_load_addr_2;
					break;
				default:
					break;
			}
			break;
		}
			
		case lima_gp_ir_op_store_temp:
		{
			lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
			instr->complex_src = get_alu_input(node, store_node->addr);
			instr->complex_op = lima_gp_complex_op_temp_store_addr;
			break;
		}
			
		default:
			assert(0);
	}
}

static void emit_pass_slot(lima_gp_instruction_t* instr,
						   lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_clamp_const:
		{
			lima_gp_ir_clamp_const_node_t* clamp_const_node =
				gp_ir_node_to_clamp_const(node);
			instr->pass_op = lima_gp_pass_op_clamp;
			instr->pass_src = get_alu_input(node, clamp_const_node->child);
			break;
		}
		case lima_gp_ir_op_mov:
		case lima_gp_ir_op_preexp2:
		case lima_gp_ir_op_postlog2:
		{
			lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
			instr->pass_src = get_alu_input(node, alu_node->children[0]);
			switch (node->op)
			{
				case lima_gp_ir_op_mov:
					instr->pass_op = lima_gp_pass_op_pass;
					break;
				case lima_gp_ir_op_preexp2:
					instr->pass_op = lima_gp_pass_op_preexp2;
					break;
				case lima_gp_ir_op_postlog2:
					instr->pass_op = lima_gp_pass_op_postlog2;
					break;
				default:
					break;
			}
			break;
		}
			
		case lima_gp_ir_op_branch_cond:
		{
			lima_gp_ir_branch_node_t* branch_node = gp_ir_node_to_branch(node);
			instr->pass_src = get_alu_input(node, branch_node->condition);
			instr->pass_op = lima_gp_pass_op_pass;
			break;
		}
			
		default:
			break;
	}
}

static void emit_uniform_slot(lima_gp_instruction_t* instr,
							  lima_gp_ir_instr_t* ir_instr)
{
	instr->load_addr = ir_instr->uniform_index;
	switch (ir_instr->uniform_off_reg)
	{
		case 0:
			instr->load_offset = lima_gp_load_off_none;
			break;
		case 1:
			instr->load_offset = lima_gp_load_off_ld_addr_0;
			break;
		case 2:
			instr->load_offset = lima_gp_load_off_ld_addr_1;
			break;
		case 3:
			instr->load_offset = lima_gp_load_off_ld_addr_0;
			break;
		default:
			assert(0);
	}
}

static void emit_reg_zero_slot(lima_gp_instruction_t* instr,
							   lima_gp_ir_instr_t* ir_instr)
{
	instr->register0_attribute = ir_instr->attr_reg_slot_is_attr;
	instr->register0_addr = ir_instr->attr_reg_index;
}

static void emit_reg_one_slot(lima_gp_instruction_t* instr,
							  lima_gp_ir_instr_t* ir_instr)
{
	instr->register1_addr = ir_instr->reg_index;
}

static void emit_branch_slot(lima_gp_instruction_t* instr)
{
	instr->branch = true;
	instr->unknown_1 = 13;
	// Branch destination gets fixed up later
}

static lima_gp_store_src_e get_store_input(lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_mov:
		{
			switch (node->sched_pos)
			{
				case 0:
					return lima_gp_store_src_acc_0;
				case 1:
					return lima_gp_store_src_acc_1;
				case 2:
					return lima_gp_store_src_mul_1;
				case 3:
					return lima_gp_store_src_mul_0;
				case 4:
					return lima_gp_store_src_complex;
				case 5:
					return lima_gp_store_src_pass;
				default:
					assert(0);
			}
		}
			
		case lima_gp_ir_op_mul:
			if (node->sched_pos == 1)
				return lima_gp_store_src_mul_0;
			else
				return lima_gp_store_src_mul_1;
			
		case lima_gp_ir_op_select:
		case lima_gp_ir_op_complex1:
		case lima_gp_ir_op_complex2:
			return lima_gp_store_src_mul_0;
			
		case lima_gp_ir_op_add:
		case lima_gp_ir_op_floor:
		case lima_gp_ir_op_sign:
		case lima_gp_ir_op_ge:
		case lima_gp_ir_op_lt:
		case lima_gp_ir_op_min:
		case lima_gp_ir_op_max:
			if (node->sched_pos == 0)
				return lima_gp_store_src_acc_0;
			else
				return lima_gp_store_src_acc_1;
			
		case lima_gp_ir_op_neg:
		{
			switch (node->sched_pos)
			{
				case 0:
					return lima_gp_store_src_acc_0;
				case 1:
					return lima_gp_store_src_acc_1;
				case 2:
					return lima_gp_store_src_mul_1;
				case 3:
					return lima_gp_store_src_mul_0;
				default:
					assert(0);
			}
		}
			
		case lima_gp_ir_op_clamp_const:
		case lima_gp_ir_op_preexp2:
		case lima_gp_ir_op_postlog2:
			return lima_gp_store_src_pass;
			
		case lima_gp_ir_op_exp2_impl:
		case lima_gp_ir_op_log2_impl:
		case lima_gp_ir_op_rcp_impl:
		case lima_gp_ir_op_rsqrt_impl:
			return lima_gp_store_src_complex;
			
		default:
			assert(0);
	}
	
	return lima_gp_store_src_none;
}

static void emit_store_slot(lima_gp_instruction_t* instr,
							lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_store_temp:
		case lima_gp_ir_op_store_varying:
		{
			lima_gp_ir_store_node_t* store_node =
				gp_ir_node_to_store(node);
			
			if (store_node->mask[0])
				instr->store0_src_x = get_store_input(store_node->children[0]);
			
			if (store_node->mask[1])
				instr->store0_src_y = get_store_input(store_node->children[1]);
			
			if (store_node->mask[2])
				instr->store1_src_z = get_store_input(store_node->children[2]);
			
			if (store_node->mask[3])
				instr->store1_src_w = get_store_input(store_node->children[3]);
			
			switch (node->op)
			{
				case lima_gp_ir_op_store_temp:
					if (store_node->mask[0] || store_node->mask[1])
						instr->store0_temporary = true;
					if (store_node->mask[2] || store_node->mask[3])
						instr->store1_temporary = true;
					instr->unknown_1 = 12;
					break;
					
				case lima_gp_ir_op_store_varying:
					if (store_node->mask[0] || store_node->mask[1])
					{
						instr->store0_varying = true;
						instr->store0_addr = store_node->index;
					}
					if (store_node->mask[2] || store_node->mask[3])
					{
						instr->store1_varying = true;
						instr->store1_addr = store_node->index;
					}
					break;
				default:
					break;
			}
			
			break;
		}
			
		case lima_gp_ir_op_store_reg:
		{
			lima_gp_ir_store_reg_node_t* store_node =
				gp_ir_node_to_store_reg(node);
			
			unsigned offset = store_node->reg->phys_reg_offset;
			
			if (offset == 0 && store_node->mask[0])
				instr->store0_src_x = get_store_input(store_node->children[0]);
			
			if (offset <= 1 && store_node->mask[1 - offset])
				instr->store0_src_y =
					get_store_input(store_node->children[1 - offset]);
			
			if (offset <= 2 && store_node->mask[2 - offset])
				instr->store1_src_z =
					get_store_input(store_node->children[2 - offset]);
			
			if (store_node->mask[3 - offset])
				instr->store1_src_w =
					get_store_input(store_node->children[3 - offset]);
			
			if ((offset == 0 && store_node->mask[0]) ||
				(offset <= 1 && store_node->mask[1 - offset]))
				instr->store0_addr = store_node->reg->phys_reg;
			if ((offset <= 2 && store_node->mask[2 - offset]) ||
				store_node->mask[3 - offset])
				instr->store1_addr = store_node->reg->phys_reg;
			
			break;
		}
			
		default:
			assert(0);
	}
}

static void emit_instr(lima_gp_instruction_t* instr,
					   lima_gp_ir_instr_t* ir_instr)
{
	static const lima_gp_instruction_t nop_instr = {
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
		.branch_target       = 0
	};
	
	*instr = nop_instr;
	
	if (ir_instr->mul_slots[0])
		emit_mul_slot_zero(instr, ir_instr->mul_slots[0]);
	if (ir_instr->mul_slots[1])
		emit_mul_slot_one(instr, ir_instr->mul_slots[1]);
	if (ir_instr->add_slots[0])
		emit_add_slot_zero(instr, ir_instr->add_slots[0]);
	if (ir_instr->add_slots[1])
		emit_add_slot_one(instr, ir_instr->add_slots[1]);
	if (ir_instr->uniform_slot_num_used)
		emit_uniform_slot(instr, ir_instr);
	if (ir_instr->attr_reg_slot_num_used)
		emit_reg_zero_slot(instr, ir_instr);
	if (ir_instr->reg_slot_num_used)
		emit_reg_one_slot(instr, ir_instr);
	if (ir_instr->branch_slot)
		emit_branch_slot(instr);
	
	unsigned i;
	for (i = 0; i < ir_instr->store_slot_num_used; i++)
		emit_store_slot(instr, ir_instr->store_slot[i]);
	
	if (ir_instr->complex_slot)
		emit_complex_slot(instr, ir_instr->complex_slot);
	if (ir_instr->pass_slot)
		emit_pass_slot(instr, ir_instr->pass_slot);
}

static bool emit_block(codegen_block_t* block,
					   lima_gp_ir_block_t* ir_block)
{
	block->instrs =
		malloc(sizeof(lima_gp_instruction_t) * ir_block->num_instrs);
	if (!block->instrs)
		return false;
	
	block->num_instrs = ir_block->num_instrs;
	block->has_branch = false;
	lima_gp_ir_instr_t* instr;
	unsigned i = 0;
	gp_ir_block_for_each_instr(ir_block, instr)
	{
		emit_instr(&block->instrs[i], instr);
		if (instr->branch_slot)
		{
			block->has_branch = true;
			lima_gp_ir_branch_node_t* branch_node =
				gp_ir_node_to_branch(instr->branch_slot);
			block->branch_dest = branch_node->dest->index;
		}
		i++;
	}
	
	return true;
}

static bool emit_program(codegen_prog_t* prog, lima_gp_ir_prog_t* ir_prog)
{
	prog->blocks = malloc(sizeof(codegen_block_t) * ir_prog->num_blocks);
	if (!prog->blocks)
		return false;
	
	prog->num_blocks = ir_prog->num_blocks;
	
	lima_gp_ir_block_t* block;
	unsigned i = 0;
	gp_ir_prog_for_each_block(ir_prog, block)
	{
		block->index = i;
		i++;
	}
	
	i = 0;
	unsigned cur_instr = 0;
	gp_ir_prog_for_each_block(ir_prog, block)
	{
		if (!emit_block(&prog->blocks[i], block))
		{
			free(prog->blocks);
			return false;
		}
		prog->blocks[i].start_instr = cur_instr;
		cur_instr += prog->blocks[i].num_instrs;
		i++;
	}
	
	return true;
}

static void delete_program(codegen_prog_t prog)
{
	unsigned i;
	for (i = 0; i < prog.num_blocks; i++)
		free(prog.blocks[i].instrs);
	
	free(prog.blocks);
}

static void fixup_branches(codegen_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		codegen_block_t* block = &prog->blocks[i];
		if (block->has_branch)
		{
			unsigned dest_instr = prog->blocks[block->branch_dest].start_instr;
			lima_gp_instruction_t* instr = &block->instrs[block->num_instrs-1];
			instr->branch_target_lo = ~(dest_instr >> 8);
			instr->branch_target = dest_instr & 0xFF;
		}
	}
}

static unsigned calc_attrib_prefetch(lima_gp_instruction_t* instrs,
									 unsigned size)
{
	unsigned num_instrs = size / sizeof(lima_gp_instruction_t);
	unsigned ret = 0, i;
	for (i = 0; i < num_instrs; i++)
		if (instrs[i].register0_attribute)
			ret = i + 1;
	return ret;
}

void* lima_gp_ir_codegen(lima_gp_ir_prog_t* ir_prog, unsigned* size,
						 unsigned* attrib_prefetch)
{
	codegen_prog_t prog;
	if (!emit_program(&prog, ir_prog))
		return false;
	
	fixup_branches(&prog);
	
	*size = 0;
	unsigned i;
	for (i = 0; i < prog.num_blocks; i++)
	{
		*size += prog.blocks[i].num_instrs * sizeof(lima_gp_instruction_t);
	}
	
	void* data = malloc(*size);
	if (!data)
		return NULL;
	
	unsigned cur_pos = 0;
	for (i = 0; i < prog.num_blocks; i++)
	{
		memcpy(data + cur_pos, prog.blocks[i].instrs,
			   prog.blocks[i].num_instrs * sizeof(lima_gp_instruction_t));
		cur_pos += prog.blocks[i].num_instrs * sizeof(lima_gp_instruction_t);
	}
	
	delete_program(prog);
	
	*attrib_prefetch = calc_attrib_prefetch((lima_gp_instruction_t*) data,
											*size);
	
	return data;
}
