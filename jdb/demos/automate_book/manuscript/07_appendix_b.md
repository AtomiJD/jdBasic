<!-- pagebreak -->

# Appendix B: Library Reference

The libraries are jdBasic programs of their own, in the `lib` folder of
jdBasic, and the book's own modules are in `recipes/lib`. A program
loads them with `IMPORT` and calls their functions with the library's
name in front: `CONF.GET(cfg, "mail.server", "")`. This appendix lists
the functions the recipes call. Each library file starts with a comment
that describes the rest; open it in any editor.

Arguments with a value, such as `fallback = 0`, may be left out.

## The libraries of Chapter 2

### CONF: settings files

| Function | What it does |
|---|---|
| `CONF.TOML(path$)` | Reads a TOML file into a map of maps |
| `CONF.TOML_TEXT(text$)` | The same from a text |
| `CONF.INI(path$, policy$ = "last")` | Reads an INI file; `policy$` decides about a repeated key |
| `CONF.ENV(path$, apply = FALSE)` | Reads a `.env` file; with `apply` into the environment too |
| `CONF.GET(cfg, path$, fallback = 0)` | A value by a dotted path such as `"mail.server"`, or the fallback |

### WORKCONF: the book's settings

| Function | What it does |
|---|---|
| `WORKCONF.CONFIG_PATH$()` | Where `work.conf` is: `--config`, then `AUTOMATEWORK_CONF`, then the work folder |
| `WORKCONF.SETTINGS(path$)` | The settings; a missing file is an error that says what to do |
| `WORKCONF.VALUE(cfg, key$, fallback)` | A value by its dotted key |
| `WORKCONF.FOLDER$(cfg, key$, fallback$)` | A folder setting with `~` turned into the home folder |
| `WORKCONF.FLAG(name$)` | Whether `--name` is on the command line |
| `WORKCONF.ARG$(name$)` | The word after `--name`, or `""` |
| `WORKCONF.EXPAND$(path$)` | A path with `~` expanded |
| `WORKCONF.HOME$()` | The home folder |

### LOGGER: a diary for programs

| Function | What it does |
|---|---|
| `LOGGER.NEW(name$)` | A new logger |
| `LOGGER.LEVEL(lg, level$)` | Drops what lies below `"DEBUG"`, `"INFO"`, `"WARN"` or `"ERROR"` |
| `LOGGER.TO_CONSOLE(lg, colour = TRUE)` | Writes to the console |
| `LOGGER.TO_FILE(lg, path$, max_bytes = 0, keep = 3)` | Writes to a file, starting a new one at `max_bytes` |
| `LOGGER.TO_JSONL(lg, path$, max_bytes = 0, keep = 3)` | Writes one JSON object per line |
| `LOGGER.DEBUG(lg, message$, fields = 0)` | A line of the level DEBUG, with a map of fields |
| `LOGGER.INFO(lg, message$, fields = 0)` | The same for INFO |
| `LOGGER.WARN(lg, message$, fields = 0)` | The same for WARN |
| `LOGGER.ERROR(lg, message$, fields = 0)` | The same for ERROR |

`LOGGER.TO_FILE` does not create the folder of the file; `MKDIR` it
first.

### DT: dates as numbers

DT counts time in seconds since 1 January 1970, UTC. `offset` is the
offset from UTC in hours that the wall clock uses; `DT.LOCAL_OFFSET()`
gives the computer's own.

| Function | What it does |
|---|---|
| `DT.NOW()` | This moment |
| `DT.PARSE(text$, offset = 0)` | ISO dates and times, RFC 2822, `day.month.year`; `NONE` when it cannot read it |
| `DT.MAKE(yy, mm, dd, hh = 0, mi = 0, ss = 0, offset = 0)` | An instant from its parts |
| `DT.PARTS(t, offset = 0)` | The parts as a map: year, month, day, hour and more |
| `DT.FMT$(t, fmt$, offset = 0)` | Text with `%Y %m %d %H %M %S %A %B` and more |
| `DT.ISO$(t, offset = 0)` | `2026-09-12T15:00:00+02:00` |
| `DT.WEEKDAY$(t, lang$ = "en", offset = 0)` | The name of the day |
| `DT.ADD(t, spec$, offset = 0)` | Moved by `"+1 month -2 days"` or `"2w"` |
| `DT.DIFF(a, b, unit$, offset = 0)` | From `a` to `b` in seconds, minutes, hours, days, weeks, months or years |
| `DT.STARTOF(t, unit$, offset = 0)` | The start of its day, week, month, quarter or year |
| `DT.ENDOF(t, unit$, offset = 0)` | The last second of it |
| `DT.ISOWEEK(t, offset = 0)` | A map with `"year"` and `"week"` |
| `DT.IS_LEAP(yy)`, `DT.DAYS_IN(yy, mm)` | Leap years and the length of a month |
| `DT.LOCAL_OFFSET()` | The computer's offset from UTC in hours |

### DF: tables

| Function | What it does |
|---|---|
| `DF.READCSV(path$, delim$ = ",", types = 0)` | A CSV file with a header line as a table |
| `DF.FROM(matrix, names)` | A table from rows and column names |
| `DF.FROMROWS(rows)` | A table from an array of maps |
| `DF.NAMES(df)`, `DF.NROWS(df)` | The column names, the number of rows |
| `DF.COL(df, name$)` | One column as an array |
| `DF.ROW(df, i)` | One row as a map |
| `DF.WHERE(df, mask)` | The rows where the mask is true |
| `DF.SORT(df, name$, desc = FALSE)` | Sorted by a column |
| `DF.GROUPBY(df, keys, aggs)` | One row per key, with sums, counts or means |
| `DF.WRITECSV(df, path$, delim$ = ",")` | Writes a CSV file |
| `DF.TEXT$(df, n_rows = 10, opts = 0)` | The first rows as a text table |

### XLSX: Excel files

| Function | What it does |
|---|---|
| `XLSX.NEW()` | A new workbook |
| `XLSX.SHEET(wb, name$, rows, header = TRUE)` | A sheet from rows; the first row is bold |
| `XLSX.COLUMN(s, col, opts)` | Width and number format of a column, by letter or from 1 |
| `XLSX.STYLE(s, ref$, opts)` | Fill, bold and format of a cell or a range |
| `XLSX.FREEZE(s, rows, cols = 0)` | Keeps the first rows and columns in view |
| `XLSX.WRITE(path$, wb)` | Writes the file |
| `XLSX.READ(path$)` | Every sheet as rows: a map from sheet name to rows |
| `XLSX.SHEETS(path$)` | The names of the sheets |

`XLSX.WRITE` takes the path first, `DOCX.WRITE` the document first.
Dates come out of `XLSX.READ` as the serial number Excel stores.

### DOCX: Word documents

| Function | What it does |
|---|---|
| `DOCX.NEW()` | A new document |
| `DOCX.TITLE(doc, text$)` | The title in the document properties |
| `DOCX.LANGUAGE(doc, tag$)` | The language of the spelling check, such as `"en-US"` |
| `DOCX.HEADING(doc, text$, level = 1)` | A heading of level 1 to 3 |
| `DOCX.PARAGRAPH(doc, text$, align$ = "")` | A paragraph; `**bold**` and `*italic*` are kept |
| `DOCX.BULLETS(doc, items)`, `DOCX.NUMBERED(doc, items)` | A list |
| `DOCX.TABLE(doc, rows, header = TRUE)` | A table with grid lines |
| `DOCX.CODE(doc, text$)` | A code listing |
| `DOCX.WRITE(doc, path$)` | Writes the file |
| `DOCX.PLACEHOLDERS(path$)` | The `{{name}}` holes of a template |
| `DOCX.FILL(src_path$, dst_path$, values)` | A copy with the holes filled from a map |
| `DOCX.READ(path$)` | A map with the `"paragraphs"` and `"tables"` of a file |
| `DOCX.PARAGRAPHS(path$)`, `DOCX.TABLES(path$)` | One of the two |

### PDFGEN: PDF files

Positions and sizes are millimetres from the top left corner of the
page.

| Function | What it does |
|---|---|
| `PDFGEN.DOC(opts = 0)` | A new document; `opts` may set `"size"`, `"margin"` and more |
| `PDFGEN.ADDPAGE(doc)` | A new page |
| `PDFGEN.SETINFO(doc, title$, author$ = "", subject$ = "")` | The document properties |
| `PDFGEN.ADDFONT(doc, family$, path$, style$ = "")` | Embeds a TrueType font |
| `PDFGEN.USEFONT(doc, family$, style$ = "", size = 0)` | Chooses a font, its style and size |
| `PDFGEN.SETCOLOR(doc, r, g, b)` | The colour of text |
| `PDFGEN.SETFILL(doc, r, g, b)`, `PDFGEN.SETDRAW(doc, r, g, b)` | The colours of fills and lines |
| `PDFGEN.SETLINEWIDTH(doc, mm)` | The width of lines |
| `PDFGEN.TEXTAT(doc, x, y, text$)` | Text with its baseline at `x`, `y` |
| `PDFGEN.SETXY(doc, x, y)`, `PDFGEN.POSY(doc)` | Sets and reads the current position |
| `PDFGEN.LN(doc, h)` | To the left margin, `h` further down |
| `PDFGEN.CELL(doc, w, h, text$, align$ = "L", border = 0, fill = 0)` | One line of text in a box |
| `PDFGEN.MULTICELL(doc, w, lineh, text$, align$ = "L")` | A wrapped paragraph |
| `PDFGEN.SPLITLINES(doc, text$, w)` | The lines a text breaks into |
| `PDFGEN.FIT$(doc, text$, w)` | A text cut to fit a width |
| `PDFGEN.DRAWLINE(doc, x1, y1, x2, y2)` | A line |
| `PDFGEN.BOX(doc, x, y, w, h, style$ = "D")` | A rectangle, drawn, filled or both |
| `PDFGEN.IMAGE(doc, path$, x, y, w = 0, h = 0)` | A JPEG picture |
| `PDFGEN.TABLE(doc, rows, opts = 0)` | A table across pages |
| `PDFGEN.PAGECOUNT(doc)`, `PDFGEN.PAGEWIDTH(doc)` | The number of pages, the width |
| `PDFGEN.WRITEFILE(doc, path$)` | Writes the file |
| `PDFGEN.BUILD$(doc)` | The file as bytes, to attach to a mail |

### MAIL: messages

| Function | What it does |
|---|---|
| `MAIL.MESSAGE(from$, recipients, subject$)` | A new message; recipients as text or array |
| `MAIL.PLAIN(msg, text$)`, `MAIL.HTMLBODY(msg, html$)` | The text, and an HTML version |
| `MAIL.CC(msg, recipients)` | Copies |
| `MAIL.HEADER(msg, name$, value$)` | Any other header |
| `MAIL.SETDATE(msg, date$)`, `MAIL.SETID(msg, id$)` | A fixed date and message id, for tests |
| `MAIL.ATTACH(msg, path$, name$ = "", mime$ = "")` | Attaches a file |
| `MAIL.ATTACHDATA(msg, name$, data$, mime$ = "")` | Attaches bytes held in a string |
| `MAIL.INLINE(msg, path$, cid$)` | A picture the HTML shows |
| `MAIL.BUILD$(msg)` | The message as text |
| `MAIL.WRITEEML(msg, path$)` | Writes an `.eml` file any mail program opens |
| `MAIL.SEND(msg, server)` | Sends through curl; a map with `"ok"` |
| `MAIL.COMMAND(msg, server, eml_path$, config_path$)` | The curl arguments that would send it |
| `MAIL.PARSE(eml$)`, `MAIL.PARSEFILE(path$)` | A message read back into a map |
| `MAIL.HEADERVALUE$(parsed, name$)` | One header of a read message |
| `MAIL.SAVEATTACHMENT(parsed, index, path$)` | Saves an attachment |
| `MAIL.BAREADDRESS$(address$)`, `MAIL.ADDRESSNAME$(address$)` | The two parts of `"Name <address>"` |

### ICAL: calendars

| Function | What it does |
|---|---|
| `ICAL.PARSEFILE(file_path$)`, `ICAL.PARSE(src$)` | Reads an `.ics` calendar |
| `ICAL.EVENTS(cal)` | Its events |
| `ICAL.PROP$(cal, comp, prop_name$, fallback$ = "")` | A property such as `"SUMMARY"` |
| `ICAL.VALUES(cal, comp, prop_name$)` | Every value of a property that repeats |
| `ICAL.ALLDAY(cal, comp)` | Whether an event lasts whole days |
| `ICAL.STARTAT(cal, comp)`, `ICAL.ENDAT(cal, comp)` | Start and end as DT instants |
| `ICAL.OCCURRENCES(cal, comp, t_from, t_to)` | The starts of a repeating event in a range |
| `ICAL.EXPAND(cal, t_from, t_to)` | Every event in a range as a DF table |
| `ICAL.NEW(prodid$)` | A new calendar |
| `ICAL.ADDEVENT(cal, summary$, first_start, finish, zone$ = "")` | Adds an event |
| `ICAL.ADDALLDAY(cal, summary$, day_start, days = 1)` | Adds an all-day event |
| `ICAL.ADDALARM(cal, comp, minutes_before, text_in$)` | A reminder |
| `ICAL.SETPROP(cal, comp, prop_name$, value$, params$ = "")` | Sets a property as written |
| `ICAL.SETTEXT(cal, comp, prop_name$, text_in$)` | Sets a text property |
| `ICAL.TEXT$(cal)`, `ICAL.WRITEFILE(cal, file_path$)` | The calendar as text, or as a file |

### SCHED: when something runs

| Function | What it does |
|---|---|
| `SCHED.VALID(when$)` | Whether a schedule can be read |
| `SCHED.PROBLEM$(when$)` | What is wrong with it |
| `SCHED.NEXTRUN(when$, t, offset = 0)` | The next run after `t` |
| `SCHED.PREVRUN(when$, t, offset = 0)` | The last run before `t` |
| `SCHED.UPCOMING(when$, t, n, offset = 0)` | The next `n` runs |
| `SCHED.JOB(name$, when$, fn, opts = 0)` | A job for the loop |
| `SCHED.SERVE(end_at)` | Runs the jobs until `end_at`, 0 for no end |

A schedule is a cron line such as `"30 7 * * mon-fri"`, a daily time
such as `"daily 06:00"`, or an interval such as `"every 5 minutes"`.

### TMPL: text with holes

| Function | What it does |
|---|---|
| `TMPL.RENDER$(path$, model)` | A template file filled from a map |
| `TMPL.RENDERSTR$(src$, model)` | The same for a template in a text |

`{{ name }}` is a hole, `{{{ html }}}` one whose text is not escaped,
and `{% if %}` and `{% for x in items %}` repeat or leave out parts.

## The further libraries

Each of these is used by a few recipes; the recipes named show how.

| Library | Functions the recipes call | Recipes |
|---|---|---|
| CACHE | `NEW`, `FETCH`, `PUT`, `HAS`, `SAVE`, `LOAD`: values kept for a time, on disk | X05, X11 |
| CLI | `NEW`, `FLAG`, `OPT`, `ARG`, `MANY`, `CMD`, `PARSE`, `HELP$`, `STATUS`, `DONE`: command lines with options | M13, M14 |
| DB | `OPEN`, `CREATE`, `INSERT`, `INSERTMANY`, `FROM`, `WHERE`, `ORDERBY`, `ROWS`, `CLOSE`: SQLite without SQL | X09 |
| FUZZY | `SCORE(a$, b$)`: how alike two names are, 0 to 100 | M11 |
| HTMLDOM | `PARSE`, `FIND(doc, selector$)`, `ATTR$`, `FLAT$`, `ESCAPE$`: web pages read by CSS selector | X03, X11 |
| JDWEB | `GET`, `POST`, `PATCH`, `ASSETS`, `MOUNT`, `LISTEN`, `SERVE`, `REPLY`, `REPLY_JSON`, `PARAM$`, `FETCH`: small web servers on this computer | M08, X03, X15 |
| JWT | `SIGN$`, `VERIFY`, `DECODE`: signed tokens, behind AUDIT | X01, X14 |
| LLMAPI | `NEW(provider$, model$, api_key$, base_url$)`, `SYSTEM`, `SET`, `JSON`: language models, local or online | X02 |
| METRICS | `NEWGAUGE`, `SETVALUE`, `EXPOSE$`, `RESET`: numbers for monitoring | X13 |
| OAUTH | `NEW`, `SETOPT`, `DEVICESTART`, `DEVICEPOLL`, `SESSION`, `GET`, `STORE`, `CACHEOF`, `EXPIRE`: signing in to web services | X05 |
| PKG | `COMPARE(a$, b$)`: -1, 0 or 1 as version `a$` is older, the same or newer | X07 |
| POOL | `CREATE`, `SPAWN`, `JOBS`, `RESULTS`, `TAKE_JOB`, `DONE`, `FAILED`, `GATHER`, `VALUE`, `SHUTDOWN`: work spread over all cores | X08 |
| PROPTEST | `FORALL(name$, gen$, prop)`: a property checked on many random cases | X12 |
| QR | `MATRIX`, `TEXT$`, `TOPDF`, `INFO`: QR codes | E15 |
| REQ | `NEW`, `HEADER`, `BEARER`, `TIMEOUT`, `RETRIES`, `GET`, `POST`, `PATCH`, `JSON`, `RAISE_FOR_STATUS`, `QUERY$`: HTTP with sessions | X04, X05, X11 |
| RETRY | `RUN(fn, arg, opts)`, `ATTEMPT`, `TRANSIENT`: trying again with growing pauses | X01, X02, X05, X07, X09, X11, X13 |
| SEARCH | `INDEX`, `ADD`, `REMOVE`, `QUERY`, `DOCCOUNT`, `WRITEFILE`, `READFILE`: full-text search | X03 |
| SECRET | `TOKEN$`: random tokens and keys | X14 |
| SVG | `CHART(kind$, w_px, h_px)`, `SETOPT`, `LABELS`, `SERIES`, `RENDER`, `MARKUP$`, `WRITEFILE`: charts | M08, M09, X09, X15 |
| TESTKIT | `SUITE`, `EQ`, `NE`, `EQARRAY`, `ISTRUE`, `ISFALSE`, `NEAR`, `THROWS`, `SKIP`, `REPORT`: the tests of every recipe | all |
| TEXTDIFF | `TOKENS`, `OPCODES`, `RATIO`, `WORDDIFF$`: what changed between two texts | E14, M14 |
| VALID | `ISIBAN`, `IBAN$`: checks for bank account numbers | M11 |

## The book's own modules

These live in `recipes/lib`, and the wizard copies them into your work
folder.

| Module | Functions | Used by |
|---|---|---|
| WORKCONF | See above | every recipe |
| OUTBOX | `PUT$(msg, cfg, file_name$)`, `PENDING(cfg)`, `ASK$(cfg)`, `SEND_ALL(cfg, password$)`, `FOLDER$`, `FROM$`, `SERVER`, `SEND_ARGS`: the mail rule of Chapter 4 | M01, M03, M06, M08, M15, X02, X09, X10, X12, X13, X15 |
| VAULT | `PUT(target$, user$, secret$)`, `GET$(target$)`, `HAS`, `USER$`, `DELETE`, `FIND$(target$, env$, prompt$)`, `ASK$(prompt$)`: secrets in the Windows Credential Manager | OUTBOX, X01, X13, X14 |
| JOBSTATE | `JOBS(path$)`, `READ`, `WRITE`, `HEARTBEAT_AGE`, and the paths `FOLDER$`, `JOBS_PATH$`, `STATE_PATH$`, `LOG_PATH$`, `LOCK_PATH$`: the files of the job server | X01, X13, X14 |
| AUDIT | `APPEND(path$, key$, fields)`, `LINES`, `ENTRIES`, `VERIFY(path$, key$)`: a signed record of every run | X01, X14 |
| INPUTKIT | `PARSE`, `PLAN_LINES`, `COUNTDOWN`, `RUN_STEPS`, `ACTIVATE`, `TYPETEXT`, `PRESS`, `CLICK`: keyboard and mouse for programs without another way in, with an emergency stop | the bonus recipes |

`OUTBOX.ASK$` takes the mail password from the Credential Manager when
you stored it there as `AutomateWork/mail`, and otherwise asks for it
with a prompt that prints stars.
