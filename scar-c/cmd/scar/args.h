#ifndef SCAR_CMD_ARGS_H
#define SCAR_CMD_ARGS_H

#include <stdbool.h>

#include <scar/scar.h>

struct args {
	struct scar_file_handle input;
	struct scar_file_handle output;
	struct scar_compression comp;
	char *chdir;
	int level;
	bool force;
};

#endif
