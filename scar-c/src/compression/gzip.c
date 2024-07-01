#include "compression.h"

#include "../internal-util.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <assert.h>

static const unsigned char MAGIC[] = {0x1f, 0x8b};
static const unsigned char EOF_MARKER[] = {
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x0b, 0x76, 0x76,
	0x0c, 0xd2, 0x75, 0xf5, 0x77, 0xe3, 0x02, 0x00, 0xf8, 0xf3, 0x55, 0x01, 0x09,
	0x00, 0x00, 0x00,
};

struct gzip_compressor {
	struct scar_compressor c;
	struct scar_io_writer *w;
	z_stream stream;
	gz_header header;
};

static scar_ssize write_deflate(
	struct gzip_compressor *c, const void *buf, size_t len, int flush
) {
	unsigned char chunk[4 * 1024];

	assert((size_t)(uInt)len == len);
	c->stream.avail_in = (uInt)len;
	c->stream.next_in = (Bytef *)buf;

	do {
		c->stream.next_out = chunk;
		c->stream.avail_out = sizeof(chunk);

		if (deflate(&c->stream, flush) == Z_STREAM_ERROR) {
			SCAR_ERETURN(-1);
		}

		size_t outlen = sizeof(chunk) - c->stream.avail_out;
		if (outlen > 0) {
			if (c->w->write(c->w, chunk, outlen) < 0) {
				SCAR_ERETURN(-1);
			}
		}
	} while (c->stream.avail_out == 0);
	assert(c->stream.avail_in == 0);

	return (scar_ssize)len;
}

static scar_ssize gzip_compressor_write(
	struct scar_io_writer *ptr, const void *buf, size_t len
) {
	return write_deflate((struct gzip_compressor *)ptr, buf, len, Z_NO_FLUSH);
}

static int gzip_compressor_flush(struct scar_compressor *ptr)
{
	return (int)write_deflate((struct gzip_compressor *)ptr, NULL, 0, Z_FULL_FLUSH);
}

static int gzip_compressor_finish(struct scar_compressor *ptr)
{
	return (int)write_deflate((struct gzip_compressor *)ptr, NULL, 0, Z_FINISH);
}

static struct scar_compressor *create_gzip_compressor(
	struct scar_io_writer *w, int level
) {
	struct gzip_compressor *c = malloc(sizeof(*c));
	if (!c) {
		SCAR_ERETURN(NULL);
	}

	c->stream.zalloc = Z_NULL;
	c->stream.zfree = Z_NULL;
	c->stream.opaque = Z_NULL;
	if (deflateInit2(
		&c->stream, level, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK
	) {
		free(c);
		SCAR_ERETURN(NULL);
	}

	memset(&c->header, 0, sizeof(c->header));
	c->header.os = 0xff;
	if (deflateSetHeader(&c->stream, &c->header) != Z_OK) {
		free(c);
		SCAR_ERETURN(NULL);
	}

	c->w = w;
	c->c.w.write = gzip_compressor_write;
	c->c.flush = gzip_compressor_flush;
	c->c.finish = gzip_compressor_finish;
	return &c->c;
}

static void destroy_gzip_compressor(struct scar_compressor *ptr)
{
	struct gzip_compressor *c = (struct gzip_compressor *)ptr;
	deflateEnd(&c->stream);
	free(c);
}

struct gzip_decompressor {
	struct scar_decompressor d;
	struct scar_io_reader *r;
	z_stream stream;
	unsigned char chunk[4 * 1024];
};

static scar_ssize gzip_decompressor_read(
	struct scar_io_reader *ptr, void *buf, size_t len
) {
	struct gzip_decompressor *d = (struct gzip_decompressor *)ptr;

	assert((size_t)(uInt)len == len);
	d->stream.avail_out = (uInt)len;
	d->stream.next_out = (Bytef *)buf;

	do {
		if (d->stream.avail_in == 0) {
			d->stream.next_in = d->chunk;
			scar_ssize n = d->r->read(d->r, d->chunk, sizeof(d->chunk));
			if (n < 0) {
				SCAR_ERETURN(-1);
			} else if (n == 0) {
				return (scar_ssize)(len - d->stream.avail_out);
			}

			d->stream.avail_in = (uInt)n;
		}

		int ret = inflate(&d->stream, Z_NO_FLUSH);
		switch (ret) {
		case Z_STREAM_ERROR:
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			SCAR_ERETURN(-1);
		case Z_STREAM_END:
			return (scar_ssize)(len - d->stream.avail_out);
		}
	} while (d->stream.avail_out > 0);

	return (scar_ssize)len;
}

static struct scar_decompressor *create_gzip_decompressor(struct scar_io_reader *r)
{
	struct gzip_decompressor *d = malloc(sizeof(*d));
	if (!d) {
		SCAR_ERETURN(NULL);
	}

	d->stream.next_in = NULL;
	d->stream.avail_in = 0;
	d->stream.zalloc = Z_NULL;
	d->stream.zfree = Z_NULL;
	d->stream.opaque = Z_NULL;
	if (inflateInit2(&d->stream, 15 | 16) != Z_OK) {
		free(d);
		SCAR_ERETURN(NULL);
	}

	d->r = r;
	d->d.r.read = gzip_decompressor_read;
	return &d->d;
}

static void destroy_gzip_decompressor(struct scar_decompressor *ptr)
{
	struct gzip_decompressor *d = (struct gzip_decompressor *)ptr;
	inflateEnd(&d->stream);
	free(d);
}

void scar_compression_init_gzip(struct scar_compression *c)
{
	c->create_compressor = create_gzip_compressor;
	c->destroy_compressor = destroy_gzip_compressor;
	c->create_decompressor = create_gzip_decompressor;
	c->destroy_decompressor = destroy_gzip_decompressor;
	c->magic = MAGIC;
	c->magic_len = sizeof(MAGIC);
	c->eof_marker = EOF_MARKER;
	c->eof_marker_len = sizeof(EOF_MARKER);
}
