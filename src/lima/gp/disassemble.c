/* Author(s):
 *   Connor Abbott
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx), Codethink (http://www.codethink.co.uk)
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

#include "lima_gp.h"
#include <stdio.h>

static void print_tabs(unsigned tabs)
{
	unsigned i;
	for (i = 0; i < tabs; i++)
		printf("\t");
}

const char* lima_gp_acc_op_name[] =
{
	"add", "floor",
	"sign", "op3",
	"ge" , "lt",
	"min", "max"
};

const char* lima_gp_acc_op_sym[] =
{
	"+",
	NULL, NULL, NULL,
	">=", "<",
	NULL, NULL
};

const char* lima_gp_complex_op_name[] =
{
	"nop", "op1", "exp2", "log2",
	"rsqrt", "rcp", "op6", "op7",
	"op8", "pass", "op10", "op11",
	"st_addr", "ld_addr_0", "ld_addr_1", "ld_addr_2"
};

const char* lima_gp_mul_op_name[] =
{
	"mul" , "complex1" , "op2" , "complex2",
	"select", "op5" , "op6" , "op7" ,
};

const char* lima_gp_pass_op_name[] =
{
	"op0", "op1", "pass", "op3",
	"op4", "op5", "clamp", "op7"
};

const char* lima_gp_src_name[] =
{
	"attribute.x", "attribute.y", "attribute.z", "attribute.w",
	"register[1].x", "register[1].y", "register[1].z", "register[1].w",
	"unknown0", "unknown1", "unknown2", "unknown3",
	"uniform.x", "uniform.y", "uniform.z", "uniform.w",
	"acc[0].out[1]", "acc[1].out[1]", "mul[0].out[1]", "mul[1].out[1]",
	"pass.out[1]", "unused", "complex.out[1]", "pass.out[2]",
	"acc[0].out[2]", "acc[1].out[2]", "mul[0].out[2]", "mul[1].out[2]",
	"attrib.x[1]", "attrib.y[1]", "attrib.z[1]", "attrib.w[1]",
};

const char* lima_gp_store_src_name[] =
{
	"acc[0].out", "acc[1].out", "mul[0].out" , "mul[1].out",
	"pass.out"  , "unknown_1"   , "complex.out", "unused",
};

const char* lima_gp_load_off_name[] =
{
	"addr[0]", "addr[1]", "addr[2]", "addr[3]",
	"unknown_4", "unknown_5", "unknown_6", "unused",
};



static const char* lima_gp__source_name(
	lima_gp_instruction_t* code,
	lima_gp_src_e src, bool ident)
{
	if (src > lima_gp_src_p1_attrib_w)
		return NULL;

	switch (src)
	{
		case lima_gp_src_ident:
			if (ident) return "ident";
			break;
		case lima_gp_src_attrib_x:
			if (!code->register0_attribute)
				return "register[0].x";
			break;
		case lima_gp_src_attrib_y:
			if (!code->register0_attribute)
				return "register[0].y";
			break;
		case lima_gp_src_attrib_z:
			if (!code->register0_attribute)
				return "register[0].z";
			break;
		case lima_gp_src_attrib_w:
			if (!code->register0_attribute)
				return "register[0].w";
			break;
		default:
			break;
	}

	return lima_gp_src_name[src];
}



void lima_gp_instruction_print_explicit(
	lima_gp_instruction_t* code, unsigned tabs)
{
	print_tabs(tabs);
	printf("{\n");


	print_tabs(tabs + 1);
	printf(".mul_op = %u\n", code->mul_op);

	if ((code->mul0_src0 != lima_gp_src_unused)
		|| (code->mul0_src1 != lima_gp_src_unused))
	{
		print_tabs(tabs + 1);
		printf("mul0 ");
		if (code->mul0_neg)
			printf("-");
		printf("%u, %u\n", code->mul0_src0, code->mul0_src1);
	}

	if ((code->mul1_src0 != lima_gp_src_unused)
		|| (code->mul1_src1 != lima_gp_src_unused))
	{
		print_tabs(tabs + 1);
		printf("mul1 ");
		if (code->mul1_neg)
			printf("-");
		printf("%u, %u\n", code->mul1_src0, code->mul1_src1);
	}

	print_tabs(tabs + 1);
	printf(".acc_op = %u\n", code->acc_op);

	if ((code->acc0_src0 != lima_gp_src_unused)
		|| (code->acc0_src1 != lima_gp_src_unused))
	{
		print_tabs(tabs + 1);
		printf("add0 ");
		if(code->acc0_src0_neg)
			printf("-");
		printf("%u, ", code->acc0_src0);
		if(code->acc0_src1_neg)
			printf("-");
		printf("%u\n", code->acc0_src1);
	}

	if ((code->acc1_src0 != lima_gp_src_unused)
		|| (code->acc1_src1 != lima_gp_src_unused))
	{
		print_tabs(tabs + 1);
		printf("add1 ");
		if(code->acc1_src0_neg)
			printf("-");
		printf("%u, ", code->acc1_src0);
		if(code->acc1_src1_neg)
			printf("-");
		printf("%u\n", code->acc1_src1);
	}

	if (code->complex_src != lima_gp_src_unused)
	{
		print_tabs(tabs + 1);
		printf(".complex_op = %u\n", code->complex_op);

		print_tabs(tabs + 1);
		printf(".complex_src = %u\n", code->complex_src);
	}

	if (code->pass_src != lima_gp_src_unused)
	{
		print_tabs(tabs + 1);
		printf(".pass_op = %u\n", code->pass_op);

		print_tabs(tabs + 1);
		printf(".pass_src = %u\n", code->pass_src);
	}

	print_tabs(tabs + 1);
	printf(".load_addr = %u\n", code->load_addr);

	print_tabs(tabs + 1);
	printf(".load_offset = %u\n", code->load_offset);

	if (code->register0_attribute)
	{
		print_tabs(tabs + 1);
		printf(".register0_attribute = 1\n");
	}

	print_tabs(tabs + 1);
	printf(".register0_addr = %u\n", code->register0_addr);

	print_tabs(tabs + 1);
	printf(".register1_addr = %u\n", code->register1_addr);

	if ((code->store0_src_x != lima_gp_store_src_none)
		|| (code->store0_src_y != lima_gp_store_src_none))
	{
		if (code->store0_varying)
		{
			print_tabs(tabs + 1);
			printf(".store0_varying = 1\n");
		}
		if (code->store0_temporary)
		{
			print_tabs(tabs + 1);
			printf(".store0_temporary = 1\n");
		}
		print_tabs(tabs + 1);
		printf(".store0_address = %u\n", code->store0_addr);

		if (code->store0_src_x != lima_gp_store_src_none)
		{
			print_tabs(tabs + 1);
			printf(".store0_src_x = %u\n", code->store0_src_x);
		}

		if (code->store0_src_y != lima_gp_store_src_none)
		{
			print_tabs(tabs + 1);
			printf(".store0_src_y = %u\n", code->store0_src_y);
		}
	}

	if ((code->store1_src_z != lima_gp_store_src_none)
		|| (code->store1_src_w != lima_gp_store_src_none))
	{
		if (code->store1_varying)
		{
			print_tabs(tabs + 1);
			printf(".store1_varying = 1\n");
		}
		if (code->store1_temporary)
		{
			print_tabs(tabs + 1);
			printf(".store1_temporary = 1\n");
		}
		print_tabs(tabs + 1);
		printf(".store1_address = %u\n", code->store1_addr);

		if (code->store1_src_z != lima_gp_store_src_none)
		{
			print_tabs(tabs + 1);
			printf(".store1_src_z = %u\n", code->store1_src_z);
		}

		if(code->store1_src_w != lima_gp_store_src_none)
		{
			print_tabs(tabs + 1);
			printf(".store1_src_w = %u\n", code->store1_src_w);
		}
	}

	if (code->branch)
	{
		print_tabs(tabs + 1);
		printf(".branch = 1\n");
		print_tabs(tabs + 1);
		printf(".branch_target_lo = %u\n", code->branch_target_lo);
		print_tabs(tabs + 1);
		printf(".branch_target = %u\n", code->branch_target);
	}

	print_tabs(tabs + 1);
	printf(".unknown_1 = %u\n", code->unknown_1);
	
	print_tabs(tabs);
	printf("}");
}

#if 0

void lima_gp_instruction_print_verbose(
	lima_gp_instruction_t* code, 
	ogt_link_map_t* map,
	unsigned tabs)
{
	print_tabs(tabs);

	bool first    = true;
	bool uniform  = false;
	bool reg_used = false;

	{
		unsigned i;
		lima_gp_src_e s[] = 
		{
			code->mul0_src0,
			code->mul0_src1,
			code->mul1_src0,
			code->mul1_src1,
			code->acc0_src0,
			code->acc0_src1,
			code->acc1_src0,
			code->acc1_src1,
			code->complex_src,
			code->pass_src,
		};
		for (i = 0; i < 10; i++)
		{
			switch (s[i])
			{
				case lima_gp_src_load_x:
				case lima_gp_src_load_y:
				case lima_gp_src_load_z:
				case lima_gp_src_load_w:
					uniform = true;
					break;
				case lima_gp_src_register_x:
				case lima_gp_src_register_y:
				case lima_gp_src_register_z:
				case lima_gp_src_register_w:
					reg_used = true;
					break;
				default:
					break;
			}
		}
	}

	/* Scan for pass.clamp op. */
	switch (code->pass_op)
	{
		case lima_gp_pass_op_clamp:
			uniform = true;
		default:
			break;
	}

	if (uniform)
	{
		first = false;

		bool temporary = false;
		if (map)
		{
			temporary = (((unsigned)code->load_addr << 2)
				>= ogt_link_map_uniform_area(map));
			/* TODO - Resolve vector name where possible. */
		}

		if (temporary)
			printf("temporary");
		else
			printf("uniform");

		printf(".load(%u", code->load_addr);
		if (code->load_offset != lima_gp_load_off_none)
			printf(", %s",
				lima_gp_load_off_name[code->load_offset]);
		printf(")");
	}

	if (code->register0_attribute)
	{
		if (!first) printf(", ");
		else first = false;

		printf("attribute.load(%u)", code->register0_addr);
	}
	else if (reg_used
		|| (code->register0_addr != 0))
	{
		if (!first) printf(", ");
		else first = false;

		printf("register[0].load(%u)", code->register0_addr);
	}

	if (reg_used)
	{
		if (!first) printf(", ");
		else first = false;

		printf("register[1].load(%u)", code->register1_addr);
	}
	
	switch (code->mul_op)
	{
		case lima_gp_mul_op_mul:
		case lima_gp_mul_op_complex2:
		{
			if ((code->mul0_src0 != lima_gp_src_unused)
				|| (code->mul0_src1 != lima_gp_src_unused))
			{
				if (!first) printf(", ");
				else first = false;

				const char* src0_name
					= lima_gp__source_name(code, code->mul0_src0, false);

				if ((code->mul_op == lima_gp_mul_op_mul)
					&& (code->mul0_src1 == lima_gp_src_ident))
				{
					printf("mul[0].pass(%s)", src0_name);
				} else {
					printf("mul[0].%s(%s, %s%s)",
						lima_gp_mul_op_name[code->mul_op],
						src0_name,
						(code->mul0_neg ? "-" : ""),
						lima_gp__source_name(code, code->mul0_src1, true));
				}
			}

			if ((code->mul1_src0 != lima_gp_src_unused)
				|| (code->mul1_src1 != lima_gp_src_unused))
			{
				if (!first) printf(", ");
				else first = false;

				const char* src0_name
					= lima_gp__source_name(code, code->mul1_src0, false);

				if (code->mul1_src1 == lima_gp_src_ident)
				{
					printf("mul[1].pass(%s)", src0_name);
				} else {
					printf("mul[1].mul(%s, %s%s)",
						src0_name,
						(code->mul1_neg ? "-" : ""),
						lima_gp__source_name(code, code->mul1_src1, true));
				}
			}
		} break;
		case lima_gp_mul_op_select:
		{
			if (!first) printf(", ");
			else first = false;

			printf("mul.%s(%s%s, %s, %s)",
				lima_gp_mul_op_name[code->mul_op],
				(code->mul0_neg ? "-" : ""),
				lima_gp__source_name(code, code->mul0_src1, true),
				lima_gp__source_name(code, code->mul0_src0, false),
				lima_gp__source_name(code, code->mul1_src0, false));
		} break;
		default:
		{
			if (!first) printf(", ");
			else first = false;

			printf("mul.%s(%s, %s%s, %s, %s%s)",
				lima_gp_mul_op_name[code->mul_op],
				lima_gp__source_name(code, code->mul0_src0, false),
				(code->mul0_neg ? "-" : ""),
				lima_gp__source_name(code, code->mul0_src1, true),
				lima_gp__source_name(code, code->mul1_src0, false),
				(code->mul1_neg ? "-" : ""),
				lima_gp__source_name(code, code->mul1_src1, true));
		} break;
	}

	if ((code->acc0_src0 != lima_gp_src_unused)
		|| (code->acc0_src1 != lima_gp_src_unused))
	{
		if (!first) printf(", ");
		else first = false;

		if ((code->acc_op == lima_gp_acc_op_add)
			&& code->acc0_src1_neg
			&& (code->acc0_src1 == lima_gp_src_ident))
		{
			printf("acc[0].pass(%s%s)",
				(code->acc0_src0_neg ? "-" : ""),
				lima_gp__source_name(code, code->acc0_src0, false));
		}
		else
		{
			printf("acc[0].%s(%s%s, %s%s)",
				lima_gp_acc_op_name[code->acc_op],
				(code->acc0_src0_neg ? "-" : ""),
				lima_gp__source_name(code, code->acc0_src0, false),
				(code->acc0_src1_neg ? "-" : ""),
				lima_gp__source_name(code, code->acc0_src1, true));
		}
	}

	if ((code->acc1_src0 != lima_gp_src_unused)
		|| (code->acc1_src1 != lima_gp_src_unused))
	{
		if (!first) printf(", ");
		else first = false;

		if ((code->acc_op == lima_gp_acc_op_add)
			&& code->acc1_src1_neg
			&& (code->acc1_src1 == lima_gp_src_ident))
		{
			printf("acc[1].pass(%s%s)",
				(code->acc1_src0_neg ? "-" : ""),
				lima_gp__source_name(code, code->acc1_src0, false));
		}
		else
		{
			printf("acc[1].%s(%s%s, %s%s)",
				lima_gp_acc_op_name[code->acc_op],
				(code->acc1_src0_neg ? "-" : ""),
				lima_gp__source_name(code, code->acc1_src0, false),
				(code->acc1_src1_neg ? "-" : ""),
				lima_gp__source_name(code, code->acc1_src1, true));
		}
	}


	if (code->complex_src != lima_gp_src_unused)
	{
		if (!first) printf(", ");
		else first = false;

		printf("complex.%s(%s)",
			lima_gp_complex_op_name[code->complex_op],
			lima_gp__source_name(code, code->complex_src, false));
	}

	if (code->pass_src != lima_gp_src_unused)
	{
		if (!first) printf(", ");
		else first = false;

		printf("pass.%s(%s)",
			lima_gp_pass_op_name[code->pass_op],
			lima_gp__source_name(code, code->pass_src, false));
	}

	if ((code->store0_src_x != lima_gp_store_src_none)
		|| (code->store0_src_y != lima_gp_store_src_none))
	{
		if (!first) printf(", ");
		else first = false;

		printf("store[0].");

		if (code->store0_varying)
			printf("varying");
		else if (code->store0_temporary)
			printf("temporary");
		else
			printf("register");

		printf("(%u, %s, %s)",
			code->store0_addr,
			lima_gp_store_src_name[code->store0_src_x],
			lima_gp_store_src_name[code->store0_src_y]);
	}

	if ((code->store1_src_z != lima_gp_store_src_none)
		|| (code->store1_src_w != lima_gp_store_src_none))
	{
		if (!first) printf(", ");
		else first = false;

		printf("store[1].");

		if (code->store1_varying)
			printf("varying");
		else if (code->store1_temporary)
			printf("temporary");
		else
			printf("register");
			
		printf("(%u, %s, %s)",
			code->store1_addr,
			lima_gp_store_src_name[code->store1_src_z],
			lima_gp_store_src_name[code->store1_src_w]);
	}

	if (code->branch)
	{
		if (!first) printf(", ");
		else first = false;

		unsigned branch_target
			= code->branch_target;
		if (!code->branch_target_lo)
			branch_target += 0x100;
		printf("branch(%u)", branch_target);
	}

	if (code->unknown_1)
	{
		if (!first) printf(", ");
		else first = false;

		printf("unknown_1(%u)", code->unknown_1);
	}

	printf(";");
}



bool lima_gp_disassemble(
	void* code, unsigned size,
	ogt_link_map_t* map,
	ogt_asm_type_e type,
	ogt_asm_syntax_e syntax,
	unsigned tabs)
{
	(void)type; /* Not used. */

	if (size % sizeof(lima_gp_instruction_t))
	{
		fprintf(stderr, "Error: Invalid vertex assembly"
			" (size must be multiple of 4 (32-bit) words)\n");
		return false;
	}

	/* Print symbol table. */
	essl_program_t* program
		= ogt_link_map_export_essl(map);
	if (program)
	{
		essl_program_print(program, stdout, tabs);
		printf("\n");
	}

	lima_gp_instruction_t* icode
		= (lima_gp_instruction_t*)code;
	size /= sizeof(lima_gp_instruction_t);

	switch (syntax)
	{
		case ogt_asm_syntax_explicit:
		{
			unsigned i;
			for (i = 0; i < size; i++)
			{
				lima_gp_instruction_print_explicit(&icode[i], tabs);
				printf("\n");
			}
		} break;
		case ogt_asm_syntax_verbose:
		{
			unsigned i;
			for (i = 0; i < size; i++)
			{
				lima_gp_instruction_print_verbose(&icode[i], map, tabs);
				printf("\n");
			}
		} break;
		case ogt_asm_syntax_decompile:
		{
			print_tabs(tabs); printf("void main()\n");
			print_tabs(tabs); printf("{\n");
			tabs++;

			unsigned i;
			for (i = 0; i < size; i++)
				lima_gp_instruction_print_decompile(
					icode, i, map, tabs);

			tabs--;
			print_tabs(tabs); printf("}\n");
		} break;
		default:
			return false;
	}
	
	return true;
}

#endif
