#include "../subcmds.h"

#include <stdio.h>
#include <stdlib.h>
#include <scar/scar.h>
#include <string.h>

#include "../platform.h"
#include "../util.h"

static int create_directory_entry(
	struct scar_writer *sw,
	struct scar_dir *dir,
	const char *dirpath
);

static int ensure_path_format(struct scar_meta *meta, char **pathbuf)
{
	if (meta->type != SCAR_FT_DIRECTORY) {
		return 0;
	}

	size_t len = strlen(meta->path);
	if (meta->path[len - 1] == '/') {
		return -1;
	}

	*pathbuf = malloc(len + 2);
	if (!*pathbuf) {
		SCAR_PERROR("malloc");
		return -1;
	}

	snprintf(*pathbuf, len + 2, "%s/", meta->path);
	meta->path = *pathbuf;
	return 0;
}

static int create_entry(
	struct scar_writer *sw,
	struct scar_dir *dir, 
	const char *path,
	const char *name
) {
	int ret = 0;
	struct scar_meta meta = {0};
	struct scar_file_handle fh = {0};
	struct scar_dir *subdir = NULL;
	char *pathbuf = NULL;

	if (scar_stat_at(dir, name, &meta) < 0) {
		fprintf(stderr, "%s: Failed to stat file\n", path);
		goto err;
	}

	meta.path = (char *)path;
	if (ensure_path_format(&meta, &pathbuf) < 0) {
		goto err;
	}

	if (meta.type == SCAR_FT_FILE) {
		scar_file_handle_init(&fh, scar_open_at(dir, name));
		if (!fh.f) {
			fprintf(stderr, "%s: Failed to open\n", path);
			goto err;
		}
	}

	if (scar_writer_write_entry(sw, &meta, &fh.r) < 0) {
		fprintf(stderr, "%s: Failed to create entry\n", path);
		goto err;
	}

	if (meta.type == SCAR_FT_DIRECTORY) {
		subdir = scar_dir_open_at(dir, name);
		if (!subdir) {
			fprintf(stderr, "%s: Failed to open dir\n", path);
			goto err;
		}

		if (create_directory_entry(sw, subdir, meta.path) < 0) {
			fprintf(stderr, "%s: Failed to create dir\n", path);
			goto err;
		}
	}

exit:
	if (pathbuf) {
		free(pathbuf);
	}

	if (subdir) {
		scar_dir_close(subdir);
	}

	if (fh.f) {
		fclose(fh.f);
	}

	// We don't own meta.path, don't free it
	meta.path = NULL;
	scar_meta_destroy(&meta);

	return ret;

err:
	ret = -1;
	goto exit;
}

static int create_directory_entry(
	struct scar_writer *sw,
	struct scar_dir *dir,
	const char *dirpath
) {
	int ret = 0;
	char **ents = NULL;
	char *subpath = NULL;
	size_t subpathlen = 0;

	ents = scar_dir_list(dir);
	if (!ents) {
		goto err;
	}

	size_t dirpathlen = strlen(dirpath);
	for (char **it = ents; *it; ++it) {
		size_t len = dirpathlen + strlen(*it);
		if (len > subpathlen) {
			char *buf = realloc(subpath, len + 1);
			if (!buf) {
				SCAR_PERROR("realloc");
				goto err;
			}

			subpath = buf;
			subpathlen = len;
		}

		snprintf(subpath, len + 1, "%s%s", dirpath, *it);
		if (create_entry(sw, dir, subpath, *it) < 0) {
			fprintf(stderr, "%s: Failed to create entry\n", subpath);
			goto exit;
		}
	}

exit:
	if (subpath) {
		free(subpath);
	}

	if (ents) {
		for (char **it = ents; *it; ++it) {
			free(*it);
		}
		free(ents);
	}

	return ret;

err:
	ret = -1;
	goto exit;
}

int cmd_create(struct args *args, char **argv, int argc)
{
	int ret = 0;
	struct scar_writer *sw = NULL;
	struct scar_dir *dir = NULL;

	if (argc == 0) {
		fprintf(stderr, "Expected arguments\n");
		goto err;
	}

	if (scar_is_file_tty(args->output.f) && !args->force) {
		fprintf(stderr, "Refusing to write to a TTY.\n");
		fprintf(stderr, "Re-run with '--force' to ignore this check.\n");
		goto err;
	}

	sw = scar_writer_create(&args->output.w, &args->comp, args->level);
	if (sw == NULL) {
		fprintf(stderr, "Failed to create writer\n");
		goto err;
	}

	if (args->chdir) {
		dir = scar_dir_open(args->chdir);
	} else {
		dir = scar_dir_open_cwd();
	}

	for (int i = 0; i < argc; ++i) {
		const char *path = argv[i];
		while (*path && *path == '/') {
			fprintf(stderr, "Removing leading '/' from %s\n", path);
			path += 1;
		}

		if (create_entry(sw, dir, path, argv[i]) < 0) {
			goto err;
		}
	}

	if (scar_writer_finish(sw) < 0) {
		fprintf(stderr, "Failed to finish\n");
		goto err;
	}

exit:
	if (dir) {
		scar_dir_close(dir);
	}

	if (sw) {
		scar_writer_free(sw);
	}

	return ret;

err:
	ret = 1;
	goto exit;
}
