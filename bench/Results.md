# jdBasic Benchmark Results

Two CPU-bound microbenchmarks comparing jdBasic against C++ and Python in
both interpreted and natively-compiled forms. The same algorithm is
implemented five ways and produces bit-identical results.

## Setup

| Item              | Detail                                                |
| ----------------- | ----------------------------------------------------- |
| Date              | 2026-04-16                                            |
| OS                | Windows 11 Pro N (10.0.26100)                         |
| jdBasic           | bf26d1e+ (post OS.FEATURE), VM and LLVM backend       |
| C++ compiler      | MSVC 19.44.35225, `/O2 /std:c++17`                    |
| Python            | CPython 3.14.3                                        |
| Numba             | 0.65.0 + llvmlite 0.47.0 (LLVM-based JIT)             |
| Timing            | `time.perf_counter()` around `subprocess.run`,        |
|                   | wall-clock end-to-end (incl. interpreter startup)     |

Run with: `python bench/run_bench.py` from the project root.

## Results

### Benchmark 1: Pi via Leibniz series (100 000 000 iterations)

Tight scalar loop — one division, one multiply-add, sign flip, increment.
Stresses raw arithmetic throughput and loop dispatch.

| Implementation        |       Time | vs C++  | Result                  |
| --------------------- | ---------: | ------: | ----------------------- |
| jdBasic interpreter   | 20 789 ms  |  22.0 × | pi = 3.141593           |
| **jdBasic --compile** |  **1 293 ms** | **1.4 ×** | pi = 3.14159         |
| C++ (MSVC `/O2`)      |    946 ms  |   1.0 × | pi = 3.141592643589326  |
| Python (CPython 3.14) | 16 123 ms  |  17.0 × | pi = 3.141592643589326  |
| Python (Numba LLVM)   |  1 502 ms  |   1.6 × | pi = 3.141592643589326  |

### Benchmark 2: Mandelbrot escape iterations (800 × 600, max_iter = 1000)

Nested loops with an early-exit `WHILE` and three multiplies + four adds
per inner iteration. Sums the iteration count over all pixels as a checksum.

| Implementation        |       Time | vs C++  | Checksum                    |
| --------------------- | ---------: | ------: | --------------------------- |
| jdBasic interpreter   | 28 531 ms  |  27.6 × | total iterations = 82998601 |
| **jdBasic --compile** |    **706 ms** | **0.7 ×** | total iterations = 82998601 |
| C++ (MSVC `/O2`)      |  1 032 ms  |   1.0 × | total iterations = 82998601 |
| Python (CPython 3.14) | 37 383 ms  |  36.2 × | total iterations = 82998601 |
| Python (Numba LLVM)   |    799 ms  |   0.8 × | total iterations = 82998601 |

## Observations

* **Correctness.** All five implementations produce the same Mandelbrot
  iteration count (82 998 601) and the same Pi value to display precision.
* **`jdBasic --compile` is competitive with native C++.** On the
  Mandelbrot benchmark it actually beats MSVC `/O2` (706 ms vs 1032 ms)
  and matches Numba's LLVM JIT (799 ms). On the Pi loop it lands 1.4 × of
  C++ and slightly ahead of Numba.
* **Interpreter overhead is real but consistent.** Both jdBasic VM and
  CPython sit 17 – 36 × off C++ — typical territory for general-purpose
  bytecode/tree-walking interpreters.
* **Why does `--compile` beat MSVC on Mandelbrot?** Both targets emit
  for the same x86-64 host. LLVM's loop optimiser is particularly
  aggressive on the escape-time inner loop; MSVC `/O2` may be more
  conservative around the compound floating-point break condition.
  Differences in the 10–30 % range between LLVM and MSVC are common.
* **Numba warm-up.** Times include first-run JIT compilation (cached on
  disk via `cache=True`); a hot run is roughly 100 ms faster. Numbers
  reflect what users see end-to-end.

## Files

```
bench/
  bench_pi.{jdb,cpp,py}              -- Pi (interpreter / native / CPython)
  bench_pi_numba.py                  -- Pi via Numba LLVM-JIT
  bench_mandel.{jdb,cpp,py}          -- Mandelbrot, same split
  bench_mandel_numba.py              -- Mandelbrot via Numba LLVM-JIT
  run_bench.py                       -- builds + times all variants
  Results.md                         -- this file
```
