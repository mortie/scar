#ifndef SCAR_PAX_PARSER_H
#define SCAR_PAX_PARSER_H

#include <stdint.h>

struct scar_pax_meta;
struct scar_io_reader;
struct scar_block_reader;

/// Parse pax extended attribute syntax into 'meta'.
/// 'meta' is expected to be initialized.
int scar_pax_parse(
	struct scar_pax_meta *meta, struct scar_io_reader *r, uint64_t size);

#endif
