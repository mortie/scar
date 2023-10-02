#include "pax.h"

#include "ioutil.h"
#include "pax-syntax.h"

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

struct ustar_field {
	size_t start;
	size_t length;
};

const struct ustar_field UST_NAME = {0, 100};
const struct ustar_field UST_MODE = {100, 8};
const struct ustar_field UST_UID = {108, 8};
const struct ustar_field UST_GID = {116, 8};
const struct ustar_field UST_SIZE = {124, 12};
const struct ustar_field UST_MTIME = {136, 12};
const struct ustar_field UST_CHKSUM = {148, 8};
const struct ustar_field UST_TYPEFLAG = {156, 1};
const struct ustar_field UST_LINKNAME = {157, 100};
const struct ustar_field UST_MAGIC = {257, 6};
const struct ustar_field UST_VERSION = {263, 2};
const struct ustar_field UST_UNAME = {265, 32};
const struct ustar_field UST_GNAME = {297, 32};
const struct ustar_field UST_DEVMAJOR = {329, 8};
const struct ustar_field UST_DEVMINOR = {337, 8};
const struct ustar_field UST_PREFIX = {345, 155};

// strdup isn't in standard C :(
static char *dupstr(const char *src)
{
	if (!src) {
		return NULL;
	}

	size_t len = strlen(src);
	char *dest = malloc(len + 1);
	memcpy(dest, src, len + 1);
	return dest;
}

enum scar_pax_filetype scar_pax_filetype_from_char(char ch)
{
	switch (ch) {
	case '0':
	case '\0':
	case '7':
		return SCAR_FT_FILE;
	case '1':
		return SCAR_FT_HARDLINK;
	case '2':
		return SCAR_FT_SYMLINK;
	case '3':
		return SCAR_FT_CHARDEV;
	case '4':
		return SCAR_FT_BLOCKDEV;
	case '5':
		return SCAR_FT_DIRECTORY;
	case '6':
		return SCAR_FT_FIFO;
	}

	return SCAR_FT_UNKNOWN;
}

char scar_pax_filetype_to_char(enum scar_pax_filetype ft)
{
	switch (ft) {
	case SCAR_FT_UNKNOWN:
		return '?';
	case SCAR_FT_FILE:
		return '0';
	case SCAR_FT_HARDLINK:
		return '1';
	case SCAR_FT_SYMLINK:
		return '2';
	case SCAR_FT_CHARDEV:
		return '3';
	case SCAR_FT_BLOCKDEV:
		return '4';
	case SCAR_FT_DIRECTORY:
		return '5';
	case SCAR_FT_FIFO:
		return '6';
	}

	return '?';
}

void scar_pax_meta_init_empty(struct scar_pax_meta *meta)
{
	memset(meta, 0, sizeof(*meta));
	meta->atime = NAN;
	meta->mtime = NAN;
}

void scar_pax_meta_init_file(struct scar_pax_meta *meta, char *path)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_FILE;
	meta->path = dupstr(path);
}

void scar_pax_meta_init_hardlink(struct scar_pax_meta *meta, char *path, char *linkpath)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_HARDLINK;
	meta->path = dupstr(path);
	meta->linkpath = dupstr(linkpath);
}

void scar_pax_meta_init_symlink(struct scar_pax_meta *meta, char *path, char *linkpath)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_SYMLINK;
	meta->path = dupstr(path);
	meta->linkpath = dupstr(linkpath);
}

void scar_pax_meta_init_directory(struct scar_pax_meta *meta, char *path)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_DIRECTORY;
	meta->path = dupstr(path);
}

void scar_pax_meta_init_chardev(struct scar_pax_meta *meta, char *path, uint32_t maj, uint32_t min)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_CHARDEV;
	meta->path = dupstr(path);
	meta->devmajor = maj;
	meta->devminor = min;
}

void scar_pax_meta_init_blockdev(struct scar_pax_meta *meta, char *path, uint32_t maj, uint32_t min)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_BLOCKDEV;
	meta->path = dupstr(path);
	meta->devmajor = maj;
	meta->devminor = min;
}

void scar_pax_meta_init_fifo(struct scar_pax_meta *meta, char *path)
{
	scar_pax_meta_init_empty(meta);
	meta->type = SCAR_FT_FIFO;
	meta->path = dupstr(path);
}

void scar_pax_meta_copy(struct scar_pax_meta *dest, struct scar_pax_meta *src)
{
	memcpy(dest, src, sizeof(struct scar_pax_meta));
	dest->charset = dupstr(src->charset);
	dest->comment = dupstr(src->comment);
	dest->gname = dupstr(src->gname);
	dest->hdrcharset = dupstr(src->hdrcharset);
	dest->linkpath = dupstr(src->linkpath);
	dest->path = dupstr(src->path);
	dest->uname = dupstr(src->uname);
}

void scar_pax_meta_destroy(struct scar_pax_meta *meta)
{
	free(meta->charset);
	free(meta->comment);
	free(meta->gname);
	free(meta->hdrcharset);
	free(meta->linkpath);
	free(meta->path);
	free(meta->uname);
}

void scar_pax_meta_print(struct scar_pax_meta *meta, struct scar_io_writer *w)
{
	scar_io_printf(w, "Metadata{\n");
	scar_io_printf(w, "\ttype: %c\n", scar_pax_filetype_to_char(meta->type));
	scar_io_printf(w, "\tmode: 0%03" PRIu32 "\n", meta->mode);
	scar_io_printf(w, "\tdevmajor: %" PRIu32 "\n", meta->devmajor);
	scar_io_printf(w, "\tdevminor: %" PRIu32 "\n", meta->devminor);

	if (!isnan(meta->atime))
		scar_io_printf(w, "\tatime: %f\n", meta->atime);
	if (meta->charset)
		scar_io_printf(w, "\tcharset: %s\n", meta->charset);
	if (meta->comment)
		scar_io_printf(w, "\tcomment: %s\n", meta->comment);

	scar_io_printf(w, "\tgid: %" PRIu64 "\n", meta->gid);
	if (meta->gname)
		scar_io_printf(w, "\tgname: %s\n", meta->gname);
	if (meta->hdrcharset)
		scar_io_printf(w, "\thdrcharset: %s\n", meta->hdrcharset);
	if (meta->linkpath)
		scar_io_printf(w, "\tlinkpath: %s\n", meta->linkpath);
	if (!isnan(meta->mtime))
		scar_io_printf(w, "\tmtime: %f\n", meta->mtime);
	if (meta->path)
		scar_io_printf(w, "\tpath: %s\n", meta->path);
	scar_io_printf(w, "\tsize: %" PRIu64 "\n", meta->size);
	scar_io_printf(w, "\tuid: %" PRIu64 "\n", meta->uid);
	if (meta->uname)
		scar_io_printf(w, "\tuname: %s\n", meta->uname);
	scar_io_printf(w, "}\n");
}

static scar_offset parse_octal(unsigned char *text, size_t size)
{
	uint64_t num = 0;
	for (size_t i = 0; i < size; ++i) {
		unsigned char ch = text[i];
		if (ch < '0' || ch > '7') {
			return num;
		}

		num *= 8;
		num += ch - '0';
	}

	return num;
}

static uint64_t parse_size(unsigned char *text, size_t size)
{
	if (text[0] < 128) {
		return parse_octal(text, size);
	}

	uint64_t num = text[0] & 0x7f;
	for (size_t i = 1; i < size; ++i) {
		num *= 256;
		num += text[i];
	}

	return num;
}

static int read_block_aligned(void *buf, size_t size, struct scar_io_reader *r)
{
	unsigned char block[512];

	uint64_t aligned = 0;
	while (size >= 512) {
		aligned += 512;
		size -= 512;
	}

	if (aligned > 0) {
		scar_ssize n = r->read(r, buf, aligned);
		if (n < 0 || (uint64_t)n < aligned) {
			return -1;
		}
	}

	if (size > 0) {
		if (r->read(r, block, 512) < 512) {
			return -1;
		}

		memcpy((char *)buf + aligned, block, size);
	}

	return 0;
}

static int read_pax_aligned(struct scar_pax_meta *meta, struct scar_io_reader *r)
{
	return 0;
}

int scar_pax_meta_parse(
	struct scar_pax_meta *global, struct scar_pax_meta *meta, struct scar_io_reader *r)
{
	unsigned char block[512];
	unsigned char ftype;
	uint64_t size;

	scar_pax_meta_copy(meta, global);

	// Read past any metadata style archive entries.
	// Once this loop finishes, we'll have 'meta' and 'global' filled,
	// and we'll be ready to read the next non-metadata entry's header block.
	while (1) {
		if (r->read(r, block, 512) < 512) {
			return -1;
		}

		size = parse_size(&block[UST_SIZE.start], UST_SIZE.length);
		ftype = block[UST_TYPEFLAG.start];

		// GNU extension: path block.
		if (ftype == 'L') {
			free(meta->path);
			meta->path = malloc(size + 1);
			if (meta->path == NULL) {
				return -1;
			}

			if (read_block_aligned(meta->path, size, r) < 0) {
				return -1;
			}

			meta->path[size] = '\0';
		}

		// GNU extension: linkpath block.
		else if (ftype == 'K') {
			free(meta->linkpath);
			meta->linkpath = malloc(size + 1);
			if (meta->linkpath == NULL) {
				return -1;
			}

			if (read_block_aligned(meta->linkpath, size, r) < 0) {
				return -1;
			}

			meta->linkpath[size] = '\0';
		}

		// Pax extension: metadata block.
		else if (ftype == 'x') {
			if (read_pax_aligned(meta, r) < 0) {
				return -1;
			}
		}

		// Pax extension: global metadata block.
		else if (ftype == 'g') {
			if (read_pax_aligned(global, r) < 0) {
				return -1;
			}

			scar_pax_meta_copy(meta, global);
		}

		// Anything else should be a valid scar_pax_filetype.
		// That means we reached the header block for the actual file entry we're interested in.
		else {
			break;
		}
	}

	meta->type = scar_pax_filetype_from_char(ftype);
	if (meta->type == SCAR_FT_UNKNOWN) {
		return -1;
	}

	return 0;
}
