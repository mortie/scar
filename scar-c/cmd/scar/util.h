#ifndef SCAR_CMD_UTIL_H
#define SCAR_CMD_UTIL_H

#include <scar/ioutil.h>

int open_ifile(struct scar_file_handle *sf, char *path);
int open_ofile(struct scar_file_handle *sf, char *path);
void close_file(struct scar_file_handle *sf);

char *next_arg(char ***argv, int *argc);

#endif
