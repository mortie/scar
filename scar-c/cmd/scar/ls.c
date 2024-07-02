#include <scar/pax.h>
#include <scar/scar-reader.h>
#include <stdbool.h>

#include "util.h"
#include "subcommands.h"

int cmd_ls(struct args *args, char **argv, int argc)
{
	int ret = 0;

	struct scar_reader *sr = NULL;
	struct scar_index_iterator *it = NULL;
	struct regexes rxs = {};

	sr = scar_reader_create(&args->input.r, &args->input.s);
	if (!sr) {
		fprintf(stderr, "Failed to create reader\n");
		ret = 1;
		goto exit;
	}

	it = scar_reader_iterate(sr);
	if (it == NULL) {
		fprintf(stderr, "Failed to create index iterator\n");
		ret = 1;
		goto exit;
	}

	char *default_pattern = ".";
	char **patterns;
	size_t patcount;
	if (argc == 0) {
		patterns = &default_pattern;
		patcount = 1;
	} else {
		patterns = argv;
		patcount = argc;
	}

	build_regexes(&rxs, patterns, patcount);

	struct scar_index_entry entry;
	while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
		if (regexes_match(&rxs, entry.name)) {
			fprintf(
				stderr, "%c: %s @ %lld\n",
				scar_pax_filetype_to_char(entry.ft),
				entry.name, entry.offset);
		}
	}

	if (ret < 0) {
		fprintf(stderr, "Failed to iterate index\n");
		ret = 1;
		goto exit;
	}

exit:
	free_regexes(&rxs);
	if (it) {
		scar_index_iterator_free(it);
	}
	if (sr) {
		scar_reader_free(sr);
	}
	return ret;
}
