#ifndef SCAR_PAX_H
#define SCAR_PAX_H

#include <stdint.h>

struct scar_io_reader;
struct scar_io_writer;
struct scar_meta;

/// Read all the metadata for the next pax entry.
/// 'global' is expected to be initialized, and will be overwritten
/// by the data in any 'g' metadata entry if it's encountered.
/// 'meta' is expected to be uninitialized,
/// its fields will be unconditionally overwritten without being freed.
/// The reader 'r' is expected to be positioned right at the start
/// of an archive entry.
/// Returns 1 on success, 0 if the end-of-archive indicator was reached,
/// -1 on error.
int scar_pax_read_meta(
	struct scar_meta *global, struct scar_meta *meta,
	struct scar_io_reader *r);

/// Read the contents of an archive entry.
/// This will basically copy up to 'size' bytes from 'r' to 'w',
/// but will round up the amount of data read to 512-byte blocks.
int scar_pax_read_content(
	struct scar_io_reader *r, struct scar_io_writer *w, uint64_t size);

/// Write out a header for the given metadata.
/// Will create exactly one 512-byte USTAR header block if the metadata
/// is fully representable using the USTAR format,
/// or one 'x'-type PAX extended header followed by a 512-byte
/// USTAR header block if the metadata isn't fully representable
/// using the USTAR format.
/// Returns 0 on success, -1 on error.
int scar_pax_write_meta(struct scar_meta *meta, struct scar_io_writer *w);

/// Write the contents of an archive entry.
/// This will basically copy up to 'size' bytes from 'r' to 'w',
/// but will round up the amount of data written to fill 512-byte blocks.
int scar_pax_write_content(
	struct scar_io_reader *r, struct scar_io_writer *w, uint64_t size);

/// Write a header + content.
/// Utility function to combine scar_pax_write_meta and scar_pax_write_content.
int scar_pax_write_entry(
	struct scar_meta *meta, struct scar_io_reader *r,
	struct scar_io_writer *w);

/// Write the end-of-archive indicator.
/// That basically means writing 1024 0-bytes to 'w'.
int scar_pax_write_end(struct scar_io_writer *w);

#endif
