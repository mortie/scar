#ifndef SCAR_UTIL_H
#define SCAR_UTIL_H

#include <stddef.h>

#ifdef SCAR_TRACE_ERROR
#include <stdio.h>
#endif

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
/// You can turn the 'y' pointer into a pointer to its containing 'struct foobar' like this:
///
/// struct foobar *fb = SCAR_BASE(struct foobar, y)
///
/// Note: the pointer name and field name must match.
#define SCAR_BASE(base, field) \
	((base *)((char *)field - offsetof(base, field)))

#ifdef SCAR_TRACE_ERROR
#define SCAR_ERETURN(ret) do { \
	fprintf(stderr, "SCAR TRACE ERROR: %s:%d(%s)\n", __FILE__, __LINE__, __func__); \
	return (ret); \
} while (0)
#else
#define SCAR_ERETURN(ret) return (ret)
#endif

#endif
