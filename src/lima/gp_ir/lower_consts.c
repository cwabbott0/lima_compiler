/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2014 Connor Abbott
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

#include "gp_ir.h"

static bool lower_const_node(lima_gp_ir_const_node_t* const_node,
							 lima_shader_symbols_t* symbols)
{
	unsigned offset = lima_shader_symbols_add_const(symbols,
													const_node->constant);
	
	lima_gp_ir_load_node_t* load =
		lima_gp_ir_load_node_create(lima_gp_ir_op_load_uniform);
	
	if (!load)
		return false;
	
	load->index = offset / 4;
	load->component = offset % 4;
	load->offset = false;
	
	return lima_gp_ir_node_replace(&const_node->node, &load->node);
}

static void lower_clamp_const(lima_gp_ir_clamp_const_node_t* node,
							  lima_shader_symbols_t* symbols)
{
	if (!node->is_inline_const)
		return;
	
	unsigned index = lima_shader_symbols_add_clamp_const(symbols, node->low,
														 node->high);
	node->uniform_index = index;
	node->is_inline_const = false;
}

static bool lower_cb(lima_gp_ir_node_t* node, void* state)
{
	lima_shader_symbols_t* symbols = (lima_shader_symbols_t*) state;
	
	switch (node->op)
	{
		case lima_gp_ir_op_const:
			return lower_const_node(gp_ir_node_to_const(node), symbols);
			
		case lima_gp_ir_op_clamp_const:
			lower_clamp_const(gp_ir_node_to_clamp_const(node), symbols);
			break;
			
		default:
			break;
	}
	
	return true;
}

bool lima_gp_ir_lower_consts(lima_gp_ir_prog_t* prog,
							 lima_shader_symbols_t* symbols)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_root_node_t* node;
		gp_ir_block_for_each_node(block, node)
		{
			if (!lima_gp_ir_node_dfs(&node->node, lower_cb, NULL, symbols))
				return false;
		}
	}
	
	return true;
}
