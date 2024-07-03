#include "rx.h"

#include <stdio.h>
#include <stdlib.h>

#include <scar/scar.h>

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
