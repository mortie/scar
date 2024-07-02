#include <scar/pax.h>
#include <scar/scar-reader.h>
#include <stdio.h>

#include "../rx.h"
#include "../subcmds.h"

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

	build_regexes(&rxs, patterns, patcount, RX_MATCH_DIR_ENTRIES);

	struct scar_index_entry entry;
	while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
		if (!regexes_match(&rxs, entry.name)) {
			continue;
		}

		fprintf(stderr, "%s\n", entry.name);
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
