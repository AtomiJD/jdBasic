# I18N - text catalogs, plural rules, a shared locale

`lib/i18n.jdb` keeps the texts of an application per locale, finds a text
through a fallback chain (`de-AT` -> `de` -> `en`), fills `{name}`
placeholders, picks the plural form from the CLDR rules, chooses a locale
from a browser's Accept-Language header, and hands one current locale on to
NUMFMT, DT and TMPL.

Stands in for: gettext, babel's plural and negotiation parts, python-i18n.

## Quick start

```basic
IMPORT I18N

I18N.LOADFILE("en", "lang/en.json")
I18N.LOADFILE("de", "lang/de.yaml")
I18N.LOADFILE("de-AT", "lang/de-AT.toml")

I18N.USELOCALE(I18N.LOCALE_OF$(request, ["en", "de", "de-AT"]))
PRINT I18N.T$("greeting", {"name": "Ann"})     ' Hallo Ann (from de)
PRINT I18N.N$("files", 3)                        ' 3 Dateien
PRINT I18N.CURRENCY$(1234.5, "EUR")              ' € 1.234,50 in de-AT
```

A catalog in any of the formats:

```yaml
greeting: "Hallo {name}"
files:
  one: "{count} Datei"
  other: "{count} Dateien"
```

## API

### Locales

| Call | What it does |
|------|--------------|
| `NORMALIZE$(tag$)` | A tag in its usual spelling: `DE_at` -> `de-AT`, `zh-hant-tw` -> `zh-Hant-TW`. |
| `USELOCALE(locale$)` / `LOCALE$()` | The current locale, used by every call that is given none (`en` at start). |
| `SETDEFAULT(locale$)` / `DEFAULT$()` | The locale each fallback chain ends in (`en`). |
| `FALLBACKS([locale$])` | The chain a lookup tries: the locale, then one part shorter down to the language, then the default and its chain. |

### Catalogs and texts

| Call | What it does |
|------|--------------|
| `LOADFILE(locale$, path$)` | A catalog file; `.json`, `.yaml`/`.yml`, `.toml` or `.ini` by its extension. Answers the number of texts. |
| `LOADTEXT(locale$, text$, format$)` | The same from text; an unknown format raises. |
| `ADDMAP(locale$, map)` / `ADDTEXT(locale$, key$, text$)` | Texts from a map (nested maps become dotted keys, INI sections and TOML tables too) or one text. |
| `T$(key$, [vars], [locale$])` | The text of the first locale in the chain that has the key, placeholders filled; the key itself when none has it. |
| `N$(key$, number, [vars], [locale$])` | The text of `key.<category>` for the plural category of the number, `key.other` when that category has no text; `{count}` holds the number as given. |
| `FILL$(text$, [vars])` | `{name}` replaced by `vars{name}`; `{{` and `}}` give a brace; a placeholder without a value stays. |
| `HAS(key$, [locale$])` / `KEYS(locale$)` / `FORGET([locale$])` | Whether the chain has a key, the keys of one locale (sorted), and dropping one locale's texts or all. |

### Plural rules

| Call | What it does |
|------|--------------|
| `PLURAL$(number, [locale$])` | `zero`, `one`, `two`, `few`, `many` or `other` from the CLDR rules of the locale's language. A number given as text keeps its decimals, so `"1"` is `one` and `"1.0"` is `other` in English. |
| `ADDRULE(lang$, category$, rule$)` | A rule in CLDR syntax: operands `n i v w f t e c`, `mod` or `%`, `in`, `not in`, `=`, `!=`, `within`, values and ranges like `0,1` or `2..4`, `and`, `or`. It replaces the rule of that category. |
| `RULES(lang$)` | The rules of a language as a map of category to rule text. |

Built in are the rules of `en`, `de`, `fr` and `pl`, CLDR's text as babel
2.18 carries it. A language without rules answers `other`.

### Accept-Language

| Call | What it does |
|------|--------------|
| `ACCEPT(header$)` | The tags of the header, normalized, by quality and in header order among equal ones; `*` and `q=0` are left out. |
| `NEGOTIATE$(preferred, available)` | The available locale that fits best, or `""`. `preferred` is a header or a list. For each preferred tag: an available locale spelled the same (case aside), then its bare language, then, for a bare language, the first available locale of it. |
| `LOCALE_OF$(request, available, [default$])` | The locale for a JDWEB request from its `accept-language` header; `default$` or the default locale when none fits. |

### Numbers, money, dates, templates

| Call | What it does |
|------|--------------|
| `NUMBER$(value, [decimals], [locale$])` / `CURRENCY$(value, [code$], [locale$], [decimals])` | Through NUMFMT in the locale. |
| `NUMFMTLOCALE$([locale$])` | The NUMFMT locale: the same when NUMFMT has it, else `de-DE`, `fr-FR` for the language, else `en-US`. |
| `DTLANG$([locale$])` | `de` or `en` for DT's names of weekdays and months. |
| `FILTERS()` | Registers the TMPL filters `t` (`{{ key \| t }}`, an argument names a locale), `number` (`{{ total \| number:2 }}`) and `currency` (`{{ total \| currency:EUR }}`), all in the current locale. |

## Notes

- The current locale is module state. An `ASYNC FUNC` runs on its own copy
  of the globals and does not see a locale set with `USELOCALE`: pass the
  locale as the last argument there; every call takes one.
- The plural categories of 55 numbers in `en`, `de`, `fr` and `pl`, whole
  and with decimals, equal babel's; the negotiation cases equal babel's
  `negotiate_locale` apart from letter case (I18N answers the spelling of
  `available`) and the bare-language step, which babel leaves to its alias
  table. Numbers and amounts in the six NUMFMT locales equal babel's
  formatting.
- NUMFMT knows six locales; Polish money, for example, falls back to
  `en-US`. `DTLANG$` has the two languages DT names things in.
- A plural rule is read when it is used, so a mistake in one added with
  `ADDRULE` raises at the first `PLURAL$` for that language.

## Tests and demo

- `tests/jdlibs/i18n_selftest.jdb`
- `jdb/demos/jdlibs/i18n_demo.jdb` (a shop's basket in four locales, locales picked from browser headers, a template with the filters)
