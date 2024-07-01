#include "scar-reader.h"

#include "internal-util.h"
#include "compression.h"
#include "ioutil.h"

#include <stdlib.h>
#include <string.h>

struct scar_reader {
	struct scar_io_reader *raw_r;
	struct scar_io_seeker *raw_s;
	struct scar_compression comp;

	scar_offset index_offset;
	scar_offset checkpoints_offset;
};

struct scar_index_iterator {
	struct scar_mem_writer buf;
	struct scar_block_reader br;
	struct scar_counting_reader counter;
	scar_offset next_offset;
	struct scar_io_seeker *seeker;
};

static int suffix_match(
		const void *heystack, size_t heystack_len,
		const void *needle, size_t needle_len)
{
	if (needle_len > heystack_len) {
		return 0;
	}

	unsigned char *heystack_ptr = (unsigned char *)heystack + (heystack_len - needle_len);
	return memcmp(heystack_ptr, needle, needle_len) == 0;
}

static int find_compression(void *end, size_t end_len, struct scar_compression *out)
{
	scar_compression_init_gzip(out);
	if (suffix_match(end, (size_t)end_len, out->eof_marker, out->eof_marker_len)) {
		return 0;
	}

	SCAR_ERETURN(-1);
}

// Returns 1 if the tail was successfully parsed, 0 if not,
// and -1 if it failed.
static int parse_tail(struct scar_reader *sr, unsigned char *tail, size_t len)
{
	char plainbuf[512];
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, tail, len);

	struct scar_decompressor *d = sr->comp.create_decompressor(&mr.r);
	if (!d) {
		SCAR_ERETURN(-1);
	}

	scar_ssize plainlen = d->r.read(&d->r, plainbuf, sizeof(plainbuf));
	if (plainlen < 10) {
		free(d);
		return 0;
	}

	char *plain = plainbuf;
	if (memcmp("SCAR-TAIL\n", plain, 10) != 0) {
		free(d);
		return 0;
	}

	plain += 10;
	plainlen -= 10;

	sr->index_offset = 0;
	while (plainlen > 1 && *plain != '\n') {
		if (*plain < '0' || *plain > '9') {
			free(d);
			return 0;
		}
		sr->index_offset *= 10;
		sr->index_offset += *plain - '0';
		plain += 1;
		plainlen -= 1;
	}

	if (*plain != '\n') {
		free(d);
		return 0;
	}
	plain += 1;
	plainlen -= 1;

	sr->checkpoints_offset = 0;
	while (plainlen > 1 && *plain != '\n') {
		if (*plain < '0' || *plain > '9') {
			free(d);
			return 0;
		}
		sr->checkpoints_offset *= 10;
		sr->checkpoints_offset += *plain - '0';
		plain += 1;
		plainlen -= 1;
	}

	if (*plain != '\n') {
		free(d);
		return 0;
	}

	return 1;
}

static int find_tail(struct scar_reader *sr, unsigned char *end, size_t len)
{
	unsigned char *ptr = end + len - sr->comp.magic_len;
	while (ptr >= end) {
		if (memcmp(ptr, sr->comp.magic, sr->comp.magic_len) == 0) {
			int ret = parse_tail(sr, ptr, len - (size_t)(ptr - end));
			if (ret < 0) {
				SCAR_ERETURN(-1);
			} else if (ret) {
				return 0;
				}
		}

		ptr -= 1;
	}

	SCAR_ERETURN(-1);
}

struct scar_reader *scar_reader_create(
	struct scar_io_reader *r, struct scar_io_seeker *s)
{
	unsigned char end_block[512];

	if (s->seek(s, 0, SCAR_SEEK_END) < 0) {
		SCAR_ERETURN(NULL);
	}

	scar_offset file_len = s->tell(s);
	if (file_len < 0) {
		SCAR_ERETURN(NULL);
	}

	scar_offset end_block_len = sizeof(end_block);
	if (end_block_len > file_len) {
		end_block_len = file_len;
	}

	if (s->seek(s, -end_block_len, SCAR_SEEK_CURRENT) < 0) {
		SCAR_ERETURN(NULL);
	}

	if (r->read(r, end_block, (size_t)end_block_len) < end_block_len) {
		SCAR_ERETURN(NULL);
	}

	struct scar_reader *sr = malloc(sizeof(*sr));
	if (!sr) {
		SCAR_ERETURN(NULL);
	}

	// Find the correct compression, based on a suffix match of the scar-end section
	if (find_compression(end_block, (size_t)end_block_len, &sr->comp) < 0) {
		free(sr);
		SCAR_ERETURN(NULL);
	}

	// Now, find the tail, and populate the offsets to the index and the checkpoints
	if (find_tail(sr, end_block, (size_t)end_block_len - sr->comp.eof_marker_len) < 0) {
		free(sr);
		SCAR_ERETURN(NULL);
	}

	printf("Hey found tail: %d, %d\n", (int)sr->index_offset, (int)sr->checkpoints_offset);

	sr->raw_r = r;
	sr->raw_s = s;

	return sr;
}

struct scar_index_iterator *scar_reader_iterate(struct scar_reader *sr)
{
	struct scar_index_iterator *it = NULL;

	if (sr->raw_s->seek(sr->raw_s, sr->index_offset, SCAR_SEEK_START) < 0) {
		return NULL;
	}

	it = malloc(sizeof(*it));
	if (!it) {
		return NULL;
	}

	scar_counting_reader_init(&it->counter, sr->raw_r);
	scar_block_reader_init(&it->br, &it->counter.r, 0);

	const char *head = "SCAR-INDEX\n";
	const size_t head_len = strlen(head);
	size_t remaining = head_len;
	while (remaining > 0) {
		if (it->br.eof) {
			free(it->buf.buf);
			free(it);
			return NULL;
		}

		if (scar_mem_writer_put(&it->buf, it->br.next) < 0) {
			free(it->buf.buf);
			free(it);
			return NULL;
		}

		remaining -= 1;
	}

	if (strncmp(head, it->buf.buf, head_len) != 0) {
		free(it->buf.buf);
		free(it);
		return NULL;
	}

	it->next_offset = sr->index_offset + it->counter.count;
	it->seeker = sr->raw_s;

	return it;
}

int scar_index_iterator_next(
	struct scar_index_iterator *it,
	struct scar_index_entry *entry
) {
	// TODO
	return 0;
}

void scar_index_iterator_free(struct scar_index_iterator *it)
{
	free(it->buf.buf);
	free(it);
}

void scar_reader_free(struct scar_reader *sr)
{
	free(sr);
}
