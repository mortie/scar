#define _POSIX_C_SOURCE 200112L

#include "../platform.h"

#include <unistd.h>

bool is_file_tty(FILE *f)
{
	return isatty(fileno(f));
}
