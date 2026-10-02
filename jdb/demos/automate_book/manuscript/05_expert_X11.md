<!-- pagebreak -->

## X11 Web Harvester

### The chore

Every Monday Lena updates a sheet with the prices of the supplies her
department buys most: copy paper, toner, the coffee for the kitchen.
The purchasing team wants to know when a supplier raises a price, and
the numbers are on the suppliers' web shops, one page per product. So
she opens fourteen pages, finds the price on each, types it into
Excel and checks that the comma ended up in the right place.

### What you get

A CSV file with one line per value and run: the time, the name you
gave the value, the value as the page shows it, the same value as a
plain number, the address, and *ok* or what went wrong. Excel opens
the file, and a chart over the weeks shows when a price moved.

The harvester reads the pages the way a considerate visitor does. It
asks each site's `robots.txt` which pages it may read, waits between
two requests to the same site, keeps a page it has read for twelve
hours instead of asking again, and gives a busy server a second and a
third chance before it reports the page as failed.

### Before you start

Pick the pages and find the element that holds each value:

1. Open the page in your browser, right-click the price and choose
   *Inspect*. The browser shows the HTML around it, for example
   `<span class="price">4,99 €</span>`.
2. Write a CSS selector for it: `span.price` means a `span` element
   with the class `price`. If the page has several prices, make it
   more precise, such as `div.product-main span.price`.
3. Put each value into `targets.toml` in the harvester's folder, as
   in the example file that comes with the recipe:

```toml
[[target]]
name = "Copy paper A4, Office Shop"
url = "https://shop.example.com/paper/a4-80g"
select = "div.product-main span.price"

[[target]]
name = "Toner X200 data sheet"
url = "https://shop.example.com/toner/x200"
select = "a.datasheet"
attr = "href"
```

`attr` reads an attribute of the element instead of its text, here
the address of a data sheet.

> **Watch out**
> Being polite to a server is not the same as being allowed to read
> it. Many shops say in their terms of use whether automated reading
> is permitted, and some offer a price list or an interface for it.
> Prefer those, read the terms before you harvest a site, and never
> collect personal data this way.

### The program

The program reads the targets, runs the harvester and adds the lines
to the CSV file:

<!-- include recipes/expert/X11_web_harvester/web_harvester.jdb -->

The module holds the politeness and the reading:

<!-- include recipes/expert/X11_web_harvester/harvest.jdb -->

### How it works

1. `NEW` sets up a harvester: the name it gives itself in the
   `User-Agent` header of every request, the pause between two
   requests to a site, and a CACHE of pages. The cache is loaded from
   `cache.json` if a run left one, and an entry is dropped after
   `cache_hours`.
2. `PAGE$` answers a page from the cache while it is fresh. Otherwise
   `RulesFor` reads the site's `robots.txt` once per run and `ROBOTS`
   picks the rules that apply: the group of lines that names this
   harvester, else the group for every visitor (`User-agent: *`).
3. `ALLOWED` decides with the longest matching rule, which is how the
   big search engines read the file: with `Disallow: /shop` and
   `Allow: /shop/prices`, the prices are allowed and the cart is not.
   `MATCHES` knows the two wildcards of `robots.txt`: `*` for any
   characters and a final `$` for the end of the address.
4. `Pause` waits until the last request to the same site is long
   enough ago: `delay_seconds`, or the `Crawl-delay` the site asks
   for in its `robots.txt` when that is longer. `TICK()` gives the
   time in milliseconds.
5. `GetOnce` sends one request. A busy answer (429 or 5xx) or no
   answer at all raises an error starting with *transient*, and
   `RETRY.ATTEMPT` in `Get` tries again after a pause that doubles.
   Any other answer comes back to `PAGE$`, which keeps a good page
   and reports a missing one.
6. `VALUE$` parses the page with HTMLDOM, finds the first element the
   selector matches and answers its text with the spaces tidied up,
   or the attribute.
7. `NUMBER$` turns what the page shows into a plain number. It looks
   at the last point or comma: followed by exactly three digits and
   alone, it separates thousands (`1,234 pieces`); otherwise it is the
   decimal mark, and every other point or comma is dropped. So
   `1.234,50 EUR` and `$1,234.50` both become `1234.50`.
8. `RUN` goes through the targets. A target that fails gets its
   reason in the status column and the others go on, so one changed
   shop page does not cost you the whole Monday sheet.

### Run it

Try the targets with a dry run first. It reads the pages and shows
the values, but writes neither the CSV file nor the cache:

```
jdbasic web_harvester.jdb --dry-run
Copy paper A4, Office Shop: 4,99 €  (4.99)
Toner X200, Office Shop: 1.234,50 EUR  (1234.50)
2 values read; nothing written
```

A target that failed shows up as a warning with its reason. When the
values look right, run it for real; the CSV file is
`harvest.csv` in the harvester's folder and the log `harvest.log`
next to it. The CSV file starts with the three bytes of a UTF-8 byte
order mark, the sign Excel looks for to read the euro sign and other
letters right when you open the file with a double click.

### Schedule it

The wizard runs the harvester on weekdays at seven. With the default
pause of five seconds, fourteen pages from two shops take about a
minute. Pages read at seven are kept until seven in the evening, so
a second run during the day costs the shops nothing.

### Make it yours

The settings are the `[web_harvester]` part of `work.conf`:

```toml
[web_harvester]
folder = "~/Documents/AutomateWork/harvest"
delay_seconds = 5
cache_hours = 12
```

The harvester names itself *AutomateWork harvester (jdBasic)* and
adds your mail address from the `[user]` part, so a site owner who
wonders about the requests can write to you.

Changes in the code:

- **One CSV file per week.** In the program, build the file name from
  the week:

  ```
  DIM week$ = FORMAT_DATE(NOW(), "%Y-W%V")
  DIM output$ = PATH.JOIN$(folder$, "harvest-" + week$ + ".csv")
  ```

- **A mail when a price changes.** Read the last value of each name
  from the CSV file before `RUN`, compare it with the new number, and
  put a message into the outbox with `OUTBOX.PUT$` as the recipes of
  Chapter 4 do.
- **A longer pause for one site.** If a site asks for more, it says
  so with `Crawl-delay`, which the harvester already respects. To be
  slower on your own, raise `delay_seconds`.

### When it goes wrong

- **"nothing on the page matches"**: the shop changed its page and
  the selector no longer fits. Inspect the page again and correct
  `select`.
- **"robots.txt asks to stay out of"**: the site does not want this
  page read by programs. Leave it, or ask the site for another way.
- **"transient: the server answered 503"** after three tries: the
  site is down or limits requests. The next run tries again.
- **"the page answered 404"**: the address is wrong or the product is
  gone.
- **The euro sign looks wrong in Excel**: the file was started by an
  older version without the byte order mark. Rename it; the next run
  starts a new one. In other programs choose UTF-8 when you open it.

> **Balance dividend**
> About 45 minutes a week of looking up and typing prices, and a
> price history that no one has to keep by hand.
