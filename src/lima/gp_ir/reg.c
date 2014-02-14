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

#include "gp_ir.h"
#include <stdlib.h>

lima_gp_ir_reg_t* lima_gp_ir_reg_create(lima_gp_ir_prog_t* prog)
{
	lima_gp_ir_reg_t* reg = malloc(sizeof(lima_gp_ir_reg_t));
	if (!reg)
		return NULL;
	
	reg->index = prog->reg_alloc++;
	reg->size = 4;
	reg->phys_reg_assigned = false;
	
	if (!ptrset_create(&reg->uses))
	{
		free(reg);
		return NULL;
	}
	
	if (!ptrset_create(&reg->defs))
	{
		ptrset_delete(reg->uses);
		free(reg);
		return NULL;
	}
	
	reg->prog = prog;
	list_add(&reg->reg_list, prog->reg_list.prev);
	
	return reg;
}

void lima_gp_ir_reg_delete(lima_gp_ir_reg_t* reg)
{
	list_del(&reg->reg_list);
	
	ptrset_delete(reg->defs);
	ptrset_delete(reg->uses);
	free(reg);
}

lima_gp_ir_reg_t* lima_gp_ir_reg_find(lima_gp_ir_prog_t* prog, unsigned index)
{
	lima_gp_ir_reg_t* reg;
	gp_ir_prog_for_each_reg(prog, reg)
	{
		if (reg->index == index)
			return reg;
	}
	
	return NULL;
}
