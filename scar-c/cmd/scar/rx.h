#ifndef SCAR_CMD_RX_H
#define SCAR_CMD_RX_H

#include <stdbool.h>

struct rx;

enum rx_opts {
	// Match the contents of directories specified by a pattern
	RX_MATCH_DIR_ENTRIES = 1 << 0,
};

struct rx *rx_build(const char *pattern, enum rx_opts opts);
bool rx_match(struct rx *rx, const char *str);
void rx_free(struct rx *rx);

#endif
