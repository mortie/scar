#include "compression.h"

#include <string.h>

bool scar_compression_init_from_name(
	struct scar_compression *comp, const char *name
) {
	if (strcmp(name, "gzip") == 0) {
		scar_compression_init_gzip(comp);
		return true;
	}

	return false;
}

static int suffix_match(
		const void *heystack, size_t heystack_len,
		const void *needle, size_t needle_len)
{
	if (needle_len > heystack_len) {
		return 0;
	}

	unsigned char *heystack_ptr =
		(unsigned char *)heystack + (heystack_len - needle_len);
	return memcmp(heystack_ptr, needle, needle_len) == 0;
}

bool scar_compression_init_from_tail(
	struct scar_compression *comp, void *buf, size_t len
) {
	scar_compression_init_gzip(comp);
	if (suffix_match(
		buf, (size_t)len, comp->eof_marker, comp->eof_marker_len)
	) {
		return true;
	}

	return false;
}
