#include "../platform.h"

#include <fileapi.h>
#include <io.h>

bool is_file_tty(FILE *f)
{
	HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
	return GetFileType(h) == FILE_TYPE_CHAR;
}
