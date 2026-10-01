# Automate Your Work for a Better Life Balance

The manuscript, the recipes and the build of a book about automating office
work with jdBasic: 45 programs in three levels and a setup wizard for
readers who have never programmed.

The plan, the outline and the page budget are in [PLAN.md](PLAN.md).

## State

Work package 0a is done: the build turns the manuscript into a Word file
and an HTML preview, and recipe E01 with its chapter pages is the sample.
The PDF, EPUB and cover outputs follow in work packages 0b and 0c.

## Layout

```
book.toml          title, authors, page size, chapter order
manuscript/        the chapters in Markdown, img/ for their figures
recipes/           the programs: lib/ shared modules, easy/ medium/ expert/
tools/             build_book, check_recipes, mdbook (with its test), figures/
fonts/             Source Serif 4 and Source Code Pro (SIL Open Font License)
out/               build output, not in git
```

## Building

Run from this folder; the tools import modules from `lib/` and
`recipes/lib/`:

```
set JDBASIC_PATH=C:\path\to\jdBasic\lib;C:\path\to\jdBasic\jdb\demos\automate_book\recipes\lib
jdbasic tools/build_book.jdb             # out/book.docx, out/html/, page report
jdbasic tools/check_recipes.jdb          # every recipe: lint, test, -c, test as .exe
jdbasic tools/mdbook_test.jdb            # the Markdown reader
jdbasic tools/figures/e01_before_after.jdb   # redraws a figure
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
