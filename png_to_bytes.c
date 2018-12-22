/*
 * Copyright 2002-2010 Guillaume Cottenceau.
 *
 * This software may be freely redistributed under the terms
 * of the X11 license.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "png_to_bytes.h"
#include "png_to_bytes_opt.h"

#define PNG_DEBUG 3
#include <png.h>

void abort_(const char * s, ...)
{
	va_list args;
	va_start(args, s);
	vfprintf(stderr, s, args);
	fprintf(stderr, "\n");
	va_end(args);
	abort();
}

int width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytepp rows;

void read_png_file(char* file_name)
{
	unsigned char header[8];    // 8 is the maximum size that can be checked

	/* open file and test for it being a png */
	FILE *fp = fopen(file_name, "rb");
	if (!fp)
		abort_("[read_png_file] File %s could not be opened for reading", file_name);
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8))
		abort_("[read_png_file] File %s is not recognized as a PNG file", file_name);


	/* initialize stuff */
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!png_ptr)
		abort_("[read_png_file] png_create_read_struct failed");

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
		abort_("[read_png_file] png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png_ptr)))
		abort_("[read_png_file] Error during init_io");

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);

	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_EXPAND, NULL);

	width = png_get_image_width(png_ptr, info_ptr);
	height = png_get_image_height(png_ptr, info_ptr);
	color_type = png_get_color_type(png_ptr, info_ptr);
	bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	rows = png_get_rows (png_ptr, info_ptr);

	fclose(fp);
}

int func_8ppbbw_formatter(struct png_to_bytes_opt_args_info *args_info, FILE *out)
{
	int x, y, z;
	int channels = png_get_channels(png_ptr, info_ptr);
	int cur_bit = 0;

	fprintf(out, "#define WIDTH %d\n", ALIGN(width, 8));
	fprintf(out, "#define HEIGHT %d\n", height);
	fprintf(out, "static unsigned char image[%d][%d] = {\n", ALIGN(width, 8)/8, height);

	for (y = 0; y < height; y++) {
		png_bytep row = rows[y];
		for (x = 0; x < width; x++) {
			png_byte* ptr = &(row[x*channels]);
			if (cur_bit == 0)
				fprintf(out, "{0b");
				
			if (ptr[0])
				fprintf(out, "1");
			else
				fprintf(out, "0");

			cur_bit++;
			if (cur_bit == 8) {
				fprintf(out, ",");
				cur_bit = 0;
			}
		}
		/* Pad with 0 */
		for (z = 0; z < (8 - cur_bit); z++)
			fprintf(out, "0");
		fprintf(out, "}\n");
	}

	fprintf(out, "}\n");

	return 0;
}

struct output_formatter {
	const char *name;
	int (*format_func)(struct png_to_bytes_opt_args_info *, FILE *);
};

static struct output_formatter formats[] = {
	{"8ppbbw", func_8ppbbw_formatter},
	{NULL, NULL}
};

static struct output_formatter *get_formatter(const char *name)
{
	struct output_formatter *formatter = &formats[0];

	while (formatter->name != NULL) {
		if (strcmp(name, formatter->name) == 0)
			return formatter;

		formatter++;
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct output_formatter *formatter = NULL;
	struct png_to_bytes_opt_args_info args_info;
	FILE *output;

	if(png_to_bytes_opt (argc, argv, &args_info))
		return EINVAL;

	read_png_file(args_info.input_arg);

	formatter = get_formatter(args_info.format_arg);
	if (!formatter) {
		fprintf(stderr, "Invalid output format name");
		return EINVAL;
	}
	
	output = fopen(args_info.output_arg, "w");
	if (!output) {
		fprintf(stderr, "Fail to open output file %s\n", args_info.output_arg);
		return errno;
	}

	return formatter->format_func(&args_info, output);
	
	fclose(output);

	return 0;
}
