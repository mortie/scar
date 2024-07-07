#ifndef SCAR_READER_H
#define SCAR_READER_H

#include "io.h"
#include "ioutil.h"
#include "meta.h"

/// The scar_reader is an opaque type which is used to read a SCAR archive.
struct scar_reader;

/// The scar_index_iterator is an opaque type which is used to iterate
/// through the index of a SCAR archive.
struct scar_index_iterator;

/// The scar_index_entry represents the current entry of an index iterator.
struct scar_index_entry {
	enum scar_meta_filetype ft;
	char *name;
	scar_offset offset;
};

/// Create a scar_reader.
struct scar_reader *scar_reader_create(
	struct scar_io_reader *r, struct scar_io_seeker *s);

/// Start iterating through the index.
struct scar_index_iterator *scar_reader_iterate(struct scar_reader *sr);

/// Find the next entry in the index.
int scar_index_iterator_next(
	struct scar_index_iterator *it,
	struct scar_index_entry *entry);

// Free a scar_index_iterator.
void scar_index_iterator_free(struct scar_index_iterator *it);

/// Free a scar_reader.
/// Does not free the 'scar_io_reader' or 'scar_io_seeker'
/// that was passed in to the scar_reader_create function;
/// those must be freed separately.
void scar_reader_free(struct scar_reader *sr);

#endif
