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
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
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

	/* open file and test for it being a png_ptr */
	FILE *fp = fopen(file_name, "rb");
	if (!fp)
		abort_("[read_png_file] File %s could not be opened for reading", file_name);
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8))
		abort_("[read_png_file] File %s is not recognized as a png_ptr file", file_name);


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
	
	png_read_info(png_ptr, info_ptr);

	width = png_get_image_width(png_ptr, info_ptr);
	height = png_get_image_height(png_ptr, info_ptr);
	color_type = png_get_color_type(png_ptr, info_ptr);
	bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	rows = png_get_rows (png_ptr, info_ptr);

	if (bit_depth == 16)
		png_set_strip_16(png_ptr);

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);

	// PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png_ptr);

	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);

	// These color_type don't have an alpha channel then fill it with 0xff.
	if (color_type == PNG_COLOR_TYPE_RGB ||
		color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png_ptr, 0x00, PNG_FILLER_AFTER);

	if (color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);

	png_read_update_info(png_ptr, info_ptr);

	rows = (png_bytepp) malloc(sizeof(png_bytep) * height);
	for(int y = 0; y < height; y++) {
		rows[y] = (png_bytep) malloc(png_get_rowbytes(png_ptr, info_ptr));
	}

	png_read_image(png_ptr, rows);

	fclose(fp);
}

static void strtoupper(char *str)
{
	while(*str) {
		*str = toupper(*str);
		str++;
	}
}

int func_8ppbbw_formatter(struct png_to_bytes_opt_args_info *args_info, FILE *out)
{
	int x, y, z;
	int channels = png_get_channels(png_ptr, info_ptr);
	int cur_bit = 0;
	bool invert = args_info->invert_given;
	char *name = strdup(args_info->name_arg);
	strtoupper(name);

	fprintf(out, "#define %s_WIDTH %d\n", name, ALIGN(width, 8));
	fprintf(out, "#define %s_HEIGHT %d\n", name, height);
	fprintf(out, "static PROGMEM unsigned char %s_data[%d][%d] = {\n", args_info->name_arg, height, ALIGN(width, 8)/8);

	for (y = 0; y < height; y++) {
		png_bytep row = rows[y];
		
		fprintf(out, "{");
		for (x = 0; x < width; x++) {
			png_byte* ptr = &(row[x*channels]);
			if (cur_bit == 0)
				fprintf(out, "0b");

			if (ptr[0] < 250 && ptr[1] < 250 && ptr[2] < 250)
				fprintf(out, invert ? "0" : "1");
			else
				fprintf(out, invert ? "1" : "0");

			cur_bit++;
			if (cur_bit == 8) {
				fprintf(out, ", ");
				cur_bit = 0;
			}
		}
		/* Pad with zeros */
		if (cur_bit != 0) {
			/* Pad with 0 */
			for (z = 0; z < (8 - cur_bit); z++)
				fprintf(out, invert ? "1" : "0");
			cur_bit = 0;
		}
		fprintf(out, "},\n");
	}

	fprintf(out, "};\n");

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
