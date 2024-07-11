#include "compression.h"

#include <stdlib.h>

#include "../internal-util.h"

static const unsigned char MAGIC[] = {
	0x53, 0x43, 0x41, 0x52, 0x2d, 0x54, 0x41, 0x49, 0x4c, 0x0a,
};

static const unsigned char EOF_MARKER[] = {
	0x53, 0x43, 0x41, 0x52, 0x2d, 0x45, 0x4f, 0x46, 0x0a,
};

struct plain_compressor {
	struct scar_compressor c;
	struct scar_io_writer *w;
};

static scar_ssize plain_compressor_write(
	struct scar_io_writer *ptr, const void *buf, size_t len
) {
	struct plain_compressor *c = (struct plain_compressor *)ptr;
	return c->w->write(c->w, buf, len);
}

static int plain_compressor_flush(struct scar_compressor *ptr)
{
	(void)ptr;
	return 0;
}

static int plain_compressor_finish(struct scar_compressor *ptr)
{
	(void)ptr;
	return 0;
}

static struct scar_compressor *create_plain_compressor(
	struct scar_io_writer *w, int level
) {
	(void)level;

	struct plain_compressor *c = malloc(sizeof(*c));
	if (!c) {
		SCAR_ERETURN(NULL);
	}

	c->w = w;
	c->c.w.write = plain_compressor_write;
	c->c.flush = plain_compressor_flush;
	c->c.finish = plain_compressor_finish;
	return &c->c;
}

static void destroy_plain_compressor(struct scar_compressor *ptr)
{
	struct plain_compressor *c = (struct plain_compressor *)ptr;
	free(c);
}

struct gzip_decompressor {
	struct scar_decompressor d;
	struct scar_io_reader *r;
};

static scar_ssize plain_decompressor_read(
	struct scar_io_reader *ptr, void *buf, size_t len
) {
	struct gzip_decompressor *d = (struct gzip_decompressor *)ptr;
	return d->r->read(d->r, buf, len);
}

static struct scar_decompressor *create_plain_decompressor(
	struct scar_io_reader *r
) {
	struct gzip_decompressor *d = malloc(sizeof(*d));
	if (!d) {
		SCAR_ERETURN(NULL);
	}

	d->r = r;
	d->d.r.read = plain_decompressor_read;
	return &d->d;
}

static void destroy_plain_decompressor(struct scar_decompressor *ptr)
{
	struct plain_decompressor *d = (struct plain_decompressor *)ptr;
	free(d);
}

void scar_compression_init_plain(struct scar_compression *c)
{
	c->create_compressor = create_plain_compressor;
	c->destroy_compressor = destroy_plain_compressor;
	c->create_decompressor = create_plain_decompressor;
	c->destroy_decompressor = destroy_plain_decompressor;
	c->magic = MAGIC;
	c->magic_len = sizeof(MAGIC);
	c->eof_marker = EOF_MARKER;
	c->eof_marker_len = sizeof(EOF_MARKER);
}
