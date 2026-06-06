"""Mandelbrot escape iterations: 800x600 grid, max_iter=1000 (Numba LLVM-JIT)."""

from numba import njit

@njit(cache=True)
def mandel(W, H, MAXITER):
    total = 0
    for py in range(H):
        cy = -1.5 + 3.0 * py / H
        for px in range(W):
            cx = -2.0 + 3.0 * px / W
            x = 0.0
            y = 0.0
            x2 = 0.0
            y2 = 0.0
            i = 0
            while x2 + y2 < 4.0 and i < MAXITER:
                tmp = x2 - y2 + cx
                y = 2.0 * x * y + cy
                x = tmp
                x2 = x * x
                y2 = y * y
                i += 1
            total += i
    return total

# Warm-up
mandel(4, 4, 10)
print(f"total iterations = {mandel(800, 600, 1000)}")
