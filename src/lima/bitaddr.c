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



#include <stdint.h>
#include <string.h>



static uint32_t _bitread32(uint32_t* src, unsigned offset, uint32_t size)
{
	unsigned read_max  = (32 - offset);
	unsigned read_size = (size > read_max ? read_max : size);
	uint32_t data = (src[0] >> offset)
		& ((1ULL << read_size) - 1);
	if (read_size < size)
	{
		data |= (src[1] << read_size);
		data &= ((1ULL << size) - 1);
	}
	return data;
}

static void _bitwrite32(
	uint32_t* dst, uint32_t offset, unsigned size, uint32_t value)
{
	unsigned write_max  = (32 - offset);
	unsigned write_size = (size > write_max ? write_max : size);
	dst[0] &= ~(((1ULL << write_size) - 1) << offset);
	dst[0] |= (value & ((1ULL << write_size) - 1)) << offset;
	if (write_size < size)
	{
		dst[1] &= ~((1ULL << (size - write_size)) - 1);
		dst[1] |= value >> write_size;
	}
}



void bitclear(uint32_t* dst, uint32_t dst_offset, uint32_t size)
{
	dst += (dst_offset >> 5);
	dst_offset &= 0x1F;

	if (dst_offset == 0)
	{
		memset(dst, 0x00, (size >> 5));
		dst += (size >> 5);
		size &= 0x1F;
	} else {
		for(; size >= 32; size -= 32)
			_bitwrite32(dst++, dst_offset, 32, 0);
	}
	if (size != 0)
		_bitwrite32(dst, dst_offset, size, 0);
}

void bitcopy(
	uint32_t* dst, uint32_t dst_offset,
	uint32_t* src, uint32_t src_offset,
	uint32_t size)
{
	if (!src)
	{
		bitclear(dst, dst_offset, size);
		return;
	}

	dst += (dst_offset >> 5);
	dst_offset &= 0x1F;
	src += (src_offset >> 5);
	src_offset &= 0x1F;

	if ((dst_offset == 0)
		&& (src_offset == 0))
	{
		memcpy(dst, src, (size >> 5) << 2);
		src += (size >> 5);
		dst += (size >> 5);
		size &= 0x1F;
	}

	if (dst_offset == 0) {
		for(; size >= 32; size -=32)
			*dst++ = _bitread32(src++, src_offset, 32);
		_bitwrite32(dst, 0, size,
			_bitread32(src, src_offset, size));
	} else if (src_offset == 0) {
		for(; size >= 32; size -=32)
			_bitwrite32(dst++, dst_offset, 32, *src++);
		_bitwrite32(dst, dst_offset, size, *src);
	} else {
		for(; size >= 32; size -=32)
			_bitwrite32(
				dst++, dst_offset, 32,
				_bitread32(src++, src_offset, 32));
		_bitwrite32(
			dst, dst_offset, size,
			_bitread32(src, src_offset, size));
	}
}
