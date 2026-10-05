<!-- pagebreak -->

## X07 Tools for Colleagues

### The chore

Lena's week sheet program has fans. Three colleagues want it, and none
of them wants to install jdBasic, open a command prompt or learn what
a module is. So far she copied the files to each of them by hand, and
after every improvement she walked around with a USB stick. Twice a
colleague kept an old version for weeks and sent her a sheet with a
bug she had fixed long before. Copying, explaining and chasing old
versions costs her close to an hour a week.

### What you get

Three commands. `build` tests a recipe, compiles it into an `.exe`
with everything it needs next to it, stamps it with a version and puts
it into a release folder, for example on a shared drive. `check` tells
a colleague whether a newer version is there. `update` brings their
copy to the newest version, checks every file against its checksum
before anything changes, and keeps the old version in a folder of its
own.

```
tools_for_colleagues.exe check C:/Tools/week_sheet
week_sheet: installed 1.0.0, released 1.1.0
An update is ready.
```

The colleagues need nothing from jdBasic. The compiled program runs on
any Windows computer, and the release folder may also be a web address
inside the company.

### Before you start

You need a folder that you may write to and your colleagues may read,
such as `S:/Tools` on a shared drive, and the recipe you want to hand
out, with its test. The compiler must be the jdBasic you work with,
named in the `[paths]` part of `work.conf`, which the wizard wrote,
and it must be the release with the native compiler:
`jdbasic --version` lists *NativeC*. The forms release this book
starts with does not have it, as Chapter 2 explains under *From
Script to Program*.

A compiled recipe still reads its settings from a `work.conf`. Give
your colleagues one with the part of the recipe, or start it with
`--config` and a settings file on the shared drive.

Ask your IT department before you hand out programs. Many companies
allow only programs from known publishers; a program you compiled
yourself is not one of them until IT says so. The section
*Talking to IT and Security* in Chapter 6 is about that conversation.

### The program

The program reads the settings and the command, and calls the module:

<!-- include recipes/expert/X07_tools_for_colleagues/tools_for_colleagues.jdb -->

The module compiles, releases and updates:

<!-- include recipes/expert/X07_tools_for_colleagues/ship.jdb -->

### How it works

1. **Test before release.** `build` looks for the recipe's test next
   to it, `week_sheet_test.jdb` for `week_sheet.jdb`, and runs it with
   jdBasic. When the test fails, nothing is compiled and nothing is
   released. A version that is already in the release folder is
   refused as well, so a released version never changes under the
   feet of a colleague who has it.

2. **Compiling.** `BUILD` runs `jdbasic -c -o` with the path of the
   `.exe`. The compiler puts the runtime `jdbrt.dll`, the libraries it
   needs and the default font next to the program. That folder runs
   on its own, on a computer without jdBasic. A program that does not
   compile gives `ok` as `FALSE` and the compiler's message in
   `output`.

3. **The version.** `STAMP` writes `version.toml` into the folder,
   with the name, the version and the time of the build. `VERSION$`
   reads it back, on your computer and on your colleagues'. Versions
   are three numbers such as `1.2.0`. `NEWER` compares them with
   `PKG.COMPARE` from the package library, which knows that `1.10.0`
   comes after `1.9.0`.

4. **The release.** `PACKAGE` copies the folder into the release
   folder as `week_sheet-1.2.0` and writes `files.txt`, one line per
   file with its SHA-256 checksum. Then it writes `latest.toml`, which
   names the newest version and the checksum of `files.txt`. It
   writes it under another name first and moves it into place, so a
   colleague who checks at that moment reads the old file or the new
   one, never half of it.

5. **Fetching.** `FETCH$` reads a file of the release from a folder,
   or from a web address when the release folder starts with `http`.
   A request that fails is tried three times with a growing pause by
   the `RETRY` library, since a web server that is busy for a second
   answers the next time.

6. **Checking every byte.** `Download$` fetches `files.txt` and
   compares its checksum with the one in `latest.toml`, then fetches
   each file and compares its checksum with its line. Everything goes
   into a temporary folder first. A single wrong checksum stops the
   update before the colleague's copy is touched.

7. **The update.** `UPDATE` copies the old files into `previous` and
   moves the new ones in. A colleague whose new version misbehaves
   copies the files of `previous` back and has the old one again.

### Run it

Release version 1.0.0 of the week sheet. Add `--dry-run` first to see
what it would test, compile and release. The real run:

```
jdbasic tools_for_colleagues.jdb build week_sheet.jdb --version 1.0.0
test passed
released week_sheet 1.0.0, 10 files in S:/Tools\week_sheet-1.0.0
```

The same version a second time is refused:

```
jdbasic tools_for_colleagues.jdb build week_sheet.jdb --version 1.0.0
Error #99: 1.0.0 is released. at line 55
```

On the colleague's computer, the compiled tool checks and updates:

```
tools_for_colleagues.exe check C:/Tools/week_sheet
week_sheet: installed none, released 1.0.0
An update is ready.
tools_for_colleagues.exe update C:/Tools/week_sheet
updated week_sheet from nothing to 1.0.0, 10 files
```

After you released 1.1.0, the next update says:

```
updated week_sheet from 1.0.0 to 1.1.0, 10 files
```

Each version takes about 20 MB in the release folder, most of it the
runtime that travels with every version.

### Schedule it

Releasing is something you decide, so `build` runs by hand. The update
can run on its own on each colleague's computer: a task in the Task
Scheduler at logon, or a job of the X01 server on yours for the copy
you use yourself:

```toml
[[job]]
name = "update week sheet"
when = "weekdays 08:00"
program = "X07_tools_for_colleagues/tools_for_colleagues.jdb"
args = ["update", "C:/Tools/week_sheet"]
```

### Make it yours

The settings are in the part of `work.conf` that starts with
`[tools_for_colleagues]`:

```toml
[tools_for_colleagues]
release = "S:/Tools"
builds = "~/Documents/AutomateWork/builds"
```

- **A web address.** Put the release folder on an internal web server
  and set `release = "http://tools.example.local/tools"`. Check and
  update work the same way; only `build` needs the folder itself, so
  release with a second settings file that names the folder.
- **More than one tool.** One release folder holds one tool, because
  `latest.toml` names one version. Give each tool its own folder, such
  as `S:/Tools/week_sheet` and `S:/Tools/invoices`.
- **Clean up old versions.** Each release stays in the folder. Delete
  folders of versions nobody uses any more by hand; `latest.toml`
  points only to the newest one.
- **A shortcut for colleagues.** Put a link to the `.exe` on their
  desktop, with `--config` and the settings file in the link's target,
  so a double click starts the tool.

### When it goes wrong

- **"The test fails"**: the recipe's test does not pass. Run it
  yourself with `jdbasic week_sheet_test.jdb` and fix what it shows.
- **"Compiling failed"**: the program uses something the compiler
  rejects, such as a variable without `DIM`. The message names the
  line. The interpreter is more forgiving, which is why a program can
  run there and still fail to compile.
- **"the checksum does not match"**: a file of the release changed
  after it was released, or the download was cut off. Release a new
  version; never change a released folder.
- **The program does not start on a colleague's computer**: Windows
  or the virus scanner blocked an unknown program. That is the
  conversation with IT from *Before you start*.

> **Balance dividend**
> About 45 minutes a week of copying, explaining and chasing old
> versions, and colleagues who always run the version you fixed.
