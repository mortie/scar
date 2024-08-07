// For fseeko/ftello
#ifndef __WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include "ioutil.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "internal-util.h"

// The standard ftell/fseek is 32 bit on common platforms.
// These SCAR_FSEEK/SCAR_FTELL macros will use 64-bit compatible variants.
#ifdef _WIN32
#define SCAR_FSEEK(f, o, w) _fseeki64(f, (__int64)(o), w)
#define SCAR_FTELL(f) ((long long)_ftelli64(f))
#else
#define SCAR_FSEEK(f, o, w) fseeko(f, (off_t)(o), w)
#define SCAR_FTELL(f) ((long long)ftello(f))
#endif

//
// Utility functions
//

scar_ssize scar_io_printf(struct scar_io_writer *w, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	scar_ssize ret = scar_io_vprintf(w, fmt, ap);
	va_end(ap);
	return ret;
}

scar_ssize scar_io_vprintf(
	struct scar_io_writer *w, const char *fmt, va_list ap
) {
	char buf[128];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (n < 0) {
		SCAR_ERETURN(-1);
	} else if ((size_t)n <= sizeof(buf) - 1) {
		return w->write(w, buf, (size_t)n);
	}

	void *mbuf = malloc((size_t)n + 1);
	n = vsnprintf(mbuf, (size_t)n + 1, fmt, ap);
	scar_ssize ret = w->write(w, mbuf, (size_t)n);
	free(mbuf);
	return ret;
}

scar_ssize scar_io_puts(struct scar_io_writer *w, const char *str)
{
	return w->write(w, str, strlen(str));
}

scar_ssize scar_io_copy(struct scar_io_reader *r, struct scar_io_writer *w)
{
	scar_ssize count = 0;
	char buf[512];
	while (1) {
		scar_ssize nr = r->read(r, buf, sizeof(buf));
		if (nr < 0) {
			return nr;
		} else if (nr == 0) {
			return count;
		}

		scar_ssize nw = w->write(w, buf, nr);
		if (nw < 0) {
			return nw;
		} else if (nw < nr) {
			return -1;
		}

		count += nw;
	}
}

//
// scar_file_handle
//

void scar_file_handle_init(struct scar_file_handle *r, FILE *f)
{
	r->r.read = scar_file_handle_read;
	r->w.write = scar_file_handle_write;
	r->s.seek = scar_file_handle_seek;
	r->s.tell = scar_file_handle_tell;
	r->f = f;
}

scar_ssize scar_file_handle_read(
	struct scar_io_reader *r, void *buf, size_t len
) {
	struct scar_file_handle *sf = SCAR_BASE(struct scar_file_handle, r);
	size_t n = fread(buf, 1, len, sf->f);
	if (n == 0 && ferror(sf->f)) {
		SCAR_ERETURN(-1);
	}

	return (scar_ssize)n;
}

scar_ssize scar_file_handle_write(struct scar_io_writer *w, const void *buf, size_t len)
{
	struct scar_file_handle *sf = SCAR_BASE(struct scar_file_handle, w);
	size_t n = fwrite(buf, 1, len, sf->f);
	if (n == 0 && ferror(sf->f)) {
		SCAR_ERETURN(-1);
	}

	return (scar_ssize)n;
}

static const int whences[] = {
	[SCAR_SEEK_START] = SEEK_SET,
	[SCAR_SEEK_CURRENT] = SEEK_CUR,
	[SCAR_SEEK_END] = SEEK_END,
};

int scar_file_handle_seek(
	struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence
) {
	struct scar_file_handle *sf = SCAR_BASE(struct scar_file_handle, s);
	return SCAR_FSEEK(sf->f, (long)offset, whences[whence]);
}

scar_offset scar_file_handle_tell(struct scar_io_seeker *s)
{
	struct scar_file_handle *sf = SCAR_BASE(struct scar_file_handle, s);
	return SCAR_FTELL(sf->f);
}

//
// scar_mem_reader
//

void scar_mem_reader_init(
	struct scar_mem_reader *mr, const void *buf, size_t len
) {
	mr->r.read = scar_mem_reader_read;
	mr->s.seek = scar_mem_reader_seek;
	mr->s.tell = scar_mem_reader_tell;
	mr->buf = buf;
	mr->len = len;
	mr->pos = 0;
}

scar_ssize scar_mem_reader_read(
	struct scar_io_reader *r, void *buf, size_t len
) {
	struct scar_mem_reader *mr = SCAR_BASE(struct scar_mem_reader, r);
	if (mr->pos >= mr->len) {
		return 0;
	}

	size_t left = mr->len - mr->pos;
	size_t n;
	if (left > len) {
		n = len;
	} else {
		n = left;
	}

	memcpy(buf, (unsigned char *)mr->buf + mr->pos, n);
	mr->pos += n;
	return (scar_ssize)n;
}

int scar_mem_reader_seek(
	struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence
) {
	struct scar_mem_reader *mr = SCAR_BASE(struct scar_mem_reader, s);
	scar_ssize newpos = 0;
	switch (whence) {
	case SCAR_SEEK_START:
		newpos = offset;
		break;
	case SCAR_SEEK_CURRENT:
		newpos = (scar_ssize)mr->pos + offset;
		break;
	case SCAR_SEEK_END:
		newpos = (scar_ssize)mr->len + offset;
		break;
	}

	if (newpos < 0 || (size_t)newpos > mr->len) {
		SCAR_ERETURN(-1);
	}

	mr->pos = (size_t)newpos;
	return 0;
}

scar_offset scar_mem_reader_tell(struct scar_io_seeker *s)
{
	struct scar_mem_reader *mr = SCAR_BASE(struct scar_mem_reader, s);
	return (scar_offset)mr->pos;
}

//
// scar_mem_writer
//

static int mem_writer_grow(struct scar_mem_writer *mw, size_t len)
{
	if (mw->cap < mw->len + len) {
		if (mw->cap == 0) {
			mw->cap = 8;
		}

		while (mw->cap < mw->len + len) {
			mw->cap *= 2;
		}

		void *newbuf = realloc(mw->buf, mw->cap);
		if (newbuf == NULL) {
			SCAR_ERETURN(-1);
		}

		mw->buf = newbuf;
	}

	return 0;
}

void scar_mem_writer_init(struct scar_mem_writer *mw)
{
	mw->w.write = scar_mem_writer_write;
	mw->buf = NULL;
	mw->len = 0;
	mw->cap = 0;
}

scar_ssize scar_mem_writer_write(
	struct scar_io_writer *w, const void *buf, size_t len
) {
	struct scar_mem_writer *mw = SCAR_BASE(struct scar_mem_writer, w);
	if (mem_writer_grow(mw, len) < 0) {
		SCAR_ERETURN(-1);
	}

	memcpy(&((unsigned char *)mw->buf)[mw->len], buf, len);
	mw->len += len;
	return (scar_ssize)len;
}

int scar_mem_writer_put(struct scar_mem_writer *mw, unsigned char ch)
{
	if (mem_writer_grow(mw, 1) < 0) {
		SCAR_ERETURN(-1);
	}

	((unsigned char *)mw->buf)[mw->len] = ch;
	mw->len += 1;
	return 0;
}

void *scar_mem_writer_get_buffer(struct scar_mem_writer *mw, size_t len)
{
	if (mem_writer_grow(mw, len) < 0) {
		SCAR_ERETURN(NULL);
	}

	void *buf = &((unsigned char *)mw->buf)[mw->len];
	mw->len += len;
	return buf;
}

//
// scar_counting_writer
//

void scar_counting_writer_init(
	struct scar_counting_writer *cw, struct scar_io_writer *w
) {
	cw->w.write = scar_counting_writer_write;
	cw->backing_w = w;
	cw->count = 0;
}

scar_ssize scar_counting_writer_write(
	struct scar_io_writer *w, const void *buf, size_t len
) {
	struct scar_counting_writer *cw =
		SCAR_BASE(struct scar_counting_writer, w);
	scar_ssize count = cw->backing_w->write(cw->backing_w, buf, len);
	if (count > 0) {
		cw->count += count;
	}

	return count;
}

//
// scar_counting_reader
//

void scar_counting_reader_init(
	struct scar_counting_reader *cr, struct scar_io_reader *r
) {
	cr->r.read = scar_counting_reader_read;
	cr->backing_r = r;
	cr->count = 0;
}

scar_ssize scar_counting_reader_read(
	struct scar_io_reader *r, void *buf, size_t len
) {
	struct scar_counting_reader *cr =
		SCAR_BASE(struct scar_counting_reader, r);
	scar_ssize count = cr->backing_r->read(cr->backing_r, buf, len);
	if (count > 0) {
		cr->count += count;
	}

	return count;
}

//
// scar_limited_reader
//

void scar_limited_reader_init(
	struct scar_limited_reader *cr, struct scar_io_reader *r, scar_offset limit
) {
	cr->r.read = scar_limited_reader_read;
	cr->backing_r = r;
	cr->limit = limit;
}

scar_ssize scar_limited_reader_read(
	struct scar_io_reader *r, void *buf, size_t len
) {
	struct scar_limited_reader *lr = SCAR_BASE(struct scar_limited_reader, r);
	if (lr->limit <= 0) {
		return 0;
	}

	if ((scar_offset)len > lr->limit) {
		len = lr->limit;
	}

	scar_ssize count = lr->backing_r->read(lr->backing_r, buf, len);
	if (count > 0) {
		lr->limit -= count;
	}

	return count;
}

//
// scar_block_reader
//

void scar_block_reader_init(
	struct scar_block_reader *br, struct scar_io_reader *r
) {
	br->r.read = scar_block_reader_read;
	br->backing_r = r;
	br->index = 0;
	br->bufcap = 0;

	scar_ssize n = r->read(r, br->block, sizeof(br->block));
	if (n < 1) {
		br->next = EOF;
		br->error = (int)n;
	} else {
		br->bufcap = (int)n;
		br->next = br->block[br->index++];
		br->error = 0;
	}
}

void scar_block_reader_consume(struct scar_block_reader *br)
{
	if (br->next == EOF) {
		return;
	}

	if (br->index >= br->bufcap) {
		scar_ssize n = br->backing_r->read(
			br->backing_r, br->block, sizeof(br->block));
		if (n < 1) {
			br->next = EOF;
			br->error = (int)n;
		} else {
			br->index = 0;
			br->next = br->block[br->index++];
			br->bufcap = (int)n;
		}

		return;
	}

	br->next = br->block[br->index++];
}

int scar_block_reader_skip(struct scar_block_reader *br, size_t n)
{
	// This could be faster...
	while (n > 0) {
		if (br->next == EOF) {
			SCAR_ERETURN(-1);
		}

		scar_block_reader_consume(br);
		n -= 1;
	}

	return 0;
}

scar_ssize scar_block_reader_read(struct scar_io_reader *r, void *buf, size_t n)
{
	struct scar_block_reader *br = SCAR_BASE(struct scar_block_reader, r);

	scar_ssize ret = 0;

	// This could also be faster...
	unsigned char *cbuf = (unsigned char *)buf;
	while (ret < (scar_ssize)n) {
		if (br->next == EOF) {
			return ret;
		}

		*cbuf = (unsigned char)br->next;
		cbuf += 1;

		scar_block_reader_consume(br);
		ret += 1;
	}

	return ret;
}

scar_ssize scar_block_reader_read_line(
	struct scar_block_reader *br, void *buf, size_t n
) {
	scar_ssize ret = 0;

	unsigned char *cbuf = (unsigned char *)buf;
	while (ret - 1 < (scar_ssize)n) {
		if (br->next == EOF) {
			*cbuf = '\0';
			return ret;
		} else if (br->next == '\n' || br->next == '\r') {
			*cbuf = '\0';
			break;
		}

		*cbuf = (unsigned char)br->next;
		cbuf += 1;
		ret += 1;

		scar_block_reader_consume(br);
	}

	while (br->next == '\n' || br->next == '\r') {
		scar_block_reader_consume(br);
	}

	return ret;
}
