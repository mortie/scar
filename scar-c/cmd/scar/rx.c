#include "rx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <scar/scar.h>

#include "util.h"

struct rx {
	pcre2_code *code;
	pcre2_match_data *match;
};

static void print_error(int err)
{
	unsigned char errbuf[64] = "<unknown>";
	pcre2_get_error_message(err, errbuf, sizeof(errbuf));
	fprintf(stderr, "%s", (char *)errbuf);
}

static unsigned char *build_rx_string(const char *pattern, enum rx_opts opts)
{
	struct scar_mem_writer mw = {0};
	scar_mem_writer_init(&mw);

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
		if (((char *)mw.buf)[mw.len - 1] == '/') {
			scar_io_puts(&mw.w, "[^\\/]*/?");
		} else {
			scar_io_puts(&mw.w, "(/[^\\/]*)?/?");
		}
	}

	if (opts & RX_MATCH_ALL_CHILDREN) {
		if (((char *)mw.buf)[mw.len - 1] == '/') {
			scar_io_puts(&mw.w, ".*");
		} else {
			scar_io_puts(&mw.w, "(/.*)?");
		}
	}

	scar_mem_writer_put(&mw, '\0');

	return mw.buf;
}

struct rx *rx_build(const char *pattern, enum rx_opts opts)
{
	struct rx *rx = malloc(sizeof(*rx));
	if (!rx) {
		SCAR_PERROR("malloc");
		return NULL;
	}

	unsigned char *rxstr = build_rx_string(pattern, opts);
	int err = 0;
	size_t erroffset = 0;
	rx->code = pcre2_compile(
		(unsigned char *)rxstr, PCRE2_ZERO_TERMINATED, 0,
		&err, &erroffset, NULL);

	if (!rx->code) {
		fprintf(stderr, "Failed to compile rx @ %zu: ", erroffset);
		print_error(err);
		fprintf(stderr, "\nRegex: %s\n", rxstr);
		free(rxstr);
		free(rx);
		return NULL;
	}

	free(rxstr);

	if ((err = pcre2_jit_compile(rx->code, PCRE2_JIT_COMPLETE)) < 0) {
		fprintf(stderr, "Failed to JIT compile: ");
		print_error(err);
		fprintf(stderr, "\n");
	}

	rx->match = pcre2_match_data_create(1, NULL);
	if (!rx->match) {
		fprintf(stderr, "Failed to create match data\n");
		pcre2_code_free(rx->code);
		free(rx);
		return NULL;
	}

	return rx;
}

bool rx_match(struct rx *rx, const char *str)
{
	int ret = pcre2_match(
		rx->code, (unsigned char *)str, PCRE2_ZERO_TERMINATED, 0,
		PCRE2_ANCHORED | PCRE2_ENDANCHORED, rx->match, NULL);

	if (ret < -1) {
		fprintf(stderr, "Match error: ");
		print_error(ret);
		fprintf(stderr, "\n");
	}

	return ret >= 0;
}

void rx_free(struct rx *rx)
{
	pcre2_code_free(rx->code);
	pcre2_match_data_free(rx->match);
}
