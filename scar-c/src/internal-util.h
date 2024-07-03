#ifndef SCAR_INTERNAL_UTIL
#define SCAR_INTERNAL_UTIL

#include <stddef.h>

#ifdef SCAR_TRACE_ERROR
#include <stdio.h>
#endif

static inline size_t log10_ceil(size_t num)
{
	size_t lg = 0;
	while (num > 0) {
		lg += 1;
		num /= 10;
	}

	return lg;
}

#ifdef SCAR_TRACE_ERROR
#define SCAR_ERETURN(ret) do { \
	fprintf( \
		stderr, "SCAR TRACE ERROR: %s:%d(%s)\n", \
		__FILE__, __LINE__, __func__); \
	return (ret); \
} while (0)
#else
#define SCAR_ERETURN(ret) return (ret)
#endif

#endif
