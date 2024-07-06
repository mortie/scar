#include "scar-reader.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "internal-util.h"
#include "compression.h"
#include "ioutil.h"
#include "types.h"
#include "pax-syntax.h"

struct scar_reader {
	struct scar_io_reader *raw_r;
	struct scar_io_seeker *raw_s;
	struct scar_compression comp;

	scar_offset index_offset;
	scar_offset checkpoints_offset;
};

struct scar_index_iterator {
	struct scar_compression *comp;
	struct scar_decompressor *decompressor;

	struct scar_mem_writer buf;
	struct scar_block_reader br;
	struct scar_counting_reader counter;
	scar_offset next_offset;
	struct scar_io_seeker *seeker;
	struct scar_pax_meta global;
};

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

	// Find the correct compression, based on a suffix match of
	// the scar-end section
	if (!scar_compression_init_from_tail(
		&sr->comp, end_block, (size_t)end_block_len)
	) {
		free(sr);
		SCAR_ERETURN(NULL);
	}

	// Now, find the tail, and populate the offsets to
	// the index and the checkpoints
	if (find_tail(sr, end_block, (size_t)end_block_len - sr->comp.eof_marker_len) < 0) {
		free(sr);
		SCAR_ERETURN(NULL);
	}

	sr->raw_r = r;
	sr->raw_s = s;

	return sr;
}

struct scar_index_iterator *scar_reader_iterate(struct scar_reader *sr)
{
	struct scar_index_iterator *it = NULL;

	it = malloc(sizeof(*it));
	if (!it) {
		return NULL;
	}

	scar_pax_meta_init_empty(&it->global);

	it->seeker = sr->raw_s;
	if (it->seeker->seek(it->seeker, sr->index_offset, SCAR_SEEK_START) < 0) {
		scar_index_iterator_free(it);
		SCAR_ERETURN(NULL);
	}

	it->comp = &sr->comp;
	it->decompressor = it->comp->create_decompressor(sr->raw_r);

	scar_counting_reader_init(&it->counter, &it->decompressor->r);
	scar_block_reader_init(&it->br, &it->counter.r);
	scar_mem_writer_init(&it->buf);

	const char *head = "SCAR-INDEX\n";
	const size_t head_len = strlen(head);
	size_t remaining = head_len;
	while (remaining > 0) {
		if (it->br.next == EOF) {
			scar_index_iterator_free(it);
			SCAR_ERETURN(NULL);
		}

		if (scar_mem_writer_put(&it->buf, it->br.next) < 0) {
			scar_index_iterator_free(it);
			SCAR_ERETURN(NULL);
		}

		scar_block_reader_consume(&it->br);
		remaining -= 1;
	}

	if (strncmp(head, it->buf.buf, head_len) != 0) {
		scar_index_iterator_free(it);
		SCAR_ERETURN(NULL);
	}

	it->next_offset = sr->index_offset + it->counter.count;
	it->counter.count = 0;
	it->seeker = sr->raw_s;

	return it;
}

int scar_index_iterator_next(
	struct scar_index_iterator *it,
	struct scar_index_entry *entry
) {
start:
	if (it->br.next < '0' || it->br.next > '9') {
		return 0;
	}

	it->seeker->seek(it->seeker, it->next_offset, SCAR_SEEK_START);

	scar_ssize fieldsize = 0;
	scar_ssize fieldsizelen = 0;
	while (1) {
		if (it->br.next == ' ') {
			break;
		}

		if (it->br.next < '0' || it->br.next > '9') {
			SCAR_ERETURN(-1);
		}

		fieldsize *= 10;
		fieldsize += it->br.next - '0';
		fieldsizelen += 1;
		scar_block_reader_consume(&it->br);
	}

	if (fieldsizelen == 0) {
		SCAR_ERETURN(-1);
	}

	scar_ssize remaining = fieldsize - fieldsizelen;

	scar_block_reader_consume(&it->br); // ' '
	remaining -= 1;

	if (remaining < 0) {
		SCAR_ERETURN(-1);
	}

	// The next character is the type of the entry.
	// However, we don't convert it to a scar_pax_filetype just yet:
	// it might be a 'g', which needs special consideration.
	char ft = it->br.next;
	scar_block_reader_consume(&it->br); // type
	remaining -= 1;

	if (it->br.next != ' ') {
		SCAR_ERETURN(-1);
	}
	scar_block_reader_consume(&it->br); // ' '
	remaining -= 1;

	if (it->br.next == ' ') {
		SCAR_ERETURN(-1);
	}

	entry->offset = 0;
	while (1) {
		if (it->br.next == ' ') {
			break;
		}

		if (it->br.next < '0' || it->br.next > '9') {
			SCAR_ERETURN(-1);
		}

		entry->offset *= 10;
		entry->offset += it->br.next - '0';
		scar_block_reader_consume(&it->br);
		remaining -= 1;
	}

	scar_block_reader_consume(&it->br); // ' '
	remaining -= 1;

	if (remaining <= 1) {
		SCAR_ERETURN(-1);
	}

	if (ft == 'g') {
		if (scar_pax_parse(&it->global, &it->br.r, remaining) < 0) {
			SCAR_ERETURN(-1);
		}

		// We would do 'return scar_index_iterator_next(it, entry)' here
		// if tail recursion was guaranteed,
		// but it's not, and we wanna avoid blowing the stack.
		// So this 'goto' is used as manual guaranteed tail recursion.
		goto start;
	}

	entry->ft = scar_pax_filetype_from_char(ft);

	it->buf.len = 0;
	while (remaining > 1) {
		if (it->br.next == EOF) {
			SCAR_ERETURN(-1);
		}

		if (scar_mem_writer_put(&it->buf, it->br.next) < 0) {
			SCAR_ERETURN(-1);
		}

		scar_block_reader_consume(&it->br);
		remaining -= 1;
	}

	if (it->br.next != '\n') {
		SCAR_ERETURN(-1);
	}

	scar_block_reader_consume(&it->br); // '\n'

	if (scar_mem_writer_put(&it->buf, '\0') < 0) {
		SCAR_ERETURN(-1);
	}

	entry->name = it->buf.buf;
	it->next_offset += it->counter.count;
	it->counter.count = 0;
	return 1;
}

void scar_index_iterator_free(struct scar_index_iterator *it)
{
	it->comp->destroy_decompressor(it->decompressor);
	free(it->buf.buf);
	free(it);
}

void scar_reader_free(struct scar_reader *sr)
{
	free(sr);
}
