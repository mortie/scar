#include "pax-meta.h"

#include "ioutil.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>

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
