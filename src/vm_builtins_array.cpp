// VM builtins: array creation, the APL primitives, reductions, linear algebra and statistics.

#include "vm_internal.h"

// ── Helpers for matrix operations ─────────────────────────────

static std::vector<Value> flatten_values(const Value& v) {
    std::vector<Value> flat;
    std::function<void(const Value&)> go = [&](const Value& val) {
        if (val.type == ValueType::ARRAY)
            for (auto& e : val.as_array()->elements) go(e);
        else flat.push_back(val);
    };
    go(v);
    return flat;
}



static void get_2d(const Value& m, int& rows, int& cols) {
    auto* a = m.as_array();
    rows = (int)a->elements.size();
    cols = (rows > 0 && a->elements[0].type == ValueType::ARRAY)
        ? (int)a->elements[0].as_array()->elements.size() : 0;
}

static double elem2d(const Value& m, int r, int c) {
    return m.as_array()->elements[r].as_array()->elements[c].to_double();
}

// Reduction argument shape. A reducer takes one array; a second argument
// names the axis and is only meaningful for a matrix.
enum class ReduceForm { WHOLE, ALONG_AXIS, BAD_SCALAR };

static ReduceForm reduce_form(const std::vector<Value>& args) {
    if (args.empty()) return ReduceForm::WHOLE;
    bool arr = args[0].type == ValueType::ARRAY;
    if (args.size() < 2) return arr ? ReduceForm::WHOLE : ReduceForm::WHOLE;
    // Second argument present: only an array of rows can be reduced along
    // an axis. A scalar here means someone wrote MAX(a, b) expecting a
    // two-argument maximum, which these functions have never been.
    if (!arr) return ReduceForm::BAD_SCALAR;
    auto* a = args[0].as_array();
    if (a->elements.empty() || a->elements[0].type != ValueType::ARRAY)
        return ReduceForm::WHOLE;
    return ReduceForm::ALONG_AXIS;
}

static double lane_sum(const std::vector<double>& v) {
    double s = 0; for (double d : v) s += d; return s;
}
static double lane_min(const std::vector<double>& v) {
    if (v.empty()) return 0;
    double m = v[0]; for (double d : v) if (d < m) m = d; return m;
}
static double lane_max(const std::vector<double>& v) {
    if (v.empty()) return 0;
    double m = v[0]; for (double d : v) if (d > m) m = d; return m;
}
static double lane_product(const std::vector<double>& v) {
    if (v.empty()) return 0;
    double s = 1; for (double d : v) s *= d; return s;
}
static double lane_mean(const std::vector<double>& v) {
    if (v.empty()) return 0;
    return lane_sum(v) / (double)v.size();
}
static double lane_median(const std::vector<double>& v) {
    if (v.empty()) return 0;
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    size_t n = s.size();
    return n % 2 ? s[n/2] : (s[n/2-1] + s[n/2]) / 2.0;
}
static double lane_variance(const std::vector<double>& v) {
    if (v.size() < 2) return 0;
    double m = lane_mean(v), ss = 0;
    for (double d : v) { double x = d - m; ss += x * x; }
    return ss / (double)v.size();
}
static double lane_stdev(const std::vector<double>& v) {
    return std::sqrt(lane_variance(v));
}
static double lane_any(const std::vector<double>& v) {
    for (double d : v) if (d != 0.0) return 1.0;
    return 0.0;
}
static double lane_all(const std::vector<double>& v) {
    for (double d : v) if (d == 0.0) return 0.0;
    return 1.0;
}

// Gather each lane along `dim` and reduce it. dim 0 walks down the rows and
// yields one value per column; dim 1 walks across a row.
static Value reduce_along_axis(const Value& m, int dim,
        const std::function<double(const std::vector<double>&)>& reduce_lane,
        bool as_bool = false) {
    int rows, cols; get_2d(m, rows, cols);
    Value r = Value::make_array();
    std::vector<double> lane;
    auto emit = [&](double d) {
        r.as_array()->elements.push_back(
            as_bool ? Value::make_bool(d != 0.0) : Value::make_f64(d));
    };
    if (dim == 0) {
        for (int c = 0; c < cols; c++) {
            lane.clear();
            for (int ro = 0; ro < rows; ro++) lane.push_back(elem2d(m, ro, c));
            emit(reduce_lane(lane));
        }
    } else {
        for (int ro = 0; ro < rows; ro++) {
            lane.clear();
            for (int c = 0; c < cols; c++) lane.push_back(elem2d(m, ro, c));
            emit(reduce_lane(lane));
        }
    }
    return r;
}

static Value make_matrix(int rows, int cols, double fill = 0.0) {
    Value m = Value::make_array();
    auto* ma = m.as_array();
    ma->elements.reserve(rows);
    for (int i = 0; i < rows; i++) {
        Value row = Value::make_array();
        auto* ra = row.as_array();
        ra->elements.reserve(cols);
        for (int j = 0; j < cols; j++) ra->elements.push_back(Value::make_f64(fill));
        ma->elements.push_back(std::move(row));
    }
    return m;
}

// ── ROTATE / SHIFT: one axis per entry of the shift vector ───────────────
// Both used to read only the first entry and move the outer vector, which on
// a matrix meant rows worked and columns were silently dropped. SHIFT also
// wrote its scalar fill into row slots, so the result was a ragged array whose
// LEN no longer matched its contents.
//
// The two keep the sign conventions they have always had on a vector, because
// programs depend on them and they differ on purpose:
//   ROTATE  out[i] = in[i + k]   (positive k pulls from ahead, APL's rotate)
//   SHIFT   out[i] = in[i - k]   (positive k pushes along, a shift register)

// A value shaped like proto but filled with fill, so a row that shifts off the
// edge comes back as a row of fill and not as a bare scalar.
static Value shaped_fill(const Value& proto, const Value& fill) {
    if (proto.type != ValueType::ARRAY) return fill;
    auto* pa = proto.as_array();
    Value r = Value::make_array();
    auto* out = r.as_array();
    out->elements.reserve(pa->elements.size());
    for (size_t i = 0; i < pa->elements.size(); i++)
        out->elements.push_back(shaped_fill(pa->elements[i], fill));
    return r;
}

static Value axis_move(const Value& v, const std::vector<int64_t>& shifts,
                       size_t depth, bool cyclic, const Value& fill) {
    if (depth >= shifts.size() || v.type != ValueType::ARRAY) return v;
    auto* arr = v.as_array();
    const size_t n = arr->elements.size();
    if (n == 0) return v;

    // deeper axes first, so this level only has to move whole elements
    std::vector<Value> kids;
    kids.reserve(n);
    for (size_t i = 0; i < n; i++)
        kids.push_back(axis_move(arr->elements[i], shifts, depth + 1, cyclic, fill));

    const int64_t k = shifts[depth];
    Value r = Value::make_array();
    auto* out = r.as_array();
    out->elements.reserve(n);
    if (cyclic) {
        const int64_t m = ((k % (int64_t)n) + (int64_t)n) % (int64_t)n;
        for (size_t i = 0; i < n; i++)
            out->elements.push_back(kids[(i + (size_t)m) % n]);
    } else {
        const Value blank = shaped_fill(kids[0], fill);
        for (size_t i = 0; i < n; i++) {
            const int64_t src = (int64_t)i - k;
            if (src >= 0 && src < (int64_t)n) out->elements.push_back(kids[(size_t)src]);
            else out->elements.push_back(blank);
        }
    }
    return r;
}

// A scalar, or one entry per axis. A longer vector than the array has axes is
// a mistake worth saying out loud rather than half-applying.
static std::vector<int64_t> shift_vector(const Value& v) {
    std::vector<int64_t> out;
    if (v.type == ValueType::ARRAY) {
        for (auto& e : v.as_array()->elements) out.push_back(e.to_int());
    } else {
        out.push_back(v.to_int());
    }
    return out;
}

static int array_rank(const Value& v) {
    int rank = 0;
    const Value* cur = &v;
    while (cur->type == ValueType::ARRAY && !cur->as_array()->elements.empty()) {
        rank++;
        cur = &cur->as_array()->elements[0];
    }
    return rank;
}

void VM::register_array_builtins() {
    // Array functions
    register_native("PUSH", 2, -1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        arr->elements.push_back(args[1]);
        return Value::make_i64(arr->elements.size());
    });
    register_native("POP", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        if (arr->elements.empty()) return Value::make_none();
        Value v = arr->elements.back();
        arr->elements.pop_back();
        return v;
    });

    // ── FILLV / COPYV - bulk array mutators ─────────────────────────
    // FILLV arr, value         → fills every leaf element with value
    // COPYV dst, src           → copies src into dst with cyclic broadcast
    // Both work as statement (no return needed) AND function form
    // (returns dst for chaining: result = FILLV(arr, 0)).
    register_native("FILLV", 2, 2, [](const std::vector<Value>& args) -> Value {
        if (args[0].type != ValueType::ARRAY)
            throw jdError(ErrCode::TYPE_MISMATCH, "FILLV: arg 1 must be ARRAY");
        if (args[1].type == ValueType::STRING)
            throw jdError(ErrCode::TYPE_MISMATCH, "FILLV: STRING value not supported");
        if (args[1].type == ValueType::OBJECT)
            throw jdError(ErrCode::TYPE_MISMATCH, "FILLV: OBJECT value not supported");
        const Value& fill_val = args[1];
        std::function<void(ArrayObj*)> fill = [&](ArrayObj* arr) {
            for (auto& elem : arr->elements) {
                if (elem.type == ValueType::ARRAY) fill(elem.as_array());
                else elem = fill_val;
            }
        };
        fill(args[0].as_array());
        return args[0];
    });
    register_native("COPYV", 2, 2, [](const std::vector<Value>& args) -> Value {
        if (args[0].type != ValueType::ARRAY)
            throw jdError(ErrCode::TYPE_MISMATCH, "COPYV: arg 1 (dst) must be ARRAY");
        // src can be scalar → behaves like FILLV (single value broadcast)
        if (args[1].type != ValueType::ARRAY) {
            if (args[1].type == ValueType::STRING)
                throw jdError(ErrCode::TYPE_MISMATCH, "COPYV: STRING source not supported");
            if (args[1].type == ValueType::OBJECT)
                throw jdError(ErrCode::TYPE_MISMATCH, "COPYV: OBJECT source not supported");
            const Value& fill_val = args[1];
            std::function<void(ArrayObj*)> fill = [&](ArrayObj* arr) {
                for (auto& elem : arr->elements) {
                    if (elem.type == ValueType::ARRAY) fill(elem.as_array());
                    else elem = fill_val;
                }
            };
            fill(args[0].as_array());
            return args[0];
        }
        // Flatten src to a leaf-vector; cyclic-broadcast into dst leaves.
        std::vector<Value> src_flat;
        std::function<void(const ArrayObj*)> flatten = [&](const ArrayObj* arr) {
            for (auto& elem : arr->elements) {
                if (elem.type == ValueType::ARRAY) flatten(elem.as_array());
                else src_flat.push_back(elem);
            }
        };
        flatten(args[1].as_array());
        if (src_flat.empty()) return args[0];
        size_t si = 0;
        std::function<void(ArrayObj*)> copy_in = [&](ArrayObj* arr) {
            for (auto& elem : arr->elements) {
                if (elem.type == ValueType::ARRAY) copy_in(elem.as_array());
                else {
                    elem = src_flat[si % src_flat.size()];
                    si++;
                }
            }
        };
        copy_in(args[0].as_array());
        return args[0];
    });

    // IOTA(N, [B=1], [S=1]) -> vector of N numbers starting at B with step S
    register_native("IOTA", 1, 3, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_array();
        int64_t n = args[0].to_int();
        double base = (args.size() >= 2) ? args[1].to_double() : 1.0;
        double step = (args.size() >= 3) ? args[2].to_double() : 1.0;

        Value result = Value::make_array();
        auto* arr = result.as_array();
        arr->elements.reserve(n);

        // Use integer path when base and step are whole numbers
        bool use_int = (base == (int64_t)base) && (step == (int64_t)step);
        if (use_int) {
            int64_t ib = (int64_t)base, is = (int64_t)step;
            for (int64_t i = 0; i < n; i++) {
                arr->elements.push_back(Value::make_i64(ib + i * is));
            }
        } else {
            for (int64_t i = 0; i < n; i++) {
                arr->elements.push_back(Value::make_f64(base + i * step));
            }
        }
        return result;
    });

    // RESHAPE(source_array, shape_vector) -> nested array
    // e.g. RESHAPE(IOTA(8), [2,2,2]) -> [[[1,2],[3,4]],[[5,6],[7,8]]]
    register_native("RESHAPE", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_none();

        // Flatten source into a flat list of values
        std::vector<Value> flat;
        std::function<void(const Value&)> flatten = [&](const Value& v) {
            if (v.type == ValueType::ARRAY) {
                for (auto& e : v.as_array()->elements) flatten(e);
            } else {
                flat.push_back(v);
            }
        };
        flatten(args[0]);

        // Read shape
        std::vector<int64_t> shape;
        if (args[1].type == ValueType::ARRAY) {
            for (auto& e : args[1].as_array()->elements)
                shape.push_back(e.to_int());
        } else {
            shape.push_back(args[1].to_int());
        }

        if (shape.empty()) return Value::make_array();

        // Recursive builder
        size_t idx = 0;
        std::function<Value(size_t)> build = [&](size_t dim) -> Value {
            if (dim == shape.size() - 1) {
                // Innermost dimension: create leaf array
                Value arr = Value::make_array();
                auto* a = arr.as_array();
                a->elements.reserve(shape[dim]);
                for (int64_t i = 0; i < shape[dim]; i++) {
                    a->elements.push_back(flat[idx % flat.size()]);
                    idx++;
                }
                return arr;
            }
            // Outer dimension: array of sub-arrays
            Value arr = Value::make_array();
            auto* a = arr.as_array();
            a->elements.reserve(shape[dim]);
            for (int64_t i = 0; i < shape[dim]; i++) {
                a->elements.push_back(build(dim + 1));
            }
            return arr;
        };

        return build(0);
    });

    // ── APPEND ───────────────────────────────────────────────
    register_native("APPEND", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::vector<Value> flat;
        for (auto& a : args) {
            if (a.type == ValueType::ARRAY)
                for (auto& e : a.as_array()->elements) flat.push_back(e);
            else flat.push_back(a);
        }
        Value r = Value::make_array();
        r.as_array()->elements = std::move(flat);
        return r;
    });

    // ── DIFF ─────────────────────────────────────────────────
    register_native("DIFF", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return args[0];
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        for (auto& e : a->elements) {
            bool found = false;
            for (auto& f : b->elements) {
                if (e.to_string() == f.to_string()) { found = true; break; }
            }
            if (!found) out->elements.push_back(e);
        }
        return r;
    });

    // ── TAKE / DROP ──────────────────────────────────────────
    register_native("TAKE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        int sz = (int)arr->elements.size();
        if (n >= 0) {
            for (int i = 0; i < n && i < sz; i++) out->elements.push_back(arr->elements[i]);
        } else {
            int start = std::max(0, sz + (int)n);
            for (int i = start; i < sz; i++) out->elements.push_back(arr->elements[i]);
        }
        return r;
    });

    register_native("DROP", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        int sz = (int)arr->elements.size();
        if (n >= 0) {
            for (int i = (int)n; i < sz; i++) out->elements.push_back(arr->elements[i]);
        } else {
            int end = sz + (int)n;
            for (int i = 0; i < end && i < sz; i++) out->elements.push_back(arr->elements[i]);
        }
        return r;
    });

    // ── REVERSE ──────────────────────────────────────────────
    register_native("REVERSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        auto* arr = args[0].as_array();
        auto* out = r.as_array();
        out->elements.assign(arr->elements.rbegin(), arr->elements.rend());
        return r;
    });

    // ── UNIQUE ───────────────────────────────────────────────
    register_native("UNIQUE", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        for (auto& e : arr->elements) {
            bool dup = false;
            for (auto& o : out->elements) {
                if (e.to_string() == o.to_string()) { dup = true; break; }
            }
            if (!dup) out->elements.push_back(e);
        }
        return r;
    });

    // ── SHUFFLE ──────────────────────────────────────────────
    register_native("SHUFFLE", 1, 1, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        r.as_array()->elements = args[0].as_array()->elements;
        auto& elems = r.as_array()->elements;
        for (int i = (int)elems.size() - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            std::swap(elems[i], elems[j]);
        }
        return r;
    });

    // ── FIND_IN_ARRAY ────────────────────────────────────────
    register_native("FIND_IN_ARRAY", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        std::string target = args[1].to_string();
        for (size_t i = 0; i < arr->elements.size(); i++) {
            if (arr->elements[i].to_string() == target) return Value::make_i64(i);
        }
        return Value::make_i64(-1);
    });

    // ── NORMALIZE ────────────────────────────────────────────
    register_native("NORMALIZE", [](const std::vector<Value>& args) -> Value {
        bool seen = false;
        double mn = 0, mx = 0;
        size_t n = 0;
        for_each_leaf(args[0], [&](const Value& v) {
            double d = v.to_double();
            if (!seen) { mn = mx = d; seen = true; }
            else if (d < mn) mn = d;
            else if (d > mx) mx = d;
            n++;
        });
        if (!seen) return Value::make_array();
        double range = mx - mn;
        Value r = Value::make_array();
        auto* out = r.as_array();
        out->elements.reserve(n);
        for_each_leaf(args[0], [&](const Value& v) {
            out->elements.push_back(Value::make_f64(range == 0 ? 0.0 : (v.to_double() - mn) / range));
        });
        return r;
    });

    // ── DISTANCE ─────────────────────────────────────────────
    register_native("DISTANCE", [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        double sum = 0;
        size_t n = std::min(a->elements.size(), b->elements.size());
        for (size_t i = 0; i < n; i++) {
            double d = a->elements[i].to_double() - b->elements[i].to_double();
            sum += d * d;
        }
        return Value::make_f64(std::sqrt(sum));
    });

    // ── GRADE ────────────────────────────────────────────────
    // Strings order lexicographically after every number; equal keys
    // keep their original order.
    register_native("GRADE", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        std::vector<int64_t> idx(arr->elements.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](int64_t a, int64_t b) {
            const Value& va = arr->elements[a];
            const Value& vb = arr->elements[b];
            bool a_str = (va.type == ValueType::STRING);
            bool b_str = (vb.type == ValueType::STRING);
            if (a_str != b_str) return !a_str;
            if (a_str) return va.as_string()->data < vb.as_string()->data;
            return va.to_double() < vb.to_double();
        });
        Value r = Value::make_array();
        for (auto i : idx) r.as_array()->elements.push_back(Value::make_i64(i));
        return r;
    });

    // ── XSORT ────────────────────────────────────────────────
    // SORT - simple ascending sort over a 1D array, alias for the
    // codegen-side jdb_sort helper. Strings sort lexicographically,
    // numerics by value. Multi-dim shape goes through XSORT.
    register_native("SORT", 1, 2, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        r.as_array()->elements = args[0].as_array()->elements;
        bool desc = (args.size() >= 2 && args[1].to_bool());
        auto& elems = r.as_array()->elements;
        bool string_sort = !elems.empty() && elems[0].type == ValueType::STRING;
        if (string_sort) {
            std::sort(elems.begin(), elems.end(), [desc](const Value& a, const Value& b) {
                const std::string& sa = a.as_string()->data;
                const std::string& sb = b.as_string()->data;
                return desc ? sa > sb : sa < sb;
            });
        } else {
            std::sort(elems.begin(), elems.end(), [desc](const Value& a, const Value& b) {
                return desc ? a.to_double() > b.to_double() : a.to_double() < b.to_double();
            });
        }
        return r;
    });
    register_native("XSORT", [](const std::vector<Value>& args) -> Value {
        // Type-aware ordering: numerics sort before strings, strings compare
        // lexicographically. Shared by the 1D path and the 2D
        // sort-rows-by-column path, so string columns work as sort keys.
        auto value_less = [](const Value& a, const Value& b) -> bool {
            bool a_str = (a.type == ValueType::STRING);
            bool b_str = (b.type == ValueType::STRING);
            if (a_str != b_str) return !a_str;
            if (a_str) return a.as_string()->data < b.as_string()->data;
            return a.to_double() < b.to_double();
        };
        Value r = Value::make_array();
        r.as_array()->elements = args[0].as_array()->elements;
        bool desc = (args.size() >= 3 && args[2].to_bool());
        auto& elems = r.as_array()->elements;
        // Check if 2D (sort rows by dimension)
        if (args.size() >= 2 && args[0].as_array()->elements.size() > 0 &&
            args[0].as_array()->elements[0].type == ValueType::ARRAY) {
            int dim = (int)args[1].to_int();
            std::sort(elems.begin(), elems.end(), [dim, desc, &value_less](const Value& a, const Value& b) {
                const Value& va = a.as_array()->elements[dim];
                const Value& vb = b.as_array()->elements[dim];
                return desc ? value_less(vb, va) : value_less(va, vb);
            });
        } else {
            std::sort(elems.begin(), elems.end(), [desc, &value_less](const Value& a, const Value& b) {
                return desc ? value_less(b, a) : value_less(a, b);
            });
        }
        return r;
    });

    // ── ROTATE ───────────────────────────────────────────────
    register_native("ROTATE", [](const std::vector<Value>& args) -> Value {
        auto shifts = shift_vector(args[1]);
        if (shifts.empty()) return args[0];
        if ((int)shifts.size() > array_rank(args[0]))
            throw std::runtime_error("ROTATE got " + std::to_string(shifts.size()) +
                " shifts for an array of rank " + std::to_string(array_rank(args[0])));
        return axis_move(args[0], shifts, 0, true, Value::make_i64(0));
    });

    // ── SHIFT ────────────────────────────────────────────────
    register_native("SHIFT", [](const std::vector<Value>& args) -> Value {
        auto shifts = shift_vector(args[1]);
        if (shifts.empty()) return args[0];
        if ((int)shifts.size() > array_rank(args[0]))
            throw std::runtime_error("SHIFT got " + std::to_string(shifts.size()) +
                " shifts for an array of rank " + std::to_string(array_rank(args[0])));
        Value fill = (args.size() >= 3) ? args[2] : Value::make_i64(0);
        return axis_move(args[0], shifts, 0, false, fill);
    });

    // ── Reductions: SUM, PRODUCT, MIN, MAX, ANY, ALL ─────────
    register_native("SUM", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("SUM takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_sum);
        double s = 0;
        for_each_leaf(args[0], [&](const Value& v) { s += v.to_double(); });
        return Value::make_f64(s);
    });

    register_native("PRODUCT", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("PRODUCT takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_product);
        double s = 1;
        for_each_leaf(args[0], [&](const Value& v) { s *= v.to_double(); });
        return Value::make_f64(s);
    });

    register_native("MIN", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("MIN takes one array; the second argument "
                "is the axis of a matrix, not a second value - use MIN([a, b])");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_min);
        bool seen = false;
        double m = 0;
        for_each_leaf(args[0], [&](const Value& v) {
            double d = v.to_double();
            if (!seen || d < m) { m = d; seen = true; }
        });
        return Value::make_f64(seen ? m : 0);
    });

    register_native("MAX", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("MAX takes one array; the second argument "
                "is the axis of a matrix, not a second value - use MAX([a, b])");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_max);
        bool seen = false;
        double m = 0;
        for_each_leaf(args[0], [&](const Value& v) {
            double d = v.to_double();
            if (!seen || d > m) { m = d; seen = true; }
        });
        return Value::make_f64(seen ? m : 0);
    });

    register_native("ANY", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("ANY takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_any, true);
        return Value::make_bool(any_leaf(args[0], [](const Value& v) { return v.to_bool(); }));
    });

    register_native("ALL", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("ALL takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_all, true);
        return Value::make_bool(!any_leaf(args[0], [](const Value& v) { return !v.to_bool(); }));
    });

    // ── SCAN ─────────────────────────────────────────────────
    register_native("SCAN", [this](const std::vector<Value>& args) -> Value {
        std::string op = args[0].as_string()->data;
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        if (arr->elements.empty()) return r;
        Value acc = arr->elements[0];
        out->elements.push_back(acc);
        for (size_t i = 1; i < arr->elements.size(); i++) {
            acc = apply_binary_op(op, acc, arr->elements[i]);
            out->elements.push_back(acc);
        }
        return r;
    });

    // ── TRANSPOSE ────────────────────────────────────────────
    register_native("TRANSPOSE", [](const std::vector<Value>& args) -> Value {
        int rows, cols; get_2d(args[0], rows, cols);
        if (cols == 0) return args[0];
        Value r = make_matrix(cols, rows);
        for (int i = 0; i < rows; i++)
            for (int j = 0; j < cols; j++)
                r.as_array()->elements[j].as_array()->elements[i] = args[0].as_array()->elements[i].as_array()->elements[j];
        return r;
    });

    // ── MATMUL ───────────────────────────────────────────────
    register_native("MATMUL", [](const std::vector<Value>& args) -> Value {
        int ar, ac, br, bc;
        get_2d(args[0], ar, ac); get_2d(args[1], br, bc);
        if (ac != br) throw std::runtime_error("MATMUL: incompatible dimensions");
        Value r = make_matrix(ar, bc);
        for (int i = 0; i < ar; i++)
            for (int j = 0; j < bc; j++) {
                double s = 0;
                for (int k = 0; k < ac; k++) s += elem2d(args[0], i, k) * elem2d(args[1], k, j);
                r.as_array()->elements[i].as_array()->elements[j] = Value::make_f64(s);
            }
        return r;
    });

    // ── MVLET ────────────────────────────────────────────────
    register_native("MVLET", [](const std::vector<Value>& args) -> Value {
        // MVLET(matrix, dim, index, vector)
        int rows, cols; get_2d(args[0], rows, cols);
        int dim = (int)args[1].to_int();
        int idx = (int)args[2].to_int();
        auto* vec = args[3].as_array();
        // Deep copy
        Value r = Value::make_array();
        for (auto& row : args[0].as_array()->elements) {
            Value nr = Value::make_array();
            nr.as_array()->elements = row.as_array()->elements;
            r.as_array()->elements.push_back(std::move(nr));
        }
        if (dim == 0) { // replace row
            for (int c = 0; c < cols && c < (int)vec->elements.size(); c++)
                r.as_array()->elements[idx].as_array()->elements[c] = vec->elements[c];
        } else { // replace column
            for (int ro = 0; ro < rows && ro < (int)vec->elements.size(); ro++)
                r.as_array()->elements[ro].as_array()->elements[idx] = vec->elements[ro];
        }
        return r;
    });

    // ── MVINS ────────────────────────────────────────────────
    // MVINS(matrix, dim, index, value) - INSERT a row (dim 0) or column
    // (dim 1) into a 2D matrix at `index`. index == size appends at the end.
    // `value` is a vector (one entry per row/col) OR a scalar broadcast to
    // fill the new row/column. Returns a new matrix (the original is untouched).
    register_native("MVINS", [](const std::vector<Value>& args) -> Value {
        int rows, cols; get_2d(args[0], rows, cols);
        int dim = (int)args[1].to_int();
        int idx = (int)args[2].to_int();
        const Value& val = args[3];
        bool is_vec = (val.type == ValueType::ARRAY);
        const std::vector<Value>* vec = is_vec ? &val.as_array()->elements : nullptr;
        auto pick = [&](int i) -> Value {
            if (!is_vec) return val;                       // scalar broadcast
            return (i >= 0 && i < (int)vec->size()) ? (*vec)[i] : Value::make_i64(0);
        };
        // Deep copy the rows so the source matrix is not mutated.
        Value r = Value::make_array();
        for (auto& row : args[0].as_array()->elements) {
            Value nr = Value::make_array();
            nr.as_array()->elements = row.as_array()->elements;
            r.as_array()->elements.push_back(std::move(nr));
        }
        auto& R = r.as_array()->elements;
        if (dim == 0) {                                    // insert a new ROW
            if (idx < 0) idx = 0;
            if (idx > rows) idx = rows;
            Value nr = Value::make_array();
            for (int c = 0; c < cols; c++) nr.as_array()->elements.push_back(pick(c));
            R.insert(R.begin() + idx, std::move(nr));
        } else {                                           // insert a new COLUMN
            for (int ro = 0; ro < rows; ro++) {
                auto& cells = R[ro].as_array()->elements;
                int ci = idx;
                if (ci < 0) ci = 0;
                if (ci > (int)cells.size()) ci = (int)cells.size();
                cells.insert(cells.begin() + ci, pick(ro));
            }
        }
        return r;
    });

    // ── STACK ────────────────────────────────────────────────
    register_native("STACK", [](const std::vector<Value>& args) -> Value {
        int dim = (int)args[0].to_int();
        Value r = Value::make_array();
        if (dim == 0) { // stack as rows
            for (size_t i = 1; i < args.size(); i++) {
                Value row = Value::make_array();
                if (args[i].type == ValueType::ARRAY)
                    row.as_array()->elements = args[i].as_array()->elements;
                else row.as_array()->elements.push_back(args[i]);
                r.as_array()->elements.push_back(std::move(row));
            }
        } else { // stack as columns (transpose of row stack)
            size_t num_vecs = args.size() - 1;
            size_t len = (num_vecs > 0 && args[1].type == ValueType::ARRAY) ? args[1].as_array()->elements.size() : 0;
            for (size_t i = 0; i < len; i++) {
                Value row = Value::make_array();
                for (size_t v = 1; v <= num_vecs; v++)
                    row.as_array()->elements.push_back(args[v].as_array()->elements[i]);
                r.as_array()->elements.push_back(std::move(row));
            }
        }
        return r;
    });

    // ── SLICE ────────────────────────────────────────────────
    register_native("SLICE", [](const std::vector<Value>& args) -> Value {
        int dim = (int)args[1].to_int();
        int idx = (int)args[2].to_int();
        auto* src = args[0].as_array();
        if (!src) throw std::runtime_error("SLICE: expected array");
        // 4-arg form: SLICE(arr, axis, start, count) - extract sub-matrix
        if (args.size() >= 4) {
            int count = (int)args[3].to_int();
            Value r = Value::make_array();
            if (dim == 0) { // extract rows [start..start+count)
                int end = std::min(idx + count, (int)src->elements.size());
                for (int i = idx; i < end; i++)
                    r.as_array()->elements.push_back(src->elements[i]);
            } else { // extract columns [start..start+count) from each row
                for (auto& row : src->elements) {
                    auto* ra = row.as_array();
                    Value nr = Value::make_array();
                    int end = std::min(idx + count, (int)ra->elements.size());
                    for (int j = idx; j < end; j++)
                        nr.as_array()->elements.push_back(ra->elements[j]);
                    r.as_array()->elements.push_back(std::move(nr));
                }
            }
            return r;
        }
        // 3-arg form: SLICE(arr, axis, index) - extract single row/column
        int rows, cols; get_2d(args[0], rows, cols);
        Value r = Value::make_array();
        if (dim == 0) { // extract row
            r.as_array()->elements = src->elements[idx].as_array()->elements;
        } else { // extract column
            for (int ro = 0; ro < rows; ro++)
                r.as_array()->elements.push_back(src->elements[ro].as_array()->elements[idx]);
        }
        return r;
    });

    // ── SOLVE (Ax=b) ─────────────────────────────────────────
    register_native("SOLVE", [](const std::vector<Value>& args) -> Value {
        int n, nc; get_2d(args[0], n, nc);
        auto* bv = args[1].as_array();
        // Build augmented matrix
        std::vector<std::vector<double>> A(n, std::vector<double>(n + 1));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) A[i][j] = elem2d(args[0], i, j);
            A[i][n] = bv->elements[i].to_double();
        }
        // Gaussian elimination with partial pivoting
        for (int col = 0; col < n; col++) {
            int best = col;
            for (int r = col + 1; r < n; r++) if (std::abs(A[r][col]) > std::abs(A[best][col])) best = r;
            std::swap(A[col], A[best]);
            if (std::abs(A[col][col]) < 1e-15) throw std::runtime_error("SOLVE: singular matrix");
            for (int r = col + 1; r < n; r++) {
                double f = A[r][col] / A[col][col];
                for (int j = col; j <= n; j++) A[r][j] -= f * A[col][j];
            }
        }
        // Back substitution
        std::vector<double> x(n);
        for (int i = n - 1; i >= 0; i--) {
            x[i] = A[i][n];
            for (int j = i + 1; j < n; j++) x[i] -= A[i][j] * x[j];
            x[i] /= A[i][i];
        }
        Value r = Value::make_array();
        for (double v : x) r.as_array()->elements.push_back(Value::make_f64(v));
        return r;
    });

    // ── INVERT ───────────────────────────────────────────────
    register_native("INVERT", [](const std::vector<Value>& args) -> Value {
        int n, nc; get_2d(args[0], n, nc);
        // Augmented [A|I]
        std::vector<std::vector<double>> A(n, std::vector<double>(2 * n, 0.0));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) A[i][j] = elem2d(args[0], i, j);
            A[i][n + i] = 1.0;
        }
        // Gauss-Jordan
        for (int col = 0; col < n; col++) {
            int best = col;
            for (int r = col + 1; r < n; r++) if (std::abs(A[r][col]) > std::abs(A[best][col])) best = r;
            std::swap(A[col], A[best]);
            double piv = A[col][col];
            if (std::abs(piv) < 1e-15) throw std::runtime_error("INVERT: singular matrix");
            for (int j = 0; j < 2 * n; j++) A[col][j] /= piv;
            for (int r = 0; r < n; r++) {
                if (r == col) continue;
                double f = A[r][col];
                for (int j = 0; j < 2 * n; j++) A[r][j] -= f * A[col][j];
            }
        }
        Value r = make_matrix(n, n);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                r.as_array()->elements[i].as_array()->elements[j] = Value::make_f64(A[i][n + j]);
        return r;
    });

    // ── CONVOLVE ─────────────────────────────────────────────
    register_native("CONVOLVE", [](const std::vector<Value>& args) -> Value {
        auto* src = args[0].as_array();
        auto* ker = args.size() >= 2 ? args[1].as_array() : nullptr;
        if (!src || !ker) throw std::runtime_error("CONVOLVE: needs an array and a kernel");
        if (src->elements.empty() || src->elements[0].type != ValueType::ARRAY) {
            // A vector: the kernel slides along it centred on each element,
            // as a correlation (the kernel is not flipped).
            if (!ker->elements.empty() && ker->elements[0].type == ValueType::ARRAY)
                throw std::runtime_error("CONVOLVE: a 1-D array takes a 1-D kernel");
            bool wrap1 = (args.size() >= 3) ? args[2].to_bool() : false;
            int n = (int)src->elements.size(), kn = (int)ker->elements.size(), half = kn / 2;
            Value r = Value::make_array();
            r.as_array()->elements.reserve(n);
            for (int i = 0; i < n; i++) {
                double s = 0;
                for (int k = 0; k < kn; k++) {
                    int si = i + k - half;
                    if (wrap1) si = ((si % n) + n) % n;
                    else if (si < 0 || si >= n) continue;
                    s += src->elements[si].to_double() * ker->elements[k].to_double();
                }
                r.as_array()->elements.push_back(Value::make_f64(s));
            }
            return r;
        }
        int ar, ac, kr, kc;
        get_2d(args[0], ar, ac); get_2d(args[1], kr, kc);
        bool wrap = (args.size() >= 3) ? args[2].to_bool() : false;
        int hr = kr / 2, hc = kc / 2;
        Value r = make_matrix(ar, ac);
        for (int i = 0; i < ar; i++)
            for (int j = 0; j < ac; j++) {
                double s = 0;
                for (int ki = 0; ki < kr; ki++)
                    for (int kj = 0; kj < kc; kj++) {
                        int si = i + ki - hr, sj = j + kj - hc;
                        if (wrap) { si = ((si % ar) + ar) % ar; sj = ((sj % ac) + ac) % ac; }
                        else if (si < 0 || si >= ar || sj < 0 || sj >= ac) continue;
                        s += elem2d(args[0], si, sj) * elem2d(args[1], ki, kj);
                    }
                r.as_array()->elements[i].as_array()->elements[j] = Value::make_f64(s);
            }
        return r;
    });

    // ── PLACE ────────────────────────────────────────────────
    register_native("PLACE", [](const std::vector<Value>& args) -> Value {
        // Deep copy destination
        int dr, dc; get_2d(args[0], dr, dc);
        Value r = make_matrix(dr, dc);
        for (int i = 0; i < dr; i++)
            for (int j = 0; j < dc; j++)
                r.as_array()->elements[i].as_array()->elements[j] =
                    args[0].as_array()->elements[i].as_array()->elements[j];
        // Place source at coordinates
        int sr, sc; get_2d(args[1], sr, sc);
        auto* coords = args[2].as_array();
        int oy = (int)coords->elements[0].to_int();
        int ox = (int)coords->elements[1].to_int();
        for (int i = 0; i < sr; i++)
            for (int j = 0; j < sc; j++) {
                int ti = oy + i, tj = ox + j;
                if (ti >= 0 && ti < dr && tj >= 0 && tj < dc)
                    r.as_array()->elements[ti].as_array()->elements[tj] = args[1].as_array()->elements[i].as_array()->elements[j];
            }
        return r;
    });

    // ── SELECT (higher-order) ────────────────────────────────
    register_native("SELECT", 2, 3, [this](const std::vector<Value>& args) -> Value {
        // SELECT(func, array, [row_wise])   - function first, APL/functional style
        Value fn = args[0];
        auto* arr = args[1].as_array();
        bool row_wise = (args.size() >= 3 && args[2].to_bool());
        Value r = Value::make_array();
        if (row_wise) {
            for (auto& row : arr->elements)
                r.as_array()->elements.push_back(call_funcref(fn, {row}));
        } else {
            for (auto& e : arr->elements)
                r.as_array()->elements.push_back(call_funcref(fn, {e}));
        }
        return r;
    });

    // ── FILTER (higher-order) ────────────────────────────────
    register_native("FILTER", 2, 2, [this](const std::vector<Value>& args) -> Value {
        // FILTER(func, array)   - function first
        Value fn = args[0];
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        for (auto& e : arr->elements) {
            Value res = call_funcref(fn, {e});
            if (res.to_bool()) r.as_array()->elements.push_back(e);
        }
        return r;
    });

    // ── TAKE_WHILE (higher-order) ────────────────────────────
    register_native("TAKE_WHILE", 2, 2, [this](const std::vector<Value>& args) -> Value {
        // TAKE_WHILE(func, array) - take elements from the front while
        // predicate returns true; stop at first false.
        Value fn = args[0];
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        for (auto& e : arr->elements) {
            if (!call_funcref(fn, {e}).to_bool()) break;
            r.as_array()->elements.push_back(e);
        }
        return r;
    });

    // ── DROP_WHILE (higher-order) ────────────────────────────
    register_native("DROP_WHILE", 2, 2, [this](const std::vector<Value>& args) -> Value {
        // DROP_WHILE(func, array) - skip elements from the front while
        // predicate returns true; keep everything from the first false on.
        Value fn = args[0];
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        bool dropping = true;
        for (auto& e : arr->elements) {
            if (dropping && call_funcref(fn, {e}).to_bool()) continue;
            dropping = false;
            r.as_array()->elements.push_back(e);
        }
        return r;
    });

    // ── CHUNK ────────────────────────────────────────────────
    register_native("CHUNK", 2, 2, [](const std::vector<Value>& args) -> Value {
        // CHUNK(array, size) - split into sub-arrays of given size.
        // Last chunk may be smaller. Size must be >= 1.
        auto* arr = args[0].as_array();
        int64_t n = args[1].to_int();
        Value r = Value::make_array();
        if (n < 1) return r;
        auto* out = r.as_array();
        Value chunk = Value::make_array();
        auto* cur = chunk.as_array();
        for (auto& e : arr->elements) {
            cur->elements.push_back(e);
            if ((int64_t)cur->elements.size() == n) {
                out->elements.push_back(std::move(chunk));
                chunk = Value::make_array();
                cur = chunk.as_array();
            }
        }
        if (!cur->elements.empty()) out->elements.push_back(std::move(chunk));
        return r;
    });

    // ── ENUMERATE ────────────────────────────────────────────
    register_native("ENUMERATE", 1, 1, [](const std::vector<Value>& args) -> Value {
        // ENUMERATE(array) - returns [[0, elem0], [1, elem1], ...]
        auto* arr = args[0].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        for (size_t i = 0; i < arr->elements.size(); i++) {
            Value pair = Value::make_array();
            pair.as_array()->elements.push_back(Value::make_i64((int64_t)i));
            pair.as_array()->elements.push_back(arr->elements[i]);
            out->elements.push_back(std::move(pair));
        }
        return r;
    });

    // ── GROUPBY (higher-order) ───────────────────────────────
    register_native("GROUPBY", 2, 2, [this](const std::vector<Value>& args) -> Value {
        // GROUPBY(func, array) - bucket elements into a map keyed by
        // the result of calling func on each element (coerced to string).
        Value fn = args[0];
        auto* arr = args[1].as_array();
        Value r = Value::make_object();
        auto* obj = r.as_object();
        for (auto& e : arr->elements) {
            std::string key = call_funcref(fn, {e}).to_string();
            Value* bucket = obj->get(key);
            if (bucket) {
                bucket->as_array()->elements.push_back(e);
            } else {
                Value fresh = Value::make_array();
                fresh.as_array()->elements.push_back(e);
                obj->set(key, std::move(fresh));
            }
        }
        return r;
    });

    register_native("AGG", 3, 3, [this](const std::vector<Value>& args) -> Value {
        // AGG(keys, values, fn@) - group `values` by the matching `keys` entry,
        // apply fn to each group's value-array, and return a 2-column table
        // [[key, fn(group)], ...] in first-seen key order. O(n), one pass. This
        // is APL's dyadic Key (⌸): group + reduce + assemble in one primitive.
        auto* keys = args[0].as_array();
        auto* vals = args[1].as_array();
        Value fn = args[2];
        size_t n = std::min(keys->elements.size(), vals->elements.size());
        std::unordered_map<std::string, size_t> idx;
        std::vector<Value> repr;     // original key Value per group (type preserved)
        std::vector<Value> groups;   // array of grouped values per group
        for (size_t i = 0; i < n; i++) {
            std::string k = keys->elements[i].to_string();
            auto it = idx.find(k);
            size_t gi;
            if (it == idx.end()) {
                gi = groups.size();
                idx.emplace(k, gi);
                repr.push_back(keys->elements[i]);
                groups.push_back(Value::make_array());
            } else gi = it->second;
            groups[gi].as_array()->elements.push_back(vals->elements[i]);
        }
        Value r = Value::make_array();
        for (size_t gi = 0; gi < groups.size(); gi++) {
            Value row = Value::make_array();
            row.as_array()->elements.push_back(repr[gi]);
            row.as_array()->elements.push_back(call_funcref(fn, {groups[gi]}));
            r.as_array()->elements.push_back(std::move(row));
        }
        return r;
    });

    register_native("TALLY", 1, 1, [](const std::vector<Value>& args) -> Value {
        // TALLY(array) - [[value, count], ...] of distinct values in first-seen
        // order. The most common "verdichtung" (value_counts).
        auto* arr = args[0].as_array();
        std::unordered_map<std::string, size_t> idx;
        std::vector<Value> repr;
        std::vector<int64_t> cnt;
        for (auto& e : arr->elements) {
            std::string k = e.to_string();
            auto it = idx.find(k);
            if (it == idx.end()) { idx.emplace(k, repr.size()); repr.push_back(e); cnt.push_back(1); }
            else cnt[it->second]++;
        }
        Value r = Value::make_array();
        for (size_t i = 0; i < repr.size(); i++) {
            Value row = Value::make_array();
            row.as_array()->elements.push_back(repr[i]);
            row.as_array()->elements.push_back(Value::make_i64(cnt[i]));
            r.as_array()->elements.push_back(std::move(row));
        }
        return r;
    });

    // ── REDUCE (higher-order) ────────────────────────────────
    register_native("REDUCE", 2, 3, [this](const std::vector<Value>& args) -> Value {
        // REDUCE(func, array, [init])   - function first
        Value fn = args[0];
        auto* arr = args[1].as_array();
        if (arr->elements.empty()) return (args.size() >= 3) ? args[2] : Value::make_none();
        Value acc = (args.size() >= 3) ? args[2] : arr->elements[0];
        size_t start = (args.size() >= 3) ? 0 : 1;
        for (size_t i = start; i < arr->elements.size(); i++)
            acc = call_funcref(fn, {acc, arr->elements[i]});
        return acc;
    });

    // ── OUTER ────────────────────────────────────────────────
    register_native("OUTER", 3, 3, [this](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        std::string op = args[2].as_string()->data;
        Value r = Value::make_array();
        for (auto& ai : a->elements) {
            Value row = Value::make_array();
            for (auto& bj : b->elements)
                row.as_array()->elements.push_back(apply_binary_op(op, ai, bj));
            r.as_array()->elements.push_back(std::move(row));
        }
        return r;
    });

    // ── INTEGRATE (Gauss-Legendre) ───────────────────────────
    register_native("INTEGRATE", 2, 3, [this](const std::vector<Value>& args) -> Value {
        Value fn = args[0];
        auto* lim = args[1].as_array();
        int n = (args.size() >= 3) ? (int)args[2].to_int() : 5;
        double a = lim->elements[0].to_double();
        double b = lim->elements[1].to_double();
        // Gauss-Legendre points and weights (up to 5)
        static const double pts5[] = {-0.9061798459, -0.5384693101, 0.0, 0.5384693101, 0.9061798459};
        static const double wts5[] = {0.2369268851, 0.4786286705, 0.5688888889, 0.4786286705, 0.2369268851};
        static const double pts3[] = {-0.7745966692, 0.0, 0.7745966692};
        static const double wts3[] = {0.5555555556, 0.8888888889, 0.5555555556};
        const double* pts = (n >= 5) ? pts5 : pts3;
        const double* wts = (n >= 5) ? wts5 : wts3;
        int np = (n >= 5) ? 5 : 3;
        double mid = (a + b) / 2.0, half = (b - a) / 2.0;
        double sum = 0;
        for (int i = 0; i < np; i++) {
            double x = mid + half * pts[i];
            Value fx = call_funcref(fn, {Value::make_f64(x)});
            sum += wts[i] * fx.to_double();
        }
        return Value::make_f64(sum * half);
    });

    // ── 3. Statistics ────────────────────────────────────────

    register_native("MEAN", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("MEAN takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_mean);
        double s = 0;
        size_t n = 0;
        for_each_leaf(args[0], [&](const Value& v) { s += v.to_double(); n++; });
        if (n == 0) return Value::make_f64(0);
        return Value::make_f64(s / n);
    });
    register_native("MEDIAN", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("MEDIAN takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_median);
        auto flat = flatten_values(args[0]);
        if (flat.empty()) return Value::make_f64(0);
        std::vector<double> vals; for (auto& v : flat) vals.push_back(v.to_double());
        std::sort(vals.begin(), vals.end());
        size_t n = vals.size();
        return Value::make_f64(n % 2 ? vals[n/2] : (vals[n/2-1] + vals[n/2]) / 2.0);
    });
    register_native("VARIANCE", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("VARIANCE takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_variance);
        double s = 0;
        size_t n = 0;
        for_each_leaf(args[0], [&](const Value& v) { s += v.to_double(); n++; });
        if (n < 2) return Value::make_f64(0);
        double mean = s / n, ss = 0;
        for_each_leaf(args[0], [&](const Value& v) { double d = v.to_double() - mean; ss += d * d; });
        return Value::make_f64(ss / n);
    });
    register_native("STDEV", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (reduce_form(args) == ReduceForm::BAD_SCALAR)
            throw std::runtime_error("STDEV takes one array; the second argument "
                "is the axis of a matrix, not a second value");
        if (reduce_form(args) == ReduceForm::ALONG_AXIS)
            return reduce_along_axis(args[0], (int)args[1].to_int(), lane_stdev);
        double s = 0;
        size_t n = 0;
        for_each_leaf(args[0], [&](const Value& v) { s += v.to_double(); n++; });
        if (n < 2) return Value::make_f64(0);
        double mean = s / n, ss = 0;
        for_each_leaf(args[0], [&](const Value& v) { double d = v.to_double() - mean; ss += d * d; });
        return Value::make_f64(std::sqrt(ss / n));
    });
    register_native("DOT", [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array(); auto* b = args[1].as_array();
        double s = 0; size_t n = std::min(a->elements.size(), b->elements.size());
        for (size_t i = 0; i < n; i++) s += a->elements[i].to_double() * b->elements[i].to_double();
        return Value::make_f64(s);
    });
    register_native("CROSS", [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array(); auto* b = args[1].as_array();
        if (a->elements.size() < 3 || b->elements.size() < 3) throw std::runtime_error("CROSS requires 3D vectors");
        double ax=a->elements[0].to_double(), ay=a->elements[1].to_double(), az=a->elements[2].to_double();
        double bx=b->elements[0].to_double(), by=b->elements[1].to_double(), bz=b->elements[2].to_double();
        Value r = Value::make_array();
        r.as_array()->elements.push_back(Value::make_f64(ay*bz - az*by));
        r.as_array()->elements.push_back(Value::make_f64(az*bx - ax*bz));
        r.as_array()->elements.push_back(Value::make_f64(ax*by - ay*bx));
        return r;
    });
    register_native("CUMSUM", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        Value r = Value::make_array(); double s = 0;
        for (auto& v : arr->elements) { s += v.to_double(); r.as_array()->elements.push_back(Value::make_f64(s)); }
        return r;
    });
    register_native("CUMPROD", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        Value r = Value::make_array(); double s = 1;
        for (auto& v : arr->elements) { s *= v.to_double(); r.as_array()->elements.push_back(Value::make_f64(s)); }
        return r;
    });
    register_native("HISTOGRAM", [](const std::vector<Value>& args) -> Value {
        int bins = (args.size() >= 2) ? (int)args[1].to_int() : 10;
        bool seen = false;
        double mn = 0, mx = 0;
        for_each_leaf(args[0], [&](const Value& v) {
            double d = v.to_double();
            if (!seen) { mn = mx = d; seen = true; }
            else if (d < mn) mn = d;
            else if (d > mx) mx = d;
        });
        double range = mx - mn;
        if (range == 0) range = 1;
        Value r = Value::make_array();
        r.as_array()->elements.resize(bins, Value::make_i64(0));
        for_each_leaf(args[0], [&](const Value& v) {
            int b = (int)((v.to_double() - mn) / range * bins);
            if (b >= bins) b = bins - 1;
            if (b < 0) b = 0;
            r.as_array()->elements[b] = Value::make_i64(r.as_array()->elements[b].to_int() + 1);
        });
        return r;
    });
    // HISTEDGES(values, [bins]) -> the bins + 1 edges HISTOGRAM counts between.
    register_native("HISTEDGES", 1, 2, [](const std::vector<Value>& args) -> Value {
        int bins = (args.size() >= 2) ? (int)args[1].to_int() : 10;
        if (bins < 1) throw std::runtime_error("HISTEDGES: bins must be at least 1");
        bool seen = false;
        double mn = 0, mx = 0;
        for_each_leaf(args[0], [&](const Value& v) {
            double d = v.to_double();
            if (!seen) { mn = mx = d; seen = true; }
            else if (d < mn) mn = d;
            else if (d > mx) mx = d;
        });
        double range = mx - mn;
        if (range == 0) range = 1;
        Value r = Value::make_array();
        r.as_array()->elements.reserve(bins + 1);
        for (int i = 0; i <= bins; i++)
            r.as_array()->elements.push_back(Value::make_f64(mn + range * i / bins));
        return r;
    });
    register_native("LINSPACE", [](const std::vector<Value>& args) -> Value {
        double start = args[0].to_double(), end = args[1].to_double();
        int n = (int)args[2].to_int();
        Value r = Value::make_array();
        if (n <= 1) { r.as_array()->elements.push_back(Value::make_f64(start)); return r; }
        for (int i = 0; i < n; i++)
            r.as_array()->elements.push_back(Value::make_f64(start + (end - start) * i / (n - 1)));
        return r;
    });

    // ── 4. Array Utilities ───────────────────────────────────

    register_native("FLATTEN", [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        r.as_array()->elements = flatten_values(args[0]);
        return r;
    });
    register_native("ZIP", [](const std::vector<Value>& args) -> Value {
        size_t n = args[0].as_array()->elements.size();
        for (size_t a = 1; a < args.size(); a++)
            n = std::min(n, args[a].as_array()->elements.size());
        Value r = Value::make_array();
        for (size_t i = 0; i < n; i++) {
            Value row = Value::make_array();
            for (size_t a = 0; a < args.size(); a++)
                row.as_array()->elements.push_back(args[a].as_array()->elements[i]);
            r.as_array()->elements.push_back(std::move(row));
        }
        return r;
    });
    // ZEROS / ONES: accept either a scalar (1-D vector of given length) or
    // a shape_vector ([rows, cols, ...]) for N-dimensional arrays.
    auto make_filled = [](const Value& shape_arg, const Value& fill) -> Value {
        std::vector<int64_t> shape;
        if (shape_arg.type == ValueType::ARRAY) {
            for (auto& e : shape_arg.as_array()->elements) shape.push_back(e.to_int());
        } else {
            shape.push_back(shape_arg.to_int());
        }
        if (shape.empty()) return Value::make_array();
        std::function<Value(size_t)> build = [&](size_t dim) -> Value {
            Value r = Value::make_array();
            int64_t n = shape[dim];
            r.as_array()->elements.reserve(n);
            if (dim + 1 == shape.size()) {
                r.as_array()->elements.resize(n, fill);
            } else {
                for (int64_t i = 0; i < n; i++) r.as_array()->elements.push_back(build(dim + 1));
            }
            return r;
        };
        return build(0);
    };
    register_native("ZEROS", [make_filled](const std::vector<Value>& args) -> Value {
        return make_filled(args[0], Value::make_i64(0));
    });
    // __MAKE_UDT_ARRAY__(shape, "TypeName" [, vec1, vec2, ...]) - fill an
    // array of the given shape with freshly-constructed UDT instances. Used
    // by the parser to desugar `DIM A[N] AS UserType[(vec1, vec2)]`. We can't
    // use ZEROS here because each slot needs its own constructor result.
    // When vector args are present, they are spread element-wise across
    // slots: slot i receives ctor args (vec1[i], vec2[i], ...) and INIT is
    // called after __NEW__.
    register_native("__MAKE_UDT_ARRAY__", 2, 32, [this, make_filled](const std::vector<Value>& args) -> Value {
        std::string type_name = args[1].to_string();
        std::string ctor_name = type_name + ".__NEW__";
        std::string init_name = type_name + ".INIT";
        bool has_init = function_exists(init_name);
        // Collect ctor vectors (args[2..]). Each must be an array; we'll
        // index into it per slot.
        std::vector<const ArrayObj*> ctor_vecs;
        for (size_t i = 2; i < args.size(); i++) {
            if (args[i].type != ValueType::ARRAY) {
                throw std::runtime_error("DIM " + type_name +
                    "[]: constructor argument " + std::to_string(i - 1) +
                    " must be an array (got " + args[i].to_string() + ")");
            }
            ctor_vecs.push_back(args[i].as_array());
        }
        if (!ctor_vecs.empty() && !has_init) {
            throw std::runtime_error("Type '" + type_name +
                "' has no SUB INIT, cannot pass constructor argument vectors");
        }
        // First build a zeros-array of the right shape, then walk it and
        // replace every leaf scalar with a fresh constructor result.
        Value arr = make_filled(args[0], Value::make_i64(0));
        size_t leaf_idx = 0;
        std::function<void(Value&)> fill = [&](Value& v) {
            if (v.type == ValueType::ARRAY) {
                for (auto& e : v.as_array()->elements) fill(e);
            } else {
                v = call_function(ctor_name, {});
                if (!ctor_vecs.empty()) {
                    std::vector<Value> init_args;
                    init_args.reserve(ctor_vecs.size() + 1);
                    init_args.push_back(v); // THIS
                    for (auto* vec : ctor_vecs) {
                        if (leaf_idx >= vec->elements.size()) {
                            throw std::runtime_error("DIM " + type_name +
                                "[]: constructor vector shorter than array shape (slot " +
                                std::to_string(leaf_idx) + ")");
                        }
                        init_args.push_back(vec->elements[leaf_idx]);
                    }
                    call_function(init_name, init_args);
                }
                leaf_idx++;
            }
        };
        fill(arr);
        return arr;
    });
    register_native("ONES", [make_filled](const std::vector<Value>& args) -> Value {
        return make_filled(args[0], Value::make_i64(1));
    });
    register_native("RANGE", [](const std::vector<Value>& args) -> Value {
        int64_t start = args[0].to_int();
        int64_t end = args[1].to_int();
        int64_t step = (args.size() >= 3) ? args[2].to_int() : (start <= end ? 1 : -1);
        Value r = Value::make_array();
        if (step > 0) for (int64_t i = start; i < end; i += step) r.as_array()->elements.push_back(Value::make_i64(i));
        else if (step < 0) for (int64_t i = start; i > end; i += step) r.as_array()->elements.push_back(Value::make_i64(i));
        return r;
    });
    register_native("COUNT", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array(); std::string target = args[1].to_string();
        int64_t c = 0; for (auto& e : arr->elements) if (e.to_string() == target) c++;
        return Value::make_i64(c);
    });
    register_native("INDEXOF", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array(); std::string target = args[1].to_string();
        for (size_t i = 0; i < arr->elements.size(); i++)
            if (arr->elements[i].to_string() == target) return Value::make_i64(i);
        return Value::make_i64(-1);
    });
}
