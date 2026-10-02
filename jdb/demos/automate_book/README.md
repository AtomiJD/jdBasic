# Automate Your Work for a Better Life Balance

The manuscript, the recipes and the build of a book about automating office
work with jdBasic: 48 programs in three levels and a setup wizard for
readers who have never programmed.

The plan, the outline and the page budget are in [PLAN.md](PLAN.md).

## State

Work packages 0 to 5 are done: all six chapters with 48 recipes, the
appendices and the index, 655 pages in print. The build turns the
manuscript into a
Word file, an HTML preview, a print PDF (17 x 24 cm, fonts embedded) and a
screen PDF with bookmarks and links. Recipe E01 with its chapter pages is
the sample. The build also draws the cover (front, and the print wrap
with a spine as wide as the page count) and writes an EPUB 3 e-book.

## Layout

```
book.toml          title, authors, page size, chapter order
manuscript/        the chapters in Markdown, img/ for their figures
recipes/           the programs: lib/ shared modules (workconf, outbox),
                   easy/ medium/ expert/, template/ for your own recipe,
                   each with recipe.toml for the wizard
wizard/            setup wizard: wizard.jdb (the work), setup_console.jdb,
                   setup_wizard.jdb (window, FORMS build)
tools/             build_book, check_recipes, mdbook, prose, layout, cover, epub,
                   preview (most with a test), figures/, preview/render.html
fonts/             Source Serif 4 and Source Code Pro (SIL Open Font License)
out/               build output, not in git
```

## Building

Run from this folder; the tools import modules from `lib/` and
`recipes/lib/`:

```
set JDBASIC_PATH=C:\path\to\jdBasic\lib;C:\path\to\jdBasic\jdb\demos\automate_book\recipes\lib
jdbasic tools/build_book.jdb             # out/: docx, html/, print and screen PDF, covers, epub
jdbasic tools/check_recipes.jdb          # every recipe: lint, test, -c, test as .exe
jdbasic tools/mdbook_test.jdb            # the Markdown reader (also prose_, layout_, cover_, epub_test)
jdbasic tools/pdf_preview.jdb out/book_print.pdf 1-4   # pages as PNG, needs Chrome
jdbasic tools/figures/e01_before_after.jdb   # redraws a figure
jdbasic wizard/wizard_test.jdb           # the wizard core: conf, schedules, install
jdbasic wizard/setup_wizard.jdb --selftest   # the window wizard clicks through itself
```

`check_recipes` sets `JDBASIC_PATH` for the programs it starts and looks for
the compiler at `../../../build/jdBasic.exe`; `--jdbasic path` names another.

## Writing rules

- A listing is included from its tested file with
  `<!-- include recipes/... -->`, never pasted.
- A note box is a quote whose first line is bold: `> **Balance dividend**`.
- `<!-- pagebreak -->` and `<!-- toc -->` place a page break and the table
  of contents.
- Italics with `*stars*`, not underscores.
- The build stops when a chapter, a comment in an included listing, PLAN.md
  or this README breaks the writing rules in `tools/prose.jdb`: no dash as
  punctuation, no ellipsis character, no marketing words, no slogan
  repetitions, no "not just X" contrasts.
