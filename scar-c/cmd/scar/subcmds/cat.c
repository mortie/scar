#include "../subcmds.h"

#include <stdio.h>

#include <scar/scar.h>

#include "../rx.h"

int cmd_cat(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct scar_reader *sr = NULL;
	struct scar_index_iterator *it = NULL;
	struct scar_meta meta = {0};

	if (argc == 0) {
		fprintf(stderr, "Expected at least 1 argument\n");
		goto err;
	}

	sr = scar_reader_create(&args->input.r, &args->input.s);
	if (!sr) {
		fprintf(stderr, "Failed to create scar reader.\n");
		fprintf(stderr, "Is the file a scar archive?\n");
		goto err;
	}

	for (int i = 0; i < argc; ++i) {
		struct rx *rx = rx_build(argv[i], 0);
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
			if (entry.ft != SCAR_FT_FILE) {
				continue;
			}

			if (!rx_match(rx, entry.name)) {
				continue;
			}

			scar_meta_destroy(&meta);
			if (scar_reader_read_meta(
				sr, entry.offset, entry.global, &meta) < 0
			) { 
				fprintf(stderr, "Failed to read '%s'\n", entry.name);
				continue;
			}

			if (scar_reader_read_content(sr, &args->output.w, meta.size) < 0) {
				fprintf(stderr, "Failed to read '%s'\n", entry.name);
				continue;
			}
		}

		rx_free(rx);

		if (ret < 0) {
			fprintf(stderr, "Failed to iterate index\n");
			goto err;
		}
	}

exit:
	scar_meta_destroy(&meta);
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
