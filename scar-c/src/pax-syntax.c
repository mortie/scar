#include "pax-syntax.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct block_reader {
	struct scar_io_reader *r;
	size_t index;
	int next;
	int eof;
	int error;
	int bufcap;
	int64_t size;
	unsigned char block[512];
};

static void block_reader_init(struct block_reader *br, struct scar_io_reader *r, uint64_t size)
{
	br->r = r;
	br->index = 0;
	br->next = EOF;
	br->eof = 0;
	br->error = 0;
	br->size = size;
	br->bufcap = 0;

	scar_ssize n = r->read(r, br->block, 512);

	if (n > 0) {
		br->size -= n;
		br->bufcap = (int)n;
		br->next = br->block[0];
	} else if (n < 0) {
		br->error = 1;
		br->eof = 1;
	} else if (n == 0) {
		br->eof = 1;
	}
}

static void block_reader_consume(struct block_reader *br)
{
	if (br->eof) {
		br->next = EOF;
		return;
	}

	br->next = br->block[br->index++];
	if (br->index >= 512) {
		if (br->size <= 0) {
			br->eof = 1;
			return;
		}

		scar_ssize n = br->r->read(br->r, br->block, 512);

		if (n > 0) {
			br->size -= n;
		}

		if (n == 512) {
			br->next = br->block[0];
		} else {
			br->next = EOF;
			br->error = 1;
			br->eof = 1;
		}
	}
}

static int block_reader_skip(struct block_reader *br, size_t n)
{
	// This could be faster...
	while (n > 0) {
		if (br->next == EOF) {
			return -1;
		}

		block_reader_consume(br);
		n -= 1;
	}

	return 0;
}

static int block_reader_read(struct block_reader *br, void *buf, size_t n)
{
	// This could also be faster...
	unsigned char *cbuf = (unsigned char *)buf;
	while (n > 0) {
		if (br->next == EOF) {
			return -1;
		}

		*cbuf = br->next;
		cbuf += 1;

		block_reader_consume(br);
		n -= 1;
	}

	return 0;
}

static int parse_double(struct block_reader *br, size_t size, double *num)
{
}

static int parse_string(struct block_reader *br, size_t size, char **str)
{
	char *buf = malloc(size + 1);
	if (buf == NULL) {
		return -1;
	}

	if (block_reader_read(br, buf, size) < 0) {
		free(buf);
		return -1;
	}

	buf[size] = '\0';
	free(*str);
	*str = buf;
	return 0;
}

static int parse_u64(struct block_reader *br, size_t size, uint64_t *num)
{
	uint64_t n = 0;
	while (size > 0) {
		if (br->next < '0' || br->next > '9') {
			return -1;
		}

		n *= 10;
		n += br->next - '0';
		block_reader_consume(br);
	}

	*num = n;
	return 0;
}

static int parse_one(struct scar_pax_meta *meta, struct block_reader *br) {
	size_t fieldsize = 0;
	size_t fieldsize_len = 0;
	while (br->next != ' ') {
		if (br->next < '0' || br->next > '9') {
			return -1;
		}

		fieldsize *= 10;
		fieldsize += br->next - '0';
		fieldsize_len += 1;
		block_reader_consume(br);
	}

	block_reader_consume(br); // ' '

	// Ignore the length of the field size number itself, the space separator,
	// and the trailing newline
	fieldsize -= fieldsize_len + 1 + 1;

	char fieldname[64];
	size_t fieldname_len = 0;
	while (br->next != '=') {
		if (br->next == EOF) {
			return -1;
		}

		fieldname[fieldname_len++] = br->next;
		if (fieldname_len >= sizeof(fieldname) - 1) {
			return -1;
		}

		fieldsize -= 1;
		if (fieldsize == 0) {
			return -1;
		}

		block_reader_consume(br);
	}
	fieldname[fieldname_len] = '\0';

	block_reader_consume(br); // '='

	int ret;
	if (strcmp(fieldname, "atime") == 0) {
		ret = parse_double(br, fieldsize, &meta->atime);
	} else if (strcmp(fieldname, "charset") == 0) {
		ret = parse_string(br, fieldsize, &meta->charset);
	} else if (strcmp(fieldname, "comment") == 0) {
		ret = parse_string(br, fieldsize, &meta->comment);
	} else if (strcmp(fieldname, "gid") == 0) {
		ret = parse_u64(br, fieldsize, &meta->gid);
	} else if (strcmp(fieldname, "gname") == 0) {
		ret = parse_string(br, fieldsize, &meta->gname);
	} else if (strcmp(fieldname, "hdrcharset") == 0) {
		ret = parse_string(br, fieldsize, &meta->hdrcharset);
	} else if (strcmp(fieldname, "linkpath") == 0) {
		ret = parse_string(br, fieldsize, &meta->linkpath);
	} else if (strcmp(fieldname, "mtime") == 0) {
		ret = parse_double(br, fieldsize, &meta->mtime);
	} else if (strcmp(fieldname, "path") == 0) {
		ret = parse_string(br, fieldsize, &meta->path);
	} else if (strcmp(fieldname, "size") == 0) {
		ret = parse_u64(br, fieldsize, &meta->size);
	} else if (strcmp(fieldname, "uid") == 0) {
		ret = parse_u64(br, fieldsize, &meta->uid);
	} else if (strcmp(fieldname, "uname") == 0) {
		ret = parse_string(br, fieldsize, &meta->uname);
	} else {
		ret = block_reader_skip(br, fieldsize);
	}

	if (ret < 0) {
		return ret;
	}

	if (br->next != '\n') {
		return -1;
	}

	block_reader_consume(br); // '\n'
	return 0;
}

int scar_pax_parse(struct scar_pax_meta *meta, struct scar_io_reader *r, uint64_t size)
{
	struct block_reader br;
	block_reader_init(&br, r, size);

	while (br.next != EOF) {
		if (parse_one(meta, &br) < 0) {
			return -1;
		}
	}

	if (br.error) {
		return -1;
	} else {
		return 0;
	}
}
