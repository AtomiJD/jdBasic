<!-- pagebreak -->

## M04 Receipt Sorter

### The chore

Jonas travels to customers twice a month and pays for fuel, trains,
hotels and the odd team lunch. The receipts end up as photos on his
phone and as PDF files in his mail, and at the end of the month the
expense form wants them by category with a total. Finding the twelve
files, renaming them, adding up and typing the sums takes an evening
that nobody pays for.

### What you get

You drop every receipt into one inbox folder, named the way you would
name it anyway: date, what it was, the amount. Once a day the Receipt
Sorter moves each file into a folder for its month and category, and
writes an Excel report per month with the sums by category on the
first sheet and every receipt on the second. At the end of the month
the report is there, and the receipts are next to it.

### Before you start

The setup wizard has run. Make the inbox folder, and name new receipts
so that the name holds the date and the amount:

```
2026-10-03 fuel 45.20.pdf
20261005 rail ticket 89,90.pdf
07.10.2026 lunch team 64.50.jpg
```

The date can be written as `2026-10-03`, `20261003` or `03.10.2026`.
The amount is the last number with two decimals, with a point or a
comma. A receipt without a date in its name goes into the month of the
file's time, which for a photo is the day you took it.

### The program

The program reads its settings, moves the receipts and writes the
report of every month it touched:

<!-- include recipes/medium/M04_receipt_sorter/receipt_sorter.jdb -->

The module `RECEIPTS` reads names, finds free names and builds the
report:

<!-- include recipes/medium/M04_receipt_sorter/receipts.jdb -->

### How it works

1. **Rules.** The `rules` setting is a list of texts such as
   `"fuel = Travel"`. `RULES` splits each one at the `=` into a word
   and a category and drops texts without one. `CATEGORY$` takes the
   first rule whose word appears anywhere in the file name, in any
   case; a name no rule fits goes to `Other`.
2. **Date and amount.** `DATE$` and `AMOUNT` look for the patterns
   with `REGEX.FINDALL`, which answers the groups of each match. The
   amount takes the last match, because a date such as `03.10` looks
   like an amount too, and the amount usually comes last in a name.
3. **Choosing the files.** Only files with an extension from `kinds`
   are moved. Everything else, such as a text file with notes, stays
   in the inbox and is named at the end of the run.
4. **Moving.** `FREE_NAME$` checks whether the target name is taken
   and adds ` (2)`, ` (3)` and so on until it is free. No receipt
   overwrites another, even when you drop the same file twice.
5. **The report.** `MONTH_ROWS` reads the month folder again, all
   category folders, and makes one row per file. The report thus
   always shows what is in the folder, including files you moved
   there by hand. `SUMS` adds up per category and counts the receipts;
   `WRITE_REPORT` writes both tables into `2026-10 expenses.xlsx`.

### Run it

```
jdbasic receipt_sorter.jdb --dry-run
would move 07.10.2026 lunch team 64.50.jpg  -> 2026-10/Meals  64.50
would move 2026-10-03 fuel 45.20.pdf  -> 2026-10/Travel  45.20
would move 20261005 Rail ticket 89,90.pdf  -> 2026-10/Travel  89.90
would move office paper 12.99.png  -> 2026-10/Office  12.99
Left in the inbox: notes.txt
```

The office paper has no date in its name and goes by the file's time.
Then file them:

```
jdbasic receipt_sorter.jdb
moved 07.10.2026 lunch team 64.50.jpg  -> 2026-10/Meals  64.50
...
Left in the inbox: notes.txt
report 2026-10: 4 receipts, 212.59
```

To write a month's report again after you changed something by hand:

```
jdbasic receipt_sorter.jdb --month 2026-10
```

### Schedule it

The wizard runs the sorter every day at the end of your working day,
so the inbox is empty each morning and the report of the month is up
to date.

### Make it yours

The settings:

```toml
[receipt_sorter]
inbox = "~/Documents/AutomateWork/receipts/inbox"
folder = "~/Documents/AutomateWork/receipts"
kinds = ["pdf", "jpg", "jpeg", "png", "heic"]
rules = ["fuel = Travel", "rail = Travel", "hotel = Travel",
         "taxi = Travel", "office = Office", "lunch = Meals",
         "parking = Travel"]
```

A new rule is a new line in `rules`. The first rule that fits wins,
so put the narrow ones first: `"hotel bar = Meals"` before
`"hotel = Travel"`.

Changes in the code:

- **The report in another folder,** such as a shared drive of the
  accounting department: in `Report`, change `path$` to
  `PATH.JOIN$("S:/Accounting", month$ + " Jonas.xlsx")`.
- **A column for the VAT** in the report: in `MONTH_ROWS`, add
  `ROUND(amount * 19 / 119 * 100) / 100` as a fifth cell to the row
  with an amount and `""` to the row without, and add `"VAT"` to the
  header row.
- **Rename the file** to a clean form while moving it, such as
  `2026-10-03 Travel 45.20.pdf`: in the program, build the new name
  as `r{"date"} + " " + r{"category"} + " " + amount$ + "." + ext$`
  and pass it to `FREE_NAME$` instead of `file$`.
- **Mail the report** to the accounting department at the end of the
  month: build a message with `MAIL.MESSAGE` and `MAIL.ATTACH`, and put
  it into the outbox following the outbox rule of this chapter; M01
  shows the lines.

### When it goes wrong

- **A receipt is in the wrong month**: its name has no date and the
  file's time is the day you saved it, not the day you paid. Put the
  date at the front of the name.
- **"no amount"** on a receipt: the name has no number with two
  decimals, such as `fuel 45.pdf`. Write `45.00`.
- **A receipt counts twice in the report**: the same file was dropped
  twice and filed as `... (2).pdf`. Delete the copy and run with
  `--month`.
- **A receipt went to `Other`**: no rule word is in its name. Add a
  rule, move the file back into the inbox, and run again.

> **Balance dividend**
> An evening a month of searching, renaming and adding up, about
> fifteen minutes a week, and a report that is ready before the form
> asks for it.
