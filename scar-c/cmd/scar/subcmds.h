#ifndef SCAR_CMD_SUBCMDS_H
#define SCAR_CMD_SUBCMDS_H

#include "args.h"

int cmd_ls(struct args *args, char **argv, int argc);
int cmd_cat(struct args *args, char **argv, int argc);
int cmd_tree(struct args *args, char **argv, int argc);
int cmd_convert(struct args *args, char **argv, int argc);

#endif
