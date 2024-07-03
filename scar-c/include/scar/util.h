#ifndef SCAR_UTIL_H
#define SCAR_UTIL_H

#include <stddef.h>

/// Get a pointer to a struct from a pointer to one of its members.
/// Given code like this:
///
/// struct foobar {
///     int x;
///     int y;
/// };
/// struct foobar fb = { 10, 20 };
/// int *y = &fb.y;
///
/// You can turn the 'y' pointer into a pointer to its containing
/// 'struct foobar' like this:
///
/// struct foobar *fb = SCAR_BASE(struct foobar, y)
///
/// Note: the pointer name and field name must match.
#define SCAR_BASE(base, field) \
	((base *)((char *)field - offsetof(base, field)))

#endif
