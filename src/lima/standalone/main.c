/* Author(s):
 *   Connor Abbott
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include "shader.h"

#define USAGE \
"usage: limasc -t [vert|frag] -o [output] input \n" \
"\n" \
"options:\n" \
"\t--type (-t) [vert|frag] -- choose which kind of shader\n" \
"\t--core (-c) -- choose which processor to compile for.\n" \
"\t\tMali-200\n" \
"\t\tMali-400\n" \
"\t\tDefault: Mali-400\n" \
"\t--dump-hir -- print the GLSL IR before optimization\n" \
"\t--dump-lir -- print the GLSL IR after optimization\n" \
"\t--dump-asm (-d) -- print out the resulting assembly\n" \
"\t--syntax [verbose|explicit|decompile] -- " \
"choose the syntax for the disassembly\n\n" \
"\t\tFor vertex shaders: verbose will dump the raw fields, with\n" \
"\t\tlittle interpretation except for suppressing certain fields\n" \
"\t\twhen they are unused. Explicit will print a more normal\n" \
"\t\tassembly, but due to the nature of the instruction set it\n" \
"\t\twill still be verbose and unreadable. Decompile will try to\n" \
"\t\tproduce a more readable output at the expense of losing some\n" \
"\t\tdetails (such as how efficient the assembly is).\n\n" \
"\t\tFor fragment shaders: verbose will print out a more readable\n" \
"\t\tassembly, but sometimes it will be less clear which instructions\n" \
"\t\tare scheduled in which unit. The explicit syntax is more\n" \
"\t\tassembly-like and easier to parse, but at the expense of being\n" \
"\t\tless readable. Decompile is invalid for fragment shaders.\n\n" \
"\t\tExplicit is the default for vertex shaders, while verbose is the \n" \
"\t\tdefault for fragment shaders.\n\n" \
"\t--output (-o) -- the output file. Defaults to out.mbs\n" \
"\t--help (-h) -- print this message and quit.\n"

static void usage(void)
{
	fprintf(stderr, USAGE);
}

static const char* read_file(const char* path)
{
	FILE* fp = fopen(path, "rb");
	if (!fp) return NULL;
	
	if (fseek(fp, 0, SEEK_END) != 0)
	{
		fclose(fp);
		return NULL;
	}
	long fsize = ftell(fp);
	if ((fsize <= 0)
		|| (fseek(fp, 0, SEEK_SET) != 0))
	{
		fclose(fp);
		return NULL;
	}
	
	char* data = (char*)malloc(fsize + 1);
	if (!data)
	{
		fclose(fp);
		return NULL;
	}
	
	if (fread(data, fsize, 1, fp) != 1)
	{
		fclose(fp);
		free(data);
		return NULL;
	}
	data[fsize] = '\0';
	
	fclose(fp);
	return data;
}

static void shader_errors(lima_shader_t* shader)
{
	fprintf(stderr, "There were error(s) during compilation.\n");
	fprintf(stderr, "Info log:\n%s", lima_shader_info_log(shader));
	exit(1);
}

int main(int argc, char** argv)
{
	bool dump_asm = false, dump_hir = false, dump_lir = false;
	lima_shader_stage_e stage = lima_shader_stage_unknown;
	lima_core_e core = lima_core_mali_400;
	lima_asm_syntax_e syntax = lima_asm_syntax_unknown;
	char* outfile = NULL;
	char* infile = NULL;
	
	static struct option long_options[] = {
		{"type",     required_argument, NULL, 't'},
		{"core",     required_argument, NULL, 'c'},
		{"dump-hir", no_argument,       NULL, 'i'},
		{"dump-lir", no_argument,       NULL, 'l'},
		{"dump-asm", no_argument,       NULL, 'd'},
		{"syntax",   required_argument, NULL, 's'},
		{"output",   required_argument, NULL, 'o'},
		{"help",     no_argument,       NULL, 'h'},
		{0, 0, 0, 0}
	};
	
	while (true)
	{
		int option_index = 0;
		
		int c = getopt_long(argc, argv, "t:c:ds:o:h", long_options, &option_index);
		
		if (c == -1)
			break;
		
		switch (c)
		{
			case 't':
				if (strcmp(optarg, "vert") == 0)
					stage = lima_shader_stage_vertex;
				else if (strcmp(optarg, "frag") == 0)
					stage = lima_shader_stage_fragment;
				else
				{
					fprintf(stderr, "Error: unknown shader type %s\n", optarg);
					usage();
					exit(1);
				}
				break;
				
			case 'c':
				if (strcmp(optarg, "Mali-200") == 0)
					core = lima_core_mali_200;
				else if (strcmp(optarg, "Mali-400") == 0)
					core = lima_core_mali_400;
				else
				{
					fprintf(stderr, "Error: unknown core type %s\n", optarg);
					usage();
					exit(1);
				}
			
			case 'd':
				dump_asm = true;
				break;
			
			case 'i':
				dump_hir = true;
				break;
				
			case 'l':
				dump_lir = true;
				break;
				
			case 's':
				if (strcmp(optarg, "explicit") == 0)
					syntax = lima_asm_syntax_explicit;
				else if (strcmp(optarg, "verbose") == 0)
					syntax = lima_asm_syntax_verbose;
				else if (strcmp(optarg, "decompile") == 0)
					syntax = lima_asm_syntax_decompile;
				else
				{
					fprintf(stderr, "Error: unknown assembly syntax %s\n",
							optarg);
					usage();
					exit(1);
				}
				break;
				
			case 'o':
				if (outfile)
				{
					fprintf(stderr, "Error: output file specified more than once\n");
					usage();
					exit(1);
				}
				outfile = optarg;
				break;
				
			case 'h':
				usage();
				exit(0);
				
			case '?':
				usage();
				exit(1);
				break;
			
			default:
				abort();
		}
	}
	
	if (stage == lima_shader_stage_unknown)
	{
		fprintf(stderr, "Error: no shader type specified\n");
		usage();
		exit(1);
	}
	
	if (syntax == lima_asm_syntax_unknown)
	{
		switch (stage)
		{
			case lima_shader_stage_vertex:
				syntax = lima_asm_syntax_explicit;
				break;
			
			case lima_shader_stage_fragment:
				syntax = lima_asm_syntax_verbose;
				break;
				
			default:
				abort();
		}
	}
	
	if (optind == argc)
	{
		fprintf(stderr, "Error: no input specified\n");
		usage();
		exit(1);
	}
	
	if (optind < argc - 1)
	{
		fprintf(stderr, "Error: more than one input specified\n");
		usage();
		exit(1);
	}
	
	infile = argv[optind];
	if (!outfile)
		outfile = "out.mbs";
	
	const char* source = read_file(infile);
	if (!source)
	{
		fprintf(stderr, "Error: could not read input file %s\n", infile);
		usage();
		exit(1);
	}
	
	lima_shader_t* shader = lima_shader_create(stage, core);
	lima_shader_parse(shader, source);
	if (lima_shader_error(shader))
		shader_errors(shader);
	
	if (dump_hir)
	{
		printf("HIR:\n\n");
		lima_shader_print_glsl(shader);
		printf("\n\n");
	}
	
	lima_shader_optimize(shader);
	
	if (dump_lir)
	{
		printf("LIR:\n\n");
		lima_shader_print_glsl(shader);
		printf("\n\n");
	}
	
	lima_shader_compile(shader);
	
	if (lima_shader_error(shader))
		shader_errors(shader);
	
	mbs_chunk_t* chunk = lima_shader_export_offline(shader);
	if (!chunk)
		return 1;
	
	unsigned size = mbs_chunk_size(chunk);
	void* data = malloc(size);
	if (!data)
		return 1;
	
	mbs_chunk_export(chunk, data);
	
	FILE* fp = fopen(outfile, "wb");
	if (!fp)
	{
		fprintf(stderr, "Failed to open output file\n");
		return 1;
	}
	
	fwrite(data, 1, size, fp);
	
	fclose(fp);
	free(data);
	lima_shader_delete(shader);
	
	return 0;
}
