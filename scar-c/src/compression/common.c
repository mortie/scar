#include "compression.h"

#include <string.h>

int scar_compression_init_from_name(const char *name, struct scar_compression *comp)
{
	if (strcmp(name, "gzip") == 0) {
		scar_compression_init_gzip(comp);
		return 1;
	}

	return 0;
}
