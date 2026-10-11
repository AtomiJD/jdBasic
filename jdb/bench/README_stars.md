# update_stars: Vectorisation Benchmark

## Files
- `bench_stars.jdb`: single-N (140) detailed comparison of three impls
- `bench_stars_sweep.jdb`: crossover sweep N ∈ {140 ... 14000}, v1 vs v3

## Result

```
                  Interpreter           Native
            v1     v3   speed       v1     v3   speed
N=140       582   1218  0.48x       34    330   0.11x
N=280       580   1203  0.48x       36    322   0.11x
N=700       579   1107  0.52x       38    377   0.10x
N=1400      577   1099  0.53x       42    368   0.11x
N=2800      577   1431  0.40x       41    355   0.12x
N=7000      604   1743  0.35x       37    325   0.11x
N=14000     607   1781  0.34x       37    323   0.12x
```

(All ms-totals; 30000 iters at N=140 down to 300 iters at N=14000.)

## Why vector loses here

* Per frame the vector variant allocates **five to seven fresh N-element
  arrays** (mask, 1-mask, y*keep, new_x, x*keep, new_x*mask, sum). The
  allocation churn dominates the per-element work.
* The wrap condition `y >= SCR_H` is **sparse**: one to three of 140 stars
  wrap in a typical frame. Branch prediction hits above 95%, so the `FOR`
  loop almost always takes the no-op path.
* The native compiler inlines the `FOR` loop aggressively, about 17 times
  faster than the interpreter. The vector operations still go through arena
  allocation and runtime dispatch and gain only about 4 times, so v3 is
  **always** worse here, not just at small N.

## Conclusion

The `update_stars` implementation that `space_shooter.jdb` uses (`FOR` plus a
branch) is already near the optimum. An APL version would slow it down.

## When vectorisation wins in jdBasic

* **Large N and a dense condition**, where every element needs the operation
* **Plain arithmetic without a branch**. `star_y = star_y + speed` stays
  vectorised: the element work still runs in an internal loop with room for
  SIMD, but without a per-operation allocation.
* **Pairwise and outer-product shapes** (collision detection, distance
  matrices). There the vector form wins by orders of magnitude.
* **No per-element FUNC call.** `SELECT` with a lambda carries real overhead;
  a `FOR` loop is the better answer then.

## When FOR wins, or ties

* **Small N** (a few thousand at most) **and a sparse condition** (a tenth of
  the elements or fewer). Branch prediction plus writing in place beats the
  allocation cost of the vector operations.
* **Complex per-element logic** with a different code path per element kind,
  such as the `e_kind` dispatch in `update_enemies`
* **Iteration with an early exit.** `FOR` with `EXITFOR` always wins.

## Suspected real wins in space_shooter (untested)

* **collisions()**: pairwise N_BULLETS x N_ENEMIES = 64 x 24 = 1536 pairs. The
  outer-product form `dx^2 + dy^2 < r^2` should win well.
* **update_fx**: where every active effect is treated alike, vector arithmetic
  without intermediate masks could be faster.
* **draw_***: already batched through `GFX.PLOT_POINTS` and the matrix `LINE`.
