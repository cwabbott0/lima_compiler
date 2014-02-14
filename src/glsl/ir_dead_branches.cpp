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
#include "ir_visitor.h"
#include "ir_dead_branches.h"
#include "main/hash_table.h"

/**
 * \file ir_dead_branches.h
 *
 * Provides a visitor which determines, for each if instruction, whether
 * control will never flow the from the then-block or else-block
 * to the next instruction due to jump statements (break, continue, return,
 * discard).
 */

/*
 * Note that we keep track of whether a given branch is dead due to a return-
 * like statement (return or discard) or due to a loop jump. For example,
 * imagine you have a control flow like the following:
 *
 * if (...) {
 *    while (...) {
 *	 if (...) {
 *	    ...
 *	    continue;
 *	 } else {
 *	    ...
 *	    return;
 *	 }
 *    }
 * }
 *
 * After processing the inner if statement, we see that both branches are dead;
 * normally, this would result in declaring the then-branch of the outer if
 * statement dead, but in this case, there is a loop in between the inner and
 * outer if statement, so the branch can in fact be taken. However, if the
 * continue statement were a discard or return instead, then control would
 * always leave the function as soon as the while loop was reached, so in this
 * case the dead branch must "skip" across the loop. So we keep track of whether
 * the immediately enclosing control statement is a loop (in_loop), and if we
 * are, then after processing an if statement, we only propagate the dead branch
 * through the loop if both branches of the inner if statement are dead due to
 * a return or discard statement (then_dead_return and else_dead_return).
 */

ir_dead_branches_visitor::ir_dead_branches_visitor()
{
   this->ht = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
   this->in_loop = false;
   this->outer_db = NULL;
   this->in_then = false;
}

static void
free_entry(struct hash_entry *entry)
{
   ir_dead_branches *dead_branches = (ir_dead_branches *) entry->data;
   delete dead_branches;
}

ir_dead_branches_visitor::~ir_dead_branches_visitor()
{
   _mesa_hash_table_destroy(this->ht, free_entry);
}

ir_dead_branches::ir_dead_branches(ir_if *ir)
{
   this->ir = ir;
   this->then_dead = false;
   this->else_dead = false;
   this->then_dead_return = false;
   this->else_dead_return = false;
}

ir_dead_branches *
ir_dead_branches_visitor::get_dead_branches(ir_if *ir)
{
   assert(ir);

   struct hash_entry *e = _mesa_hash_table_search(this->ht,
						  _mesa_hash_pointer(ir),
						  ir);
   if (e)
      return (ir_dead_branches *)e->data;

   assert(0);
   return NULL;
}

ir_visitor_status
ir_dead_branches_visitor::visit_enter(ir_if *ir)
{
   ir_dead_branches *db = new ir_dead_branches(ir);
   _mesa_hash_table_insert(this->ht, _mesa_hash_pointer(ir), ir, db);

   ir_dead_branches *old_outer_db = this->outer_db;
   this->outer_db = db;

   bool old_in_loop = this->in_loop;
   this->in_loop = false;

   bool old_in_then = this->in_then;
   this->in_then = true;

   visit_list_elements(this, &ir->then_instructions);

   this->in_then = false;

   visit_list_elements(this, &ir->else_instructions);

   this->outer_db = old_outer_db;
   this->in_loop = old_in_loop;
   this->in_then = old_in_then;

   if (db->then_dead && db->else_dead && this->outer_db) {
      if (this->in_then) {
	 if (db->then_dead_return && db->else_dead_return) {
	    outer_db->then_dead = true;
	    outer_db->then_dead_return = true;
	 } else if (!this->in_loop) {
	    outer_db->then_dead = true;
	 }
      } else {
	 if (db->then_dead_return && db->else_dead_return) {
	    outer_db->else_dead = true;
	    outer_db->else_dead_return = true;
	 } else if (!this->in_loop) {
	    outer_db->else_dead = true;
	 }
      }
   }

   return visit_continue_with_parent;
}

ir_visitor_status
ir_dead_branches_visitor::visit_enter(ir_loop *loop)
{
   bool old_in_loop = this->in_loop;
   this->in_loop = true;

   visit_list_elements(this, &loop->body_instructions);

   this->in_loop = old_in_loop;

   return visit_continue_with_parent;
}

ir_visitor_status
ir_dead_branches_visitor::visit(ir_loop_jump *ir)
{
   (void) ir;

   if (this->outer_db) {
      if (this->in_then) {
	 this->outer_db->then_dead = true;
      } else {
	 this->outer_db->else_dead = true;
      }
   }

   return visit_continue;
}

ir_visitor_status
ir_dead_branches_visitor::visit_enter(ir_return *ir)
{
   (void) ir;

   this->visit_return_or_discard();
   return visit_continue;
}

ir_visitor_status
ir_dead_branches_visitor::visit_enter(ir_discard *ir)
{
   if (ir->condition != NULL) {
      ir_constant *constant = ir->condition->as_constant();
      if (constant == NULL || constant->is_zero())
	 return visit_continue;
   }

   this->visit_return_or_discard();
   return visit_continue;
}

void
ir_dead_branches_visitor::visit_return_or_discard()
{
   if (this->outer_db) {
      if (this->in_then) {
	 this->outer_db->then_dead = true;
	 this->outer_db->then_dead_return = true;
      } else {
	 this->outer_db->else_dead = true;
	 this->outer_db->else_dead_return = true;
      }
   }
}
