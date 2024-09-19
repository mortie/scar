#include "../subcmds.h"

#include <stdbool.h>
#include <stdlib.h>

#include <scar/scar.h>

#include "../rx.h"

int cmd_extract(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct rx **rxv = NULL;
	size_t rxc = 0;
	struct scar_reader *sr = NULL;
	struct scar_index_iterator *it = NULL;

	if (argc > 0) {
		rxv = malloc(argc * sizeof(struct rx *));
		if (!rxv) {
			fprintf(stderr, "a");
			goto err;
		}
	}

	for (int i = 0; i < argc; ++i) {
		rxv[rxc] = rx_build(argv[i], RX_MATCH_ALL_CHILDREN);
		if (!rxv[rxc]) {
			fprintf(stderr, "Failed to compile pattern: '%s'\n", argv[i]);
			goto err;
		}

		rxc += 1;
	}

	sr = scar_reader_create(&args->input.r, &args->input.s);
	if (!sr) {
		fprintf(stderr, "Failed to create scar reader.\n");
		fprintf(stderr, "Is the file a scar archive?\n");
		goto err;
	}

	it = scar_reader_iterate(sr);
	if (!it) {
		fprintf(stderr, "Failed to create index iterator.\n");
		goto err;
	}

	struct scar_index_entry entry;
	while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
		bool matches = rxc == 0;
		for (size_t i = 0; i < rxc; ++i) {
			if (rx_match(rxv[i], entry.name)) {
				matches = true;
				break;
			}
		}

		if (!matches) {
			continue;
		}

		// TODO: Actually extract
		fprintf(stderr, "I wanna extract %s\n", entry.name);
	}

	if (ret < 0) {
		fprintf(stderr, "Failed to iterate index\n");
		goto err;
	}

exit:
	if (it) {
		scar_index_iterator_free(it);
	}

	if (sr) {
		scar_reader_free(sr);
	}

	if (rxv) {
		while (rxc > 0) {
			rx_free(rxv[--rxc]);
		}
		free(rxv);
	}

	return ret;

err:
	ret = 1;
	fprintf(stderr, "err\n");
	goto exit;
}
