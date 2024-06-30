#ifndef SCAR_READER_H
#define SCAR_READER_H

#include "io.h"

/// The scar_reader is an opaque type which is used to read a SCAR archive.
struct scar_reader;

/// Create a scar_reader.
struct scar_reader *scar_reader_create(
	struct scar_io_reader *r, struct scar_io_seeker *s);

/// Free a scar_reader.
/// Does not free the 'scar_io_reader' or 'scar_io_seeker'
/// that was passed in to the scar_reader_create function;
/// those must be freed separately.
void scar_reader_free(struct scar_reader *sr);

#endif
