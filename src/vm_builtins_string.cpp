// VM builtins: text: slicing, case, trim, search, SPLIT/JOIN, FORMAT$, PACK$/UNPACK, regex.

#include "vm_internal.h"
#include "jdb_format.h"
#include "jdb_text.h"
#include "jdb_pack.h"
#ifndef JDB_LEAN
#include "jdb_regex.h"
#endif
using jdb_text::View;

void VM::register_string_builtins() {
    // String functions
    register_native("LEN", 1, 1, [](const std::vector<Value>& args) -> Value {
        // Always scalar: element count for arrays, byte count for strings.
        // Use LENV for the shape vector of a nested array.
        if (args[0].type == ValueType::STRING)
            return Value::make_i64(args[0].as_string()->data.size());
        if (args[0].type == ValueType::ARRAY)
            return Value::make_i64(args[0].as_array()->elements.size());
        if (args[0].type == ValueType::OBJECT)
            return Value::make_i64(args[0].as_object()->fields.size());
        return Value::make_i64(0);
    });
    register_native("LENV", 1, 1, [](const std::vector<Value>& args) -> Value {
        // Shape vector: [dim0, dim1, …]. Non-nested arrays return [n];
        // strings return [byte_count]; scalars return [0].
        Value shape = Value::make_array();
        auto& elems = shape.as_array()->elements;
        if (args[0].type == ValueType::STRING) {
            elems.push_back(Value::make_i64(args[0].as_string()->data.size()));
            return shape;
        }
        if (args[0].type != ValueType::ARRAY) {
            elems.push_back(Value::make_i64(0));
            return shape;
        }
        const ArrayObj* cur = args[0].as_array();
        while (cur) {
            elems.push_back(Value::make_i64(cur->elements.size()));
            if (!cur->elements.empty() && cur->elements[0].type == ValueType::ARRAY)
                cur = cur->elements[0].as_array();
            else
                break;
        }
        return shape;
    });
    register_native("LEFT", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::left(args[0].as_string()->data, args[1].to_int()));
    });
    register_native("RIGHT", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::right(args[0].as_string()->data, args[1].to_int()));
    });
    // Strict MID: a start past the end is an error TRY/CATCH can observe.
    register_native("MID", 2, 3, [](const std::vector<Value>& args) -> Value {
        const std::string& s = args[0].as_string()->data;
        int64_t start = args[1].to_int();
        if (!jdb_text::in_range(s, start)) throw std::runtime_error("MID: index out of range");
        int64_t len = args.size() > 2 ? args[2].to_int() : -1;
        return Value::make_string(jdb_text::mid(s, start, len));
    });
    register_native("STR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("VAL", 1, 1, [](const std::vector<Value>& args) -> Value {
        // A number is already the value, as in the compiled program.
        if (is_numeric(args[0].type)) return Value::make_f64(args[0].to_double());
        return Value::make_f64(jdb_num::parse_prefix(args[0].as_string()->data.c_str()));
    });
    register_native("CHR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::chr(args[0].to_int()));
    });
    register_native("ASC", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_text::asc(args[0].as_string()->data));
    });
    register_native("UCASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::upper(args[0].as_string()->data));
    });
    register_native("LCASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::lower(args[0].as_string()->data));
    });

    register_native("LCASE$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::lower(args[0].as_string()->data));
    });

    // ── String functions ($ aliases + new ones) ──────────────

    // $ aliases for existing functions
    // String slicing helpers - implicitly stringify non-string inputs so
    // callers can pass e.g. a DATE-tagged FLOAT64 or a number directly,
    // matching classic BASIC's loose typing.
    register_native("LEFT$", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::left(args[0].to_string(), args[1].to_int()));
    });
    register_native("RIGHT$", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::right(args[0].to_string(), args[1].to_int()));
    });
    register_native("MID$", [](const std::vector<Value>& args) -> Value {
        int64_t len = args.size() > 2 ? args[2].to_int() : -1;
        return Value::make_string(jdb_text::mid(args[0].to_string(), args[1].to_int(), len));
    });
    register_native("UCASE$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::upper(args[0].as_string()->data));
    });
    // UPPER$ / LOWER$ - aliases the native compiler already exports
    // through its arr_apply table (jdb_upper / jdb_lower). Registered
    // in the VM so interpreter mode mirrors native and the shared
    // test suite (tests/native_test.jdb) doesn't fail on either side.
    register_native("UPPER$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::upper(args[0].as_string()->data));
    });
    register_native("LOWER$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::lower(args[0].as_string()->data));
    });
    register_native("STR$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("CHR$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::chr(args[0].to_int()));
    });
    register_native("INSTR$", [](const std::vector<Value>& args) -> Value {
        // Same as INSTR
        int64_t start = 0;
        std::string haystack, needle;
        if (args.size() >= 3) {
            // The position comes first. The other dialect's order puts the
            // text first and the position last, and reading a string as a
            // position answered 0 without saying anything.
            if (args[0].type == ValueType::STRING)
                throw jdError(ErrCode::WRONG_ARG_TYPE,
                    "INSTR$: the position comes first - INSTR$(start, haystack, needle)");
            start = args[0].to_int();
            haystack = args[1].as_string()->data;
            needle = args[2].as_string()->data;
        } else {
            haystack = args[0].as_string()->data;
            needle = args[1].as_string()->data;
        }
        return Value::make_i64(jdb_text::find(haystack, needle, start));
    });

    register_native("TRIM$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::trim(args[0].as_string()->data));
    });
    register_native("TRIM", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::trim(args[0].as_string()->data));
    });

    // INSTR
    register_native("INSTR", [](const std::vector<Value>& args) -> Value {
        int64_t start = 0;
        std::string haystack, needle;
        if (args.size() >= 3) {
            // The position comes first. The other dialect's order puts the
            // text first and the position last, and reading a string as a
            // position answered 0 without saying anything.
            if (args[0].type == ValueType::STRING)
                throw jdError(ErrCode::WRONG_ARG_TYPE,
                    "INSTR: the position comes first - INSTR(start, haystack, needle)");
            start = args[0].to_int();
            haystack = args[1].as_string()->data;
            needle = args[2].as_string()->data;
        } else {
            haystack = args[0].as_string()->data;
            needle = args[1].as_string()->data;
        }
        return Value::make_i64(jdb_text::find(haystack, needle, start));
    });

    // INSERT$: insert text into string or element into array at position
    register_native("INSERT$", 3, 3, [](const std::vector<Value>& args) -> Value {
        if (args[2].type == ValueType::STRING || args[2].type == ValueType::ARRAY)
            throw jdError(ErrCode::TYPE_MISMATCH, std::string("INSERT$: argument 3 is ") +
                (args[2].type == ValueType::ARRAY ? "an array" : "a string") + ", a number is expected");
        if (args[0].type == ValueType::STRING) {
            if (args[1].type != ValueType::STRING)
                throw jdError(ErrCode::TYPE_MISMATCH, "INSERT$: argument 2 is not a string");
            return Value::make_string(jdb_text::insert(args[0].as_string()->data,
                                                       args[1].as_string()->data, args[2].to_int()));
        }
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            r.as_array()->elements = args[0].as_array()->elements;
            int64_t pos = args[2].to_int();
            if (pos < 0) pos = 0;
            if (pos > (int64_t)r.as_array()->elements.size()) pos = r.as_array()->elements.size();
            r.as_array()->elements.insert(r.as_array()->elements.begin() + pos, args[1]);
            return r;
        }
        return args[0];
    });

    // SPLIT: the parts of a text between its delimiters.
    register_native("SPLIT", 2, 2, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        auto& out = r.as_array()->elements;
        jdb_text::split(args[0].as_string()->data, args[1].as_string()->data,
                        [&](std::string part) { out.push_back(Value::make_string(std::move(part))); });
        return r;
    });

    // REPLACE$: every occurrence replaced, in a text or in every string of an array.
    register_native("REPLACE$", 3, 3, [](const std::vector<Value>& args) -> Value {
        View find_text(args[1].as_string()->data), rep(args[2].as_string()->data);
        if (args[0].type == ValueType::STRING)
            return Value::make_string(jdb_text::replace_all(args[0].as_string()->data, find_text, rep));
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            for (auto& e : args[0].as_array()->elements) {
                if (e.type == ValueType::STRING)
                    r.as_array()->elements.push_back(
                        Value::make_string(jdb_text::replace_all(e.as_string()->data, find_text, rep)));
                else
                    r.as_array()->elements.push_back(e);
            }
            return r;
        }
        return args[0];
    });

    // REVERSE$: a text with its characters in the opposite order, or an
    // array with its elements reversed.
    register_native("REVERSE$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING)
            return Value::make_string(jdb_text::reverse(args[0].as_string()->data));
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            auto& src = args[0].as_array()->elements;
            r.as_array()->elements.assign(src.rbegin(), src.rend());
            return r;
        }
        return args[0];
    });

    // BYTEAT: raw byte value at index
    register_native("BYTEAT", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t idx = args[1].to_int();
        if (idx < 0 || idx >= (int64_t)s.size()) return Value::make_i64(0);
        return Value::make_i64((uint8_t)s[idx]);
    });

    // FORMAT$: the placeholder language of jdb_format.h over Values. A
    // placeholder past the arguments renders as "?".
    register_native("FORMAT$", [](const std::vector<Value>& args) -> Value {
        std::string result;
        jdb_fmt::expand(args[0].as_string()->data.c_str(), result,
            [&](int index, const std::string& spec, std::string& out) {
                size_t ai = (size_t)index + 1;
                if (ai >= args.size()) { out += "?"; return true; }
                const Value& v = args[ai];
                if (spec.empty()) { out += v.to_string(); return true; }
                jdb_fmt::Spec s;
                if (!jdb_fmt::parse_spec(spec.c_str(), s))
                    throw jdError(ErrCode::WRONG_ARG_TYPE, jdb_fmt::spec_error(spec.c_str()));
                bool number = is_numeric(v.type);
                std::string text;
                if (number) {
                    text = jdb_fmt::number_text(s, v.to_double(), v.to_int(), v.to_string());
                } else if (jdb_fmt::wants_number(s)) {
                    // A string under a numeric spec is read as the number it spells.
                    double d = v.type == ValueType::STRING ? std::strtod(v.as_string()->data.c_str(), nullptr)
                                                           : v.to_double();
                    text = jdb_fmt::number_text(s, d, (int64_t)d, v.to_string());
                } else {
                    text = v.to_string();
                }
                out += jdb_fmt::lay_out(s, text, number);
                return true;
            });
        return Value::make_string(result);
    });

    // FRMV$: format array as aligned table
    register_native("FRMV$", [this](const std::vector<Value>& args) -> Value {
        bool is_2d = (args[0].type == ValueType::ARRAY && !args[0].as_array()->elements.empty() &&
                      args[0].as_array()->elements[0].type == ValueType::ARRAY);
        bool has_fmt = (args.size() >= 2 && args[1].type == ValueType::STRING);
        std::string result;

        if (has_fmt) {
            // Apply format string per row: FORMAT$(fmt, col1, col2, ...)
            std::string fmt = args[1].as_string()->data;
            if (is_2d) {
                for (auto& row : args[0].as_array()->elements) {
                    if (row.type != ValueType::ARRAY) continue;
                    std::vector<Value> fmt_args;
                    fmt_args.push_back(Value::make_string(fmt));
                    for (auto& cell : row.as_array()->elements) fmt_args.push_back(cell);
                    Value line = call_function("FORMAT$", fmt_args);
                    result += line.as_string()->data + "\n";
                }
            } else {
                // 1D: format each element
                for (auto& e : args[0].as_array()->elements) {
                    std::vector<Value> fmt_args;
                    fmt_args.push_back(Value::make_string(fmt));
                    fmt_args.push_back(e);
                    Value line = call_function("FORMAT$", fmt_args);
                    result += line.as_string()->data + "\n";
                }
            }
        } else if (is_2d) {
            auto* rows = args[0].as_array();
            int cols = 0;
            for (auto& row : rows->elements)
                if (row.type == ValueType::ARRAY)
                    cols = std::max(cols, (int)row.as_array()->elements.size());
            std::vector<int> widths(cols, 0);
            for (auto& row : rows->elements) {
                if (row.type != ValueType::ARRAY) continue;
                for (int c = 0; c < (int)row.as_array()->elements.size(); c++) {
                    int w = (int)row.as_array()->elements[c].to_string().size();
                    if (w > widths[c]) widths[c] = w;
                }
            }
            for (auto& row : rows->elements) {
                if (row.type != ValueType::ARRAY) continue;
                for (int c = 0; c < (int)row.as_array()->elements.size(); c++) {
                    std::string s = row.as_array()->elements[c].to_string();
                    int pad = widths[c] - (int)s.size();
                    if (pad > 0) result += std::string(pad, ' ');
                    result += s;
                    if (c + 1 < (int)row.as_array()->elements.size()) result += " ";
                }
                result += "\n";
            }
        } else {
            for_each_leaf(args[0], [&](const Value& v) { result += v.to_string() + "\n"; });
        }
        return Value::make_string(result);
    });

    // PACK$: pack values to binary string. Format chars are CASE-INSENSITIVE
    // (so `>Isbsd` and `>isbsd` are equivalent - matches the docs which use
    // `I = Integer (4 bytes)`). Unknown chars throw a clean error so a typo
    // can't silently drop a value and produce a too-short string.
    //   < / >  little / big endian
    //   b      byte    (1)
    //   s      short   (2)
    //   i      int     (4)
    //   l      long    (8)
    //   f      float   (4)
    //   d      double  (8)
    //   c h n  the signed readings of b s i
    //   a      fixed string, the count is its length (NUL padded or cut)
    //   x      zero pad byte
    // A digit prefix repeats a code: "3i" is "iii".
    register_native("PACK$", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) throw std::runtime_error("PACK$: needs a format");
        auto fields = jdb_pack::parse(args[0].as_string()->data, "PACK$");
        std::string result;
        result.reserve(jdb_pack::size(fields));
        size_t ai = 1;
        for (auto& fd : fields) {
            if (fd.code == 'x') { result.append((size_t)fd.count, '\0'); continue; }
            if (fd.code == 'a') {
                if (ai >= args.size()) throw std::runtime_error("PACK$: not enough values for format");
                jdb_pack::put_text(result, fd, args[ai++].to_string());
                continue;
            }
            for (int k = 0; k < fd.count; k++) {
                if (ai >= args.size()) throw std::runtime_error("PACK$: not enough values for format");
                const Value& v = args[ai++];
                jdb_pack::put(result, fd, jdb_pack::is_float(fd.code) ? jdb_pack::float_bits(fd, v.to_double())
                                                                     : (uint64_t)v.to_int());
            }
        }
        return Value::make_string(result);
    });

    // UNPACK(fmt$, data$, [offset]): the same codes as PACK$. b s i read
    // unsigned, c h n and l signed, a gives the bytes as a string. Data
    // shorter than the format is an error.
    register_native("UNPACK", 2, 3, [](const std::vector<Value>& args) -> Value {
        auto fields = jdb_pack::parse(args[0].as_string()->data, "UNPACK");
        const std::string& data = args[1].as_string()->data;
        int64_t offset = args.size() >= 3 ? args[2].to_int() : 0;
        if (offset < 0 || (uint64_t)offset > data.size())
            throw std::runtime_error("UNPACK: offset " + std::to_string(offset) + " is outside the data");
        size_t need = jdb_pack::size(fields);
        if (data.size() - (size_t)offset < need)
            throw std::runtime_error("UNPACK: the format needs " + std::to_string(need) +
                                     " bytes, the data has " + std::to_string(data.size() - (size_t)offset));
        size_t pos = (size_t)offset;
        Value r = Value::make_array();
        auto& out = r.as_array()->elements;
        for (auto& fd : fields) {
            if (fd.code == 'x') { pos += (size_t)fd.count; continue; }
            if (fd.code == 'a') {
                out.push_back(Value::make_string(data.substr(pos, (size_t)fd.count)));
                pos += (size_t)fd.count;
                continue;
            }
            for (int k = 0; k < fd.count; k++) {
                uint64_t u = jdb_pack::get(data, pos, fd);
                pos += (size_t)jdb_pack::width(fd.code);
                if (jdb_pack::is_float(fd.code)) out.push_back(Value::make_f64(jdb_pack::as_double(fd, u)));
                else out.push_back(Value::make_i64(jdb_pack::as_int(fd, u)));
            }
        }
        return r;
    });

    // PACKSIZE(fmt$) -> the number of bytes PACK$ writes for the format.
    register_native("PACKSIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64((int64_t)jdb_pack::size(jdb_pack::parse(args[0].as_string()->data, "PACKSIZE")));
    });

    // The text of these three lives in builtin_sigs.h, which the native
    // compiler reads too, so neither side can drift from the other.
    register_native("VBNEWLINE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string(builtin_const_text("VBNEWLINE"));
    });
    register_native("VBCRLF", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string(builtin_const_text("VBCRLF"));
    });
    register_native("VBTAB", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string(builtin_const_text("VBTAB"));
    });
    // ── 2. Additional String ─────────────────────────────────

    register_native("JOIN", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        std::string delim = (args.size() >= 2) ? args[1].as_string()->data : "";
        std::string r;
        for (size_t i = 0; i < arr->elements.size(); i++) {
            if (i > 0) r += delim;
            r += arr->elements[i].to_string();
        }
        return Value::make_string(r);
    });
    register_native("STARTSWITH", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(jdb_text::starts_with(args[0].as_string()->data, args[1].as_string()->data));
    });
    register_native("ENDSWITH", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(jdb_text::ends_with(args[0].as_string()->data, args[1].as_string()->data));
    });
    register_native("LTRIM$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::ltrim(args[0].as_string()->data));
    });
    register_native("RTRIM$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::rtrim(args[0].as_string()->data));
    });
    register_native("SPACE$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::spaces(args[0].to_int()));
    });
    register_native("REPEAT$", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_text::repeat(args[0].as_string()->data, args[1].to_int()));
    });
    register_native("LPAD$", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string pad = args.size() >= 3 ? args[2].as_string()->data : " ";
        return Value::make_string(jdb_text::lpad(args[0].as_string()->data, args[1].to_int(), pad));
    });
    register_native("RPAD$", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string pad = args.size() >= 3 ? args[2].as_string()->data : " ";
        return Value::make_string(jdb_text::rpad(args[0].as_string()->data, args[1].to_int(), pad));
    });
    register_native("HEX$", [](const std::vector<Value>& args) -> Value {
        char buf[32]; jdb_num::format_hex(buf, sizeof(buf), args[0].to_int());
        return Value::make_string(buf);
    });
    register_native("BIN$", [](const std::vector<Value>& args) -> Value {
        char buf[72]; jdb_num::format_bin(buf, sizeof(buf), args[0].to_int());
        return Value::make_string(buf);
    });
    register_native("OCT$", [](const std::vector<Value>& args) -> Value {
        char buf[32]; jdb_num::format_oct(buf, sizeof(buf), args[0].to_int());
        return Value::make_string(buf);
    });
#ifndef JDB_LEAN
    // The legacy pair takes the text first.
    register_native("REGEX_MATCH", 2, 2, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        for (const std::string& m : jdb_regex::matches(args[1].as_string()->data, args[0].as_string()->data))
            r.as_array()->elements.push_back(Value::make_string(m));
        return r;
    });
    register_native("REGEX_REPLACE$", 3, 3, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_regex::replace(args[1].as_string()->data, args[0].as_string()->data,
                                                     args[2].as_string()->data));
    });

    // REGEX.MATCH(pattern, text): the captures when the pattern has groups,
    // a truth value when it has none.
    register_native("REGEX.MATCH", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::vector<std::string> groups;
        if (!jdb_regex::match(args[0].as_string()->data, args[1].as_string()->data, groups))
            return Value::make_bool(false);
        if (groups.empty()) return Value::make_bool(true);
        Value r = Value::make_array();
        for (const std::string& g : groups) r.as_array()->elements.push_back(Value::make_string(g));
        return r;
    });

    // REGEX.FINDALL(pattern, text): every match, or one row of groups per
    // match when the pattern has any.
    register_native("REGEX.FINDALL", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::vector<std::vector<std::string>> rows;
        bool has_groups = false;
        jdb_regex::find_all(args[0].as_string()->data, args[1].as_string()->data, rows, has_groups);
        Value r = Value::make_array();
        for (auto& row : rows) {
            if (!has_groups) { r.as_array()->elements.push_back(Value::make_string(row[0])); continue; }
            Value cells = Value::make_array();
            for (const std::string& g : row) cells.as_array()->elements.push_back(Value::make_string(g));
            r.as_array()->elements.push_back(std::move(cells));
        }
        return r;
    });

    // REGEX.REPLACE(pattern, text, replacement)
    register_native("REGEX.REPLACE", 3, 3, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(jdb_regex::replace(args[0].as_string()->data, args[1].as_string()->data,
                                                     args[2].as_string()->data));
    });
#endif
}
