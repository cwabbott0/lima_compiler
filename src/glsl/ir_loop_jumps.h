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
 * \file ir_loop_jumps.h
 *
 * Provides a visitor that collects all the continue and break statements for
 * each loop.
 */

#include "ir.h"
#include "ir_visitor.h"

class ir_loop_jump_entry : public exec_node
{
public:
   ir_loop_jump *ir;
};

class ir_loop_jumps
{
public:
   ir_loop_jumps(ir_loop *loop);
   ~ir_loop_jumps();

   void add_continue(ir_loop_jump *ir);
   void add_break(ir_loop_jump *ir);

   ir_loop *loop;

   /**
    * lists of ir_loop_jump_entry's containing all the breaks and continues in
    * the loop.
    */
   exec_list continues, breaks;

private:
   void *mem_ctx;
};

/**
 * The class that does the analysis. This is intended to used like this:
 *
 * ir_loop_jumps_visitor ljv;
 * ljv.run(ir);
 * ...
 * //use ljv.get_loop_jumps() to get the breaks and continues for a given loop
 * ...
 *
 */

class ir_loop_jumps_visitor : public ir_hierarchical_visitor
{
public:
   ir_loop_jumps_visitor();
   ~ir_loop_jumps_visitor();

   virtual ir_visitor_status visit_enter(ir_loop *);
   virtual ir_visitor_status visit(ir_loop_jump *);

   ir_loop_jumps *get_loop_jumps(ir_loop *ir);

private:
   /**
    * The ir_loop_jumps corresponding to the innermost loop that contains the
    * code we are visiting.
    */
   ir_loop_jumps *outer_lj;

   struct hash_table *ht;
};
