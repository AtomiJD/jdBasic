# TEXTDIFF - line and word diffs, patches, snapshots

`lib/textdiff.jdb` shows what changed between two texts. It finds the
matching runs and the edit steps the way Python's `difflib.SequenceMatcher`
does, on lines, words or characters; writes unified diffs the way `diff -u`
and `difflib.unified_diff` write them and applies such a patch back; marks
changed words or characters inline; and checks a text against a stored
snapshot in a test.

The module is called TEXTDIFF because `DIFF` is the array builtin.

Stands in for: difflib, patch, snapshot testing.

## Quick start

```basic
IMPORT TEXTDIFF

DIM patch$ = TEXTDIFF.UNIFIED$(old$, new$, "app.ini", "app.ini (new)")
PRINT patch$
PRINT TEXTDIFF.APPLY$(old$, patch$) = new$                             ' TRUE

PRINT TEXTDIFF.WORDDIFF$("the quick brown fox", "the quick red fox")   ' the quick [-brown-]{+red+} fox
PRINT TEXTDIFF.CHARDIFF$("Müller", "Mueller")                          ' M[-ü-]{+ue+}ller

DIM steps = TEXTDIFF.OPCODES(TEXTDIFF.LINES(old$), TEXTDIFF.LINES(new$))
PRINT TEXTDIFF.RATIO(TEXTDIFF.CHARS("abcd"), TEXTDIFF.CHARS("bcde"))    ' 0.75

TESTKIT.ISTRUE(TEXTDIFF.SNAPSHOT(report$, "tests/snapshots/report.txt"), "report unchanged")
```

## API

### Sequences

| Call | What it does |
|------|--------------|
| `LINES(text$)` | The lines of a text; a line break at the very end starts no extra line. |
| `TOKENS(text$)` | The words and the whitespace between them, in order; joined they give the text back. |
| `CHARS(text$)` | The characters of a text. |

### Comparing sequences

These take two arrays of strings, usually from LINES, TOKENS or CHARS.
`autojunk` (1) is difflib's heuristic: in a sequence of 200 elements or
more, an element that makes up more than 1% of it cannot start a match.

| Call | What it does |
|------|--------------|
| `MATCHES(a, b, [autojunk])` | The runs both share, as maps with `a`, `b` and `size`, in order; the last map is always `(LEN(a), LEN(b), 0)`. |
| `OPCODES(a, b, [autojunk])` | How to turn `a` into `b`, as maps with `tag` (`equal`, `replace`, `delete`, `insert`), `a1`, `a2`, `b1`, `b2`: `a[a1]` up to `a[a2 - 1]` becomes `b[b1]` up to `b[b2 - 1]`. |
| `RATIO(a, b, [autojunk])` | Similarity from 0 to 1: twice the matched elements over all elements; 1 for two empty sequences. |

### Patches

| Call | What it does |
|------|--------------|
| `UNIFIED$(old$, new$, [from$], [to$], [context])` | A unified diff line by line, with `context` (3) unchanged lines around each change and the headers `--- from$` and `+++ to$` (`a`, `b`); empty when the texts are equal. A text that does not end with a line break gets the `\ No newline at end of file` line. |
| `APPLY$(old$, patch$)` | The new text from the old one and a unified diff. Every context and removed line must match the old text, or the call throws; lines outside the hunks, such as the headers, are ignored. |

### Inline diffs and snapshots

| Call | What it does |
|------|--------------|
| `WORDDIFF$(old$, new$)` | The text with removed words in `[-...-]` and added words in `{+...+}`, the marks of `git diff --word-diff=plain`. |
| `CHARDIFF$(old$, new$)` | The same marks on single characters. |
| `SNAPSHOT(actual$, path$)` | Whether a text equals the snapshot stored at `path$`. A missing snapshot is written and counts as equal; with the environment variable `TEXTDIFF_UPDATE` set, every snapshot is written again. A different text prints the unified diff from the snapshot to the text and answers FALSE. |

## Notes

- The matching is difflib's, not the shortest edit script of `diff`: it
  looks for the longest run the two sequences share and repeats on both
  sides of it, which gives diffs that read the way people expect. The
  opcodes and ratios match difflib's exactly.
- A patch from UNIFIED$ applies with APPLY$ for any context width,
  including 0, and keeps or drops the final line break of the new text.
- Characters count as characters: an umlaut is one element in CHARS.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/textdiff_selftest.jdb`, 366 assertions: opcodes,
ratios and unified diffs of 26 cases against difflib of Python 3.14
(`tests/jdlibs/fixtures/textdiff_reference.tsv`), and 180 patches written
and applied back to 60 seeded random pairs.
Demo: `jdb/demos/jdlibs/textdiff_demo.jdb`.
