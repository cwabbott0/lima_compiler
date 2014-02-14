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

#ifndef __scheduler_h__
#define __scheduler_h__

#include "pp_lir.h"

/* 
 * scheduler.h
 *
 * internal header, provides the common boilerplate code for implementing a
 * scheduler. To actually implement a scheduler, you need to implement a
 * function to insert instructions using the sched_insert_before(),
 * sched_insert_start(), and sched_insert_end() helpers - you can also combine
 * the instruction, as long as it goes before its earliest successor, as well
 * as a function to determine which instructions to prioritize. Note that we
 * schedule *backwards*, starting from instructions that have no successors.
 */

static inline void sched_insert_before(lima_pp_lir_scheduled_instr_t* instr,
									   lima_pp_lir_scheduled_instr_t* after)
{
	list_add(&instr->instr_list, after->instr_list.prev);
	instr->index = after->index + 1;
	instr->block->num_instrs++;
}

static inline void sched_insert_end(lima_pp_lir_scheduled_instr_t* instr)
{
	list_add(&instr->instr_list, instr->block->instr_list.prev);
	instr->index = 0;
	instr->block->num_instrs++;
}

static inline void sched_insert_start(lima_pp_lir_scheduled_instr_t* instr)
{
	list_add(&instr->instr_list, &instr->block->instr_list);
	instr->index = instr->block->num_instrs;
	instr->block->num_instrs++;
}

typedef bool (*sched_insert_cb)(
	lima_pp_lir_scheduled_instr_t* instr);

typedef bool (*sched_priority_cb)(
	void* _instr1, void* _instr2);

bool lima_pp_lir_schedule_block(lima_pp_lir_block_t* block,
								sched_priority_cb sched_priority,
								sched_insert_cb sched_insert);

bool lima_pp_lir_schedule_prog(lima_pp_lir_prog_t* prog,
							   sched_priority_cb sched_priority,
							   sched_insert_cb sched_insert);


#endif
