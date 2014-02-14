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

#include "scheduler.h"
#include <stdlib.h>
#include <assert.h>

static const lima_gp_ir_instr_t empty_instr = {
	.mul_slots = {NULL, NULL},
	.add_slots = {NULL, NULL},
	
	.uniform_slot = {NULL, NULL, NULL, NULL},
	.uniform_slot_num_used = 0,
	
	.attr_reg_slot = {NULL, NULL, NULL, NULL},
	.attr_reg_slot_num_used = 0,
	
	.reg_slot = {NULL, NULL, NULL, NULL},
	.reg_slot_num_used = 0,
	
	.branch_slot = NULL,
	
	.store_slot = {NULL, NULL, NULL, NULL},
	.store_slot_mask = {false, false, false, false},
	.store_slot_num_used = 0,
	.store_slot_is_temp = false,
	.store_slot_is_varying = false,
	.store_slot_index = 0,
	.num_unscheduled_store_children = 0,
	
	.complex_slot = NULL,
	.pass_slot = NULL
};

void lima_gp_ir_instr_init(lima_gp_ir_instr_t* instr)
{
	*instr = empty_instr;
}

lima_gp_ir_instr_t* lima_gp_ir_instr_create(void)
{
	lima_gp_ir_instr_t* instr = malloc(sizeof(lima_gp_ir_instr_t));
	if (!instr)
		return NULL;
	
	lima_gp_ir_instr_init(instr);
	
	return instr;
}

void lima_gp_ir_instr_insert_start(lima_gp_ir_block_t* block,
								   lima_gp_ir_instr_t* instr)
{
	instr->block = block;
	list_add(&instr->instr_list, &block->instr_list);
	block->num_instrs++;
}

void lima_gp_ir_instr_insert_end(lima_gp_ir_block_t* block,
								 lima_gp_ir_instr_t* instr)
{
	instr->block = block;
	list_add(&instr->instr_list, block->instr_list.prev);
	block->num_instrs++;
}

static bool add_pos_ok(lima_gp_ir_instr_t* instr, lima_gp_ir_op_e op,
					   unsigned pos)
{
	if (instr->add_slots[pos])
		return false;
	if (!instr->add_slots[0] && !instr->add_slots[1])
		return true;
	lima_gp_ir_op_e other_op = instr->add_slots[1 - pos]->op;
	// add, mov, and neg use the same opcode in hw, so they can be together
	if ((op == lima_gp_ir_op_mov && other_op == lima_gp_ir_op_add) ||
		(op == lima_gp_ir_op_add && other_op == lima_gp_ir_op_mov) ||
		(op == lima_gp_ir_op_mov && other_op == lima_gp_ir_op_neg) ||
		(op == lima_gp_ir_op_neg && other_op == lima_gp_ir_op_mov) ||
		(op == lima_gp_ir_op_add && other_op == lima_gp_ir_op_neg) ||
		(op == lima_gp_ir_op_neg && other_op == lima_gp_ir_op_add))
		return true;
	return op == other_op;
}

static bool mul_pos_ok(lima_gp_ir_instr_t* instr, lima_gp_ir_op_e op,
					   unsigned pos)
{
	if (instr->mul_slots[pos])
		return false;
	if (!instr->mul_slots[0] && !instr->mul_slots[1])
		return true;
	
	// Special case: if complex2 is in the first slot, then we can do a
	// mov/multiply/negate in the second ...
	if (pos == 1 && instr->mul_slots[0]->op == lima_gp_ir_op_complex2 &&
		(op == lima_gp_ir_op_mov || op == lima_gp_ir_op_mul ||
		 op == lima_gp_ir_op_neg))
		return true;
	// ... and the other way around
	if (pos == 0 && op == lima_gp_ir_op_complex2 &&
		(instr->mul_slots[1]->op == lima_gp_ir_op_mov ||
		 instr->mul_slots[1]->op == lima_gp_ir_op_mul ||
		 instr->mul_slots[1]->op == lima_gp_ir_op_neg))
		return true;
	
	lima_gp_ir_op_e other_op = instr->mul_slots[1 - pos]->op;
	// mul, mov, and neg use the same opcode in hw, so they can be together
	if ((op == lima_gp_ir_op_mov && other_op == lima_gp_ir_op_mul) ||
		(op == lima_gp_ir_op_mul && other_op == lima_gp_ir_op_mov) ||
		(op == lima_gp_ir_op_mov && other_op == lima_gp_ir_op_neg) ||
		(op == lima_gp_ir_op_neg && other_op == lima_gp_ir_op_mov) ||
		(op == lima_gp_ir_op_mul && other_op == lima_gp_ir_op_neg) ||
		(op == lima_gp_ir_op_neg && other_op == lima_gp_ir_op_mul))
		return true;
	return op == other_op;
}

static bool try_insert_move(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	if (node->sched_pos < 2)
	{
		if (add_pos_ok(instr, node->op, node->sched_pos))
		{
			instr->add_slots[node->sched_pos] = node;
			return true;
		}
		else
			return false;
	}
	
	if (node->sched_pos < 4)
	{
		// Try and fill the second mul slot first, in case the next thing added
		// is complex2
		if (mul_pos_ok(instr, node->op, 3 - node->sched_pos))
		{
			instr->mul_slots[3 - node->sched_pos] = node;
			return true;
		}
		else
			return false;
	}
	
	if (node->sched_pos == 4)
	{
		if (!instr->complex_slot)
		{
			instr->complex_slot = node;
			return true;
		}
		else
			return false;
	}
	
	if (node->sched_pos == 5)
	{
		if (!instr->pass_slot)
		{
			instr->pass_slot = node;
			return true;
		}
		else
			return false;
	}
	
	return false;
}

static bool try_insert_neg(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	if (node->sched_pos < 2)
	{
		if (add_pos_ok(instr, lima_gp_ir_op_neg, node->sched_pos))
		{
			instr->add_slots[node->sched_pos] = node;
			return true;
		}
		else
			return false;
	}
	else
	{
		if (mul_pos_ok(instr, lima_gp_ir_op_neg, 3 - node->sched_pos))
		{
			instr->mul_slots[3 - node->sched_pos] = node;
			return true;
		}
		else
			return false;
	}
	
	return false;
}

static bool try_insert_clamp_const(lima_gp_ir_instr_t* instr,
								   lima_gp_ir_node_t* node)
{
	if (instr->pass_slot)
		return false;
	lima_gp_ir_clamp_const_node_t* clamp_const_node =
		gp_ir_node_to_clamp_const(node);
	if (instr->uniform_slot_num_used > 0 &&
		(instr->uniform_index != clamp_const_node->uniform_index ||
		instr->uniform_off_reg))
		return false;
	if (instr->uniform_slot_num_used == 0)
	{
		instr->uniform_index = clamp_const_node->uniform_index;
		instr->uniform_off_reg = 0;
	}
	instr->uniform_slot[instr->uniform_slot_num_used++] = node;
	instr->pass_slot = node;
	return true;
}

static bool try_insert_uniform(lima_gp_ir_instr_t* instr,
							   lima_gp_ir_node_t* node)
{
	lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(node);
	unsigned off_reg = 0;
	bool is_temp = node->op == lima_gp_ir_op_load_temp;
	if (load_node->offset)
		off_reg = load_node->off_reg + 1;
	if (instr->uniform_slot_num_used > 0 &&
		(instr->uniform_is_temp != is_temp ||
		 instr->uniform_index != load_node->index ||
		 instr->uniform_off_reg != off_reg))
		return false;
	if (instr->uniform_slot_num_used == 0)
	{
		instr->uniform_is_temp = is_temp;
		instr->uniform_index = load_node->index;
		instr->uniform_off_reg = off_reg;
	}
	instr->uniform_slot[instr->uniform_slot_num_used++] = node;
	return true;
}

static unsigned get_reg_index(lima_gp_ir_reg_t* reg)
{
	if (reg->phys_reg_assigned)
		return reg->phys_reg;
	return reg->index;
}

static bool try_insert_reg_zero(lima_gp_ir_instr_t* instr,
								lima_gp_ir_node_t* node)
{
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	if (instr->attr_reg_slot_num_used > 0 &&
		(instr->attr_reg_slot_is_attr ||
		 instr->attr_reg_is_phys_reg != load_reg_node->reg->phys_reg_assigned ||
		 instr->attr_reg_index != get_reg_index(load_reg_node->reg)))
		return false;
	if (instr->attr_reg_slot_num_used == 0)
	{
		instr->attr_reg_slot_is_attr = false;
		instr->attr_reg_is_phys_reg = load_reg_node->reg->phys_reg_assigned;
		instr->attr_reg_index = get_reg_index(load_reg_node->reg);
	}
	instr->attr_reg_slot[instr->attr_reg_slot_num_used++] = node;
	return true;
}

static bool try_insert_attr(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	lima_gp_ir_load_node_t* load_node = gp_ir_node_to_load(node);
	if (instr->attr_reg_slot_num_used > 0 &&
		(!instr->attr_reg_slot_is_attr ||
		 instr->attr_reg_index != load_node->index))
		return false;
	if (instr->attr_reg_slot_num_used == 0)
	{
		instr->attr_reg_slot_is_attr = true;
		instr->attr_reg_index = load_node->index;
	}
	instr->attr_reg_slot[instr->attr_reg_slot_num_used++] = node;
	return true;
}

static bool try_insert_reg_one(lima_gp_ir_instr_t* instr,
							   lima_gp_ir_node_t* node)
{
	lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(node);
	if (instr->reg_slot_num_used > 0 &&
		(instr->reg_is_phys_reg != load_reg_node->reg->phys_reg_assigned ||
		 instr->reg_index != get_reg_index(load_reg_node->reg)))
		return false;
	if (instr->reg_slot_num_used == 0)
	{
		instr->reg_is_phys_reg = load_reg_node->reg->phys_reg_assigned;
		instr->reg_index = get_reg_index(load_reg_node->reg);
	}
	instr->reg_slot[instr->reg_slot_num_used++] = node;
	return true;
}

static bool try_insert_reg(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	if (node->sched_pos == 0)
		return try_insert_reg_one(instr, node);
	if (node->sched_pos == 1)
		return try_insert_reg_zero(instr, node);
	return false;
}

static bool try_insert_store(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	bool mask[4];
	
	if (node->op == lima_gp_ir_op_store_varying ||
		node->op == lima_gp_ir_op_store_temp)
	{
		lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
		unsigned i;
		for (i = 0; i < 4; i++)
			mask[i] = store_node->mask[i];
	}
	else
	{
		lima_gp_ir_store_reg_node_t* store_node =
			gp_ir_node_to_store_reg(node);
		unsigned i;
		for (i = 0; i < 4; i++)
			mask[i] = store_node->mask[i];
	}
	
	unsigned index;
	if (node->op == lima_gp_ir_op_store_varying)
	{
		lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
		index = store_node->index;
	}
	else if (node->op == lima_gp_ir_op_store_reg)
	{
		lima_gp_ir_store_reg_node_t* store_node =
			gp_ir_node_to_store_reg(node);
		index = get_reg_index(store_node->reg);
	}
	
	if (instr->store_slot_num_used != 0)
	{
		if (instr->store_slot_is_temp)
			return false;
		
		if (instr->store_slot_is_varying &&
			node->op != lima_gp_ir_op_store_varying)
			return false;
		if (!instr->store_slot_is_varying &&
			node->op != lima_gp_ir_op_store_reg)
			return false;
		
		if (index != instr->store_slot_index)
			return false;
		
		unsigned i;
		for (i = 0; i < 4; i++)
			if (mask[i] && instr->store_slot_mask[i])
				return false;
	}
	else
	{
		if (node->op == lima_gp_ir_op_store_temp)
			instr->store_slot_is_temp = true;
		else
			instr->store_slot_index = index;
		
		if (node->op == lima_gp_ir_op_store_varying)
			instr->store_slot_is_varying = true;
	}
	
	instr->store_slot[instr->store_slot_num_used++] = node;
	
	unsigned i;
	for (i = 0; i < 4; i++)
		instr->store_slot_mask[i] = mask[i] || instr->store_slot_mask[i];
	
	return true;
}

static bool try_insert_node(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	switch (node->op)
	{
		case lima_gp_ir_op_mov:
			return try_insert_move(instr, node);
			
		case lima_gp_ir_op_mul:
			if (mul_pos_ok(instr, lima_gp_ir_op_mul, 1 - node->sched_pos))
			{
				instr->mul_slots[1 - node->sched_pos] = node;
				return true;
			}
			else
				return false;
			
		case lima_gp_ir_op_select:
		case lima_gp_ir_op_complex1:
			if (!instr->mul_slots[0] && !instr->mul_slots[1])
			{
				instr->mul_slots[0] = instr->mul_slots[1] = node;
				return true;
			}
			else
				return false;
			
		case lima_gp_ir_op_complex2:
			if (mul_pos_ok(instr, lima_gp_ir_op_complex2, 0))
			{
				instr->mul_slots[0] = node;
				return true;
			}
			else
				return false;
			
		case lima_gp_ir_op_add:
		case lima_gp_ir_op_floor:
		case lima_gp_ir_op_sign:
		case lima_gp_ir_op_ge:
		case lima_gp_ir_op_lt:
		case lima_gp_ir_op_min:
		case lima_gp_ir_op_max:
			if (add_pos_ok(instr, node->op, node->sched_pos))
			{
				instr->add_slots[node->sched_pos] = node;
				return true;
			}
			else
				return false;
			
		case lima_gp_ir_op_neg:
			return try_insert_neg(instr, node);
			
		case lima_gp_ir_op_clamp_const:
			return try_insert_clamp_const(instr, node);
			
		case lima_gp_ir_op_preexp2:
		case lima_gp_ir_op_postlog2:
			if (instr->pass_slot)
				return false;
			instr->pass_slot = node;
			return true;
			
		case lima_gp_ir_op_exp2_impl:
		case lima_gp_ir_op_log2_impl:
		case lima_gp_ir_op_rcp_impl:
		case lima_gp_ir_op_rsqrt_impl:
		case lima_gp_ir_op_store_temp_load_off0:
		case lima_gp_ir_op_store_temp_load_off1:
		case lima_gp_ir_op_store_temp_load_off2:
			if (instr->complex_slot)
				return false;
			instr->complex_slot = node;
			return true;
			
		case lima_gp_ir_op_load_uniform:
		case lima_gp_ir_op_load_temp:
			return try_insert_uniform(instr, node);
			
		case lima_gp_ir_op_load_attribute:
			return try_insert_attr(instr, node);
			
		case lima_gp_ir_op_load_reg:
			return try_insert_reg(instr, node);
			
		case lima_gp_ir_op_store_temp:
			if (instr->complex_slot)
				return false;
			/* fallthrough */
		case lima_gp_ir_op_store_reg:
		case lima_gp_ir_op_store_varying:
		{
			bool ret = try_insert_store(instr, node);
			if (ret && node->op == lima_gp_ir_op_store_temp)
				instr->complex_slot = node;
			return ret;
		}
			
		case lima_gp_ir_op_branch_cond:
			if (instr->branch_slot || instr->pass_slot)
				return false;
			instr->branch_slot = instr->pass_slot = node;
			return true;
			
		default:
			break;
	}
	
	return false;
}

static unsigned num_free_alu_nodes(lima_gp_ir_instr_t* instr)
{
	unsigned num_free = 0;
	
	if (!instr->mul_slots[0])
		num_free++;
	if (!instr->mul_slots[1])
		num_free++;
	if (!instr->add_slots[0])
		num_free++;
	if (!instr->add_slots[1])
		num_free++;
	if (!instr->complex_slot)
		num_free++;
	if (!instr->pass_slot)
		num_free++;
	
	return num_free;
}

static bool is_alu_node(lima_gp_ir_node_t* node)
{
	return (node->op == lima_gp_ir_op_mov ||
			node->op == lima_gp_ir_op_mul ||
			node->op == lima_gp_ir_op_select ||
			node->op == lima_gp_ir_op_complex1 ||
			node->op == lima_gp_ir_op_complex2 ||
			node->op == lima_gp_ir_op_add ||
			node->op == lima_gp_ir_op_floor ||
			node->op == lima_gp_ir_op_sign ||
			node->op == lima_gp_ir_op_ge ||
			node->op == lima_gp_ir_op_lt ||
			node->op == lima_gp_ir_op_min ||
			node->op == lima_gp_ir_op_max ||
			node->op == lima_gp_ir_op_neg ||
			node->op == lima_gp_ir_op_clamp_const ||
			node->op == lima_gp_ir_op_preexp2 ||
			node->op == lima_gp_ir_op_postlog2 ||
			node->op == lima_gp_ir_op_exp2_impl ||
			node->op == lima_gp_ir_op_log2_impl ||
			node->op == lima_gp_ir_op_rcp_impl ||
			node->op == lima_gp_ir_op_rsqrt_impl ||
			node->op == lima_gp_ir_op_store_temp_load_off0 ||
			node->op == lima_gp_ir_op_store_temp_load_off1 ||
			node->op == lima_gp_ir_op_store_temp_load_off2);
}

static bool is_store_child(lima_gp_ir_instr_t* instr, lima_gp_ir_node_t* node)
{
	unsigned i;
	for (i = 0; i < instr->store_slot_num_used; i++)
	{
		if (instr->store_slot[i]->op == lima_gp_ir_op_store_varying ||
			instr->store_slot[i]->op == lima_gp_ir_op_store_temp)
		{
			lima_gp_ir_store_node_t* store_node =
				gp_ir_node_to_store(instr->store_slot[i]);
			unsigned j;
			
			for (j = 0; j < 4; j++)
				if (store_node->mask[j] && store_node->children[j] == node)
					return true;
		}
		
		if (instr->store_slot[i]->op == lima_gp_ir_op_store_reg)
		{
			lima_gp_ir_store_reg_node_t* store_node =
				gp_ir_node_to_store_reg(instr->store_slot[i]);
			unsigned j;
			
			for (j = 0; j < 4; j++)
				if (store_node->mask[j] && store_node->children[j] == node)
					return true;
		}
	}

	return false;
}

bool lima_gp_ir_instr_try_insert_node(lima_gp_ir_instr_t* instr,
									  lima_gp_ir_node_t* node)
{
	unsigned num_components = 0;
	
	if (node->op == lima_gp_ir_op_store_varying ||
		node->op == lima_gp_ir_op_store_temp)
	{
		lima_gp_ir_store_node_t* store_node = gp_ir_node_to_store(node);
		unsigned i;
		
		for (i = 0; i < 4; i++)
			if (store_node->mask[i])
				num_components++;
		
		if (instr->num_unscheduled_store_children + num_components >
			num_free_alu_nodes(instr))
			return false;
	}
	
	if (node->op == lima_gp_ir_op_store_reg)
	{
		lima_gp_ir_store_reg_node_t* store_node = gp_ir_node_to_store_reg(node);
		unsigned i;
		
		for (i = 0; i < 4; i++)
			if (store_node->mask[i])
				num_components++;
		
		if (instr->num_unscheduled_store_children + num_components > num_free_alu_nodes(instr))
			return false;
	}
	
	bool alu_node = is_alu_node(node);
	bool store_child = is_store_child(instr, node);
	
	if (alu_node && !store_child &&
		instr->num_unscheduled_store_children == num_free_alu_nodes(instr))
		return false;
	
	assert(instr->num_unscheduled_store_children <= num_free_alu_nodes(instr));
	
	bool ret = try_insert_node(instr, node);
	
	if (ret && (node->op == lima_gp_ir_op_store_varying ||
				node->op == lima_gp_ir_op_store_temp ||
				node->op == lima_gp_ir_op_store_reg))
		instr->num_unscheduled_store_children += num_components;
	
	if (ret && alu_node && store_child)
		instr->num_unscheduled_store_children--;
	
	return ret;
	
}

void lima_gp_ir_instr_remove_alu_node(lima_gp_ir_instr_t* instr,
									  lima_gp_ir_node_t* node)
{
	if (instr->mul_slots[0] == node)
		instr->mul_slots[0] = NULL;
	if (instr->mul_slots[1] == node)
		instr->mul_slots[1] = NULL;
	if (instr->add_slots[0] == node)
		instr->add_slots[0] = NULL;
	if (instr->add_slots[1] == node)
		instr->add_slots[1] = NULL;
	if (instr->complex_slot == node)
		instr->complex_slot = NULL;
	if (instr->pass_slot == node)
		instr->pass_slot = NULL;
	
	if (is_store_child(instr, node))
		instr->num_unscheduled_store_children++;
	
	assert(instr->num_unscheduled_store_children <= num_free_alu_nodes(instr));
}

void lima_gp_ir_instr_delete(lima_gp_ir_instr_t* instr)
{
	list_del(&instr->instr_list);
	instr->block->num_instrs--;
	free(instr);
}
