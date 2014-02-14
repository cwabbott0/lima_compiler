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
#include "ir_hierarchical_visitor.h"
#include "ir_dead_branches.h"
#include "ir_loop_jumps.h"

/**
 * \file opt_to_ssa.h
 *
 * This file provides the definitions used internally by opt_to_ssa.cpp.
 */

class ir_ssa_state_visitor;

/**
 * \class ir_ssa_variable_state
 *
 * This class is used to store the state needed for each non-SSA variable during
 * the rewriting phase of the algorithm, and provides helpers to modify that
 * state.
 */
class ir_ssa_variable_state
{
public:
   ir_ssa_variable_state(ir_variable *var, ir_ssa_state_visitor *v,
			 ir_variable *undefined_var);
   ~ir_ssa_variable_state();

   ir_variable *var; /** < The original variable. */

   void stack_push(ir_variable *new_var);

   void stack_pop();

   /**
    * Get the current variable on the top of the stack of SSA replacements.
    *
    * \param use_undefined_var
    * This controls what happens when we try to get the current variable for a
    * variable that has not yet been assigned (when there are no variables on the
    * stack). For phi nodes, we want to return NULL, since phi nodes understand
    * NULL to mean "undefined." However, for normal assignments, we can't do this,
    * so we have to return a special "undefined" variable that we created earlier.
    */
   ir_variable *cur_var(bool use_undefined_var);

   ir_variable *new_var();

   ir_variable **stack; /** < The stack of replacements for the variable. */
   int num_replaced;
   int num_defs; /** < the number of times the variable is assigned */
   int stack_idx; /** < the current index into the stack */

   ir_ssa_state_visitor *v;

   ir_variable *undefined_var; /** < Used for when var is read before written. */
};

/*
 * sets up a hash table of ir_ssa_variable_state for the main phase of the
 * algorithm
 */

class ir_ssa_state_visitor : public ir_hierarchical_visitor {
public:
   ir_ssa_state_visitor();
   ~ir_ssa_state_visitor(void);

   virtual ir_visitor_status visit(ir_variable *);
   virtual ir_visitor_status visit_enter(ir_dereference_record *);
   virtual ir_visitor_status visit_enter(ir_dereference_array *);
   virtual ir_visitor_status visit(ir_dereference_variable *);

   /**
    * Get the ir_ssa_variable_state corresponding to the original (non-SSA)
    * variable
    */
   ir_ssa_variable_state *get_state(const ir_variable *var);

   /**
    * Get the ir_ssa_variable_state corresponding to the new (SSA)
    * variable
    */
   ir_ssa_variable_state *get_state_ssa(const ir_variable *var);

   /**
    * Allocate enough memory for the stack in each ir_ssa_variable_state after
    * we have the final assignment count
    */
   void allocate_state_arrays();

   void remove_decls();

private:
   /** mapping of old (non-SSA) variable -> ir_ssa_variable_state */
   struct hash_table *ht;

   /**
    * Mapping of new (SSA) variable -> old (non-SSA) variable. This is updated
    * by ir_ssa_variable_state::stack_push and ir_ssa_variable_state::stack_pop,
    * and used when we need to figure out which stack to pop when in the
    * backwards phase of renaming variables.
    */
   struct hash_table *new_to_old;

   friend class ir_ssa_variable_state;

   void remove_variable(const ir_variable *var);
};

/**
 * \class ir_parameter_visitor
 *
 * Rewrites out and inout parameters of functions to use a separate temporary.
 * For example if, we have:
 *
 * void foo(out vec4 arg1, inout vec4 arg2);
 *
 * and it gets called like:
 *
 * foo(bar, baz);
 *
 * Then assuming bar and baz are local variables to be transformed into SSA, it
 * will be rewritten as
 *
 * vec4 tmp1, tmp2 = baz;
 * foo(tmp1, tmp2);
 * bar = tmp1;
 * baz = tmp2;
 *
 * This captures the correct semantics of the original while still allowing us
 * to convert bar and baz to SSA variables; in effect, this limits the
 * "non-SSA-ness" to those four statements, hopefully allowing more
 * optimizations to occur than if we simply prevented bar and baz from being
 * transformed into SSA form.
 */
class ir_parameter_visitor : public ir_hierarchical_visitor
{
public:
   ir_parameter_visitor(ir_ssa_state_visitor *ssv) : ssv(ssv)
   {
   }

   virtual ir_visitor_status visit_enter(ir_call *call);

private:
   ir_ssa_state_visitor *ssv;
};


class ir_control_flow_entry
{
public:
   ir_control_flow_entry(ir_control_flow_entry *next) : next(next)
   {
   }

   ir_control_flow_entry *next;

   virtual class ir_if_entry *as_if_entry() { return NULL; }
   virtual class ir_loop_entry *as_loop_entry() { return NULL; };
};

class ir_if_entry : public ir_control_flow_entry
{
public:
   ir_if_entry(ir_if *ir, ir_control_flow_entry *next)
      : ir_control_flow_entry(next), ir(ir), in_then(false)
   {
   }

   virtual class ir_if_entry *as_if_entry() { return this; }

   ir_if *ir;
   bool in_then;
};

class ir_loop_entry : public ir_control_flow_entry
{
public:
   ir_loop_entry(ir_loop *loop, ir_control_flow_entry *next)
      : ir_control_flow_entry(next), loop(loop)
   {
   }

   virtual class ir_loop_entry *as_loop_entry() { return this; }

   ir_loop *loop;
};

/**
 * \class ir_phi_insertion_visitor
 *
 * Inserts "trivial" phi nodes of the form V = phi(V, V, ...) into the correct
 * places in the IR.
 */
class ir_phi_insertion_visitor : public ir_hierarchical_visitor
{
public:
   ir_phi_insertion_visitor(ir_ssa_state_visitor *ssv,
			    ir_dead_branches_visitor *dbv,
			    ir_loop_jumps_visitor *ljv)
      : ssv(ssv), dbv(dbv), ljv(ljv), cf_stack_head(NULL)
   {
   }

   virtual ir_visitor_status visit_enter(ir_if *);
   virtual ir_visitor_status visit_enter(ir_loop *);
   virtual ir_visitor_status visit(ir_dereference_variable *);

private:

   /**
    * Actually does the work of inserting a phi node into a specific basic if
    * or loop.
    *
    * \return false if a trivial phi node has already been inserted for this
    * variable, and true otherwise.
    */
   bool add_phi(ir_if *ir, ir_variable *var);
   bool add_phi(ir_loop *loop, ir_variable *var);

   ir_ssa_state_visitor *ssv;
   ir_dead_branches_visitor *dbv;
   ir_loop_jumps_visitor *ljv;

   ir_control_flow_entry *cf_stack_head;
};

/**
 * \class ir_rewrite_visitor
 * \class ir_rewrite_forward_visitor
 * \class ir_rewrite_backward_visitor
 *
 * These classes together implement the algorithm for renaming variables to SSA
 * once we have set up all the state, fixed up out and inout parameters of
 * function calls, and inserted trivial phi nodes. Essentially, the various
 * rewrite_forwards() and rewrite_backwards() methods work together to visit the
 * dominance tree in a depth-first manner, replacing non-SSA variables with the
 * current SSA variable.
 */
class ir_rewrite_forward_visitor : public ir_hierarchical_visitor
{
public:
   ir_rewrite_forward_visitor(ir_ssa_state_visitor *ssv) : ssv(ssv)
   {
   }

   virtual ir_visitor_status visit_enter(ir_assignment *ir);
   virtual ir_visitor_status visit_enter(ir_call *ir);
   virtual ir_visitor_status visit(ir_dereference_variable *ir);

private:
   ir_ssa_state_visitor *ssv;
};

class ir_rewrite_backward_visitor : public ir_hierarchical_visitor
{
public:
   ir_rewrite_backward_visitor(ir_ssa_state_visitor *ssv) : ssv(ssv)
   {
   }

   virtual ir_visitor_status visit_enter(ir_assignment *ir);
   virtual ir_visitor_status visit_enter(ir_call *ir);

private:
   ir_ssa_state_visitor *ssv;
};

class ir_rewrite_visitor {
public:
   ir_rewrite_visitor(ir_ssa_state_visitor *, ir_dead_branches_visitor *);

   void rewrite(exec_list *instructions);

private:
   void rewrite_forwards(exec_list *instructions);
   void rewrite_backwards(exec_list *instructions);

   void rewrite_forwards(ir_if *);
   void rewrite_forwards(ir_loop *);
   void rewrite_forwards(ir_loop_jump *);

   void rewrite_backwards(ir_if *);
   void rewrite_backwards(ir_loop *);

   void rewrite_phi_dest(ir_phi *);

   ir_ssa_state_visitor *ssv;
   ir_dead_branches_visitor *dbv;
   ir_rewrite_forward_visitor rfv;
   ir_rewrite_backward_visitor rbv;

   ir_loop *outer_loop;
};