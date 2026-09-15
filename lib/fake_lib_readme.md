# FAKE - made up data that comes out the same every time

`lib/fake.jdb` fills a test, a demo or a screenshot with names, addresses,
companies, account numbers, dates and text that look real and are not.
The numbers come from a generator written in the module rather than from
the one the machine offers, so the same seed gives the same data on
every computer and in both backends.

Stands in for: Faker.

## Quick start

```basic
IMPORT FAKE

FAKE.SEED(2026)
PRINT FAKE.FULLNAME$(); "  "; FAKE.EMAIL$()
PRINT FAKE.ADDRESSLINE$()

FAKE.LOCALE("de")
PRINT FAKE.COMPANY$(); "  "; FAKE.IBAN$()
```

## The generator

| Call | What it does |
|------|--------------|
| `SEED(n)` | Sets where the run starts. The same seed gives the same values in the same order. |
| `SEEDRNG(n)` | Draws from the `RNG` builtins (xoshiro256**) seeded with n instead: other values than `SEED(n)`, just as reproducible. `SEED` switches back. |
| `LOCALE(name$)` / `LOCALE$()` | Sets and reads the locale: `"en"` or `"de"`, anything else read as `"en"`. |
| `NUMBER(low, high)` | A whole number, both ends included. |
| `DECIMAL(low, high, [places])` | A number rounded to so many places, 2 by default. |
| `CHANCE(share)` | TRUE that share of the time. |
| `PICK(list)` | One entry of a list, whatever it holds. |
| `SHUFFLE(list)` | The same entries in another order, leaving the list it was given alone. |
| `SAMPLE(list, n)` | So many entries, none of them twice. |

Without a `SEED` the run still starts somewhere fixed, so a script that
never seeds is reproducible too; seed it when you want two runs to
differ, or when a fixture has to keep its exact values.

## What it makes

| Call | Example, English | Example, German |
|------|------------------|-----------------|
| `FIRSTNAME$` `LASTNAME$` `FULLNAME$` | Henry Vance | Ottilie Faber |
| `USERNAME$` `EMAIL$` | cobb34@example.org | the same shape |
| `STREET$` `CITY$` `POSTCODE$` `COUNTRY$` | 39 Orchard Yard, Dundee HN17 4FS | Weidenring 58, 17744 Siegen |
| `ADDRESS()` | a map: street, postcode, city, country | |
| `ADDRESSLINE$` | the four on one line, in the order the locale writes them | |
| `COMPANY$` `JOBTITLE$` `DEPARTMENT$` | Thornbury Joinery Ltd | Rabenhof Systeme eG |
| `PHONE$` `MOBILE$` | +44 20 7946 0820 | +49 30 5550739 |
| `IBAN$` `BIC$` | GB34BARC52424346336787 | DE89761991469535258213 |
| `ISODATE$(from$, to$)` | 2024-05-11 | |
| `ISOTIME$(from$, to$)` | 2024-05-11 21:57:11 | |
| `WORD$` `WORDS$(n)` `SENTENCE$([n])` `PARAGRAPH$([n])` | lorem ipsum text | |
| `UUID$` `ID$(prefix$, [digits])` | c421e964-2c62-43ac-aff6-d84ba8e15052, INV-582959 | |

Every address, number and name is made up. The email domains are the
ones reserved for examples, the British telephone numbers come from the
ranges reserved for fiction, and the account numbers carry the right
check digits so a form that validates one accepts them while no bank
does.

## A table from a spec

A spec is a map from column name to the kind of value that column holds.
`ROWS` gives back a matrix in the order the spec names its columns, which
is what `DF.FROM` takes.

```basic
IMPORT FAKE, DF

FAKE.SEED(7)
DIM spec = {"id": "seq", "customer": "name", "city": "city", _
            "signed": "date:2024-01-01..2024-12-31", "spend": "price:80..2400"}
DIM book = DF.FROM(FAKE.ROWS(200, spec), FAKE.FIELDS(spec))
```

| Call | What it does |
|------|--------------|
| `FIELDS(spec)` | The column names, in the order the spec names them. |
| `ROW(spec, [at])` | One row; `at` is what a `seq` column answers. |
| `ROWS(n, spec)` | A matrix of n rows. |

The kinds are `seq first last name username email company job department
street city postcode country address phone mobile iban bic uuid word
sentence paragraph bool`, and these take an argument after a colon:

| Kind | Example | What it gives |
|------|---------|---------------|
| `int` | `int:1..100` | a whole number in that range |
| `price` / `decimal` | `price:80..2400` | a number with two places |
| `date` / `datetime` | `date:2024-01-01..2024-12-31` | a date, or a date and a time |
| `words` | `words:8` | that many words |
| `pick` | `pick:bronze\|silver\|gold` | one of those |
| `id` | `id:INV-` | that prefix and six digits |

A kind the module does not know is written into the column as it stands,
so a spec with a typo shows it rather than hiding it.

## Notes

- The generator is a linear congruential one whose arithmetic stays
  inside what a double holds exactly, which is why the sequence is the
  same on every machine. The self test pins the hash of a thousand rows
  for exactly that reason.
- `SEEDRNG` shares its generator with `RAND` and the `RNG` builtins, so
  fake data and random draws come from one kind of stream; the classic
  generator stays the default, and existing fixtures keep their values.
- It is made for fixtures and demonstrations, not for anything that
  needs unguessable numbers.
- The German locale writes umlauts out as `ae`, `oe` and `ue`, so a
  fixture survives a file read in any encoding.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/fake_selftest.jdb`. Demo: `jdb/demos/jdlibs/fake_demo.jdb`.
