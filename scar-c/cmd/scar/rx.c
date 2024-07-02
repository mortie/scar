#include "rx.h"

#include <scar/ioutil.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

int build_regex(regex_t *reg, const char *pattern, enum rx_opts opts)
{
	struct scar_mem_writer mw = {0};
	scar_mem_writer_init(&mw);
	scar_io_puts(&mw.w, "^(\\./)?");

	const char *ptr = pattern;
	char ch;
	while ((ch = *(ptr++))) {
		if (
			ch == '\\' || ch == '.' || ch == '^' || ch == '$' ||
			ch == '[' || ch == ']' || ch == '(' || ch == ')' ||
			ch == '{' || ch == '}' || ch == '+' || ch == '?' || ch == '|'
		)  {
			scar_mem_writer_put(&mw, '\\');
			scar_mem_writer_put(&mw, ch);
		} else if (ch == '*' && *ptr == '*') {
			scar_io_puts(&mw.w, ".*");
			ptr += 1;
		} else if (ch == '*') {
			scar_io_puts(&mw.w, "[^/]*");
		} else {
			scar_mem_writer_put(&mw, ch);
		}
	}

	if (opts & RX_MATCH_DIR_ENTRIES) {
		scar_io_puts(&mw.w, "(/[^/]+)?/?");
	}

	scar_mem_writer_put(&mw, '$');
	scar_mem_writer_put(&mw, '\0');

	int err;
	if ((err = regcomp(reg, mw.buf, REG_EXTENDED) != 0)) {
		fprintf(stderr, "Invalid pattern: '%s'\n", pattern);
		fprintf(stderr, "  Compiled regex: %s\n", (char *)mw.buf);
		char errbuf[1024];
		regerror(err, reg, errbuf, sizeof(errbuf));
		fprintf(stderr, "  Error %d: %s\n", err, errbuf);
		free(mw.buf);
		return -1;
	}

	return 0;
}

int build_regexes(
	struct regexes *rxs, char **patterns, size_t count, enum rx_opts opts
) {
	rxs->regexes = malloc(count * sizeof(*rxs->regexes));
	rxs->count = 0;

	while (rxs->count < count) {
		if (
			build_regex(&rxs->regexes[rxs->count], patterns[rxs->count], opts) < 0
		) {
			free_regexes(rxs);
			return -1;
		}

		rxs->count += 1;
	}

	return 0;
}

bool regexes_match(struct regexes *rxs, char *text)
{
	for (size_t i = 0; i < rxs->count; ++i) {
		int ret = regexec(&rxs->regexes[i], text, 0, NULL, 0);
		if (ret == 0) {
			return true;
		} else if (ret != REG_NOMATCH) {
			fprintf(stderr, "Regex match: error %d\n", ret);
			return false;
		}
	}

	return false;
}

void free_regexes(struct regexes *rxs)
{
	for (size_t i = 0; i < rxs->count; ++i) {
		regfree(&rxs->regexes[i]);
	}
	free(rxs->regexes);
}
