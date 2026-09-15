# ARCHIVE - tar and gzip

`lib/archive.jdb` reads and writes tar archives and gzip files, in memory
and on disk. Tar is read in ustar, GNU and pax form and written as ustar
with pax records (or GNU long-name entries) wherever a value does not fit
the header. A folder goes into a `.tar.gz` in one call, and an archive
comes out into a folder that no entry can leave. It builds on the natives
`CODEC.DEFLATE$`, `CODEC.INFLATE$`, `CODEC.CRC32`, `PACK$`/`UNPACK` and
`BINREADER$`/`BINWRITER`.

Stands in for: Python's `tarfile`, `gzip` and `shutil.make_archive` /
`shutil.unpack_archive`.

## Quick start

```basic
IMPORT ARCHIVE

' a release tarball: sorted entries under a top folder, fixed mtimes
DIM opts = {"mtime": 1757000000, "exclude": ["*.log", ".git"], "executable": ["bin/*"]}
ARCHIVE.CREATE("jdbasic-linux-x64.tar.gz", "dist", "jdbasic-linux-x64", opts)

' and back out, refusing names that would leave the folder
PRINT ARCHIVE.EXTRACT("jdbasic-linux-x64.tar.gz", "unpacked")

' in memory
DIM a = ARCHIVE.BLANK()
ARCHIVE.ADDDIR(a, "notes")
ARCHIVE.ADDFILE(a, "notes/hello.txt", "hello" + CHR$(10), {"mode": 420})
DIM tar$ = ARCHIVE.TAR$(a)
DIM b = ARCHIVE.READTAR(tar$)
PRINT ARCHIVE.NAME$(b, 1), ARCHIVE.DATA$(b, 1)
ARCHIVE.FREE(a)
ARCHIVE.FREE(b)

' gzip alone
DIM gz$ = ARCHIVE.GZIP$(report$, "report.csv", NOW_EPOCH())
PRINT ARCHIVE.GZINFO(gz$){"name"}
PRINT ARCHIVE.GUNZIP$(BINREADER$("server.log.gz"))
```

## API

Archives are handles; entries are numbered from 0 in archive order.

| Call | What it does |
|------|--------------|
| `BLANK()` | A new empty archive. |
| `ADDFILE(h, name$, data$, [opts])` | A file with any bytes. `opts`: `"mode"` (420, octal 644), `"mtime"` (seconds since 1970, default now), `"uid"`, `"gid"`, `"uname"`, `"gname"`. Backslashes become slashes; a leading `./` and a trailing slash are dropped. |
| `ADDDIR(h, name$, [opts])` | A folder entry, mode 493 (octal 755) by default. |
| `ADDLINK(h, name$, target$, [opts])` | A symbolic link, mode 511 (octal 777) by default. |
| `READTAR(tar$)` | An archive read from tar bytes: ustar, GNU (`L`/`K` long names, base-256 numbers) and pax (`x` and `g` records for path, linkpath, size, mtime, uid, gid, uname, gname). A wrong header checksum or cut-off data raises. |
| `TAR$(h, [format$])` | The tar bytes, padded to a 10240-byte record. `"pax"` (default): ustar headers, a name up to 255 characters split into prefix and name, a pax record for anything longer, non-ASCII, or past the octal fields. `"gnu"`: `././@LongLink` entries and base-256 numbers. `"ustar"`: raises when a value does not fit. |
| `ENTRYCOUNT(h)`, `NAMES(h)` | The number of entries; their names as an array. |
| `NAME$(h, i)`, `KIND$(h, i)` | Name (without a trailing slash); `"file"`, `"dir"`, `"symlink"`, `"hardlink"` or `"other"`. |
| `DATA$(h, i)`, `ENTRYSIZE(h, i)` | The content of a file entry and its length (`""` and 0 for other kinds). |
| `MODE(h, i)`, `MTIME(h, i)`, `UID(h, i)`, `GID(h, i)`, `UNAME$(h, i)`, `GNAME$(h, i)`, `LINK$(h, i)` | The header fields, after pax records are applied. |
| `FIND(h, name$)` | The position of an entry, or -1. |
| `FREE(h)` | Releases the archive and its data. |
| `GZIP$(data$, [name$], [mtime], [level])` | A gzip file with the name (optional) and mtime (0) in its header, level 0 to 9 (6). |
| `GUNZIP$(gz$)` | The content of every member, one after the other; zero bytes after the last member are ignored, a damaged member raises. |
| `GZINFO(gz$)` | A map with `"name"`, `"mtime"`, `"members"` and `"size"` (bytes inside). |
| `ISGZIP(data$)` | Whether the bytes start with the gzip magic. |
| `READFILE(path$)` | An archive read from a `.tar`, `.tar.gz` or `.tgz` file (gzip is detected by its magic bytes). |
| `WRITEFILE(h, path$, [format$])` | Writes the archive, gzip-compressed at level 9 when the name ends in `.gz` or `.tgz`; the number of bytes written. |
| `FROMFOLDER(folder$, [top$], [opts])` | An archive of a folder, entries sorted by path, under the top folder name `top$`. `opts`: `"exclude"` and `"executable"` (patterns with `*` and `?`, matched against the path inside the folder and the plain name), `"mtime"` (one time for every entry), `"uid"`, `"gid"`, `"uname"`, `"gname"`. |
| `CREATE(path$, folder$, [top$], [opts])` | `FROMFOLDER` and `WRITEFILE` in one call; the number of entries. |
| `EXTRACTTO(h, target$)` | Writes files and folders below `target$`; the number written. |
| `EXTRACT(path$, target$)` | `READFILE` and `EXTRACTTO` in one call. |
| `SKIPPED()` | The entries the last extraction did not write. |

## Notes

- Extraction checks every entry before it writes anything: an absolute
  path, a drive letter or a `:` in a name, a `..` segment, and a link
  whose target is absolute or climbs out of the folder all raise an
  error, and the target folder is not even created. Symbolic links and
  special files are then not created (`SKIPPED` counts them); a hard link
  gets a copy of the file it names.
- With a fixed `"mtime"` in the options, `CREATE` gives the same bytes on
  every run: entries are sorted, the owner fields are fixed and the gzip
  header carries mtime 0 and the inner tar name.
- The native `CODEC.INFLATE$` stops after the first gzip member.
  `GUNZIP$` finds member boundaries itself, so `cat a.gz b.gz` output and
  logs rotated by appending read to the end.
- Archives written here were checked with bsdtar (`tar.exe` in Windows'
  System32) and Python's `tarfile` and `gzip`, in pax, gnu and ustar form;
  the self test carries archives made by Python in all three formats and
  by bsdtar.
- Everything is held in memory: an archive of a few hundred megabytes
  needs that much and more while it is built.
- Windows folders have no Unix modes, so `FROMFOLDER` gives 755 to folders
  and to files matching `"executable"`, 644 to the rest.

## Tests and demo

- `tests/jdlibs/archive_selftest.jdb`
- `jdb/demos/jdlibs/archive_demo.jdb` (a release tarball from a folder with a top folder name and fixed mtimes, listed and extracted, and a gzip file with two members)
