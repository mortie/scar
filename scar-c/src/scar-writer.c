#include "scar-writer.h"

#include "ioutil.h"
#include "internal-util.h"
#include "pax.h"

#include <stdlib.h>

struct scar_writer {
	int clevel;
	struct scar_compression *comp;
	scar_offset last_checkpoint_uncompressed_offset;

	struct scar_counting_writer compressed_writer;
	struct scar_compressor *compressor;
	struct scar_counting_writer uncompressed_writer;

	struct scar_mem_writer index_buf;
	struct scar_compressor *index_compressor;

	struct scar_mem_writer checkpoints_buf;
	struct scar_compressor *checkpoints_compressor;
};

struct scar_writer *scar_writer_create(
	struct scar_io_writer *w, struct scar_compression *comp, int clevel)
{
	struct scar_writer *sw = malloc(sizeof(*sw));
	if (!sw) {
		SCAR_ERETURN(NULL);
	}

	sw->clevel = clevel;
	sw->comp = comp;
	sw->last_checkpoint_uncompressed_offset = 0;

	scar_counting_writer_init(&sw->compressed_writer, w);
	sw->compressor = sw->comp->create_compressor(&sw->compressed_writer.w, clevel);
	if (!sw->compressor) {
		free(sw);
		SCAR_ERETURN(NULL);
	}
	scar_counting_writer_init(&sw->uncompressed_writer, &sw->compressor->w);

	scar_mem_writer_init(&sw->index_buf);
	sw->index_compressor = sw->comp->create_compressor(&sw->index_buf.w, clevel);
	if (!sw->index_compressor) {
		comp->destroy_compressor(sw->compressor);
		free(sw);
		SCAR_ERETURN(NULL);
	}

	if (scar_io_printf(&sw->index_compressor->w, "SCAR-INDEX\n") < 0) {
		comp->destroy_compressor(sw->compressor);
		comp->destroy_compressor(sw->index_compressor);
		free(sw);
		SCAR_ERETURN(NULL);
	}

	scar_mem_writer_init(&sw->checkpoints_buf);
	sw->checkpoints_compressor = sw->comp->create_compressor(
		&sw->checkpoints_buf.w, clevel);
	if (!sw->checkpoints_compressor) {
		comp->destroy_compressor(sw->compressor);
		comp->destroy_compressor(sw->index_compressor);
		free(sw);
		SCAR_ERETURN(NULL);
	}

	if (scar_io_printf(&sw->checkpoints_compressor->w, "SCAR-CHECKPOINTS\n") < 0) {
		comp->destroy_compressor(sw->compressor);
		comp->destroy_compressor(sw->index_compressor);
		comp->destroy_compressor(sw->checkpoints_compressor);
		free(sw);
		SCAR_ERETURN(NULL);
	}

	return sw;
}

static int create_checkpoint(struct scar_writer *sw)
{
	if (sw->compressor->flush(sw->compressor) < 0) {
		SCAR_ERETURN(-1);
	}

	scar_offset compressed_offset = sw->compressed_writer.count;
	scar_offset uncompressed_offset = sw->uncompressed_writer.count;
	sw->last_checkpoint_uncompressed_offset = uncompressed_offset;

	scar_ssize ret = scar_io_printf(
		&sw->checkpoints_compressor->w, "%lld %lld\n",
		compressed_offset, uncompressed_offset);
	if (ret < 0) {
		SCAR_ERETURN(-1);
	}

	return 0;
}

int scar_writer_write_entry(
	struct scar_writer *sw, struct scar_pax_meta *meta, struct scar_io_reader *r)
{
	scar_ssize ret;

	const int limit = 10 * 1024 * 1024;
	if (
		sw->uncompressed_writer.count >
		sw->last_checkpoint_uncompressed_offset + limit
	) {
		ret = create_checkpoint(sw);
		if (ret < 0) {
			SCAR_ERETURN(-1);
		}
	}

	struct scar_mem_writer entry_buf;
	scar_mem_writer_init(&entry_buf);
	ret = scar_io_printf(
		&entry_buf.w, "%c %lld %s\n", scar_pax_filetype_to_char(meta->type),
		sw->uncompressed_writer.count, meta->path);
	if (ret < 0) {
		free(entry_buf.buf);
		SCAR_ERETURN(-1);
	}

	size_t fieldsize = 1 + entry_buf.len;
	size_t fieldsizelen = log10_ceil(fieldsize);
	if (log10_ceil(fieldsize + fieldsizelen) > fieldsizelen) {
		fieldsizelen += 1;
	}
	fieldsize += fieldsizelen;

	ret = scar_io_printf(&sw->index_compressor->w, "%zu ", fieldsize);
	if (ret < 0) {
		free(entry_buf.buf);
		SCAR_ERETURN(-1);
	}

	ret = sw->index_compressor->w.write(
		&sw->index_compressor->w, entry_buf.buf, entry_buf.len);
	if (ret < (scar_ssize)entry_buf.len) {
		free(entry_buf.buf);
		SCAR_ERETURN(-1);
	}

	free(entry_buf.buf);

	return scar_pax_write_entry(meta, r, &sw->uncompressed_writer.w);
}

int scar_writer_finish(struct scar_writer *sw)
{
	scar_ssize ret;
	if (scar_pax_write_end(&sw->uncompressed_writer.w) < 0) {
		SCAR_ERETURN(-1);
	}

	if (sw->compressor->finish(sw->compressor) < 0) {
		SCAR_ERETURN(-1);
	}

	if (sw->index_compressor->finish(sw->index_compressor) < 0) {
		SCAR_ERETURN(-1);
	}

	if (sw->checkpoints_compressor->finish(sw->checkpoints_compressor) < 0) {
		SCAR_ERETURN(-1);
	}

	scar_offset index_compressed_offset = sw->compressed_writer.count;
	scar_offset checkpoints_compressed_offset =
		index_compressed_offset + (scar_offset)sw->index_buf.len;

	// We don't need to count anymore,
	// so we'll just directly use the backing writer for the compressed stream from now on
	struct scar_io_writer *w = sw->compressed_writer.backing_w;

	ret = w->write(w, sw->index_buf.buf, sw->index_buf.len);
	if (ret < (scar_offset)sw->index_buf.len) {
		SCAR_ERETURN(-1);
	}

	ret = w->write(w, sw->checkpoints_buf.buf, sw->checkpoints_buf.len);
	if (ret < (scar_offset)sw->checkpoints_buf.len) {
		SCAR_ERETURN(-1);
	}

	// We need one final compressor to write the tail
	struct scar_compressor *tail_compressor = sw->comp->create_compressor(w, sw->clevel);
	if (tail_compressor == NULL) {
		SCAR_ERETURN(-1);
	}

	ret = scar_io_printf(
		&tail_compressor->w, "SCAR-TAIL\n%lld\n%lld\n",
		index_compressed_offset, checkpoints_compressed_offset);
	if (ret < 0) {
		sw->comp->destroy_compressor(tail_compressor);
		SCAR_ERETURN(-1);
	}

	if (tail_compressor->finish(tail_compressor) < 0) {
		sw->comp->destroy_compressor(tail_compressor);
		SCAR_ERETURN(-1);
	}

	sw->comp->destroy_compressor(tail_compressor);

	ret = w->write(w, sw->comp->eof_marker, sw->comp->eof_marker_len);
	if (ret < (scar_offset)sw->comp->eof_marker_len) {
		SCAR_ERETURN(-1);
	}

	return 0;
}

void scar_writer_free(struct scar_writer *sw)
{
	sw->comp->destroy_compressor(sw->compressor);
	sw->comp->destroy_compressor(sw->index_compressor);
	sw->comp->destroy_compressor(sw->checkpoints_compressor);
	free(sw->index_buf.buf);
	free(sw->checkpoints_buf.buf);
	free(sw);
}
