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

#include "scheduler.h"

#include <stdlib.h>

/*static ogt_program_t* ogt_lower_to_gp_asm(ogt_arch_t* dest_arch,
										  ogt_ir_program_t* prog)
{
	if (dest_arch != ogt_arch_lima_gp)
		return NULL;
	
#if 0 //For testing sin/cos lowering
	if (!lima_gp_ir_lower_prog(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, false))
		return false;
	
	if (!lima_gp_ir_const_fold_prog(prog->prog))
		return NULL;
#else
	if (!lima_gp_ir_const_fold_prog(prog->prog))
		return NULL;
#endif
	
	if (!lima_gp_ir_convert_to_ssa(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, false))
		return NULL;
	
	if (!lima_gp_ir_if_convert(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, false))
		return NULL;
	
	if (!lima_gp_ir_dead_code_eliminate(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, false))
		return NULL;
	
	if (!lima_gp_ir_reg_eliminate(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, false))
		return NULL;
	
	if (!lima_gp_ir_eliminate_phi_nodes(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_liveness_compute_prog(prog->prog, true))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, true))
		return NULL;
	
	if (!lima_gp_ir_regalloc(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_lower_prog(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_print(prog->prog, 0, false))
		return NULL;
	
	if (!lima_gp_ir_prog_calc_dependencies(prog->prog))
		return NULL;
	
	if (!lima_gp_ir_prog_calc_crit_path(prog->prog))
		return NULL;
	
	//lima_gp_ir_prog_print_dep_info(prog->prog);
	
	if (!lima_gp_ir_schedule_prog(prog->prog))
		return NULL;
	
	return lima_gp_ir_codegen(prog->prog, prog->symbol_table);
}*/
