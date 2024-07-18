#ifndef SCAR_CMD_UTIL_H
#define SCAR_CMD_UTIL_H

 // IWYU pragma: begin_keep
#include <stdio.h>
#include <errno.h>
#include <string.h>
 // IWYU pragma: end_keep

#define SCAR_PERROR(str) \
	fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, str, strerror(errno))

#endif
