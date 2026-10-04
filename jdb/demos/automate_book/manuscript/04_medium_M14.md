<!-- pagebreak -->

## M14 Contract Diff

### The chore

A supplier sends Jonas the new version of a framework contract. "Only
small changes," the mail says. The supplier switched off track changes,
and Word's own comparison marks every renumbered clause and every
changed format until the page is more red than black. So Jonas prints
both versions and reads them side by side with a ruler. Twelve pages,
an hour, and the nagging feeling that he missed the one sentence that
counted.

### What you get

Give the recipe the old and the new version of a Word document. It
reads both, compares them paragraph by paragraph and tells you what
happened to each paragraph that is not the same in both:

- **changed**: the paragraph is still there, with some words different.
  The report marks the words: `[-100-]` was removed, `{+120+}` was
  added.
- **added** and **removed**: a paragraph that exists in only one of the
  versions.
- **moved**: the same text at a different place.

The report is printed and saved as a Word file you can forward, with a
heading per change and the text before and after.

### Before you start

The setup wizard has run. You need both versions as `.docx` files; an
old `.doc` or a PDF has to be saved as `.docx` in Word first. The
recipe does not change either file.

### The program

The program reads the two file names from the command line and leaves
the comparison to the module:

<!-- include recipes/medium/M14_contract_diff/contract_diff.jdb -->

The module reads the paragraphs, pairs them up and writes the report:

<!-- include recipes/medium/M14_contract_diff/contractdiff.jdb -->

### How it works

1. **From a file to paragraphs.** `DOCX.PARAGRAPHS` answers the text of
   every paragraph in the document, without the formatting. `PARAGRAPHS`
   trims each one with a `SELECT` and leaves out the empty ones with a
   `FILTER`, so an extra blank line in the new version is no change.

2. **What both versions share.** `TEXTDIFF.OPCODES` is the same
   comparison that programmers use for source code, applied to
   paragraphs instead of lines. It answers a list of steps: this run of
   paragraphs is equal in both, that run was replaced by another one.
   `Unmatched` keeps only what is not equal: the paragraphs that left
   the old version (`gone`) and those that came into the new one
   (`came`), each with its number.

3. **Pairing the leftovers.** A paragraph with one word changed shows up
   twice in that list, once as gone and once as came. `COMPARE` pairs
   them up again. For every gone paragraph it looks for the came
   paragraph that is most alike. `LIKENESS` measures that with
   `TEXTDIFF.RATIO` over the words: 1 means the same words in the same
   order, 0 means nothing in common. If the best match is the same
   text, the paragraph moved. If it reaches the `similar` threshold
   (0.6 unless you change it), the paragraph changed. Otherwise the old
   paragraph counts as removed, and every came paragraph without a
   partner counts as added.

4. **Marking the words.** For a changed paragraph, `Change` asks
   `TEXTDIFF.WORDDIFF$` for the words that differ. That is the line
   with `[-` and `{+` you see in the report.

5. **In reading order.** `SortChanges` puts the changes in the order of
   the new version, so you can read the report next to the new
   document. `OrderKey` gives each change its place: its paragraph
   number in the new version, or, for a removed paragraph, a place
   just before where its old number would be. A `SELECT` takes the
   place of every change, `GRADE` answers their order, and a second
   `SELECT` picks the changes in that order.

6. **Two reports from one list.** `REPORT$` writes the text you see on
   the screen and `REPORT_DOCX` the Word file. Both read the same list
   of changes, so they always agree.

### Run it

The recipe's test uses a short offer in two versions. Run on those two
files, it prints:

```
jdbasic contract_diff.jdb "offer v1.docx" "offer v2.docx"
2 changed, 1 added, 1 removed, 0 moved

Paragraph 3 changed
  The price is [-100-]{+120+} EUR per month.

Paragraph 4 removed
  - Payment within 30 days.

Paragraph 4 changed
  Support by mail {+and phone +}on working days.

Paragraph 5 added
  + A setup fee of 300 EUR applies.

Report: C:\Users\jonas\Documents\AutomateWork\reports\
        changes offer v1 to offer v2.docx
```

The price clause swapped places with the term and got a new price at
the same time. The recipe reports it once, as changed, under its
number in the new version. The term itself is reported as nothing: its
text is the same, and the price moving past it is enough. With
`--dry-run` the recipe prints the report and writes no file. When the
two versions are the same, it says `0 changed, 0 added, 0 removed,
0 moved` and writes nothing either.

### Schedule it

Contracts arrive when they arrive, so the wizard plans no task for this
recipe. Keep the command at hand instead. In Explorer, select both
files, hold Shift, right-click and choose *Copy as path*; paste that
after `jdbasic contract_diff.jdb` and the two names are in place.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[contract_diff]`:

```toml
[contract_diff]
reports = "~/Documents/AutomateWork/reports"
similar_percent = 60
```

`similar_percent` decides when two paragraphs are one changed paragraph
and when they are a removed and an added one. Raise it to 80 for legal
texts, where a paragraph with half its words different deserves to be
read as new. Lower it to 40 if the report shows too many removed and
added pairs that are really the same clause rewritten.

Three changes in the code are worth knowing:

- **Leave out page lines.** Some documents carry "Page 3 of 12" as
  paragraphs, and every new page in the new version then counts as a
  change. In `PARAGRAPHS`, change the `FILTER` that keeps a paragraph
  into:

  ```basic
  RETURN FILTER(LAMBDA p$ -> p$ <> "" ANDALSO _
      NOT STARTSWITH(p$, "Page "), cleaned)
  ```

- **Send the report to legal.** Put the report into the outbox, by the
  outbox rule of this chapter. Add `OUTBOX, MAIL` to the `IMPORT` line
  of the program, and these lines after `PRINT "Report: "; report$`:

  ```basic
  DIM subject$ = "Changes " + name1$ + " to " + name2$
  DIM msg = MAIL.MESSAGE(OUTBOX.FROM$(cfg), "legal@example.com", _
      subject$)
  MAIL.PLAIN(msg, CONTRACTDIFF.REPORT$(changes))
  MAIL.ATTACH(msg, report$)
  PRINT "In the outbox: "; OUTBOX.PUT$(msg, cfg, subject$ + ".eml")
  ```

- **Leave out what only moved.** When the order of the clauses does
  not matter to you, filter the list right after `COMPARE` in the
  program:

  ```basic
  changes = FILTER(LAMBDA c -> c{"kind"} <> "moved", changes)
  ```

### When it goes wrong

- **"No such document"**: a file name with spaces needs quotes around
  it, as in the example above.
- **Everything is removed and added**: the new version is a scan or was
  pasted as one long paragraph. Compare the paragraph counts: open both
  files and look at the pilcrow marks (*Show all* in Word's *Home*
  ribbon).
- **A table cell change is missing**: the recipe compares paragraphs of
  the main text. Text inside tables, headers and footers is not part
  of `DOCX.PARAGRAPHS`.
- **Many small changes in one long paragraph**: the marked line gets
  hard to read. Open the Word report; it shows the paragraph before and
  after in full, below the marked line.

> **Balance dividend**
> About 30 minutes a week for a team lead who signs contracts, and a
> report the lawyer can read without you.
