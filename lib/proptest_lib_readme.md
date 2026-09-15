# PROPTEST - property based tests with shrinking

`lib/proptest.jdb` tests a property against many drawn inputs instead of
a few written ones. Generators draw numbers, text, FAKE data, lists and
maps and their combinations from a seed. When an input breaks the
property, it is shrunk to a minimal one, and the report names it together
with the seed that replays it. `FORALL` turns the run into one TESTKIT
assertion.

Stands in for: hypothesis.

## Quick start

```basic
IMPORT TESTKIT, PROPTEST

FUNC KeepsLength(xs)
    RETURN LEN(MySort(xs)) = LEN(xs)
ENDFUNC

PROPTEST.FORALL("sort keeps the length", "list(int:-50..50,0..20)", KeepsLength@)
TESTKIT.REPORT()
```

A sort that drops duplicates fails, here with `{"seed": 7}` as the fourth
argument, with

```
 FAIL 1   sort keeps the length: falsified by [0, 0] (seed 7, case 5, shrunk from [-2, 1, 3, 8, 8, -26, 28])
```

Without the option the seed comes from the clock and the message names
it; passing that seed replays exactly that run.

## Generators

A generator is a string; the builders on the right write the same strings.

| Generator | Draws | Builder |
|-----------|-------|---------|
| `int:lo..hi` | whole numbers, `-1000..1000` without a range | `INTS$(lo, hi)` |
| `float:lo..hi` | numbers in the range | `FLOATS$(lo, hi)` |
| `bool` | 0 or 1, shown as `FALSE` and `TRUE` | `BOOLS$()` |
| `text:min..max` | printable ASCII text, 0 to 10 characters without a range | `TEXTS$(min, max)` |
| `letters:min..max`, `digits:min..max` | letters, digits | `LETTERS$`, `DIGITS$` |
| `fake:kind` | FAKE data: `name first last user email street city postcode country company phone iban uuid word sentence` | `FAKED$(kind$)` |
| `pick#n` | one entry of a list of text or numbers | `PICK$(entries)` |
| `list(gen,min..max)` | an array of numbers or of text, 0 to 10 entries without a range | `LISTOF$(gen$, min, max)` |
| `oneof(gen,gen,...)` | one of the generators, which must draw the same kind | `ONEOF$(gens)` |
| `map(key=gen,key=gen,...)` | a map whose fields are numbers or text | `MAPOF$(keys, gens)` |

The property receives a number, a text, an array or a map, the same kind
for every input. Lists of lists and lists of maps are refused.

## Running

| Call | What it does |
|------|--------------|
| `FORALL(name$, gen$, prop, [opts])` | A TESTKIT assertion: passes with `name (100 cases)` when the property holds for every input, fails with the minimal input, the seed, the case and the input it was shrunk from, plus the error when the minimal input made the property throw. |
| `FALSIFY(gen$, prop, [opts])` | The run as a map: `ok`, `runs` (cases tried), `seed`, `case` (the failing one, -1), `first` (the input that failed), `shown` (the minimal input), `shrinks` (steps taken), `calls` (property calls while shrinking), `error`. |
| `EXAMPLES(gen$, n, seed)` | `n` drawn inputs, shown as text, to look at a generator or to seed a test file. |

| Option | Default | What it does |
|--------|---------|--------------|
| `runs` | 100 | Inputs to try. |
| `seed` | from the clock | The run's seed; every case draws from its own `RNG` generator (xoshiro256**) seeded with a number made of it and the case number, so a seed replays the same inputs in both backends. |
| `max_shrinks` | 2000 | Property calls allowed while shrinking. |

A property is a FUNC of one argument that answers TRUE when the input is
fine. A property that answers FALSE or throws fails.

## Shrinking

Every draw takes whole numbers from a sequence of choices: a length is a
run of go-on choices, a number is one choice whose value 0 is the one
nearest zero (then 1, -1, 2, -2, ...), a character is its place in the
alphabet (`a` first). A failing input is shrunk by deleting runs of
choices, lowering single choices, and lowering pairs of equal choices
together, keeping each change that still fails and makes the sequence
shorter or smaller. Because it works on the choices rather than on the
values, every generator and combination shrinks the same way, and a
shrunk input is always one the generator can draw.

## Notes

- `fake:` kinds seed FAKE from a choice, so they replay and shrink like
  the rest, and they change FAKE's own seed.
- The property must return TRUE or FALSE: a FUNC that returns nothing
  answers 0 and fails.
- A failing `FORALL` makes `TESTKIT.REPORT` throw, like any failed
  assertion.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/proptest_selftest.jdb`. Demo: `jdb/demos/jdlibs/proptest_demo.jdb`.
