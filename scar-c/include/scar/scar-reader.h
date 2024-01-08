#ifndef SCAR_READER_H
#define SCAR_READER_H

#include "io.h"

/// The scar_reader is an opaque type which is used to read a SCAR archive.
struct scar_reader;

/// Create a scar_reader.
struct scar_reader *scar_reader_create(struct scar_io_reader *r, struct scar_io_seeker *s);

#endif
