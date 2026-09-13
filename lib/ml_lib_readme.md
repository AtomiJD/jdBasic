# ML - the scikit-learn essentials

`lib/ml.jdb` puts the everyday part of scikit-learn next to STATS and DF:
a seeded train and test split, scaling and one-hot encoding; linear and
ridge regression, logistic regression, k nearest neighbours, k-means and
a decision tree; and the metrics that judge them. Every model has the
same `FIT` and `PREDICT`, and every model is a map that JSON can store.

Stands in for: scikit-learn (preprocessing, linear_model, neighbors,
cluster, tree, metrics).

## Quick start

```basic
IMPORT ML

DIM parts = ML.TRAINTEST(rows, labels, 0.2, 7)
DIM scaler = ML.FIT("standard", parts{"x_train"}, [])
DIM x_train = ML.TRANSFORM(scaler, parts{"x_train"})
DIM x_test = ML.TRANSFORM(scaler, parts{"x_test"})

DIM model = ML.FIT("logistic", x_train, parts{"y_train"})
DIM guessed = ML.PREDICT(model, x_test)
PRINT ML.ACCURACY(parts{"y_test"}, guessed); "  F1 "; ML.F1(parts{"y_test"}, guessed)

TXTWRITER "churn.json", ML.TOJSON$(model)
DIM again = ML.FROMJSON(TXTREADER$("churn.json"))
```

Rows are arrays of numbers, `[[5.1, 3.5, 1.4, 0.2], ...]`; labels are an
array of numbers or text, targets an array of numbers.

## Preprocessing

| Call | What it does |
|------|--------------|
| `TRAINTEST(rows, y, [test_share], [seed])` | Shuffles rows and labels together with a seed and answers `x_train`, `x_test`, `y_train`, `y_test`; the test part holds `test_share` (0.25) of the rows, rounded up. |
| `FIT("standard", rows, [])` | Means and population deviations of the columns (`mean`, `scale`); a constant column keeps scale 1. |
| `FIT("minmax", rows, [])` | Minimum and maximum of the columns (`min`, `max`). |
| `FIT("onehot", values, [])` | The sorted categories of one column of values. |
| `TRANSFORM(model, rows)` | Scaled rows, or one row of 0 and 1 per value; an unknown category is all zeros. |

## Models

| Kind | Options | Model |
|------|---------|-------|
| `linear` | | least squares on centred data: `coef`, `intercept` |
| `ridge` | `alpha` (1) | the same with `alpha` added to the diagonal; the intercept is not penalised |
| `logistic` | `C` (1), `max_iter` (100), `tol` (1e-10) | multinomial logistic regression minimising `C * log loss + |W|^2 / 2`, as scikit-learn's default: `coef` (a row per class), `intercept`, `classes` |
| `knn` | `k` (5) | the training rows; the class most of the `k` nearest rows (Euclidean) have, ties to the first class |
| `kmeans` | `k` (3), `init` (starting centres), `seed` (42), `max_iter` (300) | Lloyd's algorithm from the given centres or from k-means++ with the seed: `centers`, `labels`, `inertia` |
| `tree` | `max_depth`, `min_samples_split` (2) | a CART classification tree on the Gini impurity, splitting at midpoints between neighbouring values |

| Call | What it does |
|------|--------------|
| `FIT(kind$, rows, y, [opts])` | Fits a model; `y` is `[]` for k-means and the scalers. |
| `PREDICT(model, rows)` | Targets of a regression, labels of a classifier (numbers or text as they were given), the cluster of each row for k-means. |
| `PROBA(model, rows)` | A row of class probabilities per input row in the order of `CLASSES`: the softmax of a logistic model, the vote shares of knn. |
| `SCORE(model, rows, y)` | Accuracy for a classifier, R squared for a regression, the inertia for k-means. |
| `CLASSES(model)` | The labels of a classifier, sorted. |
| `TOJSON$(model)` / `FROMJSON(text$)` | The model as JSON and back. |

The logistic regression is solved by Newton's method with a line search;
the linear models by the normal equations through `SOLVE`.

## Metrics

| Call | What it does |
|------|--------------|
| `ACCURACY(y_true, y_pred)` | The share of right labels. |
| `PRECISION / RECALL / F1(y_true, y_pred, [average$])` | `binary` for the label 1 (the default when the labels are 0 and 1), `macro` (the default otherwise), `weighted` by support, or `micro`. |
| `CONFUSION(y_true, y_pred)` | Rows of counts: the true label down, the predicted one across, both sorted. |
| `R2(y_true, y_pred)` / `MSE` / `MAE` | For regressions. |

## Notes

- Checked against scikit-learn 1.9.1 on iris and the diabetes set
  (`tests/jdlibs/fixtures/ml_*`), trained on the same rows: scaler
  statistics to 1e-9, linear and ridge coefficients to 1e-6, logistic
  coefficients to 1e-4 and its probabilities to 1e-5, k-means centres
  and labels exactly, and the predictions of the logistic regression,
  knn and a depth-3 tree row for row; the metrics to 1e-12.
- The tree breaks a tie between equally good splits by the lower column,
  scikit-learn by a random order, so two trees can differ in structure
  while they classify alike.
- The data is held as arrays of rows, so a set of a few thousand rows is
  comfortable; the logistic regression takes about 0.8 s interpreted and
  0.06 s compiled on the 120 iris training rows.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/ml_selftest.jdb`. Demo: `jdb/demos/jdlibs/ml_demo.jdb`.
