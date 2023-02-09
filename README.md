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

An index entry starts with the length of the index entry as a base 10 number, followed by a space
(`"%d ", <entry length>`), followed by some number of records in the same format as the
pax extended header records as specified in POSIX.1-2017.

A pax extended header record is the length of the record as a base 10 number, followed by a space,
followed by a keyword, followed by a '=' character, followed by a value, followed by a
line feed chracter (`"%d %s=%s\n", <length>, <keyword>, <value>`).

**Note:** The length of an index entry includes the length itself.
An empty index entry would look like `"2 "`, since it consists of two octets: the '2' and the space.
The length of a pax extended header record also includes the length itself.
A header entry with `path=foo` would look like `"12 path=foo\n"`, since it consists of 12 octets:
3 octets for the `12` and space, 8 octets for the `path=foo`, and 1 octet for the line feed.

The standard set of record types is those which are specified in the POSIX.1-2017,
plus `scar:offset` and `scar:path`.
The `scar:offset` record specifies at which offset into the (uncompressed) tar body the file can be found.
The rest of the record types are as specified in POSIX.1-2017.
The `scar:offset` and the `scar:path` records are mandatory, the rest are optional.
The implementation _must_ ignore records it doesn't recognize.

The implementation _must_ create a seek point before the start of the SCAR-INDEX section.

Here's an example of a SCAR-INDEX section with 2 files:

```
SCAR-INDEX
37 14 offset=512
20 path=./README.md
36 18 path=./scar.go
15 offset=2560
```

When extracting a file using the table, the implementation must seek to a continue point which represents
some point before or at the given **offset**, then read the tar body sequentially
as if all the index entry's records were present in an earlier global extended header record (type `g`),
until it finds a header block which doesn't represent metadata (meaning anything from `0` through `7`
or the binary `\0`).

The implementation must create the index entry such that extracting the file using the above algorithm
produces the same results as extracting the file using a normal linear pass through the tar body.

**Note:** That means, the implementation might find a given piece of metadata through
an earlier `g` or `x` record, an earlier non-standard metadata record (such as the GNU
`K` and `L` records), the record in the index entry, or the header block.

**Note:** An implementation should usually set the `offset` field to point to an earlier
metadata header (`g` or `x`) to reduce duplication.
However, it is also legal to write out all of the file's metadata as part of the file's index entry.

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
