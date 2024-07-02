#include <scar/scar-reader.h>
#include <stdio.h>

#include "../subcmds.h"

int cmd_tree(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct scar_reader *sr = NULL;
	struct scar_index_iterator *it = NULL;

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

	struct scar_index_entry entry;
	while ((ret = scar_index_iterator_next(it, &entry)) > 0) {
		fprintf(stderr, "%s\n", entry.name);
	}

	if (ret < 0) {
		fprintf(stderr, "Failed to iterate index\n");
		ret = 1;
		goto exit;
	}

exit:
	if (it) {
		scar_index_iterator_free(it);
	}
	if (sr) {
		scar_reader_free(sr);
	}
	return ret;
}
