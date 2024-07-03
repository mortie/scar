#ifndef SCAR_COMPRESSION_H
#define SCAR_COMPRESSION_H

#include <stdbool.h>

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

/// Initialize compression from human readable name.
/// Returns true if one was found, false otherwise.
bool scar_compression_init_from_name(
	struct scar_compression *comp, const char *name);

/// Initialize compression from magic bytes.
/// Will perform a suffix match on the buffer against
/// each compression's EOF marker.
/// Returns true if one was found, false otherwise.
bool scar_compression_init_from_tail(
	struct scar_compression *comp, void *buf, size_t len);

#endif
