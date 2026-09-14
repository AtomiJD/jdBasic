# VALID - identifiers checked the way banks and tax offices check them

`lib/valid.jdb` checks the identifiers an invoice, an order or a product
list carries: IBAN against the SWIFT IBAN registry, BIC, the VAT
identification numbers of all EU member states and Northern Ireland, EAN,
UPC and GTIN, ISBN. It holds the ISO 4217 currency table with the digits of
each minor unit and the ISO 3166-1 country table, and the Luhn and ISO 7064
MOD 97-10 checks the rest is built on.

Every check follows python-stdnum: the same country layouts, the same
national check digit rules, the same answers on the reference rows of the
self test.

Stands in for: python-stdnum (iban, bic, eu.vat, ean, isbn, luhn),
pycountry, iso4217.

## Quick start

```basic
IMPORT VALID

PRINT VALID.ISIBAN("DE89 3704 0044 0532 0130 00")    ' TRUE
PRINT VALID.MAKEIBAN$("DE", "370400440532013000")    ' DE89370400440532013000
PRINT VALID.ISBIC("COBADEFFXXX")                     ' TRUE
PRINT VALID.ISVATID("ATU13585627")                   ' TRUE
PRINT VALID.VATID$("be 403.019.261")                 ' BE0403019261
PRINT VALID.ISEAN("4006381333931")                   ' TRUE
PRINT VALID.ISBN13$("0-306-40615-2")                 ' 9780306406157
PRINT VALID.CURRENCYDIGITS("JPY")                    ' 0
PRINT VALID.COUNTRY3$("AT")                          ' AUT
PRINT VALID.ISFORMAT("currency", "chf")              ' TRUE
```

## API

### Currencies and countries

| Call | What it does |
|------|--------------|
| `ISCURRENCY(code$)` | Whether a text is an ISO 4217 code (178 codes, any letter case). |
| `CURRENCYDIGITS(code$)` | The digits of the minor unit: 2 for EUR, 0 for JPY, 3 for KWD, 4 for CLF; -1 for an unknown code and for the units the standard gives none (XAU, XDR, XXX). |
| `CURRENCYNUMBER$(code$)` | The three-digit ISO 4217 number (`"978"` for EUR), or `""`. |
| `CURRENCIES()` | Every code, sorted. |
| `ISCOUNTRY(code$)` | Whether a text is an ISO 3166-1 code: alpha-2, alpha-3 or the three-digit number (249 countries). |
| `COUNTRY2$(code$)` / `COUNTRY3$(code$)` / `COUNTRYNUMBER$(code$)` | A country given by any of its three codes turned into the alpha-2, alpha-3 or numeric code, or `""`. |
| `COUNTRIES()` | Every alpha-2 code, sorted. |

### IBAN and BIC

| Call | What it does |
|------|--------------|
| `ISIBAN(text$)` | Whether an IBAN has correct check digits (ISO 13616), a country of the registry and the length and layout of that country's account number (`8!n10!n` for Germany: 8 digits, then 10). For Belgium, Spain, Montenegro and Norway the national check digits inside the account number are checked too. Spaces, dashes, dots and letter case are ignored. |
| `IBAN$(text$)` | An IBAN in capitals and groups of four, the way it is printed. |
| `MAKEIBAN$(country$, bban$)` | The IBAN of a national account number with its check digits computed, or `""` when the country is not in the registry or the number does not fit its layout. |
| `IBANLENGTH(country$)` | The length of a country's IBAN (22 for DE), or 0. |
| `IBANSTRUCTURE$(country$)` | The registry layout of the account number (`n` digits, `a` capitals, `c` capitals or digits), or `""`. |
| `IBANCOUNTRIES()` | The 89 countries of the registry. |
| `ISBIC(text$)` | Whether a BIC (ISO 9362) has 8 or 11 characters: four letters, an ISO 3166 country or XK, two letters or digits, an optional branch of three. |
| `MOD97(text$)` | The remainder modulo 97 of a text read as one number, letters counting 10 to 35 (ISO 7064 MOD 97-10), or -1 for another character. An IBAN moved round by four characters gives 1. |

### VAT identification numbers

| Call | What it does |
|------|--------------|
| `ISVATID(text$)` | Whether a VAT identification number with its country prefix has the format and check digits of that country: AT BE BG CY CZ DE DK EE EL ES FI FR HR HU IE IT LT LU LV MT NL PL PT RO SE SI SK and XI. GR is taken for Greece beside EL. The national rules differ widely: a Luhn check (Austria, Italy, France's SIREN, Sweden), weighted sums modulo 10, 11, 37, 89 or 97, ISO 7064 MOD 11,10 (Germany, Croatia), check letters (Cyprus, Ireland, Spain), birth dates inside personal numbers (Bulgaria, the Czech Republic, Slovakia, Latvia, Romania). |
| `VATID$(text$)` | The number compact: the prefix and the national number in capitals without spaces, dots, dashes, slashes, commas, colons and brackets; the leading zero Belgium and Greece drop is put back, and the Dutch number before the B is filled to nine digits. `""` for a prefix outside the list. |
| `VATCOUNTRIES()` | The 28 prefixes. |

### Product and book numbers

| Call | What it does |
|------|--------------|
| `ISEAN(text$)` | An EAN-8 or EAN-13 with its check digit. |
| `ISUPC(text$)` | A UPC-A of 12 digits with its check digit. |
| `ISGTIN(text$)` | A GTIN of 8, 12, 13 or 14 digits with its check digit. |
| `EANDIGIT$(digits$)` | The GS1 check digit for the digits without it. |
| `ISISBN(text$)` | An ISBN-10 (check digit 0 to 9 or X) or an ISBN-13 (978 or 979 with the EAN check digit). Nine digits are read as an ISBN-10 with its leading zero. |
| `ISBN13$(text$)` / `ISBN10$(text$)` | The ISBN as 13 or 10 characters, converting between the two; `""` for an invalid ISBN and for an ISBN-10 of a 979 number, which has none. |
| `ISLUHN(digits$)` / `LUHNDIGIT$(digits$)` | The Luhn check of a string of digits (card numbers, IMEI) and the digit that completes one. |

Spaces and dashes are ignored in all of them.

### By name

| Call | What it does |
|------|--------------|
| `ISFORMAT(kind$, text$)` | One of the checks above by its name: `iban`, `bic`, `vatid`, `ean`, `upc`, `gtin`, `isbn`, `luhn`, `country`, `currency`. An unknown name throws. |
| `FORMATS()` | The ten names. |

SCHEMA takes the same names as `"format"` of a string field, so a
declaration such as `{"type": "string", "format": "iban"}` rejects a wrong
IBAN with `iban: not a valid iban`.

## Notes

- The IBAN registry is SWIFT's release 101 as python-stdnum 2.2 carries
  it. Belgium's list of bank codes is not part of this module: stdnum also
  refuses a Belgian IBAN whose bank code is unknown, VALID only checks its
  check digits.
- VAT numbers are checked offline. Whether a number is registered can
  only be asked of the EU VIES service; the one-stop-shop numbers with the
  prefix EU are not covered.
- stdnum ignores a different set of separators in each country (only
  spaces in Estonia, spaces and dashes in Spain); VALID ignores spaces,
  dots, dashes, slashes, commas, colons and brackets in every VAT number,
  so it accepts a few spellings stdnum refuses.
- The ISO tables are pycountry 26.2.16 and the iso4217 package of
  2026-01-01. Kosovo (XK) has no ISO 3166 code and is not a country here,
  but the BIC check accepts it, as banks do.
- NUMFMT's `ISIBAN`, `IBAN$` and `ISVATID` are built on this module.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/valid_selftest.jdb`, 172 assertions over the
reference rows in `tests/jdlibs/fixtures/valid_*.tsv` (620 IBAN rows for
every registry country, 224 VAT rows for every prefix and each of its
number forms, 80 rows of EAN, UPC, GTIN, ISBN, BIC and Luhn, the full
currency and country tables), produced by python-stdnum 2.2, pycountry and
iso4217.
Demo: `jdb/demos/jdlibs/valid_demo.jdb`.
