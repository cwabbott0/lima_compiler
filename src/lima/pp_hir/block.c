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
#include <stdbool.h>
#include <string.h>



lima_pp_hir_block_t* lima_pp_hir_block_create(void)
{
	lima_pp_hir_block_t* block
		= (lima_pp_hir_block_t*)malloc(sizeof(lima_pp_hir_block_t));
	if (!block) return NULL;

	block->size = 0;
	block->prog = NULL;
	block->is_end = true;
	block->discard = false;
	block->output = NULL;
	list_init(&block->cmd_list);
	
	block->imm_dominator = NULL;
	if (!ptrset_create(&block->dom_tree_children))
	{
		free(block);
		return NULL;
	}
	
	if (!ptrset_create(&block->dominance_frontier))
	{
		ptrset_delete(block->dom_tree_children);
		free(block);
		return NULL;
	}
	
	return block;
}

void lima_pp_hir_block_delete(lima_pp_hir_block_t* block)
{
	if (!block)
		return;
	
	if (block->is_end)
	{
		if(!block->discard && block->output)
		{
			ptrset_remove(&block->output->block_uses, block);
		}
	}
	else
	{
		if (block->branch_cond != lima_pp_hir_branch_cond_always)
		{
			if (!block->reg_cond_a.is_constant)
				ptrset_remove(&block->reg_cond_a.reg->block_uses, block);
			if (!block->reg_cond_b.is_constant)
				ptrset_remove(&block->reg_cond_b.reg->block_uses, block);
		}
	}

	while (block->size > 0)
	{
		lima_pp_hir_cmd_t* cmd = pp_hir_first_cmd(block);
		lima_pp_hir_block_remove(block, cmd);
	}
	
	if (block->preds)
		free(block->preds);
	
	ptrset_delete(block->dom_tree_children);
	ptrset_delete(block->dominance_frontier);
	
	free(block);
}

static void update_reg_alloc(lima_pp_hir_cmd_t* cmd)
{
	if (cmd->dst.reg.index >= cmd->block->prog->reg_alloc)
		cmd->block->prog->reg_alloc = cmd->dst.reg.index + 1;
}

static void add_to_uses(lima_pp_hir_cmd_t* cmd)
{
	unsigned i;
	for (i = 0; i < cmd->num_args; i++)
	{
		if (!cmd->src[i].constant && cmd->src[i].depend)
		{
			lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
			ptrset_add(&dep->cmd_uses, cmd);
		}
	}
}

static void remove_from_uses(lima_pp_hir_cmd_t* cmd)
{
	unsigned i;
	for (i = 0; i < cmd->num_args; i++)
	{
		if (!cmd->src[i].constant && cmd->src[i].depend)
		{
			lima_pp_hir_cmd_t* dep = cmd->src[i].depend;
			ptrset_remove(&dep->cmd_uses, cmd);
		}
	}
}

void lima_pp_hir_block_insert(lima_pp_hir_cmd_t* cmd,
							  lima_pp_hir_cmd_t* before)
{
	list_add(&cmd->cmd_list, &before->cmd_list);
	cmd->block = before->block;
	cmd->block->size++;
	update_reg_alloc(cmd);
	add_to_uses(cmd);
}

void lima_pp_hir_block_insert_start(lima_pp_hir_block_t* block,
									lima_pp_hir_cmd_t* cmd)
{
	list_add(&cmd->cmd_list, &block->cmd_list);
	cmd->block = block;
	block->size++;
	update_reg_alloc(cmd);
	add_to_uses(cmd);
}

void lima_pp_hir_block_insert_end(lima_pp_hir_block_t* block,
								  lima_pp_hir_cmd_t* cmd)
{
	list_add(&cmd->cmd_list, block->cmd_list.prev);
	cmd->block = block;
	block->size++;
	update_reg_alloc(cmd);
	add_to_uses(cmd);
}

void lima_pp_hir_block_remove(lima_pp_hir_block_t* block,
							  lima_pp_hir_cmd_t* cmd)
{
	block->size--;
	list_del(&cmd->cmd_list);
	remove_from_uses(cmd);
	lima_pp_hir_cmd_delete(cmd);
}

void lima_pp_hir_block_replace(lima_pp_hir_cmd_t* old_cmd,
							   lima_pp_hir_cmd_t* new_cmd)
{
	__list_add(&new_cmd->cmd_list, old_cmd->cmd_list.prev, old_cmd->cmd_list.next);
	new_cmd->block = old_cmd->block;
	remove_from_uses(old_cmd);
	lima_pp_hir_cmd_delete(old_cmd);
	update_reg_alloc(new_cmd);
	add_to_uses(new_cmd);
}

typedef struct
__attribute__((__packed__))
{
	uint32_t index;
	double constant;
	bool is_constant : 1;
} _lima_pp_hir_reg_cond_t;

typedef struct
__attribute__((__packed__))
{
	char                  ident[4]; /* ="BSB\0" */
	uint32_t              size;
	uint32_t              next[2];
	lima_pp_hir_branch_cond_e branch_cond;
	_lima_pp_hir_reg_cond_t   reg_cond_a;
	_lima_pp_hir_reg_cond_t   reg_cond_b;
	int                   output_index;
	bool                  is_end : 1;
	bool                  discard : 1;
	uint32_t reserved;
} _lima_pp_hir_file_header_t;

static lima_pp_hir_cmd_t* get_cond_reg_dep(int index, lima_pp_hir_prog_t *prog,
										   lima_pp_hir_block_t* block)
{
	lima_pp_hir_block_t* iter_block;
	lima_pp_hir_cmd_t* cmd;
	pp_hir_prog_for_each_block(prog, iter_block)
		pp_hir_block_for_each_cmd(iter_block, cmd)
			if (cmd->dst.reg.index == index)
				return cmd;
	
	pp_hir_block_for_each_cmd(block, cmd)
		if (cmd->dst.reg.index == index)
			return cmd;
	return NULL;
}

static lima_pp_hir_reg_cond_t get_cond_reg(_lima_pp_hir_reg_cond_t cond,
										   lima_pp_hir_prog_t* prog,
										   lima_pp_hir_block_t* block)
{
	lima_pp_hir_reg_cond_t ret;
	if (cond.is_constant)
	{
		ret.is_constant = true;
		ret.constant = cond.constant;
	}
	else
	{
		ret.is_constant = false;
		ret.reg = get_cond_reg_dep(cond.index, prog, block);
		ptrset_add(&ret.reg->block_uses, block);
	}
	return ret;
}

lima_pp_hir_block_t* lima_pp_hir_block_import(void* data, unsigned size, unsigned* pos,
											  lima_pp_hir_prog_t* prog)
{
	lima_pp_hir_block_t* block
		= lima_pp_hir_block_create();
	if (!block) return NULL;
	
	lima_pp_hir_prog_insert_end(block, prog);
	
	*pos = 0;
	
	*pos += sizeof(_lima_pp_hir_file_header_t);
	if (*pos > size)
		return NULL;

	_lima_pp_hir_file_header_t header = *((_lima_pp_hir_file_header_t*)data);
	data = (char*)data + sizeof(header);
	
	if (memcmp(&header.ident, "BSB\0", 4) != 0)
	{
		lima_pp_hir_block_delete(block);
		fprintf(stderr, "Error: Failed to import basic block, incorrect ident.\n");
		return NULL;
	}

	block->is_end  = header.is_end;
	if (block->is_end)
		block->discard = header.discard;
	block->next[0] = (lima_pp_hir_block_t *) header.next[0];
	block->next[1] = (lima_pp_hir_block_t *) header.next[1];
	block->branch_cond = header.branch_cond;
	

	uint32_t i;
	for (i = 0; i < header.size; i++)
	{
		unsigned cmd_pos;
		lima_pp_hir_cmd_t* cmd
			= lima_pp_hir_cmd_import((char*)data, size - *pos, &cmd_pos, prog, block);
		if (!cmd)
		{
			lima_pp_hir_block_delete(block);
			fprintf(stderr, "Error: Failed to read command.\n");
			return NULL;
		}
		lima_pp_hir_block_insert_end(block, cmd);
		
		*pos += cmd_pos;
		data = (char*)data + cmd_pos;
	}
	
	if (!block->is_end && block->branch_cond != lima_pp_hir_branch_cond_always)
	{
		block->reg_cond_a = get_cond_reg(header.reg_cond_a, prog, block);
		block->reg_cond_b = get_cond_reg(header.reg_cond_b, prog, block);
	}
	if (block->is_end && !block->discard)
	{
		block->output = get_cond_reg_dep(header.output_index, prog, block);
		ptrset_add(&block->output->block_uses, block);
	}

	return block;
}

static unsigned block_get_index(lima_pp_hir_block_t* block,
								lima_pp_hir_prog_t* prog)
{
	lima_pp_hir_block_t* iter_block;
	unsigned i = 1;
	pp_hir_prog_for_each_block(prog, iter_block)
	{
		if (iter_block == block)
			return i;
		i++;
	}
	return 0;
}

static _lima_pp_hir_reg_cond_t write_cond_reg(lima_pp_hir_reg_cond_t reg)
{
	_lima_pp_hir_reg_cond_t ret;
	if (reg.is_constant)
	{
		ret.is_constant = true;
		ret.constant = reg.constant;
	}
	else
	{
		ret.is_constant = false;
		ret.index = reg.reg->dst.reg.index;
	}
	return ret;
}

void* lima_pp_hir_block_export(lima_pp_hir_block_t* block,
							  lima_pp_hir_prog_t* prog, unsigned* size)
{
	if (!block)
		return NULL;

	_lima_pp_hir_file_header_t header;
	memcpy(&header.ident, "BSB\0", 4);
	header.size             = block->size;
	header.is_end           = block->is_end;
	
	if (block->is_end)
	{
		header.discard = block->discard;
		header.next[0] = 0;
		header.next[1] = 0;
		header.branch_cond = 0;
	}
	else
	{
		header.next[0] = block_get_index(block->next[0], prog);
		
		if (block->branch_cond == lima_pp_hir_branch_cond_always)
			header.next[1] = 0;
		else
			header.next[1] = block_get_index(block->next[1], prog);
		
		header.branch_cond = block->branch_cond;
	}
	
	if (!block->is_end && block->branch_cond != lima_pp_hir_branch_cond_always)
	{
		header.reg_cond_a = write_cond_reg(block->reg_cond_a);
		header.reg_cond_b = write_cond_reg(block->reg_cond_b);
	}
	if (block->is_end && !block->discard)
	{
		header.output_index = block->output->dst.reg.index;
	}
	header.reserved = 0;

	void* data = malloc(sizeof(header));
	if (!data)
		return NULL;
	
	memcpy(data, &header, sizeof(header));
	*size += sizeof(header);

	lima_pp_hir_cmd_t* cmd;
	pp_hir_block_for_each_cmd(block, cmd)
	{
		unsigned cmd_size;
		void* cmd_data = lima_pp_hir_cmd_export(cmd, &cmd_size);
		if (!cmd_data)
		{
			fprintf(stderr, "Error: Failed to export basic block command.\n");
			return NULL;
		}
		
		data = realloc(data, *size + cmd_size);
		memcpy((char*)data + *size, cmd_data, cmd_size);
		*size += cmd_size;
		free(cmd_data);
	}

	return data;
}
