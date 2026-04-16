// Pi via Leibniz series, 100M iterations.
#include <cstdio>

int main() {
    const long N = 100000000;
    double result = 0.0;
    double sign = 1.0;
    for (long i = 0; i < N; ++i) {
        result += sign / (2.0 * i + 1.0);
        sign = -sign;
    }
    printf("pi = %.15f\n", 4.0 * result);
    return 0;
}
