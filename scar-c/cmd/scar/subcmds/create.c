#include "../subcmds.h"

#include <stdio.h>
#include <stdlib.h>
#include <scar/scar.h>
#include <string.h>

#include "../platform.h"
#include "../util.h"

static int create_file(
	struct scar_writer *sw,
	struct scar_dir *dir,
	const char *path,
	struct scar_meta *meta
);

static int create_directory(
	struct scar_writer *sw,
	struct scar_dir *dir,
	const char *path,
	size_t pathlen,
	struct scar_meta *meta
) {
	int ret = 0;
	char **ents = NULL;
	char **it = NULL;
	char *childpath = NULL;
	size_t childpathsize = 0;
	struct scar_dir *subdir = NULL;

	ents = scar_dir_list(dir);
	if (!ents) {
		fprintf(stderr, "Failed to list contents of '%s'.\n", path);
		goto err;
	}

	for (it = ents; *it; ++it) {
		char *ent = *it;

		meta->path = NULL;
		scar_meta_destroy(meta);
		if (scar_stat_at(dir, ent, meta) < 0) {
			fprintf(stderr, "Failed to stat '%s%s'.\n", path, ent);
			goto err;
		}

		size_t required_bufsize = pathlen + strlen(ent) + 2;
		if (childpathsize < required_bufsize) {
			childpath = realloc(childpath, required_bufsize);
			if (!childpath) {
				SCAR_PERROR("realloc");
				goto err;
			}

			childpathsize = required_bufsize;
		}

		if (meta->type == SCAR_FT_DIRECTORY) {
			snprintf(childpath, childpathsize, "%s%s/", path, ent);
			subdir = scar_dir_open_at(dir, ent);
			if (!subdir) {
				fprintf(stderr, "Failed to open dir '%s'.\n", childpath);
				goto err;
			}
		} else {
			snprintf(childpath, childpathsize, "%s%s", path, ent);
		}

		if (create_file(sw, subdir, childpath, meta) < 0) {
			goto err;
		}

		free(ent);
		*it = NULL;

		if (subdir) {
			scar_dir_close(subdir);
			subdir = NULL;
		}
	}

exit:
	if (subdir) {
		scar_dir_close(subdir);
	}

	if (childpath) {
		free(childpath);
	}

	if (ents) {
		for (; *it; ++it) {
			free(*it);
		}
		free(ents);
	}

	return ret;

err:
	ret = -1;
	goto exit;
}

static int create_file(
	struct scar_writer *sw,
	struct scar_dir *dir,
	const char *path,
	struct scar_meta *meta
) {
	int ret = 0;
	struct scar_file_handle fh = {0};
	char *pathbuf = NULL;
	struct scar_dir *dirbuf = NULL;

	if (!dir && meta->type == SCAR_FT_DIRECTORY) {
		dirbuf = scar_dir_open(path);
		if (!dirbuf) {
			fprintf(stderr, "Failed to open directory '%s'\n", path);
			goto err;
		}

		dir = dirbuf;
	}

	size_t pathlen = strlen(path);

	if (path[0] != '/' && strncmp(path, "./", 2) != 0) {
		pathlen += 2;
		char *newpathbuf = realloc(pathbuf, pathlen + 1);
		if (!newpathbuf) {
			SCAR_PERROR("realloc");
			goto err;
		}
		pathbuf = newpathbuf;

		snprintf(pathbuf, pathlen + 1, "./%s", path);
		path = pathbuf;
	}

	if (meta->type == SCAR_FT_DIRECTORY && path[pathlen - 1] != '/') {
		pathlen += 1;
		char *newpathbuf = realloc(pathbuf, pathlen + 1);
		if (!newpathbuf) {
			SCAR_PERROR("realloc");
			goto err;
		}
		pathbuf = newpathbuf;

		snprintf(pathbuf, pathlen + 1, "%s/", path);
		path = pathbuf;
	}

	meta->path = (char *)path;
	if (meta->type == SCAR_FT_FILE) {
		FILE *f = fopen(meta->path, "rb");
		if (!f) {
			SCAR_PERROR(meta->path);
			goto err;
		}

		scar_file_handle_init(&fh, f);
	}

	if (scar_writer_write_entry(sw, meta, &fh.r) < 0) {
		fprintf(stderr, "Failed to write entry for '%s'\n", path);
		goto err;
	}

	if (meta->type == SCAR_FT_DIRECTORY) {
		if (create_directory(sw, dir, path, pathlen, meta) < 0) {
			goto err;
		}
	}

exit:
	if (dirbuf) {
		scar_dir_close(dirbuf);
	}

	if (fh.f) {
		fclose(fh.f);
	}

	if (pathbuf) {
		free(pathbuf);
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
	struct scar_meta meta = {0};

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

	for (int i = 0; i < argc; ++i) {
		meta.path = NULL;
		scar_meta_destroy(&meta);
		if (scar_stat(argv[i], &meta) < 0) {
			fprintf(stderr, "Failed to stat '%s'.\n", argv[i]);
			goto err;
		}

		if (create_file(sw, NULL, argv[i], &meta) < 0) {
			goto err;
		}
	}

	if (scar_writer_finish(sw) < 0) {
		fprintf(stderr, "Failed to finish\n");
		goto err;
	}

exit:
	if (sw) {
		scar_writer_free(sw);
	}

	meta.path = NULL;
	scar_meta_destroy(&meta);

	return ret;

err:
	ret = 1;
	goto exit;
}
