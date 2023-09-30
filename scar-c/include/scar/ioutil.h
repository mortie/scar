#ifndef SCAR_IOUTIL_H
#define SCAR_IOUTIL_H

#include "io.h"

#include <stdarg.h>
#include <stdio.h>

/// Print text to a stream, formatted using printf-style formatting.
/// Returns the number of bytes written, or -1 on error.
scar_ssize scar_io_printf(struct scar_io_writer *w, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/// Print text to a stream, formatted using printf-style formatting, using a va_list.
/// Returns the number of bytes written, or -1 on error.
scar_ssize scar_io_vprintf(struct scar_io_writer *w, const char *fmt, va_list ap);

/// A wrapper around a FILE* which implements reader, writer and seeker.
/// The scar_file doesn't own the underlying FILE*, so the FILE* must be closed separately.
struct scar_file {
	struct scar_io_reader r;
	struct scar_io_writer w;
	struct scar_io_seeker s;
	FILE *f;
};

void scar_file_init(struct scar_file *sf, FILE *f);
scar_ssize scar_file_read(struct scar_io_reader *r, void *buf, scar_size len);
scar_ssize scar_file_write(struct scar_io_writer *w, const void *buf, scar_size len);
int scar_file_seek(struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence);
scar_offset scar_file_tell(struct scar_io_seeker *s);

#endif
