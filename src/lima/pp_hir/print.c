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
#include <stdio.h>



static void reg_print(lima_pp_hir_reg_t* reg)
{
	printf("%%%u", reg->index);
}

static void source_print(lima_pp_hir_source_t* src)
{
	if (src->negate)
		printf("-");
	if (src->absolute)
		printf("abs(");

	if (src->constant)
	{
		double* v = src->depend;
		double vs[4] =
		{
			v[src->swizzle[0]], v[src->swizzle[1]],
			v[src->swizzle[2]], v[src->swizzle[3]]
		};

		if ((vs[0] == vs[2])
			&& (vs[1] == vs[3]))
		{
			if (vs[0] == vs[1])
				printf("%g", vs[0]);
			else
				printf("vec2(%g, %g)",
					vs[0], vs[1]);
		} else {
			printf("vec4(%g, %g, %g, %g)",
				vs[0], vs[1], vs[2], vs[3]);
		}
	} else {
		lima_pp_hir_cmd_t* cmd = src->depend;
		lima_pp_hir_reg_t* reg = &cmd->dst.reg;
		reg_print(reg);
		if (reg->size)
		{
			printf(".");
			const char* c = "xyzw";
			unsigned i;
			for (i = 0; i <= reg->size; i++)
				printf("%c", c[src->swizzle[i]]);
		}
	}

	if (src->absolute)
		printf(")");
}

static void dest_print(lima_pp_hir_dest_t* dst)
{
	if (dst->reg.size)
		printf("vec%u ", (dst->reg.size + 1));
	else
		printf("float ");

	reg_print(&dst->reg);

	printf(" = ");
}

static int get_block_index(lima_pp_hir_block_t* block, lima_pp_hir_prog_t* prog)
{
	unsigned i = 0;
	lima_pp_hir_block_t* iter_block;
	pp_hir_prog_for_each_block(prog, iter_block)
	{
		if (iter_block == block)
			return i;
		i++;
	}
	return -1;
}

void lima_pp_hir_cmd_print(lima_pp_hir_cmd_t* cmd, lima_pp_hir_block_t* block,
					   lima_pp_hir_prog_t* prog)
{
	if (lima_pp_hir_op[cmd->op].has_dest)
		dest_print(&cmd->dst);

	lima_pp_hir_op_t op
		= lima_pp_hir_op[cmd->op];

	printf("%s ", op.name);
	
	if (lima_pp_hir_op_is_load_store(cmd->op))
	{
		printf("%u", cmd->load_store_index);
		if (lima_pp_hir_op_is_store(cmd->op))
		{
			if (lima_pp_hir_op[cmd->op].args == 2)
			{
				printf(" + ");
				source_print(&cmd->src[0]);
			}
			printf(" = ");
		}
		else if(lima_pp_hir_op[cmd->op].args)
			printf(", ");
	}

	if (lima_pp_hir_op_is_store(cmd->op) &&
		lima_pp_hir_op[cmd->op].args == 2)
	{
		source_print(&cmd->src[1]);
	}
	else
	{
		if (cmd->num_args)
		{
			source_print(&cmd->src[0]);
			if (cmd->op == lima_pp_hir_op_phi)
			{
				printf(" : ");
				printf("%u", get_block_index(block->preds[0], prog));
			}

			unsigned i;
			for (i = 1; i < cmd->num_args; i++)
			{
				printf(", ");
				source_print(&cmd->src[i]);
				if (cmd->op == lima_pp_hir_op_phi)
				{
					printf(" : ");
					printf("%u", get_block_index(block->preds[i], prog));
				}
			}
		}
		
		if (cmd->op == lima_pp_hir_op_mul && cmd->shift != 0)
			printf(" << %d", cmd->shift);
	}

	printf(";\n");
}

static void reg_cond_print(lima_pp_hir_reg_cond_t reg_cond)
{
	if (reg_cond.is_constant)
		printf("%g", reg_cond.constant);
	else
		reg_print(&reg_cond.reg->dst.reg);
}

bool lima_pp_hir_block_print(lima_pp_hir_block_t* block, lima_pp_hir_prog_t* prog)
{
	if (!block)
		return false;
	
	int index = get_block_index(block, prog);
	if (index < 0)
	{
		fprintf(stderr, "Error: basic block found not in program\n");
		return false;
	}
	printf("%d:\n", index);

	lima_pp_hir_cmd_t* cmd;
	pp_hir_block_for_each_cmd(block, cmd)
	{
		if (cmd->op != lima_pp_hir_op_phi)
			break;
		lima_pp_hir_cmd_print(cmd, block, prog);
	}
	printf("%%\n");
	pp_hir_block_for_each_cmd(block, cmd)
	{
		if (cmd->op == lima_pp_hir_op_phi)
			continue;
		lima_pp_hir_cmd_print(cmd, block, prog);
	}
	
	if (!block->is_end)
	{
		if (block->branch_cond == lima_pp_hir_branch_cond_always)
			printf("branch %d;\n", get_block_index(block->next[0], prog));
		else
		{
			char *conds[7] = {
				NULL,
				"gt",
				"eq",
				"ge",
				"lt",
				"ne",
				"le"
			};
			printf("branch.%s ", conds[block->branch_cond]);
			reg_cond_print(block->reg_cond_a);
			printf(": %d, ", get_block_index(block->next[0], prog));
			reg_cond_print(block->reg_cond_b);
			printf(": %d;\n", get_block_index(block->next[1], prog));
		}
	}
	else
	{
		if (block->discard)
			printf("discard;\n");
		else
		{
			printf("output ");
			reg_print(&block->output->dst.reg);
			printf(";\n");
		}
	}
		
	printf("\n");

	return true;
}

static void array_print(lima_pp_hir_temp_array_t array)
{
	static char* align[2] = {"1", "4"};
	printf("array align(%s) [%u-%u];\n", align[array.alignment], array.start,
		   array.end);
}

bool lima_pp_hir_prog_print(lima_pp_hir_prog_t *prog)
{
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
		if(!lima_pp_hir_block_print(block, prog))
			return false;
	
	unsigned i;
	for (i = 0; i < prog->num_arrays; i++)
		array_print(prog->arrays[i]);
	
	return true;
}
