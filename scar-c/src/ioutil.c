#include "ioutil.h"

#include "util.h"
#include "internal-util.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

/*
 * Utility functions
 */

scar_ssize scar_io_printf(struct scar_io_writer *w, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	scar_ssize ret = scar_io_vprintf(w, fmt, ap);
	va_end(ap);
	return ret;
}

scar_ssize scar_io_vprintf(struct scar_io_writer *w, const char *fmt, va_list ap)
{
	char buf[128];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (n < 0) {
		SCAR_ERETURN(-1);
	} else if ((size_t)n <= sizeof(buf) - 1) {
		return w->write(w, buf, (size_t)n);
	}

	void *mbuf = malloc((size_t)n + 1);
	n = vsnprintf(mbuf, (size_t)n + 1, fmt, ap);
	printf("writing %d mallocd bytes\n", n);
	scar_ssize ret = w->write(w, mbuf, (size_t)n);
	free(mbuf);
	return ret;
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

scar_ssize scar_file_handle_read(struct scar_io_reader *r, void *buf, size_t len)
{
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

int scar_file_handle_seek(struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence)
{
	struct scar_file_handle *sf = SCAR_BASE(struct scar_file_handle, s);
	// TODO: Select fseek64 or fseeko based on platform
	return fseek(sf->f, (long)offset, whences[whence]);
}

scar_offset scar_file_handle_tell(struct scar_io_seeker *s)
{
	struct scar_file_handle *sf = SCAR_BASE(struct scar_file_handle, s);
	// TODO: Select ftell64 or ftello based on platform
	return ftell(sf->f);
}

//
// scar_mem_reader
//

void scar_mem_reader_init(struct scar_mem_reader *mr, const void *buf, size_t len)
{
	mr->r.read = scar_mem_reader_read;
	mr->s.seek = scar_mem_reader_seek;
	mr->s.tell = scar_mem_reader_tell;
	mr->buf = buf;
	mr->len = len;
	mr->pos = 0;
}

scar_ssize scar_mem_reader_read(struct scar_io_reader *r, void *buf, size_t len)
{
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

int scar_mem_reader_seek(struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence)
{
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

void *scar_mem_writer_get_buffer(struct scar_mem_writer *mw, size_t len)
{
	if (mem_writer_grow(mw, len) < 0) {
		return NULL;
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
	cw->written = 0;
}

scar_ssize scar_counting_writer_write(
	struct scar_io_writer *w, const void *buf, size_t len
) {
	struct scar_counting_writer *cw = SCAR_BASE(struct scar_counting_writer, w);
	scar_ssize written = cw->backing_w->write(cw->backing_w, buf, len);
	if (written > 0) {
		cw->written += written;
	}

	return written;
}
