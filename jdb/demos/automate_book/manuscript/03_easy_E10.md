<!-- pagebreak -->

## E10 Duplicate Finder

### The chore

Mia's documents folder holds the same budget sheet four times: the one
from the mail, the one she saved under a new name, the copy in "Old
versions" and the one a colleague sent back. Photos from the site visits
sit in three places. Every few months the disk is full, and finding out
which copies can go takes an afternoon nobody has.

### What you get

Once a week the Duplicate Finder looks through the folders you name and
finds every file that is stored more than once, whatever its name. It
writes an Excel report: one sheet with the totals, one with every copy,
its folder and the date it last changed, the copies that waste the most
space first. It deletes nothing. You decide which copy stays.

### Before you start

Nothing; the wizard has entered your Documents and Downloads folders.

### The program

The program the wizard runs reads the settings, collects the files,
finds the groups and writes the report:

<!-- include recipes/easy/E10_duplicate_finder/duplicate_finder.jdb -->

The module walks the folders, compares the files and writes the Excel
file with the XLSX library:

<!-- include recipes/easy/E10_duplicate_finder/dupes.jdb -->

### How it works

1. `FILES` walks every folder and its subfolders with `DIR$` and
   `FILE.STAT` and collects the paths. Hidden files stay out.
2. `GROUPS` sorts the files by size first. Two files of different size
   cannot have the same content, so only files that share a size are read
   at all, which keeps the program fast on a large disk.
3. `SameContent` reads each of those files and computes its SHA-256 with
   `CODEC.SHA256$`, a fingerprint that is the same for equal content and
   different for any change, however small. Files with the same
   fingerprint form a group.
4. `WASTED` adds up what the copies take beyond one file of each group.
5. `REPORT` writes the two sheets. `GRADE` orders the groups by the space
   they waste, so the worst offenders come first.

### Run it

```
jdbasic duplicate_finder.jdb
2412 files looked at
37 groups, 81 files, 412.6 MB in copies
Report: C:\Users\mia\Documents\AutomateWork\duplicates.xlsx
```

Open the report in Excel, sort or filter as you like, and delete the
copies you do not need yourself.

### Schedule it

The wizard plans the Duplicate Finder for Friday at 16:00, so the report
is waiting when you tidy up before the weekend.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[duplicate_finder]`:

```toml
[duplicate_finder]
folders = ["~/Documents", "~/Downloads", "~/Pictures"]
report = "~/Documents/AutomateWork/duplicates.xlsx"
min_size_kb = 1
```

- **More folders**: add your Pictures folder or a project folder on the
  team drive to `folders`.
- **Only the big ones**: `min_size_kb = 1024` leaves out everything below
  a megabyte, which is where the space goes.

### When it goes wrong

- **It takes long**: the first run on a full disk reads many files. Leave
  out folders such as program installations, or raise `min_size_kb`.
- **"No file is stored twice"**: check the folder names in `folders`; a
  folder that does not exist is skipped without a message.
- **Files with the same name are missing**: the recipe compares content,
  not names. Two different versions of `budget.xlsx` are no duplicates.

> **Balance dividend**
> About 10 minutes a week, and the afternoon a full disk would cost.
