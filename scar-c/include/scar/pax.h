#ifndef SCAR_PAX_H
#define SCAR_PAX_H

#include "io.h"

#include <stdint.h>

/// The scar_pax_filetype enum represents the possible archive entry types in a pax archive.
enum scar_pax_filetype {
	SCAR_FT_UNKNOWN,
	SCAR_FT_FILE,
	SCAR_FT_HARDLINK,
	SCAR_FT_SYMLINK,
	SCAR_FT_CHARDEV,
	SCAR_FT_BLOCKDEV,
	SCAR_FT_DIRECTORY,
	SCAR_FT_FIFO,
};

/// Convert a char to its associated scar_pax_filetype.
/// Characters with unknown meaning are converted to SCAR_FT_UNKNOWN.
enum scar_pax_filetype scar_pax_filetype_from_char(char ch);

/// Convert a scar_pax_filetype to its associated char.
/// SCAR_FT_UNKNOWN is converted to '?'.
char scar_pax_filetype_to_char(enum scar_pax_filetype ft);

/// Struct representing a pax entry's metadata.
/// All fields are optional.
/// A missing filetype field is represented by FT_UNKNOWN,
/// a missing unsigned int field is represented by ~0 (all bits set to 1),
/// a missing double field is represented by NaN,
/// a missing string field is represented by a null pointer.
struct scar_pax_meta {
	enum scar_pax_filetype type;
	uint32_t mode;
	uint32_t devmajor;
	uint32_t devminor;

	double atime;
	char *charset;
	char *comment;

	uint64_t gid;
	char *gname;
	char *hdrcharset;
	char *linkpath;
	double mtime;
	char *path;
	uint64_t size;
	uint64_t uid;
	char *uname;
};

/// Initialize all fields to their 'missing' value.
void scar_pax_meta_init_empty(struct scar_pax_meta *meta);

/// Initialize a pax_meta struct which represents a regular file.
void scar_pax_meta_init_file(struct scar_pax_meta *meta, char *path);

/// Initialize a pax_meta struct which represents a hardlink.
void scar_pax_meta_init_hardlink(struct scar_pax_meta *meta, char *path, char *linkpath);

/// Initialize a pax_meta struct which represents a symlink.
void scar_pax_meta_init_symlink(struct scar_pax_meta *meta, char *path, char *linkpath);

/// Initialize a pax_meta struct which represents a directory.
void scar_pax_meta_init_directory(struct scar_pax_meta *meta, char *path);

/// Initialize a pax_meta struct which represents a chardev.
void scar_pax_meta_init_chardev(struct scar_pax_meta *meta, char *path, uint32_t maj, uint32_t min);

/// Initialize a pax_meta struct which represents a blockdev.
void scar_pax_meta_init_blockdev(struct scar_pax_meta *meta, char *path, uint32_t maj, uint32_t min);

/// Initialize a pax_meta struct which represents a fifo.
void scar_pax_meta_init_fifo(struct scar_pax_meta *meta, char *path);

/// Copy a scar_pax_meta to 'dest' from 'src'.
void scar_pax_meta_copy(struct scar_pax_meta *dest, struct scar_pax_meta *src);

/// Free up every allocated string in a pax_meta struct.
/// The struct can be re-used after another call to scar_pax_meta_init.
void scar_pax_meta_destroy(struct scar_pax_meta *meta);

/// Pretty-print a metadata struct, for debugging purposes.
void scar_pax_meta_print(struct scar_pax_meta *meta, struct scar_io_writer *w);

/// Read all the metadata for the next pax entry.
/// 'global' is expected to be initialized, and will be overwritten
/// by the data in any 'g' metadata entry if it's encountered.
/// 'meta' is expected to be uninitialized, its fields will be unconditionally overwritten
/// without being freed.
/// The reader 'r' is expected to be positioned right at the start of an archive entry.
/// Returns 0 on success, -1 on error.
int scar_pax_meta_read(
		struct scar_pax_meta *global, struct scar_pax_meta *meta, struct scar_io_reader *r);

/// Write out a header for the given metadata.
/// Will create exactly one 512-byte USTAR header block if the metadata is fully representable
/// using the USTAR format,
/// or one 'x'-type PAX extended header followed by a 512-byte USTAR header block if the metadata
/// isn't fully representable using the USTAR format.
/// Returns 0 on success, -1 on error.
int scar_pax_meta_write(struct scar_pax_meta *meta, struct scar_io_writer *w);

#endif
