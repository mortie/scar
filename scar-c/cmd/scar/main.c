#include <errno.h>
#include <scar/pax.h>
#include <scar/ioutil.h>
#include <scar/scar-reader.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "subcmds.h"
#include "args.h"

static void usage(FILE *f, char *argv0)
{
	fprintf(f, "Usage: %s [options] <command> [args...]\n", argv0);
	fprintf(f, "Commands:\n");
	fprintf(f, "  ls [files...]: List the contents of directories, "
		 "like the 'ls' command.\n");
	fprintf(f, "                 Defaults to showing the contents of the root directory.\n");
	fprintf(f, "  tree:          List all the files in the archive.\n");
	fprintf(f, "  convert:       Convert a tar/pax file to a scar file.\n");
	fprintf(f, "Options:\n");
	fprintf(f, "  -i,--in    <file>  Input file (default: stdin)\n");
	fprintf(f, "  -o,--out   <file>  Output file (default: stdout)\n");
	fprintf(f, "  -c,--comp  <gzip>  Compression algorithm (default: gzip)\n");
	fprintf(f, "  -l,--level <level> Compression level (default: 6)\n");
	fprintf(f, "  -f,--force         Perform the task even if sanity checks fail\n");
	fprintf(f, "                     (for example, write binary data to stdout)\n");
	fprintf(f, "  -h,--help          Show this help output\n");
}

static bool streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

int main(int argc, char **argv)
{
	char *argv0 = argv[0];
	int ret = 0;

	struct args args;
	scar_file_handle_init(&args.input, stdin);
	scar_file_handle_init(&args.output, stdout);
	scar_compression_init_gzip(&args.comp);
	args.level = 6;
	args.force = false;

	static struct option opts[] = {
		{"in",    required_argument, NULL, 'i'},
		{"out",   required_argument, NULL, 'o'},
		{"comp" , required_argument, NULL, 'c'},
		{"level", required_argument, NULL, 'l'},
		{"force", no_argument,       NULL, 'f'},
		{"help",  no_argument,       NULL, 'h'},
		{},
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "i:o:c:l:fh", opts, NULL)) != -1) {
		switch (ch) {
		case 'i':
			if (streq(optarg, "-")) {
				break;
			}

			args.input.f = fopen(optarg, "r");
			if (!args.input.f) {
				fprintf(stderr, "%s: %s\n", optarg, strerror(errno));
				ret = 1;
				goto exit;
			}
			break;
		case 'o':
			if (streq(optarg, "-")) {
				break;
			}

			args.output.f = fopen(optarg, "w");
			if (!args.output.f) {
				fprintf(stderr, "%s: %s\n", optarg, strerror(errno));
				ret = 1;
				goto exit;
			}
			break;
		case 'c':
			if (!scar_compression_init_from_name(&args.comp, optarg)) {
				fprintf(stderr, "%s: Unknown compression\n", optarg);
				ret = 1;
				goto exit;
			}
			break;
		case 'l':
			args.level = atoi(optarg);
			break;
		case 'f':
			args.force = true;
			break;
		case 'h':
			usage(stdout, argv0);
			goto exit;
		default:
			ret = 1;
			goto exit;
		}
	}

	argv += optind;
	argc -= optind;

	if (argc == 0) {
		usage(stderr, argv0);
		ret = 1;
		goto exit;
	}

	const char *subcmd = argv[0];
	argv += 1;
	argc -= 1;

	if (streq(subcmd, "ls") ) {
		ret = cmd_ls(&args, argv, argc);
	} else if (streq(subcmd, "tree")) {
		ret = cmd_tree(&args, argv, argc);
	} else if (streq(subcmd, "convert")) {
		ret = cmd_convert(&args, argv, argc);
	} else {
		fprintf(stderr, "Unknown subcommand: %s\n", subcmd);
		usage(stderr, argv0);
		ret = 1;
	}

exit:
	if (args.input.f != stdin) {
		fclose(args.input.f);
	}
	if (args.output.f != stdout) {
		fclose(args.output.f);
	}
	return ret;
}
