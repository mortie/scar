#define _POSIX_C_SOURCE 200809L

// For major/minor.
// This is necessary for block device support.
#define _DEFAULT_SOURCE

// On glibc <2.10, _POSIX_C_SOURCE isn't enough to get the *at functions,
// but they exist if _ATFILE_SOURCE is defined
#define _ATFILE_SOURCE

#include "../platform.h"

// On Linux, minor/major is in sys/sysmacros.h,
// while on the BSDs those macros are in sys/types.h
#ifdef __linux__
#include <sys/sysmacros.h>
#endif

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include "../util.h"

static int sort_alpha(const void *aptr, const void *bptr)
{
	char *const *a = aptr;
	char *const *b = bptr;
	return strcmp(*a, *b);
}

static char *read_symlink_at(int fd, const char *name, off_t size)
{
	char *buf = NULL;

	while (true) {
		char *newbuf = realloc(buf, size + 1);
		if (!newbuf) {
			SCAR_PERROR("realloc");
			free(buf);
			return NULL;
		}
		buf = newbuf;

		ssize_t actual_size = readlinkat(fd, name, buf, size + 1);
		if (actual_size < 0) {
			SCAR_PERROR2("readlinkat", name);
			free(buf);
			return NULL;
		} else if (actual_size <= size) {
			buf[actual_size] = '\0';
			return buf;
		}

		struct stat st;
		if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
			SCAR_PERROR2("fstatat", name);
			free(buf);
			return NULL;
		}

		if ((st.st_mode & S_IFMT) != S_IFLNK) {
			fprintf(
				stderr, "Symlink changed to non-symlink during readlink\n");
			free(buf);
			return NULL;
		}

		size = st.st_size;
	}
}

bool scar_is_file_tty(FILE *f)
{
	return isatty(fileno(f));
}

struct scar_dir *scar_dir_open(const char *path)
{
	int dirfd = open(path, O_RDONLY);
	if (dirfd < 0) {
		SCAR_PERROR(path);
		return NULL;
	}

	return (struct scar_dir *)(intptr_t)dirfd;
}

struct scar_dir *scar_dir_open_at(struct scar_dir *dir, const char *name)
{
	int dirfd = (int)(intptr_t)dir;
	int subfd = openat(dirfd, name, O_RDONLY);
	if (subfd < 0) {
		SCAR_PERROR2("openat", name);
		return NULL;
	}

	return (struct scar_dir *)(intptr_t)subfd;
}

struct scar_dir *scar_dir_open_cwd(void)
{
	int dirfd = AT_FDCWD;
	return (struct scar_dir *)(intptr_t)dirfd;
}

char **scar_dir_list(struct scar_dir *dir)
{
	int dirfd = (int)(intptr_t)dir;
	DIR *dirp = NULL;
	char **names = NULL;
	size_t count = 0;

	int dupfd = dup(dirfd);
	if (dupfd < 0) {
		SCAR_PERROR("dup");
		goto err;
	}

	dirp = fdopendir(dupfd);
	if (!dirp) {
		close(dupfd);
		SCAR_PERROR("fdopendir");
		goto err;
	}

	while (true) {
		errno = 0;
		struct dirent *ent = readdir(dirp);
		if (ent == NULL && errno) {
			SCAR_PERROR("readdir");
			goto err;
		} else if (ent == NULL) {
			break;
		} else if (
			strcmp(ent->d_name, ".") == 0 ||
			strcmp(ent->d_name, "..") == 0
		) {
			continue;
		}

		count += 1;
		names = realloc(names, count * sizeof(*names));
		names[count - 1] = strdup(ent->d_name);
	}

	qsort(names, count, sizeof(*names), sort_alpha);

	// Add sentinel null value
	count += 1;
	names = realloc(names, count * sizeof(*names));
	names[count - 1] = NULL;

exit:
	if (dirp) {
		closedir(dirp);
	}

	return names;

err:
	while (count--) {
		free(names[count]);
	}

	if (names) {
		free(names);
		names = NULL;
	}

	goto exit;
}

void scar_dir_close(struct scar_dir *dir)
{
	int dirfd = (int)(intptr_t)dir;
	if (dirfd != AT_FDCWD) {
		close(dirfd);
	}
}

FILE *scar_open_at(struct scar_dir *dir, const char *name)
{
	int dirfd = (int)(intptr_t)dir;
	int fd = openat(dirfd, name, O_RDONLY);
	if (fd < 0) {
		SCAR_PERROR2("openat", name);
		return NULL;
	}

	FILE *f = fdopen(fd, "r");
	if (!f) {
		SCAR_PERROR2("fdopen", name);
		return NULL;
	}

	return f;
}

int scar_stat(const char *path, struct scar_meta *meta)
{
	int dirfd = AT_FDCWD;
	struct scar_dir *dir = (struct scar_dir *)(intptr_t)dirfd;
	return scar_stat_at(dir, path, meta);
}

int scar_stat_at(struct scar_dir *dir, const char *name, struct scar_meta *meta)
{
	scar_meta_init_empty(meta);

	int fd = (int)(intptr_t)dir;

	struct stat st;
	if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
		SCAR_PERROR2("fstatat", name);
		return -1;
	}

	meta->mode = st.st_mode & 07777;
	meta->mtime = st.st_mtime;

	switch (st.st_mode & S_IFMT) {
#if !defined(major) || !defined(minor)
	case S_IFBLK:
		fprintf(stderr, "Unsupported file type: block device\n");
		return -1;
	case S_IFCHR:
		fprintf(stderr, "Unsupported file type: character device\n");
		return -1;
#else
	case S_IFBLK:
		meta->type = SCAR_FT_BLOCKDEV;
		meta->devmajor = major(st.st_dev);
		meta->devminor = minor(st.st_dev);
		break;
	case S_IFCHR:
		meta->type = SCAR_FT_CHARDEV;
		meta->devmajor = major(st.st_dev);
		meta->devminor = minor(st.st_dev);
		break;
#endif
	case S_IFIFO:
		meta->type = SCAR_FT_FIFO;
		break;
	case S_IFREG:
		meta->size = st.st_size;
		meta->type = SCAR_FT_FILE;
		break;
	case S_IFDIR:
		meta->type = SCAR_FT_DIRECTORY;
		break;
	case S_IFLNK:
		meta->type = SCAR_FT_SYMLINK;
		meta->linkpath = read_symlink_at(fd, name, st.st_size);
		meta->size = 0;
		break;
	case S_IFSOCK:
		fprintf(stderr, "Unsupported file type: socket\n");
		return -1;
		break;
	default:
		fprintf(stderr, "Unrecognized file type: %d\n", st.st_mode & S_IFMT);
		return -1;
	}

	return 0;
}
