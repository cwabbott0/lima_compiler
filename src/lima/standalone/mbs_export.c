/* Author(s):
 *   Connor Abbott
 *
 * Copyright (c) 2014 Connor Abbott (connor@abbott.cx)
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

#include "shader.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
	uint32_t stack_size;
	uint32_t stack_offset;
} fsta_data_t;

static mbs_chunk_t* export_fsta(lima_shader_info_t info)
{
	mbs_chunk_t* chunk = mbs_chunk_create("FSTA");
	if (!chunk)
		return NULL;
	
	fsta_data_t data = {
		.stack_size = info.fs.stack_size,
		.stack_offset = info.fs.stack_offset
	};
	
	if (!mbs_chunk_append_data(chunk, &data, sizeof(fsta_data_t)))
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	return chunk;
}

typedef struct {
	uint32_t has_discard;
} fdis_data_t;

static mbs_chunk_t* export_fdis(lima_shader_info_t info)
{
	mbs_chunk_t* chunk = mbs_chunk_create("FDIS");
	if (!chunk)
		return NULL;
	
	fdis_data_t data = {
		.has_discard = info.fs.has_discard
	};
	
	if (!mbs_chunk_append_data(chunk, &data, sizeof(fdis_data_t)))
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	return chunk;
}

typedef struct {
	uint8_t reads_color; // gl_FBColor
    uint8_t writes_color; // gl_FragColor
    uint8_t reads_depth; // gl_FBDepth
    uint8_t writes_depth; // ? gl_FragDepth (not supported in GLES2)
    uint8_t reads_stencil; // gl_FBStencil
    uint8_t writes_stencil; // ? gl_FragStencil (not supported in GLES2)
    uint8_t unknown_0; // = 0
    uint8_t unknown_1; // = 0
} fbuu_data_t;

static mbs_chunk_t* export_fbuu(lima_shader_info_t info)
{
	mbs_chunk_t* chunk = mbs_chunk_create("FBUU");
	if (!chunk)
		return NULL;
	
	fbuu_data_t data = {
		.reads_color = info.fs.reads_color,
		.writes_color = info.fs.writes_color,
		.reads_depth = info.fs.reads_depth,
		.writes_depth = info.fs.writes_depth,
		.reads_stencil = info.fs.reads_stencil,
		.writes_stencil = info.fs.writes_stencil,
		.unknown_0 = 0,
		.unknown_1 = 0
	};
	
	if (!mbs_chunk_append_data(chunk, &data, sizeof(fbuu_data_t)))
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	return chunk;
}

typedef struct {
    uint32_t unknown_0; // = 0
    uint32_t num_instructions;
    uint32_t attrib_prefetch;
} fins_data_t;

static mbs_chunk_t* export_fins(lima_shader_info_t info)
{
	mbs_chunk_t* chunk = mbs_chunk_create("FINS");
	if (!chunk)
		return NULL;
	
	fins_data_t data = {
		.unknown_0 = 0,
		.num_instructions = info.vs.num_instructions,
		.attrib_prefetch = info.vs.attrib_prefetch
	};
	
	if (!mbs_chunk_append_data(chunk, &data, sizeof(fins_data_t)))
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	return chunk;
}

static mbs_chunk_t* export_cver(lima_shader_t* shader)
{
	lima_shader_info_t info = lima_shader_get_info(shader);
	
	mbs_chunk_t* chunk = mbs_chunk_create("CVER");
	if (!chunk)
		return NULL;
	
	uint32_t version;
	switch (lima_shader_get_core(shader))
	{
		case lima_core_mali_200:
			version = 2;
			break;
			
		case lima_core_mali_400:
			version = 6;
			break;
			
		default:
			assert(0);
	}
	
	if (!mbs_chunk_append_data(chunk, &version, 4))
		goto err_mem;
	
	mbs_chunk_t* fins_chunk = export_fins(info);
	if (!fins_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, fins_chunk))
	{
		mbs_chunk_delete(fins_chunk);
		goto err_mem;
	}
	
	lima_shader_symbols_t* symbols = lima_shader_get_symbols(shader);
	
	mbs_chunk_t* uniform_chunk = lima_export_uniform_table(symbols);
	if (!uniform_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, uniform_chunk))
	{
		mbs_chunk_delete(uniform_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* attribute_chunk = lima_export_attribute_table(symbols);
	if (!attribute_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, attribute_chunk))
	{
		mbs_chunk_delete(attribute_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* varying_chunk = lima_export_varying_table(symbols);
	if (!varying_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, varying_chunk))
	{
		mbs_chunk_delete(varying_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* code_chunk = mbs_chunk_create("DBIN");
	if (!code_chunk)
		goto err_mem;
	
	//TODO fill with code
	
	if (!mbs_chunk_append(chunk, code_chunk))
	{
		mbs_chunk_delete(code_chunk);
		goto err_mem;
	}
	
	return chunk;
	
	err_mem:
	
	mbs_chunk_delete(chunk);
	return NULL;
}

static mbs_chunk_t* export_cfra(lima_shader_t* shader)
{
	lima_shader_info_t info = lima_shader_get_info(shader);
	
	mbs_chunk_t* chunk = mbs_chunk_create("CFRA");
	if (!chunk)
		return NULL;
	
	uint32_t version;
	switch (lima_shader_get_core(shader))
	{
		case lima_core_mali_200:
			version = 5;
			break;
			
		case lima_core_mali_400:
			version = 7;
			break;
			
		default:
			assert(0);
	}
	
	if (!mbs_chunk_append_data(chunk, &version, 4))
		goto err_mem;
	
	mbs_chunk_t* fsta_chunk = export_fsta(info);
	if (!fsta_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, fsta_chunk))
	{
		mbs_chunk_delete(fsta_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* fdis_chunk = export_fdis(info);
	if (!fdis_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, fdis_chunk))
	{
		mbs_chunk_delete(fdis_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* fbuu_chunk = export_fbuu(info);
	if (!fbuu_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, fbuu_chunk))
	{
		mbs_chunk_delete(fbuu_chunk);
		goto err_mem;
	}
	
	lima_shader_symbols_t* symbols = lima_shader_get_symbols(shader);
	
	mbs_chunk_t* uniform_chunk = lima_export_uniform_table(symbols);
	if (!uniform_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, uniform_chunk))
	{
		mbs_chunk_delete(uniform_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* varying_chunk = lima_export_varying_table(symbols);
	if (!varying_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append(chunk, varying_chunk))
	{
		mbs_chunk_delete(varying_chunk);
		goto err_mem;
	}
	
	mbs_chunk_t* code_chunk = mbs_chunk_create("DBIN");
	if (!code_chunk)
		goto err_mem;
	
	if (!mbs_chunk_append_data(code_chunk, lima_shader_get_code(shader),
							   lima_shader_get_code_size(shader)))
	{
		mbs_chunk_delete(code_chunk);
		goto err_mem;
	}
	
	if (!mbs_chunk_append(chunk, code_chunk))
	{
		mbs_chunk_delete(code_chunk);
		goto err_mem;
	}
	
	return chunk;
	
err_mem:
	
	mbs_chunk_delete(chunk);
	return NULL;
}

mbs_chunk_t* lima_shader_export_offline(lima_shader_t* shader)
{
	mbs_chunk_t* chunk = mbs_chunk_create("MBS1");
	if (!chunk)
		return NULL;
	
	mbs_chunk_t* child_chunk;
	
	switch (lima_shader_get_stage(shader)) {
		case lima_shader_stage_vertex:
			child_chunk = export_cver(shader);
			break;
			
		case lima_shader_stage_fragment:
			child_chunk = export_cfra(shader);
			break;
			
		default:
			assert(0);
	}
	
	if (!child_chunk)
	{
		mbs_chunk_delete(chunk);
		return NULL;
	}
	
	if (!mbs_chunk_append(chunk, child_chunk))
	{
		mbs_chunk_delete(chunk);
		mbs_chunk_delete(child_chunk);
		return NULL;
	}
	
	return chunk;
}
