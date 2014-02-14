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
#include "ir_builder.h"

/**
 * \file opt_from_ssa.cpp
 *
 * This file removes all the SSA temporaries and phi nodes from a program. It
 * immplements Method I of the paper "Translating out of Single Static
 * Assignment Form" by Sreedhar et. al., a naive method that inserts many more
 * copies than necessary; it is assumed that later copy propagation passes will
 * clean up the result of this pass.
 */

using namespace ir_builder;

static ir_variable *
insert_decl(exec_list *instrs, const glsl_type *type, void *mem_ctx)
{
   ir_variable *var = new(mem_ctx) ir_variable(type, "phi_temp",
					       ir_var_temporary);
   instrs->push_head(var);
   return var;
}

static void
eliminate_phi_if(ir_phi_if *phi, ir_if *ir, exec_list *instrs)
{
   ir_variable *var = insert_decl(instrs, phi->dest->type, ralloc_parent(ir));

   /*
    * This converts the destination of the phi node into a non-SSA variable,
    * which ir_from_ssa_visitor::visit(ir_dereference_variable *) would normally
    * do. We need to do this here because the list visitor uses
    * foreach_list_safe(), so it will skip any nodes we insert.
    */

   ir->insert_after(phi->dest);
   phi->dest->insert_after(assign(phi->dest, var));
   phi->dest->data.mode = ir_var_temporary;
   phi->dest->ssa_owner = NULL;

   if (phi->if_src != NULL)
      ir->then_instructions.push_tail(assign(var, phi->if_src));

   if (phi->else_src != NULL)
      ir->else_instructions.push_tail(assign(var, phi->else_src));

   phi->remove();
}

static void
eliminate_phi_loop_begin(ir_phi_loop_begin *phi, ir_loop *ir, exec_list *instrs)
{
   ir_variable *var = insert_decl(instrs, phi->dest->type, ralloc_parent(ir));
   ir->body_instructions.push_head(phi->dest);
   phi->dest->insert_after(assign(phi->dest, var));
   phi->dest->data.mode = ir_var_temporary;
   phi->dest->ssa_owner = NULL;

   if (phi->enter_src != NULL)
      ir->insert_before(assign(var, phi->enter_src));

   if (phi->repeat_src != NULL)
      ir->body_instructions.push_tail(assign(var, phi->repeat_src));

   foreach_list(n, &phi->continue_srcs) {
      ir_phi_jump_src *src = (ir_phi_jump_src *) n;

      if (src->src != NULL)
	 src->jump->insert_before(assign(var, src->src));
   }

   phi->remove();
}

static void
eliminate_phi_loop_end(ir_phi_loop_end *phi, ir_loop *ir, exec_list *instrs)
{
   ir_variable *var = insert_decl(instrs, phi->dest->type, ralloc_parent(ir));
   ir->insert_after(phi->dest);
   phi->dest->insert_after(assign(phi->dest, var));
   phi->dest->data.mode = ir_var_temporary;
   phi->dest->ssa_owner = NULL;

   foreach_list(n, &phi->break_srcs) {
      ir_phi_jump_src *src = (ir_phi_jump_src *) n;

      if (src->src != NULL)
	 src->jump->insert_before(assign(var, src->src));
   }

   phi->remove();
}

namespace {

class ir_from_ssa_visitor : public ir_hierarchical_visitor
{
public:
   ir_from_ssa_visitor(exec_list *base_instrs) : base_instrs(base_instrs)
   {
   }

   virtual ir_visitor_status visit_leave(ir_if *);
   virtual ir_visitor_status visit_enter(ir_loop *);
   virtual ir_visitor_status visit_leave(ir_loop *);
   virtual ir_visitor_status visit_enter(ir_function_signature *);
   virtual ir_visitor_status visit_leave(ir_function_signature *);
   virtual ir_visitor_status visit(ir_dereference_variable *);

private:
   exec_list *base_instrs;
};

};

ir_visitor_status
ir_from_ssa_visitor::visit_leave(ir_if *ir)
{
   foreach_list_safe(n, &ir->phi_nodes) {
      eliminate_phi_if((ir_phi_if *) n, ir, this->base_instrs);
   }

   return visit_continue;
}

ir_visitor_status
ir_from_ssa_visitor::visit_enter(ir_loop *ir)
{
   foreach_list_safe(n, &ir->begin_phi_nodes) {
      eliminate_phi_loop_begin((ir_phi_loop_begin *) n, ir, this->base_instrs);
   }

   return visit_continue;
}

ir_visitor_status
ir_from_ssa_visitor::visit_leave(ir_loop *ir)
{
   foreach_list_safe(n, &ir->end_phi_nodes) {
      eliminate_phi_loop_end((ir_phi_loop_end *) n, ir, this->base_instrs);
   }

   return visit_continue;
}

ir_visitor_status
ir_from_ssa_visitor::visit_enter(ir_function_signature *ir)
{
   this->base_instrs = &ir->body;
   return visit_continue;
}

ir_visitor_status
ir_from_ssa_visitor::visit_leave(ir_function_signature *ir)
{
   (void) ir;
   this->base_instrs = NULL;
   return visit_continue;
}


ir_visitor_status
ir_from_ssa_visitor::visit(ir_dereference_variable *ir)
{
   if (this->in_assignee && ir->var->data.mode == ir_var_temporary_ssa) {
      this->base_ir->insert_before(ir->var);
      ir->var->data.mode = ir_var_temporary;
      ir->var->ssa_owner = NULL;
   }

   return visit_continue;
}

void
convert_from_ssa(exec_list *instructions)
{
   ir_from_ssa_visitor v(instructions);
   v.run(instructions);
}
