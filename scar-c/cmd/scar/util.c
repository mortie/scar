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

int opt_arg(char ***argv, char *name, char **val)
{
	char *arg = **argv;
	if (strcmp(arg, name) == 0) {
		if ((*argv)[1] == NULL) {
			return 0;
		}

		*argv += 1;
		*val = **argv;
		return 1;
	}

	size_t namelen = strlen(name);
	if (strncmp(arg, name, namelen) == 0 && arg[namelen] == '=') {
		*val = arg + namelen + 1;
		return 1;
	}

	return 0;
}

int opt(char ***argv, char *name)
{
	if (strcmp(**argv, name) == 0) {
		*argv += 1;
		return 1;
	}

	return 0;
}

char *next_arg(char ***argv) {
	char *arg = **argv;
	if (arg == NULL) {
		return NULL;
	}

	*argv += 1;
	return arg;
}

int opt_end(char ***argv) {
	char *arg = **argv;
	if (strcmp(arg, "--") == 0) {
		*argv += 1;
		return 1;
	} else if (arg[0] != '-') {
		return 1;
	}

	return 0;
}

int opt_none(char ***argv) {
	if (!opt_end(argv)) {
		fprintf(stderr, "Unknown option: %s\n", **argv);
		return 0;
	}

	return 1;
}
