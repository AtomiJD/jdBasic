// Mandelbrot escape iterations: 800x600 grid, max_iter=1000.
#include <cstdio>
#include <cstdint>

int main() {
    const int W = 800, H = 600, MAXITER = 1000;
    int64_t total = 0;
    for (int py = 0; py < H; ++py) {
        double cy = -1.5 + 3.0 * py / H;
        for (int px = 0; px < W; ++px) {
            double cx = -2.0 + 3.0 * px / W;
            double x = 0.0, y = 0.0, x2 = 0.0, y2 = 0.0;
            int iter = 0;
            while (x2 + y2 < 4.0 && iter < MAXITER) {
                double tmp = x2 - y2 + cx;
                y = 2.0 * x * y + cy;
                x = tmp;
                x2 = x * x;
                y2 = y * y;
                ++iter;
            }
            total += iter;
        }
    }
    printf("total iterations = %lld\n", (long long)total);
    return 0;
}
