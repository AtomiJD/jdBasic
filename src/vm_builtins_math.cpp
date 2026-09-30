// VM builtins: arithmetic, trigonometry, rounding, bits, RANDOM and the RNG.* generators.

#include "vm_internal.h"

// RNG.* generators: xoshiro256** seeded through splitmix64, one state per
// handle. The registry is process-wide, so ASYNC tasks share a handle.
struct RngState {
    uint64_t s[4];
};

static std::mutex g_rng_mtx;
static std::unordered_map<int64_t, RngState> g_rng_states;
static int64_t g_rng_next_id = 1;

static uint64_t rng_rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t rng_next64(RngState& st) {
    uint64_t* s = st.s;
    uint64_t result = rng_rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rng_rotl(s[3], 45);
    return result;
}

static RngState& rng_lookup(int64_t handle, const char* who) {
    auto it = g_rng_states.find(handle);
    if (it == g_rng_states.end())
        throw std::runtime_error(std::string(who) + ": unknown generator handle " + std::to_string(handle));
    return it->second;
}

static double rng_unit(RngState& st) {
    return (double)(rng_next64(st) >> 11) * (1.0 / 9007199254740992.0);
}

void VM::register_math_builtins() {
    // ── Math ─────────────────────────────────────────────────
    register_native("ABS", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::abs(args[0].to_double()));
    });
    register_native("SQR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::sqrt(args[0].to_double()));
    });
    register_native("INT", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(static_cast<int64_t>(args[0].to_double()));
    });
    register_native("SIN", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::sin(args[0].to_double()));
    });
    register_native("COS", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::cos(args[0].to_double()));
    });
    register_native("TAN", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::tan(args[0].to_double()));
    });
    register_native("LOG", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::log(args[0].to_double()));
    });
    register_native("EXP", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::exp(args[0].to_double()));
    });
    register_native("RND", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_f64(static_cast<double>(rand()) / RAND_MAX);
    });
    register_native("LOG10", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::log10(args[0].to_double()));
    });
    register_native("GCD", 2, -1, [](const std::vector<Value>& args) -> Value {
        // Variadic: GCD(a,b,c,...) via iterated Euclid.
        auto gcd2 = [](int64_t a, int64_t b) {
            if (a < 0) a = -a; if (b < 0) b = -b;
            while (b) { int64_t t = a % b; a = b; b = t; }
            return a;
        };
        int64_t r = args[0].to_int();
        for (size_t i = 1; i < args.size(); i++) r = gcd2(r, args[i].to_int());
        return Value::make_i64(r);
    });
    register_native("LCM", 2, -1, [](const std::vector<Value>& args) -> Value {
        auto gcd2 = [](int64_t a, int64_t b) {
            if (a < 0) a = -a; if (b < 0) b = -b;
            while (b) { int64_t t = a % b; a = b; b = t; }
            return a;
        };
        auto lcm2 = [&](int64_t a, int64_t b) -> int64_t {
            if (a == 0 || b == 0) return 0;
            int64_t g = gcd2(a, b);
            if (a < 0) a = -a; if (b < 0) b = -b;
            return (a / g) * b;
        };
        int64_t r = args[0].to_int();
        for (size_t i = 1; i < args.size(); i++) r = lcm2(r, args[i].to_int());
        return Value::make_i64(r);
    });
    register_native("ROTL", 2, 3, [](const std::vector<Value>& args) -> Value {
        // ROTL(x, n, [bits=64]) - rotate-left on an integer width 'bits' (8/16/32/64).
        uint64_t x = (uint64_t)args[0].to_int();
        int64_t n = args[1].to_int();
        int w = (args.size() >= 3) ? (int)args[2].to_int() : 64;
        if (w != 8 && w != 16 && w != 32 && w != 64)
            throw std::runtime_error("ROTL: bits must be 8, 16, 32 or 64");
        uint64_t mask = (w == 64) ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1;
        x &= mask;
        n = ((n % w) + w) % w;
        uint64_t r = ((x << n) | (x >> (w - n))) & mask;
        return Value::make_i64((int64_t)r);
    });
    register_native("ROTR", 2, 3, [](const std::vector<Value>& args) -> Value {
        uint64_t x = (uint64_t)args[0].to_int();
        int64_t n = args[1].to_int();
        int w = (args.size() >= 3) ? (int)args[2].to_int() : 64;
        if (w != 8 && w != 16 && w != 32 && w != 64)
            throw std::runtime_error("ROTR: bits must be 8, 16, 32 or 64");
        uint64_t mask = (w == 64) ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1;
        x &= mask;
        n = ((n % w) + w) % w;
        uint64_t r = ((x >> n) | (x << (w - n))) & mask;
        return Value::make_i64((int64_t)r);
    });
    register_native("FAC", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        if (n < 0) return Value::make_i64(0);
        // Stay in INT64 as long as possible (exact up to 20!), then continue
        // in FLOAT64 (still fits 170! ≈ 7.26e306). Beyond that, returns inf.
        int64_t ir = 1;
        int64_t i = 2;
        for (; i <= n; i++) {
            int64_t nr = (int64_t)((uint64_t)ir * (uint64_t)i);
            if (ir != 0 && nr / ir != i) break; // overflow → switch to double
            ir = nr;
        }
        if (i > n) return Value::make_i64(ir);
        double dr = (double)ir;
        for (; i <= n; i++) dr *= (double)i;
        return Value::make_f64(dr);
    });
    register_native("FLOOR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::floor(args[0].to_double()));
    });
    register_native("CEIL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::ceil(args[0].to_double()));
    });
    register_native("ROUND", 1, 2, [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        int dec = (args.size() >= 2) ? (int)args[1].to_int() : 0;
        double m = std::pow(10.0, dec);
        return Value::make_f64(std::round(v * m) / m);
    });
    register_native("CLAMP", 3, 3, [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        double lo = args[1].to_double();
        double hi = args[2].to_double();
        return Value::make_f64(v < lo ? lo : (v > hi ? hi : v));
    });
    register_native("TRUNC", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::trunc(args[0].to_double()));
    });
    register_native("SIGN", 1, 1, [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        return Value::make_i64(v > 0 ? 1 : (v < 0 ? -1 : 0));
    });
    // ── LERP ─────────────────────────────────────────────────
    register_native("LERP", [](const std::vector<Value>& args) -> Value {
        double a = args[0].to_double(), b = args[1].to_double(), t = args[2].to_double();
        return Value::make_f64(a + (b - a) * t);
    });

    register_native("SHL", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(args[0].to_int() << args[1].to_int());
    });
    register_native("SHR", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(args[0].to_int() >> args[1].to_int());
    });

    // ── 1. Additional Math ─────────────────────────────────────

    register_native("ASIN", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::asin(args[0].to_double()));
    });
    register_native("ACOS", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::acos(args[0].to_double()));
    });
    register_native("ATAN", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::atan(args[0].to_double()));
    });
    register_native("ATAN2", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::atan2(args[0].to_double(), args[1].to_double()));
    });
    register_native("SINH", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::sinh(args[0].to_double()));
    });
    register_native("COSH", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::cosh(args[0].to_double()));
    });
    register_native("TANH", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::tanh(args[0].to_double()));
    });
    register_native("SGN", [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        return Value::make_i64(v > 0 ? 1 : (v < 0 ? -1 : 0));
    });
    // Math constants, readable as MATH.PI or MATH.PI().
    register_const("MATH.PI", Value::make_f64(3.14159265358979323846));
    register_const("MATH.E", Value::make_f64(2.71828182845904523536));
    register_native("MATH.PI", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_f64(3.14159265358979323846);
    });
    register_native("MATH.E", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_f64(2.71828182845904523536);
    });
    register_native("RANDOM", [](const std::vector<Value>& args) -> Value {
        double lo = 0.0, hi = 1.0;
        if (args.size() == 1)      { hi = args[0].to_double(); }
        else if (args.size() >= 2) { lo = args[0].to_double(); hi = args[1].to_double(); }
        return Value::make_f64(lo + (double)rand() / RAND_MAX * (hi - lo));
    });
    register_native("RANDOMSEED", [](const std::vector<Value>& args) -> Value {
        srand((unsigned)args[0].to_int()); return Value::make_none();
    });

    // RNG.NEW(seed) -> handle of an independent, reproducible generator.
    register_native("RNG.NEW", 1, 1, [](const std::vector<Value>& args) -> Value {
        uint64_t x = (uint64_t)args[0].to_int();
        RngState st;
        for (int i = 0; i < 4; i++) {
            x += 0x9E3779B97F4A7C15ull;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            st.s[i] = z ^ (z >> 31);
        }
        std::lock_guard<std::mutex> lock(g_rng_mtx);
        int64_t id = g_rng_next_id++;
        g_rng_states[id] = st;
        return Value::make_i64(id);
    });

    // RNG.NEXT(h) -> a double in [0, 1) with 53 random bits.
    register_native("RNG.NEXT", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_rng_mtx);
        return Value::make_f64(rng_unit(rng_lookup(args[0].to_int(), "RNG.NEXT")));
    });

    // RNG.INT(h, lo, hi) -> an integer in lo..hi inclusive, without modulo bias.
    register_native("RNG.INT", 3, 3, [](const std::vector<Value>& args) -> Value {
        int64_t lo = args[1].to_int(), hi = args[2].to_int();
        if (lo > hi) throw std::runtime_error("RNG.INT: lo must not be greater than hi");
        std::lock_guard<std::mutex> lock(g_rng_mtx);
        RngState& st = rng_lookup(args[0].to_int(), "RNG.INT");
        uint64_t range = (uint64_t)hi - (uint64_t)lo + 1;
        if (range == 0) return Value::make_i64((int64_t)rng_next64(st));
        uint64_t threshold = (0 - range) % range;
        uint64_t x = rng_next64(st);
        while (x < threshold) x = rng_next64(st);
        return Value::make_i64((int64_t)((uint64_t)lo + x % range));
    });

    // RNG.FILL(h, n) -> an array of n doubles in [0, 1).
    register_native("RNG.FILL", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[1].to_int();
        if (n < 0) throw std::runtime_error("RNG.FILL: count must not be negative");
        std::lock_guard<std::mutex> lock(g_rng_mtx);
        RngState& st = rng_lookup(args[0].to_int(), "RNG.FILL");
        Value r = Value::make_array();
        auto& out = r.as_array()->elements;
        out.reserve((size_t)n);
        for (int64_t i = 0; i < n; i++) out.push_back(Value::make_f64(rng_unit(st)));
        return r;
    });

    // RNG.FREE(h) releases a generator; later use of the handle is an error.
    register_native("RNG.FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_rng_mtx);
        g_rng_states.erase(args[0].to_int());
        return Value::make_none();
    });
}
