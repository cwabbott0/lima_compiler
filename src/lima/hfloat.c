/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *
 * Copyright (c) 2013 Codethink (http://www.codethink.co.uk)
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



#include "hfloat.h"



typedef union
__attribute__((__packed__))
{
	struct __attribute__((__packed__))
	{
		unsigned fraction : 23;
		unsigned exponent :  8;
		bool     sign     :  1;
	};
	float    value;
	uint32_t mask;
} float32__t;



float ogt_hfloat_to_float(ogt_hfloat_t value)
{
	float32__t ret;
	ret.sign = value.sign;

	if (value.exponent == 0x1F)
	{
		ret.exponent = 0xFF;
	}
	else if (value.exponent == 0x00)
	{
		ret.exponent = 0x00;
	}
	else
	{
		int exponent = value.exponent - 0x10;
		ret.exponent = exponent + 0x80;
	}

	ret.fraction = (value.fraction << 13);
	return ret.value;
}

ogt_hfloat_t ogt_hfloat_from_float(float value)
{
	float32__t f = { .value = value };
	ogt_hfloat_t h;

	h.sign = f.sign;
	if (f.exponent <= (0x80 - 0x10))
	{
		h.exponent = 0;
		h.fraction = 0;
	}
	else if(f.exponent >= (0x80 + 0x0F))
	{
		h.exponent = 0x1F;
		h.fraction = 0x3FF;
	}
	else
	{
		h.exponent = f.exponent - 0x70;
		h.fraction = (f.fraction >> 13);
	}

	return h;
}



#include <stdio.h>

void ogt_hfloat_print(ogt_hfloat_t value)
{
	printf("%f", ogt_hfloat_to_float(value));
}

unsigned ogt_hfloat_parse(
	const char* src, ogt_hfloat_t* value)
{
	float f;
	unsigned i;
	if (sscanf(src, "%f%n", &f, &i) != 1)
		return 0;
	if (value)
		*value = ogt_hfloat_from_float(f);
	return i;
}
