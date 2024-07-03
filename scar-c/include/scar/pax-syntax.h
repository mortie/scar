#ifndef SCAR_PAX_PARSER_H
#define SCAR_PAX_PARSER_H

#include "pax.h"

int scar_pax_parse(
	struct scar_pax_meta *meta, struct scar_io_reader *r, uint64_t size);

#endif
