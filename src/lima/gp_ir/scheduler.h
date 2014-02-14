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

#ifndef __scheduler_h__
#define __scheduler_h__

#include "gp_ir.h"


/*
 * structure for storing information about a given dependency
 * Combined with info about instruction placement, should be enough to
 * allow the scheduler to determine if the placement is legal.
 */

typedef struct lima_gp_ir_dep_info_s
{
	/* predecessor - node which must be excecuted first */
	lima_gp_ir_node_t* pred;
	
	/* successor - node which must be excecuted last */
	lima_gp_ir_node_t* succ;
	
	/*
	 * true - is a dependency between a child and parent node
	 * false - is a read/write ordering dependency
	 */
	bool is_child_dep;
	
	/* 
	 * For temp stores, tells us whether this is an input or an offset.
	 * We need to know this because offsets and inputs must be scheduled
	 * differently.
	 */
	bool is_offset;
} lima_gp_ir_dep_info_t;

typedef struct
{
	lima_gp_ir_block_t* block;
	struct list instr_list;
	
	/* Multiply 0/1 slot */
	
	lima_gp_ir_node_t* mul_slots[2];
	
	/* Add 0/1 slot */
	
	lima_gp_ir_node_t* add_slots[2];
	
	/* Uniform load slot */
	
	lima_gp_ir_node_t* uniform_slot[6];
	unsigned uniform_slot_num_used;
	unsigned uniform_index;
	unsigned uniform_off_reg; // 0 = no offset, 1 = offset register 0, etc.
	bool uniform_is_temp;
	
	/* Attribute/Register load slot */
	
	lima_gp_ir_node_t* attr_reg_slot[6];
	unsigned attr_reg_slot_num_used;
	bool attr_reg_slot_is_attr; //true = attribute, false = register
	bool attr_reg_is_phys_reg;
	unsigned attr_reg_index;
	
	/* Register load slot */
	
	lima_gp_ir_node_t* reg_slot[6];
	unsigned reg_slot_num_used;
	unsigned reg_index;
	bool reg_is_phys_reg;
	
	/* Branch slot */
	
	lima_gp_ir_node_t* branch_slot;
	
	/* Store slot */
	
	lima_gp_ir_node_t* store_slot[4];
	bool store_slot_mask[4];
	unsigned store_slot_num_used;
	bool store_slot_is_temp;
	bool store_slot_is_varying;
	unsigned store_slot_index;
	unsigned num_unscheduled_store_children;
	
	/* Complex slot */
	
	lima_gp_ir_node_t* complex_slot;
	
	/* Passthrough slot */
	
	lima_gp_ir_node_t* pass_slot;
} lima_gp_ir_instr_t;

#define gp_ir_block_for_each_instr(block, instr) \
	list_for_each_entry(instr, &block->instr_list, instr_list)
#define gp_ir_block_first_instr(block) \
	container_of(block->instr_list.next, lima_gp_ir_instr_t, instr_list)
#define gp_ir_block_last_instr(block) \
	container_of(block->instr_list.prev, lima_gp_ir_instr_t, instr_list)
#define gp_ir_instr_next(instr) \
	container_of(instr->instr_list.next, lima_gp_ir_instr_t, instr_list)
#define gp_ir_instr_prev(instr) \
	container_of(instr->instr_list.prev, lima_gp_ir_instr_t, instr_list)
#define gp_ir_instr_is_start(instr) \
	((instr)->instr_list.prev == &(instr)->block->instr_list)
#define gp_ir_instr_is_end(instr) \
	((instr)->instr_list.next == &(instr)->block->instr_list)

bool lima_gp_ir_prog_calc_dependencies(lima_gp_ir_prog_t* prog);
bool lima_gp_ir_block_calc_dependencies(lima_gp_ir_block_t* block);
bool lima_gp_ir_dep_info_insert(lima_gp_ir_dep_info_t* dep_info);
void lima_gp_ir_dep_info_delete(lima_gp_ir_dep_info_t* dep_info);
lima_gp_ir_dep_info_t* lima_gp_ir_dep_info_find(lima_gp_ir_node_t* pred,
												lima_gp_ir_node_t* succ);

void lima_gp_ir_block_print_dep_info(lima_gp_ir_block_t* block);
void lima_gp_ir_prog_print_dep_info(lima_gp_ir_prog_t* prog);

unsigned lima_gp_ir_dep_info_get_min_dist(lima_gp_ir_dep_info_t* dep_info);
unsigned lima_gp_ir_dep_info_get_max_dist(lima_gp_ir_dep_info_t* dep_info);

bool lima_gp_ir_block_calc_crit_path(lima_gp_ir_block_t* block);
bool lima_gp_ir_prog_calc_crit_path(lima_gp_ir_prog_t* prog);

lima_gp_ir_instr_t* lima_gp_ir_instr_create(void);
void lima_gp_ir_instr_insert_start(
	lima_gp_ir_block_t* block, lima_gp_ir_instr_t* instr);
void lima_gp_ir_instr_insert_end(
	lima_gp_ir_block_t* block, lima_gp_ir_instr_t* instr);
bool lima_gp_ir_instr_try_insert_node(
	lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node);
void lima_gp_ir_instr_remove_alu_node(
	lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node);
void lima_gp_ir_instr_delete(lima_gp_ir_instr_t* instr);

bool lima_gp_ir_schedule_block(lima_gp_ir_block_t* block);
bool lima_gp_ir_schedule_prog(lima_gp_ir_prog_t* prog);

/* register allocation within the scheduler */

bitset_t lima_gp_ir_regalloc_get_free_regs(lima_gp_ir_block_t* block);
bool lima_gp_ir_regalloc_scalar_fast(lima_gp_ir_reg_t* reg, bitset_t free_regs);
bool lima_gp_ir_regalloc_scalar_slow(lima_gp_ir_reg_t* reg); //TODO
bool lima_gp_ir_liveness_compute_node(lima_gp_ir_root_node_t* node,
									  bitset_t live_before, bool virt);

#endif
