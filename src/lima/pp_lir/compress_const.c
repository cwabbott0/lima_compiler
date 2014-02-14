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

/*
 * Try to reduce the size of const0 and const1 by eliminating duplicate
 * constants. Helps later optimizations by decreasing the chance that two
 * instructions will fail to combine due to lack of space for constants.
 */

static unsigned gen_const_swizzle(double* consts, unsigned* swizzle,
								  unsigned size)
{
	unsigned new_size = 0;
	
	unsigned i, j;
	for (i = 0; i < size; i++)
	{
		for (j = 0; j < i; j++)
			if (consts[j] == consts[i])
			{
				swizzle[i] = swizzle[j];
				break;
			}
		
		if (j == i)
		{
			swizzle[i] = new_size++;
		}
	}
	
	return new_size;
}

static void swizzle_const_instr(lima_pp_lir_instr_t* instr,
								unsigned* const0_swizzle,
								unsigned* const1_swizzle)
{
	unsigned i, j;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].pipeline)
		{
			if (instr->sources[i].pipeline_reg ==
					lima_pp_lir_pipeline_reg_const0)
			{
				for (j = 0; j < lima_pp_lir_arg_size(instr, i); j++)
				{
					instr->sources[i].swizzle[j] =
						const0_swizzle[instr->sources[i].swizzle[j]];
				}
			}
			
			if (instr->sources[i].pipeline_reg ==
					lima_pp_lir_pipeline_reg_const1)
			{
				for (j = 0; j < lima_pp_lir_arg_size(instr, i); j++)
				{
					instr->sources[i].swizzle[j] =
						const1_swizzle[instr->sources[i].swizzle[j]];
				}
			}
		}
	}
}

void lima_pp_lir_instr_compress_consts(lima_pp_lir_scheduled_instr_t* instr)
{
	if (instr->const0_size == 0 && instr->const1_size == 0)
		return;
	
	unsigned const0_swizzle[4];
	unsigned const1_swizzle[4];
	
	unsigned new_const0_size = gen_const_swizzle(instr->const0, const0_swizzle,
												 instr->const0_size);
	
	
	unsigned new_const1_size = gen_const_swizzle(instr->const1, const1_swizzle,
												 instr->const1_size);
	
	double temp[4];
	
	unsigned i;
	for (i = 0; i < instr->const0_size; i++)
		temp[const0_swizzle[i]] = instr->const0[i];
	
	for (i = new_const0_size; i < 4; i++)
		temp[i] = 0.0;
	
	memcpy(instr->const0, temp, 4 * sizeof(double));
	instr->const0_size = new_const0_size;
	
	for (i = 0; i < instr->const1_size; i++)
		temp[const1_swizzle[i]] = instr->const1[i];
	
	for (i = new_const1_size; i < 4; i++)
		temp[i] = 0.0;
	
	memcpy(instr->const1, temp, 4 * sizeof(double));
	instr->const1_size = new_const1_size;
	
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			swizzle_const_instr(instr->alu_instrs[i], const0_swizzle,
								const1_swizzle);
	
	if (instr->temp_store_instr)
		swizzle_const_instr(instr->temp_store_instr, const0_swizzle,
							const1_swizzle);
	
	if (instr->branch_instr)
		swizzle_const_instr(instr->branch_instr, const0_swizzle,
							const1_swizzle);
}
