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



#include "lima_gp.h"
#include <stdio.h>

#if 0

typedef enum
{
	lima_gp_src_simple_unused,
	lima_gp_src_simple_ident,
	lima_gp_src_simple_attrib_x,
	lima_gp_src_simple_attrib_y,
	lima_gp_src_simple_attrib_z,
	lima_gp_src_simple_attrib_w,
	lima_gp_src_simple_register_x,
	lima_gp_src_simple_register_y,
	lima_gp_src_simple_register_z,
	lima_gp_src_simple_register_w,
	lima_gp_src_simple_uniform_x,
	lima_gp_src_simple_uniform_y,
	lima_gp_src_simple_uniform_z,
	lima_gp_src_simple_uniform_w,
	lima_gp_src_simple_acc_0,
	lima_gp_src_simple_acc_1,
	lima_gp_src_simple_mul_0,
	lima_gp_src_simple_mul_1,
	lima_gp_src_simple_pass,
	lima_gp_src_simple_complex,
	lima_gp_src_simple_unknown,
} lima_gp_src_simple_e;

typedef struct
{
	lima_gp_src_simple_e src;
	unsigned             off;
} lima_gp_src_simple_map_e;

static lima_gp_src_simple_map_e lima_gp_src_simple_map[] =
{
	{ lima_gp_src_simple_attrib_x  , 0 },
	{ lima_gp_src_simple_attrib_y  , 0 },
	{ lima_gp_src_simple_attrib_z  , 0 },
	{ lima_gp_src_simple_attrib_w  , 0 },
	{ lima_gp_src_simple_register_x, 0 },
	{ lima_gp_src_simple_register_y, 0 },
	{ lima_gp_src_simple_register_z, 0 },
	{ lima_gp_src_simple_register_w, 0 },
	{ lima_gp_src_simple_unknown   , 0 },
	{ lima_gp_src_simple_unknown   , 0 },
	{ lima_gp_src_simple_unknown   , 0 },
	{ lima_gp_src_simple_unknown   , 0 },
	{ lima_gp_src_simple_uniform_x , 0 },
	{ lima_gp_src_simple_uniform_y , 0 },
	{ lima_gp_src_simple_uniform_z , 0 },
	{ lima_gp_src_simple_uniform_w , 0 },
	{ lima_gp_src_simple_acc_0     , 1 },
	{ lima_gp_src_simple_acc_1     , 1 },
	{ lima_gp_src_simple_mul_0     , 1 },
	{ lima_gp_src_simple_mul_1     , 1 },
	{ lima_gp_src_simple_pass      , 1 },
	{ lima_gp_src_simple_unused    , 0 },
	{ lima_gp_src_simple_ident     , 0 },
	{ lima_gp_src_simple_pass      , 2 },
	{ lima_gp_src_simple_acc_0     , 2 },
	{ lima_gp_src_simple_acc_1     , 2 },
	{ lima_gp_src_simple_mul_0     , 2 },
	{ lima_gp_src_simple_mul_1     , 2 },
	{ lima_gp_src_simple_attrib_x  , 1 },
	{ lima_gp_src_simple_attrib_y  , 1 },
	{ lima_gp_src_simple_attrib_z  , 1 },
	{ lima_gp_src_simple_attrib_w  , 1 },
};

static lima_gp_src_simple_map_e lima_gp_src_simple_map_reg[] =
{
	{ lima_gp_src_simple_acc_0  , 0 },
	{ lima_gp_src_simple_acc_1  , 0 },
	{ lima_gp_src_simple_mul_0  , 0 },
	{ lima_gp_src_simple_mul_1  , 0 },
	{ lima_gp_src_simple_pass   , 0 },
	{ lima_gp_src_simple_unknown, 0 },
	{ lima_gp_src_simple_complex, 0 },
	{ lima_gp_src_simple_unused , 0 },
};

static bool lima_gp__mul_op_full[] =
{
	0, 1, 1, 0,
	1, 1, 1, 1,
	1, 1, 1, 1,
	1, 1, 1, 1,
};



static void lima_gp__varying_print(ogt_link_map_t* map, unsigned index, unsigned offset)
{
	unsigned o = (index << 2) + offset;
	unsigned i, c;
	essl_symbol_t* s
		= ogt_link_map_reference(
			map, essl_storage_qualifier_varying, o,
			&i, &c);

	if (!s)
		printf("varying[%u].%c",
			index, "xyzw"[offset]);
	else
	{
		printf("%s", s->name);
		if (s->count != 0)
			printf("[%u]", i);

		const char* cname
			= essl_symbol_component_name(s, c);
		if (cname) printf(".%s", cname);
	}
}

static void lima_gp__uniform_print(
	ogt_link_map_t* map,
	unsigned index, unsigned component, lima_gp_load_off_t offset_reg)
{
	unsigned o = (index << 2) + component;
	unsigned i, c;
	essl_symbol_t* s
		= ogt_link_map_reference(
			map, essl_storage_qualifier_uniform, o,
			&i, &c);

	if (!s)
	{
		printf("temp[");
		if ((index != 0)
			|| (offset_reg == lima_gp_load_off_none))
		{
			printf("%u", index);
			if (offset_reg != lima_gp_load_off_none)
				printf(" + ");
		}

		if (offset_reg != lima_gp_load_off_none)
				printf("$%s",
					lima_gp_load_off_name[offset_reg]);
		printf("].%c", "xyzw"[component]);
	} else {
		if ((s->storage_qualifier
			== essl_storage_qualifier_const)
			&& (offset_reg == lima_gp_load_off_none)
			&& (s->name[0] == '?'))
		{
			unsigned v
				= (essl_type_components(s->type)
					* i) + c;
			printf("%g", s->value[v].f);
		} else {
			printf("%s", s->name);
			if ((s->count != 0)
				|| (offset_reg != lima_gp_load_off_none))
				printf("[");
			if ((s->count != 0)
				&& ((i != 0) || (offset_reg == lima_gp_load_off_none)))
			{
				printf("%u", i);
				if (offset_reg != lima_gp_load_off_none)
					printf(" + ");
			}
			if (offset_reg != lima_gp_load_off_none)
				printf("$%s",
					lima_gp_load_off_name[offset_reg]);

			if ((s->count != 0)
				|| (offset_reg != lima_gp_load_off_none))
				printf("]");

			const char* cname
				= essl_symbol_component_name(s, c);
			if (cname) printf(".%s", cname);
		}
	}
}

static void lima_gp__attribute_print(ogt_link_map_t* map, unsigned index, unsigned offset)
{
	unsigned o = (index << 2) + offset;
	unsigned i, c;
	essl_symbol_t* s
		= ogt_link_map_reference(
			map, essl_storage_qualifier_attribute, o,
			&i, &c);

	if (!s)
	{
		printf("attribute[%u].%c",
			(o >> 2), "xyzw"[o & 3]);
	}
	else
	{
		printf("%s", s->name);
		if (s->count != 0)
			printf("[%u]", i);

		const char* cname
			= essl_symbol_component_name(s, c);
		if (cname) printf(".%s", cname);
	}
}



static void lima_gp_src_simple_print(
	lima_gp_src_simple_e src,
	lima_gp_instruction_t* code, unsigned offset,
	ogt_link_map_t* map)
{
	if (!code)
		return;

	switch (src)
	{
		case lima_gp_src_simple_unused:
			printf("unused");
			break;
		case lima_gp_src_simple_ident:
			printf("ident");
			break;
		case lima_gp_src_simple_attrib_x:
		case lima_gp_src_simple_attrib_y:
		case lima_gp_src_simple_attrib_z:
		case lima_gp_src_simple_attrib_w:
		{
			if (code[offset].register0_attribute)
			{
				lima_gp__attribute_print(map,
					code[offset].register0_addr,
					(src - lima_gp_src_simple_attrib_x));
			} else {
				printf("$%u.%c",
					code[offset].register0_addr,
				"xyzw"[src - lima_gp_src_simple_attrib_x]);
			}
		} break;
		case lima_gp_src_simple_register_x:
		case lima_gp_src_simple_register_y:
		case lima_gp_src_simple_register_z:
		case lima_gp_src_simple_register_w:
		{
			printf("$%u.%c",
				code[offset].register1_addr,
				"xyzw"[src - lima_gp_src_simple_register_x]);
		} break;
		case lima_gp_src_simple_uniform_x:
		case lima_gp_src_simple_uniform_y:
		case lima_gp_src_simple_uniform_z:
		case lima_gp_src_simple_uniform_w:
		{
			lima_gp__uniform_print(map,
				code[offset].load_addr,
				(src - lima_gp_src_simple_uniform_x),
				code[offset].load_offset);
		} break;
		case lima_gp_src_simple_acc_0:
		case lima_gp_src_simple_acc_1:
		{
			lima_gp_src_simple_map_e m[2];
			bool n[2];

			if (src == lima_gp_src_simple_acc_0)
			{
				m[0] = lima_gp_src_simple_map[code[offset].acc0_src0];
				m[1] = lima_gp_src_simple_map[code[offset].acc0_src1];
				n[0] = code[offset].acc0_src0_neg;
				n[1] = code[offset].acc0_src1_neg;
			} else {
				m[0] = lima_gp_src_simple_map[code[offset].acc1_src0];
				m[1] = lima_gp_src_simple_map[code[offset].acc1_src1];
				n[0] = code[offset].acc1_src0_neg;
				n[1] = code[offset].acc1_src1_neg;
			}

			if (m[0].src == lima_gp_src_simple_ident)
			{
				m[0].src = lima_gp_src_simple_complex;
				m[0].off = 1;
			}


			if (m[1].src == lima_gp_src_simple_ident)
			{
				if (n[0]) printf("-");
				lima_gp_src_simple_print(
					m[0].src, code, (offset - m[0].off), map);
			}
			else if (((code[offset].acc_op == lima_gp_acc_op_min)
					|| (code[offset].acc_op == lima_gp_acc_op_max))
					&& (m[0].src == m[1].src)
					&& (m[0].off == m[1].off)
					&& (n[0] != n[1]))
			{
				if (code[offset].acc_op == lima_gp_acc_op_min)
					printf("-");

				printf("acc.abs(");
				lima_gp_src_simple_print(
					m[0].src, code, (offset - m[0].off), map);
				printf(")");
			}
			else
			{
				

				const char* sym
					= lima_gp_acc_op_sym[code[offset].acc_op];
				if (!sym)
					printf("acc.%s",
						lima_gp_acc_op_name[code[offset].acc_op]);
				printf("(");

				if (n[0]) printf("-");
				lima_gp_src_simple_print(
					m[0].src, code, (offset - m[0].off), map);

				if (m[1].src != lima_gp_src_simple_unused)
				{
					if (sym)
						printf(" %s ", sym);
					else
						printf(", ");
	
					if (n[1]) printf("-");
					lima_gp_src_simple_print(
						m[1].src, code, (offset - m[1].off), map);
				}
				printf(")");
			}
		} break;
		case lima_gp_src_simple_mul_0:
		case lima_gp_src_simple_mul_1:
		{
			lima_gp_src_simple_map_e mx[4];
			mx[0] = lima_gp_src_simple_map[code[offset].mul0_src0];
			mx[1] = lima_gp_src_simple_map[code[offset].mul0_src1];
			mx[2] = lima_gp_src_simple_map[code[offset].mul1_src0];
			mx[3] = lima_gp_src_simple_map[code[offset].mul1_src1];
			bool mxn[2];
			mxn[0] = code[offset].mul0_neg;
			mxn[1] = code[offset].mul1_neg;
			
			if (mx[0].src == lima_gp_src_simple_ident)
			{
				mx[0].src = lima_gp_src_simple_complex;
				mx[0].off = 1;
			}

			if (mx[2].src == lima_gp_src_simple_ident)
			{
				mx[2].src = lima_gp_src_simple_complex;
				mx[2].off = 1;
			}

			lima_gp_src_simple_map_e m[2];
			bool mn;
			if (src == lima_gp_src_simple_mul_0)
			{
				m[0] = mx[0];
				m[1] = mx[1];
				mn   = mxn[0];
			} else {
				m[0] = mx[2];
				m[1] = mx[3];
				mn   = mxn[1];
			}

			lima_gp_mul_op_e op
				= code[offset].mul_op;
			switch (code[offset].mul_op)
			{
				case lima_gp_mul_op_complex2:
					if (src != lima_gp_src_simple_mul_0)
						op = lima_gp_mul_op_mul;
					break;
				default:
					break;
			}

			switch (op)
			{
				case lima_gp_mul_op_mul:
				{
					if (m[0].src == lima_gp_src_simple_ident)
					{
						if (mn) printf("-");
						if(m[1].src == lima_gp_src_simple_ident)
							printf("1.0");
						else
							lima_gp_src_simple_print(
								m[1].src, code, (offset - m[1].off), map);
					}
					else if(m[1].src == lima_gp_src_simple_ident)
					{
						if (mn) printf("-");
						lima_gp_src_simple_print(
							m[0].src, code, (offset - m[0].off), map);
					}
					else
					{
						if (mn) printf("-");
						printf("(");
						lima_gp_src_simple_print(
							m[0].src, code, (offset - m[0].off), map);
						printf(" * ");
						lima_gp_src_simple_print(
							m[1].src, code, (offset - m[1].off), map);
						printf(")");
					}
				} break;
				case lima_gp_mul_op_select:
				{
					printf("(");

					lima_gp_src_simple_print(
						mx[1].src, code, (offset - mx[1].off), map);
					printf(" ? ");

					if (mxn[0])
						printf("-");
					lima_gp_src_simple_print(
						mx[0].src, code, (offset - mx[0].off), map);
					printf(" : ");

					if (mxn[1])
						printf("-");
					lima_gp_src_simple_print(
						mx[2].src, code, (offset - mx[2].off), map);

					printf(")");
				} break;
				default:
				{
					bool full
						= lima_gp__mul_op_full[op];

					if (!full && mn)
						printf("-");

					printf("mul");
					if (!full)
						printf("[%u]",
							(src == lima_gp_src_simple_mul_0 ? 0 : 1));
					printf(".%s(",
						lima_gp_mul_op_name[op]);

					if (full)
					{
						if (mxn[0])
							printf("-");
						lima_gp_src_simple_print(
							mx[0].src, code, (offset - mx[0].off), map);

						printf(", ");

						lima_gp_src_simple_print(
							mx[1].src, code, (offset - mx[1].off), map);
	
						printf(", ");

						if (mxn[1])
							printf("-");
						lima_gp_src_simple_print(
							mx[2].src, code, (offset - mx[2].off), map);

						printf(", ");

						lima_gp_src_simple_print(
							mx[3].src, code, (offset - mx[3].off), map);
					} else {
						lima_gp_src_simple_print(
							m[0].src, code, (offset - m[0].off), map);
						printf(", ");
						lima_gp_src_simple_print(
							m[1].src, code, (offset - m[1].off), map);
					}
					printf(")");
				} break;
			}
		} break;
		case lima_gp_src_simple_pass:
		{
			lima_gp_src_simple_map_e m
				= lima_gp_src_simple_map[code[offset].pass_src];
			if (m.src == lima_gp_src_simple_ident)
			{
				m.src = lima_gp_src_simple_complex;
				m.off = 1;
			}
			switch (code[offset].pass_op)
			{
				case lima_gp_pass_op_pass:
					lima_gp_src_simple_print(m.src, code, (offset - m.off), map);
					break;
				case lima_gp_pass_op_clamp:
					printf("clamp(");
					lima_gp_src_simple_print(m.src, code, (offset - m.off), map);
					printf(", ");
					lima_gp_src_simple_print(
						lima_gp_src_simple_uniform_x,
						code, offset, map);
					printf(", ");
					lima_gp_src_simple_print(
						lima_gp_src_simple_uniform_y,
						code, offset, map);
					printf(")");
					break;
				default:
					printf("pass.%s(",
						lima_gp_pass_op_name[code[offset].pass_op]);
					lima_gp_src_simple_print(m.src, code, (offset - m.off), map);
					printf(")");
					break;
			}
		} break;
		case lima_gp_src_simple_complex:
		{
			lima_gp_src_simple_map_e m
				= lima_gp_src_simple_map[code[offset].complex_src];
			if (m.src == lima_gp_src_simple_ident)
			{
				m.src = lima_gp_src_simple_complex;
				m.off = 1;
			}
			switch (code[offset].complex_op)
			{
				case lima_gp_complex_op_nop:
					fprintf(stderr, "Warning: Referencing unused complex field.\n");
					printf("!complex");
					break;
				case lima_gp_complex_op_pass:
					lima_gp_src_simple_print(m.src, code, (offset - m.off), map);
					break;
				default:
					printf("complex.%s(",
						lima_gp_complex_op_name[code[offset].complex_op]);
					lima_gp_src_simple_print(m.src, code, (offset - m.off), map);
					printf(")");
					break;
			}
		} break;
		default:
		{
			fprintf(stderr, "Warning: Referencing unknown field.\n");
			printf("?");
		} break;
	}
}

void lima_gp_instruction_print_decompile(
	lima_gp_instruction_t* code, unsigned offset,
	ogt_link_map_t* map, unsigned tabs)
{
	if (!code)
		return;

	if (code[offset].complex_op
		>= lima_gp_complex_op_temp_load_addr_0)
	{
		ogt_asm_print_tabs(tabs);
		printf("%03X: ", offset);

		unsigned i = code[offset].complex_op
			- lima_gp_complex_op_temp_load_addr_0;
		printf("$ld_addr_%u = ", i);
		lima_gp_src_simple_print(
			lima_gp_src_simple_map[code[offset].complex_src].src,
			code, (offset - lima_gp_src_simple_map[code[offset].complex_src].off), map);
		printf(";\n");
	}

	lima_gp_src_simple_map_e m[] =
	{
		lima_gp_src_simple_map_reg[code[offset].store0_src_x],
		lima_gp_src_simple_map_reg[code[offset].store0_src_y],
		lima_gp_src_simple_map_reg[code[offset].store1_src_z],
		lima_gp_src_simple_map_reg[code[offset].store1_src_w],
	};

	unsigned varying[] =
	{
		code[offset].store0_varying,
		code[offset].store0_varying,
		code[offset].store1_varying,
		code[offset].store1_varying,
	};

	bool temporary[] =
	{
		code[offset].store0_temporary,
		code[offset].store0_temporary,
		code[offset].store1_temporary,
		code[offset].store1_temporary,
	};

	unsigned address[] =
	{
		code[offset].store0_addr,
		code[offset].store0_addr,
		code[offset].store1_addr,
		code[offset].store1_addr,
	};

	if ((code[offset].store0_temporary
		|| code[offset].store1_temporary)
			&& ((code[offset].unknown_1 & 12) != 12))
	{
		fprintf(stderr,
				"Warning: Unexpected value for unknown_1: %u.\n",
				code[offset].unknown_1);
	}

	unsigned i;
	for (i = 0; i < 4; i++)
	{
		if (m[i].src == lima_gp_src_simple_unused)
			continue;

		ogt_asm_print_tabs(tabs);
		printf("%03X: ", offset);

		if (temporary[i])
		{
			/* TODO - Check if we're writing to a uniform? */
			/*lima_gp__uniform_print(
				map, varying[i], i, 7);*/

			printf("temp[");
			if (varying[i] != 0)
				printf("%u + ", address[i]);

			if (code[offset].complex_op
				!= lima_gp_complex_op_temp_store_addr)
			{
				fprintf(stderr, "Warning: Temporary store address not set within instruction.\n");
				printf("$st_addr");
			} else {
				lima_gp_src_simple_map_e m
					= lima_gp_src_simple_map[code[offset].complex_src];
				if (m.src == lima_gp_src_simple_ident)
				{
					m.src = lima_gp_src_simple_complex;
					m.off = 1;
				}

				lima_gp_src_simple_print(
					m.src, code, (offset - m.off), map);
			}
			printf("].%c", "xyzw"[i]);
		}
		else if (varying[i])
		{
			lima_gp__varying_print(
				map, address[i], i);
		} else {
			printf("$%u.%c",
				address[i], "xyzw"[i]);
		}
		printf(" = ");

		lima_gp_src_simple_print(m[i].src, code, (offset - m[i].off), map);
		printf(";\n");
	}

	if (code[offset].branch)
	{
		ogt_asm_print_tabs(tabs);
		printf("%03X: ", offset);

		unsigned branch_target
			= code[offset].branch_target;
		if (!code[offset].branch_target_lo)
			branch_target += 0x100;

		if (code[offset].unknown_1 != 13)
		{
			fprintf(stderr,
				"Warning: Unexpected value for unknown_1: %u.\n",
				code[offset].unknown_1);
		}

		printf("if (");
		lima_gp_src_simple_print(
				lima_gp_src_simple_pass,
				code, offset, map);
		printf(") goto 0x%03X;\n", branch_target);
	}
}

#endif
