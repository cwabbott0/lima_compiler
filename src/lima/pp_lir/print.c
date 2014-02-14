/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
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

#include "pp_lir.h"
#include <stdio.h>

static void print_reg(lima_pp_lir_reg_t* reg)
{
	if (reg->precolored)
		printf("$%u", reg->index);
	else
		printf("%%%u", reg->index);
}

static void print_tabs(unsigned num_tabs)
{
	unsigned i;
	for (i = 0; i < num_tabs; i++)
		printf("\t");
}

static void print_pipeline_reg(lima_pp_lir_pipeline_reg_e reg)
{
	switch(reg)
	{
		case lima_pp_lir_pipeline_reg_const0:
			printf("^const0");
			break;
		case lima_pp_lir_pipeline_reg_const1:
			printf("^const1");
			break;
		case lima_pp_lir_pipeline_reg_sampler:
			printf("^sampler");
			break;
		case lima_pp_lir_pipeline_reg_uniform:
			printf("^uniform");
			break;
		case lima_pp_lir_pipeline_reg_vmul:
			printf("^vmul");
			break;
		case lima_pp_lir_pipeline_reg_fmul:
			printf("^fmul");
			break;
		case lima_pp_lir_pipeline_reg_discard:
			printf("^discard");
			break;
		default:
			printf("unknown pipeline register %u", (unsigned) reg);
	}
}

static void print_live_vars(bitset_t live_regs)
{
	static char* c = "xyzw";
	unsigned i, j;
	bool first = true;
	
	printf("{");
	
	for (i = 0; i < (live_regs.size * 8) - 1; i++)
	{
		if (bitset_get(live_regs, 4*(i + 1)) ||
			bitset_get(live_regs, 4*(i + 1) + 1) ||
			bitset_get(live_regs, 4*(i + 1) + 2) ||
			bitset_get(live_regs, 4*(i + 1) + 3))
		{
			if (!first)
				printf(", ");
			printf("%%%u.", i);
			for (j = 0; j < 4; j++)
				if (bitset_get(live_regs, 4*(i + 1) + j))
					printf("%c", c[j]);
			first = false;
		}
	}
	
	if (bitset_get(live_regs, 0) ||
		bitset_get(live_regs, 1) ||
		bitset_get(live_regs, 2) ||
		bitset_get(live_regs, 3))
	{
		if (!first)
			printf(", ");
		printf("$0.");
		for (j = 0; j < 4; j++)
			if (bitset_get(live_regs, j))
				printf("%c", c[j]);
		first = false;
	}
	
	printf("}\n");
}


static bool print_dest(lima_pp_lir_dest_t* dest, lima_pp_hir_op_e op)
{

	if (!dest->pipeline)
	{
		switch (dest->reg->size)
		{
			case 1:
				printf("float");
				break;
		
			case 2:
			case 3:
			case 4:
				printf("vec%u", dest->reg->size);
				break;
			default:
				fprintf(stderr, "Error: unknown destination register size %u\n", 
						dest->reg->size);
				return false;
		}
	}
	
	switch (dest->modifier) {
		case lima_pp_outmod_none:
			break;
		case lima_pp_outmod_clamp_fraction:
			printf(" sat");
			break;
		case lima_pp_outmod_clamp_positive:
			printf(" pos");
			break;
		case lima_pp_outmod_round:
			printf(" int");
			break;
		default:
			fprintf(stderr, "Error: unknown output modifier %u\n", 
					dest->modifier);
			return false;
	}
	
	printf(" ");
	if (dest->pipeline)
		print_pipeline_reg(dest->pipeline_reg);
	else
		print_reg(dest->reg);

	printf(".");
	static char* c = "xyzw";
	
	unsigned i;
	for (i = 0; i < 4; i++)
		if (dest->mask[i])
			printf("%c", c[i]);
	
	printf(" = ");
	
	return true;
}

static void print_source(lima_pp_lir_source_t* source)
{
	if (source->negate)
		printf("-");
	if (source->absolute)
		printf("abs(");
	
	if(source->constant)
	{
		printf("(");
		bool first = true;
		double *constant = source->reg;
		unsigned i;
		for (i = 0; i < 4; i++)
		{
			if (!first)
				printf(", ");
			printf("%lf", constant[i]);
			first = false;
		}
		printf(")");
	}
	else
	{
		if (source->pipeline)
			print_pipeline_reg(source->pipeline_reg);
		else
			print_reg(source->reg);
		printf(".");
		unsigned i;
		const char* c = "xyzw";
		for (i = 0; i < 4; i++)
		{
			printf("%c", c[source->swizzle[i]]);
		}
	}
	
	if (source->absolute)
		printf(")");
}

bool lima_pp_lir_instr_print(lima_pp_lir_instr_t* instr, bool live_vars, unsigned tabs)
{
	unsigned i;
	
	if (live_vars)
	{
			print_tabs(tabs);
			print_live_vars(instr->live_in);
	}
	
	print_tabs(tabs);
	
	if (lima_pp_hir_op[instr->op].has_dest
		&& !print_dest(&instr->dest, instr->op))
			return false;
	
	printf("%s ", lima_pp_hir_op[instr->op].name);
	
	if (lima_pp_hir_op_is_load_store(instr->op))
	{
		printf("%u", instr->load_store_index);
		if (lima_pp_hir_op_is_store(instr->op))
		{
			if (lima_pp_hir_op[instr->op].args == 2)
			{
				printf(" + ");
				print_source(&instr->sources[0]);
			}
			printf(" = ");
		}
		else if(lima_pp_hir_op[instr->op].args)
			printf(", ");
	}
	
	if (lima_pp_hir_op_is_store(instr->op) &&
		lima_pp_hir_op[instr->op].args == 2)
	{
		print_source(&instr->sources[1]);
	}
	else
	{
		bool first = true;
		for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
		{
			if (!first)
				printf(", ");
			print_source(&instr->sources[i]);
			first = false;
		}
		
		if (instr->op == lima_pp_hir_op_mul && instr->shift != 0)
		{
			printf(" << %d", instr->shift);
		}
	}
	
	if (lima_pp_hir_op_is_branch(instr->op))
	{
		if (instr->op != lima_pp_hir_op_branch)
			printf(", ");
		printf("%u", instr->branch_dest);
	}
	
	printf(";\n");

	if (live_vars)
	{
		print_tabs(tabs);
		print_live_vars(instr->live_out);
	}

	return true;
}

static void print_instr_set(ptrset_t set)
{
	ptrset_iter_t iter = ptrset_iter_create(set);
	lima_pp_lir_scheduled_instr_t* instr;
	ptrset_iter_for_each(iter, instr)
	{
		printf("%u ", instr->index);
	}
}

bool lima_pp_lir_scheduled_instr_print(lima_pp_lir_scheduled_instr_t* instr, bool live_vars)
{
	if (live_vars)
		print_live_vars(instr->live_in);
	
	printf("//(%u)\n", instr->index);
	if (ptrset_size(instr->preds))
	{
		printf("//preds: ");
		print_instr_set(instr->preds);
		printf("\n");
	}
	if (ptrset_size(instr->succs))
	{
		printf("//succs: ");
		print_instr_set(instr->succs);
		printf("\n");
	}
	if (ptrset_size(instr->true_preds))
	{
		printf("//true preds: ");
		print_instr_set(instr->true_preds);
		printf("\n");
	}
	if (ptrset_size(instr->true_succs))
	{
		printf("//true succs: ");
		print_instr_set(instr->true_succs);
		printf("\n");
	}
	if (ptrset_size(instr->min_preds))
	{
		printf("//min preds: ");
		print_instr_set(instr->min_preds);
		printf("\n");
	}
	if (ptrset_size(instr->min_succs))
	{
		printf("//min succs: ");
		print_instr_set(instr->min_succs);
		printf("\n");
	}

	printf("{\n");
	if (instr->const0_size)
	{
		printf("\t^const0 = ");
		unsigned i;
		bool first = true;
		for (i = 0; i < instr->const0_size; i++)
		{
			if (!first)
				printf(", ");
			else
				first = false;
			
			printf("%lf", instr->const0[i]);
		}
		printf(";\n");
	}

	if (instr->const1_size)
	{
		printf("\t^const1 = ");
		unsigned i;
		bool first = true;
		for (i = 0; i < instr->const1_size; i++)
		{
			if (!first)
				printf(", ");
			else
				first = false;
			
			printf("%lf", instr->const1[i]);
		}
		printf(";\n");
	}

	if (instr->varying_instr)
	{
		if (!lima_pp_lir_instr_print(instr->varying_instr, live_vars, 1))
			return false;
	}
	
	if (instr->texld_instr)
	{
		if (!lima_pp_lir_instr_print(instr->texld_instr, live_vars, 1))
			return false;
	}
	
	if (instr->uniform_instr)
	{
		if (!lima_pp_lir_instr_print(instr->uniform_instr, live_vars, 1))
			return false;
	}
	
	unsigned i;
	for (i = 0; i < 5; i++)
	{
		if (!instr->alu_instrs[i])
			continue;
		
		if (!lima_pp_lir_instr_print(instr->alu_instrs[i], live_vars, 1))
			return false;
	}
	
	if (instr->temp_store_instr)
	{
		if (!lima_pp_lir_instr_print(instr->temp_store_instr, live_vars, 1))
			return false;
	}
	
	if (instr->branch_instr)
	{
		if (!lima_pp_lir_instr_print(instr->branch_instr, live_vars, 1))
			return false;
	}
	
	printf("}\n");
	
	if (live_vars)
		print_live_vars(instr->live_out);
	
	return true;
}

static void index_instrs(lima_pp_lir_block_t* block)
{
	lima_pp_lir_scheduled_instr_t* instr;
	unsigned i = 0;
	pp_lir_block_for_each_instr(block, instr)
	{
		instr->index = i++;
	}
}

bool lima_pp_lir_block_print(lima_pp_lir_block_t* block, bool live_vars)
{
	index_instrs(block);
	
	if (live_vars)
		print_live_vars(block->live_in);
	
	lima_pp_lir_scheduled_instr_t* instr;
	pp_lir_block_for_each_instr(block, instr)
	{
		if (!lima_pp_lir_scheduled_instr_print(instr, live_vars))
			return false;
	}
	
	if (live_vars)
		print_live_vars(block->live_out);
	
	if (block->is_end)
	{
		if (block->discard)
			printf("discard;\n");
		else
			printf("stop;\n");
	}

	printf("\n");
	
	return true;
}

bool lima_pp_lir_prog_print(lima_pp_lir_prog_t* prog, bool live_vars)
{
	unsigned i;
	for (i = 0; i < prog->num_blocks; i++)
	{
		printf("%u:\n", i);
		if (!lima_pp_lir_block_print(prog->blocks[i], live_vars))
			return false;
	}
	return true;
}
