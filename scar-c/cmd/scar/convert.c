#include <scar/pax.h>
#include <scar/compression.h>
#include <scar/scar-writer.h>
#include <getopt.h>
#include <stdlib.h>

#include "util.h"
#include "subcommands.h"

int cmd_convert(char **argv, int argc)
{
	int ret = 0;

	struct scar_pax_meta global = {0};
	struct scar_pax_meta meta = {0};

	struct scar_file_handle ifile = {0};
	struct scar_file_handle ofile = {0};

	struct scar_writer *sw = NULL;
	struct scar_compression comp = {0};
	const char *comp_name = "gzip";
	int comp_level = 6;

	static struct option opts[] = {
		{"comp", required_argument, NULL, 'c'},
		{"level", required_argument, NULL, 'l'},
		{},
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "c:", opts, NULL)) != -1) {
		switch (ch) {
		case 'c':
			comp_name = optarg;
			break;
		case 'l':
			comp_level = atoi(optarg);
			break;
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

	if (open_ofile(&ofile, next_arg(&argv, &argc)) < 0) {
		ret = 1;
		goto exit;
	}

	if (argc > 0) {
		fprintf(stderr, "Unexpected argument: '%s'\n", argv[0]);
		ret = 1;
		goto exit;
	}

	if (!scar_compression_init_from_name(comp_name, &comp)) {
		fprintf(stderr, "Unknown compression: '%s'\n", comp_name);
		ret = 1;
		goto exit;
	}

	sw = scar_writer_create(&ofile.w, &comp, comp_level);
	if (sw == NULL) {
		fprintf(stderr, "Failed to create writer\n");
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

		if (scar_writer_write_entry(sw, &meta, &ifile.r) < 0) {
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
	close_file(&ifile);
	close_file(&ofile);
	scar_pax_meta_destroy(&meta);
	scar_pax_meta_destroy(&global);
	return ret;
}
