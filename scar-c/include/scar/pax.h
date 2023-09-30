#ifndef SCAR_PAX_H
#define SCAR_PAX_H

#include "io.h"

#include <stdint.h>

enum scar_pax_filetype {
	SCAR_FT_FILE,
	SCAR_FT_HARDLINK,
	SCAR_FT_SYMLINK,
	SCAR_FT_CHARDEV,
	SCAR_FT_BLOCKDEV,
	SCAR_FT_DIRECTORY,
	SCAR_FT_FIFO,
	SCAR_FT_UNKNOWN,
};

/// Convert a char to its associated scar_pax_filetype.
/// Characters with unknown meaning are converted to SCAR_FT_UNKNOWN.
enum scar_pax_filetype scar_pax_filetype_from_char(char ch);

/// Convert a scar_pax_filetype to its associated char.
/// SCAR_FT_UNKNOWN is converted to '?'.
char scar_pax_filetype_to_char(enum scar_pax_filetype ft);

/// Struct representing a pax entry's metadata.
/// Any 'double' field might be NaN, any 'char *' field might be null.
/// All 'char *' fields are owning pointers, and will be freed by the '_destroy' function.
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

/// Initialize a pax_meta struct to its "zero" value.
/// Every double is set to NaN, every string is set to null.
void scar_pax_meta_init_empty(struct scar_pax_meta *meta);

/// Copy a scar_pax_meta to 'dest' from 'src'.
void scar_pax_meta_copy(struct scar_pax_meta *dest, struct scar_pax_meta *src);

/// Free up every allocated string in a pax_meta struct.
/// The struct can be re-used after another call to scar_pax_meta_init.
void scar_pax_meta_destroy(struct scar_pax_meta *meta);

/// Pretty-print a metadata struct, for debugging purposes.
void scar_pax_meta_print(struct scar_pax_meta *meta, struct scar_io_writer *w);

/// Parse a single pax metadata block from the input stream, and populate the meta struct.
/// Note: the function might read more than one USTAR header block.

#endif
