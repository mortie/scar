#ifndef SCAR_CMD_SUBCOMMANDS_H
#define SCAR_CMD_SUBCOMMANDS_H

#include "args.h"

int cmd_ls(struct args *args, char **argv, int argc);
int cmd_convert(struct args *args, char **argv, int argc);

#endif
