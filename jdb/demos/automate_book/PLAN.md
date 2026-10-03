# Office Automation with jdBasic

Project plan for a book of at least 300 pages: 45 jdBasic programs in three
levels (easy, medium, expert) that take routine office work off the reader's
desk, a setup wizard for the easy level, and a build that turns the Markdown
manuscript into a Word document with jdBasic itself.

Status, 2026-10-02: WP 0a to 0c and WP 1 to WP 5 done; print PDF 655
pages. WP 5: chapter 6 (with a worked change: a monthly schedule in the
wizard), appendices A to E (D and E generated from the recipe folders
on every build), the index (two passes, terms in manuscript/index.txt),
page references {{page:...}}, includes by from=/to= markers, and three
bonus recipes for keyboard and mouse (E16, M16, X16, module INPUTKIT). WP 4: the fifteen expert recipes X01 to X15 with chapter 5 and
"The Whole Picture", shared modules vault (Windows Credential Manager),
jobstate and audit, `[[task]]` for a second scheduled task, all 46
recipes green on all five columns. WP 3: the fifteen medium recipes M01 to M15 with chapter 4, the
outbox rule for mail (recipes/lib/outbox.jdb), the template recipe T00
with "Your Own Recipe", and a fifth check_recipes column that compiles
every main program; all 31 recipes green. Medium recipes came out at 8 to
16 pages each against the 5 of the budget. A monthly schedule in the
wizard is planned for a later chapter on changing a program. WP 2:
chapters 1 and 2, 73 examples checked by the build. WP 1: setup wizard (core
module, console and window front ends, recipe.toml contract) and the 15
easy recipes, all green in check_recipes. Chapter 3 sets to about 98 pages
against the 64 of the budget, so the book will end well above 300. Before:
DOCX, HTML, print and screen PDF,
print cover and front cover, EPUB 3,
check_recipes, the prose and width checks, recipe E01
with its chapter pages). E01 came out at about 7 pages against the 4 the
budget gives an easy recipe, mostly for the two listings.

## 1. The book in one paragraph

Most office workers lose a few hours every week to the same small chores:
sorting downloads, renaming files, copying numbers from one sheet into
another, writing the same mail again, building the Friday report. The book
shows how to hand these chores to small programs and what to do with the
time that comes back. Every chapter ends with a program that runs, every
program is tested on both jdBasic backends, and the reader can start on day
one with the wizard and no programming at all.

## 2. Readers and promise

| Reader | Starts at | Leaves with |
|---|---|---|
| Office worker, never programmed | Chapter 2, the wizard | 15 running automations, set up by clicking, and the courage to change a line of code |
| Power user who writes Excel formulas or macros | Chapter 3 | Own programs for documents, mail, calendars and data |
| Developer or IT person | Chapter 5 | A personal job server, API integrations, an AI assistant and tools to hand out to colleagues as `.exe` |

The promise is measurable: each recipe states the time it saves per week,
and Chapter 6 shows how to track the hours won and decide where they go.

## 3. Outline and page budget

Page estimate: 17 x 24 cm trim size, a 10.5 pt book face, about 330 words
per page of prose and about 40 lines per page of code. The budget totals
365 pages so the book stays above 300 after editing.

| Part | Title | Pages |
|---|---|---:|
| Front | Title, copyright, contents, how to read this book | 10 |
| 1 | Why Automate: Time, Focus and Balance | 25 |
| 2 | Your Toolbox: jdBasic in One Afternoon and the Setup Wizard | 35 |
| 3 | Easy: Fifteen Automations You Switch On | 64 |
| 4 | Medium: Fifteen Programs for Documents, Mail and Data | 80 |
| 5 | Expert: Fifteen Systems That Run Without You | 96 |
| 6 | Living With Your Automations | 25 |
| Back | Appendices A to E, index | 30 |
| | **Total** | **365** |

### Chapter 1: Why Automate: Time, Focus and Balance (25 pages)

1.1 The hidden week: where office time goes (a one week self-audit, with a
    worksheet the reader fills in)
1.2 The automation test: frequency, effort, risk, and the chores that should
    stay manual
1.3 Time won is not time filled: deciding up front what the hours are for
1.4 Small programs instead of big platforms: why a script on your own
    machine beats a ticket to IT for most chores
1.5 Ethics and boundaries: your employer's rules, data protection, mail on
    behalf of others, monitoring yourself and not your colleagues
1.6 How the three levels work, the three personas and how to read a recipe
1.7 The life balance ledger: the simple log the book keeps coming back to

Personas who carry the recipes (one per level, they reappear in stories):
- **Mia**, project assistant, drowning in files and meeting notes (easy)
- **Jonas**, team lead, lives in Excel and his inbox (medium)
- **Priya**, IT generalist, automates for her whole department (expert)

### Chapter 2: Your Toolbox (35 pages)

2.1 Installing the jdBasic FORMS release on Windows
2.2 The setup wizard, screen by screen (section 6 of this plan)
2.3 Your first program: five lines that greet you with today's calendar
2.4 jdBasic for office people: variables, text, lists, maps, IF, loops,
    functions, in the order the recipes need them
2.5 Working with files and folders: `DIR`, `FILE.*`, `PATH.*`
2.6 The libraries the book uses, one page each: CONF, LOGGER, DT, DF (with
    the `CSVREADER` builtin), XLSX, DOCX, PDFGEN, MAIL, ICAL, SCHED, TMPL
2.7 Running a program on a schedule: Windows Task Scheduler and the jdBasic
    job loop
2.8 When something goes wrong: reading an error, the log file, `--lint`
2.9 From script to `.exe` with `jdbasic -c`, and why that matters for
    handing a tool to a colleague

### Chapter 3: Easy (64 pages)

Each recipe is about 4 pages. The wizard installs and schedules all of them;
the reader edits settings in one config file and changes code only in the
"Make it yours" section.

| # | Recipe | What it takes off your desk | Built on |
|---|---|---|---|
| E01 | Downloads Butler | Sorts the Downloads folder into Documents, Images, Installers, Archives by type and month | `DIR`, `FILE.STAT`, `PATH.*`, CONF |
| E02 | Morning Launcher | Opens the apps, folders and web pages of your workday at one click, different sets per weekday | `OS.EXEC`, CONF |
| E03 | Batch Renamer | Renames scans and photos to `2026-10-01 Invoice Miller.pdf` from date and a pattern | `DIR`, `FILE.STAT`, `REGEX` |
| E04 | One-Click Backup | Zips the folders you name into a dated archive, keeps the last ten | `ZIP.WRITE`, ARCHIVE, DT |
| E05 | Break Reminder | Reminds you to stand up and drink water at your rhythm, quiet outside working hours | SCHED, `MSGBOX`, sound |
| E06 | Shutdown Ritual | At the end of the day asks three questions, writes them to your journal, tells you to stop | `MSGBOX`, `INPUT`, file append |
| E07 | Time Tracker | Start and stop per project from a desktop shortcut or the command line, a weekly sheet in Excel | `CSVREADER`, `CSVWRITER`, XLSX, DT |
| E08 | Mail Templates | Fills your standard replies with name and date and puts them on the clipboard | TMPL, `CLIPBOARD.SET` |
| E09 | Meeting Notes Starter | Creates a Word file for the next meeting with date, attendees and agenda headings | DOCX, ICAL |
| E10 | Duplicate Finder | Finds files stored twice and reports how much space they take | `DIR`, `CODEC.SHA256$`, XLSX |
| E11 | Disk Space Report | Lists the largest folders and files, warns before the disk is full | `DIR`, `FILE.SIZE` |
| E12 | Birthday Reminder | Reads a contact list and reminds you a few days before birthdays and anniversaries | `CSVREADER`, DT, ICAL export |
| E13 | Printable Week | Turns a plain to-do text file into a one-page PDF for your desk | PDFGEN |
| E14 | Page Watcher | Checks a web page for a change (a price, an opening, a status) and tells you | `HTTP.GET$`, TEXTDIFF |
| E15 | Guest Wi-Fi Card | Prints a QR code card that logs visitors into the guest Wi-Fi | QR, PDFGEN |

Chapter close: the reader's ledger after one week of the easy level.

### Chapter 4: Medium (80 pages)

Each recipe is about 5 pages: the program is 80 to 200 lines, the reader is
expected to read it and adapt it.

| # | Recipe | What it takes off your desk | Built on |
|---|---|---|---|
| M01 | Invoice Generator | Turns rows of an Excel sheet into numbered Word or PDF invoices and ready mails | XLSX, DOCX, PDFGEN, MAIL |
| M02 | Report Merger | Collects the monthly sheets of ten colleagues into one workbook with totals | XLSX, DF |
| M03 | Mail Merge | Personal letters from a Word template and a contact list, one file or one mail each | DOCX `FILL`, `CSVREADER`, MAIL |
| M04 | Receipt Sorter | Files scanned receipts by month and category and builds the expense report | `DIR`, REGEX, XLSX |
| M05 | Shift and Vacation Planner | Builds the team calendar from a sheet and publishes it as an `.ics` everyone subscribes to | XLSX, ICAL |
| M06 | Friday Status Mail | Collects done and open items from files and sends the weekly status to the team | TMPL, MAIL, SCHED |
| M07 | Inbox Unpacker | Reads saved mails, stores attachments in folders by sender and subject | MAIL `PARSEFILE` |
| M08 | Personal Dashboard | A local web page with your numbers: hours, open items, mails, disk | JDWEB, SVG |
| M09 | Log Detective | Reads application logs, counts errors per hour, draws the chart | `FILE.STREAM_LINES`, DF, SVG |
| M10 | Hot Folder | Watches a folder and processes whatever lands there (convert, rename, mail on) | SCHED, `DIR` |
| M11 | Data Cleaner | Fixes messy CSV exports: dates, numbers, duplicates, near-duplicate names | VALID, SCHEMA, FUZZY |
| M12 | Meeting Cost Meter | Reads your calendar, sums hours in meetings and what they cost per month | ICAL, DF |
| M13 | Snippet Tool | A small command line tool for the text blocks you type ten times a day | CLI, TMPL, `CLIPBOARD.SET` |
| M14 | Contract Diff | Compares two versions of a Word document and writes a readable change report | DOCX `READ`, TEXTDIFF |
| M15 | Timesheet for the Boss | Turns the time tracker log into a monthly timesheet document and mails it | `CSVREADER`, DOCX, MAIL, SCHED |

Chapter close: building your own recipe from a blank file, the checklist.

### Chapter 5: Expert (96 pages)

Each recipe is about 6 pages. These are small systems with error handling,
logging, retries, tests and a way to ship them.

| # | Recipe | What it takes off your desk | Built on |
|---|---|---|---|
| X01 | Personal Job Server | One process that runs all your automations on time, with locks, retries and a log | SCHED, RETRY, LOGGER |
| X02 | Inbox Assistant | Summarises new mail and drafts replies with a language model, you approve each one | LLMAPI, MAIL |
| X03 | Find Anything | Full-text search over your documents with a search page in the browser | SEARCH, DOCX `READ`, JDWEB |
| X04 | Ticket Sync | Mirrors tasks between a ticket system and your to-do list over its REST API | REQ, JWT, RETRY |
| X05 | Calendar Bridge | Syncs a work calendar with a personal one through OAuth | OAUTH, ICAL, REQ |
| X06 | Work Cockpit | A Windows desktop app with buttons for every automation and their status | FORM.* |
| X07 | Tools for Colleagues | Compiling recipes to `.exe`, packaging, versioning and updating them | `jdbasic -c`, PKG |
| X08 | Parallel Crunching | Processing thousands of files on all cores with a worker pool | POOL, channels |
| X09 | Report Pipeline | Database to data frame to charts to Word and PDF to mail, every Monday at seven | DB, DF, SVG, DOCX, PDFGEN, MAIL |
| X10 | Let an AI Drive | Exposing your automations to an AI agent through the jdBasic MCP server | MCP server, tool files |
| X11 | Web Harvester | Collecting data from web pages politely: cache, retry, parsing | REQ, HTMLDOM, CACHE, RETRY |
| X12 | Tested Automations | Self tests and property tests so a change never breaks Monday's report | TESTKIT, PROPTEST |
| X13 | Health Checks and Alerts | Metrics for your jobs and a mail when one fails | METRICS, MAIL |
| X14 | Secrets and Audit Trail | Keeping passwords out of scripts, signing and logging what each job did | SECRET, JWT, LOGGER |
| X15 | Balance Score | Combines calendar, timesheet and mail statistics into a weekly life balance score and trend | ICAL, DF, SVG, JDWEB |

Chapter close: the architecture of everything the reader has built, on one page.

### Chapter 6: Living With Your Automations (25 pages)

6.1 Maintenance: what breaks (paths, passwords, web pages) and the monthly check
6.2 Sharing with the team without becoming its help desk
6.3 Talking to IT and security about your scripts
6.4 When an automation should be retired
6.5 The ledger after three months: reading your own numbers
6.6 What to do with the time: a short, honest closing chapter

### Appendices (30 pages including index)

- A. jdBasic quick reference for the book (statements, operators, the builtins used)
- B. Library reference: the eleven libraries of section 2.6 in full, the others the recipes use in short
- C. Troubleshooting: the 25 errors readers will meet, with fixes
- D. The wizard's config file, every key explained
- E. All 45 recipes at a glance: time saved, level, libraries, page
- Index

## 4. The anatomy of a recipe

Every recipe follows the same template so readers can skim:

1. **The chore** (half a page): a short scene with the persona, the cost in
   minutes per week.
2. **What you get**: one sentence and a screenshot or sample output.
3. **Before you start**: what must be in place (wizard done, mail set up, a file).
4. **The program**: the full listing, included from the tested `.jdb` file,
   never typed into the manuscript by hand.
5. **How it works**: a walk through the listing in short numbered steps.
6. **Run it**: the command, the expected output.
7. **Schedule it**: the wizard switch (easy), or the SCHED line (medium, expert).
8. **Make it yours**: three to five variations with the line to change.
9. **When it goes wrong**: the two or three likely failures.
10. **Your balance dividend**: the minutes saved per week, for the ledger.

## 5. Folder layout

```
jdb/demos/automate_book/
  PLAN.md                 this file
  README.md               how to build and test the book
  book.toml               chapter order, title, author, version
  manuscript/
    00_front.md
    01_why_automate.md
    02_toolbox.md
    03_easy.md
    04_medium.md
    05_expert.md
    06_living_with_it.md
    90_appendix_a.md ... 94_appendix_e.md
    img/                  screenshots and diagrams
  recipes/
    easy/E01_downloads_butler/
      downloads_butler.jdb
      downloads_butler_test.jdb
      fixtures/
    medium/M01_invoice_generator/...
    expert/X01_job_server/...
  wizard/
    setup_wizard.jdb      the GUI wizard (FORMS)
    setup_console.jdb     the same steps in the console
    work.conf.example
  tools/
    build_book.jdb        the build: every output from the manuscript
    check_recipes.jdb     runs every recipe and its test on both backends
    page_count.jdb        page estimate per chapter against the budget
    layout.jdb            the page layout engine for both PDFs
    epub.jdb              the EPUB 3 writer
    cover.jdb             the paperback cover
  fonts/                  the book and code typefaces (open licence)
  out/                    build output, ignored by git
```

## 6. The setup wizard (easy level)

The wizard is what lets a reader with no programming background use
Chapter 3. It is itself a jdBasic program and a recipe of the book (the
listing appears in Chapter 2).

Two front ends with one core: `setup_wizard.jdb` uses `FORM.*` on Windows,
`setup_console.jdb` asks the same questions in the console for other systems
and for builds without FORMS. Both call the same step functions in a shared
module, so the logic is tested once.

Steps:

1. **Welcome**: what the wizard will do, what it will never do (no data
   leaves the machine unless the reader turns mail on).
2. **Check**: jdBasic version and features (`OS.FEATURE`), write access to
   the chosen folder, free disk space.
3. **Your work folder**: default `Documents\AutomateWork`, created with
   `recipes`, `logs`, `config`, `journal`.
4. **About you**: name, mail address, working hours, break rhythm, the
   weekday sets for the Morning Launcher.
5. **Your folders**: Downloads, Desktop and Documents detected, confirmed or
   changed; which folders the backup covers.
6. **Mail (optional)**: SMTP server, port, user; a test mail with
   `MAIL.SEND`; the password is not stored in the config file (see section 10).
7. **Pick your automations**: a check box per easy recipe with its one line
   description and the minutes it saves.
8. **Schedule**: for each picked recipe either a Windows Task Scheduler entry
   (`schtasks` through `OS.EXEC`) or an entry for the job server of X01.
9. **Dry run**: every picked recipe runs once in a mode that only reports
   what it would do.
10. **Done**: a summary page, where the config file lives, how to undo.

Re-running the wizard changes settings instead of starting over; a
`--doctor` switch checks an existing setup and repairs schedules;
`--uninstall` removes the scheduled tasks and leaves the reader's files alone.

Everything the wizard sets lands in one file, `config/work.conf` (CONF
library, TOML subset), documented key by key in Appendix D.

## 7. Building the book

The book is free as PDF and e-book and sold at cost as a print on demand
paperback. All three come out of one build, written in jdBasic, from the
same Markdown manuscript. Showing that this works at print quality is part
of the book's point, so the colophon says how the book was made.

| Output | For | Made by |
|---|---|---|
| `out/book.docx` | Reviewing and commenting in Word | `lib/docx.jdb` |
| `out/book_print.pdf` | The print on demand interior, 17 x 24 cm, all fonts embedded | `lib/pdfgen.jdb` and the layout engine below |
| `out/book_screen.pdf` | The free download: same pages, with clickable links and bookmarks | the same engine |
| `out/book.epub` | E-book readers (EPUB 3) | `lib/md.jdb` for the XHTML, `ZIP.WRITE` for the container |
| `out/cover_print.pdf` | The paperback cover, spine width from the page count | `lib/pdfgen.jdb` |
| `out/html/` | Preview in the browser while writing | `lib/md.jdb` |

No Word and no LibreOffice is involved; this notebook has neither.

Pipeline:

1. Read `book.toml`: chapter files in order, title, authors, version, trim
   size, fonts.
2. For each chapter, read the Markdown into blocks: headings, paragraphs,
   lists, tables, code fences, block quotes, note boxes, images, page breaks.
   This block list is the one model every output is written from.
3. Resolve includes. A line `<!-- include recipes/easy/E01_downloads_butler/downloads_butler.jdb -->`
   is replaced by that file as a code block; `lines=12-40` takes a range.
   The manuscript never holds a copy of a listing.
4. Write each output from the blocks.
5. Report word count, code lines and the page count per chapter against
   the budget in section 3 (exact for the PDF, estimated before).

### The layout engine (`tools/layout.jdb`)

Sets the block list into pages on top of PDFGEN: body text justified with
hyphenation off, headings that never sit alone at the bottom of a page,
listings that break between lines with a "continued" mark, note boxes that
stay whole, running heads with the chapter title, page numbers outside,
chapters starting on a right hand page. The table of contents and the index
need page numbers, so the engine runs twice: the first pass records where
each heading and index term lands, the second writes the pages.

### What the libraries need first (work package 0)

Each addition goes into the library with its self test, so every jdBasic
user gains it.

`lib/docx.jdb`:

| Addition | Why the book needs it |
|---|---|
| `DOCX.CODE(doc, text$)` | Listings in a monospace font on a shaded background, line breaks kept |
| Inline code in `PARAGRAPH` | `` `MAIL.SEND` `` in running text set in the code font |
| `DOCX.IMAGE(doc, path$, width_cm)` | Screenshots of the wizard and the outputs |
| `DOCX.TOC(doc)` | A table of contents field Word fills on opening |
| `DOCX.NOTE(doc, kind$, text$)` | The boxes "Balance dividend", "Watch out", "Try this" |
| Links | `[text](url)` as a clickable hyperlink |

`lib/pdfgen.jdb`:

| Addition | Why the book needs it |
|---|---|
| Any page size in mm | The 17 x 24 cm trim size |
| TrueType fonts, embedded | Print on demand services reject files with fonts that are not embedded; a book face and a code face with all the characters the text needs |
| PNG images | Screenshots are PNG; decoding uses the inflate the ZIP reader already has, or JPEG conversion in the build if that is simpler |
| Bookmarks (outline) | Chapter navigation in the screen PDF |
| Links | Clickable URLs and cross references in the screen PDF |
| Compressed streams | A 365 page book uncompressed gets large |

The EPUB writer is new: `tools/epub.jdb`, an EPUB 3 container (mimetype
stored first, OPF package, navigation document, one XHTML file per chapter,
CSS, images), checked against EPUBCheck at home if it is not available here.

All of this is plain jdBasic and testable on this notebook in both backends.

## 8. Quality rules

- Every listing is a real file under `recipes/`, included at build time.
- Every recipe has a test with fixtures; nothing in a test sends a real mail
  (it writes `.eml` files) or calls a real web site (it starts a local JDWEB
  server).
- `tools/check_recipes.jdb` runs, for every recipe: `--lint`, the test in the
  interpreter, `-c`, the test as `.exe`, and `-c` of the main program. A
  recipe is done when all five pass.
- Recipes that need a feature this notebook lacks are marked: X02 needs an
  API key or a local model, X09 needs the SQLITE build. They are tested at
  home.
- Prose follows the repo rules: plain sentences, no dash as punctuation,
  no marketing words. The check hooks run on every manuscript file.
- Code follows the `jdbwrite` checklist: no reserved names, documented
  signatures only, array operations where they exist, comments that say
  what a function does.
- Screenshots come from `jdbeyes` captures of the real programs. Before any
  capture that drives the mouse or keyboard, Atomi is asked to keep their hands
  off the machine.

## 9. Work packages and order

| WP | Content | Done when | Where |
|---|---|---|---|
| 0a | Folder, `book.toml`, Markdown block reader, include resolver, DOCX additions, `build_book.jdb` for DOCX and HTML, `check_recipes.jdb`, a sample chapter | `out/book.docx` of the sample chapter has TOC, code, image, note box and reads back with `DOCX.READ` | notebook |
| 0b | PDFGEN additions (page size, TrueType embedding, PNG, bookmarks, links, compression), `layout.jdb` | the sample chapter as print and screen PDF, fonts embedded, two pass TOC with right page numbers | notebook |
| 0c | `epub.jdb`, `cover.jdb` | the sample chapter as EPUB opens in a reader; a cover for a given page count | notebook, EPUBCheck at home |
| 1 | Wizard (both front ends) and recipes E01 to E15 | `check_recipes` green for the easy level, wizard dry run clean on a fresh user folder | notebook |
| 2 | Chapters 1, 2, 3 | Chapters build in all outputs, page count within 10 percent of budget | notebook |
| 3 | Recipes M01 to M15, Chapter 4 | `check_recipes` green, chapter builds | notebook |
| 4 | Recipes X01 to X15, Chapter 5 | green except the marked recipes | notebook, X02 and X09 at home |
| 5 | Chapter 6, appendices, index, colophon | full book builds, at least 300 pages in the print PDF | notebook |
| 6 | Screenshots, editing pass, consistency check of every API mention against `help.txt`, preflight of the print PDF for the print on demand service | Atomi's read-through, a proof copy | both |

A sensible first session: WP 0a up to a sample chapter in DOCX, then E01 to
E03 with their recipe pages, so the template is proven before the other 42.
WP 0b is the largest single piece (font embedding and the layout engine)
and can run alongside the easy recipes.

## 10. Decisions

Taken on 2026-10-01:

1. **Language**: English.
2. **Title**: "Office Automation with jdBasic: 50+ Programs Bringing BASIC
   Back to Business and Your Hours Back to You" (decided 2026-10-02;
   first working title "Automate Your Work for a Better Life Balance").
3. **Authors**: Achim Christ and Claude. The book shows that a whole
   production, from code to typeset pages, can be automated at high quality;
   the colophon describes how it was made.
4. **Publishing**: free as PDF and EPUB, and a print on demand paperback.
5. **Release pack**: the reader installs the FORMS release (the wizard and
   the Work Cockpit need it). A later professional edition uses a pack with
   NATIVEC; until then the parts about `jdbasic -c` (section 2.9, recipe
   X07) are marked as needing that pack, and `check_recipes` still runs every
   recipe compiled so the professional edition needs no rework.
6. **Mail passwords**: asked for at the start, never written to disk. A
   recipe run by hand asks when it needs the password. Scheduled recipes
   cannot ask, so they either put finished mails as `.eml` files into an
   outbox folder the reader sends, or run under the job server (X01), which
   asks once when it starts and keeps the password in memory only.

Still open:

7. **Print on demand service** (for example KDP, BoD or IngramSpark): decides the
   exact trim size, margins, bleed and the cover template. The plan assumes
   17 x 24 cm until then.
8. **Typefaces**: a book face and a code face under an open licence that
   allows embedding, with the characters of the text (Source Serif and
   Source Code Pro, or similar). To be fetched once, small download.
