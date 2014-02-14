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

#ifndef __pp_lir_h__
#define __pp_lir_h__

#include "../pp_hir/pp_hir.h"
#include "bitset.h"
#include "ptrset.h"
#include "ptr_vector.h"


/*
 * IR for the pixel processor backend.
 * Differences from the normal IR:
 * * Registers are stored differently, because we're not in SSA anymore.
 *   Instead, there's now a list of all registers. Each register stores
 *   a list of all its defs/uses, and
 *   instructions point to the appropriate entry as well.
 * * Basic blocks now have a defined ordering for live interval analysis;
 *   we can now assume that basic blocks will not be changed / removed.
 */

struct lima_pp_lir_block_s;
struct lima_pp_lir_prog_s;

typedef enum {
	lima_pp_lir_reg_state_initial,
	lima_pp_lir_reg_state_to_simplify,
	lima_pp_lir_reg_state_simplified,
	lima_pp_lir_reg_state_to_spill,
	lima_pp_lir_reg_state_spilled,
	lima_pp_lir_reg_state_to_freeze,
	lima_pp_lir_reg_state_colored,
	lima_pp_lir_reg_state_coalesced,
} lima_pp_lir_reg_state_e;

typedef struct lima_pp_lir_reg_s {
	unsigned index;
	bool precolored;
	unsigned size; // 1-4
	ptrset_t defs, uses;
	
	//Whether this register has to be allocated at the beginning of an allocated register
	//Used for dests of varying/uniform/temp loads, and source of temp stores,
	//Because we don't have a bitfield for swizzling the source/dest
	bool beginning;
	
	//Whether this register was created as a result of spilling another register
	bool spilled;
	
	lima_pp_lir_reg_state_e state;
	
	//List of registers this one interferes with
	ptr_vector_t adjacent;
	
	//Color assigned by select phase
	unsigned allocated_index;
	unsigned allocated_offset;
	
	//Moves using/defining this register
	ptrset_t moves;
	
	unsigned q_total;
	
	//Register this register has been coalesced with, if in coalesced state
	struct lima_pp_lir_reg_s* alias;
	//For each component of this register, the component of the aliased register
	//that it corresponds with
	unsigned alias_swizzle[4];
	
	struct lima_pp_lir_prog_s* prog;
} lima_pp_lir_reg_t;

typedef enum {
	lima_pp_lir_pipeline_reg_const0,
	lima_pp_lir_pipeline_reg_const1,
	lima_pp_lir_pipeline_reg_sampler,
	lima_pp_lir_pipeline_reg_uniform,
	lima_pp_lir_pipeline_reg_vmul,
	lima_pp_lir_pipeline_reg_fmul,
	lima_pp_lir_pipeline_reg_discard /* varying load */
} lima_pp_lir_pipeline_reg_e;

/* Note: we need to track the liveness of each component
 * of each register seperately, because it is possible that
 * at some point, some components will be live and others
 * will be dead at the same time. For example, in this code:
 *
 * 1: $0.xy = ...;
 * 2: $0.zw = ...;
 * 3: stop;
 *
 * after exiting statement 1, $0.xy is live (since it will be
 * consumed by statement 3), but $0.zw is dead (since it will
 * be rewritten by statement 2).
 *
 * To keep things simple, each register is given 4 slots, even
 * if it's actually smaller than that, and unused slots are
 * always marked as dead.
 */

typedef struct {
	bool constant;
	bool pipeline;
	void *reg;
	lima_pp_lir_pipeline_reg_e pipeline_reg;
	unsigned swizzle[4];
	bool absolute, negate;
} lima_pp_lir_source_t;

typedef struct {
	bool pipeline;
	lima_pp_lir_reg_t* reg;
	lima_pp_lir_pipeline_reg_e pipeline_reg;
	bool mask[4];
	lima_pp_outmod_e modifier;
} lima_pp_lir_dest_t;

typedef struct lima_pp_lir_instr_s {
	lima_pp_hir_op_e op;
	lima_pp_lir_source_t sources[3];
	lima_pp_lir_dest_t dest;
	int shift : 3; //Only used for lima_pp_hir_op_mul
	
	struct lima_pp_lir_scheduled_instr_s* sched_instr;

	unsigned load_store_index;
	
	unsigned branch_dest;
	
	// liveness info
	bitset_t live_in, live_out;
} lima_pp_lir_instr_t;

typedef enum
{
	lima_pp_lir_alu_vector_mul,
	lima_pp_lir_alu_scalar_mul,
	lima_pp_lir_alu_vector_add,
	lima_pp_lir_alu_scalar_add,
	lima_pp_lir_alu_combine
} lima_pp_lir_alu_e;

typedef struct lima_pp_lir_scheduled_instr_s {
	struct list instr_list;
	
	struct lima_pp_lir_block_s* block;
	
	lima_pp_lir_instr_t* varying_instr;
	lima_pp_lir_instr_t* texld_instr;
	lima_pp_lir_instr_t* uniform_instr;
	
	lima_pp_lir_instr_t* alu_instrs[5];
	bool possible_alu_instr_pos[5][5];
	
	lima_pp_lir_instr_t* temp_store_instr;
	
	lima_pp_lir_instr_t* branch_instr;
	
	double const0[4], const1[4];
	unsigned const0_size, const1_size;
	
	bitset_t live_in, live_out;
	
	unsigned index;
	
	/* bitset of register components read and written, used in calculating
	 * dependency info
	 */
	
	bitset_t read_regs, write_regs;
	
	/* successors and predecessors in the dependency graph */
	ptrset_t preds, succs;
	
	/* successors and predecessors in the transitive reduction of the
	 * dependency graph
	 */
	ptrset_t min_preds, min_succs;
	
	/* predecessors and successors due to true (read-after-write) dependencies */
	ptrset_t true_preds, true_succs;
	
	/* longest distance from some starting node */
	unsigned max_dist;
	
	/* estimated register pressure */
	unsigned reg_pressure;
	
	bool visited;
} lima_pp_lir_scheduled_instr_t;

typedef struct lima_pp_lir_block_s {
	struct list instr_list;
	unsigned num_instrs;
	
	struct lima_pp_lir_prog_s* prog;
	
	unsigned num_preds;
	unsigned* preds;
	
	unsigned num_succs;
	unsigned succs[2];
	
	bool is_end;
	bool discard;
	
	bitset_t live_in, live_out;
} lima_pp_lir_block_t;

typedef struct lima_pp_lir_prog_s {
	unsigned num_blocks;
	lima_pp_lir_block_t** blocks;
	
	unsigned reg_alloc, temp_alloc;
	unsigned num_regs;
	lima_pp_lir_reg_t** regs;
} lima_pp_lir_prog_t;

lima_pp_lir_prog_t* lima_pp_lir_convert(lima_pp_hir_prog_t* prog);

lima_pp_lir_prog_t* lima_pp_lir_prog_create(void);
void lima_pp_lir_prog_delete(lima_pp_lir_prog_t* prog);
void* lima_pp_lir_prog_export(lima_pp_lir_prog_t* prog, unsigned* size);
lima_pp_lir_prog_t* lima_pp_lir_prog_import(void* data, unsigned size);
bool lima_pp_lir_prog_append_reg(
	lima_pp_lir_prog_t* prog,
	lima_pp_lir_reg_t* reg);
bool lima_pp_lir_prog_delete_reg(lima_pp_lir_prog_t* prog,
	unsigned index);
lima_pp_lir_reg_t* lima_pp_lir_prog_find_reg(
	lima_pp_lir_prog_t* prog, unsigned index, bool precolored);

lima_pp_lir_block_t* lima_pp_lir_block_create(void);
void lima_pp_lir_block_delete(lima_pp_lir_block_t* block);
void* lima_pp_lir_block_export(lima_pp_lir_block_t* block, unsigned* size);
lima_pp_lir_block_t* lima_pp_lir_block_import(
	void* data, unsigned* len, lima_pp_lir_prog_t* prog);

void lima_pp_lir_block_insert_start(
	lima_pp_lir_block_t* block,
	lima_pp_lir_scheduled_instr_t* instr);
void lima_pp_lir_block_insert_end(
	lima_pp_lir_block_t* block,
	lima_pp_lir_scheduled_instr_t* instr);
void lima_pp_lir_block_insert(
	lima_pp_lir_scheduled_instr_t* instr,
	lima_pp_lir_scheduled_instr_t* before);
void lima_pp_lir_block_insert_before(
	lima_pp_lir_scheduled_instr_t* instr,
	lima_pp_lir_scheduled_instr_t* after);
void lima_pp_lir_block_remove(
	lima_pp_lir_scheduled_instr_t* instr);

#define pp_lir_block_for_each_instr(block, instr) \
	list_for_each_entry(instr, &block->instr_list, instr_list)
#define pp_lir_block_for_each_instr_reverse(block, instr) \
	list_for_each_entry_reverse(instr, &block->instr_list, instr_list)
#define pp_lir_block_for_each_instr_safe(block, tmp_instr, instr) \
	list_for_each_entry_safe(instr, tmp_instr, &block->instr_list, instr_list)
#define pp_lir_block_first_instr(block) \
	container_of(block->instr_list.next, lima_pp_lir_scheduled_instr_t, instr_list)
#define pp_lir_block_last_instr(block) \
	container_of(block->instr_list.prev, lima_pp_lir_scheduled_instr_t, instr_list)
#define pp_lir_block_next_instr(instr) \
	container_of(instr->instr_list.next, lima_pp_lir_scheduled_instr_t, instr_list)
#define pp_lir_block_prev_instr(instr) \
	container_of(instr->instr_list.prev, lima_pp_lir_scheduled_instr_t, instr_list)

lima_pp_lir_reg_t* lima_pp_lir_reg_create(void);
void lima_pp_lir_reg_delete(lima_pp_lir_reg_t* reg);


lima_pp_lir_instr_t* lima_pp_lir_instr_create(void);
void lima_pp_lir_instr_delete(lima_pp_lir_instr_t* instr);
void* lima_pp_lir_instr_export(lima_pp_lir_instr_t* instr, unsigned* size);
lima_pp_lir_instr_t* lima_pp_lir_instr_import(
	void* data, unsigned* len, lima_pp_lir_prog_t* prog);

lima_pp_lir_scheduled_instr_t* lima_pp_lir_scheduled_instr_create(void);
void lima_pp_lir_scheduled_instr_delete(lima_pp_lir_scheduled_instr_t* instr);
void* lima_pp_lir_scheduled_instr_export(
	lima_pp_lir_scheduled_instr_t* instr, unsigned* size);
lima_pp_lir_scheduled_instr_t* lima_pp_lir_scheduled_instr_import(
	void* data, unsigned* len, lima_pp_lir_prog_t* prog);
lima_pp_lir_scheduled_instr_t* lima_pp_lir_instr_to_sched_instr(
	lima_pp_lir_instr_t* instr);
bool lima_pp_lir_sched_instr_is_empty(lima_pp_lir_scheduled_instr_t* instr);
void lima_pp_lir_instr_compress_consts(lima_pp_lir_scheduled_instr_t* instr);

bool lima_pp_lir_liveness_init(lima_pp_lir_prog_t* prog);
void lima_pp_lir_liveness_delete(lima_pp_lir_prog_t* prog);
bool lima_pp_lir_liveness_calc_instr(lima_pp_lir_instr_t* instr);
bool lima_pp_lir_liveness_calc_scheduled_instr(lima_pp_lir_scheduled_instr_t* instr);
bool lima_pp_lir_liveness_calc_block(lima_pp_lir_block_t* block);
void lima_pp_lir_liveness_calc_prog(lima_pp_lir_prog_t* prog);

bool lima_pp_lir_instr_print(
	lima_pp_lir_instr_t* instr,
	bool print_live_vars, unsigned tabs);
bool lima_pp_lir_block_print(
	lima_pp_lir_block_t* block,
	bool print_live_vars);
bool lima_pp_lir_prog_print(
	lima_pp_lir_prog_t* prog, bool print_live_vars);

bool lima_pp_lir_instr_can_swap(
	lima_pp_lir_instr_t* before, lima_pp_lir_instr_t* after);

bool lima_pp_lir_regalloc(lima_pp_lir_prog_t* prog);

void lima_pp_lir_calc_dep_info(lima_pp_lir_prog_t* prog);
void lima_pp_lir_calc_min_dep_info(lima_pp_lir_block_t* block);
void lima_pp_lir_delete_dep_info(lima_pp_lir_prog_t* prog);

bool lima_pp_lir_peephole(lima_pp_lir_prog_t* prog);

bool lima_pp_lir_simple_schedule_prog(lima_pp_lir_prog_t* prog);

bool lima_pp_lir_reg_pressure_schedule_block(lima_pp_lir_block_t* block);
bool lima_pp_lir_reg_pressure_schedule_prog(lima_pp_lir_prog_t* prog);

bool lima_pp_lir_combine_schedule_block(lima_pp_lir_block_t* block);
bool lima_pp_lir_combine_schedule_prog(lima_pp_lir_prog_t* prog);

bool lima_pp_lir_instr_combine_before(
	lima_pp_lir_scheduled_instr_t* before,
	lima_pp_lir_scheduled_instr_t* instr);

bool lima_pp_lir_instr_combine_after(
	lima_pp_lir_scheduled_instr_t* after,
	lima_pp_lir_scheduled_instr_t* instr);

bool lima_pp_lir_instr_combine_indep(
	lima_pp_lir_scheduled_instr_t* instr,
	lima_pp_lir_scheduled_instr_t* other);

void* lima_pp_lir_codegen(lima_pp_lir_prog_t* prog, unsigned* code_size);
//Returns the number of channels possibly used for an argument
static inline unsigned lima_pp_lir_arg_size(lima_pp_lir_instr_t* instr, unsigned arg)
{
	if (lima_pp_hir_op[instr->op].arg_sizes[arg] != 0)
		return lima_pp_hir_op[instr->op].arg_sizes[arg];
	if (!instr->dest.pipeline)
		return instr->dest.reg->size;
	else
		return 4;
}


//Returns whether a channel is actually used
//Assumes channel < the result of lima_pp_lir_arg_size()
static inline bool lima_pp_lir_channel_used(lima_pp_lir_instr_t* instr, unsigned arg,
											unsigned channel)
{	if (lima_pp_hir_op[instr->op].arg_sizes[arg] != 0)
		return true;
	return instr->dest.mask[channel];
}

#endif
