#include <errno.h>
#include <scar/pax.h>
#include <scar/ioutil.h>
#include <scar/scar-reader.h>
#include <string.h>

#include "subcommands.h"

static void usage(char *argv0)
{
	printf("Usage: %s convert [in] [out]\n", argv0);
	printf("       %s list [in] [files...]\n", argv0);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "convert") == 0) {
		return cmd_convert(argv + 1, argc - 1);
	} else if (strcmp(argv[1], "list") == 0) {
		return cmd_list(argv + 1, argc - 1);
	} else {
		usage(argv[0]);
		return 1;
	}
}
