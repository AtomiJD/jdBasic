# APL-style array programming in jdBasic

jdBasic borrows two pages from APL and numpy: every arithmetic operator
broadcasts over arrays, and bitwise operators do too. That makes it
practical to push real workloads — physics, cellular automata, SAT,
DSP — through whole-array operations instead of per-element loops.

This tutorial walks the path from "tight FOR loops" to "one line per
update step" using the demos under `bench/` and `jdb/`. Each section
ends with a take-away so you can decide *when* the APL form pays off
and when it doesn't.

> **Honest framing.** jdBasic is still an interpreted dynamic language.
> APL form wins by amortising per-step overhead across many elements.
> When the per-step compute is tiny (a single multiply per cell with an
> early escape, say) a tight scalar loop in compiled native mode beats
> the vector form. We'll show one such case.

---

## 1. The basics — broadcasting

Any arithmetic between an array and a scalar broadcasts the scalar:

```basic
LET xs = IOTA(5)        ' [1, 2, 3, 4, 5]
LET ys = xs * 10 + 1    ' [11, 21, 31, 41, 51]
```

Two arrays of the same length operate element-wise:

```basic
LET a = [1, 2, 3]
LET b = [10, 20, 30]
LET c = a + b           ' [11, 22, 33]
```

Comparison operators return arrays of 0/1 — useful as masks:

```basic
LET hit = xs > 3        ' [0, 0, 0, 1, 1]
LET kept = xs * hit     ' [0, 0, 0, 4, 5]
```

Trig and math functions also broadcast:

```basic
LET t = LINSPACE(0, 1, 4096)
LET wave = SIN(2 * PI * 440 * t)   ' 4096 samples in one call
```

This is what the synth demo uses: `jdb/synth_apl.jdb` builds a 4096-
sample additive synthesis buffer with one `+ amp * SIN(2π f t)` per
harmonic — five harmonics, five vector ops, no inner FOR.

---

## 2. Vectorised bitops (Phase 0+1)

`BAND`, `BOR`, `BXOR`, `SHL`, `SHR`, and `NOT` (bitwise) all broadcast.
That makes whole-array bitmask manipulation a one-liner. Subset-Sum
brute force, for example, enumerates all 2^N subsets in a single
pipeline:

```basic
' Each bit of `mask` says "include element i". Vector form: build a
' (2^N × N) bit matrix in one OUTER call, MATMUL against the value
' column, and ask if any row sums to target.
LET Bits = OUTER(IOTA(2 ^ N) - 1, IOTA(N) - 1, "SHR") BAND 1
LET Sums = FLATTEN(MATMUL(Bits, RESHAPE(xs, [N, 1])))
IF ANY(Sums = target) THEN ...
```

Same idea for cellular automata: for Game of Life, count neighbours by
shifting the field eight ways and summing, then express the update rule
as element-wise comparisons. `SHIFT` is 1D in jdBasic; you build a 2D
shift from a row-shift then a column-shift:

```basic
' Inner step (per iteration in bench/life_bench.jdb)
LET shifted = SHIFT_2D_COLS(SHIFT_2D_ROWS(field, dx), dy)
nf = ADD_2D(nf, shifted)
' …after summing eight (dy, dx) pairs, the update rule is vectorised:
LET stay = a_row * ((n_row = 2) + (n_row = 3))
LET born = (1 - a_row) * (n_row = 3)
LET nxt  = (stay + born) > 0
```

---

## 3. The whole-grid update pattern (Game of Life)

`bench/life_bench.jdb` runs Conway three ways: triple-loop native,
APL with 8 SHIFTs, ONNX 3×3-conv. Same 64×64 grid, same step:

| Backend            |  Time per step |
|--------------------|---------------:|
| Native triple loop |        11.4 ms |
| APL (8 SHIFT+ADD)  |         2.7 ms |
| ONNX 3×3 conv      |         1.2 ms |

The APL form wins by ~4× because every grid cell is updated in a fixed
number of vector passes, regardless of grid size. ONNX wins again
because the convolution kernel runs as a single SIMD operation.

`jdb/life_demo.jdb` is the live version — 200 × 150 cells, 60 FPS via
the ONNX backend.

---

## 4. When APL loses — Mandelbrot

`bench/mandelbrot_bench.jdb` is the honest counter-example. The
Mandelbrot escape iteration has two facts working against the vector
form:

1. **Per-cell early escape.** Most pixels escape in a handful of
   iterations. A tight FOR loop with `EXITFOR` skips the work.
2. **No cheap per-step compute.** Each iteration is 4–5 arithmetic
   ops — the per-element work is too small to amortise dispatch
   overhead, and the vector form has to iterate every cell to
   `MAX_ITER` because there's no SIMD-friendly early exit.

| Backend                          | Time |
|----------------------------------|-----:|
| Native loop (FOR + EXITFOR)      | 116 ms |
| APL (no early escape)            | 437 ms |

**APL loses 4×.** This is fine — the right tool depends on the
workload. Whole-grid update steps without escape (Life, Boids, FFT,
DSP) are where APL pays. Iterative per-cell convergence with cheap
inner code (Mandelbrot, Newton iteration) is where you want native.

---

## 5. Algorithm > notation — Subset-Sum

`bench/subset_sum_*.jdb` shows that algorithmic improvements dominate
notation-level speedups. The same problem, three implementations:

| N  | 2^N    | Brute APL [ms] | DP (vector) [ms] |
|----|--------|----------------|------------------|
| 12 | 4 096  |             11 |             0.27 |
| 16 | 65 536 |            211 |             0.33 |
| 18 | 262 K  |            919 |             0.39 |
| 50 | 1.1e15 |          (∞)   |             0.94 |

DP wins by **a complexity-class change** (O(N · target) instead of
O(2^N)). APL form would never reach N = 50 — DP gets there in a
millisecond. Reach for vectorisation as a constant-factor lever, not
a substitute for choosing the right algorithm.

---

## 6. ONNX as a generic SIMD backend (Phase 3a)

Tiny one-op `.onnx` files can act as accelerated kernels for any dense
linear-algebra primitive. `bench/matmul.onnx` is **136 bytes** of MatMul
with dynamic shapes — same model handles every size. `bench/conv3x3.onnx`
is 213 bytes and runs every 3×3 convolution.

Square N × N MatMul versus the native interpreter triple-loop:

| N   | native [ms] | ONNX [ms] | speedup |
|-----|-------------|-----------|---------|
| 80  |         7.5 |       0.5 |     16× |
| 240 |         140 |       1.9 |     73× |
| 512 |        1420 |        11 |    127× |

When *not* to use ONNX:

* **Tall-thin matmul.** Per-call overhead doesn't amortise — barely a
  win.
* **Tiny grids.** Same reason. Below ~60 elements, the native loop is
  competitive.
* **State-bearing iteration.** Anything that needs per-step branching
  on per-cell state. ONNX gives you whole-tensor ops; it does not give
  you "iterate while mask is true."

Generate the models once with `python bench/gen_matmul_onnx.py` /
`bench/gen_conv_onnx.py`. They're committed in the repo.

---

## 7. Putting it together — the SAT solver

`bench/sat_dpll_plus.jdb` combines several APL idioms with classical
algorithm tricks. Pure-literal elimination needs a per-variable
positive-vs-negative occurrence count over all open clauses — that's a
loop over clauses, but each clause's contribution is a vector
update:

```basic
LET col_v = (assigns SHR v) BAND 1   ' extract bit v of every assignment
                                     ' — no materialised column needed
```

Combined with DLIS branching and unit propagation, DPLL+ reaches
V = 50 (210 clauses) in ~280 ms — basic DPLL never gets there.

---

## 8. Gotchas

A few things the demos taught us, fixed in the current build but worth
keeping in mind:

* **Identifiers are case-insensitive.** `v` and `V` share the same
  storage slot. The compiler now rejects `DIM v` inside `FUNC F(V)` —
  but `LET v = ...` will still happily overwrite the parameter `V`.
  Pick distinct names (e.g. `vidx` vs `V`).
* **`LET row = arr[i]` is a reference, not a copy.** Earlier versions
  silently mutated `row` when `arr[i] = newvec` ran. The current build
  does proper slot replacement when both sides are flat 1D arrays of
  equal length, so the alias of the OLD row stays intact. Multi-D
  targets still broadcast.
* **APL form allocates.** Each vector op produces a fresh array. Hot
  loops with millions of iterations may benefit from in-place forms
  (`xs += vxs` is element-wise; the result still reuses storage).

---

## 9. Where to look in the repo

| File                          | Demonstrates                              |
|-------------------------------|-------------------------------------------|
| `jdb/synth_apl.jdb`           | Additive synthesis (5 vector ops/frame)   |
| `jdb/boids_apl.jdb`           | 5000 particles at 630 FPS                 |
| `jdb/life_demo.jdb`           | Live Conway 200×150 via ONNX-Conv         |
| `bench/life_bench.jdb`        | Native vs APL vs ONNX comparison          |
| `bench/mandelbrot_bench.jdb`  | Counter-example — APL loses to native loop |
| `bench/subset_sum_*.jdb`      | Brute → APL → DP progression              |
| `bench/sat_brute_dpll.jdb`    | Vectorised brute + basic DPLL             |
| `bench/sat_dpll_plus.jdb`     | DPLL with pure-literal + DLIS             |
| `bench/matmul_onnx.jdb`       | ONNX as a SIMD MatMul backend             |
| `bench/conv_onnx.jdb`         | ONNX as a 3×3 conv backend                |
| `bench/Results.md`            | Numbers from a recent run                 |

For the bigger picture — interpreter vs native compiler, classic Pi/
Mandelbrot benches against C++ and Python — see `bench/Results.md`.
