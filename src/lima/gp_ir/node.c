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
#include <assert.h>

const lima_gp_ir_op_t lima_gp_ir_op[] = {
	{
		.name = "mov",
		.num_sched_positions = 6,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "mul",
		.num_sched_positions = 2,
		.can_negate_dest = true,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "select",
		.num_sched_positions = 1,
		.can_negate_dest = true,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "complex1",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "complex2",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "add",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, true, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "floor",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "sign",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "ge",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "lt",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "min",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, true, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "max",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {true, true, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "neg",
		.num_sched_positions = 4,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "clamp_const",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_clamp_const
	},
	{
		.name = "preexp2",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "postlog2",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "exp2_impl",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "log2_impl",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "rcp_impl",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "rsqrt_impl",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "load_uniform",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_load
	},
	{
		.name = "load_temp",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_load
	},
	{
		.name = "load_attribute",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_load
	},
	{
		.name = "virt_reg",
		.num_sched_positions = 2,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_load_reg
	},
	{
		.name = "store_temp",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_store
	},
	{
		.name = "def_virt_reg",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_store_reg,
	},
	{
		.name = "store_varying",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_store
	},
	{
		.name = "store_off0",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_store
	},
	{
		.name = "store_off1",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_store
	},
	{
		.name = "store_off2",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_store
	},
	{
		.name = "branch",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_branch
	},
	{
		.name = "inline_const",
		.num_sched_positions = 1,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_const
	},
	{
		.name = "exp2",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "log2",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "rcp",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "rsqrt",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "ceil",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "fract",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "exp",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "log",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "pow",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "sqrt",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "sin",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "cos",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "tan",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {true, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_alu
	},
	{
		.name = "branch",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = true,
		.type = lima_gp_ir_node_type_branch
	},
	{
		.name = "phi",
		.num_sched_positions = 0,
		.can_negate_dest = false,
		.can_negate_sources = {false, false, false},
		.is_root_node = false,
		.type = lima_gp_ir_node_type_phi
	}
};

void lima_gp_ir_print_tabs(unsigned tabs)
{
	unsigned i;
	for (i = 0; i < tabs; i++)
		printf("\t");
}

lima_gp_ir_node_t* lima_gp_ir_node_create(lima_gp_ir_op_e op)
{
	switch (lima_gp_ir_op[op].type)
	{
		case lima_gp_ir_node_type_alu:
		{
			lima_gp_ir_alu_node_t* node = lima_gp_ir_alu_node_create(op);
			if (!node)
				return NULL;
			return &node->node;
		}
			
		case lima_gp_ir_node_type_clamp_const:
		{
			lima_gp_ir_clamp_const_node_t* node =
				lima_gp_ir_clamp_const_node_create();
			if (!node)
				return NULL;
			return &node->node;
		}
			
		case lima_gp_ir_node_type_const:
		{
			lima_gp_ir_const_node_t* node = lima_gp_ir_const_node_create();
			if (!node)
				return NULL;
			
			return &node->node;
		}
			
		case lima_gp_ir_node_type_load:
		{
			lima_gp_ir_load_node_t* node = lima_gp_ir_load_node_create(op);
			if (!node)
				return NULL;
			return &node->node;
		}
			
		case lima_gp_ir_node_type_load_reg:
		{
			lima_gp_ir_load_reg_node_t* node =
				lima_gp_ir_load_reg_node_create();
			if (!node)
				return NULL;
			return &node->node;
		}
			
		case lima_gp_ir_node_type_store:
		{
			lima_gp_ir_store_node_t* node = lima_gp_ir_store_node_create(op);
			if (!node)
				return NULL;
			return &node->root_node.node;
		}
			
		case lima_gp_ir_node_type_store_reg:
		{
			lima_gp_ir_store_reg_node_t* node =
				lima_gp_ir_store_reg_node_create();
			if (!node)
				return NULL;
			return &node->root_node.node;
		}
			
		case lima_gp_ir_node_type_branch:
		{
			lima_gp_ir_branch_node_t* node = lima_gp_ir_branch_node_create(op);
			if (!node)
				return NULL;
			return &node->root_node.node;
		}
			
		case lima_gp_ir_node_type_phi:
		{
			lima_gp_ir_phi_node_t* node = lima_gp_ir_phi_node_create(0);
			if (!node)
				return NULL;
			return &node->node;
		}
			
		default:
			break;
	}
	
	return NULL;
}

static bool node_init(lima_gp_ir_node_t* node, lima_gp_ir_op_e op)
{
	node->op = op;
	node->successor = NULL;
	
	if (!ptrset_create(&node->parents))
		return false;
	
	if (!ptrset_create(&node->succs))
		return false;
	
	if (!ptrset_create(&node->preds))
		return false;
	
	return true;
}

/*
 * Orders two root nodes, assuming they are not equal and in the same block.
 * return true if node1 is first, and false if node2 is first.
 */
static bool root_node_order(lima_gp_ir_root_node_t* node1,
							lima_gp_ir_root_node_t* node2)
{
	struct list* start = &node1->block->node_list;
	struct list* end = node1->block->node_list.prev;
	
	struct list* node1_forward = &node1->node_list;
	struct list* node1_backward = &node1->node_list;
	struct list* node2_forward = &node2->node_list;
	struct list* node2_backward = &node2->node_list;
	
	while (true)
	{
		// Move forward one iteration
		node1_forward = node1_forward->next;
		node1_backward = node1_backward->prev;
		node2_forward = node2_forward->next;
		node2_backward = node2_backward->prev;
		
		
		if (node1_forward == node2_backward ||
			node1_forward == node2_backward->next)
		{
			// Node1 going forwards has passed node2 going backwards, so node1
			// must be before node2
			return true;
		}
		
		if (node2_forward == node1_backward ||
			node2_forward == node1_backward->next)
			return false;
		
		if (node1_forward == end)
		{
			// We've reached the end without passing by node2, so node1 must be
			// last.
			return false;
		}
		if (node1_backward == start)
			return true;
		if (node2_forward == end)
			return true;
		if (node2_backward == start)
			return false;
	}
	
	return false;
}

static void add_parent_successor(lima_gp_ir_node_t* node,
								 lima_gp_ir_root_node_t* successor)
{
	node->successor = successor;
	
	lima_gp_ir_child_node_iter_t iter;
	gp_ir_node_for_each_child(node, iter)
	{
		if ((*iter.child)->successor == NULL ||
			((*iter.child)->successor != successor &&
			root_node_order(successor, (*iter.child)->successor)))
		{
			add_parent_successor(*iter.child, successor);
		}
	}
}

void lima_gp_ir_node_link(lima_gp_ir_node_t* parent,
						  lima_gp_ir_node_t* child)
{
	if (child->op == lima_gp_ir_op_load_reg)
	{
		lima_gp_ir_load_reg_node_t* load_reg_node =
			gp_ir_node_to_load_reg(child);
		lima_gp_ir_reg_t* reg = load_reg_node->reg;
		ptrset_add(&reg->uses, child);
	}
	
	ptrset_add(&child->parents, parent);
	
	// Update child's successor recursively, but only if we know the parent's
	// successor. The parent may not be linked yet, in which case the child's
	// successor will be updated when the parent is linked (i.e. called as the
	// child argument to lima_gp_ir_node_link())
	if (parent->successor != NULL &&
		(child->successor == NULL ||
		(child->successor != parent->successor &&
		root_node_order(parent->successor, child->successor))))
	{
		add_parent_successor(child, parent->successor);
	}
}

static bool has_successor(lima_gp_ir_node_t* node,
						  lima_gp_ir_root_node_t* successor)
{
	ptrset_iter_t parent_iter = ptrset_iter_create(node->parents);
	lima_gp_ir_node_t* parent;
	ptrset_iter_for_each(parent_iter, parent)
	{
		if (parent->successor == successor)
			return true;
	}
	
	return false;
}

static void remove_parent_successor(lima_gp_ir_node_t* node,
									lima_gp_ir_root_node_t* successor)
{
	lima_gp_ir_root_node_t* orig_successor = successor;
	
	if (ptrset_size(node->parents) == 1)
	{
		// Special case: if there's only one remaining parent, the successor
		// for the child node must be the same as its parent
		lima_gp_ir_node_t* new_parent = ptrset_first(node->parents);
		node->successor = new_parent->successor;
	}
	else
	{
		while (!has_successor(node, successor))
			successor = gp_ir_root_node_next(successor);
		node->successor = successor;
	}
	
	if (node->successor != orig_successor)
	{
		//We updated the successor for this node, so in case any child nodes
		//were getting their successor from this node, then we need to update
		//them recursively
		lima_gp_ir_child_node_iter_t iter;
		gp_ir_node_for_each_child(node, iter)
		{
			if ((*iter.child)->successor == orig_successor)
				remove_parent_successor(*iter.child, orig_successor);
		}
	}
}

void lima_gp_ir_node_unlink(lima_gp_ir_node_t* parent,
							lima_gp_ir_node_t* child)
{
	ptrset_remove(&child->parents, parent);
	if (ptrset_size(child->parents) == 0)
	{
		// The child node has no parents/uses, so we can delete it
		lima_gp_ir_node_delete(child);
		return;
	}
	
	if (child->successor == parent->successor)
	{
		// This child may have gotten its successor from the parent node we're
		// removing, so we may have to recalculate it (find the next earliest
		// successor out of the remaining parents), recursively updating child
		// nodes
		remove_parent_successor(child, parent->successor);
	}
}

void lima_gp_ir_node_replace_child(lima_gp_ir_node_t* parent,
								   lima_gp_ir_node_t* old_child,
								   lima_gp_ir_node_t* new_child)
{
	lima_gp_ir_node_link(parent, new_child);
	lima_gp_ir_node_unlink(parent, old_child);
	
	lima_gp_ir_child_node_iter_t child_iter;
	gp_ir_node_for_each_child(parent, child_iter)
	{
		if (*child_iter.child == old_child)
			*child_iter.child = new_child;
	}
}

bool lima_gp_ir_node_replace(lima_gp_ir_node_t* old_node,
							 lima_gp_ir_node_t* new_node)
{
	/* We can't use the old_node's list of parents around since it's going
	 * to be deleted, so save a copy and iterate on that
	 */
	
	ptrset_t parents;
	if (!ptrset_copy(&parents, old_node->parents))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(parents);
	lima_gp_ir_node_t* parent;
	ptrset_iter_for_each(iter, parent)
	{
		lima_gp_ir_node_replace_child(parent, old_node, new_node);
	}
	
	ptrset_delete(parents);
	return true;
}

void lima_gp_ir_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	if (ptrset_size(node->parents) > 1)
	{
		//Nodes with more than one parent must be represented as expressions
		lima_gp_ir_print_tabs(tabs);
		printf("(expr expr_%u)", node->index);
	}
	else
		node->print(node, tabs);
}

void lima_gp_ir_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_child_node_iter_t iter;
	ptrset_t children;
	ptrset_create(&children);
	gp_ir_node_for_each_child(node, iter)
	{
		ptrset_add(&children, *iter.child);
	}
	
	ptrset_iter_t ptrset_iter = ptrset_iter_create(children);
	lima_gp_ir_node_t* child;
	ptrset_iter_for_each(ptrset_iter, child)
	{
		lima_gp_ir_node_unlink(node, child);
	}
	
	ptrset_delete(children);
	
	ptrset_delete(node->parents);
	
	ptrset_iter = ptrset_iter_create(node->succs);
	lima_gp_ir_dep_info_t* dep_info;
	ptrset_iter_for_each(ptrset_iter, dep_info)
	{
		ptrset_remove(&dep_info->succ->preds, dep_info);
		free(dep_info);
	}
	ptrset_delete(node->succs);
	
	ptrset_iter = ptrset_iter_create(node->preds);
	ptrset_iter_for_each(ptrset_iter, dep_info)
	{
		ptrset_remove(&dep_info->pred->succs, dep_info);
		free(dep_info);
	}
	ptrset_delete(node->preds);
	
	node->delete(node);
}

/* TODO: make this more efficient/scalable by allocating the stack the heap and
 * handling it manually */

static bool node_dfs_impl(lima_gp_ir_node_t* node,
						  lima_gp_ir_node_traverse_cb preorder,
						  lima_gp_ir_node_traverse_cb postorder,
						  ptrset_t* visited,
						  void* state)
{
	if (preorder)
		if (!preorder(node, state))
			return false;
	
	lima_gp_ir_child_node_iter_t iter;
	gp_ir_node_for_each_child(node, iter)
	{
		if ((*iter.child)->successor == node->successor &&
			!ptrset_contains(*visited, *iter.child))
			if (!node_dfs_impl(*iter.child, preorder, postorder, visited,
							   state))
				return false;
	}
	
	if (postorder)
		if (!postorder(node, state))
			return false;
	
	ptrset_add(visited, node);
	return true;
}

bool lima_gp_ir_node_dfs(lima_gp_ir_node_t* node,
						 lima_gp_ir_node_traverse_cb preorder,
						 lima_gp_ir_node_traverse_cb postorder,
						 void* state)
{
	ptrset_t visited;
	if (!ptrset_create(&visited))
		return false;
	
	bool ret = node_dfs_impl(node, preorder, postorder, &visited, state);
	ptrset_delete(visited);
	return ret;
}


unsigned lima_gp_ir_alu_node_num_children(lima_gp_ir_op_e op)
{
	switch (op)
	{
		case lima_gp_ir_op_mov:
		case lima_gp_ir_op_floor:
		case lima_gp_ir_op_sign:
		case lima_gp_ir_op_neg:
		case lima_gp_ir_op_preexp2:
		case lima_gp_ir_op_postlog2:
		case lima_gp_ir_op_exp2_impl:
		case lima_gp_ir_op_log2_impl:
		case lima_gp_ir_op_rcp_impl:
		case lima_gp_ir_op_rsqrt_impl:
		case lima_gp_ir_op_exp2:
		case lima_gp_ir_op_log2:
		case lima_gp_ir_op_complex2:
		case lima_gp_ir_op_rcp:
		case lima_gp_ir_op_rsqrt:
		case lima_gp_ir_op_ceil:
		case lima_gp_ir_op_fract:
		case lima_gp_ir_op_exp:
		case lima_gp_ir_op_log:
		case lima_gp_ir_op_sqrt:
		case lima_gp_ir_op_sin:
		case lima_gp_ir_op_cos:
		case lima_gp_ir_op_tan:
			return 1;
			
		case lima_gp_ir_op_add:
		case lima_gp_ir_op_mul:
		case lima_gp_ir_op_ge:
		case lima_gp_ir_op_lt:
		case lima_gp_ir_op_min:
		case lima_gp_ir_op_max:
		case lima_gp_ir_op_pow:
			return 2;
			
		case lima_gp_ir_op_select:
		case lima_gp_ir_op_complex1:
			return 3;
			
		default:
			assert(0);
			break;
	}
	
	return 0;
}

static lima_gp_ir_child_node_iter_t alu_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	lima_gp_ir_child_node_iter_t child_iter;
	lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(parent);
	
	child_iter.child = &alu_node->children[0];
	child_iter.at_end = false;
	child_iter.parent = parent;
	child_iter.child_index = 0;
	return child_iter;
}

static void alu_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	iter->child_index++;
	if (iter->child_index == lima_gp_ir_alu_node_num_children(iter->parent->op))
		iter->at_end = true;
	else
	{
		lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(iter->parent);
		iter->child = &alu_node->children[iter->child_index];
	}
}

static void alu_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(");
	if (alu_node->dest_negate)
		printf("-");
	printf("%s", lima_gp_ir_op[node->op].name);
	
	unsigned i;
	for (i = 0; i < lima_gp_ir_alu_node_num_children(node->op); i++)
	{
		printf("\n");
		if (alu_node->children_negate[i])
			printf("-");
		lima_gp_ir_node_print(alu_node->children[i], tabs + 1);
	}
	
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t child_index[3];
	bool dest_negate   : 1;
	bool child0_negate : 1;
	bool child1_negate : 1;
	bool child2_negate : 1;
} alu_node_data_t;

static void* alu_node_export(lima_gp_ir_node_t* node, lima_gp_ir_block_t* block,
							 unsigned* size)
{
	(void) block;
	
	lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
	
	*size = sizeof(alu_node_data_t);
	
	alu_node_data_t* data = malloc(sizeof(alu_node_data_t));
	if (!data)
		return NULL;
	
	data->header.size = sizeof(alu_node_data_t);
	data->header.op = node->op;
	
	unsigned i;
	for (i = 0; i < lima_gp_ir_alu_node_num_children(node->op); i++)
	{
		data->child_index[i] = alu_node->children[i]->index;
	}
	data->dest_negate = alu_node->dest_negate;
	data->child0_negate = alu_node->children_negate[0];
	data->child1_negate = alu_node->children_negate[1];
	data->child2_negate = alu_node->children_negate[2];
	
	return (void*) data;
}

static bool alu_node_import(lima_gp_ir_node_t* node, lima_gp_ir_node_t** nodes,
							lima_gp_ir_block_t* block, void* _data)
{
	(void) block;
	
	lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
	
	alu_node_data_t* data = _data;
	
	unsigned i;
	for (i = 0; i < lima_gp_ir_alu_node_num_children(node->op); i++)
		alu_node->children[i] = nodes[data->child_index[i]];
	
	alu_node->dest_negate = data->dest_negate;
	alu_node->children_negate[0] = data->child0_negate;
	alu_node->children_negate[1] = data->child1_negate;
	alu_node->children_negate[2] = data->child2_negate;
	
	return true;
}

static void alu_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_alu_node_t* alu_node = gp_ir_node_to_alu(node);
	free(alu_node);
}

lima_gp_ir_alu_node_t* lima_gp_ir_alu_node_create(lima_gp_ir_op_e op)
{
	lima_gp_ir_alu_node_t* alu_node = malloc(sizeof(lima_gp_ir_alu_node_t));
	if (!alu_node)
		return NULL;
	if (!node_init(&alu_node->node, op))
	{
		free(alu_node);
		return NULL;
	}
	
	alu_node->dest_negate = false;
	alu_node->children_negate[0] = false;
	alu_node->children_negate[1] = false;
	alu_node->children_negate[2] = false;
	
	alu_node->node.delete = alu_node_delete;
	alu_node->node.print = alu_node_print;
	alu_node->node.export_node = alu_node_export;
	alu_node->node.import = alu_node_import;
	alu_node->node.child_iter_create = alu_node_child_iter_create;
	alu_node->node.child_iter_next = alu_node_child_iter_next;
	
	return alu_node;
}


static void clamp_const_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		gp_ir_node_to_clamp_const(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(clamp_const ");
	if (clamp_const_node->is_inline_const)
		printf("inline %f %f\n", clamp_const_node->low, clamp_const_node->high);
	else
		printf("%u\n", clamp_const_node->uniform_index);
	lima_gp_ir_node_print(clamp_const_node->child, tabs + 1);
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t uniform_index;
	float low, high;
	uint32_t child_index;
	bool is_inline_const : 1;
} clamp_const_node_data_t;

static void* clamp_const_node_export(lima_gp_ir_node_t* node,
									 lima_gp_ir_block_t* block,
									 unsigned* size)
{
	(void) block;
	
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		gp_ir_node_to_clamp_const(node);
	
	clamp_const_node_data_t* data = malloc(sizeof(clamp_const_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(clamp_const_node_data_t);
	
	data->header.size = sizeof(clamp_const_node_data_t);
	data->header.op = node->op;
	data->uniform_index = clamp_const_node->uniform_index;
	data->low = clamp_const_node->low;
	data->high = clamp_const_node->high;
	data->child_index = clamp_const_node->child->index;
	data->is_inline_const = clamp_const_node->is_inline_const;
	
	return (void*) data;
}

static bool clamp_const_node_import(lima_gp_ir_node_t* node,
									lima_gp_ir_node_t** nodes,
									lima_gp_ir_block_t* block, void* _data)
{
	(void) block;
	
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		gp_ir_node_to_clamp_const(node);
	
	clamp_const_node_data_t* data = _data;
	
	clamp_const_node->uniform_index = data->uniform_index;
	clamp_const_node->low = data->low;
	clamp_const_node->high = data->high;
	clamp_const_node->is_inline_const = data->is_inline_const;
	clamp_const_node->child = nodes[data->child_index];
	
	return true;
}

static lima_gp_ir_child_node_iter_t clamp_const_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	lima_gp_ir_child_node_iter_t child_iter;
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		gp_ir_node_to_clamp_const(parent);
	
	child_iter.parent = parent;
	child_iter.child = &clamp_const_node->child;
	child_iter.child_index = 0;
	child_iter.at_end = false;
	return child_iter;
}

static void clamp_const_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	iter->at_end = true;
}

static void clamp_const_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		gp_ir_node_to_clamp_const(node);
	
	free(clamp_const_node);
}

lima_gp_ir_clamp_const_node_t* lima_gp_ir_clamp_const_node_create()
{
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		malloc(sizeof(lima_gp_ir_clamp_const_node_t));
	
	if (!node_init(&clamp_const_node->node, lima_gp_ir_op_clamp_const))
	{
		free(clamp_const_node);
		return NULL;
	}
	
	clamp_const_node->low = clamp_const_node->high = 0.0f;
	clamp_const_node->is_inline_const = true;
	
	clamp_const_node->node.delete = clamp_const_node_delete;
	clamp_const_node->node.print = clamp_const_node_print;
	clamp_const_node->node.export_node = clamp_const_node_export;
	clamp_const_node->node.import = clamp_const_node_import;
	clamp_const_node->node.child_iter_create = clamp_const_child_iter_create;
	clamp_const_node->node.child_iter_next = clamp_const_child_iter_next;
	return clamp_const_node;
}


static void const_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_const_node_t* const_node = gp_ir_node_to_const(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(inline_const %f)", const_node->constant);
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	float constant;
} const_node_data_t;

static void* const_node_export(lima_gp_ir_node_t* node,
							   lima_gp_ir_block_t* block,
							   unsigned* size)
{
	(void) block;
	
	lima_gp_ir_const_node_t* const_node = gp_ir_node_to_const(node);
	
	const_node_data_t* data = malloc(sizeof(const_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(const_node_data_t);
	
	data->header.size = sizeof(const_node_data_t);
	data->header.op = node->op;
	data->constant = const_node->constant;
	
	return (void*) data;
}

static bool const_node_import(lima_gp_ir_node_t* node,
							  lima_gp_ir_node_t** nodes,
							  lima_gp_ir_block_t* block, void* _data)
{
	(void) nodes;
	(void) block;
	
	lima_gp_ir_const_node_t* const_node = gp_ir_node_to_const(node);
	
	const_node_data_t* data = _data;
	
	const_node->constant = data->constant;
	
	return true;
}

static lima_gp_ir_child_node_iter_t const_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	(void) parent;
	
	lima_gp_ir_child_node_iter_t child_iter;
	
	child_iter.at_end = true;
	return child_iter;
}

static void const_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	iter->at_end = true;
}

static void const_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_const_node_t* const_node =
		gp_ir_node_to_const(node);
	
	free(const_node);
}

lima_gp_ir_const_node_t* lima_gp_ir_const_node_create()
{
	lima_gp_ir_const_node_t* const_node =
		malloc(sizeof(lima_gp_ir_const_node_t));
	
	if (!node_init(&const_node->node, lima_gp_ir_op_const))
	{
		free(const_node);
		return NULL;
	}
	
	const_node->node.delete = const_node_delete;
	const_node->node.print = const_node_print;
	const_node->node.export_node = const_node_export;
	const_node->node.import = const_node_import;
	const_node->node.child_iter_create = const_child_iter_create;
	const_node->node.child_iter_next = const_child_iter_next;
	return const_node;
}


static bool root_node_init(lima_gp_ir_root_node_t* root_node,
						   lima_gp_ir_op_e op)
{
	if (!node_init(&root_node->node, op))
		return false;
	
	root_node->node.successor = root_node;
	
	root_node->live_phys_after = bitset_create(16*4);
	
	//Variably sized, has to be created before live variable analysis
	root_node->live_virt_after = bitset_create(0);
	
	return true;
}

static void root_node_cleanup(lima_gp_ir_root_node_t* root_node)
{
	bitset_delete(root_node->live_phys_after);
	bitset_delete(root_node->live_virt_after);
}


static void load_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(node);
	
	lima_gp_ir_print_tabs(tabs);
	
	const char* c = "xyzw";
	printf("(%s %u.%c", lima_gp_ir_op[node->op].name, load_node->index,
		   c[load_node->component]);
	
	if (load_node->offset)
		printf(" off_reg: %u", load_node->off_reg);
	
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t index, component, off_reg;
	bool offset : 1;
} load_node_data_t;

static void* load_node_export(lima_gp_ir_node_t* node,
							  lima_gp_ir_block_t* block,
							  unsigned* size)
{
	(void) block;
	
	lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(node);
	
	load_node_data_t* data = malloc(sizeof(load_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(load_node_data_t);
	
	data->header.size = sizeof(load_node_data_t);
	data->header.op = node->op;
	data->index = load_node->index;
	data->component = load_node->component;
	data->off_reg = load_node->off_reg;
	data->offset = load_node->offset;
	
	return (void*) data;
}

static bool load_node_import(lima_gp_ir_node_t* node, lima_gp_ir_node_t** nodes,
							lima_gp_ir_block_t* block, void* _data)
{
	(void) block;
	(void) nodes;
	
	lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(node);
	
	load_node_data_t* data = _data;
	
	load_node->index = data->index;
	load_node->component = data->component;
	load_node->off_reg = data->off_reg;
	load_node->offset = data->offset;
	
	return true;
}

static lima_gp_ir_child_node_iter_t load_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	(void) parent;
	lima_gp_ir_child_node_iter_t child_iter;
	child_iter.at_end = true;
	return child_iter;
}

static void load_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	iter->at_end = true;
}

static void load_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(node);
	free(load_node);
}

lima_gp_ir_load_node_t* lima_gp_ir_load_node_create(lima_gp_ir_op_e op)
{
	lima_gp_ir_load_node_t* load_node = malloc(sizeof(lima_gp_ir_load_node_t));
	if (!load_node)
		return NULL;
	if (!node_init(&load_node->node, op))
	{
		free(load_node);
		return NULL;
	}
	
	load_node->node.child_iter_create = load_node_child_iter_create;
	load_node->node.child_iter_next = load_node_child_iter_next;
	load_node->node.export_node = load_node_export;
	load_node->node.import = load_node_import;
	load_node->node.print = load_node_print;
	load_node->node.delete = load_node_delete;
	
	return load_node;
}


static void load_reg_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(virt_reg reg_%u", load_reg_node->reg->index);
	if (load_reg_node->offset)
	{
		printf("\n");
		lima_gp_ir_node_print(load_reg_node->offset, tabs + 1);
	}
	
	const char* c = "xyzw";
	printf(".%c)", c[load_reg_node->component]);
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t reg_index, component, offset_index;
	bool offset : 1;
} load_reg_node_data_t;

static void* load_reg_node_export(lima_gp_ir_node_t* node,
								  lima_gp_ir_block_t* block,
								  unsigned* size)
{
	(void) block;
	
	load_reg_node_data_t* data = malloc(sizeof(load_reg_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(load_reg_node_data_t);
	
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	
	data->header.size = sizeof(load_reg_node_data_t);
	data->header.op = node->op;
	data->reg_index = load_reg_node->reg->index;
	data->component = load_reg_node->component;
	data->offset = !!(load_reg_node->offset);
	if (load_reg_node->offset)
		data->offset_index = load_reg_node->offset->index;
	
	return (void*) data;
}

static bool load_reg_node_import(lima_gp_ir_node_t* node,
								 lima_gp_ir_node_t** nodes,
								 lima_gp_ir_block_t* block, void* _data)
{
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	
	load_reg_node_data_t* data = _data;
	
	load_reg_node->reg = lima_gp_ir_reg_find(block->prog, data->reg_index);
	if (!load_reg_node->reg)
		return false;
	load_reg_node->component = data->component;
	if (data->offset)
		load_reg_node->offset = nodes[data->offset_index];
	else
		load_reg_node->offset = NULL;
	
	return true;
	
}

static lima_gp_ir_child_node_iter_t load_reg_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	lima_gp_ir_child_node_iter_t child_iter;
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(parent);
	
	if (load_reg_node->offset)
	{
		child_iter.parent = parent;
		child_iter.child = &load_reg_node->offset;
		child_iter.child_index = 0;
		child_iter.at_end = false;
	}
	else
		child_iter.at_end = true;
	
	return child_iter;
}

static void load_reg_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	iter->at_end = true;
}

static void load_reg_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	
	ptrset_remove(&load_reg_node->reg->uses, node);
	free(load_reg_node);
}


lima_gp_ir_load_reg_node_t* lima_gp_ir_load_reg_node_create(void)
{
	lima_gp_ir_load_reg_node_t* load_reg_node =
		malloc(sizeof(lima_gp_ir_load_reg_node_t));
	if (!load_reg_node)
		return NULL;
	if (!node_init(&load_reg_node->node, lima_gp_ir_op_load_reg))
	{
		free(load_reg_node);
		return NULL;
	}
	
	load_reg_node->node.child_iter_create = load_reg_node_child_iter_create;
	load_reg_node->node.child_iter_next = load_reg_node_child_iter_next;
	load_reg_node->node.export_node = load_reg_node_export;
	load_reg_node->node.import = load_reg_node_import;
	load_reg_node->node.print = load_reg_node_print;
	load_reg_node->node.delete = load_reg_node_delete;
	load_reg_node->offset = NULL;
	
	return load_reg_node;
}


static void store_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(%s", lima_gp_ir_op[node->op].name);
	
	if (node->op != lima_gp_ir_op_store_temp_load_off0 &&
		node->op != lima_gp_ir_op_store_temp_load_off1 &&
		node->op != lima_gp_ir_op_store_temp_load_off2)
	{
		if (node->op == lima_gp_ir_op_store_temp)
		{
			printf("\n");
			lima_gp_ir_node_print(store_node->addr, tabs + 1);
		}
		else
		{
			printf(" %u", store_node->index);
		}
	}
	
	const char* c = "xyzw";
	unsigned i;
	for (i = 0; i < 4; i++)
		if (store_node->mask[i])
		{
			printf("\n");
			
			if (node->op != lima_gp_ir_op_store_temp_load_off0 &&
				node->op != lima_gp_ir_op_store_temp_load_off1 &&
				node->op != lima_gp_ir_op_store_temp_load_off2)
			{
				lima_gp_ir_print_tabs(tabs + 1);
				printf("%c:\n", c[i]);
			}
			
			lima_gp_ir_node_print(store_node->children[i], tabs + 1);
		}
	
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t index, children[4], addr_index;
	bool addr : 1;
	bool mask_0 : 1;
	bool mask_1 : 1;
	bool mask_2 : 1;
	bool mask_3 : 1;
} store_node_data_t;

static void* store_node_export(lima_gp_ir_node_t* node,
							   lima_gp_ir_block_t* block,
							   unsigned* size)
{
	(void) block;
	
	lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
	
	store_node_data_t* data = malloc(sizeof(store_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(store_node_data_t);
	
	data->header.size = sizeof(store_node_data_t);
	data->header.op = node->op;
	data->index = store_node->index;
	
	data->addr = !!(store_node->addr);
	if (store_node->addr)
		data->addr_index = store_node->addr->index;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		if (store_node->mask[i])
			data->children[i] = store_node->children[i]->index;
	
	data->mask_0 = store_node->mask[0];
	data->mask_1 = store_node->mask[1];
	data->mask_2 = store_node->mask[2];
	data->mask_3 = store_node->mask[3];
	
	return (void*) data;
}

static bool store_node_import(lima_gp_ir_node_t* node,
							  lima_gp_ir_node_t** nodes,
							  lima_gp_ir_block_t* block, void* _data)
{
	(void) block;
	
	lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
	
	store_node_data_t* data = _data;
	
	if (data->addr)
		store_node->addr = nodes[data->addr_index];
	else
		store_node->addr = NULL;
	
	store_node->index = data->index;
	
	store_node->mask[0] = data->mask_0;
	store_node->mask[1] = data->mask_1;
	store_node->mask[2] = data->mask_2;
	store_node->mask[3] = data->mask_3;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		if (store_node->mask[i])
			store_node->children[i] = nodes[data->children[i]];
	
	return true;
}

static lima_gp_ir_child_node_iter_t store_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	lima_gp_ir_child_node_iter_t iter;
	lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(parent);
	
	iter.parent = parent;
	for (iter.child_index = 0; iter.child_index < 4; iter.child_index++)
	{
		if (store_node->mask[iter.child_index])
		{
			iter.child = &store_node->children[iter.child_index];
			iter.at_end = false;
			return iter;
		}
	}
	
	iter.at_end = true;
	return iter;
}

static void store_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(iter->parent);
	
	for (iter->child_index++; iter->child_index < 4; iter->child_index++)
	{
		if (store_node->mask[iter->child_index])
		{
			iter->child = &store_node->children[iter->child_index];
			return;
		}
	}
	
	if (iter->child_index == 4 && store_node->addr)
		iter->child = &store_node->addr;
	else
		iter->at_end = true;
}

static void store_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
	root_node_cleanup(&store_node->root_node);
	free(store_node);
}

lima_gp_ir_store_node_t* lima_gp_ir_store_node_create(lima_gp_ir_op_e op)
{
	lima_gp_ir_store_node_t* store_node =
		malloc(sizeof(lima_gp_ir_store_node_t));
	if (!store_node)
		return NULL;
	
	if (!root_node_init(&store_node->root_node, op))
	{
		free(store_node);
		return NULL;
	}
	
	store_node->mask[0] = false;
	store_node->mask[1] = false;
	store_node->mask[2] = false;
	store_node->mask[3] = false;
	
	store_node->root_node.node.child_iter_create = store_node_child_iter_create;
	store_node->root_node.node.child_iter_next = store_node_child_iter_next;
	store_node->root_node.node.export_node = store_node_export;
	store_node->root_node.node.import = store_node_import;
	store_node->root_node.node.print = store_node_print;
	store_node->root_node.node.delete = store_node_delete;
	store_node->addr = NULL;
	
	return store_node;
}


static void print_reg_type(lima_gp_ir_reg_t* reg)
{
	const char* sizes[] = {
		"float",
		"vec2",
		"vec3",
		"vec4"
	};
	
	printf("<%s>", sizes[reg->size - 1]);
}

static void store_reg_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_store_reg_node_t* store_reg_node = gp_ir_node_to_store_reg(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(store_virt_reg ");
	print_reg_type(store_reg_node->reg);
	printf(" reg_%u", store_reg_node->reg->index);
	
	const char* c = "xyzw";
	unsigned i;
	for (i = 0; i < 4; i++)
		if (store_reg_node->mask[i])
		{
			printf("\n");
			lima_gp_ir_print_tabs(tabs + 1);
			printf("%c:\n", c[i]);
			lima_gp_ir_node_print(store_reg_node->children[i], tabs + 1);
		}
	
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t reg_index, children[4];
	bool mask_0 : 1;
	bool mask_1 : 1;
	bool mask_2 : 1;
	bool mask_3 : 1;
} store_reg_node_data_t;

static void* store_reg_node_export(lima_gp_ir_node_t* node,
								   lima_gp_ir_block_t* block,
								   unsigned* size)
{
	(void) block;
	
	lima_gp_ir_store_reg_node_t* store_reg_node = gp_ir_node_to_store_reg(node);
	
	store_reg_node_data_t* data = malloc(sizeof(store_reg_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(store_reg_node_data_t);
	
	data->header.size = sizeof(store_reg_node_data_t);
	data->header.op = node->op;
	data->reg_index = store_reg_node->reg->index;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		if (store_reg_node->mask[i])
			data->children[i] = store_reg_node->children[i]->index;
	
	data->mask_0 = store_reg_node->mask[0];
	data->mask_1 = store_reg_node->mask[1];
	data->mask_2 = store_reg_node->mask[2];
	data->mask_3 = store_reg_node->mask[3];
	
	return (void*) data;
}

static bool store_reg_node_import(lima_gp_ir_node_t* node,
								  lima_gp_ir_node_t** nodes,
								  lima_gp_ir_block_t* block, void* _data)
{
	lima_gp_ir_store_reg_node_t* store_reg_node = gp_ir_node_to_store_reg(node);
	
	store_reg_node_data_t* data = _data;
	
	store_reg_node->reg = lima_gp_ir_reg_find(block->prog, data->reg_index);
	if (!store_reg_node->reg)
		return false;
	
	store_reg_node->mask[0] = data->mask_0;
	store_reg_node->mask[1] = data->mask_1;
	store_reg_node->mask[2] = data->mask_2;
	store_reg_node->mask[3] = data->mask_3;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		if (store_reg_node->mask[i])
			store_reg_node->children[i] = nodes[data->children[i]];
	
	return true;
}

static lima_gp_ir_child_node_iter_t store_reg_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	lima_gp_ir_child_node_iter_t iter;
	lima_gp_ir_store_reg_node_t* store_reg_node = gp_ir_node_to_store_reg(parent);
	
	iter.parent = parent;
	for (iter.child_index = 0; iter.child_index < 4; iter.child_index++)
	{
		if (store_reg_node->mask[iter.child_index])
		{
			iter.child = &store_reg_node->children[iter.child_index];
			iter.at_end = false;
			return iter;
		}
	}
	
	iter.at_end = true;
	return iter;
}

static void store_reg_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	lima_gp_ir_store_reg_node_t* store_reg_node =
		gp_ir_node_to_store_reg(iter->parent);
	
	for (iter->child_index++; iter->child_index < 4; iter->child_index++)
	{
		if (store_reg_node->mask[iter->child_index])
		{
			iter->child = &store_reg_node->children[iter->child_index];
			return;
		}
	}
	iter->at_end = true;
}

static void store_reg_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_store_reg_node_t* store_reg_node = gp_ir_node_to_store_reg(node);
	
	ptrset_remove(&store_reg_node->reg->defs, node);
	root_node_cleanup(&store_reg_node->root_node);
	free(store_reg_node);
}

lima_gp_ir_store_reg_node_t* lima_gp_ir_store_reg_node_create(void)
{
	lima_gp_ir_store_reg_node_t* store_reg_node =
		malloc(sizeof(lima_gp_ir_store_reg_node_t));
	if (!store_reg_node)
		return NULL;
	
	if (!root_node_init(&store_reg_node->root_node, lima_gp_ir_op_store_reg))
	{
		free(store_reg_node);
		return NULL;
	}
	
	store_reg_node->mask[0] = false;
	store_reg_node->mask[1] = false;
	store_reg_node->mask[2] = false;
	store_reg_node->mask[3] = false;
	
	store_reg_node->root_node.node.child_iter_create =
		store_reg_node_child_iter_create;
	store_reg_node->root_node.node.child_iter_next =
		store_reg_node_child_iter_next;
	store_reg_node->root_node.node.export_node = store_reg_node_export;
	store_reg_node->root_node.node.import = store_reg_node_import;
	store_reg_node->root_node.node.print = store_reg_node_print;
	store_reg_node->root_node.node.delete = store_reg_node_delete;
	
	return store_reg_node;
}


static void branch_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_branch_node_t* branch_node = gp_ir_node_to_branch(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(branch block_%u", branch_node->dest->index);
	
	if (branch_node->condition)
	{
		printf("\n");
		lima_gp_ir_node_print(branch_node->condition, tabs + 1);
	}
	
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t dest_index, condition_index;
	bool condition : 1;
} branch_node_data_t;

static lima_gp_ir_block_t* find_block(lima_gp_ir_prog_t* prog, unsigned index)
{
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
		if (block->index == index)
			return block;
	return NULL;
}

static void* branch_node_export(lima_gp_ir_node_t* node,
								lima_gp_ir_block_t* block,
								unsigned* size)
{
	(void) block;
	
	branch_node_data_t* data = malloc(sizeof(branch_node_data_t));
	if (!data)
		return NULL;
	
	*size = sizeof(branch_node_data_t);
	
	lima_gp_ir_branch_node_t* branch_node = gp_ir_node_to_branch(node);
	
	data->header.op = node->op;
	data->header.size = sizeof(branch_node_data_t);
	
	data->dest_index = branch_node->dest->index;
	data->condition = !!(branch_node->condition);
	if (branch_node->condition)
		data->condition_index = branch_node->condition->index;
	
	return (void*) data;
}

static bool branch_node_import(lima_gp_ir_node_t* node,
							   lima_gp_ir_node_t** nodes,
							   lima_gp_ir_block_t* block, void* _data)
{
	branch_node_data_t* data = _data;
	
	lima_gp_ir_branch_node_t* branch_node = gp_ir_node_to_branch(node);
	
	branch_node->dest = find_block(block->prog, data->dest_index);
	if (!branch_node->dest)
		return false;
	if (data->condition)
		branch_node->condition = nodes[data->condition_index];
	else
		branch_node->condition = NULL;
	
	return true;
}

static lima_gp_ir_child_node_iter_t branch_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	lima_gp_ir_child_node_iter_t child_iter;
	lima_gp_ir_branch_node_t* branch_node = gp_ir_node_to_branch(parent);
	
	if (branch_node->condition)
	{
		child_iter.parent = parent;
		child_iter.child = &branch_node->condition;
		child_iter.child_index = 0;
		child_iter.at_end = false;
	}
	else
		child_iter.at_end = true;
	
	return child_iter;
}

static void branch_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	iter->at_end = true;
}

static void branch_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_branch_node_t* branch_node = gp_ir_node_to_branch(node);
	root_node_cleanup(&branch_node->root_node);
	free(branch_node);
}

lima_gp_ir_branch_node_t* lima_gp_ir_branch_node_create(lima_gp_ir_op_e op)
{
	lima_gp_ir_branch_node_t* branch_node =
		malloc(sizeof(lima_gp_ir_branch_node_t));
	if (!branch_node)
		return NULL;
	
	if (!root_node_init(&branch_node->root_node, op))
	{
		free(branch_node);
		return NULL;
	}
	
	branch_node->root_node.node.child_iter_create =
		branch_node_child_iter_create;
	branch_node->root_node.node.child_iter_next = branch_node_child_iter_next;
	branch_node->root_node.node.export_node = branch_node_export;
	branch_node->root_node.node.import = branch_node_import;
	branch_node->root_node.node.print = branch_node_print;
	branch_node->root_node.node.delete = branch_node_delete;
	
	return branch_node;
}


static void phi_node_print(lima_gp_ir_node_t* node, unsigned tabs)
{
	lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(node);
	
	lima_gp_ir_print_tabs(tabs);
	printf("(phi reg_%u\n", phi_node->dest->index);
	
	unsigned i;
	for (i = 0; i < phi_node->num_sources; i++)
	{
		lima_gp_ir_print_tabs(tabs + 1);
		printf("(block_%u reg_%u)", phi_node->sources[i].pred->index,
			   phi_node->sources[i].reg->index);
		if (i != phi_node->num_sources - 1)
			printf("\n");
	}
	printf(")");
}

typedef struct
{
	lima_gp_ir_node_header_t header;
	uint32_t dest_index;
	uint32_t num_sources;
} phi_node_header_t;

typedef struct
{
	uint32_t reg_index;
	uint32_t pred_index;
} phi_node_src_data_t;

static void* phi_node_export(lima_gp_ir_node_t* node,
							 lima_gp_ir_block_t* block,
							 unsigned* size)
{
	(void) block;
	
	lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(node);
	
	*size = sizeof(phi_node_header_t) +
		phi_node->num_sources*sizeof(phi_node_src_data_t);
	void* data = malloc(*size);
	if (!data)
		return NULL;
	
	phi_node_header_t* header = data;
	header->header.op = node->op;
	header->header.size = *size;
	header->dest_index = phi_node->dest->index;
	header->num_sources = phi_node->num_sources;
	
	phi_node_src_data_t* src_data = (phi_node_src_data_t*)(header + 1);
	unsigned i;
	for (i = 0; i < phi_node->num_sources; i++)
	{
		src_data->reg_index = phi_node->sources[i].reg->index;
		src_data->pred_index = phi_node->sources[i].pred->index;
		src_data = src_data + 1;
	}
	
	return data;
}

static bool phi_node_import(lima_gp_ir_node_t* node,
							lima_gp_ir_node_t** nodes,
							lima_gp_ir_block_t* block, void* data)
{
	(void) nodes;
	
	phi_node_header_t* header = data;
	
	lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(node);
	
	phi_node->dest = lima_gp_ir_reg_find(block->prog, header->dest_index);
	phi_node->num_sources = header->num_sources;
	
	phi_node->sources =
		malloc(sizeof(lima_gp_ir_phi_node_src_t)*phi_node->num_sources);
	if (!phi_node->sources)
		return false;
	
	phi_node_src_data_t* src_data =
		(phi_node_src_data_t*)((char*)data + sizeof(phi_node_header_t));
	unsigned i;
	for (i = 0; i < phi_node->num_sources; i++)
	{
		phi_node->sources[i].reg = lima_gp_ir_reg_find(block->prog,
													   src_data->reg_index);
		if (!phi_node->sources[i].reg)
			return false;
		phi_node->sources[i].pred = find_block(block->prog,
											   src_data->pred_index);
		if (!phi_node->sources[i].pred)
			return false;
		src_data = src_data + 1;
	}
	
	return true;
}

static lima_gp_ir_child_node_iter_t phi_node_child_iter_create(
	lima_gp_ir_node_t* parent)
{
	(void) parent;
	
	lima_gp_ir_child_node_iter_t child_iter;
	child_iter.at_end = true;
	return child_iter;
}

static void phi_node_child_iter_next(lima_gp_ir_child_node_iter_t* iter)
{
	(void) iter;
}

static void phi_node_delete(lima_gp_ir_node_t* node)
{
	lima_gp_ir_phi_node_t* phi_node = gp_ir_node_to_phi(node);
	free(phi_node->sources);
	free(phi_node);
}

lima_gp_ir_phi_node_t* lima_gp_ir_phi_node_create(unsigned num_sources)
{
	lima_gp_ir_phi_node_t* phi_node = malloc(sizeof(lima_gp_ir_phi_node_t));
	if (!phi_node)
		return NULL;
	
	if (!node_init(&phi_node->node, lima_gp_ir_op_phi))
	{
		free(phi_node);
		return NULL;
	}
	
	phi_node->num_sources = num_sources;
	if (num_sources)
	{
		phi_node->sources =
			malloc(num_sources * sizeof(lima_gp_ir_phi_node_src_t));
		if (!phi_node->sources)
		{
			free(phi_node);
			return NULL;
		}
	}
	else
		phi_node->sources = NULL;
	
	phi_node->node.child_iter_create = phi_node_child_iter_create;
	phi_node->node.child_iter_next = phi_node_child_iter_next;
	phi_node->node.export_node = phi_node_export;
	phi_node->node.import = phi_node_import;
	phi_node->node.print = phi_node_print;
	phi_node->node.delete = phi_node_delete;
	
	return phi_node;
}


