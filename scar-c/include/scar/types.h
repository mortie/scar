#ifndef SCAR_TYPES_H
#define SCAR_TYPES_H

#include <stddef.h>

/// Type to indicate either a size (positive) or an error (-1).
/// Similar to POSIX's ssize_t.
typedef long long scar_ssize;

/// Type used to indicate an offset into a stream.
/// Similar to POSIX's off_t.
typedef long long scar_offset;

#endif
