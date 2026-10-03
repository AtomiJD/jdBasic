<!-- pagebreak -->

## Bonus: M17 Slide Builder

### The chore

Every quarter Jonas presents the numbers of his team to the head of the
department. The content is ready in his notes long before the meeting:
a few points per topic, the revenue chart from the report, a table of
the regions. What takes the evening is PowerPoint: a new slide, the
title into the box, the points into the other box, the chart dragged
into place, the table built cell by cell, and the same again for every
slide.

### What you get

You write the presentation as a short outline in a text file, the way
you would write notes, and the Slide Builder turns it into a finished
PowerPoint file: a title slide, one slide per topic with its bullet
points, pictures and tables placed and sized, and your speaker notes
under each slide. If you keep the plan in Excel instead, one row per
slide works as well.

![A slide the Slide Builder made from three lines of the outline and a chart](img/m17_slides.png)

### Before you start

The setup wizard has run, and you have a folder for your outlines, by
default `slides` in your work folder. The recipe brings a sample,
`review.md`, with a chart next to it. You need PowerPoint, or any other
program that opens `.pptx` files, only to look at the result; the
Slide Builder itself writes the file without it.

An outline is a text file with a few kinds of lines:

<!-- include recipes/medium/M17_slide_builder/review.md -->

- `# Title` starts the title slide; the plain line below it is its
  subtitle.
- `## Title` starts a new slide.
- `- point` is a bullet point, and `  - point` with two blanks in front
  one level further in.
- `![text](chart.png)` puts a picture on the slide, from the folder of
  the outline.
- Lines between `|` signs make a table; the line of dashes under the
  first row is left out.
- `> text` is a speaker note, which you see in PowerPoint below the
  slide but the audience does not.

### The program

The program finds the outline or the sheet, reads it into slides,
shows them, and writes the file:

<!-- include recipes/medium/M17_slide_builder/slide_builder.jdb -->

The module reads both kinds of input into the same list of slides and
hands them to the library PPTX, which writes the PowerPoint file:

<!-- include recipes/medium/M17_slide_builder/slides.jdb -->

### How it works

1. `SLIDES.SOURCE_ARG$` takes the first word on the command line that
   is not an option. If there is no such file, the program looks for
   it in the folder from `work.conf`.
2. For a `.xlsx` file, `XLSX.READ` reads the workbook and
   `SLIDES.FROM_ROWS` takes one slide per row with a title. The columns
   are `Title`, `Points` (separated by `;`), `Picture` and `Notes`;
   the first row under the header becomes the title slide. Any other
   file is read as an outline by `SLIDES.FROM_OUTLINE`.
3. Both answer the slides and a list of mistakes, each with its line
   or row. A picture that is not there, or text before the first
   title, stops the program before anything is written.
4. `SLIDES.PLAN_LINES` prints one line per slide, which is all a dry
   run does.
5. `SLIDES.BUILD` creates the deck with `PPTX.NEW` and adds the slides
   one by one: `PPTX.TITLE_SLIDE` or `PPTX.SLIDE`, then `PPTX.IMAGE`,
   `PPTX.TABLE` and `PPTX.NOTES` for what the slide holds.
   `PPTX.WRITE` packs the XML parts of the presentation into the
   `.pptx` file, which is a ZIP archive like a Word file.
6. `SLIDES.FREE_PATH$` makes sure an older presentation is never
   overwritten: a second run writes `review (2).pptx`.

### Run it

Look at the slides first:

```
jdbasic slide_builder.jdb review.md --dry-run
1 Team Review, Fourth Quarter (title slide)
2 Highlights: 5 points, picture chart.png, notes
3 Revenue by region: 3 table rows, notes
4 Next quarter: 3 points
Nothing written (dry run).
```

Then without `--dry-run`. The program prints the same lines and the
name of the file it wrote, `review.pptx` next to the outline. Open it
in PowerPoint: four slides in 16:9, ready to present or to polish.

### Schedule it

The Slide Builder has no schedule; a presentation is due when it is
due. Start it by hand, or let a recipe that writes a report also write
the outline and its chart as a PNG picture, and run the Slide Builder
after it.

### Make it yours

The settings are the `[slide_builder]` part of `work.conf`:

```toml
[slide_builder]
folder = "~/Documents/AutomateWork/slides"
out_folder = ""
tab = ""
```

- **One folder for the results.** Set `out_folder` and every
  presentation lands there instead of next to its outline.
- **A plan in Excel.** Name the sheet in `tab` when the slides are not
  on the first one.
- **Your colours.** The library draws titles in `1F4E79`, a dark blue,
  and text in `333333`. Both are in `lib/pptx.jdb`, in `SlideXml$`
  and `BulletParas$`; change them there for every deck you build.
- **A slide of your own kind.** Add a line type to `FROM_OUTLINE`, for
  example `=== quote` for a slide with one large sentence, and a
  `PPTX` call for it in `BUILD`.

### When it goes wrong

- **"line 5: no picture chart.png"**: the picture is not in the folder
  of the outline. Copy it there, or write the path relative to the
  outline.
- **"text before the first title"**: the outline starts with a point
  or a sentence. Put a `#` or `##` title in front.
- **"the sheet has no column Title"**: the first row of the sheet must
  hold the column names. Add a header row.
- **PowerPoint says the file needs repair**: tell the authors, and
  send the outline that caused it. Every deck built while this recipe
  was written opened in PowerPoint without one.

> **Balance dividend**
> About 30 minutes a week for a team lead who presents every week or
> two, and an evening saved before every quarterly review.
