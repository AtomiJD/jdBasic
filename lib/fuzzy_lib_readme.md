# FUZZY - near matches and phonetics

`lib/fuzzy.jdb` finds texts that are almost the same: the edit distances
between two texts, similarity scores from 0 to 100 in the manner of
rapidfuzz, the best matches of a text in a list, and the phonetic codes
that give names spoken alike the same key, Koelner Phonetik for German and
Soundex for English.

Every function counts characters, not bytes: `Müller` has six letters and
`LEVENSHTEIN("Müller", "Muller")` is 1.

Stands in for: rapidfuzz, jellyfish (Soundex), cologne_phonetics.

## Quick start

```basic
IMPORT FUZZY

PRINT FUZZY.LEVENSHTEIN("kitten", "sitting")                       ' 3
PRINT FUZZY.DAMERAU("ca", "abc")                                   ' 2
PRINT FUZZY.JAROWINKLER("martha", "marhta")                        ' 0.9611...
PRINT FUZZY.RATIO("new york mets", "new york meats")               ' 96.29...
PRINT FUZZY.TOKENSETRATIO("fuzzy was a bear", "fuzzy fuzzy was a bear")   ' 100
PRINT FUZZY.SCORE("Müller Papier GmbH", "papier müller gmbh", "wratio", 1)  ' 95

DIM names = ["Papier & Toner GmbH", "Mueller Papierhandel GmbH", "Müller Papier"]
DIM best = FUZZY.EXTRACTONE("Müller Papier GmbH", names)
PRINT best{"choice"}, best{"score"}, best{"index"}                 ' Müller Papier  95  2

PRINT FUZZY.KOELNER$("Meier"), FUZZY.KOELNER$("Mayer")             ' 67  67
PRINT FUZZY.SOUNDEX$("Robert"), FUZZY.SOUNDEX$("Rupert")           ' R163  R163
```

## API

### Distances

| Call | What it does |
|------|--------------|
| `LEVENSHTEIN(a$, b$)` | Insertions, deletions and substitutions that turn one text into the other. |
| `OSA(a$, b$)` | Like LEVENSHTEIN, and two neighbouring characters swapped count as one edit, as long as no part is edited twice (optimal string alignment). |
| `DAMERAU(a$, b$)` | Unrestricted Damerau-Levenshtein: a swapped pair may be edited again, so `"ca"` to `"abc"` is 2 where OSA says 3. |
| `INDEL(a$, b$)` | Insertions and deletions only; a substitution costs two. |
| `JARO(a$, b$)` | Jaro similarity from 0 to 1. |
| `JAROWINKLER(a$, b$, [weight])` | Jaro-Winkler: a common start of up to four characters raises a Jaro score above 0.7, by `weight` (0.1) per character. |

### Scores from 0 to 100

| Call | What it does |
|------|--------------|
| `RATIO(a$, b$)` | `100 - 100 * INDEL / (total length)`. |
| `PARTIALRATIO(a$, b$)` | The RATIO of the shorter text against its best matching part of the longer one. |
| `TOKENSORTRATIO(a$, b$)` | RATIO with the words of both texts sorted: word order does not count. |
| `TOKENSETRATIO(a$, b$)` | The shared words against each text's own words: 100 when all words of one appear in the other. |
| `TOKENRATIO(a$, b$)` | The better of the two above. |
| `PARTIALTOKENRATIO(a$, b$)` | PARTIALRATIO on the sorted words and on the words the texts do not share; 100 as soon as they share a word. |
| `WRATIO(a$, b$)` | rapidfuzz's weighted ratio: RATIO, then the word ratios (times 0.95) for texts of similar length, or the partial ratios (times 0.9, or 0.6 beyond eight times the length) when one text is at least half as long again. The best general choice. |
| `PREP$(text$)` | Lower case, every character that is not a letter or digit turned into a space, both ends trimmed: rapidfuzz's `default_process`. |
| `SCORE(a$, b$, [scorer$], [clean])` | One of the scores by name: `"wratio"` (default), `"ratio"`, `"partial"`, `"token_sort"`, `"token_set"`, `"token"`, `"partial_token"`, and `"jaro"` and `"jaro_winkler"` on the scale of 100. With `clean` true both texts go through PREP$ first. An unknown name throws. |

### Matching against a list

| Call | What it does |
|------|--------------|
| `EXTRACT(query$, choices, [limit_n], [scorer$], [cutoff], [clean])` | The best matches, best first, as maps with `choice`, `score` and `index`: at most `limit_n` (5; 0 for all), none below `cutoff` (0), scored by `scorer$` (`"wratio"`), with PREP$ when `clean` is true. Equal scores keep the order of the list. |
| `EXTRACTONE(query$, choices, [scorer$], [cutoff], [clean])` | The single best match as such a map; `index` -1 and an empty `choice` when nothing reaches the cutoff or the list is empty. |

### Phonetics

| Call | What it does |
|------|--------------|
| `KOELNER$(text$)` | The Koelner Phonetik code, one code per word separated by a space; a hyphen separates words too. Umlauts are read as `ae`, `oe`, `ue`, `ß` as `s`, accents are dropped. Meier, Mayer, Maier and Meyer are all `67`; Christoph and Kristof are `47823`. |
| `SOUNDEX$(name$)` | American Soundex: the first character and three digits. Umlauts and accented letters count as their base letter and `ß` as `s`; H and W do not separate letters with the same digit. |

## Notes

- The scores are the ones rapidfuzz 3.14 gives with its default of no
  processor; pass `clean` to SCORE, EXTRACT and EXTRACTONE for what
  `processor=utils.default_process` gives.
- PREP$ lowers ASCII and the capitals of Latin-1 (À to Þ); letters of other
  scripts are kept as they are.
- PARTIALRATIO looks at every alignment. rapidfuzz does the same for a
  shorter text of up to 64 characters and uses a faster search beyond
  that, which can score a long needle a little lower.
- The scores cost time with the length of the texts: RATIO is a table of
  both lengths, PARTIALRATIO repeats it for each alignment. Texts of the
  length of names, addresses and commands are what the module is for.
- KOELNER$ follows cologne_phonetics 2.0, including its choice to collapse
  repeated digits rather than repeated codes (`xx` is `4848`).
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/fuzzy_selftest.jdb`, 790 assertions against a
reference table from rapidfuzz 3.14, jellyfish and cologne_phonetics 2.0
(`tests/jdlibs/fixtures/fuzzy_*.tsv`).
Demo: `jdb/demos/jdlibs/fuzzy_demo.jdb`.
