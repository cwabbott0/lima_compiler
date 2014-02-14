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

/**
 * \file ir_dead_branches.h
 *
 * Provides a visitor which determines, for each if instruction, whether
 * control will never flow the from the then-block or else-block
 * to the next instruction due to jump statements (break, continue, return,
 * discard).
 */

#include "ir.h"
#include "ir_visitor.h"

class ir_dead_branches
{
public:
   ir_dead_branches(ir_if *ir);

   ir_if *ir;

   /**
    * True if the then branch is dead due to a return or discard; false if the
    * then branch is dead due to a continue.
    */
   bool then_dead;
   bool else_dead; /** < ditto for the else_instructions */

   /** whether the then branch is dead due to a return or discard */
   bool then_dead_return;
   bool else_dead_return; /** < ditto for else branch */
};

/**
 * The class that does the analysis. This is intended to used like this:
 *
 * ir_dead_branches_visitor dbv;
 * dbv.run(ir);
 * ...
 * //use dbv.get_dead_branches() to get the dead branch info for a given
 * //if statement...
 * ...
 *
 */

class ir_dead_branches_visitor : public ir_hierarchical_visitor
{
public:
   ir_dead_branches_visitor();
   ~ir_dead_branches_visitor();

   virtual ir_visitor_status visit_enter(ir_if *);
   virtual ir_visitor_status visit_enter(ir_loop *);
   virtual ir_visitor_status visit(ir_loop_jump *);
   virtual ir_visitor_status visit_enter(ir_return *);
   virtual ir_visitor_status visit_enter(ir_discard *);

   ir_dead_branches *get_dead_branches(ir_if *ir);

private:
   void visit_return_or_discard();

   /**
    * The ir_dead_branches corresponding to the innermost if statement that
    * contains the code we're visting.
    */
   ir_dead_branches *outer_db;
   /** True if we're visiting a loop contained in the innermost if statement. */
   bool in_loop;
   /**
    * Indicates whether we're visiting the then or else branch of the innermost
    * if statement.
    */
   bool in_then;

   /**
    * The hash table that stores the result of the analysis. The key is an
    * ir_if pointer, and the value is the associated ir_dead_branches.
    */
   struct hash_table *ht;
};
