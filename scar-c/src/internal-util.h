#ifndef SCAR_INTERNAL_UTIL
#define SCAR_INTERNAL_UTIL

#include <stddef.h>
#include <stdio.h>

static inline size_t log10_ceil(size_t num)
{
	size_t lg = 0;
	while (num > 0) {
		lg += 1;
		num /= 10;
	}

	return lg;
}

// The standard ftell/fseek is 32 bit on common platforms.
// These SCAR_FSEEK/SCAR_FTELL macros will use 64-bit compatible variants.
#ifdef _WIN32
#define SCAR_FSEEK(f, o, w) _fseeki64(f, (__int64)(o), w)
#define SCAR_FTELL(f) ((long long)_ftelli64(f))
#else
#define SCAR_FSEEK(f, o, w) fseeko(f, (off_t)(o), w)
#define SCAR_FTELL(f) ((long long)ftello(f))
#endif

#ifdef SCAR_TRACE_ERROR
#define SCAR_ELOG() do { \
	fprintf( \
		stderr, "SCAR TRACE ERROR: %s:%d(%s)\n", \
		__FILE__, __LINE__, __func__); \
} while (0)
#define SCAR_ERETURN(ret) do { \
	SCAR_ELOG(); \
	return (ret); \
} while (0)
#else
#define SCAR_ELOG() (void)0
#define SCAR_ERETURN(ret) return (ret)
#endif

#endif
