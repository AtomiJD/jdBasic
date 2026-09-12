# STATS - description, association, tests, distributions

`lib/stats.jdb` is the layer above the array reducers. `MEAN`, `MEDIAN`,
`STDEV` and `VARIANCE` are builtins already; this module adds the sample
forms, the shape of a sample, the measures that relate two samples, the
significance tests, and the three distributions those tests need.

Every function answers what `scipy.stats` answers for the same input, to
six decimals. The self test carries the comparison with the reference
values written out in full.

Stands in for: scipy.stats (the everyday part), statistics.

## Quick start

```basic
IMPORT STATS

DIM d = STATS.DESCRIBE(response_times)
PRINT d{"mean"}; " +- "; d{"sd"}; "   median "; d{"median"}; "   skew "; d{"skew"}

DIM t = STATS.TTEST2(control, variant)
PRINT "t "; t{"t"}; " on "; t{"df"}; " degrees, p "; t{"p"}

DIM fit = STATS.LINREGRESS(spend, revenue)
PRINT fit{"slope"}; " per euro, r squared "; fit{"r2"}
```

## Describing one sample

| Call | What it does |
|------|--------------|
| `DESCRIBE(v)` | One map: `n mean variance sd sem min q1 median q3 max range iqr skew kurtosis`. |
| `VAR(v, [ddof])` / `SD(v, [ddof])` | The variance and the deviation; `ddof` 1 (the default) is the sample form, 0 the population form the builtins use. |
| `SEM(v)` | The standard error of the mean. |
| `QUANTILE(v, share)` | The value at a share, interpolated between neighbours, the way numpy and R type 7 do it. |
| `IQR(v)` | The spread of the middle half. |
| `SKEW(v)` / `KURTOSIS(v)` | The lopsidedness and the excess over the normal curve, the biased estimators scipy reports by default. |
| `ZSCORES(v, [ddof])` | Every value in deviations from the mean; `ddof` 0 by default, as in scipy. |
| `OUTLIERS(v, [opts])` | The positions outside the fence. `method` is `iqr` (default, factor 1.5) or `z` (factor 3). |
| `RANKS(v)` | The rank of every value, ties sharing the average of their places. |

## Two samples together

| Call | What it does |
|------|--------------|
| `COV(x, y, [ddof])` | The covariance. |
| `CORR(x, y)` | The straight-line correlation. |
| `SPEARMAN(x, y)` | The correlation of the ranks, which sees any rising relation. |
| `CORRTEST(x, y)` | `r t df p` for the correlation, two sided. |
| `CORRMATRIX(columns)` | Every column against every other, as a matrix. |
| `LINREGRESS(x, y)` | `slope intercept r r2 df stderr intercept_stderr t p`. |

## Tests

| Call | What it answers |
|------|-----------------|
| `TTEST1(v, mu)` | Is the mean of one sample the value it should be? |
| `TTEST2(a, b, [equal_var])` | Do two samples have the same mean? Pooled by default, Welch with `FALSE`. |
| `TTESTPAIRED(a, b)` | The same subjects measured twice. |
| `CHISQ(observed, [expected])` | Do the counts follow the share they should? Without an expectation every class holds the same share. |
| `CHISQTABLE(table, [correction])` | Are the rows and columns of a table independent? A two by two table is corrected for continuity unless that is switched off. Returns `expected` as well. |
| `MANNWHITNEY(a, b, [continuity])` | Do two samples come from the same distribution, without assuming a shape? `u z p n1 n2`, read through the normal approximation with the tie correction. |

Every test returns a map; `p` is always the two-sided value.

## Distributions

| Call | What it does |
|------|--------------|
| `NORMPDF/NORMCDF/NORMPPF(x, [mu], [sd])` | The normal curve, its share below a value, and the value a share lies below. |
| `TPDF/TCDF/TPPF(x, df)` | The t distribution. |
| `CHI2PDF/CHI2CDF/CHI2PPF(x, df)` | The chi square distribution. |
| `ERF(x)` / `ERFC(x)` | The error function and what it leaves over. |

The two tails are computed from the incomplete gamma and beta functions
(the series and the continued fraction, by the modified Lentz method),
so the far tail keeps its digits: `NORMCDF(-6)` is 9.865876e-10 and not
zero. The quantiles are found by bisection on the distribution
function, which is exact to the last bit a double can hold.

## Notes

- A test needs at least two values per sample, a regression at least
  three points, and `KURTOSIS` at least four values; below that they
  answer zero rather than raising.
- `MANNWHITNEY` uses the normal approximation, which is what scipy does
  with `method="asymptotic"`. For a handful of values the exact
  permutation p value differs a little.
- The self test compares against scipy.stats 1.18.1.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/stats_selftest.jdb`. Demo: `jdb/demos/jdlibs/stats_demo.jdb`.
