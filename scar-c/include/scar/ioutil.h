#ifndef SCAR_IOUTIL_H
#define SCAR_IOUTIL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "io.h"
#include "types.h"

/// Print text to a stream, formatted using printf-style formatting.
/// Returns the number of bytes written, or -1 on error.
scar_ssize scar_io_printf(struct scar_io_writer *w, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/// Print text to a stream, formatted using printf-style formatting,
/// using a va_list.
/// Returns the number of bytes written, or -1 on error.
scar_ssize scar_io_vprintf(struct scar_io_writer *w, const char *fmt, va_list ap);

/// Write a string to a stream.
/// Calls the write method with 'strlen(str)' as the size.
scar_ssize scar_io_puts(struct scar_io_writer *w, const char *str);

/// Write everything from one reader to a writer.
scar_ssize scar_io_copy(struct scar_io_reader *r, struct scar_io_writer *w);

/// A wrapper around a FILE* which implements reader, writer and seeker.
struct scar_file_handle {
	struct scar_io_reader r;
	struct scar_io_writer w;
	struct scar_io_seeker s;
	FILE *f;
};

void scar_file_handle_init(struct scar_file_handle *sf, FILE *f);
scar_ssize scar_file_handle_read(struct scar_io_reader *r, void *buf, size_t len);
scar_ssize scar_file_handle_write(struct scar_io_writer *w, const void *buf, size_t len);
int scar_file_handle_seek(
	struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence);
scar_offset scar_file_handle_tell(struct scar_io_seeker *s);

/// An in-memory buffer which can be read from as a reader
/// and seeked as a seeker.
struct scar_mem_reader {
	struct scar_io_reader r;
	struct scar_io_seeker s;
	const void *buf;
	size_t len;
	size_t pos;
};

void scar_mem_reader_init(
	struct scar_mem_reader *mr, const void *buf, size_t len);
scar_ssize scar_mem_reader_read(
	struct scar_io_reader *r, void *buf, size_t len);
int scar_mem_reader_seek(
	struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence);
scar_offset scar_mem_reader_tell(struct scar_io_seeker *s);

/// An in-memory buffer which can be written to as a writer.
struct scar_mem_writer {
	struct scar_io_writer w;
	void *buf;
	size_t len;
	size_t cap;
};

void scar_mem_writer_init(struct scar_mem_writer *mw);
scar_ssize scar_mem_writer_write(
	struct scar_io_writer *w, const void *buf, size_t len);
int scar_mem_writer_put(struct scar_mem_writer *mw, unsigned char ch);
void *scar_mem_writer_get_buffer(struct scar_mem_writer *mw, size_t len);

/// A writer wrapper which counts the number of bytes written.
struct scar_counting_writer {
	struct scar_io_writer w;
	struct scar_io_writer *backing_w;
	scar_offset count;
};

void scar_counting_writer_init(
	struct scar_counting_writer *cw, struct scar_io_writer *w);
scar_ssize scar_counting_writer_write(
	struct scar_io_writer *w, const void *buf, size_t len);

/// A reader wrapper which counts the number of bytes read.
struct scar_counting_reader {
	struct scar_io_reader r;
	struct scar_io_reader *backing_r;
	scar_offset count;
};

void scar_counting_reader_init(
	struct scar_counting_reader *cr, struct scar_io_reader *r);
scar_ssize scar_counting_reader_read(
	struct scar_io_reader *r, void *buf, size_t len);

/// A reader wrapper which limits the number of bytes read.
struct scar_limited_reader {
	struct scar_io_reader r;
	struct scar_io_reader *backing_r;
	scar_offset limit;
};

void scar_limited_reader_init(
	struct scar_limited_reader *lr, struct scar_io_reader *r,
	scar_offset limit);
scar_ssize scar_limited_reader_read(
	struct scar_io_reader *r, void *buf, size_t len);

/// A wrapper around a reader which reads 512-byte blocks
struct scar_block_reader {
	struct scar_io_reader r;
	struct scar_io_reader *backing_r;
	int next;
	int error;

	int index;
	int bufcap;
	unsigned char block[512];
};

void scar_block_reader_init(
	struct scar_block_reader *br, struct scar_io_reader *r);
void scar_block_reader_consume(struct scar_block_reader *br);
int scar_block_reader_skip(struct scar_block_reader *br, size_t n);
scar_ssize scar_block_reader_read(
	struct scar_io_reader *r, void *buf, size_t n);
scar_ssize scar_block_reader_read_line(
	struct scar_block_reader *br, void *buf, size_t n);

#endif
