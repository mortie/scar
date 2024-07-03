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

	sr = scar_reader_create(&args->input.r, &args->input.s);
	if (!sr) {
		fprintf(stderr, "Failed to create reader\n");
		goto err;
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

	for (size_t i = 0; i < patcount; ++i) {
		regex_t rx;
		if (build_regex(&rx, patterns[i], RX_MATCH_DIR_ENTRIES) < 0) {
			goto err;
		}

		it = scar_reader_iterate(sr);
		if (it == NULL) {
			regfree(&rx);
			fprintf(stderr, "Failed to create index iterator\n");
			goto err;
		}

		struct scar_index_entry entry;
		while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
			if (regexec(&rx, entry.name, 0, NULL, 0) != 0) {
				continue;
			}

			fprintf(stderr, "%s\n", entry.name);
		}

		regfree(&rx);

		if (ret < 0) {
			fprintf(stderr, "Failed to iterate index\n");
			goto err;
		}
	}

exit:
	if (it) {
		scar_index_iterator_free(it);
	}
	if (sr) {
		scar_reader_free(sr);
	}
	return ret;
err:
	ret = 1;
	goto exit;
}
