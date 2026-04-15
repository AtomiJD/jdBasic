#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <locale>
#include <stdexcept>

// Global locale for number formatting (set by SETLOCALE)
inline std::locale g_jd_locale("C");

enum class ValueType : uint8_t {
    NONE, BOOLEAN, BYTE, INT16, INT32, INT64,
    FLOAT16, FLOAT32, FLOAT64,
    STRING, OBJECT, TENSOR, ARRAY
};

// ── Heap objects ─────────────────────────────────────────────
// Intrusive refcounting (thread-safe). Replaces std::shared_ptr so that:
//   - Value is 24 bytes instead of 40 (better cache behaviour)
//   - push/pop in the VM only pays refcount cost for actual heap values,
//     not for every INT/FLOAT on the stack
//   - no control-block allocation per heap object

struct Value; // forward

struct HeapObject {
    // Starts at 1: newly created objects are owned by their creator.
    mutable std::atomic<int> refcount{1};
    virtual ~HeapObject() = default;
};

inline void heap_retain(HeapObject* o) {
    if (o) o->refcount.fetch_add(1, std::memory_order_relaxed);
}

inline void heap_release(HeapObject* o) {
    if (o && o->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete o;
    }
}

struct StringObj : HeapObject {
    std::string data;
    StringObj() = default;
    explicit StringObj(std::string s) : data(std::move(s)) {}
};

struct ArrayObj : HeapObject {
    std::vector<Value> elements;
    ValueType elem_type = ValueType::NONE;
};

struct TensorObj : HeapObject {
    std::vector<size_t> shape;
    std::vector<double> data;
};

struct ObjectObj : HeapObject {
    std::vector<std::pair<std::string, Value>> fields;

    Value* get(const std::string& name);
    void set(const std::string& name, Value v);
};

// ── COM Object (only with #define COM) ──────────────────────
#ifdef COM
struct IDispatch; // forward declare
struct ComObj : HeapObject {
    void* dispatch = nullptr; // IDispatch* stored as void* to avoid windows.h in header
    ComObj() = default;
    ~ComObj();
};
#endif

// ── Float16 helpers ──────────────────────────────────────────

inline uint16_t float_to_half(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint16_t sign = (x >> 16) & 0x8000;
    int exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (exp <= 0) return sign;
    if (exp >= 31) return sign | 0x7C00;
    return sign | (uint16_t)(exp << 10) | (uint16_t)(mant >> 13);
}

inline float half_to_float(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    if (exp == 0) {
        if (mant == 0) { float f; uint32_t r = sign; std::memcpy(&f, &r, 4); return f; }
        // subnormal
        while (!(mant & 0x400)) { mant <<= 1; exp--; }
        exp++; mant &= ~0x400;
    } else if (exp == 31) {
        uint32_t r = sign | 0x7F800000 | (mant << 13);
        float f; std::memcpy(&f, &r, 4); return f;
    }
    uint32_t result = sign | ((exp + 112) << 23) | (mant << 13);
    float f; std::memcpy(&f, &result, 4); return f;
}

// ── Value ────────────────────────────────────────────────────

// Optional sub-type tag — lives in padding bytes after `type`, so it costs
// no extra storage. Used to mark FLOAT64 values as Unix-epoch dates so
// `PRINT` formats them readably and TYPEOF returns "DATE" instead of
// "FLOAT64". Arithmetic helpers (DATEADD) propagate the tag.
enum class ValueSubtype : uint8_t {
    NONE = 0,
    DATE = 1,
};

struct Value {
    ValueType type = ValueType::NONE;
    ValueSubtype subtype = ValueSubtype::NONE;

    union {
        bool boolean;
        uint8_t byte_val;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint16_t f16_bits;
        float f32;
        double f64;
    };

    // Raw pointer with manual ref-counting. nullptr for scalar values.
    HeapObject* obj = nullptr;

    // ── Lifecycle ────────────────────────────────────────────

    Value() noexcept : type(ValueType::NONE), subtype(ValueSubtype::NONE), i64(0), obj(nullptr) {}

    ~Value() { heap_release(obj); }

    Value(const Value& other) noexcept
        : type(other.type), subtype(other.subtype), i64(other.i64), obj(other.obj) {
        heap_retain(obj);
    }

    Value(Value&& other) noexcept
        : type(other.type), subtype(other.subtype), i64(other.i64), obj(other.obj) {
        other.obj = nullptr;
        other.type = ValueType::NONE;
        other.subtype = ValueSubtype::NONE;
    }

    Value& operator=(const Value& other) noexcept {
        if (this != &other) {
            HeapObject* old = obj;
            type = other.type;
            subtype = other.subtype;
            i64 = other.i64;
            obj = other.obj;
            heap_retain(obj);   // retain new before releasing old (self-assign safety)
            heap_release(old);
        }
        return *this;
    }

    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            HeapObject* old = obj;
            type = other.type;
            subtype = other.subtype;
            i64 = other.i64;
            obj = other.obj;
            other.obj = nullptr;
            other.type = ValueType::NONE;
            other.subtype = ValueSubtype::NONE;
            heap_release(old);
        }
        return *this;
    }

    // ── Factories ────────────────────────────────────────────

    static Value make_none()    { Value v; return v; }
    static Value make_bool(bool b)   { Value v; v.type = ValueType::BOOLEAN; v.boolean = b; return v; }
    static Value make_byte(uint8_t b){ Value v; v.type = ValueType::BYTE; v.byte_val = b; return v; }
    static Value make_i16(int16_t x) { Value v; v.type = ValueType::INT16; v.i16 = x; return v; }
    static Value make_i32(int32_t x) { Value v; v.type = ValueType::INT32; v.i32 = x; return v; }
    static Value make_i64(int64_t x) { Value v; v.type = ValueType::INT64; v.i64 = x; return v; }
    static Value make_f16(float x)   { Value v; v.type = ValueType::FLOAT16; v.f16_bits = float_to_half(x); return v; }
    static Value make_f32(float x)   { Value v; v.type = ValueType::FLOAT32; v.f32 = x; return v; }
    static Value make_f64(double x)  { Value v; v.type = ValueType::FLOAT64; v.f64 = x; return v; }
    // FLOAT64 tagged as Unix-epoch date — printed via strftime, TYPEOF "DATE".
    static Value make_date(double epoch) {
        Value v; v.type = ValueType::FLOAT64; v.subtype = ValueSubtype::DATE; v.f64 = epoch;
        return v;
    }

    static Value make_string(const std::string& s) {
        Value v; v.type = ValueType::STRING;
        v.i64 = 0;
        v.obj = new StringObj(s); // refcount starts at 1
        return v;
    }

    static Value make_array() {
        Value v; v.type = ValueType::ARRAY;
        v.i64 = 0;
        v.obj = new ArrayObj();
        return v;
    }

    static Value make_tensor(std::vector<size_t> shape, std::vector<double> data) {
        Value v; v.type = ValueType::TENSOR;
        v.i64 = 0;
        auto* t = new TensorObj();
        t->shape = std::move(shape);
        t->data = std::move(data);
        v.obj = t;
        return v;
    }

    static Value make_object() {
        Value v; v.type = ValueType::OBJECT;
        v.i64 = 0;
        v.obj = new ObjectObj();
        return v;
    }

    // ── Accessors ────────────────────────────────────────────

    StringObj* as_string() const {
        static StringObj empty_str;
        if (type != ValueType::STRING || !obj) return &empty_str;
        return static_cast<StringObj*>(obj);
    }
    ArrayObj* as_array() const {
        static ArrayObj empty_arr;
        if (type != ValueType::ARRAY || !obj) return &empty_arr;
        return static_cast<ArrayObj*>(obj);
    }
    TensorObj* as_tensor() const { return static_cast<TensorObj*>(obj); }
    ObjectObj* as_object() const {
        static ObjectObj empty_obj;
        if (type != ValueType::OBJECT || !obj) return &empty_obj;
        return static_cast<ObjectObj*>(obj);
    }

#ifdef COM
    static Value make_com(void* idispatch);
    ComObj* as_com() const { return dynamic_cast<ComObj*>(obj); }
    bool is_com() const { return type == ValueType::OBJECT && dynamic_cast<ComObj*>(obj) != nullptr; }
#endif

    // ── Convert to double for arithmetic ─────────────────────

    double to_double() const {
        switch (type) {
            case ValueType::BOOLEAN: return boolean ? 1.0 : 0.0;
            case ValueType::BYTE:    return byte_val;
            case ValueType::INT16:   return i16;
            case ValueType::INT32:   return i32;
            case ValueType::INT64:   return (double)i64;
            case ValueType::FLOAT16: return half_to_float(f16_bits);
            case ValueType::FLOAT32: return f32;
            case ValueType::FLOAT64: return f64;
            default: return 0.0;
        }
    }

    int64_t to_int() const {
        switch (type) {
            case ValueType::BOOLEAN: return boolean ? 1 : 0;
            case ValueType::BYTE:    return byte_val;
            case ValueType::INT16:   return i16;
            case ValueType::INT32:   return i32;
            case ValueType::INT64:   return i64;
            case ValueType::FLOAT16: return (int64_t)half_to_float(f16_bits);
            case ValueType::FLOAT32: return (int64_t)f32;
            case ValueType::FLOAT64: return (int64_t)f64;
            default: return 0;
        }
    }

    bool to_bool() const {
        switch (type) {
            case ValueType::BOOLEAN: return boolean;
            case ValueType::BYTE:    return byte_val != 0;
            case ValueType::INT16:   return i16 != 0;
            case ValueType::INT32:   return i32 != 0;
            case ValueType::INT64:   return i64 != 0;
            case ValueType::FLOAT16: return f16_bits != 0;
            case ValueType::FLOAT32: return f32 != 0.0f;
            case ValueType::FLOAT64: return f64 != 0.0;
            case ValueType::STRING:  return as_string() && !as_string()->data.empty();
            case ValueType::ARRAY: {
                // APL-style: an array is true iff every element is truthy
                // (= ALL). An empty array is false (nothing to be true about).
                // Without this, `IF [FALSE,FALSE,FALSE] THEN ...` would take
                // the THEN branch because the array is non-empty — surprising
                // for `IF y = 0 THEN ...` patterns when y is a vector.
                auto* a = as_array();
                if (!a || a->elements.empty()) return false;
                for (auto& e : a->elements) if (!e.to_bool()) return false;
                return true;
            }
            default: return false;
        }
    }

    // Format a floating-point number with locale-aware thousand separators,
    // fixed precision 6, trailing zeros and trailing decimal point removed.
    static std::string format_float_locale(double val) {
        std::ostringstream ss;
        ss.imbue(g_jd_locale);
        ss << std::fixed << std::setprecision(6) << val;
        std::string s = ss.str();
        // Remove trailing zeros
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        // Remove trailing decimal point
        char dp = std::use_facet<std::numpunct<char>>(g_jd_locale).decimal_point();
        if (!s.empty() && s.back() == dp) s.pop_back();
        return s;
    }

    // Format an integer with locale-aware thousand separators.
    static std::string format_int_locale(int64_t val) {
        std::ostringstream ss;
        ss.imbue(g_jd_locale);
        ss << val;
        return ss.str();
    }

    std::string to_string() const {
        switch (type) {
            case ValueType::NONE:    return "NONE";
            case ValueType::BOOLEAN: return boolean ? "TRUE" : "FALSE";
            case ValueType::BYTE:    return format_int_locale(byte_val);
            case ValueType::INT16:   return format_int_locale(i16);
            case ValueType::INT32:   return format_int_locale(i32);
            case ValueType::INT64:   return format_int_locale(i64);
            case ValueType::FLOAT16: return format_float_locale(half_to_float(f16_bits));
            case ValueType::FLOAT32: return format_float_locale(f32);
            case ValueType::FLOAT64:
                if (subtype == ValueSubtype::DATE) {
                    std::time_t t = static_cast<std::time_t>(f64);
                    auto* tm = std::localtime(&t);
                    if (!tm) return format_float_locale(f64);
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
                    return std::string(buf);
                }
                return format_float_locale(f64);
            case ValueType::STRING:  return as_string() ? as_string()->data : "";
            case ValueType::ARRAY: {
                std::ostringstream os;
                os << "[";
                auto* arr = as_array();
                for (size_t i = 0; i < arr->elements.size(); i++) {
                    if (i > 0) os << ", ";
                    os << arr->elements[i].to_string();
                }
                os << "]";
                return os.str();
            }
            case ValueType::TENSOR: {
                auto* t = as_tensor();
                std::ostringstream os;
                os << "Tensor(";
                for (size_t i = 0; i < t->shape.size(); i++) {
                    if (i > 0) os << "x";
                    os << t->shape[i];
                }
                os << ")";
                return os.str();
            }
            case ValueType::OBJECT: {
#ifdef COM
                // ComObj and ObjectObj share the OBJECT type tag. Casting a
                // ComObj to ObjectObj would be UB and crash inside fields[].
                // Format COM dispatch pointers as an opaque handle instead.
                if (auto* c = dynamic_cast<ComObj*>(obj)) {
                    std::ostringstream os;
                    os << "<COM dispatch ";
                    if (c->dispatch) os << c->dispatch;
                    else             os << "(null)";
                    os << ">";
                    return os.str();
                }
#endif
                auto* o = as_object();
                if (!o) return "<object>";
                std::ostringstream os;
                os << "{";
                for (size_t i = 0; i < o->fields.size(); i++) {
                    if (i > 0) os << ", ";
                    os << "\"" << o->fields[i].first << "\": ";
                    if (o->fields[i].second.type == ValueType::STRING)
                        os << "\"" << o->fields[i].second.to_string() << "\"";
                    else
                        os << o->fields[i].second.to_string();
                }
                os << "}";
                return os.str();
            }
        }
        return "?";
    }
};

inline Value* ObjectObj::get(const std::string& name) {
    for (auto& [k, v] : fields) if (k == name) return &v;
    return nullptr;
}

inline void ObjectObj::set(const std::string& name, Value val) {
    for (auto& [k, v] : fields) { if (k == name) { v = std::move(val); return; } }
    fields.emplace_back(name, std::move(val));
}

// ── Promote type for arithmetic ──────────────────────────────

inline bool is_integer_type(ValueType t) {
    return t == ValueType::BYTE || t == ValueType::INT16 ||
           t == ValueType::INT32 || t == ValueType::INT64;
}

inline bool is_float_type(ValueType t) {
    return t == ValueType::FLOAT16 || t == ValueType::FLOAT32 || t == ValueType::FLOAT64;
}

inline bool is_numeric(ValueType t) {
    return is_integer_type(t) || is_float_type(t) || t == ValueType::BOOLEAN;
}

// Fast equality check for reactive cache (avoids full string comparison for arrays)
inline bool values_equal(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case ValueType::NONE:    return true;
        case ValueType::BOOLEAN: return a.boolean == b.boolean;
        case ValueType::BYTE:    return a.byte_val == b.byte_val;
        case ValueType::INT16:   return a.i16 == b.i16;
        case ValueType::INT32:   return a.i32 == b.i32;
        case ValueType::INT64:   return a.i64 == b.i64;
        case ValueType::FLOAT32: return a.f32 == b.f32;
        case ValueType::FLOAT64: return a.f64 == b.f64;
        case ValueType::STRING:
            return a.as_string() && b.as_string() && a.as_string()->data == b.as_string()->data;
        case ValueType::ARRAY: {
            if (!a.as_array() || !b.as_array()) return a.obj == b.obj;
            auto& ae = a.as_array()->elements;
            auto& be = b.as_array()->elements;
            if (ae.size() != be.size()) return false;
            for (size_t i = 0; i < ae.size(); i++)
                if (!values_equal(ae[i], be[i])) return false;
            return true;
        }
        default: return a.obj == b.obj; // pointer compare for objects
    }
}
