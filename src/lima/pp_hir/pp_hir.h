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



#ifndef __lima_pp_hir_h__
#define __lima_pp_hir_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "list.h"
#include "ptrset.h"
#include "../pp/lima_pp.h"


struct lima_pp_hir_block_s;
struct lima_pp_hir_prog_s;

typedef struct
{
	double x, y, z, w;
} lima_pp_hir_vec4_t;

typedef union
__attribute__((__packed__))
{
	struct
	__attribute__((__packed__))
	{
		unsigned index      : 30;
		unsigned size       :  2;
	};
	uint32_t mask;
} lima_pp_hir_reg_t;

static const lima_pp_hir_reg_t
	lima_pp_hir_reg_default =
{{
	.index      = 0,
	.size       = 3,
}};

typedef struct
{
	bool          constant;
	void*         depend;
	unsigned      swizzle[4];
	bool          absolute;
	bool          negate;
} lima_pp_hir_source_t;

static const lima_pp_hir_source_t
	lima_pp_hir_source_default =
{
	.constant = false,
	.depend   = NULL,
	.swizzle  = { 0, 1, 2, 3 },
	.absolute = false,
	.negate   = false,
};

typedef struct
{
	lima_pp_hir_reg_t      reg;
	lima_pp_outmod_e modifier;
} lima_pp_hir_dest_t;

static const lima_pp_hir_dest_t
	lima_pp_hir_dest_default =
{
	.reg      = { .mask = 0xC0000000 },
	.modifier = lima_pp_outmod_none,
};

typedef enum
{
	lima_pp_hir_op_mov,

	lima_pp_hir_op_neg,
	lima_pp_hir_op_add,
	lima_pp_hir_op_sub,
	
	lima_pp_hir_op_ddx,
	lima_pp_hir_op_ddy,

	lima_pp_hir_op_mul,
	lima_pp_hir_op_rcp,
	lima_pp_hir_op_div,

	lima_pp_hir_op_sin_lut,
	lima_pp_hir_op_cos_lut,

	lima_pp_hir_op_sum3,
	lima_pp_hir_op_sum4,
	
	lima_pp_hir_op_normalize2,
	lima_pp_hir_op_normalize3,
	lima_pp_hir_op_normalize4,
	
	lima_pp_hir_op_select,

	lima_pp_hir_op_sin,
	lima_pp_hir_op_cos,
	lima_pp_hir_op_tan,
	lima_pp_hir_op_asin,
	lima_pp_hir_op_acos,
	
	lima_pp_hir_op_atan,
	lima_pp_hir_op_atan2,
	lima_pp_hir_op_atan_pt1,
	lima_pp_hir_op_atan2_pt1,
	lima_pp_hir_op_atan_pt2,

	lima_pp_hir_op_pow,
	lima_pp_hir_op_exp,
	lima_pp_hir_op_log,
	lima_pp_hir_op_exp2,
	lima_pp_hir_op_log2,
	lima_pp_hir_op_sqrt,
	lima_pp_hir_op_rsqrt,

	lima_pp_hir_op_abs,
	lima_pp_hir_op_sign,
	lima_pp_hir_op_floor,
	lima_pp_hir_op_ceil,
	lima_pp_hir_op_fract,
	lima_pp_hir_op_mod,
	lima_pp_hir_op_min,
	lima_pp_hir_op_max,

	lima_pp_hir_op_dot2,
	lima_pp_hir_op_dot3,
	lima_pp_hir_op_dot4,
	
	lima_pp_hir_op_lrp,

	/* NOTE:
	 * We only include the comparison operators the acc units can handle directly,
	 * because otherwise we could mistakenly assign ^vmul or ^fmul as the first input,
	 * when in reality the acc units cannot handle that (because we would have to swap inputs,
	 * leaving ^vmul or ^fmul as the second input, which we cannot codegen).
	 */
	lima_pp_hir_op_gt,
	lima_pp_hir_op_ge,
	lima_pp_hir_op_eq,
	lima_pp_hir_op_ne,
	lima_pp_hir_op_any2,
	lima_pp_hir_op_any3,
	lima_pp_hir_op_any4,
	lima_pp_hir_op_all2,
	lima_pp_hir_op_all3,
	lima_pp_hir_op_all4,
	lima_pp_hir_op_not,
	
	lima_pp_hir_op_phi,
	
	lima_pp_hir_op_combine,
	
	lima_pp_hir_op_loadu_one,
	lima_pp_hir_op_loadu_one_off,
	lima_pp_hir_op_loadu_two,
	lima_pp_hir_op_loadu_two_off,
	lima_pp_hir_op_loadu_four,
	lima_pp_hir_op_loadu_four_off,
	
	lima_pp_hir_op_loadv_one,
	lima_pp_hir_op_loadv_one_off,
	lima_pp_hir_op_loadv_two,
	lima_pp_hir_op_loadv_two_off,
	lima_pp_hir_op_loadv_three,
	lima_pp_hir_op_loadv_three_off,
	lima_pp_hir_op_loadv_four,
	lima_pp_hir_op_loadv_four_off,
	
	lima_pp_hir_op_loadt_one,
	lima_pp_hir_op_loadt_one_off,
	lima_pp_hir_op_loadt_two,
	lima_pp_hir_op_loadt_two_off,
	lima_pp_hir_op_loadt_four,
	lima_pp_hir_op_loadt_four_off,
	
	lima_pp_hir_op_storet_one,
	lima_pp_hir_op_storet_one_off,
	lima_pp_hir_op_storet_two,
	lima_pp_hir_op_storet_two_off,
	lima_pp_hir_op_storet_four,
	lima_pp_hir_op_storet_four_off,
	
	lima_pp_hir_op_frag_coord,
	lima_pp_hir_op_frag_coord_impl,
	lima_pp_hir_op_point_coord,
	lima_pp_hir_op_point_coord_impl,
	lima_pp_hir_op_front_facing,
	
	lima_pp_hir_op_fb_color,
	lima_pp_hir_op_fb_depth,
	
	lima_pp_hir_op_texld_2d,
	lima_pp_hir_op_texld_2d_off,
	lima_pp_hir_op_texld_2d_lod,
	lima_pp_hir_op_texld_2d_off_lod,
	lima_pp_hir_op_texld_2d_proj_z,
	lima_pp_hir_op_texld_2d_proj_z_off,
	lima_pp_hir_op_texld_2d_proj_z_lod,
	lima_pp_hir_op_texld_2d_proj_z_off_lod,
	lima_pp_hir_op_texld_2d_proj_w,
	lima_pp_hir_op_texld_2d_proj_w_off,
	lima_pp_hir_op_texld_2d_proj_w_lod,
	lima_pp_hir_op_texld_2d_proj_w_off_lod,
	lima_pp_hir_op_texld_cube,
	lima_pp_hir_op_texld_cube_off,
	lima_pp_hir_op_texld_cube_lod,
	lima_pp_hir_op_texld_cube_off_lod,
	
	lima_pp_hir_op_branch,
	lima_pp_hir_op_branch_gt,
	lima_pp_hir_op_branch_eq,
	lima_pp_hir_op_branch_ge,
	lima_pp_hir_op_branch_lt,
	lima_pp_hir_op_branch_ne,
	lima_pp_hir_op_branch_le,

	lima_pp_hir_op_count
} lima_pp_hir_op_e;

typedef struct _lima_pp_hir_cmd_t
{
	struct list cmd_list;
	
	struct lima_pp_hir_block_s* block;
	
	lima_pp_hir_op_e               op;
	lima_pp_hir_dest_t             dst;
	
	ptrset_t cmd_uses;
	ptrset_t block_uses;
	
	unsigned                   load_store_index;
	
	/* for dead code elimination */
	bool is_live;
	
	unsigned                   num_args;
	int						   shift : 3; //Only used with lima_pp_hir_op_mul
	lima_pp_hir_source_t       src[1];
} lima_pp_hir_cmd_t;

typedef enum
{
	lima_pp_hir_branch_cond_gt     = 1,
	lima_pp_hir_branch_cond_eq     = 2,
	lima_pp_hir_branch_cond_ge     = 3,
	lima_pp_hir_branch_cond_lt     = 4,
	lima_pp_hir_branch_cond_ne     = 5,
	lima_pp_hir_branch_cond_le     = 6,
	lima_pp_hir_branch_cond_always = 7
} lima_pp_hir_branch_cond_e;

typedef struct
{
	bool is_constant;
	double constant;
	lima_pp_hir_cmd_t* reg;
} lima_pp_hir_reg_cond_t;

typedef struct lima_pp_hir_block_s
{
	unsigned size;
	struct list cmd_list;
	struct list block_list;
	
	struct lima_pp_hir_prog_s* prog;
	
	unsigned index;
	
	unsigned num_preds;
	struct lima_pp_hir_block_s** preds;
	/* true  - ends with an output or discard opcode 
	   false - ends with a branch */
	bool is_end;
	
	/* only relevant if is_end is true,
	 * tells us whether this block ends with
	 * a discard statement. Note that, unlike
	 * some other architectures, we don't have
	 * a conditional discard/kill; instead, we
	 * emulate it with branches.
	 */
	bool discard;
	
	/* if discard is false and is_end is true,
	 * tells us which register to output for
	 * gl_FragColor
	 */
	lima_pp_hir_cmd_t* output;
	
	/* if is_end is false, tells us the branch condition
	 * and which blocks to go to next. if branch_cond
	 * is lima_pp_hir_branch_cond_always, then
	 * next[1], reg_cond_a, and reg_cond_b are
	 * unused.
	 */
	struct lima_pp_hir_block_s* next[2];
	lima_pp_hir_branch_cond_e branch_cond;
	lima_pp_hir_reg_cond_t reg_cond_a;
	lima_pp_hir_reg_cond_t reg_cond_b;
	
	/* dominance information */
	
	struct lima_pp_hir_block_s* imm_dominator;
	ptrset_t dom_tree_children, dominance_frontier;
	
	bool visited; //for cfg traversal
} lima_pp_hir_block_t;

typedef enum
{
	lima_pp_hir_align_one,
	lima_pp_hir_align_two,
	lima_pp_hir_align_four
} lima_pp_hir_align_e;

/*
 * Declares an array in the temporary address space from start to end inclusive
 * that may be accessed indirectly. Note that start and end depend upon the
 * alignment, so an array from 0 to 7 when alignment is one is the same as an
 * array from 0 to 1 when alignment is four.
 */

typedef struct
{
	unsigned start, end;
	lima_pp_hir_align_e alignment;
} lima_pp_hir_temp_array_t;

typedef struct lima_pp_hir_prog_s
{
	struct list block_list;
	unsigned num_blocks;
	unsigned reg_alloc;
	unsigned temp_alloc;
	
	unsigned num_arrays;
	lima_pp_hir_temp_array_t* arrays;
} lima_pp_hir_prog_t;


typedef struct
{
	const char*     name;
	unsigned        args;
	bool            commutative;
	
	/* whether this op writes to a register */
	bool has_dest;
	
	/* whether the destination must be allocated at the beginning of a physical
	 * register
	 */
	bool dest_beginning;
	
	/* arg_sizes
	 *
	 * If nonzero, input i should use the first arg_sizes[i] channels (note
	 * that swizzles are still used) and ignore the mask. Then, we consider
	 * input i "horizantal" - i.e. each component of the result depends on
	 * every component of the input. In the case that every input is horizantal
	 * (see is_horizantal), the destination channels must be specified using
	 * the write mask, and there must be dest_size channels enabled.
	 *
	 * If zero, we assume a "vertical" one-to-one correspondance between source
	 * and dest.
	 */
	const unsigned            arg_sizes[3];
	
	/* see above */
	bool is_horizantal;
	
	/* see above */
	unsigned dest_size;
	
	/* whether output modifiers (saturate, positive, round) are supported */
	bool output_modifiers;
	
	/* whether intput modifiers (absolute, negate) are supported per-input */
	bool input_modifiers[3];
} lima_pp_hir_op_t;

extern const lima_pp_hir_op_t lima_pp_hir_op[];

//Returns the number of channels used for an argument
static inline unsigned lima_pp_hir_arg_size(lima_pp_hir_cmd_t* cmd, unsigned arg)
{
	if (cmd->op == lima_pp_hir_op_combine)
	{
		unsigned i, size = 0;
		for (i = 0; i < arg; i++)
		{
			if (cmd->src[i].constant)
				continue;
			lima_pp_hir_cmd_t* dep = (lima_pp_hir_cmd_t*) cmd->src[i].depend;
			size += dep->dst.reg.size + 1;
		}
		lima_pp_hir_cmd_t* dep = (lima_pp_hir_cmd_t*) cmd->src[arg].depend;
		unsigned arg_size = dep->dst.reg.size + 1;
		if (size + arg_size > cmd->dst.reg.size)
			return cmd->dst.reg.size + 1 - size;
		else
			return arg_size;
	}
	if (cmd->op != lima_pp_hir_op_phi &&
		lima_pp_hir_op[cmd->op].arg_sizes[arg] != 0)
		return lima_pp_hir_op[cmd->op].arg_sizes[arg];
	return cmd->dst.reg.size + 1;
}

static inline bool lima_pp_hir_input_modifier(lima_pp_hir_op_e op, unsigned arg)
{
	return (op == lima_pp_hir_op_phi || op == lima_pp_hir_op_combine) ?
		false : lima_pp_hir_op[op].input_modifiers[arg];
}

bool lima_pp_hir_op_is_texld(lima_pp_hir_op_e op);
bool lima_pp_hir_op_is_load(lima_pp_hir_op_e op);
bool lima_pp_hir_op_is_store(lima_pp_hir_op_e op);
bool lima_pp_hir_op_is_load_store(lima_pp_hir_op_e op);
bool lima_pp_hir_op_is_branch(lima_pp_hir_op_e op);

lima_pp_hir_cmd_t* lima_pp_hir_cmd_create(lima_pp_hir_op_e op);
lima_pp_hir_cmd_t* lima_pp_hir_phi_create(unsigned num_args);
lima_pp_hir_cmd_t* lima_pp_hir_combine_create(unsigned num_args);
void lima_pp_hir_cmd_delete(lima_pp_hir_cmd_t* cmd);
lima_pp_hir_cmd_t* lima_pp_hir_cmd_import(
	void* data, unsigned size, unsigned* pos,
	lima_pp_hir_prog_t* prog,
	lima_pp_hir_block_t* block);
void* lima_pp_hir_cmd_export(
	lima_pp_hir_cmd_t* cmd, unsigned* size);

void lima_pp_hir_cmd_replace_uses(
	lima_pp_hir_cmd_t* old_cmd, lima_pp_hir_cmd_t* new_cmd);

lima_pp_hir_source_t lima_pp_hir_source_copy(
	lima_pp_hir_source_t src);

lima_pp_hir_prog_t* lima_pp_hir_prog_create(void);
void lima_pp_hir_prog_delete(lima_pp_hir_prog_t* prog);
void lima_pp_hir_prog_insert_start(
	lima_pp_hir_block_t* block,
	lima_pp_hir_prog_t* prog);
void lima_pp_hir_prog_insert_end(
	lima_pp_hir_block_t* block,
	lima_pp_hir_prog_t* prog);
void lima_pp_hir_prog_insert(
	lima_pp_hir_block_t* block,
	lima_pp_hir_block_t* before);
void lima_pp_hir_prog_remove(
	lima_pp_hir_block_t* block);
void lima_pp_hir_prog_replace(
	lima_pp_hir_block_t* old_block,
	lima_pp_hir_block_t* new_block);
lima_pp_hir_prog_t* lima_pp_hir_prog_import(void* data, unsigned size);
void* lima_pp_hir_prog_export(lima_pp_hir_prog_t* prog, unsigned* size);
bool lima_pp_hir_prog_add_predecessors(lima_pp_hir_prog_t* prog);
bool lima_pp_hir_prog_reorder(lima_pp_hir_prog_t* prog);
bool lima_pp_hir_prog_add_array(
	lima_pp_hir_prog_t* prog, lima_pp_hir_temp_array_t array);
bool lima_pp_hir_prog_remove_array(lima_pp_hir_prog_t* prog, unsigned index);

lima_pp_hir_block_t* lima_pp_hir_block_create(void);
void lima_pp_hir_block_delete(lima_pp_hir_block_t* block);
void lima_pp_hir_block_insert_start(
	lima_pp_hir_block_t* block,
	lima_pp_hir_cmd_t* cmd);
void lima_pp_hir_block_insert_end(
	lima_pp_hir_block_t* block,
	lima_pp_hir_cmd_t* cmd);
void lima_pp_hir_block_insert(
	lima_pp_hir_cmd_t* cmd,
	lima_pp_hir_cmd_t* before);
void lima_pp_hir_block_remove(
	lima_pp_hir_block_t* block,
	lima_pp_hir_cmd_t* cmd);
void lima_pp_hir_block_replace(
	lima_pp_hir_cmd_t* old_cmd,
	lima_pp_hir_cmd_t* new_cmd);
lima_pp_hir_block_t* lima_pp_hir_block_import(
	void* data, unsigned size, unsigned* pos,
	lima_pp_hir_prog_t* prog);
void* lima_pp_hir_block_export(
	lima_pp_hir_block_t* block,
	lima_pp_hir_prog_t* prog, unsigned* size);

bool lima_pp_hir_prog_print(lima_pp_hir_prog_t* prog);
bool lima_pp_hir_block_print(
	lima_pp_hir_block_t* block,
	lima_pp_hir_prog_t* prog);
void lima_pp_hir_cmd_print(
	lima_pp_hir_cmd_t* cmd,
	lima_pp_hir_block_t* block,
	lima_pp_hir_prog_t* prog);

bool lima_pp_hir_calc_dominance(lima_pp_hir_prog_t* prog);

bool lima_pp_hir_temp_to_reg(lima_pp_hir_prog_t* prog);

bool lima_pp_hir_compress_temp_arrays(lima_pp_hir_prog_t* prog);

void lima_pp_hir_propagate_copies(lima_pp_hir_prog_t* prog);

bool lima_pp_hir_reg_narrow(lima_pp_hir_prog_t* prog);

bool lima_pp_hir_dead_code_eliminate(lima_pp_hir_prog_t* prog);

typedef bool (*lima_pp_hir_dom_tree_traverse_cb)(
	lima_pp_hir_block_t* block, void* state);

/* Walk the dominator tree in a depth-first manner */

bool lima_pp_hir_dom_tree_dfs(
	lima_pp_hir_prog_t* prog,
	lima_pp_hir_dom_tree_traverse_cb preorder,
	lima_pp_hir_dom_tree_traverse_cb postorder,
	void* state);

#define pp_hir_block_for_each_cmd(block, cmd) \
	list_for_each_entry(cmd, &block->cmd_list, cmd_list)
#define pp_hir_block_for_each_cmd_reverse(block, cmd) \
	list_for_each_entry_reverse(cmd, &block->cmd_list, cmd_list)
#define pp_hir_block_for_each_cmd_safe(block, tmp_cmd, cmd) \
	list_for_each_entry_safe(cmd, tmp_cmd, &block->cmd_list, cmd_list)
#define pp_hir_next_cmd(cmd) \
	container_of(cmd->cmd_list.next, lima_pp_hir_cmd_t, cmd_list)
#define pp_hir_first_cmd(block) \
	container_of(block->cmd_list.next, lima_pp_hir_cmd_t, cmd_list)
#define pp_hir_prog_for_each_block(prog, block) \
	list_for_each_entry(block, &prog->block_list, block_list)
#define pp_hir_prog_for_each_block_safe(prog, tmp_block, block) \
	list_for_each_entry_safe(block, tmp_block, &prog->block_list, block_list)
#define pp_hir_next_block(block) \
	container_of(block->block_list.next, lima_pp_hir_block_t, block_list)
#define pp_hir_first_block(prog) \
	container_of(prog->block_list.next, lima_pp_hir_block_t, block_list)
#define pp_hir_last_block(prog) \
	container_of(prog->block_list.prev, lima_pp_hir_block_t, block_list)

typedef struct {
	lima_pp_hir_block_t* block;
} lima_pp_hir_cfg_visitor_state;

typedef bool (*lima_pp_hir_cfg_visitor_func) (lima_pp_hir_cfg_visitor_state* state);

/* preorder:
 *    true - visits basic blocks in preorder
 *    false - visits basic blocks in postorder
 */

bool lima_pp_hir_cfg_traverse(
	lima_pp_hir_prog_t* prog,
	lima_pp_hir_cfg_visitor_state* state,
	lima_pp_hir_cfg_visitor_func visit, bool preorder);


//extern uint32_t* lima_pp_hir_gen_frag(lima_pp_hir_prog_t* prog, unsigned* size);

#ifdef __cplusplus
}
#endif

#endif
