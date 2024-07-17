#ifndef SCAR_CMD_PLATFORM_H
#define SCAR_CMD_PLATFORM_H

#include <stdbool.h>
#include <stdio.h>
#include <scar/scar.h>

struct scar_dir;

bool scar_is_file_tty(FILE *f);

struct scar_dir *scar_dir_open(const char *path);
struct scar_dir *scar_dir_open_at(struct scar_dir *dir, const char *name);
char **scar_dir_list(struct scar_dir *dir);
void scar_dir_close(struct scar_dir *dir);

int scar_stat(const char *path, struct scar_meta *meta);
int scar_stat_at(struct scar_dir *dir, const char *name, struct scar_meta *meta);

#endif
