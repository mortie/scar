#ifndef SCAR_COMPRESSION_H
#define SCAR_COMPRESSION_H

#include "io.h"

struct scar_compressor {
	struct scar_io_writer w;
	int (*flush)(struct scar_compressor *c);
	int (*finish)(struct scar_compressor *c);
};

struct scar_decompressor {
	struct scar_io_reader r;
};

struct scar_compression {
	struct scar_compressor *(*create_compressor)(struct scar_io_writer *w, int level);
	void (*destroy_compressor)(struct scar_compressor *c);

	struct scar_decompressor *(*create_decompressor)(struct scar_io_reader *r);
	void (*destroy_decompressor)(struct scar_decompressor *d);

	const unsigned char *magic;
	size_t magic_len;
	const unsigned char *eof_marker;
	size_t eof_marker_len;
};

void scar_compression_init_gzip(struct scar_compression *comp);

#endif
