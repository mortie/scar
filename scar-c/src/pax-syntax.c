#include "pax-syntax.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "internal-util.h"
#include "ioutil.h"

static int parse_time(struct scar_block_reader *br, size_t size, double *num)
{
	// This float parser probably isn't correct,
	// I'm guessing numbers don't really round-trip correctly.

	double sign = 1;
	if (br->next == '-') {
		sign = -1;
		size -= 1;
		scar_block_reader_consume(br);
	}  else if (br->next == '+') {
		size -= 1;
		scar_block_reader_consume(br);
	}

	uint64_t intpart = 0;
	while (br->next != '.' && size > 0) {
		if (br->next < '0' && br->next > '9') {
			SCAR_ERETURN(-1);
		}

		intpart *= 10;
		intpart += (uint64_t)(br->next - '0');
		size -= 1;
		scar_block_reader_consume(br);
	}

	if (size == 0) {
		*num = (double)intpart;
		return 0;
	}

	if (br->next != '.') {
		SCAR_ERETURN(-1);
	}

	size -= 1;
	scar_block_reader_consume(br); // '.'

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
		scar_block_reader_consume(br);
	}

	*num = ((double)intpart + ((double)fracpart / (double)fracpow)) * sign;
	return 0;
}

static int scar_parse_string(struct scar_block_reader *br, size_t size, char **str)
{
	char *buf = malloc(size + 1);
	if (buf == NULL) {
		SCAR_ERETURN(-1);
	}

	if (scar_block_reader_read(br, buf, size) < 0) {
		free(buf);
		SCAR_ERETURN(-1);
	}

	buf[size] = '\0';
	free(*str);
	*str = buf;
	return 0;
}

static int parse_u64(struct scar_block_reader *br, size_t size, uint64_t *num)
{
	uint64_t n = 0;
	while (size > 0) {
		if (br->next < '0' || br->next > '9') {
			SCAR_ERETURN(-1);
		}

		n *= 10;
		n += (uint64_t)(br->next - '0');
		scar_block_reader_consume(br);
		size -= 1;
	}

	*num = n;
	return 0;
}

static int parse_one(struct scar_pax_meta *meta, struct scar_block_reader *br) {
	size_t fieldsize = 0;
	size_t fieldsize_len = 0;
	while (br->next != ' ') {
		if (br->next < '0' || br->next > '9') {
			SCAR_ERETURN(-1);
		}

		fieldsize *= 10;
		fieldsize += (uint64_t)(br->next - '0');
		fieldsize_len += 1;
		scar_block_reader_consume(br);
	}

	scar_block_reader_consume(br); // ' '

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

		scar_block_reader_consume(br);
	}
	fieldname[fieldname_len] = '\0';

	scar_block_reader_consume(br); // '='

	int ret;
	if (strcmp(fieldname, "atime") == 0) {
		ret = parse_time(br, fieldsize, &meta->atime);
	} else if (strcmp(fieldname, "charset") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->charset);
	} else if (strcmp(fieldname, "comment") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->comment);
	} else if (strcmp(fieldname, "gid") == 0) {
		ret = parse_u64(br, fieldsize, &meta->gid);
	} else if (strcmp(fieldname, "gname") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->gname);
	} else if (strcmp(fieldname, "hdrcharset") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->hdrcharset);
	} else if (strcmp(fieldname, "linkpath") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->linkpath);
	} else if (strcmp(fieldname, "mtime") == 0) {
		ret = parse_time(br, fieldsize, &meta->mtime);
	} else if (strcmp(fieldname, "path") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->path);
	} else if (strcmp(fieldname, "size") == 0) {
		ret = parse_u64(br, fieldsize, &meta->size);
	} else if (strcmp(fieldname, "uid") == 0) {
		ret = parse_u64(br, fieldsize, &meta->uid);
	} else if (strcmp(fieldname, "uname") == 0) {
		ret = scar_parse_string(br, fieldsize, &meta->uname);
	} else {
		ret = scar_block_reader_skip(br, fieldsize);
	}

	if (ret < 0) {
		printf("hey returning -1 because ret < 0\n");
		return ret;
	}

	if (br->next != '\n') {
		printf("hey returning -1 because aaa\n");
		SCAR_ERETURN(-1);
	}

	scar_block_reader_consume(br); // '\n'
	return 0;
}

int scar_pax_parse(
	struct scar_pax_meta *meta, struct scar_io_reader *r, uint64_t size
) {
	struct scar_block_reader br;
	scar_block_reader_init(&br, r, size);

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
