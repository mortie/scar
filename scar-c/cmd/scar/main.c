#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <scar/scar.h>

#include "subcmds.h"
#include "args.h"

static const char *usageText =
	"Usage: %s [options] <command> [args...]\n"
	"\n"
	"Commands:\n"
	"  ls [files...]     List the contents of directories in the archive.\n"
	"  cat <files...>    Read the contents of files in the archive.\n"
	"  tree              List all the entries in the archive.\n"
	"  create <files...> Create a new scar archive.\n"
	"  convert           Convert a tar/pax file to a scar file.\n"
	"\n"
	"Options:\n"
	"  -i,--in        <file>  Input file (default: stdin)\n"
	"  -o,--out       <file>  Output file (default: stdout)\n"
	"  -c,--comp      <gzip>  Compression algorithm (default: gzip)\n"
	"  -l,--level     <level> Compression level (default: 6)\n"
	"  -C,--directory <path>  Create/extract archive relative to <path>\n"
	"                         (does not affect -i/-o)\n"
	"  -f,--force             Perform the task even if sanity checks fail\n"
	"                         (for example, write binary data to stdout)\n"
	"  -h,--help              Show this help output\n";

static void usage(FILE *f, char *argv0)
{
	fprintf(f, usageText, argv0);
}

static bool streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

static char *dupstr(const char *str)
{
	size_t len = strlen(str);
	char *buf = malloc(len + 1);
	if (!buf) {
		perror("malloc");
		return NULL;
	}

	memcpy(buf, str, len + 1);
	return buf;
}

int main(int argc, char **argv)
{
	char *argv0 = argv[0];
	int ret = 0;

	struct args args;
	scar_file_handle_init(&args.input, stdin);
	scar_file_handle_init(&args.output, stdout);
	scar_compression_init_gzip(&args.comp);
	args.chdir = NULL;
	args.level = 6;
	args.force = false;

	static struct option opts[] = {
		{"in",        required_argument, NULL, 'i'},
		{"out",       required_argument, NULL, 'o'},
		{"comp" ,     required_argument, NULL, 'c'},
		{"level",     required_argument, NULL, 'l'},
		{"directory", required_argument, NULL, 'C'},
		{"force",     no_argument,       NULL, 'f'},
		{"help",      no_argument,       NULL, 'h'},
		{0},
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "i:o:c:l:C:fh", opts, NULL)) != -1) {
		switch (ch) {
		case 'i':
			if (streq(optarg, "-")) {
				break;
			}

			args.input.f = fopen(optarg, "rb");
			if (!args.input.f) {
				fprintf(stderr, "%s: %s\n", optarg, strerror(errno));
				goto err;
			}
			break;
		case 'o':
			if (streq(optarg, "-")) {
				break;
			}

			args.output.f = fopen(optarg, "wb");
			if (!args.output.f) {
				fprintf(stderr, "%s: %s\n", optarg, strerror(errno));
				goto err;
			}
			break;
		case 'c':
			if (!scar_compression_init_from_name(&args.comp, optarg)) {
				fprintf(stderr, "%s: Unknown compression\n", optarg);
				goto err;
			}
			break;
		case 'l':
			args.level = atoi(optarg);
			break;
		case 'C':
			args.chdir = dupstr(optarg);
			if (!args.chdir) {
				goto err;
			}
			break;
		case 'f':
			args.force = true;
			break;
		case 'h':
			usage(stdout, argv0);
			goto exit;
		default:
			goto err;
		}
	}

	argv += optind;
	argc -= optind;

	if (argc < 1) {
		usage(stderr, argv0);
		goto err;
	}

	const char *subcmd = argv[0];
	argv += 1;
	argc -= 1;

	if (streq(subcmd, "ls") ) {
		ret = cmd_ls(&args, argv, argc);
	} else if (streq(subcmd, "cat")) {
		ret = cmd_cat(&args, argv, argc);
	} else if (streq(subcmd, "tree")) {
		ret = cmd_tree(&args, argv, argc);
	} else if (streq(subcmd, "create")) {
		ret = cmd_create(&args, argv, argc);
	} else if (streq(subcmd, "convert")) {
		ret = cmd_convert(&args, argv, argc);
	} else {
		fprintf(stderr, "Unknown subcommand: %s\n", subcmd);
		usage(stderr, argv0);
		ret = 1;
	}

exit:
	if (args.input.f && args.input.f != stdin) {
		fclose(args.input.f);
	}
	if (args.output.f && args.output.f != stdout) {
		fclose(args.output.f);
	}
	return ret;
err:
	ret = 1;
	goto exit;
}
