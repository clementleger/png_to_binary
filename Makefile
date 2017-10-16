LDFLAGS := $(shell pkg-config libpng --libs)
CFLAGS := $(shell pkg-config libpng --cflags)

png_to_bytes: png_to_bytes.o png_to_bytes_opt.o

png_to_bytes.o: png_to_bytes_opt.c

%.h: %.ggo
%.c: %.ggo
	gengetopt --file-name=$* --func-name=$* \
	--arg-struct-name=$*_args_info --string-parser --unamed-opts \
	--output-dir=$(@D) < $<
 

clean: 
