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
#include <stdlib.h>
#include <string.h>

lima_pp_lir_reg_t* lima_pp_lir_reg_create(void)
{
	lima_pp_lir_reg_t* reg = calloc(sizeof(lima_pp_lir_reg_t), 1);
	if (!ptrset_create(&reg->defs))
	{
		free(reg);
		return NULL;
	}

	if (!ptrset_create(&reg->uses))
	{
		ptrset_delete(reg->defs);
		free(reg);
		return NULL;
	}
	
	if (!ptrset_create(&reg->moves))
	{
		ptrset_delete(reg->defs);
		ptrset_delete(reg->uses);
		free(reg);
		return NULL;
	}
	
	reg->adjacent = ptr_vector_create();
	reg->state = lima_pp_lir_reg_state_initial;
	reg->alias = NULL;
	
	return reg;
}

void lima_pp_lir_reg_delete(lima_pp_lir_reg_t* reg)
{
	ptrset_delete(reg->defs);
	ptrset_delete(reg->uses);
	ptrset_delete(reg->moves);
	ptr_vector_delete(reg->adjacent);
	free(reg);
}
