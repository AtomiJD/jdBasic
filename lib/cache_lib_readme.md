# CACHE - remember what a call answered

`lib/cache.jdb` puts a lookup that costs time or money behind a map that
answers the second question for nothing. A cache holds at most so many
entries and throws out the one that was read longest ago; an entry older
than its lifetime is gone when it is next asked for. Both limits are off
when they are zero.

Stands in for: functools.lru_cache, cachetools.

## Quick start

```basic
IMPORT CACHE

DIM prices = CACHE.NEW({"max": 500, "ttl": 60})

DIM value = CACHE.GET(prices, sku$, Price@, sku$)

PRINT CACHE.STATS(prices){"hit_rate"}
```

`GET` is the whole point: it answers from the cache when it can and calls
the function when it cannot, and the caller cannot tell which happened.

## Reading and writing

| Call | What it does |
|------|--------------|
| `NEW([opts])` | A cache. `max` is how many entries it holds (0 for no limit), `ttl` how many seconds an entry stays fresh (0 for no expiry). |
| `GET(c, key$, fn, [arg])` | The value for the key, computed by the function when it is not there or no longer fresh. |
| `FETCH(c, key$, [fallback])` | The value, or the fallback. Counts as a read. |
| `PUT(c, key$, value)` | Puts a value in, replacing what was there. |
| `HAS(c, key$)` | Whether the key is there and still fresh. Counts as nothing. |
| `INVALIDATE(c, key$)` | Throws one key out. |
| `CLEAR(c)` | Throws everything out, keeping the counts. |
| `SIZE(c)` | How many entries are held. |
| `KEYS(c)` | The fresh keys, the one read longest ago first. |
| `STATS(c)` | `hits misses evictions expired size max ttl hit_rate`. |

A cache is a map, and the calls change it in place, so it can be handed
around and passed into a function without being given back.

## Which entry leaves

When the cache is full the entry that was read longest ago goes. A read
counts: `FETCH`, `GET` and a hit through `GET` all move an entry to the
back of the queue, a write does too, and `HAS` does not. `KEYS` returns
that queue, so the first key it names is the next one to leave.

## Keys

| Call | What it does |
|------|--------------|
| `KEY$(parts)` | One key from several parts, whatever shape they have. |

The parts may be numbers, strings, truth values, arrays and maps, nested
as deeply as they like. The same parts always give the same key, and two
different sets of parts do not collide: the pieces are joined with a
separator that cannot appear in text a program would use.

```basic
DIM key$ = CACHE.KEY$(["report", "monthly", {"region": "north", "year": 2026}])
DIM report = CACHE.GET(reports, key$, Build@, "north")
```

## The file on disk

| Call | What it does |
|------|--------------|
| `SAVE(c, path$)` | Writes what is in the cache, each entry with the time it had left. |
| `LOAD(path$, [opts])` | Reads one back. `max` and `ttl` given here win over the ones in the file. |

An entry whose time had already run out when the file was written is
dropped on the way back in. The time an entry has left is the time it
had at the moment of writing, so a cache that is loaded again carries on
from where it stopped rather than from the wall clock.

## Notes

- The lifetime is counted from a clock that only goes forward, so a
  change of the system time cannot make an entry fresh again.
- An entry is thrown out when it is asked for, not by a clock of its
  own, so `SIZE` may count an entry whose time has run out until
  something reads it.
- The counters survive `CLEAR`; `STATS` is about the cache's whole life.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/cache_selftest.jdb`. Demo: `jdb/demos/jdlibs/cache_demo.jdb`.
