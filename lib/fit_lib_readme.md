# FIT - least squares, curve fits, interpolation, roots

`lib/fit.jdb` is the numerical layer between the matrix builtins and a
result you can use: a least squares solution for a system that has none
exactly, a polynomial or any model fitted to measurements with the
uncertainty of each parameter, a spline or a straight-line interpolant
through points, the place where a function crosses zero, and the lowest
point of a function in an interval.

Every function follows its numpy or scipy counterpart step for step where
the algorithm allows it; the self test compares against numpy 2.5 and
scipy 1.18.1.

Stands in for: numpy.linalg.lstsq and pinv, numpy.polyfit, polyval,
polyder and interp, scipy.optimize.curve_fit, brentq, bisect, newton and
minimize_scalar, scipy.interpolate.CubicSpline.

## Quick start

```basic
IMPORT FIT

FUNC Decay(t, p)
    DIM amp = p[0]
    DIM rate = p[1]
    DIM floor_level = p[2]
    RETURN amp * EXP(-rate * t) + floor_level
ENDFUNC

DIM line = FIT.POLYFIT(volts, readings, 1)            ' [slope, offset]
PRINT FIT.POLYVAL(line, 3.3)

DIM fitted = FIT.CURVEFIT(Decay@, seconds, counts, [100.0, 0.5, 0.0])
PRINT fitted{"params"}; " +- "; fitted{"errors"}

DIM spl = FIT.SPLINE(hours, temperatures)             ' not-a-knot
PRINT FIT.SPLINEVAL(spl, 13.5)

FUNC Excess(x)
    RETURN x * x * x - 2.0 * x - 5.0
ENDFUNC
PRINT FIT.BRENT(Excess@, 2.0, 3.0)                     ' 2.0945514815423265
```

A matrix is an array of rows. A model for `CURVEFIT` is a `FUNC` taking
one x and the parameter array; a function for the root finders and
`MINIMIZE` takes one number.

## Linear least squares

| Call | What it does |
|------|--------------|
| `LSTSQ(a, b, [rcond])` | The least squares solution of `a x = b` with the smallest norm, from the singular value decomposition. A map: `x`, `residual` (the sum of squared residuals), `rank`, `singular` (largest first). |
| `PINV(a, [rcond])` | The pseudo-inverse, columns by rows. |

Singular values at or below `rcond` times the largest count as zero;
`rcond` 0 (the default) means machine epsilon times the larger dimension,
numpy's default. A rank-deficient or underdetermined system gets the
minimum-norm answer, as numpy's does. numpy leaves the residual empty when
the rank is below the column count or there are no more rows than columns;
`LSTSQ` always reports the sum.

## Polynomials

| Call | What it does |
|------|--------------|
| `POLYFIT(x, y, degree, [weights])` | The least squares coefficients, highest power first. `weights` multiply the residuals (numpy's `w`, one over the standard deviation). The Vandermonde columns are scaled to unit length before the solve, as numpy does. |
| `POLYVAL(coeffs, x)` / `POLYVALS(coeffs, xs)` | The polynomial at one x or at an array of them. |
| `POLYDER(coeffs)` | The coefficients of the derivative. |

## Curve fits

| Call | What it does |
|------|--------------|
| `CURVEFIT(model, x, y, p0, [opts])` | Levenberg-Marquardt from the start values `p0`. A map: `params`, `errors`, `cov`, `chi2`, `iterations`, `converged`. |

| Option | Meaning |
|--------|---------|
| `sigma` | The standard deviation of each y; the residuals are divided by it. |
| `absolute_sigma` | TRUE keeps the covariance unscaled. |
| `maxiter` | The iteration limit (500). |

`errors` are the square roots of the covariance diagonal. As in
`curve_fit`, the covariance is the inverse of J^T J scaled by `chi2` over the
degrees of freedom unless `absolute_sigma` is set. The Jacobian is taken by
central differences. A fit needs more points than parameters; otherwise it
raises. Bounds on the parameters are not supported.

## Interpolation

| Call | What it does |
|------|--------------|
| `INTERP(xp, fp, x)` / `INTERPS(xp, fp, xs)` | The straight-line interpolant through sorted points; outside them the first or last value, as `numpy.interp`. |
| `SPLINE(x, y, [bc$])` | A cubic spline through strictly increasing x. A map: `x` and the coefficients `c3 c2 c1 c0` of each interval, a polynomial in `t - x[i]`. |
| `SPLINEVAL(spl, t)` / `SPLINEVALS(spl, ts)` | The spline at one t or an array; beyond the ends the first or last piece goes on, as scipy extrapolates. |
| `SPLINEDERIV(spl, t)` / `SPLINEDERIVS(spl, ts)` | Its first derivative. |

`bc$` is `not-a-knot` (default: the third derivative is continuous at the
second and the second-to-last point), `natural` (zero second derivative at
the ends) or `clamped` (zero slope at the ends), scipy's `CubicSpline`
conditions. The slopes come from one tridiagonal solve with partial
pivoting, the elimination LAPACK's `dgtsv` does, so a spline through
thousands of points costs linear time. Two points give a straight line,
three points with not-a-knot a single parabola, as in scipy.

## Roots and minima

| Call | What it does |
|------|--------------|
| `BRENT(fn, a, b, [xtol])` | A root between `a` and `b` by Brent's method, the steps of `scipy.optimize.brentq`; `xtol` 2e-12, relative tolerance four machine epsilons. |
| `BISECT(fn, a, b, [xtol])` | A root by halving, as `scipy.optimize.bisect`. |
| `NEWTON(fn, x0, [tol])` | A root near `x0` by the secant method, as `scipy.optimize.newton` without a derivative; `tol` 1.48e-8. |
| `NEWTONDER(fn, dfn, x0, [tol])` | Newton's method with the derivative `dfn`. |
| `MINIMIZE(fn, a, b, [xatol])` | The minimum in `[a, b]` by Brent's bounded method, as `minimize_scalar(method="bounded")`; `xatol` 1e-5. A map: `x`, `fun`, `iterations` (function evaluations), `converged`. |

A bracket without a sign change, or no convergence within the iteration
limit (100 for BRENT and BISECT, 50 for NEWTON), raises.

## Notes

- Tolerances of the self test: 1e-9 relative for the direct linear algebra;
  an ill-conditioned system (condition number 2.4e7) agrees to about
  eps times that number, as any two correct implementations do. Spline
  values, slopes and coefficients agree to 1e-10, roots to 1e-12, the
  bounded minimum to 1e-9 with the same number of evaluations.
- Curve fit parameters agree with `curve_fit` to 1e-6; errors and the
  covariance to 1e-4 of their largest entry. scipy's MINPACK takes a
  forward-difference Jacobian, FIT a central one, and the covariance
  inherits that difference.
- `SVD`, `SOLVE` and the funcref calls all work compiled, so everything
  works with `-c`.
- Pass numbers as doubles (`2.0`, not `2`) into a model or function you
  write, so a compiled program keeps them as doubles.

Self test: `tests/jdlibs/fit_selftest.jdb`. Demo: `jdb/demos/jdlibs/fit_demo.jdb`.
