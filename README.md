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
followed by the SCAR-INDEX section, the SCAR-CHECKPOINTS section, and the SCAR-TAIL section.
All those parts are compressed using one of the supported compression algorithms.
The compressor is restarted in strategic locations to create "checkpoints".

### The tar body

The tar body is a normal tar archive. A Scar implementation _must_ support
[the pax Interchange Format as described in POSIX.1-2017](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html#tag_20_92_13_01),
and _may_ support compatible extensions.
This reference implementation supports the pax format and the GNU extensions.
The tar body includes the end of archive indicator, which is two 512-byte blocks of zeroes.

The implementation _may_ create a checkpoint before any header block.
The implementation _must not_ create a checkpoint at any location in the tar body other than right
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

The implementation _must_ create `g` index entries to correspond to the non-empty `g` file objects
in the tar body, such that all of a file's metadata can be found without scanning through the tar
body.

The implementation _must not_ create entries for empty `g` file objects  (i.e file objects with no
extended header records). This is to make sure that all index entries start on a new line.

The implementation _must_ create a checkpoint before the start of the SCAR-INDEX section.

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

### The SCAR-CHECKPOINTS section

The SCAR-INDEX section starts with the text `SCAR-CHECKPOINTS`, followed by a line feed character,
followed by 0 or more scar checkpoint entries.

A scar checkpoint entry is an offset into the compressed data where there's a checkpoint
as a base 10 number, followed by a space, followed by a base 10 number which indicates
which offset into the uncompressed tar body the checkpoint corresponds with,
followed by a line feed character.

Here's an example of a SCAR-CHECKPOINTS section with 3 checkpoints:

```
SCAR-CHECKPOINTS
104 512
2195 6656
6177 23552
```

That SCAR-CHECKPOINTS section indicates that there's one checkpoint at offset 104 from the beginning
of the compressed file which corresponds with offset 512 of the uncompressed file,
one checkpoint at offset 2195 which corresponds with offset 6656 in the uncompressed file,
and one checkpoint at offset 6177 which corresponds with offset 23552 in the uncompressed file.

The implementation _must_ create a checkpoint right before the start of the SCAR-CHECKPOINTS section.

The SCAR-CHECKPOINTS section should contain all checkpoints, except for the one right before the
SCAR-INDEX section, the one right before the SCAR-CHECKPOINTS section, and the one right before
the SCAR-TAIL section.

### The SCAR-TAIL section

The SCAR-TAIL section starts with the text `SCAR-TAIL`, followed by a line feed character,
followed by the offset into the compressed file where the checkpoint right before
the SCAR-INDEX section can be found as base 10, followed by a line feed character,
followed by the offset into the compressed file where the checkpoint right before the
SCAR-CHECKPOINTS section can be found as base 10, followed by a newline.

Here's an example of a SCAR-TAIL section:

```
SCAR-TAIL
6625
6858
```

That SCAR-TAIL section indicates that the SCAR-INDEX section can be found by seeking to offset 6625
in the compressed file, and the SCAR-CHECKPOINTS section can be found by seeking to offset 6858
in the compressed file.

The implementation _must_ create a checkpoint right before the start of the SCAR-TAIL section.

### The SCAR-EOF section

The SCAR-EOF section consists of just the text "SCAR-EOF\n". For each compression format,
Scar defines a single canonical way to encode "SCAR-EOF\n", which the scar archive _must_
end with. The canonical way to encode "SCAR-EOF\n" is called the "EOF marker".

The purpose of the SCAR-EOF section is to make it easy to determine the compression format
used by the Scar archive by looking at the EOF marker at the end of the file.
It is an error for a Scar archive to end with any other bytes than one of the EOF markers
listed below.

### Compression formats

The standard compression formats, as well as their magic bytes and EOF markers, are:

* **plain**: Magic bytes: `53 43 41 52 2d 54 41 49 4c 0a`, EOF marker:

```
53 43 41 52 2d 45 4f 46 0a
```

* **gzip**: Magic bytes: `1f 8b`, EOF marker:

```
1f 8b 08 00 00 00 00 00 02 03 0b 76 76 0c d2 75
f5 77 e3 02 00 f8 f3 55 01 09 00 00 00
```

* **bzip2**: Magic bytes: `42 5a 68`, EOF marker:

```
42 5a 68 39 31 41 59 26 53 59 6b f1 37 53 00 00
04 56 00 00 10 00 02 2b 00 98 00 20 00 31 06 4c
41 01 91 ea 3e 63 00 f1 77 24 53 85 09 06 bf 13
75 30
```

* **xz**: Magic bytes: `fd 37 7a 58 00`, EOF marker:

```
fd 37 7a 58 5a 00 00 04 e6 d6 b4 46 02 00 21 01
1c 00 00 00 10 cf 58 cc 01 00 08 53 43 41 52 2d
45 4f 46 0a 00 00 00 00 a2 8d f2 f6 3c cc 0f cb
00 01 21 09 6c 18 c5 d5 1f b6 f3 7d 01 00 00 00
00 04 59 5a
```

* **zstd**: Magic bytes: `28 b5 2f fd`, EOF marker:

```
28 b5 2f fd 04 58 49 00 00 53 43 41 52 2d 45 4f
46 0a 3a b2 49 61
```
