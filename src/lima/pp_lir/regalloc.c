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

#include "regalloc.h"
#include "fixed_queue.h"
#include <math.h>
#include <assert.h>

/*
 * The following implements the graph-coloring register allocator
 * described in "Retargetable Graph-Coloring Register Allocation for Irregular
 * Architectures" by Runeson and Nystrom. In our architecture, we have 4 register
 * classes based upon the size of the virtual register. There are 6 vec4's. Each
 * vec4 has 2 vec3's (.xyz and .yzw), 3 vec2's (.xy, .yz, and .zw), and 4 scalar
 * registers (z, y, z, and w) because each virtual register must be allocated inside
 * a vec4.
 *
 * In addition, for registers used/defined for loads/stores, for which there is no
 * swizzle field and therefore the (virtual) register must be allocated at the
 * beginning of a (physical) register, there are another 4 classes, indicated in
 * the lima_pp_lir_reg_t struct by the "beginning" field. There is only 1 virtual reg
 * per physical reg for each class, for obvious reasons.
 */

static void init_regs(lima_pp_lir_prog_t* prog)
{
	unsigned i, index = 0;
	for (i = 0; i < prog->num_regs; i++)
	{
		lima_pp_lir_reg_t* reg = prog->regs[i];
		if (reg->precolored)
		{
			//Conservative; this value should never drop below 6
			reg->q_total = prog->reg_alloc + 6;
		}
		else
		{
			reg->q_total = 0; //Initialized in add_edge()
			reg->index = index++;
		}
		
		reg->state = lima_pp_lir_reg_state_initial;
		ptr_vector_clear(&reg->adjacent);
		ptrset_empty(&reg->moves);
	}
	
	prog->reg_alloc = index;
}

//p and q values as desribed in the paper
static const unsigned p[8] = {6 * 4, 6 * 3, 6 * 2, 6, 6, 6, 6, 6};
static const unsigned q[8][8] = {
	{1, 2, 3, 4, 1, 2, 3, 4},
	{2, 3, 3, 3, 1, 2, 3, 3},
	{2, 2, 2, 2, 1, 2, 2, 2},
	{1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1},
};

static unsigned get_reg_class(lima_pp_lir_reg_t* reg)
{
	return reg->beginning ? reg->size + 3 : reg->size - 1;
}

static unsigned get_index(lima_pp_lir_reg_t* reg)
{
	if (reg->precolored)
	{
		assert(reg->index == 0);
		return 0;
	}
	return reg->index + 1;
}

static void add_edge(lima_pp_lir_reg_t* reg1, lima_pp_lir_reg_t* reg2,
					 unsigned reg1_components, unsigned reg2_components,
					 bitset_t matrix, unsigned num_regs)
{
	if (reg1 == reg2)
		return;
	
	unsigned i, j;
	bool added = false;
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			unsigned reg1_index = 4*get_index(reg1) + i;
			unsigned reg2_index = 4*get_index(reg2) + j;
			if (bitset_get(matrix, (1 + num_regs)*4*reg1_index + reg2_index))
			{
				added = true;
				continue;
			}
			
			if (!((reg1_components >> i) & 1) || !((reg2_components >> j) & 1))
				continue;
			
			bitset_set(matrix, (1 + num_regs)*4*reg1_index + reg2_index, true);
			bitset_set(matrix, (1 + num_regs)*4*reg2_index + reg1_index, true);
		}
	
	if (!added)
	{
		if (!reg1->precolored)
			reg1->q_total += q[get_reg_class(reg1)][get_reg_class(reg2)];
		
		if (!reg2->precolored)
			reg2->q_total += q[get_reg_class(reg2)][get_reg_class(reg1)];
		
		ptr_vector_add(&reg1->adjacent, reg2);
		ptr_vector_add(&reg2->adjacent, reg1);
	}
}

static bool is_move(lima_pp_lir_instr_t* instr)
{
	if (instr->op != lima_pp_hir_op_mov)
		return false;
	
	if (instr->dest.pipeline || instr->sources[0].pipeline)
		return false;
	
	if (instr->dest.modifier != lima_pp_outmod_none)
		return false;
	
	if (instr->sources[0].absolute || instr->sources[0].negate)
		return false;
	
	return true;
}

static void add_edge_instr(lima_pp_lir_instr_t* instr, bitset_t matrix,
						   unsigned num_regs)
{
	lima_pp_lir_prog_t* prog = instr->sched_instr->block->prog;
	
	unsigned reg1_components = 0;
	unsigned i, j;
	for (i = 0; i < 4; i++)
	{
		if (instr->dest.mask[i])
			reg1_components |= 1 << i;
	}
	
	unsigned use_components = 0;
	lima_pp_lir_reg_t* use = NULL;
	if (is_move(instr))
	{
		use = instr->sources[0].reg;
		for (i = 0; i < 4; i++)
		{
			if (!instr->dest.mask[i])
				continue;
			
			use_components |= 1 << instr->sources[0].swizzle[i];
		}
	}
	
	for (i = 0; i < prog->num_regs; i++)
	{
		lima_pp_lir_reg_t* reg = prog->regs[i];
		if (reg->precolored && reg->index != 0)
			continue;
		
		unsigned reg2_components = 0;
		unsigned reg2_index = get_index(reg);
		for (j = 0; j < 4; j++)
		{
			if (bitset_get(instr->live_out, 4*reg2_index + j))
				reg2_components |= 1 << j;
		}
		
		if (reg == use)
		{
			reg2_components &= ~use_components;
		}
		
		if (reg2_components)
		{
			add_edge(instr->dest.reg, reg, reg1_components, reg2_components,
					 matrix, num_regs);
		}
	}
}

//Gets the interference matrix, where each channel of each variable has an entry.
//Used for move elimination.
static bitset_t calc_detailed_int_matrix(lima_pp_lir_prog_t* prog)
{
	unsigned i;
	bitset_t ret = bitset_create(16 * (prog->reg_alloc + 1) * (prog->reg_alloc + 1));
	for (i = 0; i < prog->num_regs; i++)
	{
		lima_pp_lir_reg_t* reg = prog->regs[i];
		lima_pp_lir_instr_t* def;
		ptrset_iter_t iter = ptrset_iter_create(reg->defs);
		ptrset_iter_for_each(iter, def)
		{
			add_edge_instr(def, ret, prog->reg_alloc);
		}
	}
	
	return ret;
}

//Calculate coarse interference matrix for register allocation
static bitset_t calc_coarse_int_matrix(bitset_t detailed_int_matrix, unsigned num_regs)
{
	bitset_t ret = bitset_create((num_regs + 1) * (num_regs + 1));
	unsigned i, j, ii, jj;
	for (i = 0; i < num_regs + 1; i++)
		for (j = 0; j < num_regs + 1; j++)
		{
			for (ii = 0; ii < 4; ii++)
			{
				for (jj = 0; jj < 4; jj++)
					if (bitset_get(detailed_int_matrix,
								   (num_regs + 1)*4*(4*i + ii) + (4*j + jj)))
					{
						bitset_set(ret, (num_regs + 1)*i + j, true);
						goto outer_break;
					}
			}
			outer_break:
			; //Silly GCC error
		}
	
	return ret;
}

typedef struct
{
	//Registers
	fixed_queue_t simplify_queue;
	ptrset_t spilled_regs, spill_queue, freeze_queue;
	lima_pp_lir_reg_t** select_stack;
	unsigned select_stack_index;
	
	//Moves
	ptrset_t move_queue, active_moves;
} state_t;

static bool create_state(state_t* state, unsigned num_regs)
{
	state->simplify_queue = fixed_queue_create(num_regs);
	state->select_stack = malloc(num_regs * sizeof(lima_pp_lir_reg_t*));
	if (!state->select_stack)
	{
		fixed_queue_delete(state->simplify_queue);
		return false;
	}
	
	if (!ptrset_create(&state->spill_queue))
	{
		fixed_queue_delete(state->simplify_queue);
		free(state->select_stack);
		return false;
	}
	
	if (!ptrset_create(&state->spilled_regs))
	{
		fixed_queue_delete(state->simplify_queue);
		ptrset_delete(state->spill_queue);
		free(state->select_stack);
		return false;
	}
	
	if (!ptrset_create(&state->freeze_queue))
	{
		fixed_queue_delete(state->simplify_queue);
		ptrset_delete(state->spill_queue);
		ptrset_delete(state->spilled_regs);
		free(state->select_stack);
		return false;
	}
	
	if (!ptrset_create(&state->move_queue))
	{
		fixed_queue_delete(state->simplify_queue);
		ptrset_delete(state->spill_queue);
		ptrset_delete(state->spilled_regs);
		ptrset_delete(state->freeze_queue);
		free(state->select_stack);
		return false;
	}
	
	if (!ptrset_create(&state->active_moves))
	{
		fixed_queue_delete(state->simplify_queue);
		ptrset_delete(state->spill_queue);
		ptrset_delete(state->spilled_regs);
		ptrset_delete(state->freeze_queue);
		ptrset_delete(state->move_queue);
		free(state->select_stack);
		return false;
	}
	
	state->select_stack_index = 0;
	
	return true;
}

static void delete_state(state_t* state)
{
	fixed_queue_delete(state->simplify_queue);
	ptrset_delete(state->spill_queue);
	ptrset_delete(state->spilled_regs);
	ptrset_delete(state->freeze_queue);
	ptrset_delete(state->move_queue);
	ptrset_delete(state->active_moves);
	free(state->select_stack);
}

static void init_moves(state_t* state, lima_pp_lir_prog_t* prog)
{
	unsigned i, j;
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		lima_pp_lir_scheduled_instr_t* instr;
		pp_lir_block_for_each_instr(block, instr)
		{
			for (j = 0; j < 5; j++)
				if (instr->alu_instrs[j] && is_move(instr->alu_instrs[j]))
				{
					lima_pp_lir_instr_t* move = instr->alu_instrs[j];
					ptrset_add(&state->move_queue, move);
					lima_pp_lir_reg_t* use = move->sources[0].reg;
					lima_pp_lir_reg_t* def = move->dest.reg;
					ptrset_add(&use->moves, move);
					ptrset_add(&def->moves, move);
				}
			
		}
	}
}

static void init_reg_queues(state_t* state, lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
	{
		lima_pp_lir_reg_t* reg = prog->regs[i];
		
		if (reg->precolored)
			continue;
		
		if (reg->q_total >= p[get_reg_class(reg)])
		{
			ptrset_add(&state->spill_queue, reg);
			reg->state = lima_pp_lir_reg_state_to_spill;
		}
		else if (ptrset_size(reg->moves) == 0)
		{
			fixed_queue_push(&state->simplify_queue, reg);
			reg->state = lima_pp_lir_reg_state_to_simplify;
		}
		else
		{
			ptrset_add(&state->freeze_queue, reg);
			reg->state = lima_pp_lir_reg_state_to_freeze;
		}
	}
}

static bool move_related(lima_pp_lir_reg_t* reg, state_t* state)
{
	ptrset_iter_t iter = ptrset_iter_create(reg->moves);
	lima_pp_lir_instr_t* move;
	ptrset_iter_for_each(iter, move)
	{
		if (ptrset_contains(state->move_queue, move) ||
			ptrset_contains(state->active_moves, move))
			return true;
	}
	
	return false;
}

static void enable_moves(lima_pp_lir_reg_t* reg, state_t* state)
{
	lima_pp_lir_instr_t* move;
	ptrset_iter_t iter = ptrset_iter_create(reg->moves);
	ptrset_iter_for_each(iter, move)
	{
		if (ptrset_contains(state->active_moves, move))
		{
			ptrset_remove(&state->active_moves, move);
			ptrset_add(&state->move_queue, move);
		}
	}
}

static void decrement_q_total(lima_pp_lir_reg_t* reg, lima_pp_lir_reg_t* other,
							  state_t* state)
{
	reg->q_total -= q[get_reg_class(reg)][get_reg_class(other)];
	if (reg->q_total < p[get_reg_class(reg)] &&
		reg->state != lima_pp_lir_reg_state_to_simplify &&
		reg->state != lima_pp_lir_reg_state_to_freeze)
	{
		enable_moves(reg, state);
		
		unsigned i;
		for (i = 0; i < ptr_vector_size(reg->adjacent); i++)
		{
			lima_pp_lir_reg_t* other = ptr_vector_get(reg->adjacent, i);
			
			if (other->state == lima_pp_lir_reg_state_simplified ||
				other->state == lima_pp_lir_reg_state_coalesced)
				continue;
			
			enable_moves(other, state);
		}
		
		ptrset_remove(&state->spill_queue, reg);
		if (move_related(reg, state))
		{
			ptrset_add(&state->freeze_queue, reg);
			reg->state = lima_pp_lir_reg_state_to_freeze;
		}
		else
		{
			fixed_queue_push(&state->simplify_queue, reg);
			reg->state = lima_pp_lir_reg_state_to_simplify;
		}
	}
}

static void simplify(state_t* state)
{
	lima_pp_lir_reg_t* reg = fixed_queue_pop(&state->simplify_queue);
	
	state->select_stack[state->select_stack_index] = reg;
	state->select_stack_index++;
	
	printf("Pushing %%%u onto stack\n", reg->index);
	
	reg->state = lima_pp_lir_reg_state_simplified;
	
	unsigned i;
	for (i = 0; i < ptr_vector_size(reg->adjacent); i++)
	{
		lima_pp_lir_reg_t* other = ptr_vector_get(reg->adjacent, i);
		
		if (other->state == lima_pp_lir_reg_state_simplified ||
			other->state == lima_pp_lir_reg_state_coalesced)
			continue;
		
		decrement_q_total(other, reg, state);
	}
}

static lima_pp_lir_reg_t* get_alias(lima_pp_lir_reg_t* reg, unsigned* swizzle)
{
	unsigned i;
	
	if (swizzle)
	{
		for (i = 0; i < reg->size; i++)
			swizzle[i] = i;
	}
	
	lima_pp_lir_reg_t* new_reg = reg;
	while (new_reg->state == lima_pp_lir_reg_state_coalesced)
	{
		if (swizzle)
		{
			for (i = 0; i < reg->size; i++)
				swizzle[i] = new_reg->alias_swizzle[swizzle[i]];
		}
		
		new_reg = new_reg->alias;
	}
	
	return new_reg;
}

static void add_to_queue(lima_pp_lir_reg_t* reg, state_t* state)
{
	if (!reg->precolored && !move_related(reg, state) &&
		reg->q_total < p[get_reg_class(reg)])
	{
		ptrset_remove(&state->freeze_queue, reg);
		fixed_queue_push(&state->simplify_queue, reg);
		reg->state = lima_pp_lir_reg_state_to_simplify;
	}
}

static bool component_interferes(bitset_t matrix, unsigned num_regs,
								 lima_pp_lir_reg_t* reg1,
								 lima_pp_lir_reg_t* reg2,
								 unsigned reg1_component,
								 unsigned reg2_component)
{
	unsigned reg1_index = get_index(reg1);
	unsigned reg2_index = get_index(reg2);
	return bitset_get(matrix,
					  (1 + num_regs)*4*(4*reg1_index + reg1_component) +
					  (4*reg2_index + reg2_component));
}

static bool reg_interferes(bitset_t matrix, unsigned num_regs,
						   lima_pp_lir_reg_t* reg1,
						   lima_pp_lir_reg_t* reg2)
{
	unsigned reg1_index = get_index(reg1);
	unsigned reg2_index = get_index(reg2);
	return bitset_get(matrix, (1 + num_regs)*reg1_index + reg2_index);
}


//Brigg's conservative coalescing heuristic, modified to use the <p, q> test
static bool can_coalesce(lima_pp_lir_reg_t* reg1, lima_pp_lir_reg_t* reg2,
						 bitset_t matrix, lima_pp_lir_prog_t* prog,
						 unsigned num_regs)
{
	//calculate register class of combined register
	unsigned size = reg1->size > reg2->size ? reg1->size : reg2->size;
	unsigned reg_class =
		(reg1->beginning || reg2->beginning) ? size + 3 : size - 1;
	
	unsigned q_total = 0;
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
	{
		lima_pp_lir_reg_t* other = prog->regs[i];
		
		if (other->precolored && other->index != 0)
			continue;
		
		if (!reg_interferes(matrix, num_regs, reg1, other) &&
			!reg_interferes(matrix, num_regs, reg2, other))
			continue;
		
		if (other->q_total >= p[get_reg_class(other)])
		{
			q_total += q[reg_class][get_reg_class(other)];
		}
	}
	
	return q_total < p[reg_class];
}

//Add an edge between dst and other
static void add_move_edge(lima_pp_lir_reg_t* src, lima_pp_lir_reg_t* dst,
						  lima_pp_lir_reg_t* other, unsigned* swizzle,
						  bitset_t detailed_matrix, bitset_t coarse_matrix,
						  unsigned num_regs)
{
	unsigned dst_index = get_index(dst);
	unsigned other_index = get_index(other);
	
	if (!reg_interferes(coarse_matrix, num_regs, dst, other))
	{
		bitset_set(coarse_matrix, (num_regs + 1)*dst_index + other_index, true);
		bitset_set(coarse_matrix, (num_regs + 1)*other_index + dst_index, true);
		ptr_vector_add(&dst->adjacent, other);
		ptr_vector_add(&other->adjacent, dst);
		
		if (!dst->precolored)
			dst->q_total += q[get_reg_class(dst)][get_reg_class(other)];
		
		if (!other->precolored)
			other->q_total += q[get_reg_class(other)][get_reg_class(dst)];
	}
	
	unsigned i, j;
	for (i = 0; i < src->size; i++)
		for (j = 0; j < dst->size; j++)
			if (component_interferes(detailed_matrix, num_regs, src, other, i, j))
			{
				bitset_set(detailed_matrix,
						   (num_regs + 1)*4*(4*dst_index + swizzle[i])
						   + (4*other_index + j), true);
				bitset_set(detailed_matrix,
						   (num_regs + 1)*4*(4*other_index + j)
						   + (4*dst_index + swizzle[i]), true);
			}
}

static void combine(lima_pp_lir_reg_t* src, lima_pp_lir_reg_t* dst,
					unsigned* swizzle, bitset_t detailed_matrix,
					bitset_t coarse_matrix, unsigned num_regs, state_t* state)
{
	if (src->state == lima_pp_lir_reg_state_to_freeze)
		ptrset_remove(&state->freeze_queue, src);
	else
		ptrset_remove(&state->spill_queue, src);
	
	src->state = lima_pp_lir_reg_state_coalesced;
	memcpy(src->alias_swizzle, swizzle, src->size * sizeof(unsigned));
	src->alias = dst;
	
	ptrset_union(&dst->moves, src->moves);
	
	unsigned i;
	for (i = 0; i < ptr_vector_size(src->adjacent); i++)
	{
		lima_pp_lir_reg_t* other = ptr_vector_get(src->adjacent, i);
		
		if (other->state == lima_pp_lir_reg_state_simplified ||
			other->state == lima_pp_lir_reg_state_coalesced)
			continue;
		
		add_move_edge(src, dst, other, swizzle, detailed_matrix, coarse_matrix,
					  num_regs);
		decrement_q_total(other, src, state);
	}
	
	if (dst->q_total >= p[get_reg_class(dst)] &&
		dst->state == lima_pp_lir_reg_state_to_freeze)
	{
		ptrset_remove(&state->freeze_queue, dst);
		ptrset_add(&state->spill_queue, dst);
		dst->state = lima_pp_lir_reg_state_to_spill;
	}
}

static void coalesce(state_t* state, bitset_t detailed_matrix,
					 bitset_t coarse_matrix, lima_pp_lir_prog_t* prog,
					 unsigned num_regs)
{
	lima_pp_lir_instr_t* move = ptrset_first(state->move_queue);
	ptrset_remove(&state->move_queue, move);
	
	unsigned src_swizzle[4], dst_swizzle[4];
	lima_pp_lir_reg_t* src = get_alias(move->sources[0].reg, src_swizzle);
	lima_pp_lir_reg_t* dst = get_alias(move->dest.reg, dst_swizzle);
	
	if (src == dst)
	{
		add_to_queue(src, state);
		return;
	}
	
	if (src->precolored && dst->precolored)
		return;
	
	//together, src_components and dst_components represent a mapping of
	//components from src -> dst based upon the move instruction, of size
	//num_components. For example, src_components = {2, 3, 1},
	//dst_components = {3, 0, 1}, and num_components = 3 means that
	//src.z -> dst.w, src.w -> dst.x, and src.y -> dst.y, while the rest of
	//the src components can be mapped to any (non-interfering) dst component.
	unsigned src_components[4], dst_components[4], num_components = 0;
	
	//Create the mapping
	unsigned i, j;
	for (i = 0; i < 4; i++)
	{
		if (!move->dest.mask[i])
			continue;
		
		src_components[num_components] = src_swizzle[move->sources[0].swizzle[i]];
		dst_components[num_components] = dst_swizzle[i];
		num_components++;
	}
	
	if (src->precolored || src->size > dst->size)
	{
		//Flip src and dst
		lima_pp_lir_reg_t* temp = src;
		src = dst;
		dst = temp;
		
		unsigned temp_components[4];
		memcpy(temp_components, src_components, num_components * sizeof(unsigned));
		memcpy(src_components, dst_components, num_components * sizeof(unsigned));
		memcpy(dst_components, temp_components, num_components * sizeof(unsigned));
	}
	
	if (src->beginning && !dst->beginning && dst->size != 4)
	{
		add_to_queue(src, state);
		add_to_queue(dst, state);
		return;
	}
	
	//Check that src and dst don't interfere
	for (i = 0; i < num_components; i++)
	{
		if (component_interferes(detailed_matrix, num_regs, src, dst,
								 src_components[i], dst_components[i]))
		{
			add_to_queue(src, state);
			add_to_queue(dst, state);
			return;
		}
	}
	
	bool src_used[4] = {false, false, false, false};
	bool dst_used[4] = {false, false, false, false};
	unsigned swizzle[4];
	for (i = 0; i < num_components; i++)
	{
		//Check for duplicates
		if (src_used[src_components[i]] || dst_used[dst_components[i]])
		{
			add_to_queue(src, state);
			add_to_queue(dst, state);
			return;
		}
		
		src_used[src_components[i]] = true;
		dst_used[dst_components[i]] = true;
		swizzle[src_components[i]] = dst_components[i];
	}
	
	for (i = 0; i < src->size; i++)
	{
		if (src_used[i])
			continue;
		
		//Find a non-interfering unused component of dst
		for (j = 0; j < dst->size; j++)
		{
			if (!dst_used[j] &&
				!component_interferes(detailed_matrix, num_regs, src, dst, i, j))
				break;
		}
		
		if (j == dst->size)
		{
			//Couldn't find a suitable component - bail out
			add_to_queue(src, state);
			add_to_queue(dst, state);
			return;
		}
		
		dst_used[j] = true;
		swizzle[i] = j;
	}
	
	if (src->beginning)
	{
		for (i = 0; i < src->size; i++)
			if (swizzle[i] != i)
			{
				add_to_queue(src, state);
				add_to_queue(dst, state);
				return;
			}
	}
	
	//We've found a valid swizzle, so we can replace src by dst - the only thing
	//stopping us now is if it could cause extra spills.
	if (!can_coalesce(src, dst, coarse_matrix, prog, num_regs))
	{
		ptrset_add(&state->active_moves, move);
		return;
	}
	
	printf("Coalesing %%%u into ", src->index);
	if (dst->precolored)
		printf("$%u", dst->index);
	else
		printf("%%%u", dst->index);
	
	printf(", swizzle: ");
	for (i = 0; i < src->size; i++)
		printf("%c", "xyzw"[swizzle[i]]);
	printf("\n");
	
	combine(src, dst, swizzle, detailed_matrix, coarse_matrix, num_regs, state);
}

static void freeze_moves(lima_pp_lir_reg_t* reg, state_t* state)
{
	lima_pp_lir_instr_t* move;
	ptrset_iter_t iter = ptrset_iter_create(reg->moves);
	ptrset_iter_for_each(iter, move)
	{
		if (!ptrset_contains(state->active_moves, move) &&
			!ptrset_contains(state->move_queue, move))
			continue;
		
		ptrset_remove(&state->active_moves, move);
		ptrset_remove(&state->move_queue, move);
		
		lima_pp_lir_reg_t* other;
		if (move->dest.reg == reg)
			other = move->sources[0].reg;
		else
			other = move->dest.reg;
		
		other = get_alias(other, NULL);
		
		if (!move_related(other, state) &&
			other->q_total < p[get_reg_class(other)])
		{
			ptrset_remove(&state->freeze_queue, other);
			fixed_queue_push(&state->simplify_queue, other);
			other->state = lima_pp_lir_reg_state_to_simplify;
		}
	}
}

static void freeze(state_t* state)
{
	lima_pp_lir_reg_t* reg = ptrset_first(state->freeze_queue);
	
	printf("Freezing %%%u\n", reg->index);
	
	ptrset_remove(&state->freeze_queue, reg);
	fixed_queue_push(&state->simplify_queue, reg);
	reg->state = lima_pp_lir_reg_state_to_simplify;
	
	freeze_moves(reg, state);
}

//Calculates the spill cost of a register
//The benefit is defined as in the paper, and the cost is simply
//the number of defs + uses (for now)
static double calc_spill_cost(lima_pp_lir_reg_t* reg)
{
	//precolored registers and registers created from spilling another register
	//cannot be spilled
	if (reg->precolored || reg->spilled)
		return INFINITY;
	
	double spill_benefit = 0;
	unsigned i;
	for (i = 0; i < ptr_vector_size(reg->adjacent); i++)
	{
		lima_pp_lir_reg_t* temp_reg = ptr_vector_get(reg->adjacent, i);
		
		spill_benefit += (double) q[get_reg_class(temp_reg)][get_reg_class(reg)]
		/ p[get_reg_class(temp_reg)];
	}
	
	return (ptrset_size(reg->defs) + ptrset_size(reg->uses)) / spill_benefit;
}

static void select_spill(state_t* state)
{
	lima_pp_lir_reg_t* min_reg = NULL, *reg;
	double min_spill_cost = INFINITY;
	ptrset_iter_t iter = ptrset_iter_create(state->spill_queue);
	ptrset_iter_for_each(iter, reg)
	{
		double spill_cost = calc_spill_cost(reg);
		if (spill_cost < min_spill_cost)
		{
			min_reg = reg;
			min_spill_cost = spill_cost;
		}
	}
	
	printf("Optimistically choosing %%%u for simplifying\n", min_reg->index);
	
	ptrset_remove(&state->spill_queue, min_reg);
	fixed_queue_push(&state->simplify_queue, min_reg);
	min_reg->state = lima_pp_lir_reg_state_to_simplify;
	
	freeze_moves(min_reg, state);
}

static void assign_colors(state_t* state)
{
	unsigned i, j, k, l;
	
	i = state->select_stack_index;
	while (i > 0)
	{
		i--;
		lima_pp_lir_reg_t* reg = state->select_stack[i];
		
		bool conflicts;
		//Iterate over each vec4 register
		for (j = 0; j < 6; j++)
		{
			//Iterate over each position in the vec4 register
			
			for (k = 0; k < (reg->beginning ? 1 : 5 - reg->size); k++)
			{
				//Check for conflicts
				conflicts = false;
				for (l = 0; l < ptr_vector_size(reg->adjacent); l++)
				{
					lima_pp_lir_reg_t* other = ptr_vector_get(reg->adjacent, l);
					other = get_alias(other, NULL);
					
					if (other->precolored && other->index == j)
					{
						conflicts = true;
						break;
					}
					
					if (other->precolored ||
						other->state != lima_pp_lir_reg_state_colored ||
						other->allocated_index != j)
						continue;
					
					unsigned start_l = other->allocated_offset;
					unsigned end_l = start_l + other->size - 1;
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
					reg->allocated_index = j;
					reg->allocated_offset = k;
					reg->state = lima_pp_lir_reg_state_colored;
					printf("Register %%%u getting index %u, offset %u\n",
						   reg->index, j, k);
					break;
				}
			}
			
			if (!conflicts)
				break;
		}
		
		if (conflicts)
		{
			printf("Failed to find a position for register %%%u\n", reg->index);
			ptrset_add(&state->spilled_regs, reg);
			reg->state = lima_pp_lir_reg_state_spilled;
		}
	}
}

static void replace_reg_src(lima_pp_lir_instr_t* instr, unsigned arg,
							  lima_pp_lir_reg_t* src, lima_pp_lir_reg_t* dst,
							  unsigned* swizzle)
{
	if (!instr->sources[arg].pipeline &&
		!instr->sources[arg].constant)
	{
		lima_pp_lir_reg_t* reg = instr->sources[arg].reg;
		if (reg == src)
		{
			unsigned i;
			
			instr->sources[arg].reg = dst;
			for (i = 0; i < lima_pp_lir_arg_size(instr, arg); i++)
				instr->sources[arg].swizzle[i] = swizzle[instr->sources[arg].swizzle[i]];
		}
	}
}

static void replace_reg_dest(lima_pp_lir_instr_t* instr,
							  lima_pp_lir_reg_t* src, lima_pp_lir_reg_t* dst,
							  unsigned* swizzle)
{
	if (!instr->dest.pipeline &&
		instr->dest.reg == src)
	{
		bool new_mask[4] = {false};
		unsigned i;
		
		for (i = 0; i < src->size; i++)
			new_mask[swizzle[i]] = instr->dest.mask[i];
		for (i = 0; i < dst->size; i++)
			instr->dest.mask[i] = new_mask[i];
		
		instr->dest.reg = dst;
		
		for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
			if (lima_pp_hir_op[instr->op].arg_sizes[i] == 0)
			{
				unsigned new_swizzle[4], j;
				memcpy(new_swizzle, instr->sources[i].swizzle, 4 * sizeof(unsigned));
				for (j = 0; j < src->size; j++)
					new_swizzle[swizzle[j]] = instr->sources[i].swizzle[j];
				memcpy(instr->sources[i].swizzle, new_swizzle, 4 * sizeof(unsigned));
			}
	}
}

//Replaces a given register with a given subset of another register,
//with a possible swizzle
//Used for register allocation and copy propagation
//Each channel x of src will be replaced with channel swizzle[x] of dst

static void replace_reg(lima_pp_lir_reg_t* src, lima_pp_lir_reg_t* dst,
						unsigned* swizzle)
{
	ptrset_iter_t iter = ptrset_iter_create(src->defs);
	lima_pp_lir_instr_t* def;
	ptrset_iter_for_each(iter, def)
	{
		replace_reg_dest(def, src, dst, swizzle);
	}
	
	ptrset_union(&dst->defs, src->defs);
	ptrset_empty(&src->defs);
	
	iter = ptrset_iter_create(src->uses);
	lima_pp_lir_instr_t* use;
	ptrset_iter_for_each(iter, use)
	{
		unsigned i;
		for (i = 0; i < lima_pp_hir_op[use->op].args; i++)
			replace_reg_src(use, i, src, dst, swizzle);
	}
	
	ptrset_union(&dst->uses, src->uses);
	ptrset_empty(&src->uses);
}

//After register allocation, rewrites the program according to the calculated
//colorings & coalescings
static void rewrite_regs(lima_pp_lir_prog_t* prog)
{
	unsigned i, j;
	for (i = 0; i < prog->num_regs; i++)
	{
		lima_pp_lir_reg_t* reg = prog->regs[i];
		if (reg->precolored)
			continue;
		
		unsigned swizzle[4] = {0, 0, 0, 0};
		lima_pp_lir_reg_t* alias = get_alias(reg, swizzle);
		
		lima_pp_lir_reg_t* allocated_reg;
		unsigned start_pos;
		if (alias->precolored)
		{
			start_pos = 0;
			allocated_reg = alias;
		}
		else
		{
			start_pos = alias->allocated_offset;
			allocated_reg = prog->regs[alias->allocated_index];
		}

		for (j = 0; j < reg->size; j++)
			swizzle[j] = swizzle[j] + start_pos;
		
		replace_reg(reg, allocated_reg, swizzle);
	}
}

static bool is_dead_move(lima_pp_lir_instr_t* instr)
{
	if (!is_move(instr))
		return false;
	
	if (instr->dest.reg != instr->sources[0].reg)
		return false;
	
	unsigned i;
	for (i = 0; i < 4; i++)
	{
		if (!instr->dest.mask[i])
			continue;
		
		if (instr->sources[0].swizzle[i] != i)
			return false;
	}
	
	return true;
}

//Removes useless moves left over from the coalescing process
static void remove_dead_moves(lima_pp_lir_prog_t* prog)
{
	unsigned i, j;
	for (i = 0; i < prog->num_blocks; i++)
	{
		lima_pp_lir_block_t* block = prog->blocks[i];
		lima_pp_lir_scheduled_instr_t* instr, *temp;
		pp_lir_block_for_each_instr_safe(block, temp, instr)
		{
			for (j = 0; j < 5; j++)
			{
				if (instr->alu_instrs[j] && is_dead_move(instr->alu_instrs[j]))
				{
					lima_pp_lir_instr_delete(instr->alu_instrs[j]);
					instr->alu_instrs[j] = NULL;
				}
			}
			
			if (lima_pp_lir_sched_instr_is_empty(instr) &&
				(!block->is_end || block->num_instrs > 1))
				lima_pp_lir_block_remove(instr);
		}
	}
}


//Really simple register spilling

static void calc_spill_instr(lima_pp_lir_instr_t* instr, lima_pp_lir_reg_t* reg,
							 bool* load, unsigned* components_written)
{
	if (!ptrset_contains(reg->defs, instr) && !ptrset_contains(reg->uses, instr))
		return;
	
	unsigned i, j;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].pipeline || instr->sources[i].reg != reg)
			continue;
		
		for (j = 0; j < lima_pp_lir_arg_size(instr, i); j++)
		{
			if (!lima_pp_lir_channel_used(instr, i, j))
				continue;
			
			if (!((*components_written >> instr->sources[i].swizzle[j]) & 1))
				*load = true;
		}
	}
	
	if (!instr->dest.pipeline && instr->dest.reg == reg)
	{
		for (i = 0; i < 4; i++)
		{
			if (instr->dest.mask[i])
				*components_written |= 1 << i;
		}
	}
}

static void reg_to_pipeline_reg_instr(lima_pp_lir_instr_t* instr,
									  lima_pp_lir_reg_t* reg)
{
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (instr->sources[i].pipeline ||
			instr->sources[i].reg != reg)
			continue;
		
		instr->sources[i].reg = NULL;
		instr->sources[i].pipeline = true;
		instr->sources[i].pipeline_reg = lima_pp_lir_pipeline_reg_uniform;
		ptrset_remove(&reg->uses, instr);
	}
}

static void reg_to_pipeline_reg(lima_pp_lir_scheduled_instr_t* instr,
								lima_pp_lir_reg_t* reg)
{
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			reg_to_pipeline_reg_instr(instr->alu_instrs[i], reg);
	
	if (instr->temp_store_instr)
		reg_to_pipeline_reg_instr(instr->temp_store_instr, reg);
	
	if (instr->branch_instr)
		reg_to_pipeline_reg_instr(instr->branch_instr, reg);
}

static void reg_to_reg_instr(lima_pp_lir_instr_t* instr, lima_pp_lir_reg_t* reg,
							 lima_pp_lir_reg_t* new_reg)
{
	if (!instr->dest.pipeline && instr->dest.reg == reg)
	{
		ptrset_remove(&reg->defs, instr);
		ptrset_add(&new_reg->defs, instr);
		instr->dest.reg = new_reg;
	}
	
	unsigned i;
	for (i = 0; i < lima_pp_hir_op[instr->op].args; i++)
	{
		if (!instr->sources[i].pipeline && instr->sources[i].reg == reg)
		{
			ptrset_remove(&reg->defs, instr);
			ptrset_add(&new_reg->defs, instr);
			instr->sources[i].reg = new_reg;
		}
	}
}

static void reg_to_reg_sched_instr(lima_pp_lir_scheduled_instr_t* instr,
								   lima_pp_lir_reg_t* reg,
								   lima_pp_lir_reg_t* new_reg)
{
	if (instr->varying_instr)
		reg_to_reg_instr(instr->varying_instr, reg, new_reg);
	
	if (instr->texld_instr)
		reg_to_reg_instr(instr->texld_instr, reg, new_reg);
	
	if (instr->uniform_instr)
		reg_to_reg_instr(instr->uniform_instr, reg, new_reg);
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			reg_to_reg_instr(instr->alu_instrs[i], reg, new_reg);
	
	if (instr->temp_store_instr)
		reg_to_reg_instr(instr->temp_store_instr, reg, new_reg);
	
	if (instr->branch_instr)
		reg_to_reg_instr(instr->branch_instr, reg, new_reg);
}

static bool spill_sched_instr(lima_pp_lir_scheduled_instr_t* instr,
							  lima_pp_lir_reg_t* reg, unsigned index)
{	
	bool load = false;
	unsigned components_written = 0;
	bool can_pipeline = true;
	
	if (instr->varying_instr)
	{
		calc_spill_instr(instr->varying_instr, reg, &load, &components_written);
		can_pipeline = false;
	}
	
	if (instr->texld_instr)
	{
		calc_spill_instr(instr->texld_instr, reg, &load, &components_written);
		can_pipeline = false;
	}
	
	if (instr->uniform_instr)
	{
		calc_spill_instr(instr->uniform_instr, reg, &load, &components_written);
		can_pipeline = false;
	}
	
	unsigned i;
	for (i = 0; i < 5; i++)
		if (instr->alu_instrs[i])
			calc_spill_instr(instr->alu_instrs[i], reg, &load,
							 &components_written);
	
	if (instr->temp_store_instr)
		calc_spill_instr(instr->temp_store_instr, reg, &load,
						 &components_written);
	
	if (instr->branch_instr)
		calc_spill_instr(instr->branch_instr, reg, &load, &components_written);
	
	can_pipeline = can_pipeline && !components_written && !instr->uniform_instr;
	
	if (components_written != (1 << reg->size) - 1)
		load = true;
	
	lima_pp_lir_reg_t* new_reg = NULL;
	if (!can_pipeline)
	{
		new_reg = lima_pp_lir_reg_create();
		if (!new_reg)
			return false;
		
		new_reg->index = instr->block->prog->reg_alloc++;
		new_reg->precolored = false;
		new_reg->size = 4;
		new_reg->beginning = true;
		
		if (!lima_pp_lir_prog_append_reg(instr->block->prog, new_reg))
		{
			lima_pp_lir_reg_delete(new_reg);
			return false;
		}
		
		reg_to_reg_sched_instr(instr, reg, new_reg);
	}
	
	if (load)
	{
		lima_pp_lir_instr_t* load_instr = lima_pp_lir_instr_create();
		if (!load)
			return false;
		
		load_instr->op = lima_pp_hir_op_loadt_four;
		
		load_instr->dest.reg = NULL;
		load_instr->dest.pipeline = true;
		load_instr->dest.pipeline_reg = lima_pp_lir_pipeline_reg_uniform;
		load_instr->load_store_index = index;
		
		for (i = 0; i < 4; i++)
			load_instr->dest.mask[i] = true;
		
		if (can_pipeline)
		{
			instr->uniform_instr = load_instr;
			load_instr->sched_instr = instr;
			reg_to_pipeline_reg(instr, reg);
		}
		else
		{
			lima_pp_lir_instr_t* mov_instr = lima_pp_lir_instr_create();
			if (!mov_instr)
			{
				lima_pp_lir_instr_delete(load_instr);
				return false;
			}
			
			mov_instr->op = lima_pp_hir_op_mov;
			
			mov_instr->sources[0].constant = false;
			mov_instr->sources[0].pipeline = true;
			mov_instr->sources[0].absolute = false;
			mov_instr->sources[0].negate = false;
			mov_instr->sources[0].pipeline_reg = lima_pp_lir_pipeline_reg_uniform;
			
			unsigned i;
			for (i = 0; i < 4; i++)
			{
				mov_instr->sources[0].swizzle[i] = i;
				mov_instr->dest.mask[i] = true;
			}
			
			mov_instr->dest.modifier = lima_pp_outmod_none;
			mov_instr->dest.reg = reg;
			
			ptrset_add(&new_reg->defs, mov_instr);
			
			lima_pp_lir_scheduled_instr_t* load_sched_instr =
				lima_pp_lir_instr_to_sched_instr(mov_instr);
			
			if (!load_sched_instr)
			{
				lima_pp_lir_instr_delete(mov_instr);
				lima_pp_lir_instr_delete(load_instr);
				return false;
			}
			
			load_sched_instr->uniform_instr = load_instr;
			load_instr->sched_instr = load_sched_instr;
			
			lima_pp_lir_block_insert_before(load_sched_instr, instr);
		}
	}
	
	if (components_written)
	{
		lima_pp_lir_instr_t* store_instr = lima_pp_lir_instr_create();
		
		if (!store_instr)
			return false;
		
		store_instr->op = lima_pp_hir_op_storet_four;
		
		store_instr->sources[0].constant = false;
		store_instr->sources[0].pipeline = false;
		store_instr->sources[0].absolute = false;
		store_instr->sources[0].negate = false;
		store_instr->sources[0].reg = new_reg;
		store_instr->load_store_index = index;
		
		for (i = 0; i < 4; i++)
			store_instr->sources[0].swizzle[i] = i;
		
		ptrset_add(&new_reg->uses, store_instr);
		
		lima_pp_lir_scheduled_instr_t* sched_store_instr =
			lima_pp_lir_instr_to_sched_instr(store_instr);
		
		if (!sched_store_instr)
		{
			lima_pp_lir_instr_delete(store_instr);
			return false;
		}
		
		lima_pp_lir_block_insert(sched_store_instr, instr);
	}
	
	return true;
}

static void delete_reg(lima_pp_lir_reg_t* reg, lima_pp_lir_prog_t* prog)
{
	unsigned i;
	for (i = 0; i < prog->num_regs; i++)
		if (prog->regs[i] == reg)
		{
			lima_pp_lir_prog_delete_reg(prog, i);
			lima_pp_lir_reg_delete(reg);
			break;
		}
}

static bool spill_reg(lima_pp_lir_reg_t* reg, lima_pp_lir_prog_t* prog)
{
	ptrset_t sched_defs_uses;
	if (!ptrset_create(&sched_defs_uses))
		return false;
	
	ptrset_iter_t iter = ptrset_iter_create(reg->defs);
	lima_pp_lir_instr_t* def;
	ptrset_iter_for_each(iter, def)
	{
		ptrset_add(&sched_defs_uses, def->sched_instr);
	}
	
	iter = ptrset_iter_create(reg->uses);
	lima_pp_lir_instr_t* use;
	ptrset_iter_for_each(iter, use)
	{
		ptrset_add(&sched_defs_uses, use->sched_instr);
	}
	
	unsigned index = prog->temp_alloc++;
	
	iter = ptrset_iter_create(sched_defs_uses);
	lima_pp_lir_scheduled_instr_t* instr;
	ptrset_iter_for_each(iter, instr)
	{
		if (!spill_sched_instr(instr, reg, index))
		{
			ptrset_delete(sched_defs_uses);
			return false;
		}
	}
	
	delete_reg(reg, prog);
	
	return true;
}

static bool queues_empty(state_t* state)
{
	return fixed_queue_is_empty(state->simplify_queue) &&
	ptrset_size(state->move_queue) == 0 &&
	ptrset_size(state->freeze_queue) == 0 &&
	ptrset_size(state->spill_queue) == 0;
}

bool lima_pp_lir_regalloc(lima_pp_lir_prog_t* prog)
{
	unsigned i, j;
	
	for (i = 0; i < prog->num_regs; i++)
		prog->regs[i]->spilled = false;
	
	while (true)
	{
		init_regs(prog);
		
		if (!lima_pp_lir_liveness_init(prog))
			return false;
		
		lima_pp_lir_liveness_calc_prog(prog);
		
		lima_pp_lir_prog_print(prog, true);
		
		bitset_t detailed_int_matrix = calc_detailed_int_matrix(prog);
		lima_pp_lir_liveness_delete(prog);
		bitset_t coarse_int_matrix = calc_coarse_int_matrix(detailed_int_matrix,
															prog->reg_alloc);
		
		for (i = 0; i < 1 + prog->reg_alloc; i++)
		{
			for (j = 0; j < 1 + prog->reg_alloc; j++)
			{
				if (bitset_get(coarse_int_matrix, (1 + prog->reg_alloc)*i + j))
					printf("1, ");
				else
					printf("0, ");
			}
			printf("\n");
		}

		state_t state;
		if (!create_state(&state, prog->reg_alloc))
		{
			bitset_delete(detailed_int_matrix);
			bitset_delete(coarse_int_matrix);
			return false;
		}
		
		init_moves(&state, prog);
		init_reg_queues(&state, prog);
		
		while (!queues_empty(&state))
		{
			if (!fixed_queue_is_empty(state.simplify_queue))
				simplify(&state);
			else if (ptrset_size(state.move_queue) != 0)
				coalesce(&state, detailed_int_matrix, coarse_int_matrix, prog,
						 prog->reg_alloc);
			else if (ptrset_size(state.freeze_queue) != 0)
				freeze(&state);
			else if (ptrset_size(state.spill_queue) != 0)
				select_spill(&state);
		}
		
		assign_colors(&state);
		
		bitset_delete(coarse_int_matrix);
		bitset_delete(detailed_int_matrix);
		
		if (ptrset_size(state.spilled_regs) == 0)
		{
			delete_state(&state);
			break;
		}
		
		ptrset_iter_t iter = ptrset_iter_create(state.spilled_regs);
		lima_pp_lir_reg_t* reg;
		ptrset_iter_for_each(iter, reg)
		{
			if (!spill_reg(reg, prog))
			{
				delete_state(&state);
				return false;
			}
		}
		
		delete_state(&state);
	}
	
	rewrite_regs(prog);
	remove_dead_moves(prog);
	return true;
}
