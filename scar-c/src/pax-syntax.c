#include "pax-syntax.h"

#include "util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct block_reader {
	struct scar_io_reader *r;
	int next;
	int eof;
	int error;

	int index;
	int bufcap;
	uint64_t size;
	unsigned char block[512];
};

static void block_reader_init(struct block_reader *br, struct scar_io_reader *r, uint64_t size)
{
	br->r = r;
	br->index = 0;
	br->bufcap = 0;
	br->size = size;

	if (size == 0) {
		return;
	}

	size_t len = sizeof(br->block);
	if (len > size) len = size;

	scar_ssize n = r->read(r, br->block, len);
	if (n < (scar_ssize)len) {
		br->next = EOF;
		br->eof = 1;
		br->error = 1;
	} else {
		br->bufcap = (int)n;
		br->next = br->block[br->index++];
		br->eof = 0;
		br->error = 0;
		br->size -= (uint64_t)n;
	}
}

static void block_reader_consume(struct block_reader *br)
{
	if (br->eof) {
		return;
	}

	if (br->index >= br->bufcap) {
		if (br->size == 0) {
			br->next = EOF;
			br->eof = 1;
			return;
		}

		size_t len = sizeof(br->block);
		if (len > br->size) len = br->size;

		scar_ssize n = br->r->read(br->r, br->block, len);
		if (n < (scar_ssize)len) {
			br->next = EOF;
			br->eof = 1;
			br->error = 1;
		} else {
			br->index = 0;
			br->next = br->block[0];
			br->bufcap = (int)n;
			br->size -= (uint64_t)n;
		}

		return;
	}

	br->next = br->block[br->index++];
}

static int block_reader_skip(struct block_reader *br, size_t n)
{
	// This could be faster...
	while (n > 0) {
		if (br->next == EOF) {
			SCAR_ERETURN(-1);
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
			SCAR_ERETURN(-1);
		}

		*cbuf = (unsigned char)br->next;
		cbuf += 1;

		block_reader_consume(br);
		n -= 1;
	}

	return 0;
}

static int parse_time(struct block_reader *br, size_t size, double *num)
{
	// This float parser probably isn't correct,
	// I'm guessing numbers don't really round-trip correctly.

	double sign = 1;
	if (br->next == '-') {
		sign = -1;
		size -= 1;
		block_reader_consume(br);
	}  else if (br->next == '+') {
		size -= 1;
		block_reader_consume(br);
	}

	uint64_t intpart = 0;
	while (br->next != '.' && size > 0) {
		if (br->next < '0' && br->next > '9') {
			SCAR_ERETURN(-1);
		}

		intpart *= 10;
		intpart += (uint64_t)(br->next - '0');
		size -= 1;
		block_reader_consume(br);
	}

	if (size == 0) {
		*num = (double)intpart;
		return 0;
	}

	if (br->next != '.') {
		SCAR_ERETURN(-1);
	}

	size -= 1;
	block_reader_consume(br); // '.'

	uint64_t fracpart = 0;
	uint64_t fracpow = 1;
	while (size > 0) {
		if (br->next < '0' || br->next > '9') {
			SCAR_ERETURN(-1);
		}

		fracpart *= 10;
		fracpart += (uint64_t)(br->next - '0');
		fracpow *= 10;
		size -= 1;
		block_reader_consume(br);
	}

	*num = ((double)intpart + ((double)fracpart / (double)fracpow)) * sign;
	return 0;
}

static int parse_string(struct block_reader *br, size_t size, char **str)
{
	char *buf = malloc(size + 1);
	if (buf == NULL) {
		SCAR_ERETURN(-1);
	}

	if (block_reader_read(br, buf, size) < 0) {
		free(buf);
		SCAR_ERETURN(-1);
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
			SCAR_ERETURN(-1);
		}

		n *= 10;
		n += (uint64_t)(br->next - '0');
		block_reader_consume(br);
		size -= 1;
	}

	*num = n;
	return 0;
}

static int parse_one(struct scar_pax_meta *meta, struct block_reader *br) {
	size_t fieldsize = 0;
	size_t fieldsize_len = 0;
	while (br->next != ' ') {
		if (br->next < '0' || br->next > '9') {
			SCAR_ERETURN(-1);
		}

		fieldsize *= 10;
		fieldsize += (uint64_t)(br->next - '0');
		fieldsize_len += 1;
		block_reader_consume(br);
	}

	block_reader_consume(br); // ' '

	// Ignore the length of the field size number itself, the space separator,
	// the equals sign, and the trailing newline
	fieldsize -= fieldsize_len + 3;

	char fieldname[64];
	size_t fieldname_len = 0;
	while (br->next != '=') {
		if (br->next == EOF) {
			SCAR_ERETURN(-1);
		}

		fieldname[fieldname_len++] = (char)br->next;
		if (fieldname_len >= sizeof(fieldname) - 1) {
			SCAR_ERETURN(-1);
		}

		fieldsize -= 1;
		if (fieldsize == 0) {
			SCAR_ERETURN(-1);
		}

		block_reader_consume(br);
	}
	fieldname[fieldname_len] = '\0';

	block_reader_consume(br); // '='

	int ret;
	if (strcmp(fieldname, "atime") == 0) {
		ret = parse_time(br, fieldsize, &meta->atime);
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
		ret = parse_time(br, fieldsize, &meta->mtime);
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
		printf("hey returning -1 because ret < 0\n");
		return ret;
	}

	if (br->next != '\n') {
		printf("hey returning -1 because aaa\n");
		SCAR_ERETURN(-1);
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
			SCAR_ERETURN(-1);
		}
	}

	if (br.error) {
		SCAR_ERETURN(-1);
	} else {
		return 0;
	}
}
