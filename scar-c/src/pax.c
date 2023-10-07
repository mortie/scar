#include "pax.h"

#include "util.h"
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
	meta->type = SCAR_FT_UNKNOWN;
	meta->mode = ~(uint32_t)0;
	meta->devmajor = ~(uint32_t)0;
	meta->devminor = ~(uint32_t)0;
	meta->atime = NAN;
	meta->charset = NULL;
	meta->comment = NULL;
	meta->gid = ~(uint64_t)0;
	meta->gname = NULL;
	meta->hdrcharset = NULL;
	meta->linkpath = NULL;
	meta->mtime = NAN;
	meta->path = NULL;
	meta->size = ~(uint64_t)0;
	meta->uid = ~(uint64_t)0;
	meta->uname = NULL;
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

	if (~meta->mode) scar_io_printf(w, "\tmode: 0%03" PRIu32 "\n", meta->mode);
	if (~meta->devmajor) scar_io_printf(w, "\tdevmajor: %" PRIu32 "\n", meta->devmajor);
	if (~meta->devminor) scar_io_printf(w, "\tdevminor: %" PRIu32 "\n", meta->devminor);

	if (!isnan(meta->atime)) scar_io_printf(w, "\tatime: %f\n", meta->atime);
	if (meta->charset) scar_io_printf(w, "\tcharset: %s\n", meta->charset);
	if (meta->comment) scar_io_printf(w, "\tcomment: %s\n", meta->comment);

	if (~meta->gid) scar_io_printf(w, "\tgid: %" PRIu64 "\n", meta->gid);
	if (meta->gname) scar_io_printf(w, "\tgname: %s\n", meta->gname);
	if (meta->hdrcharset) scar_io_printf(w, "\thdrcharset: %s\n", meta->hdrcharset);
	if (meta->linkpath) scar_io_printf(w, "\tlinkpath: %s\n", meta->linkpath);
	if (!isnan(meta->mtime)) scar_io_printf(w, "\tmtime: %f\n", meta->mtime);
	if (meta->path) scar_io_printf(w, "\tpath: %s\n", meta->path);
	if (~meta->size) scar_io_printf(w, "\tsize: %" PRIu64 "\n", meta->size);
	if (~meta->uid) scar_io_printf(w, "\tuid: %" PRIu64 "\n", meta->uid);
	if (meta->uname) scar_io_printf(w, "\tuname: %s\n", meta->uname);

	scar_io_printf(w, "}\n");
}

static int read_bytes_block_aligned(void *buf, size_t size, struct scar_io_reader *r)
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
			SCAR_ERETURN(-1);
		}
	}

	if (size > 0) {
		if (r->read(r, block, 512) < 512) {
			SCAR_ERETURN(-1);
		}

		memcpy((char *)buf + aligned, block, size);
	}

	return 0;
}

static int read_pax_block_aligned(struct scar_pax_meta *meta, size_t size, struct scar_io_reader *r)
{
	unsigned char block[512];

	int leftover = 512 - (size % 512);
	if (leftover == 512) leftover = 0;

	if (scar_pax_parse(meta, r, size) < 0) {
		SCAR_ERETURN(-1);
	}

	if (r->read(r, block, (size_t)leftover) < leftover) {
		SCAR_ERETURN(-1);
	}

	return 0;
}

static size_t block_field_strlen(const unsigned char *block, struct ustar_field field) {
	size_t len;
	for (len = 0; len < field.length && block[field.start + len]; ++len);
	return len;
}

static uint64_t block_read_u64(const unsigned char *block, struct ustar_field field)
{
	const unsigned char *text = &block[field.start];
	uint64_t num = 0;
	for (size_t i = 0; i < field.length; ++i) {
		unsigned char ch = text[i];
		if (ch < '0' || ch > '7') {
			return num;
		}

		num *= 8;
		num += ch - '0';
	}

	return num;
}

static uint32_t block_read_u32(const unsigned char *block, struct ustar_field field)
{
	return (uint32_t)block_read_u64(block, field);
}

static uint64_t block_read_size(const unsigned char *block, struct ustar_field field)
{
	const unsigned char *text = &block[field.start];
	if (text[0] < 128) {
		return block_read_u64(block, field);
	}

	uint64_t num = text[0] & 0x7f;
	for (size_t i = 1; i < field.length; ++i) {
		num *= 256;
		num += text[i];
	}

	return num;
}

static char *block_read_string(const unsigned char *block, struct ustar_field field)
{
	size_t len = block_field_strlen(block, field);
	char *path = malloc(len + 1);
	memcpy(path, &block[field.start], len);
	path[len] = '\0';
	return path;
}

static char *block_read_path(const unsigned char *block, struct ustar_field field)
{
	size_t pfx_len = block_field_strlen(block, UST_PREFIX);
	size_t field_len = block_field_strlen(block, field);

	if (pfx_len == 0) {
		char *path = malloc(field_len + 1);
		if (path == NULL) {
			return NULL;
		}

		memcpy(path, &block[field.start], field_len);
		path[field_len] = '\0';
		return path;
	}

	char *path = malloc(pfx_len + 1 + pfx_len + 1);
	if (path == NULL) {
		return NULL;
	}

	memcpy(path, &block[UST_PREFIX.start], pfx_len);
	path[pfx_len] = '/';
	memcpy(&path[pfx_len + 1], &block[field.start], field_len);
	path[pfx_len + 1 + field_len] = '\0';
	return path;
}

int scar_pax_meta_read(
	struct scar_pax_meta *global, struct scar_pax_meta *meta, struct scar_io_reader *r)
{
	unsigned char block[512];
	char ftype;
	uint64_t size;

	scar_pax_meta_copy(meta, global);

	// Read past any metadata style archive entries.
	// Once this loop finishes, we'll have 'meta' and 'global' filled,
	// and we'll be ready to read the next non-metadata entry's header block.
	while (1) {
		if (r->read(r, block, 512) < 512) {
			SCAR_ERETURN(-1);
		}

		size = block_read_size(block, UST_SIZE);
		ftype = (char)block[UST_TYPEFLAG.start];

		// GNU extension: path block.
		if (ftype == 'L') {
			free(meta->path);
			meta->path = malloc(size + 1);
			if (meta->path == NULL) {
				SCAR_ERETURN(-1);
			}

			if (read_bytes_block_aligned(meta->path, size, r) < 0) {
				SCAR_ERETURN(-1);
			}

			meta->path[size] = '\0';
		}

		// GNU extension: linkpath block.
		else if (ftype == 'K') {
			free(meta->linkpath);
			meta->linkpath = malloc(size + 1);
			if (meta->linkpath == NULL) {
				SCAR_ERETURN(-1);
			}

			if (read_bytes_block_aligned(meta->linkpath, size, r) < 0) {
				SCAR_ERETURN(-1);
			}

			meta->linkpath[size] = '\0';
		}

		// Pax extension: metadata block.
		else if (ftype == 'x') {
			if (read_pax_block_aligned(meta, size, r) < 0) {
				SCAR_ERETURN(-1);
			}
		}

		// Pax extension: global metadata block.
		else if (ftype == 'g') {
			if (read_pax_block_aligned(global, size, r) < 0) {
				SCAR_ERETURN(-1);
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
		SCAR_ERETURN(-1);
	}

	if (!~meta->mode) meta->mode = block_read_u32(block, UST_MODE);
	if (!~meta->devmajor) meta->devmajor = block_read_u32(block, UST_DEVMAJOR);
	if (!~meta->devminor) meta->devminor = block_read_u32(block, UST_DEVMINOR);
	if (!~meta->gid) meta->gid = block_read_u64(block, UST_GID);
	if (!meta->gname) meta->gname = block_read_string(block, UST_GNAME);
	if (!meta->linkpath) meta->linkpath = block_read_path(block, UST_LINKNAME);
	if (isnan(meta->mtime)) meta->mtime = block_read_u64(block, UST_MTIME);
	if (!meta->path) meta->path = block_read_path(block, UST_NAME);
	if (!~meta->size) meta->size = block_read_size(block, UST_SIZE);
	if (!~meta->uid) meta->uid = block_read_u64(block, UST_UID);
	if (!meta->uname) meta->uname = block_read_string(block, UST_UNAME);

	return 0;
}

static void block_write_u64(unsigned char *block, struct ustar_field field, uint64_t num)
{
	if (!~num) {
		num = 0;
	}

	snprintf((char *)&block[field.start], field.length, "%0*" PRIo64, (int)field.length - 1, num);
}

static void block_write_u32(unsigned char *block, struct ustar_field field, uint32_t num)
{
	if (!~num) {
		num = 0;
	}

	snprintf((char *)&block[field.start], field.length, "%0*" PRIo32, (int)field.length - 1, num);
}

static void block_write_string(unsigned char *block, struct ustar_field field, char *str)
{
	if (str == NULL) {
		str = "";
	}

	snprintf((char *)&block[field.start], field.length, "%s", str);
}

static size_t log10_ceil(size_t num)
{
	size_t lg = 0;
	while (num > 0) {
		lg += 1;
		num /= 10;
	}

	return lg;
}

static int pax_write_field(struct scar_mem_writer *mw, char *name, void *buf, size_t len)
{
	size_t namelen = strlen(name);
	size_t fieldsize = 1 + namelen + 1 + len + 1;
	size_t fieldsizelen = log10_ceil(fieldsize);
	if (log10_ceil(fieldsize + fieldsizelen) > fieldsizelen) {
		fieldsizelen += 1;
	}
	fieldsize += fieldsizelen;

	char *destbuf = scar_mem_writer_get_buffer(mw, fieldsize);
	if (destbuf == NULL) {
		SCAR_ERETURN(-1);
	}

	destbuf += sprintf(destbuf, "%zu", fieldsize);
	*(destbuf++) = ' ';
	memcpy(destbuf, name, namelen);
	destbuf += namelen;
	*(destbuf++) = '=';
	memcpy(destbuf, buf, len);
	destbuf += len;
	*(destbuf++) = '\n';

	return 0;
}

static int pax_write_time(struct scar_mem_writer *mw, char *name, double time)
{
	int64_t seconds = (int64_t)floor(time);
	int64_t nanos = (int64_t)round((time - seconds) * 1000000000.0);

	// We'll be writing the number in reverse into buf.
	// Due to the 64-bit integer part and the nanosecond precision fractional part,
	// the numbers can take up to 21 bytes,
	// so this 32-byte buffer should be more than enough.
	// That means we can ignore size checks when writing to it.
	char buf[32];

	// We will be writing to bufptr.
	// Initialize it to point to one past the end of the array,
	// then write to it using '*(--bufptr)'.
	// That means bufptr will always point to the first character in our number.
	char *bufptr = &buf[sizeof(buf)];

	// Let most of this function not care about negatives,
	// just make the numbers positive but keep track of the sign.
	int sign = 1;
	if (nanos < 0) {
		sign = -1;
		seconds = -seconds;
		nanos = -nanos;
	}

	// Start with the reverse fraction
	int found_first_nonzero = 0;
	do {
		char digit = nanos % 10;
		nanos /= 10;

		if (found_first_nonzero || digit != 0) {
			found_first_nonzero = 1;
			*(--bufptr) = '0' + digit;
		}
	} while (nanos != 0);

	// Then the separator
	if (found_first_nonzero) {
		*(--bufptr) = '.';
	}

	// Then the integer part.
	// The fact that this is a 'do .. while' loop means that we *always* write
	// at least one character to the buffer.
	do {
		*(--bufptr) = '0' + (seconds % 10);
		seconds /= 10;
	} while (seconds > 0);

	// Sign?
	if (sign < 0) {
		*(--bufptr) = '-';
	}

	size_t buflen = (size_t)(buf + sizeof(buf) - bufptr);
	return pax_write_field(mw, name, bufptr, buflen);
}

static int pax_write_string(struct scar_mem_writer *mw, char *name, char *str)
{
	return pax_write_field(mw, name, str, strlen(str));
}

static int pax_write_uint(struct scar_mem_writer *mw, char *name, uint64_t num)
{
	char buf[32];
	int n = snprintf(buf, sizeof(buf), "%" PRIu64, num);
	return pax_write_field(mw, name, buf, (size_t)n);
}

int scar_pax_meta_write(struct scar_pax_meta *meta, struct scar_io_writer *w)
{
	unsigned char block[512] = {0};

	struct scar_mem_writer paxhdr;
	scar_mem_writer_init(&paxhdr);

	if (!isnan(meta->atime)) {
		if (pax_write_time(&paxhdr, "atime", meta->atime) < 0) SCAR_ERETURN(-1);
	}

	if (meta->charset) {
		if (pax_write_string(&paxhdr, "charset", meta->charset) < 0) SCAR_ERETURN(-1);
	}

	if (meta->comment) {
		if (pax_write_string(&paxhdr, "comment", meta->comment) < 0) SCAR_ERETURN(-1);
	}

	if (meta->comment) {
		if (pax_write_string(&paxhdr, "comment", meta->comment) < 0) SCAR_ERETURN(-1);
	}

	if (~meta->gid && meta->gid > 07777777ll) {
		if (pax_write_uint(&paxhdr, "gid", meta->gid) < 0) SCAR_ERETURN(-1);
	}

	if (meta->gname && strlen(meta->gname) > 32) {
		if (pax_write_string(&paxhdr, "gname", meta->gname) < 0) SCAR_ERETURN(-1);
	}

	if (meta->hdrcharset) {
		if (pax_write_string(&paxhdr, "hdrcharset", meta->hdrcharset) < 0) SCAR_ERETURN(-1);
	}

	if (meta->linkpath && strlen(meta->linkpath) > 100) {
		if (pax_write_string(&paxhdr, "linkpath", meta->linkpath) < 0) SCAR_ERETURN(-1);
	}

	if (!isnan(meta->mtime) && (
			meta->mtime < 0 || meta->mtime > 0777777777777ll || meta->mtime != floor(meta->mtime))) {
		if (pax_write_time(&paxhdr, "mtime", meta->mtime) < 0) SCAR_ERETURN(-1);
	}

	if (meta->path && strlen(meta->path) > 100) {
		if (pax_write_string(&paxhdr, "path", meta->path) < 0) SCAR_ERETURN(-1);
	}

	if (~meta->size && meta->size > 077777777777ll) {
		if (pax_write_uint(&paxhdr, "size", meta->size) < 0) SCAR_ERETURN(-1);
	}

	if (~meta->uid && meta->uid > 07777777ll) {
		if (pax_write_uint(&paxhdr, "uid", meta->uid) < 0) SCAR_ERETURN(-1);
	}

	if (meta->uname && strlen(meta->uname) > 32) {
		if (pax_write_string(&paxhdr, "uname", meta->uname) < 0) SCAR_ERETURN(-1);
	}

	// Write a pax extended metadata entry if necessary
	if (paxhdr.len > 0) {
		block[UST_TYPEFLAG.start] = 'x';
		block_write_u64(block, UST_SIZE, (uint64_t)paxhdr.len);
		if (w->write(w, block, 512) < 512) {
			SCAR_ERETURN(-1);
		}

		if (w->write(w, paxhdr.buf, paxhdr.len) < (scar_ssize)paxhdr.len) {
			SCAR_ERETURN(-1);
		}

		// Reset the block to 0s, both to re-use it for the header for the next entry,
		// and to use it for padding
		memset(block, 0, 512);

		size_t padding = 512 - (paxhdr.len % 512);
		if (padding < 512) {
			if (w->write(w, block, padding) < 0) {
				SCAR_ERETURN(-1);
			}
		}
	}

	block_write_string(block, UST_NAME, meta->path);
	block_write_u32(block, UST_MODE, meta->mode);
	block_write_u64(block, UST_UID, meta->uid);
	block_write_u64(block, UST_GID, meta->gid);
	block_write_u64(block, UST_SIZE, meta->size);
	block_write_u64(block, UST_MTIME, meta->mtime > 0 ? (uint64_t)meta->mtime : 0);
	block[UST_TYPEFLAG.start] = (unsigned char)scar_pax_filetype_to_char(meta->type);
	block_write_string(block, UST_LINKNAME, meta->linkpath);
	block_write_string(block, UST_UNAME, meta->uname);
	block_write_string(block, UST_GNAME, meta->gname);
	block_write_u32(block, UST_DEVMAJOR, meta->devmajor);
	block_write_u32(block, UST_DEVMINOR, meta->devminor);

	if (w->write(w, block, 512) < 512) {
		SCAR_ERETURN(-1);
	}

	return 0;
}
