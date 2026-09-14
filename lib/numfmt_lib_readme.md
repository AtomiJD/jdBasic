# NUMFMT - numbers and money the way a locale writes them

`lib/numfmt.jdb` formats numbers, money and percentages for German
(Germany, Austria, Switzerland), English (US, UK) and French (France),
reads them back, writes whole numbers and money amounts in words, and
checks and prints the identifiers an invoice carries: IBAN, the German
VAT identification number and German tax numbers.

The separators, the currency signs and their place follow the Unicode
CLDR data, the same data Babel and every browser use, including the
non-breaking spaces: `1.234,50 €` in Germany, `€ 1.234,50` in Austria,
`CHF 1’234.50` in Switzerland, `$1,234.50` in the US, `1 234,50 €` in
France.

Stands in for: babel.numbers, num2words, python-stdnum (the German parts).

## Quick start

```basic
IMPORT NUMFMT

PRINT NUMFMT.CURRENCY$(1234.5, "EUR", "de-DE")        ' 1.234,50 €
PRINT NUMFMT.CURRENCY$(-1234.5, "CHF", "de-CH")       ' CHF-1’234.50
PRINT NUMFMT.PERCENT$(0.19, 0, "de-DE")               ' 19 %
PRINT NUMFMT.PARSENUMBER("1.234,50 €", "de-DE")       ' 1234.5
PRINT NUMFMT.AMOUNTWORDS$(209.8, "EUR", "de")         ' zweihundertneun Euro und achtzig Cent
PRINT NUMFMT.IBAN$("de89370400440532013000")          ' DE89 3704 0044 0532 0130 00
PRINT NUMFMT.ISVATID("DE136695976")                   ' TRUE
PRINT NUMFMT.TAXNUMBER$("9181081508155")              ' 181/815/08155
```

## API

### Locales and rounding

| Call | What it does |
|------|--------------|
| `LOCALES()` | `de-DE`, `de-AT`, `de-CH`, `en-US`, `en-GB`, `fr-FR`. A locale may also be written `de_DE`, `DE-de`, or just `de`, `en`, `fr`. |
| `SEPARATORS(locale$)` | The decimal mark and the group separator, as a two item array. |
| `ROUNDTO(value, [decimals])` | Commercial rounding: a half goes away from zero, and `1.005` becomes `1.01` although its binary form lies below. |

### Formatting

| Call | What it does |
|------|--------------|
| `NUMBER$(value, [decimals], [locale$])` | Grouped thousands and a fixed number of decimals (2, `de-DE` by default). |
| `CURRENCY$(value, [code$], [locale$], [decimals])` | A money amount with its sign and currency where the locale puts them (`EUR`, `de-DE`, 2 by default). |
| `SYMBOL$(code$, [locale$])` | The sign a locale writes for `EUR`, `USD`, `GBP` (`US$` in Britain, `$US` and `£GB` in France, `EUR` in Switzerland); any other code stands for itself. |
| `PERCENT$(fraction, [decimals], [locale$])` | `0.256` as `26 %`; German and French put a non-breaking space before the sign. |
| `PARSENUMBER(text$, [locale$])` | A number read back: group separators, spaces, currency signs and codes and the percent sign are ignored, the locale's decimal mark decides, a minus or an opening bracket makes it negative. A text without a digit gives 0. |

The characters involved are not plain spaces and apostrophes: the group
separator in France is U+202F, in Switzerland U+2019, and the space before
a currency sign or a percent sign is U+00A0. They keep an amount from
breaking across a line; compare with them, not with `" "`.

### Words

| Call | What it does |
|------|--------------|
| `WORDS$(n, [lang$])` | A whole number in German (`"de"`) or English (`"en"`), up to 999 trillion. German writes a number below a million as one word (`zweitausendsechsundzwanzig`) and uses Million, Milliarde and Billion; English uses "and" and commas the British way (`two thousand and twenty-six`). Both match num2words. |
| `AMOUNTWORDS$(amount, [code$], [lang$])` | A money amount in words: `zweihundertneun Euro und achtzig Cent`, `two hundred and nine euros and eighty cents`. Cents are left out when there are none; euro, franc, dollar and pound have their names, any other code is used as it is. |

### Account and tax identifiers

| Call | What it does |
|------|--------------|
| `IBAN$(text$)` | An IBAN in capitals and groups of four. |
| `ISIBAN(text$)` | Whether the IBAN is right: the check digits (ISO 13616), a country of the SWIFT IBAN registry and the length and layout of that country's account number. It is `VALID.ISIBAN`, so a country outside the registry is refused. |
| `VATID$(text$)` | A German VAT identification number compact: `DE` and nine digits. |
| `ISVATID(text$)` | Whether it has nine digits, the first not 0, and the check digit of ISO 7064 MOD 11,10. A number without a country counts as German; the VAT ids of the other EU states are checked by `VALID.ISVATID`. |
| `FEDERALTAXNUMBER$(text$, [region$])` | A tax number as its state writes it turned into the nationwide 13 digit form; the state is named by its name (`Bayern`, `Thüringen` or `Thueringen`) or its short code (`BY`, `TH`). |
| `TAXNUMBER$(text$, [region$])` | A tax number the way its state prints it (`181/815/08155`, `013 815 08153` in Hessen, `93815/08152` in Baden-Württemberg), from the 13 digit form or from the state's own digits and the state. |
| `TAXREGION$(text$)` | The state of a 13 digit tax number, or `""`. |

## Notes

- The tax number functions check the layout of each of the 16 states and
  convert between the two forms; they do not compute the check digit,
  which each state calculates its own way.
- Babel rounds a half to the even neighbour by default; this module rounds
  it away from zero, as invoices do. The test values of the self test
  avoid halves where the two differ.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/numfmt_selftest.jdb`, with the reference values
produced by Babel 2.18, num2words and python-stdnum.
Demo: `jdb/demos/jdlibs/numfmt_demo.jdb`.
