/* Author(s):
 *   Ben Brewer (ben.brewer@codethink.co.uk)
 *
 * Copyright (c) 2013
 *   Codethink (http://www.codethink.co.uk)
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



#ifndef __ogt_hfloat_h__
#define __ogt_hfloat_h__

#include <stdbool.h>
#include <stdint.h>

typedef union
__attribute__((__packed__))
{
	struct
	__attribute__((__packed__))
	{
		unsigned fraction : 10;
		unsigned exponent :  5;
		bool     sign     :  1;
	};
	uint16_t mask;
} ogt_hfloat_t;

extern float        ogt_hfloat_to_float(ogt_hfloat_t value);
extern ogt_hfloat_t ogt_hfloat_from_float(float value);

extern unsigned ogt_hfloat_parse(const char* src, ogt_hfloat_t* value);
extern void     ogt_hfloat_print(ogt_hfloat_t value);

#endif
