/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2014 Connor Abbott (connor@abbott.cx)
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
#include "ir_optimization.h"
#include "main/hash_table.h"
#include "ir_dead_branches.h"
#include "pp_hir.h"
#include "shader/shader_internal.h"

/* lower GLSL IR to pp_hir */

/*
 * We can't convert the IR in one pass, since sometimes (i.e. for loop phi nodes)
 * the source instruction of an instruction we're visiting won't have been
 * visited yet. Instead, we need a two pass approach, where first we generate
 * instructions and then resolve sources. We want the conversion to happen in
 * one place, though (we don't want to convert sources in a different part then
 * we generate instructions...) so the first pass will do the bulk of the work,
 * storing which source entries in the pp_hir program correspond to which
 * SSA variables in the GLSL IR source. We'll also update a map of which SSA
 * variables correspond to which pp_hir instructions as we go along, so in the
 * second pass all we have to do is match the two up, filling in the sources.
 */

namespace {

class ir_to_pp_hir_visitor : public ir_hierarchical_visitor
{
public:
	ir_to_pp_hir_visitor(lima_pp_hir_prog_t* prog, lima_core_e core,
						 lima_shader_symbols_t* symbols,
						 struct hash_table* glsl_symbols,
						 ir_dead_branches_visitor* dbv);
	
	~ir_to_pp_hir_visitor();
	
	virtual ir_visitor_status visit_enter(ir_if*);
	virtual ir_visitor_status visit_enter(ir_loop*);
	virtual ir_visitor_status visit(ir_loop_jump*);
	virtual ir_visitor_status visit_enter(ir_return*);
	virtual ir_visitor_status visit_enter(ir_discard*);
	virtual ir_visitor_status visit_enter(ir_function*);
	virtual ir_visitor_status visit_enter(ir_assignment*);
	virtual ir_visitor_status visit_enter(ir_expression*);
	virtual ir_visitor_status visit_enter(ir_texture*);
	virtual ir_visitor_status visit(ir_constant*);
	virtual ir_visitor_status visit_enter(ir_swizzle*);
	virtual ir_visitor_status visit(ir_phi_if*);
	virtual ir_visitor_status visit(ir_phi_loop_begin*);
	virtual ir_visitor_status visit(ir_phi_loop_end*);
	virtual ir_visitor_status visit(ir_dereference_variable*);
	virtual ir_visitor_status visit_enter(ir_dereference_array*);
	virtual ir_visitor_status visit_enter(ir_dereference_record*);
	
	void rewrite_phi_if(ir_phi_if* phi, ir_if* if_stmt);
	void rewrite_phi_loop_begin(ir_phi_loop_begin* phi, ir_loop* loop);
	void rewrite_phi_loop_end(ir_phi_loop_end* phi);
	
private:
	void emit_if_cond(ir_rvalue* ir);
	
	int try_emit_sampler_index(ir_dereference* deref);
	
	void calc_deref_offset(unsigned* offset, ir_dereference* deref,
						   lima_pp_hir_cmd_t** out_indirect,
						   lima_symbol_t** out_symbol, unsigned alignment);
	void emit_load(ir_variable_mode mode, unsigned offset,
				   unsigned num_components, lima_pp_hir_cmd_t* indirect_offset);
	void emit_store(lima_pp_hir_cmd_t* value, ir_variable_mode mode,
					unsigned offset, unsigned num_components,
					lima_pp_hir_cmd_t* indirect_offset);
	void emit_writemask_store(lima_pp_hir_cmd_t* value, ir_variable_mode mode,
							  unsigned offset, unsigned num_components,
							  lima_pp_hir_cmd_t* indirect_offset,
							  unsigned write_mask);
	
	void rewrite_phi_source(lima_pp_hir_cmd_t* phi,
							lima_pp_hir_block_t* block, ir_variable* source);
	
	void rewrite_phi_jump_srcs(lima_pp_hir_cmd_t* phi, exec_list* srcs);
	
	void handle_deref(ir_dereference* ir);
	
	lima_core_e core;
	
	lima_pp_hir_prog_t* prog;
	lima_pp_hir_block_t* cur_block;
	//blocks to jump to for break and continue
	lima_pp_hir_block_t* break_block, *continue_block;
	lima_pp_hir_cmd_t* cur_cmd, *output_cmd;
	struct hash_table* var_to_cmd;
	
	lima_shader_symbols_t* symbols;
	struct hash_table* glsl_symbols;
	
	ir_dead_branches_visitor* dbv;
	
	//info used for figuring out phi sources
	struct hash_table* then_branch_to_block;
	struct hash_table* else_branch_to_block;
	struct hash_table* loop_jump_to_block;
	struct hash_table* loop_beginning_to_block;
	struct hash_table* loop_end_to_block;
	struct hash_table* phi_to_phi;
};

class ir_phi_rewrite_visitor : public ir_hierarchical_visitor
{
public:
	ir_phi_rewrite_visitor(ir_to_pp_hir_visitor* v) : v(v)
	{
	}
	
	virtual ir_visitor_status visit_leave(ir_if*);
	virtual ir_visitor_status visit_leave(ir_loop*);
	
private:
	ir_to_pp_hir_visitor* v;
};

}; /* end private namespace */

/* the entrypoint of the whole thing */

void lima_lower_to_pp_hir(lima_shader_t* shader)
{
	ir_dead_branches_visitor dbv;
	dbv.run(shader->linked_shader->ir);
	
	shader->ir.pp.hir_prog = lima_pp_hir_prog_create();
	ir_to_pp_hir_visitor v(shader->ir.pp.hir_prog, shader->core,
						   &shader->symbols, shader->glsl_symbols, &dbv);
	v.run(shader->linked_shader->ir);
	
	lima_pp_hir_prog_add_predecessors(shader->ir.pp.hir_prog);
	
	ir_phi_rewrite_visitor prv(&v);
	prv.run(shader->linked_shader->ir);
	
	unsigned temp_size = shader->symbols.temporary_table.total_size;
	shader->ir.pp.hir_prog->temp_alloc = (temp_size + 3) / 4;
}

ir_to_pp_hir_visitor::ir_to_pp_hir_visitor(lima_pp_hir_prog_t* prog,
										   lima_core_e core,
										   lima_shader_symbols_t* symbols,
										   struct hash_table* glsl_symbols,
										   ir_dead_branches_visitor* dbv)
	: core(core), prog(prog), symbols(symbols), glsl_symbols(glsl_symbols),
	  dbv(dbv)
{
	this->cur_block = lima_pp_hir_block_create();
	lima_pp_hir_prog_insert_start(this->cur_block, prog);
	this->cur_cmd = NULL;
	this->output_cmd = NULL;
	this->break_block = NULL;
	this->continue_block = NULL;
	this->var_to_cmd = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
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

ir_to_pp_hir_visitor::~ir_to_pp_hir_visitor()
{
	_mesa_hash_table_destroy(this->var_to_cmd, NULL);
	_mesa_hash_table_destroy(this->phi_to_phi, NULL);
	_mesa_hash_table_destroy(this->then_branch_to_block, NULL);
	_mesa_hash_table_destroy(this->else_branch_to_block, NULL);
	_mesa_hash_table_destroy(this->loop_jump_to_block, NULL);
	_mesa_hash_table_destroy(this->loop_beginning_to_block, NULL);
	_mesa_hash_table_destroy(this->loop_end_to_block, NULL);
}

void ir_to_pp_hir_visitor::emit_if_cond(ir_rvalue* ir)
{
	ir_expression* expr = ir->as_expression();
	if (!expr)
		goto general_case;
	
	switch (expr->operation)
	{
		case ir_binop_less:
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_lt;
			break;
			
		case ir_binop_greater:
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_gt;
			break;
			
		case ir_binop_lequal:
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_le;
			break;
			
		case ir_binop_gequal:
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_ge;
			break;
			
		case ir_binop_equal:
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_eq;
			break;
			
		case ir_binop_nequal:
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_ne;
			break;
			
		default:
			goto general_case;
	}
	
	expr->operands[0]->accept(this);
	this->cur_block->reg_cond_a.is_constant = false;
	this->cur_block->reg_cond_a.reg = this->cur_cmd;
	ptrset_add(&this->cur_cmd->block_uses, this->cur_block);
	expr->operands[1]->accept(this);
	this->cur_block->reg_cond_b.is_constant = false;
	this->cur_block->reg_cond_b.reg = this->cur_cmd;
	ptrset_add(&this->cur_cmd->block_uses, this->cur_block);
	
	return;
	
	general_case:
	
	this->cur_block->branch_cond = lima_pp_hir_branch_cond_ne;
	ir->accept(this);
	this->cur_block->reg_cond_a.is_constant = false;
	this->cur_block->reg_cond_a.reg = this->cur_cmd;
	ptrset_add(&this->cur_cmd->block_uses, this->cur_block);
	this->cur_block->reg_cond_b.is_constant = true;
	this->cur_block->reg_cond_b.constant = 0.0;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_if* ir)
{	
	this->cur_block->is_end = false;
	this->emit_if_cond(ir->condition);
	
	ir_dead_branches* db = this->dbv->get_dead_branches(ir);
	
	lima_pp_hir_block_t* old_block = this->cur_block;
	lima_pp_hir_block_t* new_block = lima_pp_hir_block_create();
	if (!ir->then_instructions.is_empty())
	{
		lima_pp_hir_block_t* if_block = lima_pp_hir_block_create();
		lima_pp_hir_prog_insert_end(if_block, this->prog);
		old_block->next[0] = if_block;
		this->cur_block = if_block;
		visit_list_elements(this, &ir->then_instructions);
		if (!db->then_dead)
		{
			this->cur_block->is_end = false;
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_always;
			this->cur_block->next[0] = new_block;
		}
		_mesa_hash_table_insert(this->then_branch_to_block,
								_mesa_hash_pointer(ir), ir, this->cur_block);
	}
	else
	{
		old_block->next[0] = new_block;
		_mesa_hash_table_insert(this->then_branch_to_block,
								_mesa_hash_pointer(ir), ir, old_block);
	}
	
	if (!ir->else_instructions.is_empty())
	{
		lima_pp_hir_block_t* else_block = lima_pp_hir_block_create();
		lima_pp_hir_prog_insert_end(else_block, this->prog);
		old_block->next[1] = else_block;
		this->cur_block = else_block;
		visit_list_elements(this, &ir->else_instructions);
		if (!db->else_dead)
		{
			this->cur_block->is_end = false;
			this->cur_block->branch_cond = lima_pp_hir_branch_cond_always;
			this->cur_block->next[0] = new_block;
		}
		_mesa_hash_table_insert(this->else_branch_to_block,
								_mesa_hash_pointer(ir), ir, this->cur_block);
	}
	else
	{
		old_block->next[1] = new_block;
		_mesa_hash_table_insert(this->else_branch_to_block,
								_mesa_hash_pointer(ir), ir, old_block);
	}
	

	lima_pp_hir_prog_insert_end(new_block, this->prog);
	this->cur_block = new_block;
	visit_list_elements(this, &ir->phi_nodes, false);
	
	return visit_continue_with_parent;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_loop* ir)
{
	visit_list_elements(this, &ir->begin_phi_nodes, false);
	
	_mesa_hash_table_insert(this->loop_beginning_to_block,
							_mesa_hash_pointer(ir), ir, this->cur_block);
	
	lima_pp_hir_block_t* loop_header = lima_pp_hir_block_create();
	lima_pp_hir_block_t* after_loop = lima_pp_hir_block_create();
	
	lima_pp_hir_block_t* old_break_block = this->break_block;
	lima_pp_hir_block_t* old_continue_block = this->continue_block;
	
	this->break_block = after_loop;
	this->continue_block = loop_header;
	
	this->cur_block->is_end = false;
	this->cur_block->branch_cond = lima_pp_hir_branch_cond_always;
	this->cur_block->next[0] = loop_header;
	lima_pp_hir_prog_insert_end(loop_header, this->prog);
	this->cur_block = loop_header;
	
	visit_list_elements(this, &ir->body_instructions);
	
	_mesa_hash_table_insert(this->loop_end_to_block,
							_mesa_hash_pointer(ir), ir, this->cur_block);
	
	this->cur_block->is_end = false;
	this->cur_block->branch_cond = lima_pp_hir_branch_cond_always;
	this->cur_block->next[0] = loop_header;
	
	lima_pp_hir_prog_insert_end(after_loop, this->prog);
	this->cur_block = after_loop;
	
	visit_list_elements(this, &ir->end_phi_nodes, false);
	
	this->break_block = old_break_block;
	this->continue_block = old_continue_block;
	
	return visit_continue_with_parent;
}

ir_visitor_status ir_to_pp_hir_visitor::visit(ir_loop_jump* ir)
{
	_mesa_hash_table_insert(this->loop_jump_to_block, _mesa_hash_pointer(ir),
							ir, this->cur_block);
	
	this->cur_block->is_end = false;
	this->cur_block->branch_cond = lima_pp_hir_branch_cond_always;
	if (ir->mode == ir_loop_jump::jump_break)
		this->cur_block->next[0] = this->break_block;
	else
		this->cur_block->next[0] = this->continue_block;
	
	return visit_continue;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_return* ir)
{
	(void) ir;
	
	this->cur_block->is_end = true;
	this->cur_block->discard = false;
	this->cur_block->output = this->output_cmd;
	ptrset_add(&this->output_cmd->block_uses, this->cur_block);
	
	return visit_continue;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_discard* ir)
{
	//conditions were removed by lima_lower_conditions()
	assert(ir->condition == NULL);
	(void) ir;
	
	this->cur_block->is_end = true;
	this->cur_block->discard = true;
	
	return visit_continue;
}

ir_visitor_status ir_to_pp_hir_visitor::visit(ir_phi_if* ir)
{
	lima_pp_hir_cmd_t* phi = lima_pp_hir_phi_create(2);
	phi->dst.reg.size = ir->dest->type->vector_elements - 1;
	phi->dst.reg.index = this->prog->reg_alloc++;
	_mesa_hash_table_insert(this->var_to_cmd, _mesa_hash_pointer(ir->dest),
							ir->dest, phi);
	_mesa_hash_table_insert(this->phi_to_phi,
							_mesa_hash_pointer(ir), ir, phi);
	lima_pp_hir_block_insert_end(this->cur_block, phi);
	
	return visit_continue;
}

static unsigned list_size(exec_list* list)
{
	unsigned i = 0;
	foreach_list(node, list)
		i++;
	
	return i;
}

ir_visitor_status ir_to_pp_hir_visitor::visit(ir_phi_loop_begin* ir)
{
	unsigned num_sources = 2 + list_size(&ir->continue_srcs);
	lima_pp_hir_cmd_t* phi = lima_pp_hir_phi_create(num_sources);
	phi->dst.reg.size = ir->dest->type->vector_elements - 1;
	phi->dst.reg.index = this->prog->reg_alloc++;
	_mesa_hash_table_insert(this->var_to_cmd, _mesa_hash_pointer(ir->dest),
							ir->dest, phi);
	_mesa_hash_table_insert(this->phi_to_phi,
							_mesa_hash_pointer(ir), ir, phi);
	lima_pp_hir_block_insert_end(this->cur_block, phi);
	
	return visit_continue;
}

ir_visitor_status ir_to_pp_hir_visitor::visit(ir_phi_loop_end* ir)
{
	unsigned num_sources = list_size(&ir->break_srcs);
	lima_pp_hir_cmd_t* phi = lima_pp_hir_phi_create(num_sources);
	phi->dst.reg.size = ir->dest->type->vector_elements - 1;
	phi->dst.reg.index = prog->reg_alloc++;
	_mesa_hash_table_insert(this->var_to_cmd, _mesa_hash_pointer(ir->dest),
							ir->dest, phi);
	_mesa_hash_table_insert(this->phi_to_phi,
							_mesa_hash_pointer(ir), ir, phi);
	lima_pp_hir_block_insert_end(this->cur_block, phi);
	
	return visit_continue;
}

static unsigned get_phi_source_index(lima_pp_hir_block_t* block,
									 lima_pp_hir_block_t* pred)
{
	for (unsigned i = 0; i < block->num_preds; i++)
		if (block->preds[i] == pred)
			return i;
	
	assert(0);
	return 0;
}

void ir_to_pp_hir_visitor::rewrite_phi_source(lima_pp_hir_cmd_t* phi,
											  lima_pp_hir_block_t* block,
											  ir_variable* source)
{
	unsigned index = get_phi_source_index(phi->block, block);
	lima_pp_hir_cmd_t* phi_src;
	if (source)
	{
		struct hash_entry* entry =
			_mesa_hash_table_search(this->var_to_cmd, _mesa_hash_pointer(source),
									source);
		phi_src = (lima_pp_hir_cmd_t*) entry->data;
		ptrset_add(&phi_src->cmd_uses, phi);
	}
	else
		phi_src = NULL;
	
	phi->src[index].depend = phi_src;
}

void ir_to_pp_hir_visitor::rewrite_phi_jump_srcs(lima_pp_hir_cmd_t* phi,
												 exec_list* srcs)
{
	foreach_list(node, srcs)
	{
		ir_phi_jump_src* src = (ir_phi_jump_src*) node;
		struct hash_entry* entry =
			_mesa_hash_table_search(this->loop_jump_to_block,
									_mesa_hash_pointer(src->jump), src->jump);
		
		lima_pp_hir_block_t* pred = (lima_pp_hir_block_t*) entry->data;
		this->rewrite_phi_source(phi, pred, src->src);
	}
}

void ir_to_pp_hir_visitor::rewrite_phi_if(ir_phi_if* ir, ir_if* if_stmt)
{
	struct hash_entry* entry =
		_mesa_hash_table_search(this->phi_to_phi, _mesa_hash_pointer(ir), ir);
	lima_pp_hir_cmd_t* phi = (lima_pp_hir_cmd_t*) entry->data;
	
	entry = _mesa_hash_table_search(this->then_branch_to_block,
									_mesa_hash_pointer(if_stmt), if_stmt);
	lima_pp_hir_block_t* then_block = (lima_pp_hir_block_t*) entry->data;
	this->rewrite_phi_source(phi, then_block, ir->if_src);
	
	entry = _mesa_hash_table_search(this->else_branch_to_block,
									_mesa_hash_pointer(if_stmt), if_stmt);
	lima_pp_hir_block_t* else_block = (lima_pp_hir_block_t*) entry->data;
	this->rewrite_phi_source(phi, else_block, ir->else_src);
}

void ir_to_pp_hir_visitor::rewrite_phi_loop_begin(ir_phi_loop_begin* ir,
												  ir_loop* loop)
{
	struct hash_entry* entry =
		_mesa_hash_table_search(this->phi_to_phi, _mesa_hash_pointer(ir), ir);
	lima_pp_hir_cmd_t* phi = (lima_pp_hir_cmd_t*) entry->data;
	
	entry = _mesa_hash_table_search(this->loop_beginning_to_block,
									_mesa_hash_pointer(loop), loop);
	lima_pp_hir_block_t* enter_block = (lima_pp_hir_block_t*) entry->data;
	this->rewrite_phi_source(phi, enter_block, ir->enter_src);
	
	entry = _mesa_hash_table_search(this->loop_end_to_block,
									_mesa_hash_pointer(loop), loop);
	lima_pp_hir_block_t* repeat_block = (lima_pp_hir_block_t*) entry->data;
	this->rewrite_phi_source(phi, repeat_block, ir->repeat_src);
	
	this->rewrite_phi_jump_srcs(phi, &ir->continue_srcs);
}

void ir_to_pp_hir_visitor::rewrite_phi_loop_end(ir_phi_loop_end* ir)
{
	struct hash_entry* entry =
		_mesa_hash_table_search(this->phi_to_phi, _mesa_hash_pointer(ir), ir);
	lima_pp_hir_cmd_t* phi = (lima_pp_hir_cmd_t*) entry->data;
	
	this->rewrite_phi_jump_srcs(phi, &ir->break_srcs);
}

ir_visitor_status ir_phi_rewrite_visitor::visit_leave(ir_if* ir)
{
	foreach_list(node, &ir->phi_nodes)
	{
		ir_phi_if* phi = (ir_phi_if*) node;
		this->v->rewrite_phi_if(phi, ir);
	}
	
	return visit_continue;
}

ir_visitor_status ir_phi_rewrite_visitor::visit_leave(ir_loop* ir)
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

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_function* ir)
{
	assert(strcmp(ir->name, "main") == 0);
	
	exec_node* node = ir->signatures.get_head();
	assert(node == ir->signatures.get_tail()); //there should only be 1 signature
	
	ir_function_signature* sig = (ir_function_signature*) node;
	visit_list_elements(this, &sig->body);
	
	this->cur_block->is_end = true;
	this->cur_block->discard = false;
	this->cur_block->output = this->output_cmd;
	ptrset_add(&this->output_cmd->block_uses, this->cur_block);
	
	return visit_continue_with_parent;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_assignment* ir)
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

static lima_pp_hir_source_t get_const_source(float* value)
{
	lima_pp_hir_source_t ret = lima_pp_hir_source_default;
	ret.constant = true;
	
	ret.depend = malloc(4 * sizeof(float));
	memcpy(ret.depend, value, 4 * sizeof(float));
	return ret;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_expression* ir)
{
	lima_pp_hir_cmd_t* sources[4];
	
	for (unsigned i = 0; i < ir->get_num_operands(); i++)
	{
		ir->operands[i]->accept(this);
		sources[i] = this->cur_cmd;
	}
	
	
	lima_pp_hir_cmd_t* cmd = NULL;
	
	float const_zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	
	switch (ir->operation)
	{
		case ir_unop_logic_not:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_not);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_neg:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_neg);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_abs:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
			cmd->src[0].depend = sources[0];
			cmd->src[0].absolute = true;
			break;
			
		case ir_unop_sign:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sign);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_rcp:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_rcp);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_rsq:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_rsqrt);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_sqrt:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sqrt);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_exp2:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_exp2);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_log2:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_log2);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_f2i:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sign);
			cmd->src[0].depend = sources[0];
			cmd->dst.modifier = lima_pp_outmod_round;
			break;
			
		case ir_unop_i2f:
		case ir_unop_b2i:
		case ir_unop_b2f:
			//everything is a float, so this is a no-op
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_f2b:
		case ir_unop_i2b:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ne);
			cmd->src[0].depend = sources[0];
			cmd->src[1] = get_const_source(const_zero);
			break;
			
		case ir_unop_any:
			switch (sources[0]->dst.reg.size)
			{
				case 0:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
					cmd->src[0].depend = sources[0];
					break;
					
				case 1:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_any2);
					cmd->src[0].depend = sources[0];
					break;
				case 2:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_any3);
					cmd->src[0].depend = sources[0];
					break;
					
				case 3:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_any4);
					cmd->src[0].depend = sources[0];
					break;
					
				default:
					assert(0);
			}
			break;
			
		case ir_unop_ceil:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ceil);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_floor:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_neg);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_fract:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_fract);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_sin:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sin);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_cos:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_cos);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_dFdx:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ddx);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_unop_dFdy:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ddy);
			cmd->src[0].depend = sources[0];
			break;
			
		case ir_binop_add:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_sub:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_sub);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_mul:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_div:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_div);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_mod:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mod);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			if (sources[1]->dst.reg.size == 0 && sources[0]->dst.reg.size != 0)
			{
				cmd->src[1].swizzle[0] = 0;
				cmd->src[1].swizzle[1] = 0;
				cmd->src[1].swizzle[2] = 0;
				cmd->src[1].swizzle[3] = 0;
			}
			break;
			
		case ir_binop_less:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_gt);
			cmd->src[0].depend = sources[1];
			cmd->src[1].depend = sources[0];
			break;
			
		case ir_binop_greater:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_gt);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_lequal:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ge);
			cmd->src[0].depend = sources[1];
			cmd->src[1].depend = sources[0];
			break;
			
		case ir_binop_gequal:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ge);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_equal:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_eq);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_nequal:
		case ir_binop_logic_xor:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_ne);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		//TODO: ir_binop_all_equal, ir_binop_any_nequal
			
		case ir_binop_dot:
			switch (sources[0]->dst.reg.size)
			{
				case 0:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
					cmd->src[0].depend = sources[0];
					cmd->src[1].depend = sources[1];
					break;
					
				case 1:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_dot2);
					cmd->src[0].depend = sources[0];
					cmd->src[1].depend = sources[1];
					break;
				case 2:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_dot3);
					cmd->src[0].depend = sources[0];
					cmd->src[1].depend = sources[1];
					break;
					
				case 3:
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_dot4);
					cmd->src[0].depend = sources[0];
					cmd->src[1].depend = sources[1];
					break;
					
				default:
					assert(0);
			}
			break;
			
		case ir_binop_min:
		case ir_binop_logic_and:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_min);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_max:
		case ir_binop_logic_or:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_max);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_binop_pow:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_pow);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			break;
			
		case ir_triop_lrp:
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_lrp);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			cmd->src[2].depend = sources[2];
			break;
			
		case ir_triop_csel:
			//TODO: fix the case where the swizzle isn't all the same
			cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_select);
			cmd->src[0].depend = sources[0];
			cmd->src[1].depend = sources[1];
			cmd->src[2].depend = sources[2];
			break;
			
		case ir_quadop_vector:
			cmd = lima_pp_hir_combine_create(ir->get_num_operands());
			for (unsigned i = 0; i < ir->get_num_operands(); i++)
				cmd->src[i].depend = sources[i];
			break;
			
		default:
			assert(!"Unhandled opcode!");
	}
	
	if (ir->type->base_type == GLSL_TYPE_INT)
		cmd->dst.modifier = lima_pp_outmod_round;
	cmd->dst.reg.size = ir->type->vector_elements - 1;
	cmd->dst.reg.index = this->prog->reg_alloc++;
	
	lima_pp_hir_block_insert_end(this->cur_block, cmd);
	this->cur_cmd = cmd;
	
	return visit_continue_with_parent;
}


//returns the indirect index in this->cur_cmd
int ir_to_pp_hir_visitor::try_emit_sampler_index(ir_dereference* deref)
{
	ir_dereference_variable* deref_var = deref->as_dereference_variable();
	ir_dereference_array* deref_array = deref->as_dereference_array();
	if (!deref_var && !deref_array)
		return -1;
	
	if (deref_array && !deref_array->array->as_dereference_variable())
		return -1;
	
	this->cur_cmd = NULL;
	
	if (deref_array)
	{
		deref_array->array_index->accept(this);
		deref_var = deref_array->array->as_dereference_variable();
	}
	
	struct hash_entry* entry =
	_mesa_hash_table_search(this->glsl_symbols,
							_mesa_hash_pointer(deref_var->var), deref_var->var);
	
	lima_symbol_t* symbol = (lima_symbol_t*) entry->data;
	
	return symbol->offset;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_texture* ir)
{
	//If this sampler isn't part of a structure, the offset is
	//the offset to use for the sampler structure
	
	int offset = this->try_emit_sampler_index(ir->sampler);
	lima_pp_hir_cmd_t* indirect_offset = this->cur_cmd;
	
	if (offset == -1)
	{
		//emit a uniform load, and then load from the offset returned
		offset = 0;
		ir->sampler->accept(this);
		indirect_offset = this->cur_cmd;
	}
	
	bool has_indirect = !!indirect_offset;
	bool has_projection = !!ir->projector;
	bool is_cube = ir->sampler->type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE;
	
	lima_pp_hir_cmd_t* input_coord;
	
	ir->coordinate->accept(this);
	
	if (has_projection)
	{
		lima_pp_hir_cmd_t* non_proj_coord = this->cur_cmd;
		ir->projector->accept(this);
		lima_pp_hir_cmd_t* proj_factor = this->cur_cmd;
		input_coord = lima_pp_hir_combine_create(2);
		input_coord->src[0].depend = non_proj_coord;
		input_coord->src[1].depend = proj_factor;
		input_coord->dst.reg.size = non_proj_coord->dst.reg.size + 1;
		input_coord->dst.reg.index = prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, input_coord);
	}
	else
		input_coord = this->cur_cmd;
	
	lima_pp_hir_cmd_t* lod_bias = NULL;
	
	if (ir->op == ir_txb)
	{
		ir->lod_info.bias->accept(this);
		lod_bias = this->cur_cmd;
	}
	
	lima_pp_hir_cmd_t* cmd = NULL;
	
	switch (ir->op)
	{
		case ir_tex:
			if (has_indirect)
			{
				if (has_projection)
				{
					
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_proj_z_off);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
					cmd->src[1].depend = indirect_offset;
					
				}
				else
				{
					if (!is_cube)
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_off);
					else
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_cube_off);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
					cmd->src[1].depend = indirect_offset;
				}
			}
			else
			{
				if (has_projection)
				{
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_proj_z);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
				}
				else
				{
					if (!is_cube)
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d);
					else
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_cube);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
				}
			}
			break;
			
		case ir_txb:
			if (has_indirect)
			{
				if (has_projection)
				{
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_proj_z_off_lod);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
					cmd->src[1].depend = indirect_offset;
					cmd->src[2].depend = lod_bias;
					
				}
				else
				{
					if (!is_cube)
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_off_lod);
					else
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_cube_off_lod);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
					cmd->src[1].depend = indirect_offset;
					cmd->src[2].depend = lod_bias;
				}
			}
			else
			{
				if (has_projection)
				{
					cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_proj_z_lod);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
					cmd->src[1].depend = lod_bias;
				}
				else
				{
					if (!is_cube)
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_2d_lod);
					else
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_texld_cube_lod);
					cmd->load_store_index = offset;
					cmd->src[0].depend = input_coord;
					cmd->src[1].depend = lod_bias;
				}
			}
			break;
			
		default:
			assert(0);
	}
	
	cmd->dst.reg.size = 3;
	cmd->dst.reg.index = prog->reg_alloc++;
	lima_pp_hir_block_insert_end(this->cur_block, cmd);
	this->cur_cmd = cmd;
	
	return visit_continue_with_parent;
}

ir_visitor_status ir_to_pp_hir_visitor::visit(ir_constant* ir)
{
	unsigned num_components = ir->type->vector_elements;
	double *values = (double*) malloc(4 * sizeof(double));
	unsigned i;
	for (i = 0; i < num_components; i++)
	{
		switch (ir->type->base_type)
		{
			case GLSL_TYPE_FLOAT:
				values[i] = ir->value.f[i];
				break;
				
			case GLSL_TYPE_INT:
				values[i] = (double) ir->value.i[i];
				break;
				
			case GLSL_TYPE_BOOL:
				values[i] = (double) ir->value.b[i];
				break;
				
			default:
				assert(0);
		}
	}
	for (; i < 4; i++)
		values[i] = 0.0;
	
	lima_pp_hir_cmd_t* cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
	cmd->src[0].constant = true;
	cmd->src[0].depend = values;
	cmd->dst.reg.size = num_components - 1;
	cmd->dst.reg.index = this->prog->reg_alloc++;
	lima_pp_hir_block_insert_end(this->cur_block, cmd);
	this->cur_cmd = cmd;
	
	return visit_continue;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_swizzle* ir)
{
	ir->val->accept(this);
	
	lima_pp_hir_cmd_t* mov = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
	mov->src[0].depend = this->cur_cmd;
	mov->src[0].swizzle[0] = ir->mask.x;
	mov->src[0].swizzle[1] = ir->mask.y;
	mov->src[0].swizzle[2] = ir->mask.z;
	mov->src[0].swizzle[3] = ir->mask.w;
	mov->dst.reg.size = ir->mask.num_components - 1;
	mov->dst.reg.index = prog->reg_alloc++;
	lima_pp_hir_block_insert_end(this->cur_block, mov);
	this->cur_cmd = mov;
	
	return visit_continue_with_parent;
}

ir_visitor_status ir_to_pp_hir_visitor::visit(ir_dereference_variable* ir)
{
	if (strcmp(ir->var->name, "gl_FragColor") == 0)
	{
		assert(this->in_assignee);
		this->output_cmd = this->cur_cmd;
		return visit_continue;
	}
	
	if (strcmp(ir->var->name, "gl_FrontFacing") == 0)
	{
		lima_pp_hir_cmd_t* cmd =
			lima_pp_hir_cmd_create(lima_pp_hir_op_front_facing);
		cmd->dst.reg.size = 0;
		cmd->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, cmd);
		this->cur_cmd = cmd;
		
		return visit_continue;
	}
	
	if (strcmp(ir->var->name, "gl_FragCoord") == 0)
	{
		lima_pp_hir_cmd_t* load_cmd =
			lima_pp_hir_cmd_create(lima_pp_hir_op_frag_coord_impl);
		load_cmd->dst.reg.size = 3;
		load_cmd->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, load_cmd);
		
		lima_pp_hir_cmd_t* xyz_cmd;
		if (this->core == lima_core_mali_200)
		{
			lima_symbol_t* scale_sym =
				lima_symbol_table_find(&this->symbols->uniform_table,
									   "gl_mali_FragCoordScale");
			
			lima_pp_hir_cmd_t* scale =
				lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_four);
			scale->load_store_index = scale_sym->offset;
			scale->dst.reg.size = 3;
			scale->dst.reg.index = this->prog->reg_alloc++;
			lima_pp_hir_block_insert_end(this->cur_block, scale);
			
			lima_pp_hir_cmd_t* mul = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
			mul->src[0].depend = load_cmd;
			mul->src[1].depend = scale;
			mul->dst.reg.size = 2;
			mul->dst.reg.index = this->prog->reg_alloc++;
			lima_pp_hir_block_insert_end(this->cur_block, mul);
			
			xyz_cmd = mul;
		}
		else
		{
			lima_pp_hir_cmd_t* mov = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
			mov->src[0].depend = load_cmd;
			mov->dst.reg.size = 2;
			mov->dst.reg.index = this->prog->reg_alloc++;
			lima_pp_hir_block_insert_end(this->cur_block, mov);
			
			xyz_cmd = mov;
		}
		
		lima_pp_hir_cmd_t* rcp = lima_pp_hir_cmd_create(lima_pp_hir_op_rcp);
		rcp->src[0].depend = load_cmd;
		rcp->src[0].swizzle[0] = 3;
		rcp->dst.reg.size = 0;
		rcp->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, rcp);
		
		lima_pp_hir_cmd_t* combine = lima_pp_hir_combine_create(2);
		combine->src[0].depend = xyz_cmd;
		combine->src[1].depend = rcp;
		combine->dst.reg.size = 3;
		combine->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, combine);
		
		this->cur_cmd = combine;
		return visit_continue;
	}
	
	if (strcmp(ir->var->name, "gl_PointCoord") == 0)
	{
		lima_pp_hir_cmd_t* load_cmd =
			lima_pp_hir_cmd_create(lima_pp_hir_op_point_coord_impl);
		load_cmd->dst.reg.size = 1;
		load_cmd->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, load_cmd);
		
		if (this->core == lima_core_mali_400)
		{
			this->cur_cmd = load_cmd;
			return visit_continue;
		}
		
		lima_symbol_t* scale_bias_sym =
		lima_symbol_table_find(&this->symbols->uniform_table,
							   "gl_mali_PointCoordScaleBias");
		
		lima_pp_hir_cmd_t* scale_bias =
			lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_four);
		scale_bias->load_store_index = scale_bias_sym->offset;
		scale_bias->dst.reg.size = 1;
		scale_bias->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, scale_bias);
		
		lima_pp_hir_cmd_t* mul = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
		mul->src[0].depend = load_cmd;
		mul->src[1].depend = scale_bias;
		mul->dst.reg.size = 1;
		mul->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, mul);
		
		lima_pp_hir_cmd_t* add = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
		add->src[0].depend = scale_bias;
		add->src[0].swizzle[0] = 2;
		add->src[0].swizzle[1] = 3;
		add->src[1].depend = mul;
		add->dst.reg.size = 1;
		add->dst.reg.index = this->prog->reg_alloc++;
		lima_pp_hir_block_insert_end(this->cur_block, add);
		
		this->cur_cmd = add;
		return visit_continue;
	}
	
	if (ir->var->data.mode == ir_var_temporary_ssa)
	{
		if (this->in_assignee)
			_mesa_hash_table_insert(this->var_to_cmd,
									_mesa_hash_pointer(ir->var),
									ir->var, this->cur_cmd);
		else
		{
			struct hash_entry* entry =
				_mesa_hash_table_search(this->var_to_cmd,
										_mesa_hash_pointer(ir->var), ir->var);
			this->cur_cmd = (lima_pp_hir_cmd_t*) entry->data;
		}
	}
	else
	{
		this->handle_deref(ir);
	}
	
	return visit_continue;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_dereference_array* ir)
{
	this->handle_deref(ir);
	return visit_continue_with_parent;
}

ir_visitor_status ir_to_pp_hir_visitor::visit_enter(ir_dereference_record* ir)
{
	this->handle_deref(ir);
	return visit_continue_with_parent;
}

void ir_to_pp_hir_visitor::handle_deref(ir_dereference* ir)
{
	lima_pp_hir_cmd_t* value = this->cur_cmd;
	
	unsigned offset = 0, alignment;
	if (ir->type->vector_elements == 1)
		alignment = 1;
	else if (ir->type->vector_elements == 2)
		alignment = 2;
	else
		alignment = 4;
	lima_pp_hir_cmd_t* indirect_offset = NULL;
	lima_symbol_t* symbol = NULL;
	this->calc_deref_offset(&offset, ir, &indirect_offset, &symbol, alignment);
	
	ir_variable* var = ir->variable_referenced();
	
	if (this->in_assignee)
	{
		if (this->base_ir->as_assignment())
		{
			unsigned write_mask = this->base_ir->as_assignment()->write_mask;
			if (write_mask == (1 << ir->type->vector_elements) - 1)
			{
				this->emit_store(value, (ir_variable_mode) var->data.mode,
								 offset, alignment, indirect_offset);
			}
			else
			{
				this->emit_writemask_store(value,
										   (ir_variable_mode) var->data.mode,
										   offset, alignment, indirect_offset,
										   write_mask);
			}
		}
		else
		{
			this->emit_store(value, (ir_variable_mode) var->data.mode, offset,
							 alignment, indirect_offset);
		}
	}
	else
	{
		this->emit_load((ir_variable_mode) var->data.mode, offset, alignment,
						indirect_offset);
	}
}

static lima_symbol_t* get_struct_field(lima_symbol_t* symbol, const char* field)
{
	for (unsigned i = 0; i < symbol->num_children; i++)
		if (strcmp(field, symbol->children[i]->name) == 0)
			return symbol->children[i];
	
	return NULL;
}

void ir_to_pp_hir_visitor::calc_deref_offset(unsigned* offset,
											 ir_dereference* deref,
											 lima_pp_hir_cmd_t** out_indirect,
											 lima_symbol_t** out_symbol,
											 unsigned alignment)
{
	ir_dereference_variable* deref_var = deref->as_dereference_variable();
	ir_dereference_array* deref_array = deref->as_dereference_array();
	ir_dereference_record* deref_record = deref->as_dereference_record();
	
	//base case - variable dereference
	if (deref_var)
	{
		struct hash_entry* entry =
			_mesa_hash_table_search(this->glsl_symbols,
									_mesa_hash_pointer(deref_var->var),
									deref_var->var);
		
		lima_symbol_t* symbol = (lima_symbol_t*) entry->data;
		*out_symbol = symbol;
		*offset += symbol->offset / alignment;
	}
	else if (deref_array)
	{
		this->calc_deref_offset(offset, deref_array->array->as_dereference(),
								out_indirect, out_symbol, alignment);
		
		ir_constant* constant = deref_array->array_index->as_constant();
		if (constant)
		{
			*offset += constant->value.i[0] * (*out_symbol)->stride / alignment;
		}
		else
		{
			bool old_in_assignee = this->in_assignee;
			this->in_assignee = false;
			
			deref_array->array_index->accept(this);
			lima_pp_hir_cmd_t* index = this->cur_cmd;
			
			lima_pp_hir_cmd_t* new_offset;
			if ((*out_symbol)->stride / alignment != 1)
			{
				lima_pp_hir_cmd_t* mul = lima_pp_hir_cmd_create(lima_pp_hir_op_mul);
				mul->dst.reg.size = 1;
				mul->dst.reg.index = prog->reg_alloc++;
				mul->dst.modifier = lima_pp_outmod_round;
				mul->src[0].depend = index;
				mul->src[1].constant = true;
				mul->src[1].depend = malloc(sizeof(float));
				float constant = (float) ((*out_symbol)->stride / alignment);
				memcpy(mul->src[1].depend, &constant, sizeof(float));
				lima_pp_hir_block_insert_end(this->cur_block, mul);
				new_offset = mul;
			}
			else
				new_offset = index;
			
			if (*out_indirect)
			{
				lima_pp_hir_cmd_t* old_indirect = *out_indirect;
				lima_pp_hir_cmd_t* add = lima_pp_hir_cmd_create(lima_pp_hir_op_add);
				add->dst.reg.size = 1;
				add->dst.reg.index = prog->reg_alloc++;
				add->dst.modifier = lima_pp_outmod_round;
				add->src[0].depend = old_indirect;
				add->src[1].depend = new_offset;
				lima_pp_hir_block_insert_end(this->cur_block, add);
				*out_indirect = add;
			}
			else
				*out_indirect = new_offset;
			
			this->in_assignee = old_in_assignee;
		}
	}
	else
	{
		this->calc_deref_offset(offset, deref_record->record->as_dereference(),
								out_indirect, out_symbol, alignment);
		
		lima_symbol_t* field = get_struct_field(*out_symbol, deref_record->field);
		
		*offset += field->offset / alignment;
		*out_symbol = field;
	}
}

void ir_to_pp_hir_visitor::emit_writemask_store(lima_pp_hir_cmd_t* value,
												ir_variable_mode mode,
												unsigned offset,
												unsigned num_components,
												lima_pp_hir_cmd_t* indirect_offset,
												unsigned write_mask)
{
	this->emit_load(mode, offset, num_components, indirect_offset);
	lima_pp_hir_cmd_t* load = this->cur_cmd;
	
	lima_pp_hir_cmd_t* combine = lima_pp_hir_combine_create(num_components);
	combine->dst.reg.size = num_components - 1;
	combine->dst.reg.index = this->prog->reg_alloc++;
	
	unsigned val_component = 0;
	for (unsigned i = 0; i < num_components; i++)
	{
		lima_pp_hir_cmd_t* mov = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
		mov->dst.reg.size = 0;
		mov->dst.reg.index = this->prog->reg_alloc++;
		if (write_mask & (1 << i))
		{
			mov->src[0].depend = value;
			mov->src[0].swizzle[0] = val_component++;
		}
		else
		{
			mov->src[0].depend = load;
			mov->src[0].swizzle[0] = i;
		}
		lima_pp_hir_block_insert_end(this->cur_block, mov);
		combine->src[i].depend = mov;
	}
	
	lima_pp_hir_block_insert_end(this->cur_block, combine);
	
	this->emit_store(combine, mode, offset, num_components, indirect_offset);
}

void ir_to_pp_hir_visitor::emit_load(ir_variable_mode mode, unsigned offset,
									 unsigned num_components,
									 lima_pp_hir_cmd_t* indirect_offset)
{
	lima_pp_hir_cmd_t* cmd = NULL;
	switch (mode)
	{
		case ir_var_uniform:
			if (indirect_offset)
			{
				switch (num_components)
				{
					case 1:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_one_off);
						break;
						
					case 2:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_two_off);
						break;
						
					case 3:
					case 4:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_four_off);
						break;
						
					default:
						assert(0);
				}
				
				cmd->src[0].depend = indirect_offset;
			}
			else
			{
				switch (num_components)
				{
					case 1:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_one);
						break;
						
					case 2:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_two);
						break;
						
					case 3:
					case 4:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadu_four);
						break;
						
					default:
						assert(0);
				}
			}
			break;
			
		case ir_var_temporary:
		case ir_var_auto:
			if (indirect_offset)
			{
				switch (num_components)
				{
					case 1:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadt_one_off);
						break;
						
					case 2:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadt_two_off);
						break;
						
					case 3:
					case 4:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadt_four_off);
						break;
						
					default:
						assert(0);
				}
				
				cmd->src[0].depend = indirect_offset;
			}
			else
			{
				switch (num_components)
				{
					case 1:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadt_one);
						break;
						
					case 2:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadt_two);
						break;
						
					case 3:
					case 4:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadt_four);
						break;
						
					default:
						assert(0);
				}
			}
			break;
			
		case ir_var_shader_in:
			if (indirect_offset)
			{
				switch (num_components)
				{
					case 1:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_one_off);
						break;
						
					case 2:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_two_off);
						break;
						
					case 3:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_three_off);
						break;
						
					case 4:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_four_off);
						break;
						
					default:
						assert(0);
				}
				
				cmd->src[0].depend = indirect_offset;
			}
			else
			{
				switch (num_components)
				{
					case 1:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_one);
						break;
						
					case 2:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_two);
						break;
						
					case 3:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_three);
						break;
						
					case 4:
						cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_loadv_four);
						break;
						
					default:
						assert(0);
				}
			}
			break;
			
		default:
			assert(0);
	}
	
	cmd->load_store_index = offset;
	cmd->dst.reg.size = num_components - 1;
	cmd->dst.reg.index = this->prog->reg_alloc++;
	
	lima_pp_hir_block_insert_end(this->cur_block, cmd);
	
	if ((mode == ir_var_temporary
		 || mode == ir_var_auto
		 || mode == ir_var_uniform)
		&& num_components == 3)
	{
		cmd->dst.reg.size = 3;
		lima_pp_hir_cmd_t* mov = lima_pp_hir_cmd_create(lima_pp_hir_op_mov);
		mov->dst.reg.size = 2;
		mov->dst.reg.index = prog->reg_alloc++;
		mov->src[0].depend = cmd;
		lima_pp_hir_block_insert_end(this->cur_block, mov);
		this->cur_cmd = mov;
	}
	else
	{
		this->cur_cmd = cmd;
	}
}

void ir_to_pp_hir_visitor::emit_store(lima_pp_hir_cmd_t* value,
									  ir_variable_mode mode,
									  unsigned offset,
									  unsigned num_components,
									  lima_pp_hir_cmd_t* indirect_offset)
{
	assert(mode == ir_var_temporary || mode == ir_var_uniform);
	(void) mode;
	
	lima_pp_hir_cmd_t* cmd = NULL;
	
	if (indirect_offset)
	{
		switch (num_components)
		{
			case 1:
				cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_storet_one_off);
				cmd->src[1].depend = indirect_offset;
				break;
				
			case 2:
				cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_storet_two_off);
				cmd->src[1].depend = indirect_offset;
				break;
				
			case 3:
			case 4:
				cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_storet_four_off);
				cmd->src[1].depend = indirect_offset;
				break;
				
			default:
				assert(0);
		}
	}
	else
	{
		switch (num_components)
		{
			case 1:
				cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_storet_one);
				break;
				
			case 2:
				cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_storet_two);
				break;
				
			case 3:
			case 4:
				cmd = lima_pp_hir_cmd_create(lima_pp_hir_op_storet_four);
				break;
				
			default:
				assert(0);
		}
	}
	
	cmd->src[0].depend = value;
	cmd->load_store_index = offset;
	lima_pp_hir_block_insert_end(this->cur_block, cmd);
	this->cur_cmd = cmd;
}
