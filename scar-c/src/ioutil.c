#include "ioutil.h"

#include "util.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

scar_ssize scar_io_printf(struct scar_io_writer *w, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ssize_t ret = scar_io_vprintf(w, fmt, ap);
	va_end(ap);
	return ret;
}

scar_ssize scar_io_vprintf(struct scar_io_writer *w, const char *fmt, va_list ap)
{
	char buf[128];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (n < 0) {
		return -1;
	} else if ((size_t)n <= sizeof(buf) - 1) {
		return w->write(w, buf, n);
	}

	void *mbuf = malloc(n + 1);
	n = vsnprintf(mbuf, n + 1, fmt, ap);
	printf("writing %d mallocd bytes\n", n);
	ssize_t ret = w->write(w, mbuf, n);
	free(mbuf);
	return ret;
}

void scar_file_init(struct scar_file *r, FILE *f)
{
	r->r.read = scar_file_read;
	r->w.write = scar_file_write;
	r->s.seek = scar_file_seek;
	r->s.tell = scar_file_tell;
	r->f = f;
}

scar_ssize scar_file_read(struct scar_io_reader *r, void *buf, size_t len)
{
	struct scar_file *sf = SCAR_BASE(struct scar_file, r);
	size_t n = fread(buf, 1, len, sf->f);
	if (n == 0 && ferror(sf->f)) {
		return -1;
	}

	return (ssize_t)n;
}

scar_ssize scar_file_write(struct scar_io_writer *w, const void *buf, size_t len)
{
	struct scar_file *sf = SCAR_BASE(struct scar_file, w);
	size_t n = fwrite(buf, 1, len, sf->f);
	if (n == 0 && ferror(sf->f)) {
		return -1;
	}

	return (ssize_t)n;
}

static const int whences[] = {
	[SCAR_SEEK_START] = SEEK_SET,
	[SCAR_SEEK_CURRENT] = SEEK_CUR,
	[SCAR_SEEK_END] = SEEK_END,
};

int scar_file_seek(struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence)
{
	struct scar_file *sf = SCAR_BASE(struct scar_file, s);
	// TODO: Select fseek64 or fseeko based on platform
	return fseek(sf->f, (long)offset, whences[whence]);
}

scar_offset scar_file_tell(struct scar_io_seeker *s)
{
	struct scar_file *sf = SCAR_BASE(struct scar_file, s);
	// TODO: Select ftell64 or ftello based on platform
	return ftell(sf->f);
}

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

	memcpy(buf, mr->buf, n);
	mr->pos += n;
	return n;
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
		newpos = mr->pos + offset;
		break;
	case SCAR_SEEK_END:
		newpos = mr->len + offset;
		break;
	}

	if (newpos < 0 || (size_t)newpos > mr->len) {
		return -1;
	}

	mr->pos = newpos;
	return 0;
}

scar_offset scar_mem_reader_tell(struct scar_io_seeker *s)
{
	struct scar_mem_reader *mr = SCAR_BASE(struct scar_mem_reader, s);
	return mr->pos;
}

void scar_mem_writer_init(struct scar_mem_writer *mw)
{
	mw->w.write = scar_mem_writer_write;
	mw->buf = NULL;
	mw->len = 0;
	mw->cap = 0;
}

scar_ssize scar_mem_writer_write(struct scar_io_writer *w, const void *buf, size_t len)
{
	struct scar_mem_writer *mw = SCAR_BASE(struct scar_mem_writer, w);
	if (mw->cap < mw->len + len) {
		if (mw->cap == 0) {
			mw->cap = 8;
		}

		while (mw->cap < mw->len + len) {
			mw->cap *= 2;
		}

		void *newbuf = realloc(mw->buf, mw->cap);
		if (newbuf == NULL) {
			return -1;
		}

		mw->buf = newbuf;
	}

	memcpy(&mw->buf[mw->len], buf, len);
	mw->len += len;
	return len;
}
