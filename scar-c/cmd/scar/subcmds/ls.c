#include "../subcmds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <scar/scar.h>

#include "../rx.h"

static int do_print_roots(FILE *out, struct scar_reader *sr)
{
	int ret = 0;
	struct scar_index_iterator *it = scar_reader_iterate(sr);
	if (!it) {
		fprintf(stderr, "Failed to create index iterator\n");
		goto err;
	}

	char *prev_root = NULL;
	size_t prev_root_len = 0;

	struct scar_index_entry entry;
	while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
		if (
			!prev_root ||
			strncmp(prev_root, entry.name, prev_root_len) != 0
		) {
			free(prev_root);
			prev_root = strdup(entry.name);
			prev_root_len = strlen(prev_root);
			fprintf(out, "%s\n", entry.name);
		}
	}

exit:
	if (it) {
		scar_index_iterator_free(it);
	}

	return ret;

err:
	ret = 1;
	goto exit;
}

static int do_print_matching(
	FILE *out,
	struct scar_reader *sr,
	int patternc,
	char **patternv
) {
	int ret = 0;
	struct scar_index_iterator *it = NULL;

	for (int i = 0; i < patternc; ++i) {
		struct rx *rx = rx_build(patternv[i], RX_MATCH_DIR_ENTRIES);
		if (!rx) {
			goto err;
		}

		it = scar_reader_iterate(sr);
		if (it == NULL) {
			rx_free(rx);
			fprintf(stderr, "Failed to create index iterator\n");
			goto err;
		}

		struct scar_index_entry entry;
		while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
			if (!rx_match(rx, entry.name)) {
				continue;
			}

			fprintf(out, "%s\n", entry.name);
		}

		rx_free(rx);

		if (ret < 0) {
			fprintf(stderr, "Failed to iterate index\n");
			goto err;
		}
	}

exit:
	if (it) {
		scar_index_iterator_free(it);
	}

	return ret;

err:
	ret = 1;
	goto exit;
}

int cmd_ls(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct scar_reader *sr = NULL;

	sr = scar_reader_create(&args->input.r, &args->input.s);
	if (!sr) {
		fprintf(stderr, "Failed to create scar reader.\n");
		fprintf(stderr, "Is the file a scar archive?\n");
		goto err;
	}

	if (argc == 0) {
		ret = do_print_roots(args->output.f, sr);
	} else {
		ret = do_print_matching(args->output.f, sr, argc, argv);
	}

exit:
	if (sr) {
		scar_reader_free(sr);
	}

	return ret;

err:
	ret = 1;
	goto exit;
}
