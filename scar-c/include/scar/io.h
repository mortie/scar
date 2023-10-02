#ifndef SCAR_IO_H
#define SCAR_IO_H

#include "types.h"

enum scar_io_whence {
	SCAR_SEEK_START,
	SCAR_SEEK_CURRENT,
	SCAR_SEEK_END,
};

/// Abstract stream reader style type which allows reading data.
struct scar_io_reader {
	/// Read up to 'len' bytes into 'buf'.
	/// Return the number of bytes read, or -1 on error.
	scar_ssize (*read)(struct scar_io_reader *r, void *buf, size_t len);
};

/// Abstract stream writer style type which allows writing data.
struct scar_io_writer {
	/// Write up to 'len' bytes from 'buf'.
	/// Return the number of bytes written, or -1 on error.
	scar_ssize (*write)(struct scar_io_writer *w, const void *buf, size_t len);
};

/// Abstroct seeker style type which allows seeking to a different place in a stream.
struct scar_io_seeker {
	/// Seek to 'offset' relative to the position given by 'whence'.
	/// If 'whence' is SCAR_SEEK_START, 'offset' is relative to the start of the stream.
	/// If 'whence' is SCAR_SEEK_CURRENT' 'offset' is relative to the current position.
	/// If 'whence' is SCAR_SEEK_END, 'offset' is relative to the end of the stream.
	/// Return 0 on success, or -1 on error.
	int (*seek)(struct scar_io_seeker *s, scar_offset offset, enum scar_io_whence whence);

	/// Return the current offset from the start of the stream, or -1 on error.
	scar_offset (*tell)(struct scar_io_seeker *s);
};

#endif
