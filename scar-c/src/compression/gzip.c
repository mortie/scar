#include "compression.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static const unsigned char MAGIC[] = {0x1f, 0x8b};
static const unsigned char EOF_MARKER[] = {
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x0b, 0x76, 0x76, 0x0c, 0xd2, 0x75,
	0xf5, 0x77, 0xe3, 0x02, 0x00, 0xf8, 0xf3, 0x55, 0x01, 0x09, 0x00, 0x00, 0x00,
};

struct gzip_compressor {
	struct scar_compressor c;
	struct scar_io_writer *w;
	z_stream stream;
};

static scar_ssize gzip_compressor_write(struct scar_io_writer *ptr, const void *buf, size_t len)
{
	struct gzip_compressor *c = (struct gzip_compressor *)ptr;
	unsigned char chunk[4 * 1024];

	c->stream.next_in = (unsigned char *)buf;
	c->stream.avail_in = (unsigned int)len;

	size_t written = 0;
	do {
		c->stream.next_out = chunk;
		c->stream.avail_out = sizeof(chunk);

		int ret = deflate(&c->stream, Z_NO_FLUSH);
		if (ret < 0) {
			return -1;
		}

		unsigned int n = (unsigned int)sizeof(chunk) - c->stream.avail_out;
		scar_ssize write_ret = c->w->write(c->w, chunk, n);
		if (write_ret < 0) {
			return -1;
		} else if (write_ret < n) {
			return (scar_ssize)written;
		} else {
			written += (unsigned int)len - c->stream.avail_in;
		}
	} while (c->stream.avail_out == 0);

	return (scar_ssize)written;
}

static int gzip_compressor_do_flush(struct scar_compressor *ptr, int flush)
{
	struct gzip_compressor *c = (struct gzip_compressor *)ptr;
	unsigned char chunk[512];

	c->stream.next_in = NULL;
	c->stream.avail_in = 0;

	do {
		c->stream.next_out = chunk;
		c->stream.avail_out = sizeof(chunk);

		int ret = deflate(&c->stream, flush);
		if (ret < 0 && ret != Z_BUF_ERROR) {
			return -1;
		}

		unsigned int n = (unsigned int)sizeof(chunk) - c->stream.avail_out;
		scar_ssize write_ret = c->w->write(c->w, chunk, n);
		if (write_ret < n) {
			return -1;
		}
	} while (c->stream.avail_out == 0);

	return 0;
}

static int gzip_compressor_flush(struct scar_compressor *ptr)
{
	return gzip_compressor_do_flush(ptr, Z_FULL_FLUSH);
}

static int gzip_compressor_finish(struct scar_compressor *ptr)
{
	return gzip_compressor_do_flush(ptr, Z_FINISH);
}

static struct scar_compressor *create_gzip_compressor(struct scar_io_writer *w, int level)
{
	struct gzip_compressor *c = malloc(sizeof(*c));
	c->c.w.write = gzip_compressor_write;
	c->c.flush = gzip_compressor_flush;
	c->c.finish = gzip_compressor_finish;
	c->w = w;
	deflateInit2(&c->stream, level, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);

	gz_header header;
	memset(&header, 0, sizeof(header));
	header.os = 0xff;
	deflateSetHeader(&c->stream, &header);

	return &c->c;
}

static void destroy_gzip_compressor(struct scar_compressor *ptr)
{
	struct gzip_compressor *c = (struct gzip_compressor *)ptr;
	deflateEnd(&c->stream);
	free(c);
}

void scar_compression_init_gzip(struct scar_compression *c)
{
	c->create_compressor = create_gzip_compressor;
	c->destroy_compressor = destroy_gzip_compressor;
	c->magic = MAGIC;
	c->magic_len = sizeof(MAGIC);
	c->eof_marker = EOF_MARKER;
	c->eof_marker_len = sizeof(EOF_MARKER);
}
