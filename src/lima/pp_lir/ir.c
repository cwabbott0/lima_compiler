/* Author(s):
 *  Connor Abbott (connor@abbott.cx)
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



#include "pp_lir.h"
#include <stdlib.h>

/*static ogt_program_t* ogt_lower_to_pp_asm(ogt_arch_t* dest_arch,
										  ogt_ir_program_t* prog)
{
	if (dest_arch != ogt_arch_lima_pp)
		return NULL;
	
	lima_pp_lir_calc_dep_info(prog->prog);
	
	if (!lima_pp_lir_peephole(prog->prog))
		return NULL;
	
	lima_pp_lir_prog_print(prog->prog, false);
	
	if (!lima_pp_lir_reg_pressure_schedule_prog(prog->prog))
		return NULL;
	
	lima_pp_lir_prog_print(prog->prog, false);
	
	lima_pp_lir_delete_dep_info(prog->prog);
	
	if (!lima_pp_lir_regalloc(prog->prog))
		return NULL;
	
	lima_pp_lir_calc_dep_info(prog->prog);
	
	lima_pp_lir_prog_print(prog->prog, false);
	
	if (!lima_pp_lir_combine_schedule_prog(prog->prog))
		return NULL;
	
	lima_pp_lir_prog_print(prog->prog, false);
	
	lima_pp_lir_delete_dep_info(prog->prog);
	
	ogt_program_t* ret = lima_pp_lir_codegen(prog->prog, prog->symbol_table);
	
	return ret;
}*/
