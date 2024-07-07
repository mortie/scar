#ifndef SCAR_WRITER_H
#define SCAR_WRITER_H

#include "compression.h"
#include "io.h"
#include "meta.h"

/// The scar_writer is an opaque type which is used to create a SCAR archive.
struct scar_writer;

/// Create a scar_writer.
struct scar_writer *scar_writer_create(
	struct scar_io_writer *w, struct scar_compression *comp, int clevel);

/// Write an entry to the SCAR archive.
int scar_writer_write_entry(
	struct scar_writer *sw, struct scar_meta *meta, struct scar_io_reader *r);

/// Flush compressors and write out the footer of the SCAR archive.
/// Does not free memory allocated by the writer;
/// use 'scar_writer_free' for that.
int scar_writer_finish(struct scar_writer *sw);

/// Free a scar_writer.
/// Does not free the 'scar_compression' or 'scar_io_writer' that was passed
/// in to the scar_writer_create function; those must be freed separately.
/// Does not write out the SCAR footer, use 'scar_writer_finish' for that.
void scar_writer_free(struct scar_writer *sw);

#endif
