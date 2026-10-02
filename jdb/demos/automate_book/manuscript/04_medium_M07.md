<!-- pagebreak -->

## M07 Inbox Unpacker

### The chore

Invoices, offers, signed contracts and spreadsheets reach Jonas as
attachments. To keep them, he opens each mail, saves each file,
finds the right folder, and renames `document.pdf` to something he
will recognise in March. Half the time he skips it, and later he
searches his inbox for "that PDF from Anna in October".

### What you get

Jonas saves the mails he wants to keep into one folder. The unpacker
takes every saved mail apart: each attachment goes into a folder
named after the sender, and inside it into a folder named after the
day and the subject:

```
Anna Miller\2026-10-23 Invoice October\invoice 2026-118.pdf
Ben Ortiz\2026-10-26 Re Budget 2027\budget.xlsx
```

The mails themselves move into a `done` folder, so the next run only
looks at new ones. Logos and pictures that sit inside the text of a
mail are left out; they are rarely worth keeping.

### Before you start

The unpacker reads mails saved as `.eml` files. Most mail programs
save these: in Thunderbird, choose *Save As* on a mail; in Outlook
on the web and the new Outlook, choose *Download* or *Save as*; in
Apple Mail, drag the mail into a Finder folder. The classic Outlook
for Windows saves `.msg` files instead, which this recipe cannot
read. There, forward the mail to yourself as an attachment and save
that, or use Outlook on the web.

Make one folder for the saved mails and one for the attachments. The
wizard suggests `saved mail` and `attachments` in your
`AutomateWork` folder.

### The program

The program reads the settings, plans what to save and saves it:

<!-- include recipes/medium/M07_inbox_unpacker/inbox_unpacker.jdb -->

The module does the work:

<!-- include recipes/medium/M07_inbox_unpacker/unpacker.jdb -->

### How it works

1. `PLAN` lists the `.eml` files with `DIR$` and reads each one with
   `MAIL.PARSEFILE`. The result is a map with the sender, the
   subject, the date, the text and the attachments. The names of
   the attachments are in `att_names`, in the order they appear in
   the mail.
2. `SENDER$` takes the name from the `From` line with
   `MAIL.ADDRESSNAME$`. A mail from a bare address such as
   `bob@supplier.example` has no name, so the address is used.
3. `DAY$` reads the `Date` line with `DT.PARSE`, which understands
   the format mails use, and writes the day in your own time zone.
   A mail without a readable date goes into an `undated` folder.
4. `SAFE$` makes any text usable as a file name. Windows does not
   allow `\ / : * ? " < > |` in names, and a subject such as
   *Invoice: October/2026* contains two of them. They become spaces.
   `-t$` splits the text into characters, so the cut to 60
   characters never breaks a letter such as *ü* in half.
5. A picture inside the text of a mail carries a Content-ID, which
   `att_cids` lists. Attachments with a Content-ID are left out
   unless `keep_inline` is on.
6. `FreeName$` makes sure no file is overwritten. When the folder
   already has an `invoice.pdf`, or the same plan already uses the
   name, the new file becomes `invoice (2).pdf`.
7. `APPLY` writes each attachment with `MAIL.SAVEATTACHMENT`, which
   decodes it byte for byte, and then moves every mail it looked at
   into the done folder with `FILE.MOVE`.

### Run it

A dry run shows where every file would go:

```
jdbasic inbox_unpacker.jdb --dry-run
2 mails in C:/Users/Jonas/Documents/AutomateWork/saved mail
would save Anna Miller\2026-10-23 Invoice October\invoice 2026-118.pdf
would save Ben Ortiz\2026-10-26 Re Budget 2027\budget.xlsx
would save Ben Ortiz\2026-10-26 Re Budget 2027\budget notes.docx
Nothing written. Run without --dry-run to save 3 files.
```

Without the switch the files are saved and the mails move into
`saved mail\done`. Nothing is ever deleted: if a folder looks wrong,
move the mails back out of `done` and run it again.

### Schedule it

Once a day is enough; the wizard plans it for 12:30. Jonas saves
mails whenever he likes, and after lunch they are filed.

### Make it yours

The settings are the `[inbox_unpacker]` part of `work.conf`:

```toml
[inbox_unpacker]
inbox_folder = "D:/Mail/saved"
target_folder = "S:/Team/Documents/from mail"
done_folder = "D:/Mail/saved/done"
keep_inline = false
```

Changes in the code:

- **One folder per month instead of per day.** In `DAY$`, write the
  month only: `"%Y-%m"` instead of `"%Y-%m-%d"`.
- **One folder per company.** In `SENDER$`, use the part of the
  address after the `@`. Replace the two lines that set `who$` from
  the name and the address with:

  ```
  DIM addr$ = MAIL.BAREADDRESS$(from$)
  DIM who$ = MID$(addr$, INSTR(addr$, "@") + 1, LEN(addr$))
  ```

  Every mail from `miller.example` then lands in one folder.
- **Skip calendar invitations and signatures.** In `PLAN`, add a list
  of extensions above the loops:

  ```
  DIM skip = [".ics", ".vcf", ".p7s"]
  ```

  In the inner loop, replace the line with `IF keep_inline` by these
  three, and delete the `DIM att$` line just below them:

  ```
  DIM att$ = att[j]
  DIM wanted = NOT (LCASE$(PATH.EXT$(att$)) IN skip)
  IF (keep_inline ORELSE cid$ = "") ANDALSO wanted THEN
  ```

### When it goes wrong

- **"No mail folder at"**: the `inbox_folder` setting points to a
  folder that does not exist. Check the spelling in `work.conf`.
- **"No .eml files"**: the mails were saved as `.msg`, or into a
  different folder. See *Before you start*.
- **A file named `unnamed`**: the attachment had no name, or only
  characters Windows does not allow.
- **The day is one off**: the folder uses the day in your time zone,
  so a mail sent late at night from another continent can land on
  the next or the previous day.

> **Balance dividend**
> About 30 minutes a week of saving and renaming, and every
> attachment can be found by who sent it and when.
