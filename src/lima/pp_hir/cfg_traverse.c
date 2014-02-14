/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
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

#include "pp_hir.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	lima_pp_hir_block_t* block;
	unsigned child_index;
} dfs_node;

typedef struct {
	dfs_node* nodes;
	int index;
} dfs_stack;

static void cleanup(dfs_stack stack)
{
	free(stack.nodes);
}

static inline unsigned get_num_children(lima_pp_hir_block_t* block)
{
	if (block->is_end)
		return 0;
	if (block->branch_cond == lima_pp_hir_branch_cond_always)
		return 1;
	return 2;
}

bool lima_pp_hir_cfg_traverse(lima_pp_hir_prog_t* prog,
							  lima_pp_hir_cfg_visitor_state* state,
						      lima_pp_hir_cfg_visitor_func visit, bool preorder)
{
	if (prog->num_blocks == 0)
		return true;
	
	lima_pp_hir_block_t* block;
	pp_hir_prog_for_each_block(prog, block)
		block->visited = false;
	
	//Initialize state
	
	dfs_stack stack;

	//Set up the head node
	stack.nodes = malloc(sizeof(dfs_node) * prog->num_blocks); //Worst-case
	stack.index = 0;
	stack.nodes[0].block = pp_hir_first_block(prog);
	stack.nodes[0].child_index = 0;

	
	while (stack.index >= 0)
	{
		dfs_node* cur_node = &stack.nodes[stack.index];
		if (cur_node->child_index == 0)
		{
			//This is our first time traversing this node, 
			//visit it and mark it as visited
			if (preorder)
			{
				state->block = cur_node->block;
				if (!visit(state))
				{
					cleanup(stack);
					return false;
				}
			}
			cur_node->block->visited = true;
		}
		
		unsigned num_children = get_num_children(cur_node->block);
		
		//Skip over already visited blocks
		//TODO: possibly label these as back-edges
		while (cur_node->child_index < num_children &&
			   cur_node->block->next[cur_node->child_index]->visited)
		{
			cur_node->child_index++;
		}
		
		if (cur_node->child_index == num_children)
		{
			//We're done with this node, visit it if doing a postorder traversal,
			//And then go up the stack
			
			if (!preorder)
			{
				state->block = cur_node->block;
				if (!visit(state))
				{
					cleanup(stack);
					return false;
				}
			}
			stack.index--;
			if (stack.index >= 0)
				stack.nodes[stack.index].child_index++;
			
			continue;
		}
		
		//Expand the current child node
		stack.index++;
		stack.nodes[stack.index].block = cur_node->block->next[cur_node->child_index];
		stack.nodes[stack.index].child_index = 0;
	}
	
	free(stack.nodes);
	
	return true;
}
