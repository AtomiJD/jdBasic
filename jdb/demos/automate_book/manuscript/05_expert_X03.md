<!-- pagebreak -->

## X03 Find Anything

### The chore

Lena knows the document exists. Somebody wrote down last year how the
travel costs of the field staff are booked, in a Word file or in the
minutes of a meeting, or perhaps in a PDF from the auditors. The
shared drive has eleven thousand files in folders named after people
who have left. Windows search finds the files whose names contain the
word, not the ones that mention it on page four. So she asks around,
and twenty minutes later a colleague sends the link.

### What you get

A search over the text of all your documents, on a page in your
browser: Word files with their tables, PDF files, text, Markdown and
CSV. Type a few words and the page lists the best matches, each with
the file name, a piece of text around the words it found, and the
full path. A misspelt word still finds what you meant.

The index lives in a folder of its own. Each run of the program
brings it up to date and reads only the files that are new or
changed since the last run, so after the first run an update of a
large folder takes seconds.

### Before you start

Decide which folders to search, such as your documents and the
department's share. Reading a large share the first time takes a
while: a few minutes for a thousand Word files. Run the first update
by hand and watch it.

The search page, like every server in this chapter, listens only on
your own computer.

### The program

The program reads the settings and does one of three things: it
updates the index, answers a search in the console, or serves the
search page:

<!-- include recipes/expert/X03_find_anything/find_anything.jdb -->

The module does the work:

<!-- include recipes/expert/X03_find_anything/finder.jdb -->

### How it works

1. `SCAN` walks through every folder and the folders below it. `Walk`
   calls itself for each folder it meets and collects the files whose
   extension is on the list, with the time they last changed and
   their size from `FILE.STAT`. Hidden files and the `~$` files Word
   leaves next to an open document are skipped.
2. `TEXT$` gets the words out of a file. A Word file is read with
   `DOCX.READ`, which answers its paragraphs and its tables; the cells
   of each table row are added as one line. A PDF goes through
   `PDF.TEXT$`, everything else is read as text.
3. `OPEN` loads two files from the index folder: the search index
   that `SEARCH.WRITEFILE` saved, and `catalog.json`, which remembers
   for each file the time it changed, its size and the first 4000
   characters of its text. Without them it starts an empty index with
   two fields, the words of the file name and the text, where a word
   in the name counts twice.
4. `PLAN` compares what `SCAN` found with the catalog. A file that is
   not in the catalog is new; one with another time or size has
   changed; one in the catalog that `SCAN` no longer found was
   deleted. Nothing is read yet, which is what the dry run shows.
5. `UPDATE` reads the new and changed files into the index with
   `SEARCH.ADD`, which replaces an older version of the same file,
   and takes the deleted ones out with `SEARCH.REMOVE`. A file that
   cannot be read, a damaged Word file for example, does not stop the
   run: `ReadOne$` catches the error, keeps the file in the catalog
   with it, and the program writes it to the log. It is tried again
   when it changes.
6. `FIND` asks the index with `SEARCH.QUERY`. The index ranks the
   files the way search engines do (the method is called BM25): a
   word that is rare in all files but frequent in one file makes that
   file a good match. With `"fuzzy": 1` a word that is not in the
   index is replaced by the closest one within one typing mistake.
7. `SNIPPET$` finds the first query word in the stored text and cuts
   about 200 characters around it, so you see why the file matched.
8. `PAGE$` writes the search page as HTML. Everything that comes from
   a file or from the query goes through `HTMLDOM.ESCAPE$`, so a file
   name with `<` in it cannot break the page. `MOUNT` puts the page
   at `/` and the same hits as JSON at `/hits.json`.

The paths in the index use forward slashes, `C:/Users/lena/...`,
which Windows understands as well as the usual backslashes.

### Run it

The first run reads everything; `--dry-run` shows the plan first:

```
jdbasic find_anything.jdb --dry-run
1840 new, 0 changed, 0 gone
would read C:/Users/lena/Documents/Budget/forecast_2027.docx
...
```

The real run ends with a line in the log, here on two lines:

```
jdbasic find_anything.jdb
2026-10-05 12:30:02 INFO  [find_anything] index up to date
    read=1838 dropped=0 failed=2
```

Search in the console. Each hit is its path and, below it, the piece
of text around the words, here wrapped to fit the page:

```
jdbasic find_anything.jdb --search "travel costs field staff"
C:/Users/lena/Share/Controlling/Minutes/2025-11-12 team.docx
    ... the travel costs of the field staff are booked on cost
    centre 4410 from January ...
```

Or start the page and open `http://localhost:8766/` in the browser:

```
jdbasic find_anything.jdb --serve
Search page: http://localhost:8766/
Only this computer can open it. Stop it with Ctrl+C.
```

### Schedule it

The wizard runs the update every day at half past twelve, when the
computer is on but you are at lunch. It also sets up a second task
that starts the search page with `--serve` when you log on, because
the recipe asks for one in its `recipe.toml`:

```toml
[[task]]
name = "search page"
schedule = "at logon"
args = ["--serve"]
```

Any recipe can declare more tasks this way; each gets a name of its
own in the *AutomateWork* folder of the Task Scheduler. The page
always shows the index as the last update left it.

### Make it yours

The settings are the `[find_anything]` part of `work.conf`:

```toml
[find_anything]
folders = ["~/Documents", "S:/Controlling"]
kinds = ["txt", "md", "csv", "docx", "pdf"]
index = "~/Documents/AutomateWork/find"
language = "en"
port = 8766
```

`language` decides how word endings are treated: with `en`, a search
for *policies* finds *policy*. Use `de` for German documents and
`none` to switch it off.

Changes in the code:

- **Excel files.** Their text is in the cells. Add `"xlsx"` to
  `kinds`, add `XLSX` to the `IMPORT` line of the module, put this
  line after the first line of `TEXT$`:

  ```
  IF ext$ = ".xlsx" THEN RETURN SheetText$(path$)
  ```

  and this function at the end of the module:

  ```
  FUNC SheetText$(path$)
      DIM book = XLSX.READ(path$)
      DIM names = MAP.KEYS(book)
      DIM lines = []
      DIM s = 0
      DIM r = 0
      FOR s = 0 TO LEN(names) - 1
          DIM rows = book{names[s]}
          FOR r = 0 TO LEN(rows) - 1
              PUSH(lines, JOIN(rows[r], " "))
          NEXT r
      NEXT s
      RETURN JOIN(lines, CHR$(10))
  ENDFUNC
  ```

- **Leave a folder out**, such as an old archive. In `Walk`, change
  the line `Walk(full$, kinds, out)` to

  ```
  IF name$ <> "Archive" THEN Walk(full$, kinds, out)
  ```
- **More hits per page.** `PAGE$` asks for 20; change the number in
  `FIND(state, query$, 20)`.

### When it goes wrong

- **"not a folder"** at the start: a folder in `folders` does not
  exist or the share is not connected. The run goes on with the
  others; connect the drive and run it again.
- **"not readable"** lines in the log: the file is damaged, protected
  with a password, or a PDF that is a scanned picture without text.
  The index knows the file by its name only.
- **"Port 8766 is taken"**: the page is already running, or another
  program uses the port. Set another `port`.
- **A file you just saved is not found**: the index is as old as the
  last update. Run the program once without options.

> **Balance dividend**
> About 40 minutes a week of searching and asking around, for anyone
> who works with a large shared drive.
