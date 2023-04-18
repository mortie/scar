# scar-rs: The Scar reference implementation

The library in [scar/](scar) and the executable in [scar-bin/](scar-bin) implement utilities
for working with Scar archives.

## The `scar` command-line utility

The `scar` program has the following features:

* Convert a tar/pax archive to Scar
* List files from a Scar archive
* Read metadata from entries of the Scar archive

### Build and install

Run `cargo build --release` to compile the `scar` program.

```
$ git clone git@github.com:mortie/scar.git
$ cd scar/scar-rs
$ cargo build --release
```

This will put a statically linked `scar` program in `target/release/scar`.
Install it by copying it into `/usr/local/bin`:

```
$ sudo cp target/release/scar /usr/local/bin/
```

### Usage

```
Usage:
  scar [options] list
  scar [options] cat <paths...>
  scar [options] ls <paths...>
  scar [options] stat <paths...>
  scar [options] convert
Options:
  -i<path>: Input file (default: stdin for 'convert')
  -o<path>: Output file (default: stdout)
  -c<format>: Compression format (gzip, plain, auto) (default: auto)
```

For most subcommands, you must provide an input file (`-i`).
The exception is `convert`, which will read from stdin if no file is provided.

### Examples

Create a Scar archive for a directory: `tar c my-directory | scar convert -o archive.scar`

Convert an existing tar archive to a Scar archive: `scar convert -i archive.tar -o archive.scar`

Convert a tar.gz file to a Scar archive: `gunzip <archive.tar.gz | scar convert -o archive.scar`

List all the files in an archive: `scar -i archive.scar list`

Stat all the files in a particular folder: `scar -i archive.scar stat 'path/to/directory/*`

## The `scar` library

The [scar/](scar) directory contains a library which implements a Scar reader and writer
as well as tools for working with pax files.

There's no documentation yet, but you can look at [scar-bin/src/main.rs](scar-bin/src/main.rs)
to get some idea about how to use it.
