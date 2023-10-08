#include <errno.h>
#include <scar/pax.h>
#include <scar/ioutil.h>
#include <string.h>

static void usage(char *argv0)
{
	printf("Usage: %s convert [in] [out]\n", argv0);
}

static int open_ifile(struct scar_file *sf, char *path)
{
	if (path == NULL || strcmp(path , "-") == 0) {
		scar_file_init(sf, stdin);
		return 0;
	}

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "Open %s: %s\n", path, strerror(errno));
		return -1;
	}

	scar_file_init(sf, f);
	return 0;
}

static int open_ofile(struct scar_file *sf, char *path)
{
	if (path == NULL || strcmp(path , "-") == 0) {
		scar_file_init(sf, stdout);
		return 0;
	}

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "Open %s: %s\n", path, strerror(errno));
		return -1;
	}

	scar_file_init(sf, f);
	return 0;
}

static void close_file(struct scar_file *sf)
{
	if (sf->f != NULL && sf->f != stdin && sf->f != stdout) {
		fclose(sf->f);
	}
}

static int opt_arg(char ***argv, char *name, char **val)
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

static int opt(char ***argv, char *name)
{
	if (strcmp(**argv, name) == 0) {
		*argv += 1;
		return 1;
	}

	return 0;
}

static char *next_arg(char ***argv) {
	char *arg = **argv;
	if (arg == NULL) {
		return NULL;
	}

	*argv += 1;
	return arg;
}

static int opt_end(char ***argv) {
	char *arg = **argv;
	if (strcmp(arg, "--") == 0) {
		*argv += 1;
		return 1;
	} else if (arg[0] != '-') {
		return 1;
	}

	return 0;
}

static int do_convert(char **argv) {
	int ret = 0;

	struct scar_pax_meta global = {0};
	struct scar_pax_meta meta = {0};

	struct scar_file ifile = {0};
	struct scar_file ofile = {0};

	while (*argv) {
		if (opt_end(&argv)) {
			break;
		} else {
			printf("Unknown option: %s\n", *argv);
			ret = 1;
			goto exit;
		}
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

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "convert") == 0) {
		return do_convert(argv + 2);
	} else {
		usage(argv[0]);
		return 1;
	}
}
