#include "../subcmds.h"

#include <stdio.h>

#include <scar/scar.h>

int cmd_tree(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct scar_reader *sr = NULL;
	struct scar_index_iterator *it = NULL;

	if (argc > 0) {
		fprintf(stderr, "Unexpected argument: '%s'\n", argv[0]);
		goto err;
	}

	sr = scar_reader_create(&args->input.r, &args->input.s);
	if (!sr) {
		fprintf(stderr, "Failed to create scar reader.\n");
		fprintf(stderr, "Is the file a scar archive?\n");
		goto err;
	}

	it = scar_reader_iterate(sr);
	if (it == NULL) {
		fprintf(stderr, "Failed to create index iterator\n");
		goto err;
	}

	struct scar_index_entry entry;
	while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
		fprintf(args->output.f, "%s\n", entry.name);
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
	return ret;
err:
	ret = 1;
	goto exit;
}
