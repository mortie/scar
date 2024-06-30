#include <scar/pax.h>
#include <scar/scar-reader.h>
#include <getopt.h>

#include "util.h"
#include "subcommands.h"

int cmd_list(char **argv, int argc)
{
	int ret = 0;

	struct scar_file_handle ifile = {0};
	struct scar_reader *sr = NULL;

	static struct option opts[] = {
		{"comp", required_argument, NULL, 'c'},
		{},
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "", opts, NULL)) != -1) {
		switch (ch) {
		default:
			fprintf(stderr, "Unknown option: %s\n", optarg);
			return 1;
		}
	}

	argv += optind;
	argc -= optind;

	if (open_ifile(&ifile, next_arg(&argv, &argc)) < 0) {
		ret = 1;
		goto exit;
	}

	if (argc > 0) {
		fprintf(stderr, "Unexpected argument: '%s'\n", argv[0]);
		ret = 1;
		goto exit;
	}

	sr = scar_reader_create(&ifile.r, &ifile.s);
	if (!sr) {
		fprintf(stderr, "Failed to create reader\n");
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
