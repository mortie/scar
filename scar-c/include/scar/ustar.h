#ifndef SCAR_USTAR_FIELDS_H
#define SCAR_USTAR_FIELDS_H

#include <stddef.h>

#include "io.h"

struct scar_ustar_field {
	size_t start;
	size_t length;
};

static const struct scar_ustar_field SCAR_UST_NAME = {0, 100};
static const struct scar_ustar_field SCAR_UST_MODE = {100, 8};
static const struct scar_ustar_field SCAR_UST_UID = {108, 8};
static const struct scar_ustar_field SCAR_UST_GID = {116, 8};
static const struct scar_ustar_field SCAR_UST_SIZE = {124, 12};
static const struct scar_ustar_field SCAR_UST_MTIME = {136, 12};
static const struct scar_ustar_field SCAR_UST_CHKSUM = {148, 8};
static const struct scar_ustar_field SCAR_UST_TYPEFLAG = {156, 1};
static const struct scar_ustar_field SCAR_UST_LINKNAME = {157, 100};
static const struct scar_ustar_field SCAR_UST_MAGIC = {257, 6};
static const struct scar_ustar_field SCAR_UST_VERSION = {263, 2};
static const struct scar_ustar_field SCAR_UST_UNAME = {265, 32};
static const struct scar_ustar_field SCAR_UST_GNAME = {297, 32};
static const struct scar_ustar_field SCAR_UST_DEVMAJOR = {329, 8};
static const struct scar_ustar_field SCAR_UST_DEVMINOR = {337, 8};
static const struct scar_ustar_field SCAR_UST_PREFIX = {345, 155};

#endif
