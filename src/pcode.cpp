#include "pcode.h"

#include "vm.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <istream>
#include <ostream>

const char PCODE_MAGIC[4] = { 'J', 'D', 'P', 'B' };

// Everything goes out little-endian and byte at a time, so the file a
// desktop writes is the file a board reads.
namespace {

void put_u8(std::ostream& o, uint8_t v)  { o.put((char)v); }

void put_u32(std::ostream& o, uint32_t v) {
    for (int i = 0; i < 4; i++) o.put((char)((v >> (i * 8)) & 0xFF));
}

void put_u64(std::ostream& o, uint64_t v) {
    for (int i = 0; i < 8; i++) o.put((char)((v >> (i * 8)) & 0xFF));
}

void put_i32(std::ostream& o, int32_t v)  { put_u32(o, (uint32_t)v); }

void put_bytes(std::ostream& o, const void* p, size_t n) {
    put_u32(o, (uint32_t)n);
    if (n) o.write((const char*)p, (std::streamsize)n);
}

void put_str(std::ostream& o, const std::string& s) {
    put_bytes(o, s.data(), s.size());
}

bool get_u8(std::istream& i, uint8_t& v) {
    int c = i.get();
    if (c < 0) return false;
    v = (uint8_t)c;
    return true;
}

bool get_u32(std::istream& i, uint32_t& v) {
    v = 0;
    for (int k = 0; k < 4; k++) {
        int c = i.get();
        if (c < 0) return false;
        v |= (uint32_t)(uint8_t)c << (k * 8);
    }
    return true;
}

bool get_u64(std::istream& i, uint64_t& v) {
    v = 0;
    for (int k = 0; k < 8; k++) {
        int c = i.get();
        if (c < 0) return false;
        v |= (uint64_t)(uint8_t)c << (k * 8);
    }
    return true;
}

bool get_i32(std::istream& i, int32_t& v) {
    uint32_t u;
    if (!get_u32(i, u)) return false;
    v = (int32_t)u;
    return true;
}

// A length and then that many bytes. The cap is what keeps a truncated
// or foreign file from asking for a gigabyte on a board.
bool get_str(std::istream& i, std::string& s, uint32_t cap) {
    uint32_t n;
    if (!get_u32(i, n) || n > cap) return false;
    s.resize(n);
    if (n) i.read(&s[0], (std::streamsize)n);
    return (bool)i;
}

#define PCODE_MAX_BLOB (8u * 1024u * 1024u)

// The opcode set the file was built against. Adding an opcode changes
// the count and an older file is refused; reordering the existing ones
// does not, which is what the format revision beside it is for.
uint32_t opcode_count() { return (uint32_t)OpCode::FOREACH_NEXT + 1u; }

bool write_value(std::ostream& o, const Value& v, std::string& err) {
    put_u8(o, (uint8_t)v.type);
    switch (v.type) {
        case ValueType::NONE:                        return true;
        case ValueType::BOOLEAN: put_u8(o, v.boolean ? 1 : 0); return true;
        case ValueType::BYTE:    put_u8(o, v.byte_val);        return true;
        case ValueType::INT16:   put_u64(o, (uint64_t)(int64_t)v.i16); return true;
        case ValueType::INT32:   put_u64(o, (uint64_t)(int64_t)v.i32); return true;
        case ValueType::INT64:   put_u64(o, (uint64_t)v.i64);          return true;
        case ValueType::FLOAT16: put_u64(o, (uint64_t)v.f16_bits);     return true;
        case ValueType::FLOAT32: {
            uint32_t bits;
            std::memcpy(&bits, &v.f32, 4);
            put_u64(o, bits);
            return true;
        }
        case ValueType::FLOAT64: {
            uint64_t bits;
            std::memcpy(&bits, &v.f64, 8);
            put_u64(o, bits);
            return true;
        }
        case ValueType::STRING:
            put_str(o, v.as_string() ? v.as_string()->data : std::string());
            return true;
        default:
            // A compiler only ever puts literals in a constant table, so
            // reaching here means the format has fallen behind the
            // language rather than that the program is unusual.
            err = "constant of kind " + std::to_string((int)v.type)
                + " cannot be written as p-code";
            return false;
    }
}

bool read_value(std::istream& i, Value& v, std::string& err) {
    uint8_t t;
    if (!get_u8(i, t)) { err = "truncated constant"; return false; }
    switch ((ValueType)t) {
        case ValueType::NONE: v = Value(); return true;
        case ValueType::BOOLEAN: {
            uint8_t b;
            if (!get_u8(i, b)) return false;
            v = Value::make_bool(b != 0);
            return true;
        }
        case ValueType::BYTE: {
            uint8_t b;
            if (!get_u8(i, b)) return false;
            v = Value::make_i64(b);
            v.type = ValueType::BYTE;
            v.byte_val = b;
            return true;
        }
        case ValueType::INT16:
        case ValueType::INT32:
        case ValueType::INT64: {
            uint64_t u;
            if (!get_u64(i, u)) return false;
            v = Value::make_i64((int64_t)u);
            v.type = (ValueType)t;
            if ((ValueType)t == ValueType::INT16) v.i16 = (int16_t)(int64_t)u;
            else if ((ValueType)t == ValueType::INT32) v.i32 = (int32_t)(int64_t)u;
            return true;
        }
        case ValueType::FLOAT16: {
            uint64_t u;
            if (!get_u64(i, u)) return false;
            v = Value::make_f64(0);
            v.type = ValueType::FLOAT16;
            v.f16_bits = (uint16_t)u;
            return true;
        }
        case ValueType::FLOAT32: {
            uint64_t u;
            if (!get_u64(i, u)) return false;
            uint32_t bits = (uint32_t)u;
            float f;
            std::memcpy(&f, &bits, 4);
            v = Value::make_f64(f);
            v.type = ValueType::FLOAT32;
            v.f32 = f;
            return true;
        }
        case ValueType::FLOAT64: {
            uint64_t u;
            if (!get_u64(i, u)) return false;
            double d;
            std::memcpy(&d, &u, 8);
            v = Value::make_f64(d);
            return true;
        }
        case ValueType::STRING: {
            std::string s;
            if (!get_str(i, s, PCODE_MAX_BLOB)) return false;
            v = Value::make_string(s);
            return true;
        }
        default:
            err = "p-code holds a constant of kind " + std::to_string((int)t);
            return false;
    }
}

// A native call carries a slot number that only means anything inside
// the build that produced it: slots are handed out in registration
// order, and a board registers a different set of builtins from a
// desktop. So the names travel with the file and the slots are rewritten
// against the target's registry on load. Without this a file built on
// one and run on the other calls the wrong builtin and says nothing.
static void collect_native_slots(const Chunk& c, std::vector<uint16_t>& out) {
    size_t ip = 0;
    while (ip + 2 < c.code.size()) {
        OpCode op = (OpCode)c.code[ip];
        int w = opcode_width(op);
        if (w <= 0) break;
        if (op == OpCode::CALL_NATIVE) {
            uint16_t slot = (uint16_t)(c.code[ip + 1] | (c.code[ip + 2] << 8));
            if (std::find(out.begin(), out.end(), slot) == out.end())
                out.push_back(slot);
        }
        ip += (size_t)w;
    }
}

static void remap_native_slots(Chunk& c, const std::map<uint16_t, uint16_t>& map) {
    if (map.empty()) return;
    size_t ip = 0;
    while (ip + 2 < c.code.size()) {
        OpCode op = (OpCode)c.code[ip];
        int w = opcode_width(op);
        if (w <= 0) break;
        if (op == OpCode::CALL_NATIVE) {
            uint16_t slot = (uint16_t)(c.code[ip + 1] | (c.code[ip + 2] << 8));
            auto it = map.find(slot);
            if (it != map.end()) {
                c.code[ip + 1] = (uint8_t)(it->second & 0xFF);
                c.code[ip + 2] = (uint8_t)(it->second >> 8);
            }
        }
        ip += (size_t)w;
    }
}

bool write_chunk(std::ostream& o, const Chunk& c, std::string& err) {
    put_bytes(o, c.code.data(), c.code.size());

    put_u32(o, (uint32_t)c.constants.size());
    for (const auto& v : c.constants)
        if (!write_value(o, v, err)) return false;

    put_str(o, c.name_blob);
    put_u32(o, (uint32_t)c.name_offsets.size());
    for (uint32_t off : c.name_offsets) put_u32(o, off);

    put_u32(o, (uint32_t)c.line_table.size());
    for (const auto& e : c.line_table) {
        put_u32(o, e.offset);
        put_i32(o, e.line);
    }
    put_str(o, c.source_file);

    // The caches grow at runtime and are not written. The STATIC storage
    // is not a cache: its slots are baked into the code, so the size has
    // to survive even though every value in it starts out empty.
    put_u32(o, (uint32_t)c.static_values.size());

    std::vector<uint16_t> slots;
    collect_native_slots(c, slots);
    put_u32(o, (uint32_t)slots.size());
    for (uint16_t slot : slots) {
        put_u32(o, (uint32_t)slot);
        put_str(o, jdb_native_name((int)slot));
    }
    return (bool)o;
}

bool read_chunk(std::istream& i, Chunk& c, std::string& err) {
    uint32_t n;

    if (!get_u32(i, n) || n > PCODE_MAX_BLOB) { err = "bad code length"; return false; }
    c.code.resize(n);
    if (n) i.read((char*)c.code.data(), (std::streamsize)n);
    if (!i) { err = "truncated code"; return false; }

    if (!get_u32(i, n) || n > PCODE_MAX_BLOB) { err = "bad constant count"; return false; }
    c.constants.resize(n);
    for (uint32_t k = 0; k < n; k++)
        if (!read_value(i, c.constants[k], err)) return false;

    if (!get_str(i, c.name_blob, PCODE_MAX_BLOB)) { err = "bad name blob"; return false; }
    if (!get_u32(i, n) || n > PCODE_MAX_BLOB) { err = "bad name count"; return false; }
    c.name_offsets.resize(n);
    for (uint32_t k = 0; k < n; k++)
        if (!get_u32(i, c.name_offsets[k])) { err = "truncated names"; return false; }

    if (!get_u32(i, n) || n > PCODE_MAX_BLOB) { err = "bad line table"; return false; }
    c.line_table.resize(n);
    for (uint32_t k = 0; k < n; k++) {
        if (!get_u32(i, c.line_table[k].offset)) { err = "truncated lines"; return false; }
        if (!get_i32(i, c.line_table[k].line))   { err = "truncated lines"; return false; }
    }
    if (!get_str(i, c.source_file, 4096)) { err = "bad source name"; return false; }

    if (!get_u32(i, n) || n > PCODE_MAX_BLOB) { err = "bad static count"; return false; }
    c.static_values.assign(n, Value());
    c.static_inited.assign(n, 0);

    if (!get_u32(i, n) || n > PCODE_MAX_BLOB) { err = "bad native count"; return false; }
    std::map<uint16_t, uint16_t> remap;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t was;
        std::string name;
        if (!get_u32(i, was)) { err = "truncated natives"; return false; }
        if (!get_str(i, name, 128)) { err = "bad native name"; return false; }
        if (name.empty()) { err = "empty native name"; return false; }
        int now = jdb_native_slot(name);
        if (now < 0) { err = "unknown builtin: " + name; return false; }
        if ((uint32_t)now != was) remap[(uint16_t)was] = (uint16_t)now;
    }
    remap_native_slots(c, remap);
    return true;
}

} // namespace

bool pcode_is_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    char head[4] = {0, 0, 0, 0};
    in.read(head, 4);
    return in.gcount() == 4 && std::memcmp(head, PCODE_MAGIC, 4) == 0;
}

bool pcode_write(std::ostream& out, const Chunk& main_chunk,
                 const std::vector<FuncProto>& funcs, std::string& err) {
    out.write(PCODE_MAGIC, 4);
    put_u32(out, PCODE_FORMAT_VERSION);
    put_u32(out, opcode_count());

    if (!write_chunk(out, main_chunk, err)) return false;

    put_u32(out, (uint32_t)funcs.size());
    for (const auto& f : funcs) {
        put_str(out, f.name);
        put_i32(out, f.arity);
        put_i32(out, f.min_arity);
        put_u8(out, (uint8_t)((f.is_sub ? 1 : 0) | (f.is_async ? 2 : 0)
                              | (f.is_exported ? 4 : 0)));
        put_u32(out, (uint32_t)f.defaults.size());
        for (const auto& d : f.defaults)
            if (!write_value(out, d, err)) return false;
        put_u32(out, (uint32_t)f.param_names.size());
        for (const auto& p : f.param_names) put_str(out, p);
        if (!write_chunk(out, f.chunk, err)) return false;
    }
    if (!out) { err = "write failed"; return false; }
    return true;
}

bool pcode_read(std::istream& in, Chunk& main_chunk,
                std::vector<FuncProto>& funcs, std::string& err) {
    char head[4];
    in.read(head, 4);
    if (in.gcount() != 4 || std::memcmp(head, PCODE_MAGIC, 4) != 0) {
        err = "not a p-code file";
        return false;
    }
    uint32_t fmt = 0, ops = 0;
    if (!get_u32(in, fmt) || !get_u32(in, ops)) { err = "truncated header"; return false; }
    if (fmt != PCODE_FORMAT_VERSION) {
        err = "p-code revision " + std::to_string(fmt) + ", this build reads "
            + std::to_string(PCODE_FORMAT_VERSION);
        return false;
    }
    if (ops != opcode_count()) {
        err = "p-code was built against " + std::to_string(ops)
            + " opcodes, this build has " + std::to_string(opcode_count())
            + " - recompile the source";
        return false;
    }

    if (!read_chunk(in, main_chunk, err)) return false;

    uint32_t n;
    if (!get_u32(in, n) || n > 65536) { err = "bad function count"; return false; }
    funcs.clear();
    funcs.resize(n);
    for (uint32_t k = 0; k < n; k++) {
        FuncProto& f = funcs[k];
        if (!get_str(in, f.name, 4096)) { err = "bad function name"; return false; }
        int32_t a = 0, m = 0;
        if (!get_i32(in, a) || !get_i32(in, m)) { err = "truncated function"; return false; }
        f.arity = a;
        f.min_arity = m;
        uint8_t flags = 0;
        if (!get_u8(in, flags)) { err = "truncated function"; return false; }
        f.is_sub      = (flags & 1) != 0;
        f.is_async    = (flags & 2) != 0;
        f.is_exported = (flags & 4) != 0;

        uint32_t d;
        if (!get_u32(in, d) || d > 4096) { err = "bad default count"; return false; }
        f.defaults.resize(d);
        for (uint32_t j = 0; j < d; j++)
            if (!read_value(in, f.defaults[j], err)) return false;

        uint32_t p;
        if (!get_u32(in, p) || p > 4096) { err = "bad parameter count"; return false; }
        f.param_names.resize(p);
        for (uint32_t j = 0; j < p; j++)
            if (!get_str(in, f.param_names[j], 4096)) { err = "bad parameter"; return false; }

        if (!read_chunk(in, f.chunk, err)) return false;
    }
    return true;
}
