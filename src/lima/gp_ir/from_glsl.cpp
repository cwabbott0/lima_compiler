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

#include "ir.h"
#include "ir_dead_branches.h"
#include "main/hash_table.h"
#include "gp_ir.h"
#include "shader/shader_internal.h"

namespace {

class gp_ir_visitor : public ir_hierarchical_visitor
{
public:
	gp_ir_visitor(lima_gp_ir_prog_t* prog, lima_shader_symbols_t* symbols,
				  struct hash_table* glsl_symbols,
				  ir_dead_branches_visitor *dbv);
	
	~gp_ir_visitor();
	
	virtual ir_visitor_status visit_enter(ir_if*);
	virtual ir_visitor_status visit_enter(ir_loop*);
	virtual ir_visitor_status visit(ir_loop_jump*);
	virtual ir_visitor_status visit_enter(ir_return*);
	virtual ir_visitor_status visit_enter(ir_function*);
	virtual ir_visitor_status visit_enter(ir_assignment*);
	virtual ir_visitor_status visit_enter(ir_expression*);
	virtual ir_visitor_status visit(ir_constant*);
	virtual ir_visitor_status visit_enter(ir_swizzle*);
	virtual ir_visitor_status visit(ir_phi_if*);
	virtual ir_visitor_status visit(ir_phi_loop_begin*);
	virtual ir_visitor_status visit(ir_phi_loop_end*);
	virtual ir_visitor_status visit(ir_dereference_variable*);
	virtual ir_visitor_status visit_enter(ir_dereference_array*);
	virtual ir_visitor_status visit_enter(ir_dereference_record*);
	
	void rewrite_phi_if(ir_phi_if*, ir_if*);
	void rewrite_phi_loop_begin(ir_phi_loop_begin*, ir_loop*);
	void rewrite_phi_loop_end(ir_phi_loop_end*);
	
private:
	void emit_inverse_cond(ir_rvalue*);
	void emit_expression(lima_gp_ir_op_e op, ir_rvalue** args,
						 unsigned num_sources);
	void insert_phi(ir_phi* ir, unsigned num_sources);
	void rewrite_phi_source(lima_gp_ir_phi_node_src_t* src,
							lima_gp_ir_block_t* block,
							ir_variable* var);
	void rewrite_phi_jump_srcs(lima_gp_ir_phi_node_t* phi,
							   exec_list* srcs, unsigned start);
	
	void handle_deref(ir_dereference*);
	void emit_reg_store(ir_dereference*);
	void emit_reg_load(ir_dereference*);
	void emit_output();
	void emit_temp_store(ir_dereference*, unsigned wrmask);
	void emit_uniform_load(ir_dereference*, bool is_temp);
	void emit_attr_load(ir_dereference*);
	void emit_varying_store(ir_dereference*, unsigned wrmask);
	
	unsigned calc_const_deref_offset(ir_dereference*, lima_symbol_t**);
	unsigned calc_deref_offset(ir_dereference*, lima_symbol_t**,
							   lima_gp_ir_node_t** out_indirect);
	
	lima_gp_ir_prog_t* prog;
	lima_gp_ir_block_t* cur_block;
	lima_gp_ir_block_t* break_block, *continue_block;
	lima_gp_ir_node_t* cur_nodes[4];
	unsigned cur_offset_reg;
	
	lima_shader_symbols_t* symbols;
	struct hash_table* glsl_symbols;
	
	ir_dead_branches_visitor *dbv;
	
	struct hash_table *var_to_reg; /* ir_variable => lima_gp_ir_reg_t */
	
	//info used for figuring out phi sources
	struct hash_table* phi_to_phi;
	struct hash_table* then_branch_to_block;
	struct hash_table* else_branch_to_block;
	struct hash_table* loop_jump_to_block;
	struct hash_table* loop_beginning_to_block;
	struct hash_table* loop_end_to_block;
};

class phi_rewrite_visitor : public ir_hierarchical_visitor
{
public:
	phi_rewrite_visitor(gp_ir_visitor* v) : v(v)
	{
	}
	
	virtual ir_visitor_status visit_leave(ir_if*);
	virtual ir_visitor_status visit_leave(ir_loop*);
	
private:
	gp_ir_visitor* v;
};

};

void lima_lower_to_gp_ir(lima_shader_t* shader)
{
	ir_dead_branches_visitor dbv;
	dbv.run(shader->linked_shader->ir);
	
	shader->ir.gp.gp_prog = lima_gp_ir_prog_create();
	gp_ir_visitor v(shader->ir.gp.gp_prog, &shader->symbols,
					shader->glsl_symbols, &dbv);
	v.run(shader->linked_shader->ir);
	
	phi_rewrite_visitor prv(&v);
	prv.run(shader->linked_shader->ir);
	
	unsigned temp_size = shader->symbols.temporary_table.total_size;
	shader->ir.gp.gp_prog->temp_alloc = (temp_size + 3) / 4;
}

gp_ir_visitor::gp_ir_visitor(lima_gp_ir_prog_t* prog,
							 lima_shader_symbols_t* symbols,
							 struct hash_table* glsl_symbols,
							 ir_dead_branches_visitor* dbv)
{
	this->prog = prog;
	this->glsl_symbols = glsl_symbols;
	this->symbols = symbols;
	this->dbv = dbv;
	
	this->cur_block = lima_gp_ir_block_create();
	lima_gp_ir_prog_insert_start(prog, this->cur_block);
	this->cur_nodes[0] = this->cur_nodes[1] =
	this->cur_nodes[2] = this->cur_nodes[3] = NULL;
	this->cur_offset_reg = 0;
	
	this->var_to_reg = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
	this->phi_to_phi = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
	this->then_branch_to_block =
		_mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
	this->else_branch_to_block =
		_mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
	this->loop_jump_to_block =
		_mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
	this->loop_beginning_to_block =
		_mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
	this->loop_end_to_block =
		_mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
}

gp_ir_visitor::~gp_ir_visitor()
{
	_mesa_hash_table_destroy(this->var_to_reg, NULL);
	_mesa_hash_table_destroy(this->phi_to_phi, NULL);
	_mesa_hash_table_destroy(this->then_branch_to_block, NULL);
	_mesa_hash_table_destroy(this->else_branch_to_block, NULL);
	_mesa_hash_table_destroy(this->loop_jump_to_block, NULL);
	_mesa_hash_table_destroy(this->loop_beginning_to_block, NULL);
	_mesa_hash_table_destroy(this->loop_end_to_block, NULL);
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_if* ir)
{
	this->emit_inverse_cond(ir->condition);
	
	ir_dead_branches* db = this->dbv->get_dead_branches(ir);
	
	lima_gp_ir_branch_node_t* branch =
		lima_gp_ir_branch_node_create(lima_gp_ir_op_branch_cond);
	branch->condition = this->cur_nodes[0];
	lima_gp_ir_block_t** beginning_dest = &branch->dest;
	lima_gp_ir_block_insert_end(this->cur_block, &branch->root_node);
	
	lima_gp_ir_block_t* if_block = lima_gp_ir_block_create();
	lima_gp_ir_prog_insert(if_block, this->cur_block);
	this->cur_block = if_block;
	
	visit_list_elements(this, &ir->then_instructions);
	
	lima_gp_ir_block_t** then_dest = NULL;
	if (!db->then_dead && !ir->else_instructions.is_empty())
	{
		branch = lima_gp_ir_branch_node_create(lima_gp_ir_op_branch_uncond);
		then_dest = &branch->dest;
		lima_gp_ir_block_insert_end(this->cur_block, &branch->root_node);
	}
	
	if (!ir->else_instructions.is_empty())
	{
		lima_gp_ir_block_t* else_block = lima_gp_ir_block_create();
		lima_gp_ir_prog_insert(this->cur_block, else_block);
		this->cur_block = else_block;
		*beginning_dest = else_block;
		
		visit_list_elements(this, &ir->else_instructions);
	}
	
	lima_gp_ir_block_t* end_block = lima_gp_ir_block_create();
	lima_gp_ir_prog_insert(this->cur_block, end_block);
	this->cur_block = end_block;
	
	if (ir->else_instructions.is_empty())
		*beginning_dest = end_block;
	
	visit_list_elements(this, &ir->phi_nodes, false);
	
	return visit_continue_with_parent;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_loop* ir)
{
	_mesa_hash_table_insert(this->loop_beginning_to_block,
							_mesa_hash_pointer(ir), ir, this->cur_block);
	
	lima_gp_ir_block_t* loop_header = lima_gp_ir_block_create();
	lima_gp_ir_prog_insert(loop_header, this->cur_block);
	this->cur_block = loop_header;
	
	//we create after_loop and append it after loop_header, but we *do not* set
	//this->cur_block - any additional blocks in the loop will go in between
	//loop_header and after_loop
	lima_gp_ir_block_t* after_loop = lima_gp_ir_block_create();
	lima_gp_ir_prog_insert(after_loop, this->cur_block);
	
	lima_gp_ir_block_t* old_break_block = this->break_block;
	lima_gp_ir_block_t* old_continue_block = this->continue_block;
	
	this->break_block = after_loop;
	this->continue_block = loop_header;
	
	visit_list_elements(this, &ir->begin_phi_nodes, false);
	visit_list_elements(this, &ir->body_instructions);
	
	_mesa_hash_table_insert(this->loop_end_to_block,
							_mesa_hash_pointer(ir), ir, this->cur_block);
	
	lima_gp_ir_branch_node_t* branch =
		lima_gp_ir_branch_node_create(lima_gp_ir_op_branch_uncond);
	branch->dest = loop_header;
	lima_gp_ir_block_insert_end(this->cur_block, &branch->root_node);
	
	this->break_block = old_break_block;
	this->continue_block = old_continue_block;
	
	this->cur_block = after_loop;
	
	visit_list_elements(this, &ir->end_phi_nodes, false);
	
	return visit_continue_with_parent;
}

ir_visitor_status gp_ir_visitor::visit(ir_loop_jump* ir)
{
	_mesa_hash_table_insert(this->loop_jump_to_block, _mesa_hash_pointer(ir),
							ir, this->cur_block);
	
	lima_gp_ir_branch_node_t* branch =
		lima_gp_ir_branch_node_create(lima_gp_ir_op_branch_uncond);
	if (ir->mode == ir_loop_jump::jump_break)
		branch->dest = this->break_block;
	else
		branch->dest = this->continue_block;
	
	lima_gp_ir_block_insert_end(this->cur_block, &branch->root_node);
	return visit_continue;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_return*)
{
	//early returns should have been eliminated
	assert(!"should not get here");
	
	return visit_continue;
}

ir_visitor_status gp_ir_visitor::visit(ir_phi_if* ir)
{
	this->insert_phi(ir, 2);
	return visit_continue;
}

static unsigned list_size(exec_list* list)
{
	unsigned i = 0;
	foreach_list(node, list)
	i++;
	
	return i;
}

ir_visitor_status gp_ir_visitor::visit(ir_phi_loop_begin* ir)
{
	this->insert_phi(ir, 2 + list_size(&ir->continue_srcs));
	return visit_continue;
}

ir_visitor_status gp_ir_visitor::visit(ir_phi_loop_end* ir)
{
	this->insert_phi(ir, list_size(&ir->break_srcs));
	return visit_continue;
}

void gp_ir_visitor::insert_phi(ir_phi* ir, unsigned num_sources)
{
	lima_gp_ir_phi_node_t* phi = lima_gp_ir_phi_node_create(num_sources);
	lima_gp_ir_reg_t* dest = lima_gp_ir_reg_create(this->prog);
	dest->size = ir->dest->type->vector_elements;
	phi->dest = dest;
	_mesa_hash_table_insert(this->var_to_reg, _mesa_hash_pointer(ir->dest),
							ir->dest, dest);
	_mesa_hash_table_insert(this->phi_to_phi, _mesa_hash_pointer(ir), ir, phi);
	ptrset_add(&this->cur_block->phi_nodes, phi);
}

void gp_ir_visitor::rewrite_phi_source(lima_gp_ir_phi_node_src_t* src,
									   lima_gp_ir_block_t* block,
									   ir_variable* var)
{
	if (var)
	{
		struct hash_entry* entry =
			_mesa_hash_table_search(this->var_to_reg, _mesa_hash_pointer(var),
									var);
		src->reg = (lima_gp_ir_reg_t*) entry->data;
	}
	else
		src->reg = NULL;
	src->pred = block;
}

void gp_ir_visitor::rewrite_phi_jump_srcs(lima_gp_ir_phi_node_t* phi,
										  exec_list* srcs, unsigned start)
{
	unsigned i = start;
	foreach_list(node, srcs)
	{
		ir_phi_jump_src* src = (ir_phi_jump_src*) node;
		struct hash_entry* entry =
			_mesa_hash_table_search(this->loop_jump_to_block,
									_mesa_hash_pointer(src->jump), src->jump);
		
		lima_gp_ir_block_t* pred = (lima_gp_ir_block_t*) entry->data;
		this->rewrite_phi_source(&phi->sources[i], pred, src->src);
		i++;
	}
}

void gp_ir_visitor::rewrite_phi_if(ir_phi_if* ir, ir_if* if_stmt)
{
	struct hash_entry* entry =
		_mesa_hash_table_search(this->phi_to_phi, _mesa_hash_pointer(ir), ir);
	lima_gp_ir_phi_node_t* phi = (lima_gp_ir_phi_node_t*) entry->data;
	
	entry = _mesa_hash_table_search(this->then_branch_to_block,
									_mesa_hash_pointer(if_stmt), if_stmt);
	lima_gp_ir_block_t* then_block = (lima_gp_ir_block_t*) entry->data;
	this->rewrite_phi_source(&phi->sources[0], then_block, ir->if_src);
	
	entry = _mesa_hash_table_search(this->else_branch_to_block,
									_mesa_hash_pointer(if_stmt), if_stmt);
	lima_gp_ir_block_t* else_block = (lima_gp_ir_block_t*) entry->data;
	this->rewrite_phi_source(&phi->sources[1], else_block, ir->else_src);
}

void gp_ir_visitor::rewrite_phi_loop_begin(ir_phi_loop_begin* ir,
										   ir_loop* loop)
{
	struct hash_entry* entry =
		_mesa_hash_table_search(this->phi_to_phi, _mesa_hash_pointer(ir), ir);
	lima_gp_ir_phi_node_t* phi = (lima_gp_ir_phi_node_t*) entry->data;
	
	entry = _mesa_hash_table_search(this->loop_beginning_to_block,
									_mesa_hash_pointer(loop), loop);
	lima_gp_ir_block_t* enter_block = (lima_gp_ir_block_t*) entry->data;
	this->rewrite_phi_source(&phi->sources[0], enter_block, ir->enter_src);
	
	entry = _mesa_hash_table_search(this->loop_end_to_block,
									_mesa_hash_pointer(loop), loop);
	lima_gp_ir_block_t* repeat_block = (lima_gp_ir_block_t*) entry->data;
	this->rewrite_phi_source(&phi->sources[1], repeat_block, ir->repeat_src);
	
	this->rewrite_phi_jump_srcs(phi, &ir->continue_srcs, 2);
}

void gp_ir_visitor::rewrite_phi_loop_end(ir_phi_loop_end* ir)
{
	struct hash_entry* entry =
		_mesa_hash_table_search(this->phi_to_phi, _mesa_hash_pointer(ir), ir);
	lima_gp_ir_phi_node_t* phi = (lima_gp_ir_phi_node_t*) entry->data;
	
	this->rewrite_phi_jump_srcs(phi, &ir->break_srcs, 0);
}

ir_visitor_status phi_rewrite_visitor::visit_leave(ir_if* ir)
{
	foreach_list(node, &ir->phi_nodes)
	{
		ir_phi_if* phi = (ir_phi_if*) node;
		this->v->rewrite_phi_if(phi, ir);
	}
	
	return visit_continue;
}

ir_visitor_status phi_rewrite_visitor::visit_leave(ir_loop* ir)
{
	foreach_list(node, &ir->begin_phi_nodes)
	{
		ir_phi_loop_begin* phi = (ir_phi_loop_begin*) node;
		this->v->rewrite_phi_loop_begin(phi, ir);
	}
	
	foreach_list(node, &ir->end_phi_nodes)
	{
		ir_phi_loop_end* phi = (ir_phi_loop_end*) node;
		this->v->rewrite_phi_loop_end(phi);
	}
	
	return visit_continue;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_function* ir)
{
	assert(strcmp(ir->name, "main") == 0);
	
	exec_node* node = ir->signatures.get_head();
	assert(node == ir->signatures.get_tail()); //there should only be 1 signature
	
	ir_function_signature* sig = (ir_function_signature*) node;
	visit_list_elements(this, &sig->body);
	
	return visit_continue_with_parent;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_assignment* ir)
{
	//conditions were lowered by lima_lower_conditions
	assert(ir->condition == NULL);
	
	this->in_assignee = false;
	ir->rhs->accept(this);
	
	this->in_assignee = true;
	ir->lhs->accept(this);
	
	this->in_assignee = false;
	
	return visit_continue_with_parent;
}

static lima_gp_ir_node_t* build_alu_single(lima_gp_ir_op_e op,
										   lima_gp_ir_node_t* child)
{
	lima_gp_ir_alu_node_t* node = lima_gp_ir_alu_node_create(op);
	node->children[0] = child;
	lima_gp_ir_node_link(&node->node, child);
	return &node->node;
}

static lima_gp_ir_node_t* build_alu_dual(lima_gp_ir_op_e op,
										 lima_gp_ir_node_t* child1,
										 lima_gp_ir_node_t* child2)
{
	lima_gp_ir_alu_node_t* node = lima_gp_ir_alu_node_create(op);
	node->children[0] = child1;
	node->children[1] = child2;
	lima_gp_ir_node_link(&node->node, child1);
	lima_gp_ir_node_link(&node->node, child2);
	return &node->node;
}

static lima_gp_ir_node_t* build_clamp_const(float min, float max,
											lima_gp_ir_node_t* child)
{
	lima_gp_ir_clamp_const_node_t* node = lima_gp_ir_clamp_const_node_create();
	node->low = min;
	node->high = max;
	node->child = child;
	lima_gp_ir_node_link(&node->node, child);
	return &node->node;
}

static void build_reduction(lima_gp_ir_op_e op, lima_gp_ir_node_t** args,
									unsigned num_args)
{
	switch (num_args)
	{
		case 4:
			args[2] = build_alu_dual(op, args[2], args[3]);
			args[3] = NULL;
			//fallthrough
		case 3:
			args[0] = build_alu_dual(op, args[0], args[1]);
			args[1] = args[2];
			args[2] = NULL;
			//fallthrough
		case 2:
			args[0] = build_alu_dual(op, args[0], args[1]);
			args[1] = NULL;
			break;
			
		default:
			assert(0);
	}
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_expression* ir)
{
	ir_rvalue* operands[4];
	lima_gp_ir_node_t* nodes[4];
	switch (ir->operation)
	{
		case ir_unop_logic_not:
			this->emit_expression(lima_gp_ir_op_not, ir->operands, 1);
			break;
			
		case ir_unop_neg:
			this->emit_expression(lima_gp_ir_op_neg, ir->operands, 1);
			break;
			
		case ir_unop_abs:
			this->emit_expression(lima_gp_ir_op_abs, ir->operands, 2);
			break;
			
		case ir_unop_sign:
			this->emit_expression(lima_gp_ir_op_sign, ir->operands, 1);
			break;
			
		case ir_unop_rcp:
			this->emit_expression(lima_gp_ir_op_rcp, ir->operands, 1);
			break;
			
		case ir_unop_rsq:
			this->emit_expression(lima_gp_ir_op_rsqrt, ir->operands, 1);
			break;
			
		case ir_unop_sqrt:
			this->emit_expression(lima_gp_ir_op_sqrt, ir->operands, 1);
			break;
			
		case ir_unop_exp2:
			this->emit_expression(lima_gp_ir_op_exp2, ir->operands, 1);
			break;
			
		case ir_unop_log2:
			this->emit_expression(lima_gp_ir_op_log2, ir->operands, 1);
			break;
			
		case ir_unop_f2i:
			this->emit_expression(lima_gp_ir_op_f2i, ir->operands, 1);
			break;
			
		case ir_unop_i2f:
		case ir_unop_b2i:
		case ir_unop_b2f:
			//no-op
			ir->operands[0]->accept(this);
			break;
			
		case ir_unop_f2b:
		case ir_unop_i2b:
			this->emit_expression(lima_gp_ir_op_f2b, ir->operands, 1);
			break;
			
		case ir_unop_any:
			ir->operands[0]->accept(this);
			build_reduction(lima_gp_ir_op_min, this->cur_nodes,
							ir->operands[0]->type->vector_elements);
			break;
			
		case ir_unop_ceil:
			this->emit_expression(lima_gp_ir_op_ceil, ir->operands, 1);
			break;
			
		case ir_unop_floor:
			this->emit_expression(lima_gp_ir_op_floor, ir->operands, 1);
			break;
			
		case ir_unop_fract:
			this->emit_expression(lima_gp_ir_op_fract, ir->operands, 1);
			break;
			
		case ir_unop_sin:
			this->emit_expression(lima_gp_ir_op_sin, ir->operands, 1);
			break;
			
		case ir_unop_cos:
			this->emit_expression(lima_gp_ir_op_ceil, ir->operands, 1);
			break;
			
		case ir_binop_add:
			this->emit_expression(lima_gp_ir_op_add, ir->operands, 2);
			break;
			
		case ir_binop_mul:
			this->emit_expression(lima_gp_ir_op_mul, ir->operands, 2);
			break;
			
		case ir_binop_div:
			this->emit_expression(lima_gp_ir_op_div, ir->operands, 2);
			break;
			
		case ir_binop_mod:
			this->emit_expression(lima_gp_ir_op_mod, ir->operands, 2);
			break;
			
		case ir_binop_less:
			this->emit_expression(lima_gp_ir_op_lt, ir->operands, 2);
			break;
			
		case ir_binop_greater:
			operands[0] = ir->operands[1];
			operands[1] = ir->operands[0];
			this->emit_expression(lima_gp_ir_op_lt, operands, 2);
			break;
			
		case ir_binop_lequal:
			operands[0] = ir->operands[1];
			operands[1] = ir->operands[0];
			this->emit_expression(lima_gp_ir_op_ge, operands, 2);
			break;
			
		case ir_binop_gequal:
			this->emit_expression(lima_gp_ir_op_ge, ir->operands, 2);
			break;
			
		case ir_binop_equal:
			this->emit_expression(lima_gp_ir_op_eq, ir->operands, 2);
			break;
			
		case ir_binop_nequal:
		case ir_binop_logic_xor:
			this->emit_expression(lima_gp_ir_op_ne, ir->operands, 2);
			break;
			
		case ir_binop_all_equal:
			this->emit_expression(lima_gp_ir_op_eq, ir->operands, 2);
			build_reduction(lima_gp_ir_op_min, this->cur_nodes,
							ir->operands[0]->type->vector_elements);
			break;
			
		case ir_binop_any_nequal:
			this->emit_expression(lima_gp_ir_op_ne, ir->operands, 2);
			build_reduction(lima_gp_ir_op_max, this->cur_nodes,
							ir->operands[0]->type->vector_elements);
			break;
			
		case ir_binop_dot:
			this->emit_expression(lima_gp_ir_op_add, ir->operands, 2);
			build_reduction(lima_gp_ir_op_add, this->cur_nodes,
							ir->operands[0]->type->vector_elements);
			break;
			
		case ir_binop_min:
		case ir_binop_logic_and:
			this->emit_expression(lima_gp_ir_op_min, ir->operands, 2);
			break;
			
		case ir_binop_max:
		case ir_binop_logic_or:
			this->emit_expression(lima_gp_ir_op_min, ir->operands, 2);
			break;
			
		case ir_binop_pow:
			this->emit_expression(lima_gp_ir_op_pow, ir->operands, 2);
			break;
			
		case ir_triop_lrp:
			this->emit_expression(lima_gp_ir_op_lrp, ir->operands, 3);
			break;
			
		case ir_triop_csel:
			this->emit_expression(lima_gp_ir_op_select, ir->operands, 3);
			break;
			
		case ir_quadop_vector:
			unsigned i;
			
			for (i = 0; i < ir->get_num_operands(); i++)
			{
				ir->operands[i]->accept(this);
				nodes[i] = this->cur_nodes[0];
			}
			
			for (i = 0; i < ir->get_num_operands(); i++)
				this->cur_nodes[i] = nodes[i];
			for (; i < 4; i++)
				this->cur_nodes[i] = NULL;
			
			break;
			
		default:
			assert(!"unhandled opcode");
	}
	
	return visit_continue_with_parent;
}

//emits the opposite of the given expression
//sometimes, we can use De Morgan's laws to make this more optimal

void gp_ir_visitor::emit_inverse_cond(ir_rvalue* ir)
{
	lima_gp_ir_node_t* inputs[4];
	ir_rvalue* operands[2];
	
	ir_expression* expr = ir->as_expression();
	if (!expr)
		goto default_case;
	
	switch (expr->operation)
	{
		case ir_unop_logic_not:
			expr->operands[0]->accept(this);
			break;
			
		case ir_unop_any:
			this->emit_inverse_cond(expr->operands[0]);
			build_reduction(lima_gp_ir_op_max, this->cur_nodes,
							expr->operands[0]->type->vector_elements);
			break;
			
		case ir_binop_logic_and:
			this->emit_inverse_cond(expr->operands[0]);
			for (unsigned i = 0; i < expr->type->vector_elements; i++)
				inputs[i] = this->cur_nodes[i];
			this->emit_inverse_cond(expr->operands[1]);
			for (unsigned i = 0; i < expr->type->vector_elements; i++)
				this->cur_nodes[i] = build_alu_dual(lima_gp_ir_op_max,
													inputs[i],
													this->cur_nodes[i]);
			break;
			
			
		case ir_binop_logic_or:
			this->emit_inverse_cond(expr->operands[0]);
			for (unsigned i = 0; i < expr->type->vector_elements; i++)
				inputs[i] = this->cur_nodes[i];
			this->emit_inverse_cond(expr->operands[1]);
			for (unsigned i = 0; i < expr->type->vector_elements; i++)
				this->cur_nodes[i] = build_alu_dual(lima_gp_ir_op_min,
													inputs[i],
													this->cur_nodes[i]);
			break;
		
		// !(a > b) = (a <= b)
		case ir_binop_greater:
			operands[0] = expr->operands[1];
			operands[1] = expr->operands[0];
			this->emit_expression(lima_gp_ir_op_ge, operands, 2);
			break;
		
		// !(a < b) = (a >= b)
		case ir_binop_less:
			this->emit_expression(lima_gp_ir_op_ge, expr->operands, 2);
			break;
		
		// !(a >= b) = (a < b)
		case ir_binop_gequal:
			this->emit_expression(lima_gp_ir_op_lt, expr->operands, 2);
			break;
		
		// !(a <= b) = (a > b)
		case ir_binop_lequal:
			operands[0] = expr->operands[1];
			operands[1] = expr->operands[0];
			this->emit_expression(lima_gp_ir_op_lt, operands, 2);
			break;
			
		case ir_binop_equal:
			this->emit_expression(lima_gp_ir_op_ne, operands, 2);
			break;
			
		case ir_binop_nequal:
			this->emit_expression(lima_gp_ir_op_eq, operands, 2);
			break;
			
		case ir_binop_all_equal:
			this->emit_expression(lima_gp_ir_op_ne, expr->operands, 2);
			build_reduction(lima_gp_ir_op_max, this->cur_nodes,
							expr->operands[0]->type->vector_elements);
			break;
			
		case ir_binop_any_nequal:
			this->emit_expression(lima_gp_ir_op_eq, expr->operands, 2);
			build_reduction(lima_gp_ir_op_min, this->cur_nodes,
							expr->operands[0]->type->vector_elements);
			break;
			
		default:
			goto default_case;
	}
	
	return;
	
	default_case:
	this->emit_expression(lima_gp_ir_op_not, &ir, 1);
}

void gp_ir_visitor::emit_expression(lima_gp_ir_op_e op, ir_rvalue** sources,
									unsigned num_sources)
{
	//some GLSL IR opcodes (e.g. mod) allow the first or second source to be
	//only one element, while the other sources aren't, which is equivalent to
	//swizzling  of all x's.
	bool replicate_first = false, replicate_second = false;
	unsigned size;
	if (num_sources != 1 &&
		sources[0]->type->vector_elements == 1 &&
		sources[1]->type->vector_elements != 1)
	{
		replicate_first = true;
		size = sources[1]->type->vector_elements;
	}
	else if (num_sources != 1 &&
			 sources[0]->type->vector_elements != 1 &&
			 sources[1]->type->vector_elements == 1)
	{
		replicate_second = true;
		size = sources[0]->type->vector_elements;
	}
	else
	{
		size = sources[0]->type->vector_elements;
	}
	
	lima_gp_ir_alu_node_t* nodes[4];
	for (unsigned i = 0; i < size; i++)
	{
		nodes[i] = lima_gp_ir_alu_node_create(op);
	}
	
	for (unsigned i = 0; i < num_sources; i++)
	{
		sources[i]->accept(this);
		for (unsigned j = 0; j < size; j++)
		{
			if ((i == 0 && replicate_first) || (i == 1 && replicate_second))
			{
				nodes[j]->children[i] = this->cur_nodes[0];
				lima_gp_ir_node_link(&nodes[j]->node, this->cur_nodes[0]);
			}
			else
			{
				nodes[j]->children[i] = this->cur_nodes[j];
				lima_gp_ir_node_link(&nodes[j]->node, this->cur_nodes[j]);
			}
		}
	}
	
	unsigned i;
	for (i = 0; i < size; i++)
		this->cur_nodes[i] = &nodes[i]->node;
	for (; i < 4; i++)
		this->cur_nodes[i] = NULL;
}

ir_visitor_status gp_ir_visitor::visit(ir_constant* ir)
{
	unsigned num_components = ir->type->vector_elements;
	unsigned i;
	for (i = 0; i < num_components; i++)
	{
		lima_gp_ir_const_node_t* node = lima_gp_ir_const_node_create();
		switch (ir->type->base_type)
		{
			case GLSL_TYPE_FLOAT:
				node->constant = ir->value.f[i];
				break;
				
			case GLSL_TYPE_INT:
				node->constant = (double) ir->value.i[i];
				break;
				
			case GLSL_TYPE_BOOL:
				node->constant = (double) ir->value.b[i];
				break;
				
			default:
				assert(0);
		}
		
		this->cur_nodes[i] = &node->node;
	}
	
	for (; i < 4; i++)
		this->cur_nodes[i] = NULL;
	
	return visit_continue;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_swizzle* ir)
{
	ir->val->accept(this);
	
	bool used[4] = {false, false, false, false};
	lima_gp_ir_node_t* nodes[4] = {NULL, NULL, NULL, NULL};
	unsigned components[4] = {
		ir->mask.x, ir->mask.y, ir->mask.z, ir->mask.w
	};
	unsigned i;
	
	for (i = 0; i < ir->type->vector_elements; i++)
	{
		nodes[i] = this->cur_nodes[components[i]];
		used[components[i]] = true;
	}
	
	//delete unused components, so we don't leak them
	for (i = 0; i < 4; i++)
		if (this->cur_nodes[i] && !used[i])
			lima_gp_ir_node_delete(this->cur_nodes[i]);
	
	for (i = 0; i < ir->type->vector_elements; i++)
		this->cur_nodes[i] = nodes[i];
	for (; i < 4; i++)
		this->cur_nodes[i] = NULL;
	
	return visit_continue_with_parent;
}

ir_visitor_status gp_ir_visitor::visit(ir_dereference_variable* ir)
{
	this->handle_deref(ir);
	return visit_continue;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_dereference_array* ir)
{
	this->handle_deref(ir);
	return visit_continue_with_parent;
}

ir_visitor_status gp_ir_visitor::visit_enter(ir_dereference_record* ir)
{
	this->handle_deref(ir);
	return visit_continue_with_parent;
}

void gp_ir_visitor::handle_deref(ir_dereference* ir)
{
	if (this->in_assignee)
	{
		unsigned wrmask = this->base_ir->as_assignment()->write_mask;
		switch (ir->variable_referenced()->data.mode)
		{
			case ir_var_temporary_ssa:
				this->emit_reg_store(ir);
				break;
				
			case ir_var_temporary:
			case ir_var_auto:
				this->emit_temp_store(ir, wrmask);
				break;
				
			case ir_var_shader_out:
				if (strcmp(ir->variable_referenced()->name, "gl_Position") == 0)
					this->emit_output();
				else
					this->emit_varying_store(ir, wrmask);
				break;
				
			default:
				assert(0);
		}
	}
	else
	{
		switch (ir->variable_referenced()->data.mode)
		{
			case ir_var_temporary_ssa:
				this->emit_reg_load(ir);
				break;
				
			case ir_var_temporary:
			case ir_var_auto:
				this->emit_uniform_load(ir, true);
				break;
				
			case ir_var_uniform:
				this->emit_uniform_load(ir, false);
				break;
				
			case ir_var_shader_in:
				this->emit_attr_load(ir);
				break;
				
			default:
				assert(0);
		}
	}
}

void gp_ir_visitor::emit_reg_store(ir_dereference* deref)
{
	lima_gp_ir_reg_t* reg = lima_gp_ir_reg_create(this->prog);
	ir_variable* var = deref->variable_referenced();
	reg->size = var->type->vector_elements;
	_mesa_hash_table_insert(this->var_to_reg, _mesa_hash_pointer(var), var, reg);
	
	lima_gp_ir_store_reg_node_t* store_reg = lima_gp_ir_store_reg_node_create();
	store_reg->reg = reg;
	
	for (unsigned i = 0; i < deref->type->vector_elements; i++)
	{
		store_reg->mask[i] = true;
		store_reg->children[i] = this->cur_nodes[i];
		lima_gp_ir_node_link(&store_reg->root_node.node, this->cur_nodes[i]);
	}
	
	lima_gp_ir_block_insert_end(this->cur_block, &store_reg->root_node);
}

void gp_ir_visitor::emit_reg_load(ir_dereference* deref)
{
	ir_variable* var = deref->variable_referenced();
	
	struct hash_entry* entry;
	entry = _mesa_hash_table_search(this->var_to_reg, _mesa_hash_pointer(var),
									var);
	lima_gp_ir_reg_t* reg = (lima_gp_ir_reg_t*) entry->data;
	
	for (unsigned i = 0; i < deref->type->vector_elements; i++)
	{
		lima_gp_ir_load_reg_node_t* load = lima_gp_ir_load_reg_node_create();
		load->reg = reg;
		load->component = i;
		this->cur_nodes[i] = &load->node;
	}
}

static lima_symbol_t* get_struct_field(lima_symbol_t* symbol, const char* field)
{
	for (unsigned i = 0; i < symbol->num_children; i++)
		if (strcmp(field, symbol->children[i]->name) == 0)
			return symbol->children[i];
	
	return NULL;
}

/* build the expression for outputting to gl_Position, which goes like:
 *
 * (def_expr gl_pos_inv
 *   (clamp_const -1e10 1e10 (rcp (expr result_w))))
 *
 * (store_varying 0
 *   x:
 *   (add
 *     (mul (mul (expr result_x) (load_uniform gl_mali_ViewpointTransform[0].x))
 *       (expr gl_pos_inv))
 *     (load_uniform gl_mali_ViewpointTransform[1].x))
 *   y:
 *   (add
 *     (mul (mul (expr result_y) (load_uniform gl_mali_ViewpointTransform[0].y))
 *       (expr gl_pos_inv))
 *     (load_uniform gl_mali_ViewpointTransform[1].y))
 *   z:
 *   (add
 *     (mul (mul (expr result_z) (load_uniform gl_mali_ViewpointTransform[0].z))
 *       (expr gl_pos_inv))
 *     (load_uniform gl_mali_ViewpointTransform[1].z))
 *   w:
 *   (expr gl_pos_inv))
 *
 * where result_x, result_y, result_z, and result_w are what was originally
 * assigned to gl_FragCoord (in this case, this->cur_nodes).
 */


void gp_ir_visitor::emit_output()
{
	lima_symbol_t* position_sym =
		lima_symbol_table_find(&this->symbols->varying_table,
							   "gl_Position");
	
	lima_symbol_t* transform_sym =
		lima_symbol_table_find(&this->symbols->uniform_table,
							   "gl_mali_ViewportTransform");
	
	unsigned trans_index = transform_sym->offset / 4;
	
	//first, build gl_pos_inv
	lima_gp_ir_node_t* gl_pos_inv =
		build_clamp_const(-1e10, 1e10, build_alu_single(lima_gp_ir_op_rcp,
														this->cur_nodes[3]));
	
	lima_gp_ir_node_t* outputs[3];
	
	for (unsigned i = 0; i < 3; i++)
	{
		lima_gp_ir_load_node_t* scale =
			lima_gp_ir_load_node_create(lima_gp_ir_op_load_uniform);
		scale->index = trans_index;
		scale->component = i;
		scale->offset = false;
		
		lima_gp_ir_load_node_t* bias =
			lima_gp_ir_load_node_create(lima_gp_ir_op_load_uniform);
		bias->index = trans_index + 1;
		bias->component = i;
		bias->offset = false;
		
		outputs[i] = build_alu_dual(lima_gp_ir_op_add,
			build_alu_dual(lima_gp_ir_op_mul,
				build_alu_dual(lima_gp_ir_op_mul,
					this->cur_nodes[i],
					&scale->node),
				gl_pos_inv),
			&bias->node);
	}
	
	lima_gp_ir_store_node_t* store = lima_gp_ir_store_node_create(lima_gp_ir_op_store_varying);
	
	lima_gp_ir_block_insert_end(this->cur_block, &store->root_node);
	
	store->mask[0] = store->mask[1] = store->mask[2] = store->mask[3] = true;
	store->index = position_sym->offset / 4;
	store->children[0] = outputs[0];
	store->children[1] = outputs[1];
	store->children[2] = outputs[2];
	store->children[3] = gl_pos_inv;
	lima_gp_ir_node_link(&store->root_node.node, outputs[0]);
	lima_gp_ir_node_link(&store->root_node.node, outputs[1]);
	lima_gp_ir_node_link(&store->root_node.node, outputs[2]);
	lima_gp_ir_node_link(&store->root_node.node, gl_pos_inv);
}

void gp_ir_visitor::emit_temp_store(ir_dereference* deref, unsigned wrmask)
{
	lima_gp_ir_node_t* inputs[4];
	
	for (unsigned i = 0; i < deref->type->vector_elements; i++)
		inputs[i] = this->cur_nodes[i];
	
	lima_gp_ir_node_t* offset;
	lima_symbol_t* symbol;
	unsigned index = this->calc_deref_offset(deref, &symbol, &offset);
	unsigned component_off = index % 4;
	index = index / 4;
	index += this->symbols->uniform_table.total_size / 4;
	
	lima_gp_ir_const_node_t* const_off = lima_gp_ir_const_node_create();
	const_off->constant = (float) index;
	
	if (offset)
		offset = build_alu_dual(lima_gp_ir_op_add, &const_off->node, offset);
	else
		offset = &const_off->node;
	
	lima_gp_ir_store_node_t* store = lima_gp_ir_store_node_create(lima_gp_ir_op_store_temp);
	store->addr = offset;
	
	lima_gp_ir_block_insert_end(this->cur_block, &store->root_node);
	
	unsigned component = 0;
	for (unsigned i = 0; i < 4; i++)
	{
		if (!(wrmask & (1 << i)))
			continue;
		
		lima_gp_ir_node_t* input = inputs[component];
		store->mask[i + component_off] = true;
		store->children[i + component_off] = input;
		lima_gp_ir_node_link(&store->root_node.node, input);
		
		component++;
	}
}

void gp_ir_visitor::emit_uniform_load(ir_dereference* deref, bool is_temp)
{
	lima_gp_ir_node_t* offset;
	lima_symbol_t* symbol;
	unsigned index = this->calc_deref_offset(deref, &symbol, &offset);
	unsigned component_off = index % 4;
	index = index / 4;
	
	if (is_temp)
		index += this->symbols->uniform_table.total_size / 4;

	//emit the offset
	
	if (offset)
	{
		lima_gp_ir_op_e op;
		switch (this->cur_offset_reg)
		{
			case 0:
				op = lima_gp_ir_op_store_temp_load_off0; break;
			case 1:
				op = lima_gp_ir_op_store_temp_load_off1; break;
			case 2:
				op = lima_gp_ir_op_store_temp_load_off2; break;
			default:
				assert(0);
				op = lima_gp_ir_op_store_temp_load_off0;
		}
		
		lima_gp_ir_store_node_t* store_off = lima_gp_ir_store_node_create(op);
		store_off->mask[0] = true;
		store_off->children[0] = offset;
		lima_gp_ir_node_link(&store_off->root_node.node, offset);
		lima_gp_ir_block_insert_end(this->cur_block, &store_off->root_node);
	}
	
	//emit a register store with the uniform/temp load as a source
	//this ensures that the uniform load will happen immediately after the
	//offset register is stored, since the load will be a child of the
	//register store node which comes immediately after the offset store.
	//Hopefully the register-elimination pass will get rid of most of the mess.
	
	lima_gp_ir_reg_t* reg = lima_gp_ir_reg_create(this->prog);
	lima_gp_ir_store_reg_node_t* store_reg = lima_gp_ir_store_reg_node_create();
	store_reg->reg = reg;
	
	lima_gp_ir_block_insert_end(this->cur_block, &store_reg->root_node);
	
	for (unsigned i = 0; i < deref->type->vector_elements; i++)
	{
		lima_gp_ir_load_node_t* load = lima_gp_ir_load_node_create(lima_gp_ir_op_load_uniform);
		load->index = index;
		load->component = i + component_off;
		if (offset)
		{
			load->offset = true;
			load->off_reg = this->cur_offset_reg;
		}
		
		store_reg->mask[i] = true;
		store_reg->children[i] = &load->node;
		lima_gp_ir_node_link(&store_reg->root_node.node, &load->node);
	}
	
	for (unsigned i = 0; i < deref->type->vector_elements; i++)
	{
		lima_gp_ir_load_reg_node_t* load = lima_gp_ir_load_reg_node_create();
		load->reg = reg;
		load->component = i;
		
		this->cur_nodes[i] = &load->node;
	}
	
	if (offset)
		this->cur_offset_reg = (this->cur_offset_reg + 1) % 3;
}

void gp_ir_visitor::emit_attr_load(ir_dereference* deref)
{
	lima_symbol_t* symbol;
	unsigned index = this->calc_const_deref_offset(deref, &symbol) / 4;
	
	for (unsigned i = 0; i < deref->type->vector_elements; i++)
	{
		lima_gp_ir_load_node_t* load =
			lima_gp_ir_load_node_create(lima_gp_ir_op_load_attribute);
		load->index = index;
		load->component = i;
		this->cur_nodes[i] = &load->node;
	}
}

void gp_ir_visitor::emit_varying_store(ir_dereference* deref, unsigned wrmask)
{
	lima_symbol_t* symbol;
	unsigned index = this->calc_const_deref_offset(deref, &symbol);
	unsigned component_off = index % 4;
	index /= 4;
	
	lima_gp_ir_store_node_t* store = lima_gp_ir_store_node_create(lima_gp_ir_op_store_varying);
	store->index = index;
	
	lima_gp_ir_block_insert_end(this->cur_block, &store->root_node);
	
	unsigned component = 0;
	for (unsigned i = 0; i < 4; i++)
	{
		if (!(wrmask & (1 << i)))
			continue;
		
		store->mask[i + component_off] = true;
		store->children[i + component_off] = this->cur_nodes[component];
		lima_gp_ir_node_link(&store->root_node.node, this->cur_nodes[component]);
		
		component++;
	}
}

//for varyings, attributes

unsigned gp_ir_visitor::calc_const_deref_offset(ir_dereference* deref,
												lima_symbol_t** out_symbol)
{
	ir_dereference_variable* deref_var = deref->as_dereference_variable();
	ir_dereference_array* deref_array = deref->as_dereference_array();
	ir_dereference_record* deref_record = deref->as_dereference_record();
	
	if (deref_var)
	{
		struct hash_entry* entry =
		_mesa_hash_table_search(this->glsl_symbols,
								_mesa_hash_pointer(deref_var->var),
								deref_var->var);
		
		lima_symbol_t* symbol = (lima_symbol_t*) entry->data;
		*out_symbol = symbol;
		return symbol->offset;
	}
	else if (deref_array)
	{
		unsigned offset = this->calc_const_deref_offset(deref_array->array->as_dereference(),
														out_symbol);
		ir_constant* constant = deref_array->array_index->as_constant();
		assert(constant);
		return offset + constant->value.i[0] * (*out_symbol)->stride;
	}
	else
	{
		unsigned offset = this->calc_const_deref_offset(deref_record->record->as_dereference(),
														out_symbol);
		
		lima_symbol_t* field = get_struct_field(*out_symbol, deref_record->field);
		
		*out_symbol = field;
		return offset + field->offset;
	}
}

//for uniforms, temporaries

unsigned gp_ir_visitor::calc_deref_offset(ir_dereference* deref,
										  lima_symbol_t** out_symbol,
										  lima_gp_ir_node_t** out_indirect)
{
	ir_dereference_variable* deref_var = deref->as_dereference_variable();
	ir_dereference_array* deref_array = deref->as_dereference_array();
	ir_dereference_record* deref_record = deref->as_dereference_record();
	
	if (deref_var)
	{
		struct hash_entry* entry =
		_mesa_hash_table_search(this->glsl_symbols,
								_mesa_hash_pointer(deref_var->var),
								deref_var->var);
		
		lima_symbol_t* symbol = (lima_symbol_t*) entry->data;
		*out_symbol = symbol;
		*out_indirect = NULL;
		return symbol->offset;
	}
	else if (deref_array)
	{
		unsigned offset = this->calc_deref_offset(deref_array->array->as_dereference(),
												  out_symbol, out_indirect);
		
		/*
		 * Matrices are accessed through array dereferences (at this point, we
		 * should've lowered everything so that matrices aren't referenced
		 * directly). The stride information inside the symbol is incorrect
		 * for this case, since it is the stride of the whole symbol and not
		 * the stride of the individual columns. So here, we detect if we're
		 * dereferencing a matrix and supply the correct stride instead.
		 */
		
		unsigned stride = (*out_symbol)->stride;
		if (deref_array->array->type->is_matrix())
			stride = stride / deref_array->array->type->matrix_columns;
		
		ir_constant* constant = deref_array->array_index->as_constant();
		if (constant)
			return offset + constant->value.i[0] * stride;
		
		bool old_in_assignee = this->in_assignee;
		this->in_assignee = false;
		
		deref_array->array_index->accept(this);
		lima_gp_ir_node_t* index = this->cur_nodes[0];
		
		lima_gp_ir_node_t* new_offset;
		
		if (stride != 4)
		{
			lima_gp_ir_const_node_t* stride_node = lima_gp_ir_const_node_create();
			stride_node->constant = stride / 4;
			new_offset = build_alu_dual(lima_gp_ir_op_mul, index,
										&stride_node->node);
		}
		else
			new_offset = index;
		
		if (*out_indirect)
		{
			*out_indirect = build_alu_dual(lima_gp_ir_op_add, *out_indirect,
										   new_offset);
		}
		else
			*out_indirect = new_offset;
		
		this->in_assignee = old_in_assignee;
		
		return offset;
	}
	
	unsigned offset = this->calc_deref_offset(deref_record->record->as_dereference(),
											  out_symbol, out_indirect);
	
	lima_symbol_t* field = get_struct_field(*out_symbol, deref_record->field);
	*out_symbol = field;
	return offset + field->offset;
}
