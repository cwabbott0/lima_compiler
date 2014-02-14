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



#include "assemble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static bool lima_gp_src__encode_alu(
	lima_gp_src_t* src, lima_gp_src_e* encode,
	bool complex)
{
	if (!src) return false;

	if (src->reg == lima_gp_reg_unused)
	{
		if (encode)
			*encode = lima_gp_src_unused;
		return true;
	}

	if (src->reg == lima_gp_reg_ident)
	{
		if (complex) return false;
		if (encode) *encode = lima_gp_src_ident;
		return true;
	}

	bool is_vec = false;
	switch (src->reg)
	{
		case lima_gp_reg_x:
		case lima_gp_reg_y:
		case lima_gp_reg_z:
		case lima_gp_reg_w:
			is_vec = true;
			break;
		default:
			break;
	}
	unsigned vec = (src->reg - lima_gp_reg_x);

	lima_gp_src_e s;
	switch (src->unit.unit)
	{
		case lima_gp_fu_multiply:
			if ((src->reg != lima_gp_reg_out)
				|| (src->time <= 0) || (src->time > 2)
				|| (src->unit.index > 1))
				return false;
			s = lima_gp_src_p1_mul_0;
			if (src->unit.index) s += 1;
			if (src->time > 1)   s += 8;
			break;
		case lima_gp_fu_accumulate:
			if ((src->reg != lima_gp_reg_out)
				|| (src->time <= 0) || (src->time > 2)
				|| (src->unit.index > 1))
				return false;
			s = lima_gp_src_p1_acc_0;
			if (src->unit.index) s += 1;
			if (src->time > 1)   s += 8;
			break;
		case lima_gp_fu_pass:
			if ((src->reg != lima_gp_reg_out)
				|| (src->time <= 0) || (src->time > 2)
				|| (src->unit.index != 0))
				return false;
			s = lima_gp_src_p1_pass;
			if (src->time > 1) s += 3;
			break;
		case lima_gp_fu_complex:
			if ((src->reg != lima_gp_reg_out)
				|| !complex || (src->time != 1)
				|| (src->unit.index != 0))
				return false;
			s = lima_gp_src_p1_complex;
			break;
		case lima_gp_fu_uniform:
		case lima_gp_fu_temporary:
			if (!is_vec || (src->time != 0)
				|| (src->unit.index != 0))
				return false;
			s = lima_gp_src_load_x + vec;
			break;
		case lima_gp_fu_attribute:
			if (!is_vec || (src->time > 1)
				|| (src->unit.index != 0))
				return false;
			s = lima_gp_src_attrib_x + vec;
			if (src->time) s += 28;
			break;
		case lima_gp_fu_register:
			if (!is_vec) return false;
			if (src->unit.index == 0)
			{
				if (src->time > 1) return false;
				s = lima_gp_src_attrib_x + vec;
				if (src->time) s += 28;
			}
			else if (src->unit.index == 1)
			{
				if (src->time != 0) return false;
				s = lima_gp_src_register_x + vec;
			}
			else
			{
				return false;
			}
			break;
		default:
			return false;
	}
	
	if (encode) *encode = s;
	return true;
}

static bool lima_gp_src__encode_store(
	lima_gp_src_t* src, lima_gp_store_src_e* encode)
{
	if (!src) return false;

	if (src->reg == lima_gp_reg_unused)
	{
		if (encode)
			*encode = lima_gp_store_src_none;
		return true;
	}

	if (src->reg != lima_gp_reg_out) return false;
	if (src->time != 0) return false;

	lima_gp_src_e s;
	switch (src->unit.unit)
	{
		case lima_gp_fu_multiply:
			if (src->unit.index > 1)
				return false;
			s = lima_gp_store_src_mul_0;
			if (src->unit.index) s++;
			break;
		case lima_gp_fu_accumulate:
			if (src->unit.index > 1)
				return false;
			s = lima_gp_store_src_acc_0;
			if (src->unit.index) s++;
			break;
		case lima_gp_fu_pass:
			if (src->unit.index != 0)
				return false;
			s = lima_gp_store_src_pass;
			break;
		case lima_gp_fu_complex:
			if (src->unit.index != 0)
				return false;
			s = lima_gp_store_src_complex;
			break;
		default:
			return false;
	}
	
	if (encode) *encode = s;
	return true;
}

static bool lima_gp_src__encode_load(
	lima_gp_src_t* src, lima_gp_load_off_t* encode)
{
	if (!src) return false;

	if (src->reg == lima_gp_reg_unused)
	{
		if (encode)
			*encode = lima_gp_load_off_none;
		return true;
	}

	if ((src->reg != lima_gp_reg_out)
		|| (src->unit.unit != lima_gp_fu_complex)
		|| (src->reg != lima_gp_reg_addr)
		|| (src->unit.index == 0)
		|| (src->time == 0)
		|| (src->time > 3))
		return false;
	
	if (encode) *encode = src->time;
	return true;
}



static bool lima_gp_assemble_field__mul(
	unsigned index, lima_gp_op_t op,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_src_t* c, lima_gp_src_t* d,
	lima_gp_instruction_partial_t* partial)
{
	if (index > 1)
	{
		fprintf(stderr, "Error: Invalid mul unit (must be 0 or 1).\n");
		return false;
	}

	unsigned args;
	switch (op.op)
	{
		case lima_gp_op_neg:
			op.op  = lima_gp_op_pass;
			op.neg = !op.neg;
		case lima_gp_op_pass:
			args = 1;
			break;
		case lima_gp_op_mul:
			args = 2;
			break;
		case lima_gp_op_select:
			args = 3;
			break;
		case lima_gp_op_complex1:
		case lima_gp_op_complex2:
			args = 4;
			break;
		default:
			fprintf(stderr, "Error: Invalid operation for mul unit.\n");
			return false;
	}

	if (((args < 2) && b)
		|| ((args < 3) && c)
		|| ((args < 4) && d))
	{
		fprintf(stderr, "Error: Too many arguments for operation.\n");
		return false;
	}

	if (args > 2)
	{
		if (op.neg)
		{
			fprintf(stderr, "Error: Invalid operation for mul to negate.\n");
			return false;
		}
		if (index != 0)
		{
			fprintf(stderr, "Error: Cannot schedule wide"
				" operation on sub-unit.\n");
			return false;
		}
	}

	lima_gp_src_e s[4];
	if (!lima_gp_src__encode_alu(a, &s[0], true))
	{
		fprintf(stderr, "Error: Invalid input field a to mul unit.\n");
		return false;
	}
	if ((args >= 2) && !lima_gp_src__encode_alu(b, &s[1], false))
	{
		fprintf(stderr, "Error: Invalid input field b to mul unit.\n");
		return false;
	}
	if ((args >= 3) && !lima_gp_src__encode_alu(c, &s[2], true))
	{
		fprintf(stderr, "Error: Invalid input field c to mul unit.\n");
		return false;
	}
	if ((args >= 4) && !lima_gp_src__encode_alu(d, &s[3], false))
	{
		fprintf(stderr, "Error: Invalid input field d to mul unit.\n");
		return false;
	}

	switch (op.op)
	{
		case lima_gp_op_mul:
			if (b->neg) op.neg = !op.neg;
		case lima_gp_op_pass:
			if (a->neg) op.neg = !op.neg;
			break;
		default:
			if ((a && a->neg)
				|| (b && b->neg)
				|| (c && c->neg)
				|| (d && d->neg))
			{
				fprintf(stderr, "Error: Cannot negate fields in"
					" mul unit operation.\n");
				return false;
			}
			break;
	}

	lima_gp_mul_op_e o;
	switch (op.op)
	{
		case lima_gp_op_pass:
			s[1] = lima_gp_src_ident;
		case lima_gp_op_mul:
			o = lima_gp_mul_op_mul;
			break;
		case lima_gp_op_complex1:
			o = lima_gp_mul_op_complex1;
			break;
		case lima_gp_op_complex2:
			o = lima_gp_mul_op_complex2;
			break;
		case lima_gp_op_select:
			o = lima_gp_mul_op_select;
			s[3] = lima_gp_src_unused;
			break;
		default:
			return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	ninst.mul_op = o; nmask.mul_op = 0x07;
	if (args <= 2)
	{
		if (index == 0)
		{
			ninst.mul0_src0 = s[0]; nmask.mul0_src0 = 0x1F;
			ninst.mul0_src1 = s[1]; nmask.mul0_src1 = 0x1F;
			ninst.mul0_neg = op.neg; nmask.mul0_neg = 0x01;
		} else {
			ninst.mul1_src0 = s[0]; nmask.mul1_src0 = 0x1F;
			ninst.mul1_src1 = s[1]; nmask.mul1_src1 = 0x1F;
			ninst.mul1_neg = op.neg; nmask.mul1_neg = 0x01;
		}
	} else {
		ninst.mul0_src0 = s[0]; nmask.mul0_src0 = 0x1F;
		ninst.mul0_src1 = s[1]; nmask.mul0_src1 = 0x1F;
		ninst.mul1_src0 = s[2]; nmask.mul1_src0 = 0x1F;
		ninst.mul1_src1 = s[3]; nmask.mul1_src1 = 0x1F;
		ninst.mul0_neg = false; nmask.mul0_neg = 0x01;
		ninst.mul1_neg = false; nmask.mul1_neg = 0x01;
	}

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}

static bool lima_gp_assemble_field__acc(
	unsigned index, lima_gp_op_t op,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_src_t* c, lima_gp_src_t* d,
	lima_gp_instruction_partial_t* partial)
{
	if (c || d)
	{
		fprintf(stderr, "Error: Too many arguments to"
			" acc unit (expects < 2).\n");
		return false;
	}

	bool unary = false;
	switch (op.op)
	{
		case lima_gp_op_pass:
		case lima_gp_op_neg:
		case lima_gp_op_floor:
		case lima_gp_op_sign:
		case lima_gp_op_abs:
			if (c)
			{
				fprintf(stderr, "Error: Too many arguments in"
					" unary operation.\n");
				return false;
			}
			unary = true;
			break;
		default:
			break;
	}

	lima_gp_src_e s[2];
	bool n[2];

	if (!lima_gp_src__encode_alu(a, &s[0], true))
	{
		fprintf(stderr, "Error: Invalid input field a to acc unit.\n");
		return false;
	}
	n[0] = a->neg;

	if (!unary)
	{
		if(!lima_gp_src__encode_alu(b, &s[1], false))
		{
			fprintf(stderr, "Error: Invalid input field b to acc unit.\n");
			return false;
		}
		n[1] = b->neg;
	}

	if (op.neg)
	{
		switch (op.op)
		{
			case lima_gp_op_neg:
				op.op = lima_gp_op_pass;
				break;
			case lima_gp_op_pass:
				op.op = lima_gp_op_neg;
				break;
			case lima_gp_op_sub:
			case lima_gp_op_add:
				n[0] = !n[0];
				n[1] = !n[1];
				break;
			case lima_gp_op_sign:
				n[0] = !n[0];
				break;
			case lima_gp_op_min:
				op.op = lima_gp_acc_op_max;
				n[0] = !n[0];
				n[1] = !n[1];
				break;
			case lima_gp_op_max:
				op.op = lima_gp_acc_op_min;
				n[0] = !n[0];
				n[1] = !n[1];
				break;
			case lima_gp_op_abs:
				op.op = lima_gp_op_nabs;
				break;
			case lima_gp_op_nabs:
				op.op = lima_gp_op_abs;
				break;
			default:
				fprintf(stderr, "Error: Invalid cannot to negate"
					" operation on acc unit.\n");
				break;
		}
	}

	lima_gp_acc_op_e o;
	switch (op.op)
	{
		case lima_gp_op_neg:
			n[0] = !n[0];
		case lima_gp_op_pass:
			o    = lima_gp_acc_op_add;
			s[1] = lima_gp_src_ident;
			n[1] = true;
			break;
		case lima_gp_op_sub:
			n[1] = !n[1];
		case lima_gp_op_add:
			o = lima_gp_acc_op_add;
			break;
		case lima_gp_op_floor:
			o    = lima_gp_acc_op_floor;
			s[1] = lima_gp_src_unused;
			n[1] = false;
			break;
		case lima_gp_op_sign:
			o    = lima_gp_acc_op_sign;
			s[1] = lima_gp_src_unused;
			n[1] = false;
			break;
		case lima_gp_op_ge:
			o = lima_gp_acc_op_ge;
			break;
		case lima_gp_op_lt:
			o = lima_gp_acc_op_lt;
			break;
		case lima_gp_op_min:
			o = lima_gp_acc_op_min;
			break;
		case lima_gp_op_max:
			o = lima_gp_acc_op_max;
			break;
		case lima_gp_op_abs:
			o = lima_gp_acc_op_max;
			s[1] = s[0];
			n[1] = !n[0];
			break;
		case lima_gp_op_nabs:
			o = lima_gp_acc_op_min;
			s[1] = s[0];
			n[1] = !n[0];
			break;
		default:
			fprintf(stderr, "Error: Invalid operation for acc unit.\n");
			return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	ninst.acc_op = o; nmask.acc_op = 0x07;
	if (index == 0)
	{
		ninst.acc0_src0 = s[0]; nmask.acc0_src0 = 0x1F;
		ninst.acc0_src1 = s[1]; nmask.acc0_src1 = 0x1F;
		ninst.acc0_src0_neg = n[0]; nmask.acc0_src0_neg = 0x01;
		ninst.acc0_src1_neg = n[1]; nmask.acc0_src1_neg = 0x01;
	} else {
		ninst.acc1_src0 = s[0]; nmask.acc1_src0 = 0x1F;
		ninst.acc1_src1 = s[1]; nmask.acc1_src1 = 0x1F;
		ninst.acc1_src0_neg = n[0]; nmask.acc1_src0_neg = 0x01;
		ninst.acc1_src1_neg = n[1]; nmask.acc1_src1_neg = 0x01;
	}

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}



static bool lima_gp_assemble_field__complex(
	lima_gp_op_t op,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_src_t* c, lima_gp_src_t* d,
	lima_gp_instruction_partial_t* partial)
{
	if (b || c || d)
	{
		fprintf(stderr, "Error: Too many arguments for"
			" complex unit (expects 1).\n");
		return false;
	}

	lima_gp_src_e s;
	if (!lima_gp_src__encode_alu(a, &s, true))
	{
		fprintf(stderr, "Error: Invalid input field to complex unit.\n");
		return false;
	}
	if (a->neg || op.neg)
	{
		fprintf(stderr, "Error: Negation not supported in complex unit.\n");
		return false;
	}

	lima_gp_complex_op_e o;
	switch (op.op)
	{
		case lima_gp_op_nop:
			o = lima_gp_complex_op_nop;
			break;
		case lima_gp_op_exp2:
			o = lima_gp_complex_op_exp2;
			break;
		case lima_gp_op_log2:
			o = lima_gp_complex_op_log2;
			break;
		case lima_gp_op_rsqrt:
			o = lima_gp_complex_op_rsqrt;
			break;
		case lima_gp_op_rcp:
			o = lima_gp_complex_op_rcp;
			break;
		case lima_gp_op_pass:
			o = lima_gp_complex_op_pass;
			break;
		case lima_gp_op_temp_store_addr:
			o = lima_gp_complex_op_temp_store_addr;
			break;
		case lima_gp_op_temp_load_addr_0:
			o = lima_gp_complex_op_temp_load_addr_0;
			break;
		case lima_gp_op_temp_load_addr_1:
			o = lima_gp_complex_op_temp_load_addr_1;
			break;
		case lima_gp_op_temp_load_addr_2:
			o = lima_gp_complex_op_temp_load_addr_2;
			break;
		default:
			fprintf(stderr, "Error: Invalid operation for complex unit.\n");
			return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	ninst.complex_op  = o; nmask.complex_op  = 0x0F;
	ninst.complex_src = s; nmask.complex_src = 0x1F;

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}



static bool lima_gp_assemble_field__pass(
	lima_gp_op_t op,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_src_t* c, lima_gp_src_t* d,
	lima_gp_instruction_partial_t* partial)
{
	if (b || c || d)
	{
		fprintf(stderr, "Error: Too many arguments"
			" to pass unit (expects 1).\n");
		return false;
	}

	lima_gp_src_e s;
	if (!lima_gp_src__encode_alu(a, &s, true))
	{
		fprintf(stderr, "Error: Invalid input field to pass unit.\n");
		return false;
	}
	if (a->neg || op.neg)
	{
		fprintf(stderr, "Error: Negation not supported in pass unit.\n");
		return false;
	}

	lima_gp_pass_op_e o;
	switch (op.op)
	{
		case lima_gp_op_pass:
			o = lima_gp_pass_op_pass;
			break;
		case lima_gp_op_clamp:
			o = lima_gp_pass_op_clamp;
			break;
		default:
			fprintf(stderr, "Error: Invalid operation for pass unit.\n");
			return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	ninst.pass_op  = o; nmask.pass_op  = 0x07;
	ninst.pass_src = s; nmask.pass_src = 0x1F;

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}



bool lima_gp_instruction_partial_merge(
	lima_gp_instruction_partial_t* dst,
	lima_gp_instruction_partial_t a,
	lima_gp_instruction_partial_t b)
{
	uint32_t *ma, *mb;
	ma = (uint32_t*)&a.mask;
	mb = (uint32_t*)&b.mask;

	uint32_t *ia, *ib;
	ia = (uint32_t*)&a.inst;
	ib = (uint32_t*)&b.inst;

	unsigned i;
	for (i = 0; i < 4; i++)
	{
		uint32_t mu = (ma[i] & mb[i]);
		if ((ib[i] & mu) || (ib[i] &mu))
			return false;
	}

	if (dst)
	{
		dst->inst = lima_gp_instruction_default;

		uint32_t* md = (uint32_t*)&dst->mask;
		uint32_t* id = (uint32_t*)&dst->inst;
		for (i = 0; i < 4; i++)
		{
			md[i]  = (ma[i] | mb[i]);
			id[i] &= ~md[i];
			id[i] |= (ia[i] & ma[i]);
			id[i] |= (ib[i] & mb[i]);
		}
	}
	return true;
}



bool lima_gp_assemble_field(
	lima_gp_fu_t unit, lima_gp_op_t op,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_src_t* c, lima_gp_src_t* d,
	lima_gp_instruction_partial_t* partial)
{
	switch (unit.unit)
	{
		case lima_gp_fu_multiply:
			if (unit.index > 1)
				return false;
			return lima_gp_assemble_field__mul(
				unit.index, op, a, b, c, d, partial);
			break;
		case lima_gp_fu_accumulate:
			if (unit.index > 1)
				return false;
			return lima_gp_assemble_field__acc(
				unit.index, op, a, b, c, d, partial);
			break;
		case lima_gp_fu_complex:
			if (unit.index != 0)
				return false;
			return lima_gp_assemble_field__complex(
				op, a, b, c, d, partial);
		case lima_gp_fu_pass:
			if (unit.index != 0)
				return false;
			return lima_gp_assemble_field__pass(
				op, a, b, c, d, partial);
		default:
			break;
	}

	fprintf(stderr, "Error: Invalid functional unit.\n");
	return false;
}


bool lima_gp_assemble_field_load(
	unsigned address, lima_gp_src_t* offset,
	lima_gp_instruction_partial_t* partial)
{
	if (address > 0x1FF)
	{
		fprintf(stderr, "Error: Load address out of range (>= 512).\n");
		return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	lima_gp_load_off_t s
		= lima_gp_load_off_none;
	if (offset && !lima_gp_src__encode_load(offset, &s))
	{
		fprintf(stderr, "Error: Invalid input in load offset field.\n");
		return false;
	}

	ninst.load_addr   = address; nmask.load_addr   = 0x1FF;
	ninst.load_offset = s;       nmask.load_offset = 0x7;

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}

bool lima_gp_assemble_field_store(
	unsigned unit, lima_gp_op_t op, unsigned addr,
	lima_gp_src_t* a, lima_gp_src_t* b,
	lima_gp_instruction_partial_t* partial)
{
	if (unit > 1)
	{
		fprintf(stderr, "Error: Invalid store unit index (must be 0 or 1).\n");
		return false;
	}

	if (op.neg
		|| (a && a->neg)
		|| (b && b->neg))
	{
		fprintf(stderr, "Error: Negation not supported in store unit.\n");
		return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	switch (op.op)
	{
		case lima_gp_op_store_register:
		case lima_gp_op_store_varying:
		case lima_gp_op_store_temporary:
			break;
		default:
			fprintf(stderr, "Error: Invalid operation for store unit.\n");
			return false;
	}

	lima_gp_store_src_e s[2] =
	{
		lima_gp_store_src_none,
		lima_gp_store_src_none,
	};

	if (a && !lima_gp_src__encode_store(a, &s[0]))
	{
		fprintf(stderr, "Error: Invalid input in store field a.\n");
		return false;
	}
	if (b && !lima_gp_src__encode_store(b, &s[1]))
	{
		fprintf(stderr, "Error: Invalid input in store field b.\n");
		return false;
	}

	if ((s[0] == lima_gp_store_src_none)
		&& (s[1] == lima_gp_store_src_none))
		return true;

	if (addr >= 16)
	{
		fprintf(stderr, "Error: Invalid store address.\n");
		return false;
	}


	bool temporary
		= (op.op == lima_gp_op_store_temporary);
	bool varying
		= (op.op == lima_gp_op_store_varying);
	if (unit == 0)
	{
		ninst.store0_temporary = temporary; nmask.store0_temporary = 0x01;
		ninst.store0_varying   = varying;   nmask.store0_varying   = 0x01;
		ninst.store0_addr      = addr;      nmask.store0_addr      = 0x0F;
		ninst.store0_src_x     = s[0];      nmask.store0_src_x     = 0x07;
		ninst.store0_src_y     = s[1];      nmask.store0_src_y     = 0x07;
	} else {
		ninst.store1_temporary = temporary; nmask.store1_temporary = 0x01;
		ninst.store1_varying   = varying;   nmask.store1_varying   = 0x01;
		ninst.store1_addr      = addr;      nmask.store1_addr      = 0x0F;
		ninst.store1_src_z     = s[0];      nmask.store1_src_z     = 0x07;
		ninst.store1_src_w     = s[1];      nmask.store1_src_w     = 0x07;
	}
	if (temporary)
	{
		ninst.unknown_1 = 12;
		nmask.unknown_1 = 12;
	}

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}

bool lima_gp_assemble_field_register(
	unsigned unit, unsigned index, bool attribute,
	lima_gp_instruction_partial_t* partial)
{
	if (unit > 1)
	{
		fprintf(stderr, "Error: Invalid register unit (Must be 0 or 1).\n");
		return false;
	}
	if (attribute && (unit != 0))
	{
		fprintf(stderr, "Error: Invalid attribute unit (only one).\n");
		return false;
	}
	if (index >= 16)
	{
		fprintf(stderr, "Error: Register/attribute index"
			" out of range (>= 16).\n");
		return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	if (unit == 0)
	{
		ninst.register0_addr      = index;     nmask.register0_addr      = 0xF;
		ninst.register0_attribute = attribute; nmask.register0_attribute = 0x1;
	} else {
		ninst.register1_addr = index; nmask.register1_addr = 0xF;		
	}

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}

bool lima_gp_assemble_field_branch(unsigned target,
	lima_gp_instruction_partial_t* partial)
{
	if (target > 0x1FF)
	{
		fprintf(stderr, "Error: Can't branch past address 0x1FF.\n");
		return false;
	}

	lima_gp_instruction_t ninst
		= lima_gp_instruction_default;
	lima_gp_instruction_t nmask;
	memset(&nmask, 0x00, sizeof(lima_gp_instruction_t));

	ninst.branch           = true;             nmask.branch           = 0x01;
	ninst.branch_target_lo = (target < 0x100); nmask.branch_target_lo = 0x01;
	ninst.branch_target    = (target & 0x0FF); nmask.branch_target    = 0xFF;
	ninst.unknown_1        = 13;               nmask.unknown_1        = 13;

	if (partial)
	{
		partial->inst = ninst;
		partial->mask = nmask;
	}
	return true;
}
