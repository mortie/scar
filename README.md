# Scar: Seekable compressed tar

Scar is an extension to the tar family of file formats which adds support for efficient seeking.
The format is intended to be compatible with existing tar implementations.

"Tar" and "the tar family of file formats" is used here to mean anything that's commonly called "tar",
including the original format from the 70s, the UStar format, the GNU tar format and the POSIX
pax format.
This Scar implementation tries to accept all variations as input.
However, an implementation which only accepts POSIX pax would also be reasonable.

## The format

This document describes the Scar format version 0.
It is subject to change at any time without notice and should not be relied upon.

Scar is implemented as an extra footer added after end of the tar archive.
A Scar file is made up of the tar body,
followed by the SCAR-INDEX section, the SCAR-CHUNKS section, and the SCAR-TAIL section.
All those parts are compressed using one of the supported compression algorithms.
The compressor is restarted in strategic locations to create "seek points".

### The tar body

The tar body is a normal tar archive. A Scar implementation _must_ support
[the pax Interchange Format as described in POSIX.1-2017](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html#tag_20_92_13_01),
and _may_ support compatible extensions.
This reference implementation supports the pax format and the GNU extensions.
The tar body includes the end of archive indicator, which is two 512-byte blocks of zeroes.

The implementation _may_ create a seek point before any header block.
The implementation _must not_ create a seek point at any location in the tar body other than right
before a header block.

### The SCAR-INDEX section

The SCAR-INDEX section starts with the text `SCAR-INDEX`, followed by a line feed character,
followed by 0 or more index entries.

An index entry starts with the length of the index entry as a base 10 number, followed by a space,
followed by a type flag (`0` for file, `1` for hard link, `5` for directory, etc; same as in the
UStar header), followed by a space, followed by the entry's offset into the (uncompressed) tar body,
followed by a path, followed by a newline
(`"%d %c %d %s\n", <entry length>, <typeflag>, <offset>, <path>`).

The typeflag `g` is also allowed. When `g` is used, the entry doesn't end with a path and a newline,
but instead in pax extended header records as specified in POSIX.1-2017
(`"%d g %d %s", <entry length>, <offset>, <pax extended header records>`).
The meaning of a `g` index entry is similar to the meaning of a `g` file entry in the main tar body
in that it affects all following entries.
The `g` entry type is intended to make it possible to turn a pax/tar archive into a tar archive
without changing anything about the tar body.

The other metadata entry types (pax's `x`, GNU's `L` and `K`) are not legal as typeflags
in index entries.

**Note:** The length of an index entry includes the length itself.

The implementation _must_ create `g` entries to correspond to the `g` file entries in the tar body,
such that all of a file's metadata can be found without scanning through the tar body.

The implementation _must_ create a seek point before the start of the SCAR-INDEX section.

If a file is preceded by metadata entries other than `g` (such as pax's `x`, GNU's `L` and `K`),
the file's index entry's offset _must_ point to the start of the earliest applicable metadata entry.

Here's an example of a SCAR-INDEX section:

```
SCAR-INDEX
20 5 512 ./somedir/
30 0 1024 ./somedir/hello.txt
83 g 2048 35 charset=ISO-IR 10646 2000 UTF-8
38 hdrcharset=ISO-IR 10646 2000 UTF-8
32 0 3072 ./somedir/goodbye.txt
```

This index shows a directory `./somedir/`, a file `./somedir/hello.txt`, a `g` entry which
sets the `charset` and `hdrcharset` of all subsequent entries, then a file `./somedir/goodbye.txt`.

### The SCAR-CHUNKS section

The SCAR-INDEX section starts with the text `SCAR-CHUNKS`, followed by a line feed character,
followed by 0 or more scar chunk entries.

A scar chunk entry is an offset into the compressed data where there's a seek point
as a base 10 number, followed by a space, followed by a base 10 number which indicates
which offset into the uncompressed tar body the seek point corresponds with,
followed by a line feed character.

Here's an example of a SCAR-CHUNKS section with 3 chunks:

```
SCAR-CHUNKS
104 512
2195 6656
6177 23552
```

That SCAR-CHUNKS section indicates that there's one seek point at offset 104 from the beginning of
the compressed file which corresponds with offset 512 of the uncompressed file,
one seek point at offset 2195 which corresponds with offset 6656 in the uncompressed file,
and one seek point at offset 6177 which corresponds with offset 23552 in the uncompressed file.

The implementation _must_ create a seek point right before the start of the SCAR-CHUNKS section.

The SCAR-CHUNKS section should contain all seek points, except for the one right before the SCAR-INDEX
section, the one right before the SCAR-CHUNKS section, and the one right before
the SCAR-TAIL section.

### The SCAR-TAIL section

The SCAR-TAIL section starts with the text `SCAR-TAIL`, followed by a line feed character,
followed by the offset into the compressed file where the seek point right before
the SCAR-INDEX section can be found as base 10, followed by a line feed character,
followed by the offset into the compressed file where the seek point right before the SCAR-CHUNKS
section can be found as base 10, followed by a newline.

Here's an example of a SCAR-TAIL section:

```
SCAR-TAIL
6625
6858
```

That SCAR-TAIL section indicates that the SCAR-INDEX section can be found by seeking to offset 6625
in the compressed file, and the SCAR-CHUNKS section can be found by seeking to offset 6858
in the compressed file.

The implementation _must_ create a seek point right before the start of the SCAR-TAIL section.
The seek point starts with some magic bytes defined by the compression format;
the implementation must avoid producing those magic bytes within the compressed
data for the SCAR-TAIL section.
The compressed data for the SCAR-TAIL section must be contained within 512 bytes.

### Compression formats

The standard compression formats are:

* **gzip**: magic bytes: `1f 8b`
* **bzip2**: magic bytes: `42 5a 68`
* **xz**: magic bytes: `fd 37 7a 58 00`
* **zstd**: magic bytes: `28 b5 2f fd`

### Finding the SCAR-TAIL

Because of the desire to be compatible with the existing tar ecosystem, even the footer is compressed.
This makes it a bit hard to find the SCAR-TAIL section.
However, this algorithm can be used:

1. Read the last 512 bytes of the file.
2. Find the last occurrence of any magic bytes sequence.
3. Try to start decompressing from that point, using the compression algorithm which corresponds to the
   magic bytes.
4. If the decompressed data starts with `"SCAR-TAIL\n"`, you have found the SCAR-TAIL section.
   If not, try again with an earlier occurrence of a magic bytes sequence.
5. Repeat until you have found `"SCAR-TAIL\n"`.
