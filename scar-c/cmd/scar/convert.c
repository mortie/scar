#include <scar/pax.h>
#include <scar/compression.h>
#include <scar/scar-writer.h>
#include <stdlib.h>

#include "util.h"
#include "subcommands.h"

int cmd_convert(struct args *args, char **argv, int argc)
{
	int ret = 0;

	struct scar_pax_meta global = {0};
	struct scar_pax_meta meta = {0};

	struct scar_writer *sw = NULL;

	if (argc > 0) {
		fprintf(stderr, "Unexpected argument: '%s'\n", argv[0]);
		ret = 1;
		goto exit;
	}

	sw = scar_writer_create(&args->output.w, &args->comp, args->level);
	if (sw == NULL) {
		fprintf(stderr, "Failed to create writer\n");
		ret = 1;
		goto exit;
	}

	scar_pax_meta_init_empty(&global);
	while (1) {
		int r = scar_pax_read_meta(&global, &meta, &args->input.r);
		if (r < 0) {
			ret = 1;
			goto exit;
		} else if (r == 0) {
			break;
		}

		if (scar_writer_write_entry(sw, &meta, &args->input.r) < 0) {
			fprintf(stderr, "Failed to write SCAR entry\n");
			ret = 1;
			goto exit;
		}
	}

	if (scar_writer_finish(sw) < 0) {
		fprintf(stderr, "Failed to finish SCAR archive\n");
		ret = 1;
		goto exit;
	}

exit:
	if (sw) {
		scar_writer_free(sw);
	}
	scar_pax_meta_destroy(&meta);
	scar_pax_meta_destroy(&global);
	return ret;
}
