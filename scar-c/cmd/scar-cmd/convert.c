#include <scar/pax.h>

#include "util.h"
#include "subcommands.h"

int cmd_convert(char **argv)
{
	int ret = 0;

	struct scar_pax_meta global = {0};
	struct scar_pax_meta meta = {0};

	struct scar_file_handle ifile = {0};
	struct scar_file_handle ofile = {0};

	if (!opt_none(&argv)) {
		return 1;
	}

	if (open_ifile(&ifile, next_arg(&argv)) < 0) {
		ret = 1;
		goto exit;
	}

	if (open_ofile(&ofile, next_arg(&argv)) < 0) {
		ret = 1;
		goto exit;
	}

	scar_pax_meta_init_empty(&global);
	while (1) {
		int r = scar_pax_read_meta(&global, &meta, &ifile.r);
		if (r < 0) {
			ret = 1;
			goto exit;
		} else if (r == 0) {
			break;
		}

		if (scar_pax_write_entry(&meta, &ifile.r, &ofile.w) < 0) {
			ret = 1;
			goto exit;
		}
	}

	if (scar_pax_write_end(&ofile.w) < 0) {
		ret = 1;
		goto exit;
	}

exit:
	close_file(&ifile);
	close_file(&ofile);
	scar_pax_meta_destroy(&meta);
	scar_pax_meta_destroy(&global);
	return ret;
}
