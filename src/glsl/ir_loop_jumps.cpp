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
#include "ir_loop_jumps.h"
#include "main/hash_table.h"

/**
 * \file ir_loop_jumps.h
 *
 * Provides a visitor that collects all the continue and break statements for
 * each loop.
 */

ir_loop_jumps::ir_loop_jumps(ir_loop *loop) : loop(loop)
{
   this->mem_ctx = ralloc_context(NULL);
}

ir_loop_jumps::~ir_loop_jumps()
{
   ralloc_free(this->mem_ctx);
}

void
ir_loop_jumps::add_continue(ir_loop_jump *ir)
{
   ir_loop_jump_entry *entry = new(this->mem_ctx) ir_loop_jump_entry();
   entry->ir = ir;
   this->continues.push_tail(entry);
}

void
ir_loop_jumps::add_break(ir_loop_jump *ir)
{
   ir_loop_jump_entry *entry = new(this->mem_ctx) ir_loop_jump_entry();
   entry->ir = ir;
   this->breaks.push_tail(entry);
}

ir_loop_jumps_visitor::ir_loop_jumps_visitor()
{
   this->ht = _mesa_hash_table_create(NULL, _mesa_key_pointer_equal);
   this->outer_lj = NULL;
}

static void
free_entry(struct hash_entry *entry)
{
   ir_loop_jumps *loop_jumps = (ir_loop_jumps *) entry->data;
   delete loop_jumps;
}

ir_loop_jumps_visitor::~ir_loop_jumps_visitor()
{
   _mesa_hash_table_destroy(this->ht, free_entry);
}

ir_visitor_status
ir_loop_jumps_visitor::visit_enter(ir_loop *ir)
{
   ir_loop_jumps *loop_jumps = new ir_loop_jumps(ir);
   _mesa_hash_table_insert(this->ht, _mesa_hash_pointer(ir), ir, loop_jumps);

   ir_loop_jumps *old_outer_lj = this->outer_lj;
   this->outer_lj = loop_jumps;

   visit_list_elements(this, &ir->body_instructions);

   this->outer_lj = old_outer_lj;
   return visit_continue_with_parent;
}

ir_visitor_status
ir_loop_jumps_visitor::visit(ir_loop_jump *ir)
{
   switch (ir->mode) {
      case ir_loop_jump::jump_break:
	 this->outer_lj->add_break(ir);
	 break;

      case ir_loop_jump::jump_continue:
	 this->outer_lj->add_continue(ir);
	 break;

      default:
	 assert(!"unknown loop jump mode");
	 break;
   }

   return visit_continue;
}

ir_loop_jumps *
ir_loop_jumps_visitor::get_loop_jumps(ir_loop *ir)
{
   assert(ir);

   struct hash_entry *e = _mesa_hash_table_search(this->ht,
						  _mesa_hash_pointer(ir),
						  ir);
   if (e)
      return (ir_loop_jumps *)e->data;

   assert(0);
   return NULL;
}
