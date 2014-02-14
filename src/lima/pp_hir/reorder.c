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

typedef struct _block_list {
	lima_pp_hir_block_t* block;
	struct _block_list* next;
} block_list;

typedef struct {
	lima_pp_hir_cfg_visitor_state visitor_state;
	block_list* blocks;
} reordering_visitor_state;

static void delete_block_list(block_list* list)
{
	block_list* cur_block = list;
	while (cur_block != NULL)
	{
		block_list* new_block = cur_block->next;
		free(cur_block);
		cur_block = new_block;
	}
}

static bool reordering_visitor(lima_pp_hir_cfg_visitor_state* visitor_state)
{
	reordering_visitor_state* state = (reordering_visitor_state*)visitor_state;

	block_list* new_block = malloc(sizeof(block_list));
	if (!new_block)
		return false;
	
	//Add our block to the front of the list
	//Note that the list is traversed front to back,
	//In reverse order to the way we add the blocks
	new_block->block = visitor_state->block;
	new_block->next = state->blocks;
	state->blocks = new_block;
	
	return true;
}


//Reorders a program's basic blocks using a reverse-postordering
//Uses lima_pp_hir_cfg_traverse to do the real "heavy lifting"
//(i.e. actually traversing the control-flow graph with a depth-first search)

bool lima_pp_hir_prog_reorder(lima_pp_hir_prog_t* prog)
{
	reordering_visitor_state state;
	state.blocks = NULL;
	
	if (!lima_pp_hir_cfg_traverse(prog, (lima_pp_hir_cfg_visitor_state*)&state, 
							  reordering_visitor, false))
	{
		delete_block_list(state.blocks);
		return false;
	}
	
	//Delete dead blocks (i.e. blocks we didn't traverse)
	lima_pp_hir_block_t* block, *tmp_block;
	pp_hir_prog_for_each_block_safe(prog, tmp_block, block)
	{
		bool found = false;
		block_list* cur_block = state.blocks;
		while (cur_block != NULL)
		{
			if (cur_block->block == block)
			{
				found = true;
				break;
			}
			cur_block = cur_block->next;
		}
		
		if (!found)
			lima_pp_hir_block_delete(block);
	}
	
	//Finally, replace the list of basic blocks
	while (prog->num_blocks > 0)
	{
		lima_pp_hir_block_t* block = pp_hir_first_block(prog);
		list_del(&block->block_list);
		prog->num_blocks--;
	}
	
	block_list* cur_block = state.blocks;
	while (cur_block != NULL)
	{
		list_add(&cur_block->block->block_list, prog->block_list.prev);
		cur_block = cur_block->next;
		prog->num_blocks++;
	}
	
	delete_block_list(state.blocks);
	
	return true;
}
