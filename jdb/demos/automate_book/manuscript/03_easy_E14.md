<!-- pagebreak -->

## E14 Page Watcher

### The chore

Mia's team needs a new desk lamp, and the one they like is too expensive.
The shop lowers its prices now and then, so Mia opens the page every
morning to look. She does the same for the opening hours of the town
hall, which change in the holidays, and for the status page of the
supplier's ordering system. Five pages, two minutes each, every day, and
on most days nothing has changed.

### What you get

Every morning the Page Watcher fetches the pages you named, cuts out the
part you care about, a price, a date, a status line, and compares it
with what it saw the time before. It tells you only what changed, word
by word:

```
Desk lamp: changed
    [-19.99-]{+17.49+} EUR
Town hall hours: same
Supplier status: same
```

### Before you start

The setup wizard has run and the Page Watcher is switched on. For every
page you want to watch you need two things: its address, and two short
pieces of the page's HTML around the part you care about. Open the page
in your browser, right-click the price and choose "View page source" or
"Inspect". Around the price you will find something like
`<span id=price>19.99 EUR</span>`. The text just before the price,
`id=price>`, is the start marker; `</span>` after it is the end marker.

### The program

The program reads the list of pages and checks them one after the other:

<!-- include recipes/easy/E14_page_watcher/page_watcher.jdb -->

The watcher fetches a page, takes the part between the markers, turns it
into plain text and compares it with the stored copy:

<!-- include recipes/easy/E14_page_watcher/watcher.jdb -->

### How it works

1. `WATCHER.FETCH` asks for the page with `HTTP.GET$`. A server that does
   not answer or answers with an error code gives a message instead of a
   page, and the program goes on with the next page.
2. `WATCHER.PART$` finds the start marker, takes what follows up to the
   end marker and hands it to `PLAINTEXT$`, which removes the HTML tags,
   scripts and styles and turns entities such as `&amp;` back into
   characters. What is left is the text a reader sees.
3. `WATCHER.COMPARE` keeps one text file per page in the store folder.
   The first look stores the text and reports `new`. Later looks report
   `same` or `changed`; for a change `TEXTDIFF.WORDDIFF$` marks the words
   that went as `[-old-]` and the words that came as `{+new+}`.
4. Every change is also added to `changes.log` in the store folder, with
   the date, so you can see later when the price dropped.

### Run it

The first run stores what it finds:

```
jdbasic page_watcher.jdb
Desk lamp: new
```

Later runs report `same` until the part changes. With `--dry-run` the
program checks and reports but remembers nothing, which is the way to
test new markers.

### Schedule it

The wizard plans the Page Watcher for every morning at 8:00. Pages that
change more often can be checked every hour; choose "every 1 hours" on
the wizard's page "Schedule".

### Make it yours

The store folder is in the `[page_watcher]` part of `work.conf`. Each page
is a `[[page_watcher.page]]` block of its own, which you add by hand:

```toml
[page_watcher]
store = "~/Documents/AutomateWork/watch"

[[page_watcher.page]]
name = "Desk lamp"
url = "https://shop.example.com/lamps/4711"
start = "id=price>"
end = "</span>"

[[page_watcher.page]]
name = "Town hall hours"
url = "https://www.example-town.de/opening-hours"
start = "<h2>Opening hours</h2>"
end = "</table>"
```

- **Watch a whole page**: leave out `start` and `end`. Small changes such
  as a date in the footer then count too.
- **Watch more pages**: copy a block and change its name, address and
  markers. The name also names the stored copy, so keep it unique.

### When it goes wrong

- **`missing`**: the start marker is no longer on the page. Shops change
  their pages; look at the source again and update the markers.
- **`error` with "cannot reach"**: the address is wrong or you are
  offline. The program tries again on its next run.
- **`changed` every day**: the part holds something that always changes,
  such as a time or a visitor counter. Narrow it with closer markers.
- **A shop shows a page that asks you to log in**: some pages need a
  browser session. Those pages are for the expert recipe X11.

> **Balance dividend**
> About 20 minutes a week of opening the same pages, and you hear about
> the lower price on the morning it drops.
