#pragma once
// JDV1 - the binary encoding of a jdBasic value (doc/design/node_bus_plan.md,
// section 5.1). NODE.PACK$ / NODE.UNPACK and the Node Bus frames carry it.
//
//   "JDV" 0x01, then one value:
//     0x00 NONE                 0x01 FALSE              0x02 TRUE
//     0x03 INT64  zigzag varint 0x04 FLOAT64  8 bytes   0x05 FLOAT32 4 bytes
//     0x06 STRING varint length + bytes
//     0x07 ARRAY  varint count + values
//     0x08 ARRAY of one numeric kind: kind byte, varint count, raw elements
//     0x09 MAP    varint count + (varint length + key bytes, value)
//     0x0A DATE   8 bytes epoch seconds as FLOAT64
//     0x0B TENSOR varint rank, varint dims, FLOAT64 data
//     0x10 BYTE 1 byte          0x11 INT16 zigzag varint
//     0x12 INT32 zigzag varint  0x13 FLOAT16 2 bytes
//
// Multi-byte fields are little-endian, the byte order of every target jdBasic
// builds for. A UDT instance is a map whose "__TYPE__" field names the type,
// so it travels as a MAP and unpacks as one.

#include "value.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace jdv {

constexpr uint8_t T_NONE = 0x00, T_FALSE = 0x01, T_TRUE = 0x02, T_INT64 = 0x03,
                  T_F64 = 0x04, T_F32 = 0x05, T_STRING = 0x06, T_ARRAY = 0x07,
                  T_PACKED = 0x08, T_MAP = 0x09, T_DATE = 0x0A, T_TENSOR = 0x0B,
                  T_BYTE = 0x10, T_INT16 = 0x11, T_INT32 = 0x12, T_F16 = 0x13;

constexpr int MAX_DEPTH = 64;
constexpr size_t MAGIC_LEN = 4;
inline const char MAGIC[MAGIC_LEN] = {'J', 'D', 'V', 1};

inline void put_varint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back((char)(uint8_t)(v | 0x80));
        v >>= 7;
    }
    out.push_back((char)(uint8_t)v);
}

inline uint64_t zigzag(int64_t v) { return ((uint64_t)v << 1) ^ (uint64_t)(v >> 63); }
inline int64_t unzigzag(uint64_t v) { return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }

template <typename T>
inline void put_raw(std::string& out, T v) {
    char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    out.append(b, sizeof(T));
}

// Element size of a numeric kind inside a packed array, 0 when it does not pack.
inline size_t packed_size(ValueType t) {
    switch (t) {
        case ValueType::BOOLEAN: case ValueType::BYTE: return 1;
        case ValueType::INT16: case ValueType::FLOAT16: return 2;
        case ValueType::INT32: case ValueType::FLOAT32: return 4;
        case ValueType::INT64: case ValueType::FLOAT64: return 8;
        default: return 0;
    }
}

inline void encode(std::string& out, const Value& v, int depth);

inline void encode_array(std::string& out, const ArrayObj* a, int depth) {
    const auto& el = a->elements;
    ValueType kind = el.empty() ? ValueType::NONE : el[0].type;
    bool packs = packed_size(kind) != 0;
    for (size_t i = 0; packs && i < el.size(); ++i)
        packs = el[i].type == kind && el[i].subtype == ValueSubtype::NONE;
    if (packs) {
        size_t sz = packed_size(kind);
        out.push_back((char)T_PACKED);
        out.push_back((char)(uint8_t)kind);
        put_varint(out, el.size());
        size_t at = out.size();
        out.resize(at + sz * el.size());
        char* dst = &out[at];
        for (const Value& e : el) {
            switch (kind) {
                case ValueType::BOOLEAN: *dst = e.boolean ? 1 : 0; break;
                case ValueType::BYTE:    std::memcpy(dst, &e.byte_val, 1); break;
                case ValueType::INT16:   std::memcpy(dst, &e.i16, 2); break;
                case ValueType::FLOAT16: std::memcpy(dst, &e.f16_bits, 2); break;
                case ValueType::INT32:   std::memcpy(dst, &e.i32, 4); break;
                case ValueType::FLOAT32: std::memcpy(dst, &e.f32, 4); break;
                case ValueType::INT64:   std::memcpy(dst, &e.i64, 8); break;
                default:                 std::memcpy(dst, &e.f64, 8); break;
            }
            dst += sz;
        }
        return;
    }
    out.push_back((char)T_ARRAY);
    put_varint(out, el.size());
    for (const Value& e : el) encode(out, e, depth + 1);
}

inline void encode(std::string& out, const Value& v, int depth) {
    if (depth > MAX_DEPTH) throw std::runtime_error("NODE.PACK: nested deeper than 64 levels");
    if (v.type == ValueType::FLOAT64 && v.subtype == ValueSubtype::DATE) {
        out.push_back((char)T_DATE);
        put_raw(out, v.f64);
        return;
    }
    switch (v.type) {
        case ValueType::NONE: out.push_back((char)T_NONE); return;
        case ValueType::BOOLEAN: out.push_back((char)(v.boolean ? T_TRUE : T_FALSE)); return;
        case ValueType::BYTE: out.push_back((char)T_BYTE); out.push_back((char)v.byte_val); return;
        case ValueType::INT16: out.push_back((char)T_INT16); put_varint(out, zigzag(v.i16)); return;
        case ValueType::INT32: out.push_back((char)T_INT32); put_varint(out, zigzag(v.i32)); return;
        case ValueType::INT64: out.push_back((char)T_INT64); put_varint(out, zigzag(v.i64)); return;
        case ValueType::FLOAT16: out.push_back((char)T_F16); put_raw(out, v.f16_bits); return;
        case ValueType::FLOAT32: out.push_back((char)T_F32); put_raw(out, v.f32); return;
        case ValueType::FLOAT64: out.push_back((char)T_F64); put_raw(out, v.f64); return;
        case ValueType::STRING: {
            const std::string& s = v.as_string()->data;
            out.push_back((char)T_STRING);
            put_varint(out, s.size());
            out.append(s);
            return;
        }
        case ValueType::ARRAY: encode_array(out, v.as_array(), depth); return;
        case ValueType::TENSOR: {
            const TensorObj* t = v.as_tensor();
            out.push_back((char)T_TENSOR);
            put_varint(out, t->shape.size());
            for (size_t d : t->shape) put_varint(out, d);
            put_varint(out, t->data.size());
            size_t at = out.size();
            out.resize(at + 8 * t->data.size());
            if (!t->data.empty()) std::memcpy(&out[at], t->data.data(), 8 * t->data.size());
            return;
        }
        case ValueType::OBJECT: {
#ifdef COM
            if (v.is_com()) throw std::runtime_error("NODE.PACK: a COM object cannot be packed");
#endif
            const ObjectObj* o = v.as_object();
            out.push_back((char)T_MAP);
            put_varint(out, o->fields.size());
            for (const auto& f : o->fields) {
                put_varint(out, f.first.size());
                out.append(f.first);
                encode(out, f.second, depth + 1);
            }
            return;
        }
    }
    throw std::runtime_error("NODE.PACK: unknown value kind");
}

// The packed form of v, magic included.
inline std::string pack(const Value& v) {
    std::string out(MAGIC, MAGIC_LEN);
    encode(out, v, 0);
    return out;
}

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t at = 0;

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("NODE.UNPACK: ") + what + " at byte " + std::to_string(at));
    }
    size_t left() const { return n - at; }
    uint8_t byte() {
        if (at >= n) fail("unexpected end");
        return p[at++];
    }
    uint64_t varint() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t b = byte();
            v |= (uint64_t)(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
        }
        fail("varint longer than 10 bytes");
    }
    // A count of items that need at least min_bytes each, checked against the rest.
    size_t count(size_t min_bytes) {
        size_t start = at;
        uint64_t c = varint();
        if (min_bytes && c > left() / min_bytes) {
            at = start;
            fail("count larger than the remaining bytes");
        }
        return (size_t)c;
    }
    template <typename T>
    T raw() {
        if (left() < sizeof(T)) fail("unexpected end");
        T v;
        std::memcpy(&v, p + at, sizeof(T));
        at += sizeof(T);
        return v;
    }
    std::string bytes(size_t len) {
        if (left() < len) fail("unexpected end");
        std::string s((const char*)p + at, len);
        at += len;
        return s;
    }
};

inline Value decode(Reader& r, int depth) {
    if (depth > MAX_DEPTH) r.fail("nested deeper than 64 levels");
    size_t tag_at = r.at;
    uint8_t tag = r.byte();
    switch (tag) {
        case T_NONE: return Value::make_none();
        case T_FALSE: return Value::make_bool(false);
        case T_TRUE: return Value::make_bool(true);
        case T_BYTE: return Value::make_byte(r.byte());
        case T_INT16: return Value::make_i16((int16_t)unzigzag(r.varint()));
        case T_INT32: return Value::make_i32((int32_t)unzigzag(r.varint()));
        case T_INT64: return Value::make_i64(unzigzag(r.varint()));
        case T_F16: { Value v; v.type = ValueType::FLOAT16; v.f16_bits = r.raw<uint16_t>(); return v; }
        case T_F32: return Value::make_f32(r.raw<float>());
        case T_F64: return Value::make_f64(r.raw<double>());
        case T_DATE: return Value::make_date(r.raw<double>());
        case T_STRING: {
            size_t len = r.count(1);
            return Value::make_string(r.bytes(len));
        }
        case T_ARRAY: {
            size_t c = r.count(1);
            Value a = Value::make_array();
            auto& el = a.as_array()->elements;
            el.reserve(c);
            for (size_t i = 0; i < c; ++i) el.push_back(decode(r, depth + 1));
            return a;
        }
        case T_PACKED: {
            ValueType kind = (ValueType)r.byte();
            size_t sz = packed_size(kind);
            if (!sz) { r.at = tag_at + 1; r.fail("packed array of an unknown kind"); }
            size_t c = r.count(sz);
            Value a = Value::make_array();
            auto& el = a.as_array()->elements;
            el.resize(c);
            const uint8_t* src = r.p + r.at;
            for (size_t i = 0; i < c; ++i, src += sz) {
                Value& e = el[i];
                e.type = kind;
                switch (kind) {
                    case ValueType::BOOLEAN: e.boolean = *src != 0; break;
                    case ValueType::BYTE:    e.byte_val = *src; break;
                    case ValueType::INT16:   std::memcpy(&e.i16, src, 2); break;
                    case ValueType::FLOAT16: std::memcpy(&e.f16_bits, src, 2); break;
                    case ValueType::INT32:   std::memcpy(&e.i32, src, 4); break;
                    case ValueType::FLOAT32: std::memcpy(&e.f32, src, 4); break;
                    case ValueType::INT64:   std::memcpy(&e.i64, src, 8); break;
                    default:                 std::memcpy(&e.f64, src, 8); break;
                }
            }
            r.at += c * sz;
            return a;
        }
        case T_MAP: {
            size_t c = r.count(2);
            Value m = Value::make_object();
            auto& fields = m.as_object()->fields;
            fields.reserve(c);
            for (size_t i = 0; i < c; ++i) {
                std::string key = r.bytes(r.count(1));
                fields.emplace_back(std::move(key), decode(r, depth + 1));
            }
            return m;
        }
        case T_TENSOR: {
            size_t rank = r.count(1);
            std::vector<size_t> shape(rank);
            size_t expect = 1;
            for (size_t i = 0; i < rank; ++i) {
                shape[i] = (size_t)r.varint();
                if (shape[i] && expect > SIZE_MAX / shape[i]) r.fail("tensor shape overflows");
                expect *= shape[i];
            }
            size_t c = r.count(8);
            if (c != expect) r.fail("tensor data does not match its shape");
            std::vector<double> data(c);
            if (c) std::memcpy(data.data(), r.p + r.at, 8 * c);
            r.at += 8 * c;
            return Value::make_tensor(std::move(shape), std::move(data));
        }
        default:
            r.at = tag_at;
            r.fail("unknown tag");
    }
}

// The value packed in s; throws on anything but one complete JDV1 value.
inline Value unpack(const std::string& s) {
    Reader r{(const uint8_t*)s.data(), s.size()};
    if (s.size() < MAGIC_LEN || std::memcmp(s.data(), MAGIC, MAGIC_LEN) != 0)
        r.fail("not a JDV1 value (magic missing)");
    r.at = MAGIC_LEN;
    Value v = decode(r, 0);
    if (r.at != r.n) r.fail("trailing bytes after the value");
    return v;
}

}  // namespace jdv
