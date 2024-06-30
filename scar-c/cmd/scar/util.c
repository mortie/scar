#include "util.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

int open_ifile(struct scar_file_handle *sf, char *path)
{
	if (path == NULL || strcmp(path , "-") == 0) {
		scar_file_handle_init(sf, stdin);
		return 0;
	}

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "Open %s: %s\n", path, strerror(errno));
		return -1;
	}

	scar_file_handle_init(sf, f);
	return 0;
}

int open_ofile(struct scar_file_handle *sf, char *path)
{
	if (path == NULL || strcmp(path , "-") == 0) {
		scar_file_handle_init(sf, stdout);
		return 0;
	}

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "Open %s: %s\n", path, strerror(errno));
		return -1;
	}

	scar_file_handle_init(sf, f);
	return 0;
}

void close_file(struct scar_file_handle *sf)
{
	if (sf->f != NULL && sf->f != stdin && sf->f != stdout) {
		fclose(sf->f);
	}
}

char *next_arg(char ***argv, int *argc)
{
	if (*argc <= 0) {
		return NULL;
	}

	char *arg = **argv;
	*argv += 1;
	*argc -= 1;
	return arg;
}
