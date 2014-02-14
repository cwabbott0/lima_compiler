/*
 * Copyright © 2014 Connor Abbott (connor@abbott.cx)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "ir.h"
#include "ir_optimization.h"
#include "ir_builder.h"
#include "ralloc.h"
#include "glsl_types.h"
#include "main/hash_table.h"
#include "opt_to_ssa.h"

/**
 * \file opt_to_ssa.cpp
 *
 * This pass will convert all temporaries and local variables to SSA
 * temporaries, except for variables which are derefenced as an array or
 * structure (which we cannot support in SSA form). The algorithm is loosely
 * based on "Efficiently Computing Static Single Assignment Form and the
 * Control Dependence Graph" by Cytron et. al., although there are a number of
 * differences caused by the fact that we are operating on a hierachical tree
 * of if's and loops instead of the graph of basic blocks that Cytron et. al.
 * assume. In particular, instead of explicitly constructing the dominance tree,
 * we use an approximation simple enough that all the information we need can
 * be found on the fly. The approximation we use is this:
 *
 * - The instruction before an if statement dominates the then and else branches
 * as well as the instructions after the branch, unless one of the branches is
 * dead. If, for example, the then branch is dead, then the instruction before
 * the if statement dominates the then branch and the else branch, and the else
 * branch dominates the instruction after the if statement because if we get
 * past the branch then we know we must have gone through the else branch.
 *
 * - The instruction before the loop dominates the instructions inside the loop
 * as well as the instructions after the loop. Here is where the approximation
 * lies: really, since the loop is guaranteed to execute at least once, the
 * instructions after the loop can potentially be dominated by an instruction
 * inside the loop. Computing that instruction, though, would be complicated,
 * and in the end it doesn't hurt much if we ignore that detail. In the end, we
 * may insert some phi nodes where all the sources are the same or equivalent,
 * but these can easily be optimized away.
 *
 * In the original algorithm, the iterated dominance frontiers are calculated
 * explicitly, and then phi "trivial" phi nodes of the form V = phi(V, V, ...)
 * are inserted in the iterated dominance frontier of each assignment of each
 * variable. Under our approximation, though, the iterated dominance frontier of
 * a particular instruction can be calculated by walking up the stack of control
 * flow elements (if's and loops) that lead to the instruction. If the
 * instruction must lead to a continue or break statement, then we skip up the
 * stack first until we find the loop that it's breaking out of or continuing
 * into. So in our implementation, we simply traverse the control flow graph,
 * and for each assigment we walk up the stack as described, inserting phi nodes
 * into the join nodes of the if statements and loops that we encounter.
 *
 * Next, we have to run the algorithm described in section 5.2 of the paper,
 * "Renaming." It involves going through the dominator tree in a depth-first
 * manner while maintaining a stack of SSA variables for each original
 * variable where the top of each stack represents the current SSA variable that
 * is replacing the old (non-SSA) variable. When we go forwards, we rewrite
 * dereferences on the RHS to point to the new SSA variables and then for LHS
 * dereferences we create a new SSA variable and push it on the stack. When we
 * go backwards, we pop the variables off the stack that we originally pushed on
 * the stack. Phi nodes require some special handling, since they occur in
 * parallel; when we get to the end of each basic block, we update the sources
 * of the phi nodes in each successor block that correspond to the current
 * block. The LHS of phi nodes are treated just like the LHS of any other
 * assignment, though.
 *
 * Once again, we use our approximation here. For ifs with no dead branches, we
 * visit the then branch forwards and then backwards, then do the same with the
 * else branch, since they are both children of whatever statements became
 * before the if. Then we return, effectively still going forwards, since the
 * statements after the if are dominated by the statements before the if as
 * well. When one of the branches is dead, though, things get a little more
 * complicated. Say, for example, that the branch from the then branch is dead.
 * Then, when we return from the function, still effectively going forwards,
 * we must be at the end of the then branch, since the then branch dominates
 * the instructions after the if. So we visit the else branch forwards and
 * backwards again, then we only visit the then branch forwards and return. When
 * we visit this branch backwards, then, we visit the else branch backwards
 * because we want to go up the dominance tree. Loops are simpler: the
 * instructions before a loop dominate the body instructions of the loop and
 * the instructions after the loop, so we visit the body instructions forwards
 * and backwards and then we return from the function.
 */

using namespace ir_builder;

static void
convert_to_ssa_function(exec_list *instructions)
{
   /*
    * Determine control flow out of ifs: needed for inserting phi nodes and
    * rewriting instructions
    */
   ir_dead_branches_visitor dbv;
   dbv.run(instructions);

   /* Find the breaks and continues associated with each loop: needed for
    * inserting phi nodes
    */
   ir_loop_jumps_visitor ljv;
   ljv.run(instructions);

   /*
    * Create an ir_ssa_variable_state object for each variable that is SSA-able,
    * this will track the variable's state during rewriting. Also, count the
    * number of times each variable is assigned to in its corresponding
    * ir_ssa_variable_state.
    */
   ir_ssa_state_visitor ssv;
   ssv.run(instructions);

   /*
    * Convert out and inout parameters of calls to a form that is easier to
    * convert to SSA. Note that we must update the assignment count that the
    * ir_ssa_state_visitor calculated since we may introduce copies that count
    * as assignments.
    */
   ir_parameter_visitor pv(&ssv);
   pv.run(instructions);

   /*
    * Insert trvial phi nodes as described above. Note that we must update the
    * assignment count that the ir_ssa_state_visitor calculated since phi nodes
    * count as assignments.
    */
   ir_phi_insertion_visitor piv(&ssv, &dbv, &ljv);
   piv.run(instructions);

   /*
    * Allocate a stack of ir_variable *'s inside each ssa_variable_state. Each
    * stack has to be big enough that we don't run out of space when
    * rewriting variables; since we know that we will always push to a stack
    * once for each assignment to its corresponding variable, so we use the
    * assignment count calculated earlier as an upper bound, and allocate an
    * array big enough to hold a pointer for each assigment to the variable.
    * We could use a dynamically growing stack, but this approach is a little
    * simpler, while also being faster due to not having to expand the stack
    * while rewriting.
    */
   ssv.allocate_state_arrays();

   /*
    * Visit our approximation of the dominance tree in a depth-first manner,
    * replacing each SSA-able variable V with SSA variables Vi such that Vi is
    * always assigned exactly once. This includes rewriting the trivial phi
    * functions we inserted earlier so that they are no longer trivial.
    */
   ir_rewrite_visitor rv(&ssv, &dbv);
   rv.rewrite(instructions);

   /*
    * Remove the declaration of each variable V now that it has been replaced
    * by Vi's.
    */
   ssv.remove_decls();
}

void
convert_to_ssa(exec_list *instructions)
{
   foreach_list(node, instructions) {
      ir_instruction *ir = (ir_instruction *) node;
      ir_function *f = ir->as_function();
      if (f) {
	 foreach_list(sig_node, &f->signatures) {
	    ir_function_signature *sig = (ir_function_signature *) sig_node;

	    convert_to_ssa_function(&sig->body);
	 }
      }
   }
}


/*
 * ir_ssa_variable_state
 */

ir_ssa_variable_state::ir_ssa_variable_state(ir_variable* var,
					     ir_ssa_state_visitor *v,
					     ir_variable *undefined_var)
   : var(var), stack(NULL), num_replaced(0), num_defs(0), stack_idx(-1), v(v),
   undefined_var(undefined_var)
{
}

ir_ssa_variable_state::~ir_ssa_variable_state()
{
   assert(this->stack_idx == -1);
   free(this->stack);
}

ir_variable *
ir_ssa_variable_state::cur_var(bool use_undefined_var)
{
   if (this->stack_idx == -1) {
      if (use_undefined_var) {
	 return this->undefined_var;
      } else {
	 return NULL;
      }
   }

   return this->stack[this->stack_idx];
}

void
ir_ssa_variable_state::stack_push(ir_variable *new_var)
{
   this->stack_idx++;
   assert(this->stack_idx < this->num_defs);
   this->stack[this->stack_idx] = new_var;
   _mesa_hash_table_insert(this->v->new_to_old, _mesa_hash_pointer(new_var),
			   new_var, this->var);
}

void
ir_ssa_variable_state::stack_pop()
{
   assert(this->stack_idx != -1);
   ir_variable *var = this->stack[this->stack_idx];
   struct hash_entry *entry = _mesa_hash_table_search(this->v->new_to_old,
						      _mesa_hash_pointer(var),
						      var);
   _mesa_hash_table_remove(this->v->new_to_old, entry);
   this->stack_idx--;
}

ir_variable *
ir_ssa_variable_state::new_var()
{
   void *mem_ctx = ralloc_parent(this->var);
   char *new_name = ralloc_asprintf(mem_ctx, "%s_%i", this->var->name,
				    this->num_replaced);
   ir_variable *new_var = new(mem_ctx) ir_variable(this->var->type, new_name,
						   ir_var_temporary_ssa);
   this->num_replaced++;
   assert(this->num_replaced <= this->num_defs);
   this->stack_push(new_var);
   return new_var;
}

/*
 * ir_ssa_state_visitor
 */

static void
free_state(struct hash_entry *entry)
{
   ir_ssa_variable_state *isvs = (ir_ssa_variable_state *) entry->data;
   delete isvs;
}

ir_ssa_state_visitor::ir_ssa_state_visitor()
{
   this->ht = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
   this->new_to_old = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
}

ir_ssa_state_visitor::~ir_ssa_state_visitor(void)
{
   _mesa_hash_table_destroy(this->ht, free_state);
   _mesa_hash_table_destroy(this->new_to_old, NULL);
}

ir_visitor_status
ir_ssa_state_visitor::visit(ir_variable *var)
{
   if (var->data.mode == ir_var_auto || var->data.mode == ir_var_temporary) {
      void *mem_ctx = ralloc_parent(var);
      ir_assignment *assign = ssa_assign("undefined",
					 ir_constant::zero(mem_ctx, var->type));
      ir_variable *undefined_var = assign->lhs->as_dereference_variable()->var;
      var->insert_after(assign);
      ir_ssa_variable_state *entry = new ir_ssa_variable_state(var, this,
							       undefined_var);
      _mesa_hash_table_insert(this->ht, _mesa_hash_pointer(var), var, entry);
   }

   return visit_continue;
}

/*
 * We currently have no way to convert variables referenced as records and
 * arrays to SSA form, so don't track them.
 */

ir_visitor_status
ir_ssa_state_visitor::visit_enter(ir_dereference_record *deref)
{
   const ir_variable *var = deref->variable_referenced();

   if (var) {
      remove_variable(var);
   }

   return visit_continue;
}

ir_visitor_status
ir_ssa_state_visitor::visit_enter(ir_dereference_array *deref)
{
   const ir_variable *var = deref->variable_referenced();

   if (var) {
      remove_variable(var);
   }

   return visit_continue;
}

ir_visitor_status
ir_ssa_state_visitor::visit(ir_dereference_variable *deref)
{
   const ir_variable *var = deref->variable_referenced();

   if (var && this->in_assignee) {
      ir_ssa_variable_state *isvs =  this->get_state(var);
      if (isvs) {
	 isvs->num_defs++;
      }
   }

   return visit_continue;
}

ir_ssa_variable_state *
ir_ssa_state_visitor::get_state(const ir_variable *var)
{
   hash_entry *entry = _mesa_hash_table_search(this->ht, _mesa_hash_pointer(var), var);

   if (entry) {
      return (ir_ssa_variable_state *) entry->data;
   }

   return NULL;
}

ir_ssa_variable_state *
ir_ssa_state_visitor::get_state_ssa(const ir_variable* var)
{
   if (var->data.mode != ir_var_temporary_ssa) {
      return NULL;
   }

   hash_entry *entry = _mesa_hash_table_search(this->new_to_old,
					       _mesa_hash_pointer(var), var);

   if (!entry) {
      /*
       * some SSA variables created (i.e. wrmask_temp) don't correspond to a
       * non-SSA variable, so we need to return NULL here
       */

      return NULL;
   }

   return this->get_state((const ir_variable *) entry->data);
}

void
ir_ssa_state_visitor::allocate_state_arrays()
{
   struct hash_entry *entry;

   hash_table_foreach(this->ht, entry) {
      ir_ssa_variable_state *isvs = (ir_ssa_variable_state *) entry->data;
      isvs->stack = (ir_variable**) malloc(sizeof(ir_variable *) * isvs->num_defs);
   }
}

/**
 * Remove the (now unused) variable declarations
 */

void
ir_ssa_state_visitor::remove_decls()
{
   struct hash_entry *entry;
   hash_table_foreach(this->ht, entry) {
      ir_variable *var = (ir_variable *) entry->key;
      var->remove();
   }
}


void
ir_ssa_state_visitor::remove_variable(const ir_variable *var)
{
   struct hash_entry *entry =
      _mesa_hash_table_search(this->ht, _mesa_hash_pointer(var), var);

   if (entry) {
      free_state(entry);
      _mesa_hash_table_remove(this->ht, entry);
   }
}

/*
 * ir_parameter_visitor
 */

ir_visitor_status
ir_parameter_visitor::visit_enter(ir_call *ir)
{
   void *mem_ctx = ralloc_parent(ir);

   ir_function_signature * callee = ir->callee;
   exec_node *formal_param_node = callee->parameters.head;
   exec_node *actual_param_node = ir->actual_parameters.head;

   while (!formal_param_node->is_tail_sentinel()) {
      ir_variable *formal_param
	 = (ir_variable *) formal_param_node;
      ir_rvalue *actual_param
	 = (ir_rvalue *) actual_param_node;

      /*
       * actual_param may get repurposed here, going from function parameter to
       * rhs of an assignment, and so we need to save a pointer to the next
       * actual parameter before the pointer in actual_param_node gets
       * destroyed.
       */

      exec_node *actual_param_next = actual_param_node->next;

      if (formal_param->data.mode == ir_var_function_out
          || formal_param->data.mode == ir_var_function_inout) {
         ir_variable *actual_param_var = actual_param->variable_referenced();
	 ir_ssa_variable_state *isvs = this->ssv->get_state(actual_param_var);
	 if (isvs != NULL) {
	    ir_variable *tmp = new(mem_ctx) ir_variable(actual_param_var->type,
							"function_temp",
							ir_var_temporary);

	    ir->insert_before(tmp);
	    if (formal_param->data.mode == ir_var_function_inout) {
	       ir_rvalue *actual_param_copy = actual_param->clone(mem_ctx, NULL);
	       ir->insert_before(assign(tmp, actual_param_copy));
	    }

	    ir_dereference_variable *deref
	       = new(mem_ctx) ir_dereference_variable(tmp);
	    actual_param_node->insert_before(deref);
	    actual_param_node->remove();

	    deref = new(mem_ctx) ir_dereference_variable(tmp);
	    ir_assignment *assign = new(mem_ctx) ir_assignment(actual_param, deref);
	    ir->insert_after(assign);
	    isvs->num_defs++;
	 }
      }

      formal_param_node = formal_param_node->next;
      actual_param_node = actual_param_next;
   }

   return visit_continue_with_parent;
}

/*
 * ir_phi_insertion_visitor
 */

ir_visitor_status
ir_phi_insertion_visitor::visit_enter(ir_if *ir)
{
   /*
    * before doing anything, visit the condition, since it's really part of
    * the block before the if
    */
   ir->condition->accept(this);

   ir_if_entry entry(ir, this->cf_stack_head);

   this->cf_stack_head = &entry; /* push onto CF stack */
   entry.in_then = true;
   visit_list_elements(this, &ir->then_instructions);
   entry.in_then = false;
   visit_list_elements(this, &ir->else_instructions);
   this->cf_stack_head = entry.next; /* pop CF stack */

   return visit_continue_with_parent;
}

ir_visitor_status
ir_phi_insertion_visitor::visit_enter(ir_loop *ir)
{
   ir_loop_entry entry(ir, this->cf_stack_head);

   this->cf_stack_head = &entry; /* push onto CF stack */
   visit_list_elements(this, &ir->body_instructions);
   this->cf_stack_head = entry.next; /* pop CF stack */

   return visit_continue_with_parent;
}

ir_visitor_status
ir_phi_insertion_visitor::visit(ir_dereference_variable *ir)
{
   if (!this->in_assignee || !this->ssv->get_state(ir->var))
      return visit_continue;

   /*
    * walk the stack of control flow elements, placing phi nodes as
    * necessary
    */

   for(ir_control_flow_entry *cf_entry = this->cf_stack_head; cf_entry != NULL;
       cf_entry = cf_entry->next) {
      ir_if_entry *if_entry = cf_entry->as_if_entry();
      if (if_entry) {
	 ir_dead_branches *db = this->dbv->get_dead_branches(if_entry->ir);
	 if (if_entry->in_then ? db->then_dead : db->else_dead) {
	    if (if_entry->in_then ? db->then_dead_return
				  : db->else_dead_return) {
	       /*
		* The branch we're on leads to a return or discard, so the
	        * assignment can't lead to any join nodes.
		*/
	       return visit_continue;
	    }

	    /*
	     * The branch we're on leads to a break or continue.
	     * We may need a phi node at the beginning, end, or both of the
	     * innermost loop, depending on if we exit through a continue,
	     * break, or both, repsectively. We use another approximation here,
	     * and simply add a phi node to the beginning and end. Again, the
	     * worst thing that can happen is that we wind up with a phi node
	     * where all the sources are the same or equivalent, which can be
	     * easily optimized away in a later pass. So, for example, something
	     * like:
	     *
	     * x = 1;
	     * loop {
	     *    if (...) {
	     *       x = x + 1;
	     *       break;
	     *    } else {
	     *       x = x + 2;
	     *    }
	     * }
	     * foo(x);
	     *
	     * will get transformed into:
	     *
	     * x_1 = 1;
	     * loop {
	     *    x_2 = phi(x_1, x_4);
	     *    if (...) {
	     *       x_3 = x_2 + 1;
	     *    } else {
	     *       x_4 = x_2 + 2;
	     *    }
	     * }
	     * x_5 = phi(x_3); //unnecessary phi node
	     */

	    /*
	     * Find the innermost nested loop. We can only reach this code if
	     * the branch we're currently visiting of the if we're currently
	     * visiting leads to a break or continue, hence we are in a loop,
	     * so there must be a loop in the control flow stack; therefore, we
	     * can never walk off the end of the list.
	     */
	    ir_loop_entry *loop_entry;
	    do {
	       cf_entry = cf_entry->next;
	       loop_entry = cf_entry->as_loop_entry();
	    } while (loop_entry == NULL);

	    if (!this->add_phi(loop_entry->loop, ir->var)) {
	       /*
		* Here we've found a duplicate phi node, i.e. a trivial phi node
		* for this variable has already been inserted. If this is the
		* case, then when we inserted the phi node previously, the
		* control flow stack after this point was the same as it is now.
		* Hence, any phi nodes we insert from here on out will be a
		* duplicate, so we can just bail out early. The same logic
		* holds for the next two places we call add_phi().
		*/
	       return visit_continue;
	    }
	 } else {
	    if (!this->add_phi(if_entry->ir, ir->var)) {
	       return visit_continue;
	    }
	 }
      } else {
	 ir_loop_entry *loop_entry = cf_entry->as_loop_entry();
	 if (!this->add_phi(loop_entry->loop, ir->var)) {
	    return visit_continue;
	 }
      }
   }

   return visit_continue;
}

static bool
phi_exists(exec_list list, ir_variable *dest)
{
   foreach_list(n, &list) {
      ir_phi *phi = (ir_phi *) n;
      if (phi->dest == dest) {
	 return true;
      }
   }

   return false;
}

bool
ir_phi_insertion_visitor::add_phi(ir_if *ir, ir_variable *var)
{
   void *mem_ctx = ralloc_parent(ir);

   /* don't duplicate phi nodes */
   if (phi_exists(ir->phi_nodes, var)) {
      return false;
   }

   /*
    * Create a trivial phi node where the sources and destination are all the
    * same. Later, ir_rewrite_visitor will replace each variable with the
    * appropriate SSA temporary.
    */
   ir_phi_if *phi = new (mem_ctx) ir_phi_if(var, var, var);
   ir->phi_nodes.push_tail(phi);

   /* make sure to update the assignment count */
   ir_ssa_variable_state *isvs = this->ssv->get_state(var);
   isvs->num_defs++;

   return true;
}

bool
ir_phi_insertion_visitor::add_phi(ir_loop *loop, ir_variable *var)
{
   void *mem_ctx = ralloc_parent(loop);

   /* don't duplicate phi nodes */
   if (phi_exists(loop->begin_phi_nodes, var)) {
      return false;
   }

   ir_loop_jumps *loop_jumps = this->ljv->get_loop_jumps(loop);

   /*
    * Create a trivial phi node where the sources and destination are all the
    * same. Later, ir_rewrite_visitor will replace each variable with the
    * appropriate SSA temporary.
    */
   ir_phi_loop_begin *phi_begin = new(mem_ctx) ir_phi_loop_begin(var, var, var);

   foreach_list(n, &loop_jumps->continues) {
      ir_loop_jump_entry *entry = (ir_loop_jump_entry *) n;

      ir_phi_jump_src *src = new(mem_ctx) ir_phi_jump_src();
      src->jump = entry->ir;
      src->src = var;

      phi_begin->continue_srcs.push_tail(src);
   }

   loop->begin_phi_nodes.push_tail(phi_begin);

   /*
    * Create a trivial phi node where the sources and destination are all the
    * same. Later, ir_rewrite_visitor will replace each variable with the
    * appropriate SSA temporary.
    */
   ir_phi_loop_end *phi_end = new(mem_ctx) ir_phi_loop_end(var);

   foreach_list(n, &loop_jumps->breaks) {
      ir_loop_jump_entry *entry = (ir_loop_jump_entry *) n;

      ir_phi_jump_src *src = new(mem_ctx) ir_phi_jump_src();
      src->jump = entry->ir;
      src->src = var;

      phi_end->break_srcs.push_tail(src);
   }

   loop->end_phi_nodes.push_tail(phi_end);

   /*
    * make sure to update the assigment count (2 since we've inserted 2 phi
    * nodes)
    */
   ir_ssa_variable_state *isvs = this->ssv->get_state(var);
   isvs->num_defs += 2;

   return true;
}

/*
 * variable renaming
 */

/*
 * ir_rewrite_forward_visitor and ir_rewrite_backward_visitor
 */

ir_visitor_status
ir_rewrite_forward_visitor::visit_enter(ir_assignment *ir)
{
   /* visit the rhs first, since variables are read before they are written */
   ir->rhs->accept(this);

   ir_dereference_variable *deref = ir->lhs->as_dereference_variable();
   if (!deref) {
      /*
       * We are dereferencing an array or structure, which we cannot handle, but
       * there might still be variables referenced as indexes, which we need to
       * convert in the same manner we would convert the rhs
       */
      ir->lhs->accept(this);
      return visit_continue_with_parent;
   }

   ir_variable *var = deref->var;
   ir_ssa_variable_state *isvs = this->ssv->get_state(var);
   if (!isvs) {
      return visit_continue_with_parent;
   }

   void *mem_ctx = ralloc_parent(var);

   /* handle writemask by lowering to quadop_vector */
   if (var->type->is_vector()
       && ir->write_mask != (1 << var->type->vector_elements) - 1) {
      ir_assignment *temp_assign = ssa_assign("wrmask_temp", ir->rhs);
      ir_variable *temp = temp_assign->whole_variable_written();
      this->base_ir->insert_before(temp_assign);

      ir_rvalue *inputs[4];
      int i, j = 0;

      for (i = 0; i < var->type->vector_elements; i++) {
	 if (ir->write_mask & (1 << i)) {
	    inputs[i] = swizzle_component(temp, j++);
	 } else {
	    inputs[i] = swizzle_component(isvs->cur_var(true), i);
	 }
      }
      for (; i < 4; i++) {
	 inputs[i] = NULL;
      }

      ir->rhs = new(mem_ctx) ir_expression(ir_quadop_vector, var->type,
					   inputs[0], inputs[1], inputs[2],
					   inputs[3]);

      ir->write_mask = (1 << var->type->vector_elements) - 1;
   }

   /* handle conditional assignment by replacing with a conditional select */
   if (ir->condition && !ir->condition->is_one()) {
      ir->condition->accept(this);
      ir_variable *old_var = isvs->cur_var(true);
      ir->rhs = csel(ir->condition, ir->rhs, old_var);
      ir->condition = NULL;
   }

   ir_variable *new_var = isvs->new_var();
   new_var->ssa_owner = ir;

   deref->var = new_var;

   return visit_continue_with_parent;
}

/*
 * Since ir_rewrite_forward_visitor::visit_enter(ir_assignment *) did a
 * new_var(), we need to do a stack_pop() to undo it
 */

ir_visitor_status
ir_rewrite_backward_visitor::visit_enter(ir_assignment *ir)
{
   ir_dereference_variable *deref = ir->lhs->as_dereference_variable();
   if (deref) {
      ir_variable *var = deref->var;
      ir_ssa_variable_state *isvs = this->ssv->get_state_ssa(var);
      if (isvs) {
	 isvs->stack_pop();
      }
   }

   return visit_continue_with_parent;
}

ir_visitor_status
ir_rewrite_forward_visitor::visit_enter(ir_call *ir)
{
   visit_list_elements(this, &ir->actual_parameters, false);

   if (ir->return_deref != NULL) {
      ir_dereference_variable *deref =
	 ir->return_deref->as_dereference_variable();

      if (!deref) {
	 ir->return_deref->accept(this);
	 return visit_continue_with_parent;
      }

      ir_variable *var = deref->var;
      ir_ssa_variable_state *isvs = this->ssv->get_state(var);
      if (!isvs) {
	 return visit_continue_with_parent;
      }

      ir_variable *new_var = isvs->new_var();
      new_var->ssa_owner = ir;
      deref->var = new_var;
   }

   return visit_continue_with_parent;
}

/*
 * Since ir_rewrite_forward_visitor::visit_enter(ir_call *) did a
 * new_var(), we need to do a stack_pop() to undo it
 */

ir_visitor_status
ir_rewrite_backward_visitor::visit_enter(ir_call *ir)
{
   if (ir->return_deref != NULL) {
      ir_dereference_variable *deref =
	 ir->return_deref->as_dereference_variable();
      if (deref) {
	 ir_variable *var = deref->var;
	 ir_ssa_variable_state *isvs = this->ssv->get_state_ssa(var);
	 if (isvs) {
	    isvs->stack_pop();
	 }
      }
   }

   return visit_continue_with_parent;
}

ir_visitor_status
ir_rewrite_forward_visitor::visit(ir_dereference_variable *ir)
{
   ir_ssa_variable_state *isvs = this->ssv->get_state(ir->var);
   if (isvs) {
      ir->var = isvs->cur_var(true);
   }
   return visit_continue;
}

/*
 * ir_rewrite_visitor
 *
 * The job of this class is to visit our approximation of the dominance tree in
 * a depth-first manner, calling ir_rewrite_forward_visitor when descending into
 * the tree and ir_rewrite_backward_visitor when going up the tree. We also
 * update the sources of phi nodes at appropriate times.
 */

ir_rewrite_visitor::ir_rewrite_visitor(ir_ssa_state_visitor *ssv,
				       ir_dead_branches_visitor* dbv)
   : ssv(ssv), dbv(dbv), rfv(ssv), rbv(ssv)
{
   outer_loop = NULL;
}

void
ir_rewrite_visitor::rewrite(exec_list *instructions)
{
   this->rewrite_forwards(instructions);
   this->rewrite_backwards(instructions);
}

/*
 * Rewrite a list of instructions going forwards. Note that after each
 * instruction, we assume that we are now in a position to visit the rest of the
 * instruction stream forwards, so when visiting an instruction (for example,
 * an if or loop) forwards, we must be careful to return in a state where we've
 * just visited whatever instruction dominates whatever comes after the
 * instruction; so, if we're visiting an instruction A, and another instruction
 * B inside of A dominates all the instructions after A, then we must return
 * having just visited B forwards.
 */
void
ir_rewrite_visitor::rewrite_forwards(exec_list *instructions)
{
   foreach_list(n, instructions) {
      ir_instruction *ir = (ir_instruction *) n;

      switch (ir->ir_type) {
	 case ir_type_if:
	    this->rewrite_forwards(ir->as_if());
	    break;

	 case ir_type_loop:
	    this->rewrite_forwards(ir->as_loop());
	    break;

	 case ir_type_loop_jump:
	    this->rewrite_forwards(ir->as_loop_jump());
	    break;

	 case ir_type_variable:
	    break;

	 default:
	    /*
	     * ir_rewrite_forward_visitor needs to know this to in order to
	     * know where to insert the writemask temp in visit(ir_assignment *)
	     */
	    this->rfv.base_ir = ir;
	    ir->accept(&this->rfv);
	    break;
      }
   }
}

/*
 * Visit a list of instructions backwards, undoing the effect of
 * rewrite_forwards(exec_list *) above. Note that when rewriting each
 * instruction, we must end up with the same state as before we visited that
 * instruction; so, if we're visiting an instruction A, and another instruction
 * B inside of A dominates all the instructions after A, then we must go
 * backwards from B through to the beginning of A.
 */

void
ir_rewrite_visitor::rewrite_backwards(exec_list *instructions)
{
   foreach_list_reverse(n, instructions) {
      ir_instruction *ir = (ir_instruction *) n;

      switch (ir->ir_type) {
	 case ir_type_if:
	    this->rewrite_backwards(ir->as_if());
	    break;

	 case ir_type_loop:
	    this->rewrite_backwards(ir->as_loop());
	    break;

	 default:
	    ir->accept(&this->rbv);
	    break;
      }
   }
}

/*
 * Visit an if statement forwards
 */

void
ir_rewrite_visitor::rewrite_forwards(ir_if *ir)
{
   ir->condition->accept(&this->rfv);

   ir_dead_branches *dead_branches = this->dbv->get_dead_branches(ir);
   if (dead_branches->then_dead) {
      if (dead_branches->else_dead) {
	 /*
	  * Both if and else branches are dead. We don't care too much about
	  * what we do with the instructions after the if, since they are
	  * unreachable, so just visit both branches forwards and backwards.
	  */
	 this->rewrite(&ir->then_instructions);
	 this->rewrite(&ir->else_instructions);
      } else {
	 /*
	  * The then branch is dead but not the else branch is not. The
	  * instructions after the if are dominated by the else branch, so
	  * make sure that when we return we've just visited the else branch
	  * forwards. We don't have to deal with phi nodes here because the
	  * phi insertion visitor was careful not to insert them after this if.
	  */
	 this->rewrite(&ir->then_instructions);
	 this->rewrite_forwards(&ir->else_instructions);
      }
   } else if (dead_branches->else_dead) {
      /*
       * This is the same as the case before, except now the else branch is
       * dead but the then branch is not so things are flip-flopped.
       */
      this->rewrite(&ir->else_instructions);
      this->rewrite_forwards(&ir->then_instructions);
   } else {
      /*
       * Neither  branch is dead. We have to visit both branches forwards and
       * backwards, because the instructions after the if are dominated by the
       * instructions before the if.
       */
      this->rewrite_forwards(&ir->then_instructions);

      /*
       * We've reached the end of the then branch. The successor to the then
       * branch is the block after the if, and the phi nodes in that block are
       * the phi nodes of the if. So, we are at the appropriate time to update
       * the if sources of the phi nodes associated with this if statement.
       */
      foreach_list(n, &ir->phi_nodes) {
	 ir_phi_if *phi = (ir_phi_if *) n;
	 ir_ssa_variable_state *isvs;

	 isvs = this->ssv->get_state(phi->if_src);
	 phi->if_src = isvs->cur_var(false);
      }

      this->rewrite_backwards(&ir->then_instructions);

      this->rewrite_forwards(&ir->else_instructions);

      /*
       * This is the same as before with the phi nodes, except now we update the
       * else sources.
       */
      foreach_list(n, &ir->phi_nodes) {
	 ir_phi_if *phi = (ir_phi_if *) n;
	 ir_ssa_variable_state *isvs;

	 isvs = this->ssv->get_state(phi->else_src);
	 phi->else_src = isvs->cur_var(false);
      }

      this->rewrite_backwards(&ir->else_instructions);

      /*
       * After visiting the if, we rewrite the destination of the phi nodes
       * after just like with any other assignment.
       */
      foreach_list(n, &ir->phi_nodes) {
	 ir_phi_if *phi = (ir_phi_if *) n;

	 this->rewrite_phi_dest(phi);
      }
   }
}

/*
 * rewrite an if statement backwards, undoing the effect of rewriting it
 * forwards
 */

void
ir_rewrite_visitor::rewrite_backwards(ir_if *ir)
{
   ir_dead_branches *dead_branches = this->dbv->get_dead_branches(ir);
   if (dead_branches->then_dead) {
      if (!dead_branches->else_dead) {
	 /*
	  * as explained earlier, we need to go from the instruction which
	  * dominates the instructions after the if (in this case, the last
	  * instruction of the else branch) to the beginning of the branch.
	  * Also, this matches the rewrite_forwards() call in the matching block
	  * in rewrite_forwards(ir_if *).
	  */
	 this->rewrite_backwards(&ir->else_instructions);
      }
   } else if (dead_branches->else_dead) {
      /* Similar logic as the case above, except now the else branch is dead */
      this->rewrite_backwards(&ir->then_instructions);
   } else {
      /* undo rewriting of the phi node destinations in rewrite_forwards() */
      foreach_list(n, &ir->phi_nodes) {
	 ir_phi_if *phi = (ir_phi_if *) n;
	 ir_ssa_variable_state *isvs = this->ssv->get_state_ssa(phi->dest);
	 isvs->stack_pop();
      }
   }
}

/*
 * Rewrite a loop forwards. The body instructions of the loop and the code after
 * the loop are dominated by the code before the loop (not really, but this is
 * our approximation...), so we need to visit the body instructions forwards
 * and then backwards, and then continue visiting the rest of the instructions
 * forwards.
 */

void
ir_rewrite_visitor::rewrite_forwards(ir_loop *ir)
{
   ir_loop *old_outer_loop = this->outer_loop;
   this->outer_loop = ir;

   /*
    * the successor block of the code before the loop is the loop entry, and the
    * phi nodes there are the loop begin phi nodes, so now is the right time to
    * update the enter sources for these phi nodes.
    */

   foreach_list(n, &ir->begin_phi_nodes) {
      ir_phi_loop_begin *phi = (ir_phi_loop_begin *) n;
      ir_ssa_variable_state *isvs;

      isvs = this->ssv->get_state(phi->enter_src);
      phi->enter_src = isvs->cur_var(false);
   }

   /*
    * Rewrite the code inside the loop forwards: the destinations of beginning
    * phi nodes are handled just like any other write.
    */

   foreach_list(n, &ir->begin_phi_nodes) {
      ir_phi_loop_begin *phi = (ir_phi_loop_begin *) n;

      this->rewrite_phi_dest(phi);
   }

   this->rewrite_forwards(&ir->body_instructions);

   /*
    * The successor block of the code at the end of the loop is also the loop
    * entry, so now is the right time to update the repeat sources for these
    * phi nodes.
    */

   foreach_list(n, &ir->begin_phi_nodes) {
      ir_phi_loop_begin *phi = (ir_phi_loop_begin *) n;
      ir_ssa_variable_state *isvs;

      isvs = this->ssv->get_state(phi->repeat_src);
      phi->repeat_src = isvs->cur_var(false);
   }

   /*
    * Rewrite the code inside the loop backwards: once again, the destinations
    * of phi nodes are treated just like any other write.
    */

   this->rewrite_backwards(&ir->body_instructions);

   foreach_list(n, &ir->begin_phi_nodes) {
      ir_phi_loop_begin *phi = (ir_phi_loop_begin *) n;
      ir_ssa_variable_state *isvs = this->ssv->get_state_ssa(phi->dest);
      isvs->stack_pop();
   }

   /*
    * Now that we've visited the loop forwards and backwards, we can start
    * visiting code after the loop forwards. The first thing we need to visit
    * forwards and rewrite are phi nodes after the loop, so we rewrite them
    * here before returning.
    */

   foreach_list(n, &ir->end_phi_nodes) {
      ir_phi_loop_end *phi = (ir_phi_loop_end *) n;

      this->rewrite_phi_dest(phi);
   }

   this->outer_loop = old_outer_loop;
}

/*
 * Rewrite a loop backwards, undoing the effect of rewrite_forwards(ir_loop *).
 */

void
ir_rewrite_visitor::rewrite_backwards(ir_loop *ir)
{
   /*
    * undo the part of rewrite_forwards() where we rewrote the end phi node
    * destinations...
    */
   foreach_list(n, &ir->end_phi_nodes) {
      ir_phi_loop_end *phi = (ir_phi_loop_end *) n;
      ir_ssa_variable_state *isvs = this->ssv->get_state_ssa(phi->dest);
      isvs->stack_pop();
   }
}

void
ir_rewrite_visitor::rewrite_forwards(ir_loop_jump *ir)
{
   switch (ir->mode) {
      case ir_loop_jump::jump_break:
	 /*
	  * The successor block of a break is the code after the innermost
	  * loop, which is where the end phi nodes for that loop are, so now is
	  * the right time to update the sources of the phi nodes there that
	  * correspond to this break.
	  */

	 foreach_list(node, &this->outer_loop->end_phi_nodes) {
	    ir_phi_loop_end *phi = (ir_phi_loop_end *) node;
	    foreach_list(src_node, &phi->break_srcs) {
	       ir_phi_jump_src *src = (ir_phi_jump_src *) src_node;
	       if (src->jump == ir) {
		  ir_ssa_variable_state *isvs = this->ssv->get_state(src->src);
		  src->src = isvs->cur_var(false);
		  break;
	       }
	    }
	 }
	 break;

      case ir_loop_jump::jump_continue:
	 /*
	  * The successor block of a continue is the beginning of the innermost
	  * loop, which is where the beginning phi nodes for that loop are, so
	  * now is the right time to update the sources of the phi nodes there
	  * that correspond to this continue.
	  */

	 foreach_list(node, &this->outer_loop->begin_phi_nodes) {
	    ir_phi_loop_begin *phi = (ir_phi_loop_begin *) node;
	    foreach_list(src_node, &phi->continue_srcs) {
	       ir_phi_jump_src *src = (ir_phi_jump_src *) src_node;
	       if (src->jump == ir) {
		  ir_ssa_variable_state *isvs = this->ssv->get_state(src->src);
		  src->src = isvs->cur_var(false);
		  break;
	       }
	    }
	 }
	 break;

      default:
	 assert(0);
	 break;
   }
}

void
ir_rewrite_visitor::rewrite_phi_dest(ir_phi *ir)
{
   ir_ssa_variable_state *isvs = this->ssv->get_state(ir->dest);
   ir_variable *new_var = isvs->new_var();
   new_var->ssa_owner = ir;
   ir->dest = new_var;
}
