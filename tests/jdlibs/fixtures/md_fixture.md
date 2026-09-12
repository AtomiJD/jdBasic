# The jdBasic Module Library

A *small* library of **pure jdBasic** modules, each a single file under `lib/`.
Import one with `IMPORT NAME`, or several at once: `IMPORT CONF, LOGGER`.

## What is in it

- TESTKIT for assertions and reports
- CLI for flags, options and subcommands
  - flags such as `--verbose`
  - options with defaults
- REQ, the HTTP client
    with a continuation line

1. Write the module
2. Write its self test
3. Write the demo

> Every module works interpreted and compiled with `-c`.
> The self tests run on both backends.

### A code sample

```basic
IMPORT CONSOLE
CONSOLE.TABLE([["widget", 3]], {"headers": ["item", "qty"]})
PRINT "<done>" & TRUE
```

| Module | Stands in for | Lines |
|:-------|:-------------:|------:|
| `testkit.jdb` | pytest | 320 |
| `req.jdb` | requests | 340 |

Links and images: [the reference](../doc/languages.md "The language reference"), <https://jdbasic.org>,
![a logo](logo.png "The logo").

Hard break here  
and the second line, then ~~struck~~ text, a snake_case_name, 2 * 3 = 6, and a literal \*star\*.

---

Final paragraph with an ampersand & an angle <bracket> that must be escaped.
