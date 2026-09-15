# SEARCH - full-text search with BM25

`lib/search.jdb` searches documents kept in memory: an inverted index with
word positions, BM25 ranking with the numbers rank_bm25's `BM25Okapi`
gives, field weights, phrase queries, typo-tolerant lookup through a
trigram index scored with FUZZY, index files, and reciprocal rank fusion to
merge the result lists of several searches (keyword and vector search, or
two indexes).

Stands in for: rank_bm25, the analyzer and BM25 part of Whoosh or a small
Lucene index.

## Quick start

```basic
IMPORT SEARCH

DIM ix = SEARCH.INDEX({"lang": "en", "fields": {"title": 2, "body": 1}})
SEARCH.ADD(ix, "terms", {"title": "Payment terms", "body": "Invoices are due within 30 days."})
SEARCH.ADD(ix, "backup", {"title": "Backups", "body": "The database server is backed up nightly."})
SEARCH.ADD(ix, "late", "Late payments cost a reminder fee.")

DIM hits = SEARCH.QUERY(ix, "payment days")
PRINT hits[0]{"key"}; " "; hits[0]{"score"}              ' terms 0.584808

PRINT LEN(SEARCH.QUERY(ix, CHR$(34) + "database server" + CHR$(34)))   ' 1
PRINT SEARCH.QUERY(ix, "databse", {"fuzzy": 1})[0]{"key"}              ' backup
PRINT SEARCH.SUGGEST(ix, "paymnt")[0]{"word"}                          ' payment

DIM merged = SEARCH.RRF([SEARCH.QUERY(ix, "payment"), SEARCH.QUERY(ix, "server")])
PRINT merged[0]{"key"}                                                 ' backup
SEARCH.WRITEFILE(ix, "docs.idx")
DIM again = SEARCH.READFILE("docs.idx")
PRINT SEARCH.DOCCOUNT(again)                                           ' 3
```

## API

### Indexes and documents

| Call | What it does |
|------|--------------|
| `INDEX([opts])` | A new, empty index; the handle. `opts` is a map: `lang` (`"en"`, `"de"` or `"none"`; `"en"`), `stem` (1), `stopwords` (1), `k1` (1.5), `b` (0.75), `epsilon` (0.25) and `fields`, a map of field name to weight (`{"body": 1}`). An unknown language throws. |
| `ADD(ix, key$, content)` | Files a document under `key$`. `content` is a text for the default field (`body`, or the first field the index names when it has no `body`) or a map of field name to text; a field the index does not name counts with weight 1. A key that is already there is replaced. |
| `REMOVE(ix, key$)` | Takes the document out; TRUE when there was one. |
| `HAS(ix, key$)` | Whether a document is filed under `key$`. |
| `DOCCOUNT(ix)` | The number of documents. |
| `STATS(ix)` | A map with `docs`, `terms` (terms in at least one document) and `avgdl`, the average weighted document length. |
| `TOKENS(ix, text$)` | The terms the index makes of a text, in order: folded, split, stop words out, stemmed. |
| `STEM$(word$, [lang$])` | The stem of one word for `"en"` (default) or `"de"`; other languages keep the word. |

### Searching

| Call | What it does |
|------|--------------|
| `QUERY(ix, query$, [opts])` | The matching documents, best first, as maps with `key` and `score`; equal scores in key order. Words are ORed; a part in double quotes must appear as a phrase. `opts`: `limit` (10; 0 for all), `all` (1: every query term must be in the document), `fuzzy` (0; 1 or 2: a query term the index does not hold is replaced by the closest indexed term within that many edits). |
| `SCORE(ix, query$, key$)` | The BM25 score of one document for a query; 0 when it does not match. |
| `SUGGEST(ix, word$, [limit_n], [max_d])` | Indexed words within `max_d` (2) edits of a word, for "did you mean": at most `limit_n` (5) maps with `word` (as first seen in a document), `distance` and `docs`, closest first, then the most frequent. |
| `RRF(lists, [k_const], [limit_n])` | Result lists merged by reciprocal rank fusion: a key scores the sum of `1 / (k_const + rank)` over the lists it is in, rank counted from 1, `k_const` 60. A list holds maps with a `key` (as QUERY returns them) or plain keys. The merged maps with `key` and `score`, best first; `limit_n` 0 keeps all. |

### Files

| Call | What it does |
|------|--------------|
| `WRITEFILE(ix, path$)` | Writes the index settings and every document's fields to a text file. |
| `READFILE(path$)` | A new index built from such a file; the handle. A file that is not an index file throws. |

## Analysis

A text becomes terms in four steps.

1. **Folding.** Letters beyond ASCII become ASCII the way unidecode writes
   them: all of U+00C0 to U+00FF (`é` to `e`, `ß` to `ss`, `Æ` to `AE`)
   and the common letters of Latin Extended-A (`ł`, `ś`, `č`, `ő`, `œ`,
   `ž` and their capitals). A German index writes `ä`, `ö`, `ü` as `ae`,
   `oe`, `ue`, so `Müller`, `Mueller` and `MÜLLER` are one term; an English
   index writes `a`, `o`, `u`. Any other character separates words.
2. **Words.** Lower case, split at everything that is not a letter or a
   digit: `e-mail user@example.org` is `e mail user example org`.
3. **Stop words.** A short list for each language (`the`, `and`, `of`, ...;
   `der`, `und`, `nicht`, ...). `lang` `"none"` and `stopwords` 0 keep
   every word.
4. **Stemming.** Light suffix stripping, no dictionary. Words of three
   letters or less and words without a letter stay as they are.
   - English: first one plural ending (`sses` to `ss`, `ies` to `y`,
     `ches`, `shes`, `xes`, `zes` lose `es`, a final `s` goes unless the
     word ends in `ss`, `us` or `is`), then one of `ing` (word of six
     letters or more), `ed` (five or more) or `ly` (five or more); after
     `ing` and `ed` a doubled final consonant is made single except `ll`,
     `ss`, `zz`. `studies` is `study`, `running` is `run`, `boxes` is
     `box`, `bus` stays.
   - German, on the folded word: one of `ern` (six letters or more), `em`,
     `en`, `er`, `es` (five or more) or `e` (four or more), then a final
     `s` (not `ss`) when four letters remain. `Kunden` is `kund`,
     `Geschäfte` is `geschaeft`.

Query words go through the same steps, so `Studies` finds `study`.

## Ranking

`QUERY` scores a document with BM25 exactly as `rank_bm25.BM25Okapi`
(0.2.2) does:

- `idf = log(N - n + 0.5) - log(n + 0.5)` for a term in `n` of `N`
  documents. A term in more than half the documents has a negative idf;
  it gets `epsilon * (average idf of all terms)` instead.
- Each query term adds `idf * f * (k1 + 1) / (f + k1 * (1 - b + b * dl / avgdl))`,
  with `f` its frequency in the document, `dl` the document length and
  `avgdl` the average length. A term written twice in the query counts
  twice.

A field weight counts every word of that field `weight` times: in
frequencies and in the document length. An index with `{"title": 2,
"body": 1}` scores what BM25Okapi scores on documents whose title words
are written twice. Weights need not be whole numbers.

A phrase in double quotes also scores its words as ordinary terms and
keeps only the documents where they stand at the same distances as in
the query; a stop word in the phrase holds its place, and a phrase never
spans two fields. With `fuzzy`, a query word not in the index is replaced
by the indexed term with the fewest edits (then the most documents, then
the alphabetically first); the trigram index narrows the candidates before
FUZZY.LEVENSHTEIN compares them.

## Notes

- REMOVE and the replacing ADD change the document counts at once: the
  scores afterwards are the ones of an index built without the old
  document. The postings of removed documents stay in memory until the
  index is written and read again.
- The index file is text: a header, one line per setting, and one line
  per document with its key and field texts in base64. READFILE analyses
  the texts again, so it costs as much as adding the documents.
- Several indexes live side by side; their handles are small integers.
- Everything works compiled with `-c`.

Timings for 10000 generated documents of 60 words each (title weight 2,
about 650000 weighted tokens, 82 distinct terms), on one core of a
desktop PC:

| Step | Interpreted | Compiled (`-c`) |
|------|-------------|-----------------|
| Index 10000 documents | 7.3 s | 3.8 s |
| Three-word query (almost every document matches) | 140 ms | 30 ms |
| Fuzzy query, one misspelt word | 73 ms | 17 ms |

Self test: `tests/jdlibs/search_selftest.jdb`, 144 assertions; 72 of them
compare the ten best results and their scores (to 1e-9) with rank_bm25
0.2.2 on 304 English and German documents, 18 queries and three settings,
also after REMOVE, a replacing ADD and a READFILE
(`tests/jdlibs/fixtures/search_*.tsv`).
Demo: `jdb/demos/jdlibs/search_demo.jdb`.
