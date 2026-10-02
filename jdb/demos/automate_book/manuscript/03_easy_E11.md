<!-- pagebreak -->

## E11 Disk Space Report

### The chore

The warning comes at the worst moment: Mia is about to save the
presentation for the board meeting, and Windows says the disk is full.
She clicks through folders and guesses. Was it the videos from the
training, the old project archives, the installers in Downloads? Ten
minutes later she has freed a little space and still does not know where
the rest went.

### What you get

Every Monday morning the Disk Space Report measures your user folder. It
lists the folders right below it with their size, the largest one first,
and the ten largest files anywhere under it. Above a limit you choose it
starts with a warning, long before the disk is full. The report is
printed and saved as a text file in your work folder.

### Before you start

Nothing; the wizard has entered your user folder.

### The program

The program reads the settings, measures, prints and saves:

<!-- include recipes/easy/E11_disk_space/disk_space.jdb -->

The module walks the folders and writes the report:

<!-- include recipes/easy/E11_disk_space/diskspace.jdb -->

### How it works

1. `SCAN` looks at each entry right below the folder. For a folder it
   calls `Walk`, which adds up the sizes of every file inside it and in
   its subfolders. Each file is also noted with its size.
2. Hidden folders, such as the settings Windows keeps in `AppData`, are
   left out. Their size is not yours to clean up.
3. `LARGEST` orders a list by size with `GRADE` and keeps the first
   entries.
4. `HUMAN$` shows a size the way Explorer does: bytes, KB, MB or GB.
5. `REPORT$` puts it together and adds the warning when the total is above
   `warn_gb`.

The report does not show how much space is left on the drive, since
jdBasic has no command for that yet on Windows. Explorer shows it in the
properties of the drive.

### Run it

```
jdbasic disk_space.jdb
Disk space under C:\Users\mia: 63.40 GB
WARNING: more than 50 GB.
(Free space: see the properties of the drive.)

Largest folders
  38.12 GB  Videos
  14.96 GB  Downloads
   7.03 GB  Documents
```

The list of the largest files follows, and the last line names the text
file the report was saved in.

### Schedule it

The wizard plans the report for Monday at 9:00, at the start of the week,
when there is still time to tidy up.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[disk_space]`:

```toml
[disk_space]
root = "~"
warn_gb = 50
top = 10
report = "~/Documents/AutomateWork/disk_space.txt"
```

- **A team drive**: set `root` to the folder of your team, such as
  `"S:/Projects"`, to see which project grows fastest.
- **A smaller disk**: lower `warn_gb` to half the size of your disk.
- **More detail**: `top = 25` lists more folders and files.

### When it goes wrong

- **It takes a while**: measuring a large folder means looking at every
  file in it once. That is why the recipe runs on Monday morning on its
  own.
- **A folder is missing from the list**: it is hidden, or you have no
  right to read it.
- **The sizes differ from Explorer**: Explorer counts the space a file
  takes on the disk, the report counts its length. Small files take a
  little more room than they are long.

> **Balance dividend**
> About 5 minutes a week, and no full disk on the morning it matters.
