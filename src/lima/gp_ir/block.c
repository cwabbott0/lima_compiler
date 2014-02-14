/* Author(s):
 *   Connor Abbott (connor@abbott.cx)
 *
 * Copyright (c) 2013 Connor Abbott
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
#include "scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

lima_gp_ir_block_t* lima_gp_ir_block_create(void)
{
	lima_gp_ir_block_t* block = malloc(sizeof(lima_gp_ir_block_t));
	if (!block)
		return NULL;
	
	list_init(&block->node_list);
	list_init(&block->instr_list);
	block->num_nodes = block->num_instrs = 0;
	
	block->num_preds = 0;
	block->preds = NULL;
	
	if (!ptrset_create(&block->phi_nodes))
	{
		free(block);
		return NULL;
	}
	
	if (!ptrset_create(&block->start_nodes))
	{
		ptrset_delete(block->phi_nodes);
		free(block);
		return NULL;
	}
	
	if (!ptrset_create(&block->end_nodes))
	{
		ptrset_delete(block->phi_nodes);
		ptrset_delete(block->start_nodes);
		free(block);
		return NULL;
	}
	
	if (!ptrset_create(&block->dominance_frontier))
	{
		ptrset_delete(block->phi_nodes);
		ptrset_delete(block->start_nodes);
		ptrset_delete(block->end_nodes);
		free(block);
		return NULL;
	}
	
	if (!ptrset_create(&block->dom_tree_children))
	{
		ptrset_delete(block->phi_nodes);
		ptrset_delete(block->start_nodes);
		ptrset_delete(block->end_nodes);
		ptrset_delete(block->dominance_frontier);
		free(block);
		return NULL;
	}
	
	block->live_phys_before = bitset_create(16*4);
	block->live_virt_before = bitset_create(0);
	
	block->imm_dominator = NULL;
	
	return block;
}

void lima_gp_ir_block_delete(lima_gp_ir_block_t* block)
{
	lima_gp_ir_root_node_t* node, *temp;
	gp_ir_block_for_each_node_safe(block, node, temp)
	{
		lima_gp_ir_node_delete(&node->node);
	}
	
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
	ptrset_iter_for_each(iter, phi_node)
	{
		lima_gp_ir_node_delete(&phi_node->node);
	}
	
	while (block->num_instrs > 0)
		lima_gp_ir_instr_delete(gp_ir_block_first_instr(block));
	
	if (block->preds)
		free(block->preds);
	
	ptrset_delete(block->phi_nodes);
	ptrset_delete(block->start_nodes);
	ptrset_delete(block->end_nodes);
	bitset_delete(block->live_phys_before);
	bitset_delete(block->live_virt_before);
	ptrset_delete(block->dominance_frontier);
	ptrset_delete(block->dom_tree_children);
	
	free(block);
}


// Update any state necessary when inserting a root node
// Currently, just updates register defs if necessary
static void block_insert_helper(lima_gp_ir_root_node_t* node)
{
	if (node->node.op == lima_gp_ir_op_store_reg)
	{
		lima_gp_ir_store_reg_node_t* store_reg_node =
			gp_ir_node_to_store_reg(node);
		lima_gp_ir_reg_t* reg = store_reg_node->reg;
		ptrset_add(&reg->defs, node);
	}
}

void lima_gp_ir_block_insert_start(lima_gp_ir_block_t* block,
								   lima_gp_ir_root_node_t* node)
{
	block_insert_helper(node);
	node->block = block;
	list_add(&node->node_list, &block->node_list);
	block->num_nodes++;
}

void lima_gp_ir_block_insert_end(lima_gp_ir_block_t* block,
								 lima_gp_ir_root_node_t* node)
{
	block_insert_helper(node);
	node->block = block;
	list_add(&node->node_list, block->node_list.prev);
	block->num_nodes++;
}

void lima_gp_ir_block_insert_after(lima_gp_ir_root_node_t* node,
								   lima_gp_ir_root_node_t* before)
{
	block_insert_helper(node);
	node->block = before->block;
	list_add(&node->node_list, &before->node_list);
	node->block->num_nodes++;
}

void lima_gp_ir_block_insert_before(lima_gp_ir_root_node_t* node,
									lima_gp_ir_root_node_t* after)
{
	block_insert_helper(node);
	node->block = after->block;
	__list_add(&node->node_list, after->node_list.prev, &after->node_list);
	node->block->num_nodes++;
}

void lima_gp_ir_block_remove(lima_gp_ir_root_node_t* node)
{
	node->block->num_nodes--;
	list_del(&node->node_list);
	lima_gp_ir_node_delete(&node->node);
}

void lima_gp_ir_block_replace(lima_gp_ir_root_node_t* old_node,
							  lima_gp_ir_root_node_t* new_node)
{
	block_insert_helper(new_node);
	new_node->block = old_node->block;
	__list_add(&new_node->node_list, old_node->node_list.prev,
			   old_node->node_list.next);
	lima_gp_ir_node_delete(&old_node->node);
}

void lima_gp_ir_block_insert_phi(lima_gp_ir_block_t* block,
								 lima_gp_ir_phi_node_t* phi_node)
{
	lima_gp_ir_node_t* node = &phi_node->node;
	
	ptrset_add(&phi_node->dest->defs, node);
	unsigned i;
	for (i = 0; i < phi_node->num_sources; i++)
		ptrset_add(&phi_node->sources[i].reg->uses, node);
	
	ptrset_add(&block->phi_nodes, phi_node);
	phi_node->block = block;
}

void lima_gp_ir_block_remove_phi(lima_gp_ir_block_t* block,
								 lima_gp_ir_phi_node_t* phi_node)
{
	lima_gp_ir_node_t* node = &phi_node->node;
	
	ptrset_remove(&phi_node->dest->defs, node);
	unsigned i;
	for (i = 0; i < phi_node->num_sources; i++)
		ptrset_remove(&phi_node->sources[i].reg->uses, node);
	
	ptrset_remove(&block->phi_nodes, phi_node);
	lima_gp_ir_node_delete(node);
}

typedef struct
{
	unsigned expr_index;
	unsigned tabs;
} expr_print_state_t;

static bool expr_print_cb(lima_gp_ir_node_t* node, void* _state)
{
	if (ptrset_size(node->parents) > 1)
	{
		expr_print_state_t* state = (expr_print_state_t*) _state;
		
		lima_gp_ir_print_tabs(state->tabs);
		printf("(def_expr expr_%u\n", state->expr_index);
		node->print(node, state->tabs + 1);
		printf(")\n");
		
		node->index = state->expr_index;
		state->expr_index++;
	}
	return true;
}

static void print_liveness(bitset_t live, unsigned size)
{
	unsigned i;
	for (i = 0; i < size; i++)
	{
		if (bitset_get(live, 4 * i + 0) ||
			bitset_get(live, 4 * i + 1) ||
			bitset_get(live, 4 * i + 2) ||
			bitset_get(live, 4 * i + 3))
		{
			printf("%u.", i);
			if (bitset_get(live, 4 * i + 0))
				printf("x");
			if (bitset_get(live, 4 * i + 1))
				printf("y");
			if (bitset_get(live, 4 * i + 2))
				printf("z");
			if (bitset_get(live, 4 * i + 3))
				printf("w");
			printf(" ");
		}
	}
}

static void print_block_liveness(lima_gp_ir_block_t* block)
{
	printf("//live_phys: ");
	print_liveness(block->live_phys_before, 16);
	printf("\n//live_virt: ");
	print_liveness(block->live_virt_before, block->prog->reg_alloc);
	printf("\n");
}

static void print_node_liveness(lima_gp_ir_root_node_t* node)
{
	printf("//live_phys: ");
	print_liveness(node->live_phys_after, 16);
	printf("\n//live_virt: ");
	print_liveness(node->live_virt_after, node->block->prog->reg_alloc);
	printf("\n");
}

static void print_dominance_info(lima_gp_ir_block_t* block)
{
	if (block->imm_dominator)
		printf("//immediate dominator: block_%u\n", block->imm_dominator->index);
	
	ptrset_iter_t iter = ptrset_iter_create(block->dominance_frontier);
	lima_gp_ir_block_t* cur_block;
	printf("//dominance frontier:\n");
	ptrset_iter_for_each(iter, cur_block)
	{
		printf("//\tblock_%u\n", cur_block->index);
	}
}

bool lima_gp_ir_block_print(lima_gp_ir_block_t* block, unsigned tabs,
							bool print_liveness)
{
	expr_print_state_t state = {
		.expr_index = 0,
		.tabs = tabs
	};
	
	printf("block_%u:\n", block->index);
	
	print_dominance_info(block);
	
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
	ptrset_iter_for_each(iter, phi_node)
	{
		phi_node->node.print(&phi_node->node, tabs);
		printf("\n");
	}
	
	if (print_liveness)
		print_block_liveness(block);
	
	lima_gp_ir_root_node_t* node;
	gp_ir_block_for_each_node(block, node)
	{
		if (!lima_gp_ir_node_dfs(&node->node, NULL, expr_print_cb,
								 (void*) &state))
			return false;
		lima_gp_ir_node_print(&node->node, tabs);
		printf("\n");
		if (print_liveness)
			print_node_liveness(node);
	}
	
	printf("\n");
	return true;
}

static bool node_index_cb(lima_gp_ir_node_t* node, void* state)
{
	unsigned* index = state;
	node->index = *index;
	(*index)++;
	return true;
}

typedef struct
{
	void** node_data;
	unsigned* node_data_size;
	lima_gp_ir_block_t* block;
} node_export_state_t;

static bool node_export_cb(lima_gp_ir_node_t* node, void* state)
{
	node_export_state_t* export_state = state;
	export_state->node_data[node->index] =
		node->export_node(node, export_state->block,
						  export_state->node_data_size + node->index);
	return !!(export_state->node_data[node->index]);
}

typedef struct
{
	uint32_t num_phi_nodes, num_nodes;
} block_header_t;

void *lima_gp_ir_block_export(lima_gp_ir_block_t* block, unsigned* size)
{
	unsigned index = 0;
	unsigned i;
	lima_gp_ir_root_node_t* root_node;
	
	gp_ir_block_for_each_node(block, root_node)
	{
		lima_gp_ir_node_dfs(&root_node->node, NULL, node_index_cb, &index);
	}
	
	//Note: index now holds the total number of nodes
	
	void** node_data = malloc(sizeof(void*) * index);
	if (!node_data)
		return NULL;
	
	unsigned* node_data_size = malloc(sizeof(unsigned) * index);
	if (!node_data_size)
	{
		free(node_data);
		return NULL;
	}
	
	node_export_state_t export_state = {
		.node_data = node_data,
		.node_data_size = node_data_size,
		.block = block
	};
	
	gp_ir_block_for_each_node(block, root_node)
	{
		lima_gp_ir_node_dfs(&root_node->node, NULL, node_export_cb,
							&export_state);
	}
	
	unsigned num_phi_nodes = ptrset_size(block->phi_nodes);
	void** phi_node_data = malloc(sizeof(void*) * num_phi_nodes);
	if (!phi_node_data)
	{
		free(node_data);
		free(node_data_size);
		return NULL;
	}
	
	unsigned* phi_node_data_size = malloc(sizeof(unsigned) * num_phi_nodes);
	if (!phi_node_data_size)
	{
		free(node_data);
		free(node_data_size);
		free(phi_node_data);
		return NULL;
	}
	
	i = 0;
	lima_gp_ir_phi_node_t* phi_node;
	ptrset_iter_t iter = ptrset_iter_create(block->phi_nodes);
	ptrset_iter_for_each(iter, phi_node)
	{
		phi_node_data[i] = phi_node->node.export_node(&phi_node->node,
													  block,
													  phi_node_data_size + i);
		if (!phi_node_data[i])
			return NULL;
		i++;
	}
	
	*size = sizeof(block_header_t);
	for (i = 0; i < num_phi_nodes; i++)
		*size += phi_node_data_size[i];
	for (i = 0; i < index; i++)
		*size += node_data_size[i];
	
	void* data = malloc(*size);
	if (!data)
	{
		free(node_data);
		free(node_data_size);
		free(phi_node_data);
		free(phi_node_data_size);
		return NULL;
	}
	
	block_header_t* header = data;
	header->num_nodes = index;
	header->num_phi_nodes = num_phi_nodes;
	void* pos = (char*)data + sizeof(block_header_t);
	for (i = 0; i < num_phi_nodes; i++)
	{
		memcpy(pos, phi_node_data[i], phi_node_data_size[i]);
		free(phi_node_data[i]);
		pos = (char*)pos + phi_node_data_size[i];
	}
	for (i = 0; i < index; i++)
	{
		memcpy(pos, node_data[i], node_data_size[i]);
		free(node_data[i]);
		pos = (char*)pos + node_data_size[i];
	}
	
	free(node_data);
	free(node_data_size);
	free(phi_node_data);
	free(phi_node_data_size);
	
	return data;
}

static void link_node(lima_gp_ir_node_t* node)
{
	lima_gp_ir_child_node_iter_t iter;
	gp_ir_node_for_each_child(node, iter)
	{
		lima_gp_ir_node_link(node, *iter.child);
		
		//Has this child already been linked?
		if (ptrset_size((*iter.child)->parents) == 1)
			link_node(*iter.child);
	}
}

bool lima_gp_ir_block_import(lima_gp_ir_block_t* block, void* data,
							 unsigned* size)
{
	block_header_t* header = data;
	unsigned num_nodes = header->num_nodes;
	
	*size = sizeof(block_header_t);
	
	lima_gp_ir_node_t** nodes = malloc(num_nodes * sizeof(lima_gp_ir_node_t*));
	if (!nodes)
		return NULL;
	
	lima_gp_ir_node_header_t* node_header =
		(lima_gp_ir_node_header_t*)((char*)data + sizeof(block_header_t));
	unsigned i;
	for (i = 0; i < header->num_phi_nodes; i++)
	{
		lima_gp_ir_phi_node_t* phi_node = lima_gp_ir_phi_node_create(0);
		if (!phi_node)
		{
			free(nodes);
			return false;
		}
		phi_node->node.import(&phi_node->node, nodes, block, node_header);
		lima_gp_ir_block_insert_phi(block, phi_node);
		*size += node_header->size;
		node_header =
			(lima_gp_ir_node_header_t*)((char*)node_header + node_header->size);
	}
	
	lima_gp_ir_node_header_t* nodes_start = node_header;
	
	for (i = 0; i < num_nodes; i++)
	{
		nodes[i] = lima_gp_ir_node_create(node_header->op);
		if (!nodes[i])
		{
			free(nodes);
			return false;
		}
		*size += node_header->size;
		node_header =
			(lima_gp_ir_node_header_t*)((char*)node_header + node_header->size);
	}
	
	node_header = nodes_start;
	for (i = 0; i < num_nodes; i++)
	{
		if (!nodes[i]->import(nodes[i], nodes, block, node_header))
		{
			free(nodes);
			return false;
		}
		
		if (lima_gp_ir_op[nodes[i]->op].is_root_node)
		{
			lima_gp_ir_root_node_t* root_node =
				container_of(nodes[i], lima_gp_ir_root_node_t, node);
			lima_gp_ir_block_insert_end(block, root_node);
		}
		
		node_header =
			(lima_gp_ir_node_header_t*)((char*)node_header + node_header->size);
	}
	
	lima_gp_ir_root_node_t* root_node;
	gp_ir_block_for_each_node(block, root_node)
	{
		link_node(&root_node->node);
	}
	
	free(nodes);
	
	return true;
}
