#ifndef SCAR_CMD_UTIL_H
#define SCAR_CMD_UTIL_H

#include <stdbool.h>
#include <regex.h>

enum rx_opts {
	// Match the contents of directories specified by a pattern
	RX_MATCH_DIR_ENTRIES = 1 << 0,
};

int build_regex(regex_t *reg, const char *pattern, enum rx_opts opts);

#endif
