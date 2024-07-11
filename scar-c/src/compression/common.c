#include "compression.h"

#include <string.h>

bool scar_compression_init_from_name(
	struct scar_compression *comp, const char *name
) {
#define X(xname) if (strcmp(name, #xname) == 0) { \
	scar_compression_init_ ## xname(comp); \
	return true; \
}
	SCAR_COMPRESSOR_NAMES
#undef X

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
#define X(xname) do { \
	scar_compression_init_ ## xname(comp); \
	if (suffix_match( \
		buf, (size_t)len, comp->eof_marker, comp->eof_marker_len) \
	) { \
		return true; \
	} \
} while (0);
	SCAR_COMPRESSOR_NAMES
#undef X

	return false;
}
