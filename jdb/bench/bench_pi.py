"""Pi via Leibniz series, 100M iterations (CPython interpreter)."""

N = 100_000_000
result = 0.0
sign = 1.0
for i in range(N):
    result += sign / (2 * i + 1)
    sign = -sign

print(f"pi = {4.0 * result:.15f}")
