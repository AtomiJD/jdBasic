# TESTKIT - assertions, suites and test reports

`lib/testkit.jdb` is the test convention every other module in this
library is built on: assertions with a message, named suites, a summary
that ends the program with a non-zero exit code when anything failed,
and machine readable output for a build gate.

Stands in for: pytest, unittest.

## Quick start

```basic
IMPORT TESTKIT

TESTKIT.SUITE("string helpers")
TESTKIT.EQ(UCASE$("ab"), "AB", "upper case")
TESTKIT.NEAR(2.0 / 3.0, 0.6667, 0.001, "two thirds")
TESTKIT.EQARRAY(SPLIT("a,b", ","), ["a", "b"], "split")
TESTKIT.THROWS(Parse@, "{", "bad json is rejected")

PRINT TESTKIT.REPORT()
```

Run it with `jdBasic mytest.jdb`. A green run prints one line per
assertion and a summary; a red run prints the failure detail under the
assertion and exits with status 1.

## API

| Call | What it does |
|------|--------------|
| `SUITE(name$)` | Starts a named group; every later assertion is filed under it. |
| `EQ(actual, expected, message$)` | Equal, for scalars. Refuses arrays, use `EQARRAY`. |
| `NE(actual, unwanted, message$)` | Not equal, for scalars. |
| `EQARRAY(actual, expected, message$)` | Same length and every element equal. |
| `NEARRAY(actual, unwanted, message$)` | The arrays differ somewhere. |
| `ISTRUE(cond, message$)` / `ISFALSE(cond, message$)` | A truth value. |
| `NEAR(actual, expected, epsilon, message$)` | Numbers within `epsilon`. |
| `THROWS(fn@, arg, message$)` | Calling `fn(arg)` raises an error. |
| `SKIP(message$)` | Records a skipped case. |
| `MODE(name$)` | Output format: `"human"` (default, coloured), `"tap"` (TAP 13), `"junit"` (JUnit XML). |
| `REPORT()` | Prints the summary in the chosen format, returns the failure count, throws when anything failed. |
| `RESET()` | Drops every recorded result, for a program that runs several reports. |

## Notes

- Every assertion takes its message last. The message is what the
  report shows, so write it as the sentence that would be wrong.
- `THROWS` takes a function reference and one argument. It is
  interpreter-only: a compiled program does not catch an error raised
  inside a called function.
- Under `-c`, an untyped parameter that is called with both a string and
  a number is rejected by the compiler. `THROWS` reads `TYPEOF` of its
  argument, which makes that parameter runtime-typed; give a helper of
  your own the same treatment if you need it polymorphic.
- `MODE("tap")` prints `TAP version 13` at once and the plan line at the
  end, so a TAP consumer can read the stream as it arrives.

## Tests and demo

- `tests/jdlibs/testkit_selftest.jdb`
- `jdb/demos/jdlibs/testkit_demo.jdb` (`tap`, `junit` and `fail` as arguments)
