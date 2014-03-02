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

#ifndef __gp_ir_h__
#define __gp_ir_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bitset.h"
#include "ptrset.h"
#include "list.h"
#include <stdint.h>

/* forward declaration of datastructures defined in scheduler.h */

struct lima_gp_ir_dep_info_s;



void lima_gp_ir_print_tabs(unsigned tabs);

/*
 * In this IR, the essential units of computation are "nodes." They map
 * directly to operations/units in the scheduled code (unless they are lowered
 * during compilation). As such, they are a little unconventional. Even in
 * IR's which have arbitrary expressions (as opposed to 3-address instructions),
 * nodes can only have one parent (i.e., they must be expression *trees*).
 * However, this isn't how our HW works - the result of each expression can be
 * and is used by multiple parents. Therefore, we have a node DAG.
 * Furthermore, each node may end up getting used by more than one instruction.
 * But to facilitate live variable analysis and to keep the meaning of the
 * program clear, for each node, we keep track of the first root node that
 * uses it (which is equal to the first successor of its parents) and declare
 * that its "successor." For the purposes of analysis, each node is considered
 * to be excecuted in between its parent and any root nodes before its
 * successor.
 */

/* list of operations that a node can do. */

typedef enum
{
	lima_gp_ir_op_mov,
	
	/* mul ops */
	lima_gp_ir_op_mul,
	lima_gp_ir_op_select,
	lima_gp_ir_op_complex1,
	lima_gp_ir_op_complex2,
	
	/* add ops */
	lima_gp_ir_op_add,
	lima_gp_ir_op_floor,
	lima_gp_ir_op_sign,
	lima_gp_ir_op_ge,
	lima_gp_ir_op_lt,
	lima_gp_ir_op_min,
	lima_gp_ir_op_max,
	
	/* mul/add ops */
	lima_gp_ir_op_neg,
	
	/* passthrough ops */
	lima_gp_ir_op_clamp_const,
	lima_gp_ir_op_preexp2,
	lima_gp_ir_op_postlog2,
	
	
	/* complex ops */
	lima_gp_ir_op_exp2_impl,
	lima_gp_ir_op_log2_impl,
	lima_gp_ir_op_rcp_impl,
	lima_gp_ir_op_rsqrt_impl,
	
	/* load/store ops */
	lima_gp_ir_op_load_uniform,
	lima_gp_ir_op_load_temp,
	lima_gp_ir_op_load_attribute,
	lima_gp_ir_op_load_reg,
	lima_gp_ir_op_store_temp,
	lima_gp_ir_op_store_reg,
	lima_gp_ir_op_store_varying,
	lima_gp_ir_op_store_temp_load_off0,
	lima_gp_ir_op_store_temp_load_off1,
	lima_gp_ir_op_store_temp_load_off2,
	
	/* branch */
	lima_gp_ir_op_branch_cond,
	
	/* const (emulated) */
	lima_gp_ir_op_const,
	
	/* emulated ops */
	lima_gp_ir_op_exp2,
	lima_gp_ir_op_log2,
	lima_gp_ir_op_rcp,
	lima_gp_ir_op_rsqrt,
	lima_gp_ir_op_ceil,
	lima_gp_ir_op_fract,
	lima_gp_ir_op_exp,
	lima_gp_ir_op_log,
	lima_gp_ir_op_pow,
	lima_gp_ir_op_sqrt,
	lima_gp_ir_op_sin,
	lima_gp_ir_op_cos,
	lima_gp_ir_op_tan,
	lima_gp_ir_op_branch_uncond,
	
	/* phi nodes (ssa-only) */
	lima_gp_ir_op_phi
} lima_gp_ir_op_e;

struct lima_gp_ir_prog_s;

typedef struct
{
	struct list reg_list;
	
	unsigned index;
	unsigned size;
	
	bool phys_reg_assigned;
	unsigned phys_reg;
	unsigned phys_reg_offset;
	
	ptrset_t uses;
	ptrset_t defs;
	
	struct lima_gp_ir_prog_s* prog;
} lima_gp_ir_reg_t;


struct lima_gp_ir_node_s;
struct lima_gp_ir_block_s;
struct lima_gp_ir_root_node_s;

typedef struct
{
	struct lima_gp_ir_node_s* parent;
	struct lima_gp_ir_node_s** child;
	unsigned child_index;
	bool at_end;
} lima_gp_ir_child_node_iter_t;

typedef lima_gp_ir_child_node_iter_t (*lima_gp_ir_node_child_iter_create_cb)(
	struct lima_gp_ir_node_s* parent);

typedef void (*lima_gp_ir_node_child_iter_next_cb)(
	lima_gp_ir_child_node_iter_t* iter);

typedef void (*lima_gp_ir_node_print_cb)(
	struct lima_gp_ir_node_s* node, unsigned tabs);

typedef bool (*lima_gp_ir_node_import_cb)(
	struct lima_gp_ir_node_s* node, struct lima_gp_ir_node_s** nodes,
	struct lima_gp_ir_block_s* block, void* data);

typedef void* (*lima_gp_ir_node_export_cb)(
	struct lima_gp_ir_node_s* node, struct lima_gp_ir_block_s* block,
	unsigned* size);

typedef void (*lima_gp_ir_node_delete_cb)(
	struct lima_gp_ir_node_s* node);

typedef struct lima_gp_ir_node_s
{
	lima_gp_ir_op_e op;
	
	/* used for reading/writing and printing */
	unsigned index;
		
	lima_gp_ir_node_delete_cb delete_;
	
	lima_gp_ir_node_print_cb print;
	
	lima_gp_ir_node_export_cb export_node;
	lima_gp_ir_node_import_cb import;
	
	lima_gp_ir_node_child_iter_create_cb child_iter_create;
	lima_gp_ir_node_child_iter_next_cb child_iter_next;
	
	/* scheduling information */
	ptrset_t preds, succs;
	unsigned max_dist;
	/* 
	 * NOTE: sched_instr is inverted from its normal meaning:
	 * 0 is the end, 1 is one before the end, etc. in order to accomodate
	 * the reverse scheduling.
	 */
	unsigned sched_pos, sched_instr;
	
	ptrset_t parents;
	struct lima_gp_ir_root_node_s* successor;
} lima_gp_ir_node_t;

#define gp_ir_node_for_each_child(node, iter) \
	for(iter = node->child_iter_create(node); !iter.at_end; \
		node->child_iter_next(&iter))

lima_gp_ir_node_t* lima_gp_ir_node_create(lima_gp_ir_op_e op);

/*
 * Links a parent node to a child node. Assumes that the parent node is already
 * part of a program (i.e., it has been the child of a lima_gp_ir_node_link()
 * call or is a root node that has been inserted into a basic block)
 */
void lima_gp_ir_node_link(lima_gp_ir_node_t* parent,
						  lima_gp_ir_node_t* child);

/*
 * Unlinks a child node from a parent node,
 * recalculating the successor of the child node if necessary
 * and deleting the child node if it has no parents.
 */
void lima_gp_ir_node_unlink(lima_gp_ir_node_t* parent,
							lima_gp_ir_node_t* child);

void lima_gp_ir_node_replace_child(lima_gp_ir_node_t* parent,
								   lima_gp_ir_node_t* old_child,
								   lima_gp_ir_node_t* new_child);

bool lima_gp_ir_node_replace(lima_gp_ir_node_t* old_node,
							 lima_gp_ir_node_t* new_node);

void lima_gp_ir_node_print(lima_gp_ir_node_t* node, unsigned tabs);

/* delete a node, unlinking it from its children */
void lima_gp_ir_node_delete(lima_gp_ir_node_t* node);


/*
 * Node traversal
 * stops when it encounters a child node with a different successor
 */

typedef bool (*lima_gp_ir_node_traverse_cb)(
	lima_gp_ir_node_t* node, void* state);

bool lima_gp_ir_node_dfs(lima_gp_ir_node_t* node,
						 lima_gp_ir_node_traverse_cb preorder,
						 lima_gp_ir_node_traverse_cb postorder,
						 void* state);

/*
 * Reading & Writing
 */

typedef struct
{
	uint32_t size;
	uint32_t op;
} lima_gp_ir_node_header_t;


/* Node Types */

/* 
 * Rather than putting all the info in the node structure, we create a number
 * of node "types" (i.e. subclasses) and let each type handle the details.
 */

typedef enum
{
	/* simple arithmetic ops */
	lima_gp_ir_node_type_alu,
	
	/*
	 * clamp_const gets its own type because it's an arithmetic op, but also has
	 * to load a const/uniform
	 */
	lima_gp_ir_node_type_clamp_const,
	
	/* holds an inline constant (used internally) */
	lima_gp_ir_node_type_const,
	
	/* root nodes (includes store and branch) */
	lima_gp_ir_node_type_root,
	
	/* load ops */
	lima_gp_ir_node_type_load,
	
	/* load reg */
	lima_gp_ir_node_type_load_reg,
	
	/* store ops */
	lima_gp_ir_node_type_store,
	
	/* store reg */
	lima_gp_ir_node_type_store_reg,
	
	/* conditional/unconditional branch */
	lima_gp_ir_node_type_branch,
	
	/* phi nodes (ssa-only) */
	lima_gp_ir_node_type_phi,
	
	lima_gp_ir_node_type_unknown
} lima_gp_ir_node_type_e;

typedef struct
{
	char* name;
	
	/* How many possible values there are for sched_pos */
	unsigned num_sched_positions;
	
	/* for ALU ops, tells whether inputs/outputs can be negated */
	bool can_negate_dest;
	bool can_negate_sources[3];
	
	bool is_root_node;
	lima_gp_ir_node_type_e type;
} lima_gp_ir_op_t;

extern const lima_gp_ir_op_t lima_gp_ir_op[];


/* ALU Node */

typedef struct
{
	lima_gp_ir_node_t node;
	
	bool dest_negate;
	lima_gp_ir_node_t* children[3];
	bool children_negate[3];
} lima_gp_ir_alu_node_t;

unsigned lima_gp_ir_alu_node_num_children(lima_gp_ir_op_e op);
lima_gp_ir_alu_node_t* lima_gp_ir_alu_node_create(lima_gp_ir_op_e op);

#define gp_ir_node_to_alu(_node) \
 container_of(_node, lima_gp_ir_alu_node_t, node)


/* Clamp-const Node */

typedef struct
{
	lima_gp_ir_node_t node;
	
	bool is_inline_const;
	unsigned uniform_index;
	float low, high; /* inline constants
					  * (lowered to x and y components of a uniform later)
					  */
	
	lima_gp_ir_node_t* child;
} lima_gp_ir_clamp_const_node_t;

lima_gp_ir_clamp_const_node_t* lima_gp_ir_clamp_const_node_create(void);

#define gp_ir_node_to_clamp_const(_node) \
	container_of(_node, lima_gp_ir_clamp_const_node_t, node)


/* Const Node */

typedef struct
{
	lima_gp_ir_node_t node;
	
	float constant;
} lima_gp_ir_const_node_t;

lima_gp_ir_const_node_t* lima_gp_ir_const_node_create(void);

#define gp_ir_node_to_const(_node) \
	container_of(_node, lima_gp_ir_const_node_t, node)

/* Root Node */

typedef struct lima_gp_ir_root_node_s
{
	lima_gp_ir_node_t node;
	
	struct list node_list; /* list of root nodes in the basic block */
	struct lima_gp_ir_block_s* block;
	
	/* liveness information 
	 * stores liveness info for 4 components per register,
	 * extra components are ignored */
	bitset_t live_phys_after;
	bitset_t live_virt_after;
	
	/* for dead code elimination */
	bool is_dead;
} lima_gp_ir_root_node_t;

#define gp_ir_root_node_next(root_node) \
	container_of(root_node->node_list.next, lima_gp_ir_root_node_t, node_list)


/* Load Node */

typedef struct
{
	lima_gp_ir_node_t node;
	
	unsigned index;
	
	unsigned component;
	
	/* for uniforms/temporaries only */
	bool offset;
	unsigned off_reg;
} lima_gp_ir_load_node_t;

lima_gp_ir_load_node_t* lima_gp_ir_load_node_create(lima_gp_ir_op_e op);

#define gp_ir_node_to_load(_node) \
	container_of(_node, lima_gp_ir_load_node_t, node)

/* Load Reg Node */

typedef struct
{
	lima_gp_ir_node_t node;
	
	lima_gp_ir_reg_t* reg;
	
	unsigned component;
	
	/* NULL for no offset */
	lima_gp_ir_node_t* offset;
} lima_gp_ir_load_reg_node_t;

lima_gp_ir_load_reg_node_t* lima_gp_ir_load_reg_node_create(void);

#define gp_ir_node_to_load_reg(_node) \
	container_of(_node, lima_gp_ir_load_reg_node_t, node)

/* Store Node */

typedef struct
{
	lima_gp_ir_root_node_t root_node;
	
	unsigned index; /* must be 0 when storing temporaries */
	
	bool mask[4]; /* which components to store to */
	lima_gp_ir_node_t* children[4]; /* undefined when mask is false */
	
	/* temporaries only */
	lima_gp_ir_node_t* addr;
} lima_gp_ir_store_node_t;

lima_gp_ir_store_node_t* lima_gp_ir_store_node_create(lima_gp_ir_op_e op);

#define gp_ir_node_to_store(_node) \
	container_of(_node, lima_gp_ir_store_node_t, root_node.node)

/* Store Reg Node */

typedef struct
{
	lima_gp_ir_root_node_t root_node;
	
	lima_gp_ir_reg_t* reg;
	
	bool mask[4];
	lima_gp_ir_node_t* children[4];
} lima_gp_ir_store_reg_node_t;

lima_gp_ir_store_reg_node_t* lima_gp_ir_store_reg_node_create(void);

#define gp_ir_node_to_store_reg(_node) \
	container_of(_node, lima_gp_ir_store_reg_node_t, root_node.node)

/* Branch Node */

typedef struct
{
	lima_gp_ir_root_node_t root_node;
	
	struct lima_gp_ir_block_s* dest;
	lima_gp_ir_node_t* condition;
} lima_gp_ir_branch_node_t;

lima_gp_ir_branch_node_t* lima_gp_ir_branch_node_create(lima_gp_ir_op_e op);

#define gp_ir_node_to_branch(_node) \
	container_of(_node, lima_gp_ir_branch_node_t, root_node.node)

/* Phi node */

typedef struct
{
	lima_gp_ir_reg_t* reg;
	struct lima_gp_ir_block_s* pred;
} lima_gp_ir_phi_node_src_t;

typedef struct
{
	lima_gp_ir_node_t node;
	
	struct lima_gp_ir_block_s* block;
	lima_gp_ir_phi_node_src_t* sources;
	unsigned num_sources;
	
	lima_gp_ir_reg_t* dest;
	
	/* for dead code elimination */
	bool is_dead;
} lima_gp_ir_phi_node_t;

lima_gp_ir_phi_node_t* lima_gp_ir_phi_node_create(unsigned num_sources);

#define gp_ir_node_to_phi(_node) \
	container_of(_node, lima_gp_ir_phi_node_t, node)

/* Basic Blocks */

struct lima_gp_ir_prog_s;

typedef struct lima_gp_ir_block_s
{
	struct list node_list;
	struct list instr_list;
	struct list block_list;
	ptrset_t phi_nodes;
	unsigned num_nodes, num_instrs;
	struct lima_gp_ir_prog_s* prog;
	
	unsigned num_preds;
	struct lima_gp_ir_block_s** preds;
	
	/* for reading/writing and printing */
	unsigned index;
	
	/* dominance info */
	struct lima_gp_ir_block_s* imm_dominator;
	ptrset_t dominance_frontier;
	ptrset_t dom_tree_children;
	
	/* scheduling info */
	
	/* nodes in this block that have no predecessor */
	ptrset_t start_nodes;
	
	/*  nodes in this block that have no successor */
	ptrset_t end_nodes;
	
	/* liveness information
	 * stores liveness info for 4 components per register,
	 * extra components are ignored */
	
	bitset_t live_phys_before;
	bitset_t live_virt_before;
} lima_gp_ir_block_t;

#define gp_ir_block_for_each_node(block, node) \
	list_for_each_entry(node, &block->node_list, node_list)
#define gp_ir_block_for_each_node_safe(block, node, temp) \
	list_for_each_entry_safe(node, temp, &block->node_list, node_list)
#define gp_ir_block_for_each_node_reverse(block, node) \
	list_for_each_entry_reverse(node, &block->node_list, node_list)
#define gp_ir_block_is_empty(block) \
	(block->node_list.next == &block->node_list)
#define gp_ir_block_first_node(block) \
	container_of(block->node_list.next, lima_gp_ir_root_node_t, node_list)
#define gp_ir_block_last_node(block) \
	container_of(block->node_list.prev, lima_gp_ir_root_node_t, node_list)
#define gp_ir_node_next(node) \
	container_of(node->node_list.next, lima_gp_ir_root_node_t, node_list)
#define gp_ir_node_prev(node) \
	container_of(node->node_list.prev, lima_gp_ir_root_node_t, node_list)
#define gp_ir_node_is_start(node) \
	((node)->node_list.prev == &(node)->block->node_list)
#define gp_ir_node_is_end(node) \
	((node)->node_list.next == &(node)->block->node_list)

lima_gp_ir_block_t* lima_gp_ir_block_create(void);
void lima_gp_ir_block_delete(lima_gp_ir_block_t* block);
void lima_gp_ir_block_insert_start(
	lima_gp_ir_block_t* block,
	lima_gp_ir_root_node_t* node);
void lima_gp_ir_block_insert_end(
	lima_gp_ir_block_t* block,
	lima_gp_ir_root_node_t* node);
void lima_gp_ir_block_insert_after(
	lima_gp_ir_root_node_t* node,
	lima_gp_ir_root_node_t* before);
void lima_gp_ir_block_insert_before(
	lima_gp_ir_root_node_t* node,
	lima_gp_ir_root_node_t* after);
void lima_gp_ir_block_remove(
	lima_gp_ir_root_node_t* node);
void lima_gp_ir_block_replace(
	lima_gp_ir_root_node_t* old_node,
	lima_gp_ir_root_node_t* new_node);
void lima_gp_ir_block_insert_phi(
	lima_gp_ir_block_t* block, lima_gp_ir_phi_node_t* node);
void lima_gp_ir_block_remove_phi(
	lima_gp_ir_block_t* block, lima_gp_ir_phi_node_t* node);
bool lima_gp_ir_block_print(
	lima_gp_ir_block_t* block, unsigned tabs, bool print_liveness);
void *lima_gp_ir_block_export(lima_gp_ir_block_t* block, unsigned* size);
bool lima_gp_ir_block_import(lima_gp_ir_block_t* block, void* data,
							 unsigned* size);

/* Programs */

typedef struct lima_gp_ir_prog_s
{
	struct list block_list;
	struct list reg_list;
	unsigned num_blocks;
	unsigned reg_alloc, temp_alloc;
} lima_gp_ir_prog_t;

#define gp_ir_prog_for_each_block(prog, block) \
	list_for_each_entry(block, &prog->block_list, block_list)
#define gp_ir_prog_for_each_block_safe(prog, block, temp) \
	list_for_each_entry_safe(block, temp, &prog->block_list, block_list)
#define gp_ir_prog_first_block(prog) \
	container_of(prog->block_list.next, lima_gp_ir_block_t, block_list)
#define gp_ir_prog_last_block(prog) \
	container_of(prog->block_list.prev, lima_gp_ir_block_t, block_list)
#define gp_ir_block_next(block) \
	container_of(block->block_list.next, lima_gp_ir_block_t, block_list)
#define gp_ir_block_is_first(block) \
	((block)->block_list.prev == &(block)->prog->block_list)
#define gp_ir_block_is_last(block) \
	((block)->block_list.next == &(block)->prog->block_list)
#define gp_ir_prog_for_each_reg(prog, reg) \
	list_for_each_entry(reg, &prog->reg_list, reg_list)
#define gp_ir_prog_for_each_reg_safe(prog, reg, temp) \
	list_for_each_entry_safe(reg, temp, &prog->reg_list, reg_list)

lima_gp_ir_prog_t* lima_gp_ir_prog_create(void);
void lima_gp_ir_prog_delete(lima_gp_ir_prog_t* prog);
void lima_gp_ir_prog_insert_start(
	lima_gp_ir_prog_t* prog,
	lima_gp_ir_block_t* block);
void lima_gp_ir_prog_insert_end(
	lima_gp_ir_prog_t* prog,
	lima_gp_ir_block_t* block);
void lima_gp_ir_prog_insert(
	lima_gp_ir_block_t* block,
	lima_gp_ir_block_t* before);
void lima_gp_ir_prog_remove(
	lima_gp_ir_block_t* block);
bool lima_gp_ir_prog_calc_preds(lima_gp_ir_prog_t* prog);
bool lima_gp_ir_prog_print(
	lima_gp_ir_prog_t* prog, unsigned tabs, bool print_liveness);
lima_gp_ir_prog_t* lima_gp_ir_prog_import(void* data, unsigned* size);
void* lima_gp_ir_prog_export(lima_gp_ir_prog_t* prog, unsigned* size);

lima_gp_ir_reg_t* lima_gp_ir_reg_create(lima_gp_ir_prog_t* prog);
void lima_gp_ir_reg_delete(lima_gp_ir_reg_t* reg);
lima_gp_ir_reg_t* lima_gp_ir_reg_find(lima_gp_ir_prog_t* prog, unsigned index);

/* Lowering - turns unsupported operations into operations the hardware can
 * support directly
 */

bool lima_gp_ir_lower_root_node(lima_gp_ir_root_node_t* node);
bool lima_gp_ir_lower_block(lima_gp_ir_block_t* block);
bool lima_gp_ir_lower_prog(lima_gp_ir_prog_t* prog);

/* Dominance & dominance frontier calculation */

bool lima_gp_ir_calc_dominance(lima_gp_ir_prog_t* prog);

typedef bool (*lima_gp_ir_dom_tree_traverse_cb)(
	lima_gp_ir_block_t* block, void* state);

/* Walk the dominator tree in a depth-first manner */

bool lima_gp_ir_dom_tree_dfs(
	lima_gp_ir_prog_t* prog,
	lima_gp_ir_dom_tree_traverse_cb preorder,
	lima_gp_ir_dom_tree_traverse_cb postorder,
	void* state);

/* Conversion to SSA form */

bool lima_gp_ir_convert_to_ssa(lima_gp_ir_prog_t* prog);

/* Phi-node elimination (conversion from SSA) */

bool lima_gp_ir_eliminate_phi_nodes(lima_gp_ir_prog_t* prog);

/* Register elimination optimization */

bool lima_gp_ir_reg_eliminate(lima_gp_ir_prog_t* prog);

/* Dead code elimination */

bool lima_gp_ir_dead_code_eliminate(lima_gp_ir_prog_t* prog);

/* If-conversion */

bool lima_gp_ir_if_convert(lima_gp_ir_prog_t* prog);

/* Liveness calculation */

bool lima_gp_ir_liveness_compute_node(
	lima_gp_ir_root_node_t* node, bitset_t live_before, bool virt);
bool lima_gp_ir_liveness_compute_block(
	lima_gp_ir_block_t* block, bool virt, bool* changed);
bool lima_gp_ir_liveness_compute_prog(lima_gp_ir_prog_t* prog, bool virt);

/* Register allocation */

bool lima_gp_ir_regalloc(lima_gp_ir_prog_t* prog);


/* Codegen */

void* lima_gp_ir_codegen(lima_gp_ir_prog_t* ir_prog, unsigned* size,
	unsigned* attrib_prefetch);

/* Constant folding */

bool lima_gp_ir_const_fold_root_node(lima_gp_ir_root_node_t* node);
bool lima_gp_ir_const_fold_block(lima_gp_ir_block_t* block);
bool lima_gp_ir_const_fold_prog(lima_gp_ir_prog_t* prog);

#ifdef __cplusplus
}
#endif

#endif
