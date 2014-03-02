# A simple makefile to build glsl-optimizer on linux

CPPFLAGS += -I ../talloc \
	-I ../mesa \
	-I ../mapi \
	-I ../../include

CXXFLAGS += -Wall -fPIC -DDEBUG -Os -fdata-sections -ffunction-sections
CFLAGS += -Wall -fPIC -DDEBUG -Os -fdata-sections -ffunction-sections

# This list gleaned from the VC project file. Update when needed
SRC_CXX = ast_array_index.cpp \
	ast_expr.cpp \
	ast_function.cpp \
	ast_to_hir.cpp \
	ast_type.cpp \
	builtin_functions.cpp \
	builtin_types.cpp \
	builtin_variables.cpp \
	glsl_lexer.cpp \
	glsl_parser.cpp \
	glsl_parser_extras.cpp \
	glsl_symbol_table.cpp \
	glsl_types.cpp \
	hir_field_selection.cpp \
	ir.cpp \
	ir_basic_block.cpp \
	ir_builder.cpp \
	ir_clone.cpp \
	ir_constant_expression.cpp \
	ir_dead_branches.cpp \
	ir_equals.cpp \
	ir_expression_flattening.cpp \
	ir_function.cpp \
	ir_function_can_inline.cpp \
	ir_function_detect_recursion.cpp \
	ir_hierarchical_visitor.cpp \
	ir_hv_accept.cpp \
	ir_import_prototypes.cpp \
	ir_loop_jumps.cpp \
	ir_print_visitor.cpp \
	ir_reader.cpp \
	ir_rvalue_visitor.cpp \
	ir_validate.cpp \
	ir_variable_refcount.cpp \
	linker.cpp \
	link_atomics.cpp \
	link_functions.cpp \
	link_interface_blocks.cpp \
	link_uniform_block_active_visitor.cpp \
	link_uniform_blocks.cpp \
	link_uniform_initializers.cpp \
	link_uniforms.cpp \
	link_varyings.cpp \
	loop_analysis.cpp \
	loop_controls.cpp \
	loop_unroll.cpp \
	lower_clip_distance.cpp \
	lower_discard.cpp \
	lower_discard_flow.cpp \
	lower_if_to_cond_assign.cpp \
	lower_instructions.cpp \
	lower_jumps.cpp \
	lower_mat_op_to_vec.cpp \
	lower_named_interface_blocks.cpp \
	lower_noise.cpp \
	lower_output_reads.cpp \
	lower_packed_varyings.cpp \
	lower_packing_builtins.cpp \
	lower_texture_projection.cpp \
	lower_ubo_reference.cpp \
	lower_variable_index_to_cond_assign.cpp \
	lower_vec_index_to_cond_assign.cpp \
	lower_vec_index_to_swizzle.cpp \
	lower_vector.cpp \
	lower_vector_insert.cpp \
	opt_algebraic.cpp \
	opt_array_splitting.cpp \
	opt_constant_folding.cpp \
	opt_constant_propagation.cpp \
	opt_constant_variable.cpp \
	opt_copy_propagation.cpp \
	opt_copy_propagation_elements.cpp \
	opt_cse.cpp \
	opt_dead_builtin_varyings.cpp \
	opt_dead_code.cpp \
	opt_dead_code_local.cpp \
	opt_dead_functions.cpp \
	opt_flatten_nested_if_blocks.cpp \
	opt_flip_matrices.cpp \
	opt_from_ssa.cpp \
	opt_function_inlining.cpp \
	opt_if_simplification.cpp \
	opt_noop_swizzle.cpp \
	opt_redundant_jumps.cpp \
	opt_structure_splitting.cpp \
	opt_swizzle_swizzle.cpp \
	opt_to_ssa.cpp \
	opt_tree_grafting.cpp \
	opt_vectorize.cpp \
	s_expression.cpp \
	standalone_scaffolding.cpp \

SRC_C = ralloc.c \
	glcpp/glcpp-lex.c \
	glcpp/glcpp-parse.c \
	glcpp/pp.c \
	strtod.c \
	../mesa/program/prog_hash_table.c \
	../mesa/program/symbol_table.c \
	../mesa/main/hash_table.c \
	../mesa/main/imports.c \


CXX_OBJS = $(patsubst %.cpp, %.o, $(SRC_CXX))
C_OBJS = $(patsubst %.c, %.o, $(SRC_C))

$(CXX_OBJS): %.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(C_OBJS): %.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

LIBNAME = libglsl.a

.PHONY: clean all

all: $(C_OBJS) $(CXX_OBJS)
	ar crs $(LIBNAME) $(C_OBJS) $(CXX_OBJS)

clean:
	rm -f $(C_OBJS) $(CXX_OBJS) $(LIBNAME)
