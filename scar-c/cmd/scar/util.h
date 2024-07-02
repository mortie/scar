#ifndef SCAR_CMD_UTIL_H
#define SCAR_CMD_UTIL_H

#include <scar/ioutil.h>
#include <stdbool.h>
#include <regex.h>

int open_ifile(struct scar_file_handle *sf, char *path);
int open_ofile(struct scar_file_handle *sf, char *path);
void close_file(struct scar_file_handle *sf);

char *next_arg(char ***argv, int *argc);

struct regexes {
	regex_t *regexes;
	size_t count;
};

int build_regex(regex_t *reg, const char *pattern);
int build_regexes(struct regexes *rxs, char **patterns, size_t count);
bool regexes_match(struct regexes *rxs, char *text);
void free_regexes(struct regexes *rxs);

#endif
