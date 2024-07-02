#ifndef SCAR_CMD_UTIL_H
#define SCAR_CMD_UTIL_H

#include <stdbool.h>
#include <regex.h>

struct regexes {
	regex_t *regexes;
	size_t count;
};

enum rx_opts {
	// Match the contents of directories specified by a pattern
	RX_MATCH_DIR_ENTRIES = 1 << 0,
};

int build_regex(regex_t *reg, const char *pattern, enum rx_opts opts);
int build_regexes(
	struct regexes *rxs, char **patterns, size_t count, enum rx_opts opts);
bool regexes_match(struct regexes *rxs, char *text);
void free_regexes(struct regexes *rxs);

#endif
