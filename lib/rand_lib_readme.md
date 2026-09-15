# RAND - distributions, weighted choice and sampling

`lib/rand.jdb` draws from the common distributions, picks by weight and
samples, shuffles and permutes lists. Every draw comes from the `RNG`
builtins (xoshiro256** seeded through splitmix64), so a seed gives the
same values in the interpreter and compiled with `-c`.

Stands in for: numpy.random, Python's random.

## Quick start

```basic
IMPORT RAND

DIM gen = RAND.GENERATOR(2026)
PRINT RAND.INTRANGE(gen, 1, 6); "  "; RAND.NORMAL(gen, 100, 15)
DIM tiers = ["common", "rare", "epic"]
DIM loot = RAND.ALIAS([70, 25, 5])
PRINT tiers[RAND.ALIASDRAW(gen, loot)]
PRINT RAND.SHUFFLED(gen, ["a", "b", "c", "d"])
RAND.FREE(gen)
```

Every call takes the generator first. `0` means the module's own default
generator, seeded with 1 until `SEED` says otherwise.

## Generators

| Call | What it does |
|------|--------------|
| `GENERATOR(seed)` | A new generator with its own stream. |
| `FREE(gen)` | Releases it. |
| `SEED(n)` | Restarts the default generator (`0`) from n. |

Two generators never share a position: drawing from one leaves the
other where it was.

## Numbers

| Call | Draws | Method |
|------|-------|--------|
| `UNIT(gen)` | a number in `[0, 1)` | 53 bits of the next output |
| `UNIFORM(gen, low, high)` | a number in `[low, high)` | scaled `UNIT` |
| `INTRANGE(gen, low, high)` | a whole number, both ends included | rejection, no modulo bias |
| `NORMAL(gen, [mu], [sigma])` | normal, 0 and 1 by default | Box-Muller, one pair of uniforms per value |
| `LOGNORMAL(gen, [mu], [sigma])` | `EXP` of a normal | |
| `EXPONENTIAL(gen, [rate])` | exponential, rate 1 by default | inversion |
| `TRIANGULAR(gen, low, mode, high)` | triangular | inversion |
| `GAMMA(gen, shape, [scale])` | gamma | Marsaglia-Tsang for shape 1 and up, `G(shape + 1) * U^(1/shape)` below |
| `BETA(gen, a, b)` | beta | two gammas |
| `POISSON(gen, lambda)` | Poisson | multiplication below 10, Hormann's PTRS from 10 |
| `BINOMIAL(gen, n, p)` | binomial | inversion while `n * min(p, 1 - p)` is 30 or less, Hormann's BTRS above |
| `DRAWS(gen, kind$, n, [p1], [p2], [p3])` | n draws as an array | the calls above |

`DRAWS` kinds are `unit uniform int normal lognormal exponential gamma
beta triangular poisson binomial`, with the parameters in the order the
single calls take them: `RAND.DRAWS(gen, "gamma", 1000, 2.5, 1)`.

The Box-Muller normal discards the second value of each pair, so the
n-th normal draw never depends on whether an odd number came before it.

## Choosing and sampling

| Call | What it does |
|------|--------------|
| `WEIGHTED(gen, weights)` | An index, drawn in proportion to the weights, by walking their running sum. Right for a one-off draw. |
| `ALIAS(weights)` | Builds an alias table (Vose) and answers its id. |
| `ALIASDRAW(gen, table)` | An index from that table in constant time. Right for many draws. |
| `CHOICE(gen, list)` | One entry. |
| `CHOICES(gen, list, k)` | k entries with replacement. |
| `SAMPLE(gen, list, k)` | k entries without replacement (partial Fisher-Yates). |
| `SHUFFLED(gen, list)` | A new array with the entries in another order; the list given is left alone. |
| `PERMUTATION(gen, n)` | `0` to `n - 1` in random order. |
| `RESERVOIR(gen, list, k)` | k entries of a list, each kept with the same chance (Algorithm R). |
| `RESERVOIR_START(k)` | A reservoir for a stream of items; answers its id. |
| `RESERVOIR_OFFER(gen, box, item$)` | Offers one item. |
| `RESERVOIR_ITEMS(box)` / `RESERVOIR_SEEN(box)` | What it holds, and how many items it was offered. |

Indexes are 0-based. A weight of zero is never drawn. Reservoirs hold
their items as text.

## Checked against scipy

The distributions were checked on 20000 draws each (seed 20260915)
against `scipy.stats`: Kolmogorov-Smirnov for the continuous ones,
chi-square with bins merged to an expected count of at least 5 for the
discrete ones. A distribution passes when p is above 0.001, which a
correct sampler fails about once in a thousand checks, while a wrong
shape or a wrong parameter drives p far below it at this sample size.
All passed; the lowest p (0.0087, Poisson with lambda 57) was repeated at
200000 draws with three other seeds and lambdas and came out at 0.94,
0.19 and 0.46. The numbers are in `tests/jdlibs/fixtures/rand_ks.tsv`.

The self test repeats the moments on every run: the mean within 5
standard errors, the variance within 15 percent, and chi-square below
the p = 0.001 critical value for alias tables, running-sum weights and
reservoirs.

## Notes

- The same seed gives exactly the same sequence in the interpreter and
  compiled; the self test pins the first draws of every call in
  `tests/jdlibs/fixtures/rand_sequences.tsv`. Whole numbers are the same
  on every platform; continuous values pass through `LOG`, `EXP`, `SQR`
  and `COS`, so another C library can change their last digits.
- Rejection loops give up with an error after 100000 tries, which a
  valid parameter never reaches.
- Wrong parameters throw: a negative sigma, a shape or rate of zero, p
  outside 0 to 1, a mode outside the triangle, a sample larger than the
  list, weights that add up to nothing.
- Not for secrets; use `CODEC.RANDOMBYTES$` there.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/rand_selftest.jdb`. Demo: `jdb/demos/jdlibs/rand_demo.jdb`.
