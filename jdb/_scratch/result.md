# Benchmark Results

**Test:** `num_of_values = 10000`, `num_of_values_to_process = 5000`, 10 runs each

## calc_partial_result

| Language | Mean (s) | Std Dev (s) | Result | vs C |
|----------|----------|-------------|--------|------|
| **jdBasic Native** | **0.000013** | **0.000000** | **78093734373750** | **385x faster** |
| jdBasic Interpreter | 0.001057 | 0.000004 | 78093734373750 | 4.7x faster |
| C | 0.005000 | 0.000471 | 78093734373750 | baseline |
| Cython | 0.005000 | 0.000737 | 78093734373750 | 1.0x |
| Julia | 0.012500 | 0.006603 | 78093734373750 | 2.5x slower |
| C# | 0.061540 | 0.033642 | 78093734373750 | 12x slower |
| Python | 5.209375 | 0.105768 | 78093734373750 | 1042x slower |

## calc_partial_conditional_result

| Language | Mean (s) | Std Dev (s) | Result | vs C |
|----------|----------|-------------|--------|------|
| **jdBasic Native** | **0.032109** | **0.000528** | **39698235518841** | **on par** |
| C | 0.032200 | 0.001229 | 39698235518841 | baseline |
| Cython | 0.028999 | 0.000666 | 39698235518841 | 1.1x faster |
| Julia | 0.053200 | 0.008230 | 39698235518841 | 1.7x slower |
| C# | 0.299488 | 0.101629 | 39698235518841 | 9.3x slower |
| jdBasic Interpreter | 1.467769 | 0.009973 | 39698235518841 | 46x slower |
| Python | 6.353125 | 0.026760 | 39698235518841 | 197x slower |

## Summary

- **jdBasic Native is faster than C** on `calc_partial_result` due to algebraic O(n) optimization
- **jdBasic Native matches C** on `calc_partial_conditional_result` (32ms vs 32ms)
- **46x speedup** over the jdBasic interpreter on the conditional benchmark
- **197x faster than Python** on the conditional benchmark
- All results are numerically identical across all languages

## Environment

- jdBasic v1.0 (Build 2), Windows 11, full feature build (COM, HTTP, GFX, ImGui, ONNX, LLM, NATIVEC)
- LLVM 22.1.3 backend, x86_64-pc-windows-msvc target
- C results from original benchmark reference
