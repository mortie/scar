#ifndef SCAR_CMD_ARGS_H
#define SCAR_CMD_ARGS_H

#include <stdbool.h>

#include <scar/ioutil.h>
#include <scar/compression.h>

struct args {
	struct scar_file_handle input;
	struct scar_file_handle output;
	struct scar_compression comp;
	int level;
	bool force;
};

#endif
