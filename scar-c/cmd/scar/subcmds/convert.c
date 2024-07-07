#define _POSIX_C_SOURCE 200112L

#include "../subcmds.h"

#include <stdio.h>
#include <unistd.h>

#include <scar/scar.h>

int cmd_convert(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct scar_meta global;
	struct scar_meta meta = {0};
	struct scar_writer *sw = NULL;

	if (argc > 0) {
		fprintf(stderr, "Unexpected argument: '%s'\n", argv[0]);
		goto err;
	}

	if (isatty(fileno(args->output.f)) && !args->force) {
		fprintf(stderr, "Refusing to write to a TTY.\n");
		fprintf(stderr, "Re-run with '--force' to ignore this check.\n");
		goto err;
	}

	sw = scar_writer_create(&args->output.w, &args->comp, args->level);
	if (sw == NULL) {
		fprintf(stderr, "Failed to create writer\n");
		goto err;
	}

	scar_meta_init_empty(&global);
	while (1) {
		int r = scar_pax_read_meta(&args->input.r, &global, &meta);
		if (r < 0) {
			goto err;
		} else if (r == 0) {
			break;
		}

		if (scar_writer_write_entry(sw, &meta, &args->input.r) < 0) {
			fprintf(stderr, "Failed to write SCAR entry\n");
			goto err;
		}

		scar_meta_destroy(&meta);
	}

	if (scar_writer_finish(sw) < 0) {
		fprintf(stderr, "Failed to finish SCAR archive\n");
		goto err;
	}

exit:
	if (sw) {
		scar_writer_free(sw);
	}
	scar_meta_destroy(&meta);
	scar_meta_destroy(&global);
	return ret;
err:
	ret = 1;
	goto exit;
}
