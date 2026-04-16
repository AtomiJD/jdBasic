"""Pi via Leibniz series, 100M iterations (Numba LLVM-JIT)."""

from numba import njit

@njit(cache=True)
def leibniz(n):
    result = 0.0
    sign = 1.0
    for i in range(n):
        result += sign / (2 * i + 1)
        sign = -sign
    return 4.0 * result

# Warm-up: trigger compilation (timed separately).
leibniz(1)
print(f"pi = {leibniz(100_000_000):.15f}")
