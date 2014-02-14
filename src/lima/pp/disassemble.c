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



#include "lima_pp.h"

#include "bitaddr.h"

#include <stdlib.h>
#include <stdio.h>

static void print_tabs(unsigned tabs)
{
	unsigned i;
	for (i = 0; i < tabs; i++)
		printf("\t");
}

const char* lima_pp_field_name[] =
{
	"varying",
	"sampler",
	"uniform",
	"vec4_mul",
	"float_mul",
	"vec4_acc",
	"float_acc",
	"combine",
	"temp_write",
	"branch",
	"vec4_const_0",
	"vec4_const_1",
};

unsigned lima_pp_field_size[] =
{
	34, 62, 41, 43,
	30, 44, 31, 30,
	41, 73, 64, 64,
};


lima_pp_asm_op_t lima_pp_vec4_mul_asm_op[] =
{
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },

	{ "not", "!" , 0, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ "ne" , "!=", 1, 1 },
	{ "lt" , "<" , 1, 1 },
	{ "le" , "<=", 1, 1 },
	{ "eq" , "==", 1, 1 },

	{ "min", NULL, 1, 1 },
	{ "max", NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },

	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ "mov", ""  , 0, 1 },
};

lima_pp_asm_op_t lima_pp_vec4_acc_asm_op[] =
{
	{ "add"  , "+" , 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ "fract", NULL, 0, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },

	{ "ne"   , "!=", 1, 1 },
	{ "lt"   , "<" , 1, 1 },
	{ "le"   , "<=", 1, 1 },
	{ "eq"   , "==", 1, 1 },
	{ "floor", NULL, 0, 1 },
	{ "ceil" , NULL, 0, 1 },
	{ "min"  , NULL, 1, 1 },
	{ "max"  , NULL, 1, 1 },

	{ "sum3", NULL, 0, 1 },
	{ "sum" , NULL, 0, 1 },
	{ NULL  , NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },
	{ "dFdx", NULL, 1, 1 },
	{ "dFdy", NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },
	{ "sel" , ":" , 1, 1 },

	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ "mov", ""  , 0, 1 },
};

lima_pp_asm_op_t lima_pp_float_mul_asm_op[] =
{
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },
	{ "mul", "*" , 1, 1 },

	{ "not", "!" , 0, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ "neq", "!=", 1, 1 },
	{ "lt" , "<" , 1, 1 },
	{ "le" , "<=", 1, 1 },
	{ "eq" , "==", 1, 1 },

	{ "min", NULL, 1, 1 },
	{ "max", NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },

	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ "mov", ""  , 0, 1 },
};

lima_pp_asm_op_t lima_pp_float_acc_asm_op[] =
{
	{ "add"  , "+" , 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ "fract", NULL, 0, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },
	{ NULL   , NULL, 1, 1 },

	{ "ne"   , "!=", 1, 1 },
	{ "lt"   , "<" , 1, 1 },
	{ "le"   , "<=", 1, 1 },
	{ "eq"   , "==", 1, 1 },
	{ "floor", NULL, 0, 1 },
	{ "ceil" , NULL, 0, 1 },
	{ "min"  , NULL, 1, 1 },
	{ "max"  , NULL, 1, 1 },

	{ NULL  , NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },
	{ "dFdx", NULL, 1, 1 },
	{ "dFdy", NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },
	{ NULL  , NULL, 1, 1 },

	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ NULL , NULL, 1, 1 },
	{ "mov", ""  , 0, 1 },
};

lima_pp_asm_op_t lima_pp_combine_asm_op[] =
{
	{ "rcp", "1.0 / ", 0, 1 },
	{ "mov", ""  , 0, 1 },
	{ "sqrt", NULL, 0, 1 },
	{ "inversesqrt", NULL, 0, 1 },
	{ "exp2", NULL, 0, 1 },
	{ "log2", NULL, 0, 1 },
	{ "sin", NULL, 0, 1 },
	{ "cos", NULL, 0, 1 },

	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
	{ NULL, NULL, 1, 1 },
};



static void _print_bin_u32n(uint32_t mask, unsigned size)
{
	unsigned i = size;
	for (i = size; i--;)
		printf("%u", (unsigned)((mask >> i) & 1));
}

static void _print_bin_un(uint32_t* mask, unsigned size)
{
	_print_bin_u32n(mask[size >> 5], (size & 31));
	for (size >>= 5; size; size--)
		_print_bin_u32n(mask[size - 1], 32);
}



static void _lima_pp_field_print_swizzle(uint8_t swizzle)
{
	if (swizzle == 0xE4)
		return;

	printf(".");
	char symbol[4] = { 'x', 'y', 'z', 'w' };
	unsigned i;
	for (i = 0; i < 4; i++, swizzle >>= 2)
		printf("%c", symbol[swizzle & 3]);
}

static void _lima_pp_field_print_mask(uint8_t mask)
{
	if (mask == 0xF)
		return;
	printf(".");
	if (mask & 1) printf("x");
	if (mask & 2) printf("y");
	if (mask & 4) printf("z");
	if (mask & 8) printf("w");
}



static void _lima_pp_field_print_reg_name(
	lima_pp_vec4_reg_e reg, const char* special,
	bool verbose)
{
	if (special)
	{
		printf("%s", special);
	} else {
		switch (reg)
		{
			case lima_pp_vec4_reg_constant0:
				printf("^const0");
				break;
			case lima_pp_vec4_reg_constant1:
				printf("^const1");
				break;
			case lima_pp_vec4_reg_texture:
				if (verbose)
					printf("^texture");
				else
					printf("^tex_sampler");
				break;
			case lima_pp_vec4_reg_uniform:
				if (verbose)
					printf("^uniform");
				else
					printf("^u");
				break;
			default:
				printf("$%u", reg);
				break;
		}
	}
}

static void _lima_pp_field_print_reg_source(
	lima_pp_vec4_reg_e reg, const char* special,
	uint8_t swizzle, bool abs, bool neg,
	bool verbose)
{
	if (neg)
		printf("-");
	if (abs)
		printf("abs(");

	_lima_pp_field_print_reg_name(
		reg, special, verbose);

	_lima_pp_field_print_swizzle(swizzle);
	if (abs)
		printf(")");
}

static void _lima_pp_field_print_reg_source_scalar(
	unsigned reg, const char* special,
	bool abs, bool neg,
	bool verbose)
{
	if (neg)
		printf("-");
	if (abs)
		printf("abs(");

	_lima_pp_field_print_reg_name(
		(reg >> 2), special, verbose);
	if (!special)
	{
		static const char c[4] = "xyzw";
		printf(".%c", c[reg & 3]);
	}
	if (abs)
		printf(")");
}

static void _lima_pp_field_print_outmod_d3d(lima_pp_outmod_e modifier)
{
	switch (modifier)
	{
		case lima_pp_outmod_clamp_fraction:
			printf("_sat");
			break;
		case lima_pp_outmod_clamp_positive:
			printf("_pos");
			break;
		case lima_pp_outmod_round:
			printf("_int");
			break;
		default:
			break;
	}
}

static void _lima_pp_field_print_reg_dest_scalar(
	unsigned reg,
	lima_pp_outmod_e modifier)
{
	printf("$%u", (reg >> 2));
	_lima_pp_field_print_outmod_d3d(modifier);
	static const char c[4] = "xyzw";
	printf(".%c", c[reg & 3]);
}

static void _lima_pp_field_print_outmod_start(lima_pp_outmod_e modifier)
{
	switch (modifier)
	{
		case lima_pp_outmod_clamp_fraction:
			printf("clamp(");
			break;
		case lima_pp_outmod_clamp_positive:
			printf("max(0.0, ");
			break;
		case lima_pp_outmod_round:
			printf("round(");
			break;
		default:
			break;
	}
}

static void _lima_pp_field_print_outmod_end(lima_pp_outmod_e modifier)
{
	switch (modifier)
	{
		case lima_pp_outmod_clamp_fraction:
			printf(", 0.0, 1.0)");
			break;
		case lima_pp_outmod_clamp_positive:
		case lima_pp_outmod_round:
			printf(")");
			break;
		default:
			break;
	}
}



static void _lima_pp_field_print_const(
	lima_pp_field_e field,
	lima_pp_vec4_t* vector,
	bool verbose)
{
	if (verbose)
		printf("^");
	printf("const%u ", (field - lima_pp_field_vec4_const_0));
	if (verbose)
		printf("= vec4(");
	
	printf("%g", ogt_hfloat_to_float(vector->x));
	if (verbose) printf(",");
	printf(" %g", ogt_hfloat_to_float(vector->y));
	if (verbose) printf(",");
	printf(" %g", ogt_hfloat_to_float(vector->z));
	if (verbose) printf(",");
	printf(" %g", ogt_hfloat_to_float(vector->w));
	if (verbose)
		printf(")");
}

static void _lima_pp_field_print_varying(
	lima_pp_field_e field,
	lima_pp_field_varying_t* varying,
	bool verbose)
{
	(void) field; /* Not used. */

	if (verbose)
	{
		if (varying->imm.dest != lima_pp_vec4_reg_discard)
		{
			printf("$%u", varying->imm.dest);
			_lima_pp_field_print_mask(varying->imm.mask);
			printf(" = ");
		}

		bool perspective
			= ((varying->imm.source_type < 2)
				&& varying->imm.perspective);
		if (perspective)
			printf("perspective(");

		switch (varying->imm.source_type)
		{
			case 1:
				_lima_pp_field_print_reg_source(
					varying->reg.source, NULL,
					varying->reg.swizzle,
					varying->reg.absolute,
					varying->reg.negate,
					verbose);
				break;
			case 2:
				printf("gl_FragCoord");
				break;
			case 3:
				if (varying->imm.perspective)
					printf("gl_FrontFacing");
				else
					printf("gl_PointCoord");
				break;
			default:
				switch (varying->imm.alignment)
				{
					case 0:
					{
						printf("varying[%u",
							(varying->imm.index >> 2));
					} break;
					case 1:
					{
						printf("varying[%u",
							(varying->imm.index >> 1));
					} break;
					default:
					{
						printf("varying[%u", varying->imm.index);
					} break;
				}
				
				if (varying->imm.offset_vector != 15)
				{
					unsigned reg = (varying->imm.offset_vector << 2)
						+ varying->imm.offset_scalar;
					printf(" + ");
					_lima_pp_field_print_reg_source_scalar(reg, NULL,
						false, false, true);
				}
				
				switch (varying->imm.alignment)
				{
					case 0:
					{
						const char c[4] = "xyzw";
						printf("].%c", c[varying->imm.index & 3]);
						break;
					}
					case 1:
					{
						const char* c[2] = {"xy", "zw"};
						printf("].%s", c[varying->imm.index & 1]);
						break;
					}
					default:
						printf("]");
						break;
				}
				break;
		}

		if (perspective)
		{
			switch (varying->imm.perspective)
			{
				case 2:
					printf(", z)");
					break;
				case 3:
					printf(", w)");
					break;
				default:
					printf(", unknown)");
					break;
			}
		}
	} else {
		printf("load");
		
		bool perspective
			= ((varying->imm.source_type < 2)
				&& varying->imm.perspective);
		if (perspective)
		{
			printf("_perspective");
			switch (varying->imm.perspective)
			{
				case 2:
					printf("_z");
					break;
				case 3:
					printf("_w");
					break;
				default:
					printf("_unknown");
					break;
			}
		}
		printf(".v ");


		switch (varying->imm.dest)
		{
			case lima_pp_vec4_reg_discard:
				printf("^discard");
				break;
			default:
				printf("$%u", varying->imm.dest);
				break;
		}
		_lima_pp_field_print_mask(varying->imm.mask);
		printf(" ");

		switch (varying->imm.source_type)
		{
			case 1:
				_lima_pp_field_print_reg_source(
					varying->reg.source, NULL,
					varying->reg.swizzle,
					varying->reg.absolute,
					varying->reg.negate,
					verbose);
				break;
			case 2:
				printf("gl_FragCoord");
				break;
			case 3:
				if (varying->imm.perspective)
					printf("gl_FrontFacing");
				else
					printf("gl_PointCoord");
				break;
			default:
				switch (varying->imm.alignment)
				{
					case 0:
					{
						const char c[4] = "xyzw";
						printf("%u.%c",
							(varying->imm.index >> 2),
							c[varying->imm.index & 3]);
					} break;
					case 1:
					{
						const char *c[2] = {"xy", "zw"};
						printf("%u.%s",
							(varying->imm.index >> 1),
							c[varying->imm.index & 1]);
					} break;
					default:
					{
						printf("%u", varying->imm.index);
					} break;
				}
				if (varying->imm.offset_vector != 15)
				{
					unsigned reg = (varying->imm.offset_vector << 2)
						+ varying->imm.offset_scalar;
					printf("+");
					_lima_pp_field_print_reg_source_scalar(reg, NULL,
						false, false, false);
				}
				break;
		}
	}
}

static void _lima_pp_field_print_sampler(
	lima_pp_field_e field,
	lima_pp_field_sampler_t* sampler,
	bool verbose)
{
	(void) field; /* Not used. */

	if (verbose)
	{
		printf("^texture = ");
		switch (sampler->type)
		{
			case lima_pp_sampler_type_2d:
				printf("sampler2D");
				break;
			case lima_pp_sampler_type_cube:
				printf("samplerCube");
				break;
			default:
				printf("sampler%u", sampler->type);
				break;
		}
		printf("(");

		printf("%u", sampler->index);

		if (sampler->offset_en)
		{
			printf(" + ");
			_lima_pp_field_print_reg_source_scalar(
				sampler->index_offset, NULL,
				false, false,
				verbose);
		}

		if (sampler->lod_bias_en)
		{
			printf(", ");
			_lima_pp_field_print_reg_source_scalar(
				sampler->lod_bias, NULL,
				false, false,
				verbose);
		}
		printf(")");
	} else {
		printf("texld");
		if (sampler->lod_bias_en)
			printf("b");

		switch (sampler->type)
		{
			case lima_pp_sampler_type_2d:
				printf("_2d");
				break;
			case lima_pp_sampler_type_cube:
				printf("_cube");
				break;
			default:
				printf("_t%u", sampler->type);
				break;
		}
		printf(" %u", sampler->index);

		if (sampler->offset_en)
		{
			printf("+");
			_lima_pp_field_print_reg_source_scalar(
				sampler->index_offset, NULL,
				false, false,
				verbose);
		}

		if (sampler->lod_bias_en)
		{
			printf(" ");
			_lima_pp_field_print_reg_source_scalar(
				sampler->lod_bias, NULL,
				false, false,
				verbose);
		}
	}
}

static void _lima_pp_field_print_uniform(
	lima_pp_field_e field,
	lima_pp_field_uniform_t* uniform,
	bool verbose)
{
	(void) field; /* Not used. */

	if (verbose)
	{
		printf("^uniform = ");
		switch (uniform->source)
		{
			case lima_pp_uniform_src_uniform:
				printf("uniform");
				break;
			case lima_pp_uniform_src_temporary:
				printf("temporary");
				break;
			default:
				printf("source%u", uniform->source);
				break;
		}

		if(uniform->alignment)
		{
			printf("[%u", uniform->index);
		} else {
			printf("[%u",
				(uniform->index >> 2));
		}
		
		if(uniform->offset_en) {
			printf(" + ");
			_lima_pp_field_print_reg_source_scalar(
				uniform->offset_reg, NULL,
				false, false,
				verbose);
		}
		
		printf("]");
		
		if(!uniform->alignment) {
			char* c = "xyzw";
			printf(".%c", c[uniform->index & 3]);
		}
	}
	else
	{
		printf("load.");

		switch (uniform->source)
		{
			case lima_pp_uniform_src_uniform:
				printf("u");
				break;
			case lima_pp_uniform_src_temporary:
				printf("t");
				break;
			default:
				printf(".u%u", uniform->source);
				break;
		}

		if (uniform->alignment)
		{
			printf(" %u", uniform->index);
		} else {
			char* c = "xyzw";
			printf(" %u.%c",
				(uniform->index >> 2),
				c[uniform->index & 3]);
		}
		
		if(uniform->offset_en) {
			printf(" ");
			_lima_pp_field_print_reg_source_scalar(
				uniform->offset_reg, NULL,
				false, false,
				verbose);
		}
	}
}

static void _lima_pp_field_print_vec4_mul(
	lima_pp_field_e field,
	lima_pp_field_vec4_mul_t* vec4_mul,
	bool verbose)
{
	(void) field; /* Not used. */

	lima_pp_asm_op_t op
		= lima_pp_vec4_mul_asm_op[vec4_mul->op];
	if (!verbose)
	{
		if (op.name)
			printf("%s", op.name);
		else
			printf("op%u", vec4_mul->op);
		printf(".v0 ");
	}

	if (vec4_mul->mask)
	{
		printf("$%u", vec4_mul->dest);
		if (!verbose)
			_lima_pp_field_print_outmod_d3d(vec4_mul->dest_modifier);
		_lima_pp_field_print_mask(vec4_mul->mask);
		if (verbose)
			printf(" =");
		printf(" ");
	}

	bool bracket = verbose;
	if (!op.arg0
		&& !op.arg1)
		bracket = false;

	const char* seperator = NULL;
	if (verbose)
	{
		printf("^vmul = ");

		_lima_pp_field_print_outmod_start(vec4_mul->dest_modifier);

		if (op.symbol)
		{
			bracket = false;
			seperator = op.symbol;
		} else {
			if (op.name)
				printf("%s", op.name);
			else
				printf("op%u", vec4_mul->op);
		}
		if (bracket)
			printf("(");
	}

	if (op.arg0)
	{
		_lima_pp_field_print_reg_source(
			vec4_mul->arg0_source, NULL,
			vec4_mul->arg0_swizzle,
			vec4_mul->arg0_absolute,
			vec4_mul->arg0_negate,
			verbose);
	}

	if (op.arg0
		&& op.arg1)
	{
		if (seperator)
			printf(" %s", seperator);
		else if (bracket)
			printf(",");
		printf(" ");
	}
	else if (seperator)
	{
		printf("%s", seperator);
	}

	if (op.arg1)
	{
		_lima_pp_field_print_reg_source(
			vec4_mul->arg1_source, NULL,
			vec4_mul->arg1_swizzle,
			vec4_mul->arg1_absolute,
			vec4_mul->arg1_negate,
			verbose);
	}
			
	if ((vec4_mul->op < 8)
		&& (vec4_mul->op > 0))
	{
		if (verbose)
			printf(" <<");
		printf(" ");
		printf("%u", vec4_mul->op);
	}

	if (verbose)
	{
		if (bracket)
			printf(")");
		_lima_pp_field_print_outmod_end(vec4_mul->dest_modifier);
	}
}

static void _lima_pp_field_print_vec4_acc(
	lima_pp_field_e field,
	lima_pp_field_vec4_acc_t* vec4_acc,
	bool verbose)
{
	(void) field; /* Not used. */

	lima_pp_asm_op_t op
		= lima_pp_vec4_acc_asm_op[vec4_acc->op];
	if (!verbose)
	{
		if (op.name)
			printf("%s", op.name);
		else
			printf("op%u", vec4_acc->op);
		printf(".v1 ");
	}

	if (vec4_acc->mask)
	{
		printf("$%u", vec4_acc->dest);
		if (!verbose)
			_lima_pp_field_print_outmod_d3d(vec4_acc->dest_modifier);
		_lima_pp_field_print_mask(vec4_acc->mask);
		if (verbose)
			printf(" =");
		printf(" ");
	}
		
	bool bracket = verbose;
	if (!op.arg0
		&& !op.arg1)
		bracket = false;

	const char* seperator = NULL;
	if (verbose)
	{
		_lima_pp_field_print_outmod_start(vec4_acc->dest_modifier);

		if (op.symbol)
		{
			if (vec4_acc->op != lima_pp_vec4_acc_op_sel)
				bracket = false;
			seperator = op.symbol;
		} else {
			if (op.name)
				printf("%s", op.name);
			else
				printf("op%u", vec4_acc->op);
		}
		if (bracket)
			printf("(");
	}

	if ((vec4_acc->op == lima_pp_vec4_acc_op_sel)
		&& verbose)
		printf("!^fmul ? ");

	if (op.arg0)
	{
		_lima_pp_field_print_reg_source(
			vec4_acc->arg0_source, NULL,
			vec4_acc->arg0_swizzle,
			vec4_acc->arg0_absolute,
			vec4_acc->arg0_negate,
			verbose);
	}

	if (op.arg0
		&& op.arg1)
	{
		if (seperator)
			printf(" %s", seperator);
		else if (bracket)
			printf(",");
		printf(" ");
	}
	else if (seperator)
	{
		printf("%s", seperator);
	}

	if (op.arg1)
	{
		_lima_pp_field_print_reg_source(
			vec4_acc->arg1_source,
			(vec4_acc->mul_in ? (verbose ? "^vmul" : "^v0") : NULL),
			vec4_acc->arg1_swizzle,
			vec4_acc->arg1_absolute,
			vec4_acc->arg1_negate,
			verbose);
	}

	if (verbose)
	{
		if (bracket)
			printf(")");
		_lima_pp_field_print_outmod_end(vec4_acc->dest_modifier);
	}
}

static void _lima_pp_field_print_float_mul(
	lima_pp_field_e field,
	lima_pp_field_float_mul_t* float_mul,
	bool verbose)
{
	(void) field; /* Not used. */

	lima_pp_asm_op_t op
		= lima_pp_float_mul_asm_op[float_mul->op];
	if (!verbose)
	{
		if (op.name)
			printf("%s", op.name);
		else
			printf("op%u", float_mul->op);
		printf(".s0 ");
	}

	if (float_mul->output_en)
	{
		_lima_pp_field_print_reg_dest_scalar(float_mul->dest,
			(verbose ? lima_pp_outmod_none : float_mul->dest_modifier));
		if (verbose)
			printf(" =");
		printf(" ");
	}

	bool bracket = verbose;
	if (!op.arg0
		&& !op.arg1)
		bracket = false;

	const char* seperator = NULL;
	if (verbose)
	{
		printf("^fmul = ");

		_lima_pp_field_print_outmod_start(float_mul->dest_modifier);

		if (op.symbol)
		{
			bracket = false;
			seperator = op.symbol;
		} else {
			if (op.name)
		printf("%s", op.name);
			else
		printf("op%u", float_mul->op);
		}
		if (bracket)
			printf("(");
	}

	if (op.arg0)
	{
		_lima_pp_field_print_reg_source_scalar(
			float_mul->arg0_source, NULL,
			float_mul->arg0_absolute,
			float_mul->arg0_negate,
			verbose);
	}

	if (op.arg0
		&& op.arg1)
	{
		if (seperator)
			printf(" %s", seperator);
		else if (bracket)
			printf(",");
		printf(" ");
	}
	else if (seperator)
	{
		printf("%s", seperator);
	}

	if (op.arg1)
	{
		_lima_pp_field_print_reg_source_scalar(
			float_mul->arg1_source, NULL,
			float_mul->arg1_absolute,
			float_mul->arg1_negate,
			verbose);
	}
	
	if ((float_mul->op < 8)
		&& (float_mul->op > 0))
	{
		if (verbose)
			printf(" << ");
		else
			printf(", ");
		printf("%u", float_mul->op);
	}

	if (verbose)
	{
		if (bracket)
			printf(")");
		_lima_pp_field_print_outmod_end(float_mul->dest_modifier);
	}
}

static void _lima_pp_field_print_float_acc(
	lima_pp_field_e field,
	lima_pp_field_float_acc_t* float_acc,
	bool verbose)
{
	(void) field; /* Not used. */

	lima_pp_asm_op_t op
		= lima_pp_float_acc_asm_op[float_acc->op];
	if (!verbose)
	{
		if (op.name)
			printf("%s", op.name);
		else
			printf("op%u", float_acc->op);
		printf(".s1 ");
	}

	if (float_acc->output_en)
	{
		_lima_pp_field_print_reg_dest_scalar(float_acc->dest,
			(verbose ? lima_pp_outmod_none : float_acc->dest_modifier));
		if (verbose)
			printf(" =");
		printf(" ");
	}

	bool bracket = verbose;
	if (!op.arg0
		&& !op.arg1)
		bracket = false;

	const char* seperator = NULL;
	if (verbose)
	{
		_lima_pp_field_print_outmod_start(float_acc->dest_modifier);

		if (op.symbol)
		{
			bracket = false;
			seperator = op.symbol;
		} else {
			if (op.name)
				printf("%s", op.name);
			else
				printf("op%u", float_acc->op);
		}

		if (bracket)
			printf("(");
	}

	if (op.arg0)
	{
		_lima_pp_field_print_reg_source_scalar(
			float_acc->arg0_source, NULL,
			float_acc->arg0_absolute,
			float_acc->arg0_negate,
			verbose);
	}

	if (op.arg0
		&& op.arg1)
	{
		if (seperator)
			printf(" %s", seperator);
		else if (bracket)
			printf(",");
		printf(" ");
	}
	else if (seperator)
	{
		printf("%s", seperator);
	}

	if (op.arg1)
	{
		_lima_pp_field_print_reg_source_scalar(
			float_acc->arg1_source,
			(float_acc->mul_in ? (verbose ? "^fmul" : "^s0") : NULL),
			float_acc->arg1_absolute,
			float_acc->arg1_negate,
			verbose);
	}
	
	if ((float_acc->op < 8)
		&& (float_acc->op > 0))
	{
		if (verbose)
			printf(" << ");
		else
			printf(" ");
		printf("%u", float_acc->op);
	}

	if (verbose)
	{
		if (bracket)
			printf(")");
		_lima_pp_field_print_outmod_end(float_acc->dest_modifier);
	}
}

static void _lima_pp_field_print_combine(
	lima_pp_field_e field,
	lima_pp_field_combine_t* combine,
	bool verbose)
{
	(void) field; /* Not used. */

	if (!combine->scalar.dest_vec)
	{
		if (!combine->scalar.arg1_en)
		{
			lima_pp_asm_op_t op
				= lima_pp_combine_asm_op[combine->scalar.op];
			if (!verbose)
			{
				if (op.name)
					printf("%s.s2 ", op.name);
				else
					printf("op%u.s2 ", combine->scalar.op);
			}

			_lima_pp_field_print_reg_dest_scalar(combine->scalar.dest,
				(verbose ? lima_pp_outmod_none : combine->scalar.dest_modifier));
			if (verbose)
				printf(" =");
			printf(" ");

			bool bracket = verbose;
		
			if (verbose)
			{
				_lima_pp_field_print_outmod_start(combine->scalar.dest_modifier);
				if (op.symbol)
				{
					printf("%s", op.symbol);
					bracket = false;
				} else {
					if (op.name)
						printf("%s", op.name);
					else
						printf("op%u", combine->scalar.op);
					if (bracket)
						printf("(");
				}
			}

			_lima_pp_field_print_reg_source_scalar(
				combine->scalar.arg0_src, NULL,
				combine->scalar.arg0_absolute,
				combine->scalar.arg0_negate,
				verbose);
	
			if (verbose)
			{
				if (bracket)
					printf(")");
				_lima_pp_field_print_outmod_end(combine->scalar.dest_modifier);
			}
		} else {
			if (!verbose)
				printf("atan_pt2.s2 ");

			_lima_pp_field_print_reg_dest_scalar(
				combine->scalar.dest, lima_pp_outmod_none);
			if (verbose)
				printf(" =");
			printf(" ");
		
			if (verbose)
			{
				printf("atan_pt2(");
			}
			
			_lima_pp_field_print_reg_source(
				combine->vector.arg1_source, NULL,
				combine->vector.arg1_swizzle, false, false,
				verbose);

			if (verbose)
				printf(")");
		}
	} else {
		if (!combine->vector.arg1_en)
		{
			if (!verbose) {
				if (combine->scalar.op == lima_pp_combine_scalar_op_atan)
					printf("atan.s2 ");
				else
					printf("atan2.s2 ");
			}
		
			printf("$%u", combine->vector.dest);
			_lima_pp_field_print_mask(combine->vector.mask);
			if (verbose)
				printf(" =");
			printf(" ");

			if (verbose) {
				if (combine->scalar.op == lima_pp_combine_scalar_op_atan)
					printf("atan(");
				else
					printf("atan2(");
			}

			_lima_pp_field_print_reg_source_scalar(
				combine->scalar.arg0_src, NULL,
				combine->scalar.arg0_absolute,
				combine->scalar.arg0_negate,
				verbose);
			
			if (combine->scalar.op == lima_pp_combine_scalar_op_atan2) {
				if (verbose)
					printf(", ");
				else
					printf(" ");
				_lima_pp_field_print_reg_source_scalar(
					combine->scalar.arg1_src, NULL,
					combine->scalar.arg1_absolute,
					combine->scalar.arg1_negate,
					verbose);
			}

			if (verbose)
				printf(")");
		} else {
			if (!verbose)
				printf("mul.s2 ");

			printf("$%u", combine->vector.dest);
			_lima_pp_field_print_mask(combine->vector.mask);
			if (verbose)
				printf(" =");
			printf(" ");

			_lima_pp_field_print_reg_source(
				combine->vector.arg1_source, NULL,
				combine->vector.arg1_swizzle, false, false,
				verbose);

			if (verbose)
				printf(" *");
			printf(" ");

			_lima_pp_field_print_reg_source_scalar(
				combine->scalar.arg0_src, NULL,
				combine->scalar.arg0_absolute,
				combine->scalar.arg0_negate,
				verbose);
		}
	}
}

static void _lima_pp_field_print_temp_write(
	lima_pp_field_e field,
	lima_pp_field_temp_write_t* temp_write,
	bool verbose)
{
	(void) field; /* Not used. */
	
	if (temp_write->fb_read.unknown_0 == 0x7)
	{
		if (verbose)
			printf("$%u = ", temp_write->fb_read.dest);
		if (temp_write->fb_read.source)
			printf("fb_color");
		else
			printf("fb_depth");
		if (!verbose)
			printf(" $%u", temp_write->fb_read.dest);
		
		return;
	}
	
	if (verbose)
	{
		printf("temporary[");
		
		if(temp_write->temp_write.alignment)
		{
			printf("%u", temp_write->temp_write.index);
		} else {
			printf("%u",
				   (temp_write->temp_write.index >> 2));
		}
		
		if(temp_write->temp_write.offset_en) {
			printf(" + ");
			_lima_pp_field_print_reg_source_scalar(
				temp_write->temp_write.offset_reg, NULL,
				false, false,
				verbose);
		}
		
		printf("]");
		
		if(!temp_write->temp_write.alignment) {
			char* c = "xyzw";
			printf(".%c", c[temp_write->temp_write.index & 3]);
		}
		
		printf(" = ");
	}
	else
	{
		printf("store.t");
		
		if (temp_write->temp_write.alignment)
		{
			printf(" %u", temp_write->temp_write.index);
		} else {
			char* c = "xyzw";
			printf(" %u.%c",
				   (temp_write->temp_write.index >> 2),
				   c[temp_write->temp_write.index & 3]);
		}
		
		if(temp_write->temp_write.offset_en) {
			printf(" ");
			_lima_pp_field_print_reg_source_scalar(
				temp_write->temp_write.offset_reg, NULL,
				false, false,
				verbose);
		}
		
		printf(" ");
	}
	
	if(temp_write->temp_write.alignment) {
		_lima_pp_field_print_reg_name(
			temp_write->temp_write.source >> 2, NULL, verbose);
	} else {
		_lima_pp_field_print_reg_source_scalar(
			temp_write->temp_write.source, NULL,
			false, false,
			verbose);
	}
}

static void _lima_pp_field_print_branch(
	lima_pp_field_e field,
	lima_pp_field_branch_t* branch,
	bool verbose)
{
	(void) field; /* Not used. */
	
	if (branch->discard.word0 == LIMA_PP_DISCARD_WORD0 &&
		branch->discard.word1 == LIMA_PP_DISCARD_WORD1 &&
		branch->discard.word2 == LIMA_PP_DISCARD_WORD2)
	{
		printf("discard");
		return;
	}

	if (!verbose)
	{
		const char* cond[] =
		{
			"nv", "lt", "eq", "le",
			"gt", "ne", "ge", ""  ,
		};
		unsigned cond_mask = 0;
		cond_mask |= (branch->branch.cond_lt ? 1 : 0);
		cond_mask |= (branch->branch.cond_eq ? 2 : 0);
		cond_mask |= (branch->branch.cond_gt ? 4 : 0);
		printf("j%s ", cond[cond_mask]);

		if (cond_mask)
		{
			_lima_pp_field_print_reg_source_scalar(
				branch->branch.arg0_source, NULL,
				false, false,
				verbose);
			printf(" ");
			_lima_pp_field_print_reg_source_scalar(
				branch->branch.arg1_source, NULL,
				false, false,
				verbose);
			printf(" ");
		}
	} else {
		if (!branch->branch.cond_lt
			|| !branch->branch.cond_eq
			|| !branch->branch.cond_gt)
		{
			printf(" if (");

			if (branch->branch.cond_lt
				|| branch->branch.cond_eq
				|| branch->branch.cond_gt)
			{
				_lima_pp_field_print_reg_source_scalar(
					branch->branch.arg0_source, NULL,
					false, false,
					verbose);

				if (branch->branch.cond_eq)
				{
					if (branch->branch.cond_gt)
						printf(" >= ");
					else if (branch->branch.cond_lt)
						printf(" <= ");
					else
						printf(" == ");
				} else {
					if (branch->branch.cond_gt
						&& branch->branch.cond_lt)
						printf(" != ");
					else if (branch->branch.cond_gt)
						printf(" > ");
					else if (branch->branch.cond_lt)
						printf(" < ");
					else
						printf(" == ");
				}

				_lima_pp_field_print_reg_source_scalar(
					branch->branch.arg1_source, NULL,
					false, false,
					verbose);
			} else {
				printf("false");
			}
			printf(") ");
		}
		printf ("goto ");
	}
	printf("%d", branch->branch.target);
}

static void _lima_pp_field_print_unknown(
	lima_pp_field_e field,
	void* data,
	bool verbose)
{
	(void) verbose; /* Not used. */

	printf("%s:", lima_pp_field_name[field]);
	_print_bin_un(data, lima_pp_field_size[field]);
}

void lima_pp_instruction_print(
	lima_pp_instruction_t* code, bool verbose, unsigned tabs)
{
	void* field[] =
	{
		&code->varying,
		&code->sampler,
		&code->uniform,
		&code->vec4_mul,
		&code->float_mul,
		&code->vec4_acc,
		&code->float_acc,
		&code->combine,
		&code->temp_write,
		&code->branch,
		&code->const0,
		&code->const1,
	};

	void (*field_print[])(lima_pp_field_e field, void* data, bool verbose) =
	{
		(void*)_lima_pp_field_print_varying,
		(void*)_lima_pp_field_print_sampler,
		(void*)_lima_pp_field_print_uniform,
		(void*)_lima_pp_field_print_vec4_mul,
		(void*)_lima_pp_field_print_float_mul,
		(void*)_lima_pp_field_print_vec4_acc,
		(void*)_lima_pp_field_print_float_acc,
		(void*)_lima_pp_field_print_combine,
		(void*)_lima_pp_field_print_temp_write,
		(void*)_lima_pp_field_print_branch,
		(void*)_lima_pp_field_print_const,
		(void*)_lima_pp_field_print_const,
	};

	unsigned field_order[] =
	{
		lima_pp_field_vec4_const_0,
		lima_pp_field_vec4_const_1,
		lima_pp_field_uniform,
		lima_pp_field_varying,
		lima_pp_field_sampler,
		lima_pp_field_vec4_mul,
		lima_pp_field_float_mul,
		lima_pp_field_vec4_acc,
		lima_pp_field_float_acc,
		lima_pp_field_combine,
		lima_pp_field_temp_write,
		lima_pp_field_branch,
	};
	
	if (!verbose)
		print_tabs(tabs);

	bool first = true;
	unsigned i;
	for (i = 0; i < lima_pp_field_count; i++)
	{
		unsigned f = field_order[i];
		if ((code->control.fields >> f) & 1)
		{
			if (first)
				first = false;
			else
				printf(",%c", (verbose ? '\n' : ' '));

			if (verbose)
				print_tabs(tabs);

			if (field_print[f])
				field_print[f](f, field[f], verbose);
			else
				_lima_pp_field_print_unknown(f, field[f], verbose);
		}
	}

	if (code->control.sync
		|| code->control.stop)
	{
		if (first)
			first = false;
		else
			printf(",%c", (verbose ? '\n' : ' '));

		if (verbose)
			print_tabs(tabs);

		if (code->control.sync)
		{
			printf("sync");
			if (code->control.stop)
				printf(", ");
		}
		if (code->control.stop)
			printf("stop");
	}

	if (verbose)
	{
		printf(";");
		if  (code->control.unknown)
		{
			printf(" # unknown = ");
			_print_bin_u32n(code->control.unknown, 6);
		}
	}
	printf("\n");
}

#if 0

bool lima_pp_disassemble(
	void* code, unsigned size,
	ogt_link_map_t* map,
	ogt_asm_type_e type,
	ogt_asm_type_e syntax,
	unsigned tabs)
{
	(void)map;  /* Not used. */
	(void)type; /* Not used. */

	/* Fragment assembler must be a multiple of 32-bits. */
	if (size & 3) return false;


	bool verbose = false;
	switch (syntax)
	{
		case ogt_asm_syntax_explicit:
			verbose = true;
			break;
		case ogt_asm_syntax_verbose:
			break;
		default:
			return false;
	}

	if (!code) return false;

	/* Print symbol table. */
	essl_program_t* program
		= ogt_link_map_export_essl(map);
	if (program)
	{
		essl_program_print(program, stdout, tabs);
		printf("\n");
	}

	uint32_t* wcode = (uint32_t*)code;
	lima_pp_ctrl_t ctrl;
	unsigned i;
	for (i = 0; i < (size >> 2); i += ctrl.count)
	{
		ctrl.mask = wcode[i];

		if (i && verbose)
			printf("\n");

		lima_pp_instruction_t inst;
		lima_pp_instruction_decode(&wcode[i], &inst);
		lima_pp_instruction_print(&inst, verbose, tabs);
	}

	return true;
}

#endif
