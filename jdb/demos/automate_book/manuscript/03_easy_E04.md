<!-- pagebreak -->

## E04 One-Click Backup

### The chore

Mia's project folders live on her laptop. The company has a backup for
the file server, not for her desktop. Once in a while she copies the
important folders to a USB stick, when she remembers, and she does not
remember which of the five copies on the stick is the newest.

### What you get

Every Friday afternoon the One-Click Backup packs the folders you name
into one ZIP archive with the date in its name, such as
`backup 2026-10-02 1630.zip`. It keeps the newest ten archives and
deletes older ones, so the backup drive does not fill up. Every ZIP
opens with a double click in Explorer.

### Before you start

The setup wizard has run and the One-Click Backup is switched on. Plug
in the drive you want to keep the archives on and name it as the target
in the wizard. A folder on the same disk protects you from mistakes;
only another drive protects you from a broken laptop.

### The program

The program reads the settings, packs the archive and cleans up:

<!-- include recipes/easy/E04_one_click_backup/one_click_backup.jdb -->

The backup module collects the files, writes the archive and finds the
old archives:

<!-- include recipes/easy/E04_one_click_backup/backup.jdb -->

### How it works

1. `FILES` walks a folder and all the folders inside it. It calls
   itself for every folder it finds, and leaves out the lock files
   Office writes while a document is open (`~$report.docx`).
2. `ENTRIES` names each file in the archive by its folder and its path
   inside it, so `Documents/Offers/2026/Miller.pdf` comes back where it
   was. Files larger than `max_file_mb` are listed and left out.
3. `WRITE` reads every file and hands the lot to `ZIP.WRITE`, which
   compresses what gets smaller and stores the rest.
4. `OLD` lists the archives that start with `backup` and end with
   `.zip`, sorted by name, which is sorted by date. Everything before
   the newest `keep` archives goes, through `PRUNE` and `KILL`.

### Run it

```
jdbasic one_click_backup.jdb --dry-run
would write backup 2026-10-02 1630.zip with 1240 files
would delete backup 2026-07-24 1630.zip
1 older archives cleaned up.
```

### Schedule it

The wizard plans the One-Click Backup for Friday at 16:30. If the
laptop sleeps then, Windows runs it at the next start.

### Make it yours

```toml
[one_click_backup]
folders = ["~/Documents", "~/Desktop"]
target = "E:/Backups"
keep = 10
max_file_mb = 200
```

- **More folders**: add them to `folders`.
- **Keep more history**: `keep = 30` is about half a year of Fridays.
- **Large videos**: raise `max_file_mb`, or keep videos in a folder you
  do not back up this way.

### When it goes wrong

- **"No such folder"**: a folder in the list was renamed or the drive is
  not plugged in. The archive is not written, so nothing half done is
  left behind.
- **The program takes long or runs out of memory**: the archive is built
  in memory. Back up the large folders in a second recipe run of their
  own, or lower `max_file_mb`.
- **Two archives in one minute**: the second replaces the first, since
  they get the same name.

> **Balance dividend**
> About 15 minutes a week of copying, and the quiet knowledge that last
> Friday's work exists twice.
