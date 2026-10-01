# Automate Your Work for a Better Life Balance

The manuscript, the recipes and the build of a book about automating office
work with jdBasic: 45 programs in three levels and a setup wizard for
readers who have never programmed.

The plan, the outline and the page budget are in [PLAN.md](PLAN.md).

## State

Planning. Decisions are in PLAN.md section 10. The folders listed in
section 5 are created as the work packages reach them.

## Building (once work package 0 is done)

```
jdbasic tools/build_book.jdb            # out/: book.docx, book_print.pdf, book_screen.pdf, book.epub, html/
jdbasic tools/page_count.jdb            # page estimate per chapter
jdbasic tools/check_recipes.jdb         # every recipe: lint, test, -c, test as .exe
```

Run from this folder. The tools import modules from `lib/`, so
`JDBASIC_PATH` has to point there.
