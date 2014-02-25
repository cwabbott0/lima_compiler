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

#include "shader_internal.h"
#include "ir.h"
#include "ast.h"
#include "glsl_parser.h"
#include "ir_optimization.h"
#include "ir_print_visitor.h"
#include "loop_analysis.h"
#include "program.h"
#include "linker.h"
#include "standalone_scaffolding.h"

extern "C" struct gl_shader *
_mesa_new_shader(struct gl_context *ctx, GLuint name, GLenum type);

static void DeleteShader(struct gl_context *ctx, struct gl_shader *shader)
{
	ralloc_free(shader);
}

lima_shader_t* lima_shader_create(lima_shader_stage_e stage, lima_core_e core)
{
	lima_shader_t* shader = (lima_shader_t*) calloc(1, sizeof(lima_shader_t));
	if (!shader)
		return NULL;
	
	if (!lima_shader_symbols_init(&shader->symbols))
		goto err_mem;
	
	shader->stage = stage;
	shader->core = core;
	shader->parsed = false;
	shader->compiled = false;
	shader->info_log = NULL;
	
	initialize_context_to_defaults(&shader->mesa_ctx, API_OPENGLES2);
	shader->mesa_ctx.Const.GLSLVersion = 100;
	shader->mesa_ctx.Version = 20;
	shader->mesa_ctx.Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 0;
	shader->mesa_ctx.Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits = 4;
	shader->mesa_ctx.Const.MaxDrawBuffers = 1;
	shader->mesa_ctx.Driver.NewShader = _mesa_new_shader;
	shader->mesa_ctx.Driver.DeleteShader = DeleteShader;
	
	shader->mem_ctx = ralloc_context(NULL);
	if (!shader->mem_ctx)
		goto err_mem2;
	
	shader->whole_program = rzalloc(shader->mem_ctx, struct gl_shader_program);
	if (!shader->whole_program)
		goto err_mem2;
	
	shader->whole_program->InfoLog = ralloc_strdup(shader->mem_ctx, "");
	shader->whole_program->NumShaders = 1;
	shader->whole_program->Shaders = reralloc(shader->whole_program,
											  shader->whole_program->Shaders,
											  struct gl_shader *,
											  shader->whole_program->NumShaders);
	
	if (!shader->whole_program->Shaders)
		goto err_mem2;
	
	shader->shader = rzalloc(shader->mem_ctx, gl_shader);
	if (!shader->shader)
		goto err_mem2;
	
	shader->whole_program->Shaders[0] = shader->shader;
	
	switch (stage)
	{
		case lima_shader_stage_vertex:
			shader->shader->Type = GL_VERTEX_SHADER;
			shader->shader->Stage = MESA_SHADER_VERTEX;
			break;
			
		case lima_shader_stage_fragment:
			shader->shader->Type = GL_FRAGMENT_SHADER;
			shader->shader->Stage = MESA_SHADER_FRAGMENT;
			break;
			
		default:
			assert(0);
	}
	
	shader->whole_program->LinkStatus = true;
	
	return shader;
	
	err_mem2:
	
	lima_shader_symbols_delete(&shader->symbols);
	ralloc_free(shader->mem_ctx);
	
	err_mem:
	
	free(shader);
	return NULL;
}

void lima_shader_delete(lima_shader_t* shader)
{
	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
		ralloc_free(shader->whole_program->_LinkedShaders[i]);
	ralloc_free(shader->mem_ctx);
	lima_shader_symbols_delete(&shader->symbols);
	free(shader);
}

bool lima_shader_parse(lima_shader_t* shader, const char* source)
{
	shader->state = new(shader->mem_ctx)
		_mesa_glsl_parse_state(&shader->mesa_ctx, shader->shader->Stage,
							   shader->mem_ctx);
	
	if (!shader->state)
		return false;
	
	shader->errors = false;
	
	shader->state->error = glcpp_preprocess(shader->mem_ctx, &source,
											&shader->state->info_log,
											shader->state->extensions,
											&shader->mesa_ctx);
	if (shader->state->error)
	{
		shader->errors = true;
		shader->info_log = shader->state->info_log;
		return true;
	}
	
	_mesa_glsl_lexer_ctor(shader->state, source);
	_mesa_glsl_parse(shader->state);
	_mesa_glsl_lexer_dtor(shader->state);
	
	if (shader->state->error)
	{
		shader->errors = true;
		shader->info_log = shader->state->info_log;
		return true;
	}
	
	exec_list* ir = new(shader->shader) exec_list();
	if (!ir)
		return false;
	
	shader->shader->ir = ir;
	
	_mesa_ast_to_hir(ir, shader->state);
	
	validate_ir_tree(ir);
	
	shader->shader->symbols = shader->state->symbols;
	shader->shader->uses_builtin_functions = shader->state->uses_builtin_functions;
	
	shader->linked_shader = link_intrastage_shaders(shader->mem_ctx,
													&shader->mesa_ctx,
													shader->whole_program,
													shader->whole_program->Shaders,
													shader->whole_program->NumShaders);
	
	if (!shader->linked_shader)
	{
		shader->errors = true;
		shader->info_log = shader->whole_program->InfoLog;
		return true;
	}
	
	/* lower things we can't support before we optimize or lower to gp_ir or pp_hir */
	do_mat_op_to_vec(shader->linked_shader->ir);
	lower_instructions(shader->linked_shader->ir,
					   DIV_TO_MUL_RCP |
					   EXP_TO_EXP2 |
					   LOG_TO_LOG2 |
					   POW_TO_EXP2 |
					   INT_DIV_TO_MUL_RCP);
	do_vec_index_to_cond_assign(shader->linked_shader->ir);
	lower_vector_insert(shader->linked_shader->ir, true);
	
	/* vertex shaders can't write to a varying or read from an attribute with
	 * a nonconstant index
	 */
	if (shader->stage == lima_shader_stage_vertex)
	{
		lower_variable_index_to_cond_assign(shader->linked_shader->ir,
											true, true, false, false);
	}
	
	validate_ir_tree(shader->linked_shader->ir);
	
	shader->parsed = true;
	return true;
}

void lima_shader_optimize(lima_shader_t* shader)
{
	if (!shader->parsed)
		return;
	
	gl_shader_stage stage = shader->linked_shader->Stage;
	exec_list* ir = shader->linked_shader->ir;
	bool progress = true;
	
	while (progress)
	{
		progress = do_common_optimization(ir, true, false, 0,
										  &shader->mesa_ctx.ShaderCompilerOptions[stage]);
		progress = do_lower_jumps(ir, true, true, false, false, false) || progress;
	}
	
	validate_ir_tree(shader->linked_shader->ir);
}

bool lima_shader_compile(lima_shader_t* shader)
{
	if (!shader->parsed)
		return true;
	
	convert_to_ssa(shader->linked_shader->ir);
	
	lima_convert_symbols(shader);
	if (!lima_shader_symbols_pack(&shader->symbols, shader->stage))
	{
		ralloc_asprintf_append(&shader->info_log,
							   "Error: could not allocate enough space for variables.\n");
		shader->errors = true;
		return true;
	}
	lima_shader_symbols_print(&shader->symbols);
	
	//XXX fill me in
	shader->info.vs.num_instructions = 0;
	shader->info.vs.attrib_prefetch = 0;
	shader->info.fs.stack_size = 1;
	shader->info.fs.stack_offset = 1;
	shader->info.fs.has_discard = false;
	shader->info.fs.reads_color = false;
	shader->info.fs.writes_color = true;
	shader->info.fs.reads_depth = false;
	shader->info.fs.writes_depth = false;
	shader->info.fs.reads_stencil = false;
	shader->info.fs.writes_stencil = false;
	shader->info.fs.first_instr_length = 0;
	
	//TODO
	shader->compiled = true;
	return true;
}

void lima_shader_print_glsl(lima_shader_t* shader)
{
	assert(shader->linked_shader);
	_mesa_print_ir(shader->linked_shader->ir, shader->state);
}

bool lima_shader_error(lima_shader_t* shader)
{
	return shader->errors;
}

const char* lima_shader_info_log(lima_shader_t* shader)
{
	return shader->info_log;
}

lima_shader_info_t lima_shader_get_info(lima_shader_t* shader)
{
	return shader->info;
}

lima_core_e lima_shader_get_core(lima_shader_t* shader)
{
	return shader->core;
}

lima_shader_stage_e lima_shader_get_stage(lima_shader_t* shader)
{
	return shader->stage;
}

lima_shader_symbols_t* lima_shader_get_symbols(lima_shader_t* shader)
{
	return &shader->symbols;
}
