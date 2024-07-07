#ifndef SCAR_META_H
#define SCAR_META_H

#include <stdint.h>
#include <math.h>

#include "io.h"

/// The scar_meta_filetype enum represents the possible archive entry types
/// in a scar/pax archive.
enum scar_meta_filetype {
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
enum scar_meta_filetype scar_meta_filetype_from_char(char ch);

/// Convert a scar_pax_filetype to its associated char.
/// SCAR_FT_UNKNOWN is converted to '?'.
char scar_meta_filetype_to_char(enum scar_meta_filetype ft);

/// Struct representing a scar/pax entry's metadata.
/// All fields are optional.
/// A missing filetype field is represented by FT_UNKNOWN,
/// a missing unsigned int field is represented by ~0 (all bits set to 1),
/// a missing double field is represented by NaN,
/// a missing string field is represented by a null pointer.
struct scar_meta {
	enum scar_meta_filetype type;
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

#define SCAR_META_IS_UINT(val) (!!(~(val)))
#define SCAR_META_IS_FLOAT(val) (!isnan(val))
#define SCAR_META_IS_STRING(val) (!!(val))

#define SCAR_META_HAS_MODE(meta) SCAR_META_IS_UINT((meta)->mode)
#define SCAR_META_HAS_DEVMAJOR(meta) SCAR_META_IS_UINT((meta)->devmajor)
#define SCAR_META_HAS_DEVMINOR(meta) SCAR_META_IS_UINT((meta)->devminor)
#define SCAR_META_HAS_ATIME(meta) SCAR_META_IS_FLOAT((meta)->atime)
#define SCAR_META_HAS_CHARSET(meta) SCAR_META_IS_STRING((meta)->charset)
#define SCAR_META_HAS_COMMENT(meta) SCAR_META_IS_STRING((meta)->comment)
#define SCAR_META_HAS_GID(meta) SCAR_META_IS_STRING((meta)->comment)
#define SCAR_META_HAS_GNAME(meta) SCAR_META_IS_STRING((meta)->gname)
#define SCAR_META_HAS_HDRCHARSET(meta) SCAR_META_IS_STRING((meta)->hdrcharset)
#define SCAR_META_HAS_LINKPATH(meta) SCAR_META_IS_STRING((meta)->linkpath)
#define SCAR_META_HAS_MTIME(meta) SCAR_META_IS_FLOAT((meta)->mtime)
#define SCAR_META_HAS_PATH(meta) SCAR_META_IS_STRING((meta)->path)
#define SCAR_META_HAS_SIZE(meta) SCAR_META_IS_UINT((meta)->size)
#define SCAR_META_HAS_UID(meta) SCAR_META_IS_UINT((meta)->uid)
#define SCAR_META_HAS_UNAME(meta) SCAR_META_IS_STRING((meta)->uname)

/// Initialize all fields to their 'missing' value.
void scar_meta_init_empty(struct scar_meta *meta);

/// Initialize a pax_meta struct which represents a regular file.
void scar_meta_init_file(
	struct scar_meta *meta, char *path, uint64_t size);

/// Initialize a pax_meta struct which represents a hardlink.
void scar_meta_init_hardlink(
	struct scar_meta *meta, char *path, char *linkpath);

/// Initialize a pax_meta struct which represents a symlink.
void scar_meta_init_symlink(
	struct scar_meta *meta, char *path, char *linkpath);

/// Initialize a pax_meta struct which represents a directory.
void scar_meta_init_directory(struct scar_meta *meta, char *path);

/// Initialize a pax_meta struct which represents a chardev.
void scar_meta_init_chardev(
	struct scar_meta *meta, char *path, uint32_t maj, uint32_t min);

/// Initialize a pax_meta struct which represents a blockdev.
void scar_meta_init_blockdev(
	struct scar_meta *meta, char *path, uint32_t maj, uint32_t min);

/// Initialize a pax_meta struct which represents a fifo.
void scar_meta_init_fifo(struct scar_meta *meta, char *path);

/// Copy a scar_meta to 'dest' from 'src'.
void scar_meta_copy(struct scar_meta *dest, struct scar_meta *src);

/// Pretty-print a metadata struct, for debugging purposes.
void scar_meta_print(struct scar_meta *meta, struct scar_io_writer *w);

/// Free up every allocated string in a pax_meta struct.
/// The scar_meta will be initialized to its empty state,
/// as if by 'scar_meta_init_empty'.
void scar_meta_destroy(struct scar_meta *meta);

#endif
