/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *   Connor Abbott
 *
 * Copyright (c) 2013
 *   Codethink (http://www.codethink.co.uk)
 *   Connor Abbott (connor@abbott.cx)
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



#include "lima_pp.h"
#include "bitaddr.h"

#include <stdlib.h>
#include <stdio.h>



void lima_pp_instruction_calc_size(lima_pp_ctrl_t* control)
{
	unsigned i, size;
	for (i = 0, size = 32; i < lima_pp_field_count; i++)
	{
		if ((control->fields >> i) & 1)
			size += lima_pp_field_size[i];
	}
	
	control->count = ((size + 0x1F) >> 5);
}

void lima_pp_instruction_encode(
	lima_pp_instruction_t* inst, uint32_t* output)
{
	void* field[] =
	{
		&inst->varying,
		&inst->sampler,
		&inst->uniform,
		&inst->vec4_mul,
		&inst->float_mul,
		&inst->vec4_acc,
		&inst->float_acc,
		&inst->combine,
		&inst->temp_write,
		&inst->branch,
		&inst->const0,
		&inst->const1,
	};

	lima_pp_instruction_calc_size(&inst->control);
	output[0] = inst->control.mask;

	unsigned offset, i;
	for (i = 0, offset = 32;
		i < lima_pp_field_count; i++)
	{
		if ((inst->control.fields >> i) & 1)
		{
			unsigned size = lima_pp_field_size[i];
			bitcopy(output, offset, field[i], 0, size);
			offset += size;
		}
	}

	if (offset & 0x1F)
		bitclear(output, offset,
			(32 - (offset & 0x1F)));
}

void lima_pp_instruction_decode(
	uint32_t* source, lima_pp_instruction_t* output)
{
	output->control.mask = source[0];

	void* field[] =
	{
		&output->varying,
		&output->sampler,
		&output->uniform,
		&output->vec4_mul,
		&output->float_mul,
		&output->vec4_acc,
		&output->float_acc,
		&output->combine,
		&output->temp_write,
		&output->branch,
		&output->const0,
		&output->const1,
	};

	unsigned i, offset;
	for (i = 0, offset = 32;
		i < lima_pp_field_count; i++)
	{
		if ((output->control.fields >> i) & 1)
		{
			unsigned size = lima_pp_field_size[i];
			bitcopy(field[i], 0, source, offset, size);
			offset += size;
		}
	}
}
