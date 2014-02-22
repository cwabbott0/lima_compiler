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

#ifndef __SHADER_H__
#define __SHADER_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	lima_shader_stage_vertex,
	lima_shader_stage_fragment,
	lima_shader_stage_unknown
} lima_shader_stage_e;

typedef enum {
	lima_asm_syntax_explicit,
	lima_asm_syntax_verbose,
	lima_asm_syntax_decompile,
	lima_asm_syntax_unknown
} lima_asm_syntax_e;

struct lima_shader_s;
typedef struct lima_shader_s lima_shader_t;

lima_shader_t* lima_shader_create(lima_shader_stage_e stage);
void lima_shader_delete(lima_shader_t* shader);

/*
 * runs the compiler frontend, after running this all compiler errors should
 * be found
 */

bool lima_shader_parse(lima_shader_t* shader, const char* source);

/* run the optimization passes */

void lima_shader_optimize(lima_shader_t* shader);
	
/* compile the code to binary */

bool lima_shader_compile(lima_shader_t* shader);

/* print out the GLSL IR */

void lima_shader_print_glsl(lima_shader_t* shader);

/* were there compiler errors? */

bool lima_shader_error(lima_shader_t* shader);

/*
 * Get the info log after running lima_shader_parse(). The returned string
 * is owned by the shader, and will be freed when lima_shader_delete() is
 * called.
 */

const char* lima_shader_info_log(lima_shader_t* shader);

#ifdef __cplusplus
}
#endif

#endif /*__SHADER_H__*/
