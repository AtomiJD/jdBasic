# PKG - install modules, versions and a lock file

`lib/pkg.jdb` installs jdBasic modules the way pip installs packages: from
an index of names, versions and sources into a lib folder, together with
the modules they require, chosen by semver ranges. It lists what is
installed, removes and updates it, and writes a lock file that gives a
project the same versions on every checkout. A package is jdBasic source,
so there is nothing to build.

Stands in for: pip, requirements.txt, pip-tools' lock files.

## Quick start

```basic
IMPORT PKG

DIM got = PKG.INSTALL("lib", "TESTKIT, LLMAPI")     ' into ~/.jdbasic/lib
PRINT JSON.STRINGIFY$(got)                         ' TESTKIT, LLMAPI, REQ, SCHEMA with versions

PRINT PKG.SATISFIES("1.4.2", "^1.2")               ' TRUE
PRINT PKG.BEST$(["1.0.0", "1.2.5", "2.0.0"], "~1.2") ' 1.2.5

PKG.SYNC("lib", "myproject")                       ' myproject/modules and jdbasic.lock
```

From the command line:

```
jdBasic jdb/tools/jdpkg.jdb install TESTKIT LLMAPI
jdBasic jdb/tools/jdpkg.jdb install "LLMAPI ^1" --index https://example.com/jdbasic/index.json
jdBasic jdb/tools/jdpkg.jdb install https://github.com/someone/somemodule.git
jdBasic jdb/tools/jdpkg.jdb list
jdBasic jdb/tools/jdpkg.jdb remove TESTKIT
jdBasic jdb/tools/jdpkg.jdb update
jdBasic jdb/tools/jdpkg.jdb sync myproject
jdBasic jdb/tools/jdpkg.jdb index lib
```

`--index` defaults to `JDBASIC_INDEX`, else the repository's `lib` folder;
`--target` defaults to `~/.jdbasic/lib`.

## Where installed modules go

`IMPORT` searches, in order: the script's folder and its `modules`
subfolder, the current folder and its `modules`, every folder in
`JDBASIC_PATH`, `<home>/.jdbasic/lib`, and the `lib` folder next to the
jdBasic executable. `INSTALL` writes into `<home>/.jdbasic/lib` (home is
`USERPROFILE`, else `HOMEDRIVE` + `HOMEPATH`, else `HOME`), so an
installed module imports from any script. `SYNC` writes into a project's
own `modules` folder, which comes first.

## How a module describes itself

No separate manifest file: the module's source says it all.

```basic
' hellopkg - greets.
' version: 1.2.0
' requires: GREETPKG ~1.1

EXPORT MODULE HELLOPKG
IMPORT GREETPKG
```

- The name is the `EXPORT MODULE` line.
- The version is the `' version:` comment, `1.0.0` without one.
- Every module on an `IMPORT` line is required, in any version.
- A `' requires:` comment gives ranges for some of them, separated by commas.

## API

### Versions

| Call | What it does |
|------|--------------|
| `COMPARE(a$, b$)` | -1, 0 or 1 as `a$` is older than, equal to or newer than `b$`. A leading `v` and a `-pre` or `+build` suffix are ignored; missing parts count as 0. |
| `SATISFIES(version$, range$)` | Whether the version is in the range. |
| `BEST$(versions, range$)` | The newest version of the array that is in the range, or `""`. |

| Range | Means |
|-------|-------|
| `^1.2` / `^1.2.3` | at least that version, below the next major (`2.0.0`) |
| `^0.2.3` | below `0.3.0`; `^0.0.3` below `0.0.4` |
| `~1.2.3` / `~1.2` | at least that version, below the next minor (`1.3.0`); `~1` below `2.0.0` |
| `>=1.0 <2`, `>=1.0, <2` | every comparison holds (`>=`, `>`, `<=`, `<`, `=`) |
| `1.2` / `1` | every `1.2.x` / every `1.x.y` |
| `1.2.3`, `=1.2.3` | exactly that version |
| `*`, `x`, `latest`, `""` | any version |
| `^1 \|\| ^3` | either alternative |

### Indexes

| Call | What it does |
|------|--------------|
| `MANIFEST(source$)` | What a module's source text says about it: a map with `name`, `version` and `requires` (module name to range). |
| `INDEX(folder$)` | The index of the `.jdb` modules in a folder. |
| `WRITEINDEX(folder$)` | Writes that index as `index.json` in the folder. |
| `OPENINDEX(source$)` | The index of a source: a URL of an index file, a folder with an `index.json`, a folder of modules, or an index file. |

An index is JSON; `files` are relative to the index (or to its URL):

```json
{"format": 1, "modules": {
  "HELLOPKG": {"versions": {
    "1.2.0": {"files": ["hellopkg-1.2.0/hellopkg.jdb"], "requires": {"GREETPKG": "~1.1"}},
    "2.0.0": {"files": ["hellopkg-2.0.0/hellopkg.jdb"], "requires": {"GREETPKG": ">=2.0"}}
  }}
}}
```

A plain folder of modules such as `lib/` is an index as it is: `INDEX`
builds one on the fly, one version per module.

### Installing

| Call | What it does |
|------|--------------|
| `RESOLVE(source$, spec$)` | The modules a request needs: a map of module name to a map with `version`, `files` and `requires`. |
| `INSTALL(source$, spec$, [target$])` | Installs them into `target$` (`~/.jdbasic/lib` when `""`); answers a map of module name to the version installed. |
| `INSTALLED([target$])` | The modules installed there, name to version. |
| `REMOVE(spec$, [target$])` | Removes modules by name; answers how many. Modules that others require are not checked. |
| `UPDATE(source$, [target$])` | Installs the newest versions of everything installed that the source has. |
| `LIBDIR$()` | `<home>/.jdbasic/lib`. |

`spec$` names modules with optional ranges: `"LLMAPI ^1, TESTKIT"` or
`"llmapi@^1"`; names are not case sensitive. `""` takes every module of
the source.

`source$` can also be a git repository (`https://.../repo.git`,
`git@...`, a local `.../.git`), which is cloned with `git`, or a ZIP
archive, local or as a URL, which is unpacked; a `lib` folder inside a
repository is used when there is one.

RESOLVE gives each module the newest version in every range asked of it
whose own requirements the index can meet, so a version whose requirement
nothing satisfies is passed over for an older one. A module that is not in
the index, or a set of ranges no version satisfies, throws.

Every target folder keeps a `pkg.json` with each module's version, files,
source and the SHA-256 of its text.

### Projects

| Call | What it does |
|------|--------------|
| `SYNC(source$, project_folder$)` | Installs what a project needs into its `modules` folder and answers the map INSTALL answers. |

The first time, the ranges come from `jdbasic.json`:

```json
{"requires": {"LLMAPI": "^1", "TESTKIT": "*"}}
```

and the versions chosen are written to `jdbasic.lock` with their
SHA-256. Once `jdbasic.lock` exists, exactly its versions are installed;
delete it to take newer ones.

## Notes

- Requirements are resolved one level at a time, the newest version first;
  there is no backtracking across modules, which is enough for indexes
  that keep their ranges wide.
- HTTP sources use `HTTP.GET$`; ZIP URLs are downloaded with `curl`, git
  sources need `git` on the PATH.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/pkg_selftest.jdb`, with a versioned fixture index
(`tests/jdlibs/fixtures/pkg_index`).
CLI: `jdb/tools/jdpkg.jdb`.
