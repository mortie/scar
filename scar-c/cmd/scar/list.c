#include <scar/pax.h>
#include <scar/scar-reader.h>

#include "util.h"
#include "subcommands.h"

int cmd_list(char **argv)
{
	int ret = 0;

	struct scar_file_handle ifile = {0};
	struct scar_reader *sr = NULL;

	if (!opt_none(&argv)) {
		return 1;
	}

	if (open_ifile(&ifile, next_arg(&argv)) < 0) {
		ret = 1;
		goto exit;
	}

	sr = scar_reader_create(&ifile.r, &ifile.s);
	if (!sr) {
		ret = 1;
		goto exit;
	}

exit:
	if (sr) {
		scar_reader_free(sr);
	}
	close_file(&ifile);

	return ret;
}
