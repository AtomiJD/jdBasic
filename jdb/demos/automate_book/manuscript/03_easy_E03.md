<!-- pagebreak -->

## E03 Batch Renamer

### The chore

The office scanner names every page `scan 4711.pdf`. The phone names a
photo `IMG_20261001_0932.jpg`. Mia renames them by hand so she can find
them again: the date first, then what it is. Twenty files a week, each
with a right click, a rename and a typo now and then.

### What you get

The Batch Renamer puts the day a file arrived in front of its name:
`scan 4711.pdf` becomes `2026-10-01 scan 4711.pdf`. Sorted by name, the
folder is sorted by date, and the name still tells you what it is.
Files that already start with a date stay as they are.

### Before you start

The setup wizard has run and the Batch Renamer is switched on. It asked
you which folder to tidy; the scanner's folder is a good first choice.

### The program

The program reads the settings, plans the new names and applies them:

<!-- include recipes/easy/E03_batch_renamer/batch_renamer.jdb -->

The renamer makes the names and checks that none is taken:

<!-- include recipes/easy/E03_batch_renamer/renamer.jdb -->

### How it works

1. The pattern is text with three marks: `{date}` for the day the file
   last changed, `{name}` for its name without the extension, `{ext}`
   for the extension with its dot.
2. `PLAN` looks at every file of the folder. It leaves out folders,
   hidden files, files whose extension is not in your list and files
   whose name already starts with a date, so a second run changes
   nothing.
3. `NEW_NAME$` fills the pattern. `FREE_NAME$` adds ` (2)`, ` (3)` and
   so on when the new name is taken, by a file in the folder or by an
   earlier file of the same plan.
4. `APPLY` renames each file with `FILE.MOVE`, or only prints the plan
   with `--dry-run`.

### Run it

```
jdbasic batch_renamer.jdb --dry-run
would rename scan 4711.pdf -> 2026-10-01 scan 4711.pdf
would rename IMG_0932.jpg -> 2026-10-01 IMG_0932.jpg
2 files would get a new name.
```

### Schedule it

The wizard plans the Batch Renamer for every evening at 18:00. Files you
scan during the day carry their date the next morning.

### Make it yours

```toml
[batch_renamer]
folder = "~/Documents/Scans"
pattern = "{date} {name}{ext}"
extensions = ["pdf", "jpg", "png"]
```

- **Date last**: `pattern = "{name} {date}{ext}"`. Files that end with a
  date are not recognised as done, so use this only for a folder you
  rename once.
- **Every kind of file**: `extensions = []`.
- **Another folder**: run the wizard again or change `folder`.

### When it goes wrong

- **A file gets the wrong date**: the date is when the file last
  changed. A file copied from an old disk may carry the day it was
  copied.
- **A name ends in (2)**: a file of that name was already there. Nothing
  was overwritten; compare the two and delete one.
- **Nothing happens**: the files already start with a date, or their
  extension is not in the list.

> **Balance dividend**
> About 10 minutes a week, and every scan found by its date.
