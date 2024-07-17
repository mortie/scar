#ifndef SCAR_READER_H
#define SCAR_READER_H

#include "io.h"
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
	const struct scar_meta *global;
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

/// Free a scar_index_iterator.
void scar_index_iterator_free(struct scar_index_iterator *it);

/// Read all the metadata for the entry at a given offset.
/// 'global' is expected to be initialized, and contain whatever
/// global attributes apply to the given entry
/// (usually this will be the 'global' field of a 'scar_index_iterator').
/// 'meta' is not expected to be initialized,
/// and its dynamically allocated fields will not be freed
/// before being overwritten.
int scar_reader_read_meta(
	struct scar_reader *sr, scar_offset offset,
	const struct scar_meta *global, struct scar_meta *meta);

/// Read all the content for the next pax entry.
/// 'scar_reader_read_meta' must have been called
/// just before 'scar_reader_read_content'.
int scar_reader_read_content(
	struct scar_reader *sr, struct scar_io_writer *w, uint64_t size);

/// Free a scar_reader.
/// Does not free the 'scar_io_reader' or 'scar_io_seeker'
/// that was passed in to the scar_reader_create function;
/// those must be freed separately.
void scar_reader_free(struct scar_reader *sr);

#endif
