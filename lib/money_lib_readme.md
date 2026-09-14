# MONEY - exact money on whole minor units

`lib/money.jdb` keeps a money amount as a whole number of minor units
(cents, yen, fils) with its ISO 4217 code beside it, and does everything
an invoice needs without a binary fraction on the way: reading amounts
from text, multiplying by a VAT or discount rate, dividing, converting
between currencies, cash rounding, sharing out by ratios and splitting
into installments, with a named rounding mode wherever a cent has to be
decided. The digits of each currency's minor unit come from VALID, and
formatting follows NUMFMT's locales.

Stands in for: Python `decimal` for money, py-moneyed, dinero.js.

## Quick start

```basic
IMPORT MONEY

DIM net = MONEY.PARSE("1.234,50", "EUR")                     ' 123450
DIM vat = MONEY.TIMES(net, "0.19")                           ' 23456
DIM gross = MONEY.PLUS(net, vat)                             ' 146906
PRINT MONEY.CURRENCY$(gross, "EUR", "de-DE")                 ' 1.469,06 €
PRINT MONEY.DECIMAL$(gross, "EUR")                           ' 1469.06
PRINT MONEY.PARTS(gross, 3)                                  ' [48969, 48969, 48968]
PRINT MONEY.ALLOCATE(10000, [1, 2])                          ' [3333, 6667]
PRINT MONEY.CONVERT(10000, "EUR", "JPY", "161.23")           ' 16123
PRINT MONEY.DIVIDE(25, 2, "half-even")                       ' 12
```

## The representation

An amount is a plain number: `123450` with `"EUR"` means 1234.50 euros.
The currency is passed to the calls that need its digits; adding and
comparing are ordinary number operations on amounts of one currency.

Amounts run up to `MAXMINOR()` = 9007199254740991 minor units
(90,071,992,547,409.91 euros). That is 2^53 - 1, the largest whole number
a double holds exactly: a compiled program passes numbers between
functions as doubles, so this is the bound both backends keep exact.
Every call checks it and throws past it rather than returning a rounded
number. The intermediate products of TIMES, CONVERT and ALLOCATE are
computed on digit strings, so a large amount times a long rate is exact
as long as the rounded result fits.

## API

### Reading and writing

| Call | What it does |
|------|--------------|
| `PARSE(text$, code$, [mode$], [locale$])` | An amount from text into minor units, exactly. Spaces, apostrophes and the no-break spaces of the locales are group separators; letters and currency signs around the number are dropped, a minus or an opening bracket makes it negative. With `locale$` (`"de-DE"`, `"en-US"`, any locale NUMFMT knows) its decimal mark decides; without it the last of `.` and `,` is the mark when both occur, a single one is the mark, and one that repeats groups thousands. Groups after the first must have three digits. More decimals than the currency has throw unless `mode$` is given. |
| `FROMNUMBER(value, code$, [mode$])` | A double through its shortest decimal text: `19.99` is 1999, `1.005` is 101 half-up (the binary value lies below 1.005, the text does not). Mode `half-up` by default. |
| `DECIMAL$(amount, code$)` | The amount as a plain decimal for storage and JSON: `"1234.50"`, `"-0.05"`, `"12"` for yen. Reads back through PARSE. |
| `NUMBER$(amount, code$, [locale$])` | Grouped digits with the locale's marks, no currency sign: `1.234,50`. |
| `CURRENCY$(amount, code$, [locale$])` | Sign and currency where NUMFMT puts them for the locale (`1.234,50 €`, `CHF-1’234.50`, `-€1,234.50`). The digits are written from the minor units, so the limit amount prints to the cent. `de-DE` by default. |
| `TOMAJOR(amount, code$)` | The amount in major units as a double, for charts; not exact for large amounts. |
| `DIGITS(code$)` | The digits of the currency's minor unit (EUR 2, JPY 0, KWD 3, CLF 4). A code ISO 4217 gives none for (gold, XDR) or an unknown one throws. |
| `MAXMINOR()` | 9007199254740991. |

### Arithmetic

| Call | What it does |
|------|--------------|
| `PLUS(a, b)`, `MINUS(a, b)` | Sum and difference; throw past the limit or for a fraction of a minor unit. |
| `TOTAL(amounts)` | The sum of an array. |
| `TIMES(amount, rate, [mode$])` | The amount times a rate, rounded. Give the rate as decimal text (`"0.19"`, `"1.075"`, `"-0.03"`) for an exact result; a number is read through its shortest decimal text, so `0.19` works too. |
| `DIVIDE(amount, divisor, [mode$])` | The amount divided by a number or decimal text of at most 14 significant digits. |
| `CONVERT(amount, from$, to$, rate, [mode$])` | Into another currency at a rate in target units per source unit, each side in its own minor units: 100.00 EUR at `"161.23"` is 16123 JPY. |
| `ROUNDSTEP(amount, unit, [mode$])` | To a multiple of `unit` minor units: `ROUNDSTEP(x, 5)` is Swiss cash rounding. |

### Sharing out

| Call | What it does |
|------|--------------|
| `ALLOCATE(amount, ratios)` | Shares by whole-number ratios (`[50, 30, 20]`, zeros allowed) that add up to the amount exactly. Each part gets its exact share rounded toward zero; the units left over go one each to the parts with the largest remainders, and of equal remainders the earlier part comes first. A negative amount gives negative parts. |
| `PARTS(amount, n)` | `n` parts as equal as minor units allow, the larger ones first: 100 in 3 is `[34, 33, 33]`. |

### Rounding modes

Every call that may cut off a fraction takes `mode$`, in any letter case:

| Mode | A fraction is rounded | 2.5 | -2.5 | 2.4 | -2.6 |
|------|------------------------|-----|------|-----|------|
| `half-up` (default) | to the nearer unit, a half away from zero | 3 | -3 | 2 | -3 |
| `half-even` | to the nearer unit, a half to the even one (banker's rounding) | 2 | -2 | 2 | -3 |
| `half-down` | to the nearer unit, a half toward zero | 2 | -2 | 2 | -3 |
| `up` | away from zero | 3 | -3 | 3 | -3 |
| `down` | toward zero | 2 | -2 | 2 | -2 |
| `ceiling` | toward plus infinity | 3 | -2 | 3 | -2 |
| `floor` | toward minus infinity | 2 | -3 | 2 | -3 |

The names and results are those of Python's `ROUND_HALF_UP`,
`ROUND_HALF_EVEN`, and so on. German invoices round half up (DIN 1333).

## Notes

- Amounts of different currencies are never mixed by PLUS or TOTAL: the
  numbers carry no code, so keep one currency per variable or per array.
- VAT per line and VAT on the total can differ by a cent; compute both
  with TIMES and choose, the module does not decide it for you.
- PARSE without a locale reads `1.234` as one point two three four; for
  EUR that has three decimals and throws instead of guessing a thousand.
  Pass the locale when the input is known to group thousands.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/money_selftest.jdb`, with 2346 reference rows in
`tests/jdlibs/fixtures/money_reference.tsv` produced by Python's `decimal`
module (parsing, rates in all seven modes, division, conversion between
currencies with 0, 2, 3 and 4 digits, cash rounding, allocation).
Demo: `jdb/demos/jdlibs/money_demo.jdb`.
