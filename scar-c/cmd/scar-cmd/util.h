#ifndef SCAR_CMD_UTIL_H
#define SCAR_CMD_UTIL_H

#include <scar/ioutil.h>

int open_ifile(struct scar_file_handle *sf, char *path);
int open_ofile(struct scar_file_handle *sf, char *path);
void close_file(struct scar_file_handle *sf);

int opt_arg(char ***argv, char *name, char **val);
int opt(char ***argv, char *name);
char *next_arg(char ***argv);
int opt_end(char ***argv);
int opt_none(char ***argv);

#endif
