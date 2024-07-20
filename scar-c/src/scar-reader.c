#include "scar-reader.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "internal-util.h"
#include "compression.h"
#include "io.h"
#include "ioutil.h"
#include "pax.h"
#include "types.h"
#include "pax-syntax.h"

struct checkpoint {
	scar_offset compressed;
	scar_offset uncompressed;
};

struct scar_reader {
	struct scar_io_reader *raw_r;
	struct scar_io_seeker *raw_s;
	struct scar_compression comp;

	struct scar_decompressor *current_decomp;

	bool has_checkpoints;
	struct checkpoint *checkpoints;
	size_t checkpointcount;

	scar_offset index_offset;
	scar_offset checkpoints_offset;
};

struct scar_index_iterator {
	struct scar_compression *comp;
	struct scar_decompressor *decompressor;

	struct scar_mem_writer buf;
	struct scar_block_reader br;
	scar_offset next_offset;
	struct scar_io_seeker *seeker;
	struct scar_meta global;
};

static int reader_ensure_checkpoint_section(struct scar_reader *sr)
{
	if (sr->has_checkpoints) {
		return 0;
	}

	if (sr->raw_s->seek(
		sr->raw_s, sr->checkpoints_offset, SCAR_SEEK_START) < 0
	) {
		SCAR_ERETURN(-1);
	}

	struct scar_decompressor *decomp = sr->comp.create_decompressor(sr->raw_r);
	if (!decomp) {
		SCAR_ERETURN(-1);
	}

	int ret = 0;
	struct scar_block_reader br;
	scar_block_reader_init(&br, &decomp->r);

	char line[64];
	scar_ssize len = scar_block_reader_read_line(&br, line, sizeof(line));
	if (len == 0) {
		sr->comp.destroy_decompressor(decomp);
		SCAR_ERETURN(-1);
	}

	if (strcmp(line, "SCAR-CHECKPOINTS") != 0) {
		sr->comp.destroy_decompressor(decomp);
		SCAR_ERETURN(-1);
	}

	while (true) {
		scar_ssize len = scar_block_reader_read_line(&br, line, sizeof(line));
		if (br.error) {
			SCAR_ELOG();
			ret = -1;
			break;
		}

		if (len == 0 || strcmp(line, "SCAR-TAIL") == 0) {
			sr->has_checkpoints = true;
			break;
		}

		char *sep = strchr(line, ' ');
		if (!sep) {
			SCAR_ELOG();
			ret = -1;
			break;
		}

		*sep = '\0';
		char *endptr = NULL;

		long long compressed = strtoll(line, &endptr, 10);
		if (*endptr != '\0') {
			SCAR_ELOG();
			ret = -1;
			break;
		}

		long long uncompressed = strtoll(sep + 1, &endptr, 10);
		if (*endptr != '\0') {
			SCAR_ELOG();
			ret = -1;
			break;
		}

		sr->checkpointcount += 1;
		void *new_alloc = realloc(
			sr->checkpoints, sr->checkpointcount * sizeof(*sr->checkpoints));
		if (!new_alloc) {
			SCAR_ELOG();
			ret = -1;
			break;
		}

		sr->checkpoints = new_alloc;
		sr->checkpoints[sr->checkpointcount - 1].compressed = compressed;
		sr->checkpoints[sr->checkpointcount - 1].uncompressed = uncompressed;
	}

	if (ret != 0) {
		free(sr->checkpoints);
		sr->checkpoints = NULL;
		sr->checkpointcount = 0;
	}

	sr->comp.destroy_decompressor(decomp);
	return ret;
}

static int reader_find_checkpoint(
	struct scar_reader *sr, scar_offset offset_uc,
	struct checkpoint *chkpoint
) {
	if (reader_ensure_checkpoint_section(sr) < 0) {
		SCAR_ERETURN(-1);
	}

	chkpoint->compressed = 0;
	chkpoint->uncompressed = 0;

	for (size_t i = 0; i < sr->checkpointcount; ++i) {
		struct checkpoint *curr = &sr->checkpoints[i];
		if (curr->uncompressed <= offset_uc) {
			chkpoint->compressed = curr->compressed;
			chkpoint->uncompressed = curr->uncompressed;
		} else {
			break;
		}
	}

	return 0;
}

static int reader_seek_to(struct scar_reader *sr, scar_offset offset_uc)
{
	struct checkpoint chkpoint;
	if (reader_find_checkpoint(sr, offset_uc, &chkpoint) < 0) {
		SCAR_ERETURN(-1);
	}

	if (sr->current_decomp) {
		sr->comp.destroy_decompressor(sr->current_decomp);
	}

	if (sr->raw_s->seek(sr->raw_s, chkpoint.compressed, SCAR_SEEK_START) < 0) {
		SCAR_ERETURN(-1);
	}

	sr->current_decomp = sr->comp.create_decompressor(sr->raw_r);
	if (!sr->current_decomp) {
		SCAR_ERETURN(-1);
	}

	char buf[512];
	scar_offset skip = offset_uc - chkpoint.uncompressed;
	while (skip > 0) {
		size_t n = skip;
		if (n > sizeof(buf)) {
			n = sizeof(buf);
		}

		scar_ssize ret = sr->current_decomp->r.read(
			&sr->current_decomp->r, buf, n);
		if (ret < (scar_ssize)n) {
			SCAR_ERETURN(-1);
		}

		skip -= n;
	}

	return 0;
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
		sr->comp.destroy_decompressor(d);
		return 0;
	}

	char *plain = plainbuf;
	if (memcmp("SCAR-TAIL\n", plain, 10) != 0) {
		sr->comp.destroy_decompressor(d);
		return 0;
	}

	plain += 10;
	plainlen -= 10;

	sr->index_offset = 0;
	while (plainlen > 1 && *plain != '\n') {
		if (*plain < '0' || *plain > '9') {
			sr->comp.destroy_decompressor(d);
			return 0;
		}
		sr->index_offset *= 10;
		sr->index_offset += *plain - '0';
		plain += 1;
		plainlen -= 1;
	}

	if (*plain != '\n') {
		sr->comp.destroy_decompressor(d);
		return 0;
	}
	plain += 1;
	plainlen -= 1;

	sr->checkpoints_offset = 0;
	while (plainlen > 1 && *plain != '\n') {
		if (*plain < '0' || *plain > '9') {
			sr->comp.destroy_decompressor(d);
			return 0;
		}
		sr->checkpoints_offset *= 10;
		sr->checkpoints_offset += *plain - '0';
		plain += 1;
		plainlen -= 1;
	}

	if (*plain != '\n') {
		sr->comp.destroy_decompressor(d);
		return 0;
	}

	sr->comp.destroy_decompressor(d);
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

	sr->current_decomp = NULL;
	sr->has_checkpoints = false;
	sr->checkpoints = NULL;
	sr->checkpointcount = 0;
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

	scar_meta_init_empty(&it->global);

	it->seeker = sr->raw_s;
	if (it->seeker->seek(it->seeker, sr->index_offset, SCAR_SEEK_START) < 0) {
		scar_index_iterator_free(it);
		SCAR_ERETURN(NULL);
	}

	it->comp = &sr->comp;
	it->decompressor = it->comp->create_decompressor(sr->raw_r);

	scar_block_reader_init(&it->br, &it->decompressor->r);
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

	it->seeker = sr->raw_s;
	it->next_offset = it->seeker->tell(it->seeker);
	if (it->next_offset < 0) {
		scar_index_iterator_free(it);
		SCAR_ERETURN(NULL);
	}

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

	entry->ft = scar_meta_filetype_from_char(ft);

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
	entry->global = &it->global;
	it->next_offset = it->seeker->tell(it->seeker);
	if (it->next_offset < 0) {
		SCAR_ERETURN(-1);
	}

	return 1;
}

void scar_index_iterator_free(struct scar_index_iterator *it)
{
	it->comp->destroy_decompressor(it->decompressor);
	free(it->buf.buf);
	free(it);
}

int scar_reader_read_meta(
	struct scar_reader *sr, scar_offset offset,
	const struct scar_meta *global, struct scar_meta *meta
) {
	if (reader_seek_to(sr, offset) < 0) {
		SCAR_ERETURN(-1);
	}

	// scar_pax_read_meta might modify global,
	// we want to avoid that so use a copy instead
	struct scar_meta global2;
	memcpy(&global2, global, sizeof(global2));

	struct scar_counting_reader cr;
	scar_counting_reader_init(&cr, &sr->current_decomp->r);

	if (scar_pax_read_meta(&sr->current_decomp->r, &global2, meta) < 0) {
		SCAR_ERETURN(-1);
	}

	return 0;
}

int scar_reader_read_content(
	struct scar_reader *sr, struct scar_io_writer *w, uint64_t size
) {
	assert(sr->current_decomp);

	struct scar_counting_reader cr;
	scar_counting_reader_init(&cr, &sr->current_decomp->r);

	if (scar_pax_read_content(&cr.r, w, size) < 0) {
		SCAR_ERETURN(-1);
	}

	return 0;
}

void scar_reader_free(struct scar_reader *sr)
{
	if (sr->current_decomp) {
		sr->comp.destroy_decompressor(sr->current_decomp);
	}

	free(sr->checkpoints);
	free(sr);
}
