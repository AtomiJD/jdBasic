# TEXTX - wrap, slug, transliterate, humanize

`lib/textx.jdb` holds the small text helpers every project writes again:
paragraphs wrapped and shortened the way Python's textwrap does it, cuts
that count characters rather than bytes, slugs and ASCII transliteration
the way python-slugify and Unidecode write them, and the human forms of
sizes, large numbers, ordinals and durations the way humanize writes them,
in English and German.

Stands in for: textwrap, python-slugify, Unidecode, humanize.

## Quick start

```basic
IMPORT TEXTX

PRINT TEXTX.FILL$("The office stays closed on Friday. Orders placed after noon ship on Monday.", 30)
PRINT TEXTX.SHORTEN$("Hello  world, and everyone in it!", 20)     ' Hello world, [...]
PRINT TEXTX.ELLIPSIS$("Grüße aus Köln", 10)                      ' Grüße aus…

PRINT TEXTX.SLUG$("Grüße aus Köln: Äpfel & Öl")                  ' grusse-aus-koln-apfel-ol
PRINT TEXTX.SLUG$("Grüße aus Köln: Äpfel & Öl", "-", 0, 1)       ' gruesse-aus-koeln-aepfel-oel
PRINT TEXTX.ASCII$("Łódź, Ærøskøbing, São Paulo")                ' Lodz, AEroskobing, Sao Paulo

PRINT TEXTX.FILESIZE$(48318382080)                               ' 48.3 GB
PRINT TEXTX.FILESIZE$(48318382080, "binary")                     ' 45.0 GiB
PRINT TEXTX.INTCOMMA$(1234567), TEXTX.INTCOMMA$(1234567, "de")   ' 1,234,567  1.234.567
PRINT TEXTX.INTWORD$(1234567, "de")                              ' 1,2 Millionen
PRINT TEXTX.ORDINAL$(22)                                         ' 22nd
PRINT TEXTX.DURATION$(40000000)                                  ' 1 year, 3 months
PRINT TEXTX.RELATIVE$(5400, "de")                                ' vor 2 Stunden
PRINT TEXTX.PRECISE$(3725)                                       ' 1 hour, 2 minutes and 5 seconds
```

## API

### Wrapping

| Call | What it does |
|------|--------------|
| `WRAP(text$, [width])` | The lines of a paragraph, none longer than `width` (70) characters: tabs expanded to the next multiple of eight, line breaks read as spaces, a line broken after the hyphen of a compound word (`self-` / `contained`), a word cut only when it is longer than a line. |
| `FILL$(text$, [width])` | WRAP joined with line breaks. |
| `SHORTEN$(text$, width, [placeholder$])` | The text on one line: whitespace collapsed, and when it is still longer than `width`, as many words as fit followed by `placeholder$` (`" [...]"`). A placeholder wider than the line throws. |
| `DEDENT$(text$)` | The whitespace all lines start with removed; lines of nothing but whitespace become empty. Tabs and spaces are different characters. |
| `INDENT$(text$, prefix$)` | `prefix$` in front of every line that holds more than whitespace; line ends are kept as they are. |

### Characters

| Call | What it does |
|------|--------------|
| `LENGTH(text$)` | The number of characters. |
| `HEAD$(text$, count_n)` | The first characters. |
| `TAIL$(text$, count_n)` | The last characters. |
| `SLICE$(text$, start, count_n)` | Characters from position `start`, counted from 0. |
| `ELLIPSIS$(text$, width, [mark$])` | The text cut to `width` characters with `mark$` (`"…"`) at the end when it is longer; spaces before the mark are dropped. |

### Slugs and ASCII

| Call | What it does |
|------|--------------|
| `ASCII$(text$, [german])` | The text in ASCII as Unidecode writes it: `ä` as `a`, `ß` as `ss`, `Æ` as `AE`, `Ł` as `L`, `€` as `EUR`, typographic quotes and dashes as their ASCII forms. With `german` true, umlauts become `ae`, `oe`, `ue`. |
| `SLUG$(text$, [sep$], [max_len], [german])` | A slug as python-slugify writes it: transliterated, lower case, every run of other characters a single `sep$` (`"-"`), apostrophes and the commas inside numbers dropped (`1,000,000` is `1000000`), the HTML entities `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;` and numeric ones decoded. `max_len` cuts the slug (0 for no limit); `german` spells umlauts as `ae`, `oe`, `ue`. |

### Human forms

| Call | What it does |
|------|--------------|
| `FILESIZE$(size_n, [style$])` | A number of bytes: `1.5 MB` with `"decimal"` (powers of 1000, the default), `1.4 MiB` with `"binary"`, `1.4M` with `"gnu"`; `1 Byte`, `999 Bytes`. |
| `INTCOMMA$(value, [lang$])` | A whole number with its thousands grouped: `1,234,567` in English, `1.234.567` in German. |
| `INTWORD$(value, [lang$])` | A large number in words: `1.2 million`, `3.0 trillion`; `1,2 Millionen`, `3,0 Billionen`. Below a thousand, the number itself. |
| `ORDINAL$(value, [lang$])` | `1st`, `2nd`, `3rd`, `11th`, `22nd`; `1.`, `22.` in German. |
| `DURATION$(seconds, [lang$])` | A duration rounded to the unit that makes sense: `a moment`, `45 seconds`, `an hour`, `2 hours`, `3 months`, `1 year, 3 months`; `ein Moment`, `2 Stunden`, `ein Jahr und 3 Monate`. Months count 30.5 days. The sign is ignored. |
| `RELATIVE$(seconds, [lang$], [future])` | A time that far in the past: `2 hours ago`, `vor 2 Stunden`; with `future` true `2 hours from now`, `2 Stunden ab jetzt`; below half a second `now`, `jetzt`. |
| `PRECISE$(seconds, [lang$], [min_unit$])` | Every unit down to `min_unit$` (`"seconds"`): `1 hour, 2 minutes and 5 seconds`, `1 Stunde, 2 Minuten und 5 Sekunden`. The smallest unit may carry two decimals (`2.08 minutes`). Units: `microseconds`, `milliseconds`, `seconds`, `minutes`, `hours`, `days`, `months`, `years`. |

`lang$` is `"en"` (the default) or `"de"`, in any spelling (`"DE-de"`);
another language throws.

## Notes

- Lengths, widths and cuts count characters: `LENGTH("Grüße")` is 5.
- WRAP and SHORTEN$ follow textwrap's defaults: tabs expanded, all ASCII
  whitespace read as spaces, long words broken, hyphenated words broken
  after the hyphen, whitespace at the ends of lines dropped.
- ASCII$ and SLUG$ know Latin-1, Latin Extended-A (Polish, Czech,
  Hungarian, Turkish letters and more) and common punctuation; other
  characters beyond ASCII are left out, where Unidecode would also
  transliterate Greek, Cyrillic or CJK.
- For relative times of calendar dates, `DT.HUMANIZE$` in the DT module
  takes two dates; RELATIVE$ takes a number of seconds.
- Three answers are deliberately not humanize 4.16's: its German catalogue
  writes one year and one month as `ein Monat` (TEXTX: `ein Jahr und ein
  Monat`) and leaves milliseconds untranslated in precisedelta (TEXTX:
  `Millisekunden`), and precisedelta with years as the smallest unit adds
  the rest as raw microseconds (TEXTX: `0 years`).
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/textx_selftest.jdb`, 505 assertions against
reference tables from textwrap (Python 3.14), python-slugify 9.0, Unidecode
1.4 and humanize 4.16 (`tests/jdlibs/fixtures/textx_*.tsv`).
Demo: `jdb/demos/jdlibs/textx_demo.jdb`.
