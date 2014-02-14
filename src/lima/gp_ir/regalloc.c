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
#include <assert.h>
#include <math.h>

/* Register Allocation
 *
 * We use the graph-coloring register allocator described in "Retargetable
 * Graph-Coloring Register Allocation for Irregular Architectures" by Runeson
 * and Nystrom. Somewhat similar to the pp_lir register allocation, except
 * simplified because we only have 4 register classes to deal with instead of
 * 8.
 */

/* interference matrix calculation */

//Calculates the interference for a single set of live variables
static void calc_interference(bitset_t live, bitset_t matrix, unsigned reg_index,
							  unsigned num_regs)
{
	unsigned i;
	for (i = 0; i < num_regs; i++)
	{
		if (i == reg_index)
			continue;
		
		if (bitset_get(live, 4 * i + 0) ||
			bitset_get(live, 4 * i + 1) ||
			bitset_get(live, 4 * i + 2) ||
			bitset_get(live, 4 * i + 3))
		{
			bitset_set(matrix, num_regs*reg_index + i, true);
			bitset_set(matrix, num_regs*i + reg_index, true);
		}
	}
}

static bitset_t calc_int_matrix(lima_gp_ir_prog_t* prog)
{
	unsigned num_regs = prog->reg_alloc;
	bitset_t ret = bitset_create(num_regs*num_regs);
	lima_gp_ir_block_t* block;
	gp_ir_prog_for_each_block(prog, block)
	{
		lima_gp_ir_root_node_t* node;
		gp_ir_block_for_each_node(block, node)
		{
			if (node->node.op != lima_gp_ir_op_store_reg)
				continue;
			
			lima_gp_ir_store_reg_node_t* store_reg_node =
				gp_ir_node_to_store_reg(&node->node);
			
			unsigned reg_index = store_reg_node->reg->index;
			calc_interference(node->live_virt_after, ret, reg_index, num_regs);
		}
	}
	
	return ret;
}

//p and q values as described in the paper
static const unsigned p[4] = {16 * 4, 16 * 3, 16 * 2, 16};
static const unsigned q[4][4] = {
	{1, 2, 3, 4},
	{2, 3, 3, 3},
	{2, 2, 2, 2},
	{1, 1, 1, 1},
};

static bool colorable(lima_gp_ir_prog_t* prog, lima_gp_ir_reg_t* reg,
					  bitset_t allocated, bitset_t int_matrix)
{
	unsigned q_total = 0, num_regs = prog->reg_alloc;
	lima_gp_ir_reg_t* cur_reg;
	gp_ir_prog_for_each_reg(prog, cur_reg)
	{
		if (cur_reg == reg || bitset_get(allocated, reg->index) ||
			!bitset_get(int_matrix, num_regs*reg->index + cur_reg->index))
			continue;
		
		q_total += q[reg->size - 1][cur_reg->size - 1];
	}
	
	return q_total < p[reg->size - 1];
}

static double calc_spill_cost(lima_gp_ir_prog_t* prog, lima_gp_ir_reg_t* reg,
							  bitset_t int_matrix)
{
	
	if (reg->phys_reg_assigned)
		return INFINITY; //allocated registers cannot be spilled
	
	double spill_benefit = 0;
	lima_gp_ir_reg_t* cur_reg;
	gp_ir_prog_for_each_reg(prog, cur_reg)
	{
		if (cur_reg == reg || cur_reg->phys_reg_assigned)
			continue;
		if (!bitset_get(int_matrix,
						prog->reg_alloc*reg->index + cur_reg->index))
				continue;
		
		spill_benefit += (double) q[cur_reg->size - 1][reg->size - 1]
		/ p[cur_reg->size - 1];
	}
	
	return (ptrset_size(reg->defs) + ptrset_size(reg->uses)) / spill_benefit;
}

typedef struct
{
	lima_gp_ir_reg_t** regs;
	unsigned index;
} reg_stack;

static void reg_simplify(lima_gp_ir_prog_t* prog, bitset_t int_matrix,
						 reg_stack* stack, double* spill_costs)
{
	bitset_t allocated = bitset_create(prog->reg_alloc);
	
	while (true)
	{
		bool found_reg = true, is_colorable;
		while (found_reg)
		{
			found_reg = false;
			is_colorable = true;
			lima_gp_ir_reg_t* reg;
			gp_ir_prog_for_each_reg(prog, reg)
			{
				if (bitset_get(allocated, reg->index))
					continue;
				
				if (!colorable(prog, reg, allocated, int_matrix))
				{
					is_colorable = false;
					continue;
				}
				
				printf("Pushing reg_%u onto stack\n", reg->index);
				stack->regs[stack->index] = reg;
				stack->index++;
				bitset_set(allocated, reg->index, true);
				found_reg = true;
				break;
			}
		}
		
		if (is_colorable)
			break;
		
		//All nodes are un-colorable
		//Pick the node with the smallest spill cost, and push it on the stack
		//Optimistically
		lima_gp_ir_reg_t* reg, *min_reg;
		double min_spill_cost = INFINITY;
		gp_ir_prog_for_each_reg(prog, reg)
		{
			if (bitset_get(allocated, reg->index))
				continue;
			
			if (spill_costs[reg->index] < min_spill_cost)
			{
				min_reg = reg;
				min_spill_cost = spill_costs[reg->index];
			}
		}
		
		printf("Pushing reg_%u onto stack (possible spill)\n", min_reg->index);
		stack->regs[stack->index] = min_reg;
		stack->index++;
		bitset_set(allocated, min_reg->index, true);
	}
	
	bitset_delete(allocated);
}

static void reg_select(lima_gp_ir_prog_t* prog, reg_stack* stack,
					   bitset_t int_matrix)
{
	unsigned i = stack->index, j, k;
	while (i > 0)
	{
		i--;
		lima_gp_ir_reg_t* reg = stack->regs[i];
		
		bool conflicts;
		//Iterate over each vec4 register
		//Note that we can go above the highest register (r15), in which case
		//we later rewrite all defs/uses as temporaries. This way, we get
		//minimal temporary usage when spilling - important because temporaries
		//are relatively limited.
		j = 0;
		while (true)
		{
			//Iterate over each position in the vec4 register
			for (k = 0; k < 5 - reg->size; k++)
			{
				conflicts = false;
				lima_gp_ir_reg_t* other_reg;
				gp_ir_prog_for_each_reg(prog, other_reg)
				{
					if (reg == other_reg || !other_reg->phys_reg_assigned ||
						other_reg->phys_reg != j ||
						!bitset_get(int_matrix,
									prog->reg_alloc*reg->index + other_reg->index))
						continue;
					
					unsigned start_l = other_reg->phys_reg_offset;
					unsigned end_l = start_l + other_reg->size - 1;
					unsigned start_k = k;
					unsigned end_k = start_k + reg->size - 1;
					if ((start_k <= start_l && start_l <= end_k) ||
						(start_k <= end_l && end_l <= end_k) ||
						(start_l <= start_k && start_k <= end_l) ||
						(start_l <= end_k && end_k <= end_l))
					{
						conflicts = true;
						break;
					}
				}
				
				if (!conflicts)
				{
					reg->phys_reg_assigned = true;
					reg->phys_reg = j;
					reg->phys_reg_offset = k;
					printf("reg_%u getting phys_reg %u, offset %u\n", reg->index,
						   j, k);
					break;
				}
			}
			
			if (!conflicts)
				break;
			
			j++;
		}
	}
}

static bool spill_reg(lima_gp_ir_reg_t* reg, unsigned temp_index,
					  unsigned offset)
{
	lima_gp_ir_node_t* use;
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	ptrset_iter_for_each(iter, use)
	{
		ptrset_remove(&reg->uses, use);
		
		lima_gp_ir_load_reg_node_t* load_reg_node = gp_ir_node_to_load_reg(use);
		
		lima_gp_ir_load_node_t* load_temp_node =
			lima_gp_ir_load_node_create(lima_gp_ir_op_load_temp);
		
		if (!load_temp_node)
			return false;
		
		load_temp_node->index = temp_index;
		load_temp_node->component = load_reg_node->component + offset;
		load_temp_node->offset = false;
		
		if (!lima_gp_ir_node_replace(use, &load_temp_node->node))
			return false;
	}
	
	lima_gp_ir_node_t* def;
	iter = ptrset_iter_create(reg->defs);
	ptrset_iter_for_each(iter, def)
	{
		ptrset_remove(&reg->defs, def);
		
		lima_gp_ir_store_reg_node_t* store_reg_node =
			gp_ir_node_to_store_reg(def);
		
		lima_gp_ir_store_node_t* store_temp_node =
			lima_gp_ir_store_node_create(lima_gp_ir_op_store_temp);
		
		if (!store_temp_node)
			return false;
		
		lima_gp_ir_const_node_t* const_node = lima_gp_ir_const_node_create();
		if (!const_node)
		{
			lima_gp_ir_node_delete(&store_temp_node->root_node.node);
			return false;
		}
		
		const_node->constant = (float)temp_index;
		
		lima_gp_ir_block_insert_before(&store_temp_node->root_node,
									   &store_reg_node->root_node);
		
		unsigned i;
		for (i = 0; i < 4; i++)
		{
			if (!store_reg_node->mask[i])
				continue;
			
			store_temp_node->mask[i + offset] = true;
			store_temp_node->children[i + offset] = store_reg_node->children[i];
			lima_gp_ir_node_link(&store_temp_node->root_node.node,
								 store_temp_node->children[i + offset]);
			lima_gp_ir_node_unlink(&store_reg_node->root_node.node,
								   store_reg_node->children[i]);
			store_reg_node->children[i] = NULL;
			store_reg_node->mask[i] = false;
		}
		
		store_temp_node->index = 0;
		store_temp_node->addr = &const_node->node;
		lima_gp_ir_node_link(&store_temp_node->root_node.node,
							 &const_node->node);
		
		lima_gp_ir_block_remove(&store_reg_node->root_node);
	}
	
	lima_gp_ir_reg_delete(reg);
	return true;
}

static bool spill_regs(lima_gp_ir_prog_t* prog)
{
	unsigned old_temp_alloc = prog->temp_alloc;
	
	lima_gp_ir_reg_t* reg, *temp;
	gp_ir_prog_for_each_reg_safe(prog, reg, temp)
	{
		if (reg->phys_reg < 16)
			continue;
		
		unsigned temp_index = reg->phys_reg - 16 + old_temp_alloc;
		if (!spill_reg(reg, temp_index,
						reg->phys_reg_offset))
			return false;
		
		if (temp_index >= prog->temp_alloc)
			prog->temp_alloc = temp_index + 1;
	}
	
	return true;
}

bool lima_gp_ir_regalloc(lima_gp_ir_prog_t* prog)
{
	if (!lima_gp_ir_liveness_compute_prog(prog, true))
		return false;
	
	while (true)
	{
		bitset_t int_matrix = calc_int_matrix(prog);
		
		unsigned i, j;
		for (i = 0; i < prog->reg_alloc; i++)
		{
			for (j = 0; j < prog->reg_alloc; j++)
			{
				if (bitset_get(int_matrix, prog->reg_alloc*i + j))
					printf("1, ");
				else
					printf("0, ");
			}
			printf("\n");
		}
		
		reg_stack stack = {
			.index = 0,
			.regs = malloc(prog->reg_alloc * sizeof(lima_gp_ir_reg_t*))
		};
		if (!stack.regs)
		{
			bitset_delete(int_matrix);
			return false;
		}
		
		double* spill_costs = malloc(prog->reg_alloc * sizeof(double*));
		if (!spill_costs)
		{
			bitset_delete(int_matrix);
			free(stack.regs);
			return false;
		}
		
		lima_gp_ir_reg_t* reg;
		gp_ir_prog_for_each_reg(prog, reg)
		{
			spill_costs[reg->index] = calc_spill_cost(prog, reg, int_matrix);
		}
		
		reg_simplify(prog, int_matrix, &stack, spill_costs);
		reg_select(prog, &stack, int_matrix);
		
		free(stack.regs);
		free(spill_costs);
		bitset_delete(int_matrix);
		
		return spill_regs(prog);
	}
}

/* Register allocation within the scheduler
 *
 * The scheduler can spill intermediate results to registers in case it cannot
 * schedule a node, in which case it needs to allocate a register for the
 * intermediate result. Here, we can assume three things:
 * 1. The register is always of size 1 (i.e. is scalar)
 * 2. There is only one definition
 * 3. All uses are in the same basic block as the definition
 * We allocate registers one-by-one within the scheduler, looking for free
 * physical registers, choosing one, filling in the liveness info, and adding
 * any "false dependencies" introduced by the allocation.
 *
 * Unfortunately, when in the middle of scheduling, we must be careful not to
 * introduce any dependencies that cannot be fulfilled by the scheduler.
 * For example, we can't insert a write to register 0.x if there is another
 * write to 0.x beforehand that the scheduler hasn't scheduled yet or another
 * write to 0.x afterwards that the scheduler has already scheduled. Rather than
 * doing a costly analysis pass, we simply make sure that each register we
 * allocate is never used in that block. This also makes register allocation a
 * lot faster, as we only have to pre-compute the set of available registers at
 * the beginning of the schedule and then choose an available register. If we
 * cannot find a register that way, then we can fall back to the more expensive
 * way which can add dependencies (or spill the register to a temporary) and
 * then re-schedule the block.
 */

bitset_t lima_gp_ir_regalloc_get_free_regs(lima_gp_ir_block_t* block)
{
	bitset_t ret = bitset_create_full(16*4);
	
	lima_gp_ir_root_node_t* node;
	gp_ir_block_for_each_node(block, node)
	{
		bitset_subtract(&ret, node->live_phys_after);
	}
	
	bitset_subtract(&ret, block->live_phys_before);
	return ret;
}

static void regalloc_scalar_mark_live_regs(lima_gp_ir_reg_t* reg)
{
	lima_gp_ir_node_t* def = ptrset_first(reg->defs);
	
	ptrset_t successors;
	ptrset_create(&successors);
	ptrset_iter_t iter = ptrset_iter_create(reg->uses);
	lima_gp_ir_node_t* use;
	ptrset_iter_for_each(iter, use)
	{
		ptrset_add(&successors, use->successor);
	}
	
	lima_gp_ir_store_reg_node_t* store_reg_node = gp_ir_node_to_store_reg(def);
	lima_gp_ir_root_node_t* cur_node = &store_reg_node->root_node;
	while (ptrset_size(successors))
	{
		bitset_set(cur_node->live_phys_after,
				   4 * reg->phys_reg + reg->phys_reg_offset, true);
		cur_node = gp_ir_node_next(cur_node);
		ptrset_remove(&successors, cur_node);
	}
	
	ptrset_delete(successors);
}

bool lima_gp_ir_regalloc_scalar_fast(lima_gp_ir_reg_t* reg, bitset_t free_regs)
{
	unsigned reg_num;
	for (reg_num = 0; reg_num < 16*4; reg_num++)
	{
		if (bitset_get(free_regs, reg_num))
		{
			reg->phys_reg_assigned = true;
			reg->phys_reg = reg_num / 4;
			reg->phys_reg_offset = reg_num % 4;
			regalloc_scalar_mark_live_regs(reg);
			bitset_set(free_regs, reg_num, false);
			return true;
		}
	}
	
	return false;
}
