// VM builtins: text: slicing, case, trim, search, SPLIT/JOIN, FORMAT$, PACK$/UNPACK, regex.

#include "vm_internal.h"

// PACK$ / UNPACK format: < or > sets the byte order for the codes after it,
// each code takes an optional repeat count. For 'a' the count is the string
// length, for 'x' the number of zero pad bytes.
struct PackField {
    char code;
    int count;
    bool big_endian;
};

static int pack_width(char code) {
    switch (code) {
        case 'b': case 'c': case 'a': case 'x': return 1;
        case 's': case 'h': return 2;
        case 'i': case 'n': case 'f': return 4;
        case 'l': case 'd': return 8;
    }
    return 0;
}

static std::vector<PackField> pack_parse(const std::string& fmt, const char* who) {
    std::vector<PackField> fields;
    bool big_endian = false;
    int64_t count = -1;
    for (char c : fmt) {
        if (c == ' ' || c == '\t') continue;
        if (c >= '0' && c <= '9') {
            count = (count < 0 ? 0 : count) * 10 + (c - '0');
            if (count > 100000000)
                throw std::runtime_error(std::string(who) + ": repeat count too large");
            continue;
        }
        if (count >= 0 && (c == '<' || c == '>'))
            throw std::runtime_error(std::string(who) + ": a count must be followed by a code");
        if (c == '<') { big_endian = false; continue; }
        if (c == '>') { big_endian = true; continue; }
        char lc = (char)std::tolower((unsigned char)c);
        if (pack_width(lc) == 0)
            throw std::runtime_error(std::string(who) + ": unknown format char '" + c + "'");
        fields.push_back({lc, count < 0 ? 1 : (int)count, big_endian});
        count = -1;
    }
    if (count >= 0)
        throw std::runtime_error(std::string(who) + ": a count must be followed by a code");
    return fields;
}

static size_t pack_size(const std::vector<PackField>& fields) {
    size_t total = 0;
    for (auto& f : fields) total += (size_t)pack_width(f.code) * (size_t)f.count;
    return total;
}

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
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        return Value::make_string(s.substr(0, n));
    });
    register_native("RIGHT", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        if (n >= (int64_t)s.size()) return Value::make_string(s);
        return Value::make_string(s.substr(s.size() - n));
    });
    register_native("MID", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t start = args[1].to_int();
        int64_t len = args.size() > 2 ? args[2].to_int() : (int64_t)s.size();
        return Value::make_string(s.substr(start, len));
    });
    register_native("STR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("VAL", 1, 1, [](const std::vector<Value>& args) -> Value {
        try { return Value::make_f64(std::stod(args[0].as_string()->data)); }
        catch (...) { return Value::make_f64(0); }
    });
    register_native("CHR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(std::string(1, static_cast<char>(args[0].to_int())));
    });
    register_native("ASC", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto& s = args[0].as_string()->data;
        return Value::make_i64(s.empty() ? 0 : static_cast<uint8_t>(s[0]));
    });
    register_native("UCASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return Value::make_string(s);
    });
    register_native("LCASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return Value::make_string(s);
    });

    register_native("LCASE$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return Value::make_string(s);
    });

    // ── String functions ($ aliases + new ones) ──────────────

    // $ aliases for existing functions
    // String slicing helpers - implicitly stringify non-string inputs so
    // callers can pass e.g. a DATE-tagged FLOAT64 or a number directly,
    // matching classic BASIC's loose typing.
    register_native("LEFT$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].to_string();
        int64_t n = args[1].to_int();
        if (n < 0) n = 0;
        if (n > (int64_t)s.size()) n = (int64_t)s.size();
        return Value::make_string(s.substr(0, n));
    });
    register_native("RIGHT$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].to_string();
        int64_t n = args[1].to_int();
        if (n < 0) n = 0;
        if (n >= (int64_t)s.size()) return Value::make_string(s);
        return Value::make_string(s.substr(s.size() - n));
    });
    register_native("MID$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].to_string();
        int64_t start = args[1].to_int();
        int64_t len = args.size() > 2 ? args[2].to_int() : (int64_t)s.size();
        if (start < 0) start = 0;
        if (start > (int64_t)s.size()) return Value::make_string("");
        return Value::make_string(s.substr(start, len));
    });
    register_native("UCASE$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return Value::make_string(s);
    });
    // UPPER$ / LOWER$ - aliases the native compiler already exports
    // through its arr_apply table (jdb_upper / jdb_lower). Registered
    // in the VM so interpreter mode mirrors native and the shared
    // test suite (tests/native_test.jdb) doesn't fail on either side.
    register_native("UPPER$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return Value::make_string(s);
    });
    register_native("LOWER$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return Value::make_string(s);
    });
    register_native("STR$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("CHR$", [](const std::vector<Value>& args) -> Value {
        // 0..255 is one raw byte, a code above 255 is that Unicode character in UTF-8.
        int64_t code = args[0].to_int();
        if (code <= 255 || code > 0x10FFFF) return Value::make_string(std::string(1, (char)code));
        std::string s;
        if (code < 0x800) {
            s += (char)(0xC0 | (code >> 6));
        } else if (code < 0x10000) {
            s += (char)(0xE0 | (code >> 12));
            s += (char)(0x80 | ((code >> 6) & 0x3F));
        } else {
            s += (char)(0xF0 | (code >> 18));
            s += (char)(0x80 | ((code >> 12) & 0x3F));
            s += (char)(0x80 | ((code >> 6) & 0x3F));
        }
        s += (char)(0x80 | (code & 0x3F));
        return Value::make_string(s);
    });
    register_native("INSTR$", [](const std::vector<Value>& args) -> Value {
        // Same as INSTR
        int64_t start = 0;
        std::string haystack, needle;
        if (args.size() >= 3) {
            start = args[0].to_int();
            haystack = args[1].as_string()->data;
            needle = args[2].as_string()->data;
        } else {
            haystack = args[0].as_string()->data;
            needle = args[1].as_string()->data;
        }
        size_t pos = haystack.find(needle, start);
        return Value::make_i64(pos == std::string::npos ? -1 : (int64_t)pos);
    });

    // TRIM$
    register_native("TRIM$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return Value::make_string(a == std::string::npos ? "" : s.substr(a, b - a + 1));
    });
    register_native("TRIM", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return Value::make_string(a == std::string::npos ? "" : s.substr(a, b - a + 1));
    });

    // INSTR
    register_native("INSTR", [](const std::vector<Value>& args) -> Value {
        int64_t start = 0;
        std::string haystack, needle;
        if (args.size() >= 3) {
            start = args[0].to_int();
            haystack = args[1].as_string()->data;
            needle = args[2].as_string()->data;
        } else {
            haystack = args[0].as_string()->data;
            needle = args[1].as_string()->data;
        }
        size_t pos = haystack.find(needle, start);
        return Value::make_i64(pos == std::string::npos ? -1 : (int64_t)pos);
    });

    // INSERT$: insert text into string or element into array at position
    register_native("INSERT$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING) {
            std::string s = args[0].as_string()->data;
            std::string ins = args[1].as_string()->data;
            int64_t pos = args[2].to_int();
            if (pos < 0) pos = 0;
            if (pos > (int64_t)s.size()) pos = s.size();
            s.insert(pos, ins);
            return Value::make_string(s);
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

    // SPLIT: split string by delimiter
    register_native("SPLIT", [](const std::vector<Value>& args) -> Value {
        std::string src = args[0].as_string()->data;
        std::string delim = args[1].as_string()->data;
        Value r = Value::make_array();
        if (delim.empty()) {
            r.as_array()->elements.push_back(Value::make_string(src));
            return r;
        }
        size_t pos = 0;
        while (true) {
            size_t found = src.find(delim, pos);
            if (found == std::string::npos) {
                r.as_array()->elements.push_back(Value::make_string(src.substr(pos)));
                break;
            }
            r.as_array()->elements.push_back(Value::make_string(src.substr(pos, found - pos)));
            pos = found + delim.size();
        }
        return r;
    });

    // REPLACE$: replace all occurrences
    register_native("REPLACE$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING) {
            std::string s = args[0].as_string()->data;
            std::string find = args[1].as_string()->data;
            std::string repl = args[2].as_string()->data;
            if (!find.empty()) {
                size_t pos = 0;
                while ((pos = s.find(find, pos)) != std::string::npos) {
                    s.replace(pos, find.size(), repl);
                    pos += repl.size();
                }
            }
            return Value::make_string(s);
        }
        // Array: element-wise
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            for (auto& e : args[0].as_array()->elements) {
                if (e.type == ValueType::STRING) {
                    std::string s = e.as_string()->data;
                    std::string find = args[1].as_string()->data;
                    std::string repl = args[2].as_string()->data;
                    if (!find.empty()) {
                        size_t pos = 0;
                        while ((pos = s.find(find, pos)) != std::string::npos) {
                            s.replace(pos, find.size(), repl);
                            pos += repl.size();
                        }
                    }
                    r.as_array()->elements.push_back(Value::make_string(s));
                } else {
                    r.as_array()->elements.push_back(e);
                }
            }
            return r;
        }
        return args[0];
    });

    // REVERSE$: reverse a string (REVERSE for arrays already exists)
    register_native("REVERSE$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING) {
            std::string s = args[0].as_string()->data;
            // UTF-8 aware reverse
            std::vector<std::string> chars;
            for (size_t i = 0; i < s.size(); ) {
                unsigned char c = s[i];
                int len = 1;
                if (c >= 0xF0) len = 4;
                else if (c >= 0xE0) len = 3;
                else if (c >= 0xC0) len = 2;
                chars.push_back(s.substr(i, len));
                i += len;
            }
            std::string r;
            for (int i = (int)chars.size() - 1; i >= 0; i--) r += chars[i];
            return Value::make_string(r);
        }
        // Array: delegate to REVERSE
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            auto& src = args[0].as_array()->elements;
            r.as_array()->elements.assign(src.rbegin(), src.rend());
            return r;
        }
        return args[0];
    });

    // BYTEAT: raw byte value at index
    register_native("BYTEAT", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t idx = args[1].to_int();
        if (idx < 0 || idx >= (int64_t)s.size()) return Value::make_i64(0);
        return Value::make_i64((uint8_t)s[idx]);
    });

    // FORMAT$: C++20-style string formatting
    register_native("FORMAT$", [](const std::vector<Value>& args) -> Value {
        std::string fmt = args[0].as_string()->data;
        std::string result;
        size_t arg_idx = 1; // next auto-arg
        size_t i = 0;
        while (i < fmt.size()) {
            if (fmt[i] == '{' && i + 1 < fmt.size()) {
                if (fmt[i + 1] == '{') { result += '{'; i += 2; continue; } // escaped {{
                size_t end = fmt.find('}', i);
                if (end == std::string::npos) { result += fmt[i++]; continue; }
                std::string spec = fmt.substr(i + 1, end - i - 1);
                i = end + 1;

                // Parse: [index][:format]
                size_t colon = spec.find(':');
                std::string idx_str = (colon != std::string::npos) ? spec.substr(0, colon) : spec;
                std::string fmt_spec = (colon != std::string::npos) ? spec.substr(colon + 1) : "";

                size_t ai;
                if (!idx_str.empty() && std::isdigit(idx_str[0]))
                    ai = std::stoi(idx_str) + 1; // +1 because args[0] is format string
                else
                    ai = arg_idx++;

                if (ai >= args.size()) { result += "?"; continue; }
                const Value& v = args[ai];

                if (fmt_spec.empty()) {
                    result += v.to_string();
                } else {
                    // Parse fill, align, width, precision, type
                    char fill = ' ', align = '>';
                    size_t fp = 0;
                    if (fmt_spec.size() >= 2 && (fmt_spec[1] == '<' || fmt_spec[1] == '>' || fmt_spec[1] == '^')) {
                        fill = fmt_spec[0]; align = fmt_spec[1]; fp = 2;
                    } else if (!fmt_spec.empty() && (fmt_spec[0] == '<' || fmt_spec[0] == '>' || fmt_spec[0] == '^')) {
                        align = fmt_spec[0]; fp = 1;
                    }
                    // Parse sign: + always, space for a positive number, - only for a negative
                    char sign = 0;
                    if (fp < fmt_spec.size() &&
                        (fmt_spec[fp] == '+' || fmt_spec[fp] == '-' || fmt_spec[fp] == ' '))
                        sign = fmt_spec[fp++];
                    // Parse width
                    int width = 0;
                    while (fp < fmt_spec.size() && std::isdigit(fmt_spec[fp]))
                        width = width * 10 + (fmt_spec[fp++] - '0');
                    // Parse .precision
                    int prec = -1;
                    if (fp < fmt_spec.size() && fmt_spec[fp] == '.') {
                        fp++; prec = 0;
                        while (fp < fmt_spec.size() && std::isdigit(fmt_spec[fp]))
                            prec = prec * 10 + (fmt_spec[fp++] - '0');
                    }
                    // Parse # flag and type
                    bool hash_flag = false;
                    char type = 0;
                    if (fp < fmt_spec.size() && fmt_spec[fp] == '#') { hash_flag = true; fp++; }
                    if (fp < fmt_spec.size()) type = fmt_spec[fp];
                    bool known_type = type == 0 || type == 'd' || type == 'f' || type == 'e' ||
                                      type == 'E' || type == 'g' || type == 'G' || type == 'x' ||
                                      type == 'X' || type == 's' || type == '%';
                    if (!known_type || fp + (type ? 1 : 0) != fmt_spec.size())
                        throw jdError(ErrCode::WRONG_ARG_TYPE,
                            "FORMAT$: unknown format spec \"{" + spec +
                            "}\"; use [[fill]align][sign][width][.precision][d|f|e|g|x|X|s|%]");

                    // Format the value
                    std::string sv;
                    if (type == 'd') {
                        sv = std::to_string(v.to_int());
                    } else if (type == 'f') {
                        std::ostringstream os;
                        if (prec >= 0) os << std::fixed << std::setprecision(prec);
                        os << v.to_double();
                        sv = os.str();
                    } else if (type == 'e' || type == 'E' || type == 'g' || type == 'G') {
                        char cfmt[8] = {'%', '.', '*', type, 0};
                        char buf[64]; snprintf(buf, sizeof(buf), cfmt, prec >= 0 ? prec : 6, v.to_double());
                        sv = buf;
                    } else if (type == '%') {
                        char buf[64]; snprintf(buf, sizeof(buf), "%.*f%%", prec >= 0 ? prec : 6, v.to_double() * 100.0);
                        sv = buf;
                    } else if (type == 'x') {
                        char buf[32]; snprintf(buf, sizeof(buf), "%llx", (long long)v.to_int()); sv = buf;
                        if (hash_flag) sv = "0x" + sv;
                    } else if (type == 'X') {
                        char buf[32]; snprintf(buf, sizeof(buf), "%llX", (long long)v.to_int()); sv = buf;
                        if (hash_flag) sv = "0x" + sv;
                    } else {
                        sv = v.to_string();
                        if (prec >= 0 && is_numeric(v.type)) {
                            std::ostringstream os;
                            os << std::fixed << std::setprecision(prec) << v.to_double();
                            sv = os.str();
                        }
                    }

                    if ((sign == '+' || sign == ' ') && is_numeric(v.type) && !sv.empty() && sv[0] != '-')
                        sv = std::string(1, sign) + sv;
                    // Apply width and alignment
                    if (width > 0 && (int)sv.size() < width) {
                        int pad = width - (int)sv.size();
                        if (align == '<') sv += std::string(pad, fill);
                        else if (align == '>') sv = std::string(pad, fill) + sv;
                        else { int l = pad / 2; sv = std::string(l, fill) + sv + std::string(pad - l, fill); }
                    }
                    result += sv;
                }
            } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
                result += '}'; i += 2;
            } else {
                result += fmt[i++];
            }
        }
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
        auto fields = pack_parse(args[0].as_string()->data, "PACK$");
        std::string result;
        result.reserve(pack_size(fields));
        size_t ai = 1;
        for (auto& fd : fields) {
            auto write = [&](const void* data, int n) {
                const uint8_t* p = (const uint8_t*)data;
                if (fd.big_endian) for (int i = n - 1; i >= 0; i--) result += (char)p[i];
                else for (int i = 0; i < n; i++) result += (char)p[i];
            };
            if (fd.code == 'x') { result.append((size_t)fd.count, '\0'); continue; }
            if (fd.code == 'a') {
                if (ai >= args.size())
                    throw std::runtime_error("PACK$: not enough values for format");
                std::string s = args[ai++].to_string();
                s.resize((size_t)fd.count, '\0');
                result += s;
                continue;
            }
            for (int k = 0; k < fd.count; k++) {
                if (ai >= args.size())
                    throw std::runtime_error("PACK$: not enough values for format");
                const Value& v = args[ai++];
                switch (fd.code) {
                    case 'b': case 'c': { uint8_t x = (uint8_t)v.to_int(); write(&x, 1); break; }
                    case 's': case 'h': { int16_t x = (int16_t)v.to_int(); write(&x, 2); break; }
                    case 'i': case 'n': { int32_t x = (int32_t)v.to_int(); write(&x, 4); break; }
                    case 'l': { int64_t x = v.to_int(); write(&x, 8); break; }
                    case 'f': { float x = (float)v.to_double(); write(&x, 4); break; }
                    case 'd': { double x = v.to_double(); write(&x, 8); break; }
                }
            }
        }
        return Value::make_string(result);
    });

    // UNPACK(fmt$, data$, [offset]): the same codes as PACK$. b s i read
    // unsigned, c h n and l signed, a gives the bytes as a string. Data
    // shorter than the format is an error.
    register_native("UNPACK", 2, 3, [](const std::vector<Value>& args) -> Value {
        auto fields = pack_parse(args[0].as_string()->data, "UNPACK");
        const std::string& data = args[1].as_string()->data;
        int64_t offset = args.size() >= 3 ? args[2].to_int() : 0;
        if (offset < 0 || (uint64_t)offset > data.size())
            throw std::runtime_error("UNPACK: offset " + std::to_string(offset) + " is outside the data");
        size_t need = pack_size(fields);
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
            int w = pack_width(fd.code);
            for (int k = 0; k < fd.count; k++) {
                uint64_t u = 0;
                for (int j = 0; j < w; j++) {
                    uint8_t byte = (uint8_t)(fd.big_endian ? data[pos + w - 1 - j] : data[pos + j]);
                    u |= (uint64_t)byte << (8 * j);
                }
                pos += (size_t)w;
                switch (fd.code) {
                    case 'b': case 's': case 'i': out.push_back(Value::make_i64((int64_t)u)); break;
                    case 'c': out.push_back(Value::make_i64((int8_t)(uint8_t)u)); break;
                    case 'h': out.push_back(Value::make_i64((int16_t)(uint16_t)u)); break;
                    case 'n': out.push_back(Value::make_i64((int32_t)(uint32_t)u)); break;
                    case 'l': out.push_back(Value::make_i64((int64_t)u)); break;
                    case 'f': { uint32_t b32 = (uint32_t)u; float fv; std::memcpy(&fv, &b32, 4);
                                out.push_back(Value::make_f64(fv)); break; }
                    case 'd': { double dv; std::memcpy(&dv, &u, 8);
                                out.push_back(Value::make_f64(dv)); break; }
                }
            }
        }
        return r;
    });

    // PACKSIZE(fmt$) -> the number of bytes PACK$ writes for the format.
    register_native("PACKSIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64((int64_t)pack_size(pack_parse(args[0].as_string()->data, "PACKSIZE")));
    });

    register_native("VBNEWLINE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string("\r\n");
    });
    register_native("VBCRLF", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string("\r\n");
    });
    register_native("VBTAB", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string("\t");
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
    register_native("STARTSWITH", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, p = args[1].as_string()->data;
        return Value::make_bool(s.size() >= p.size() && s.substr(0, p.size()) == p);
    });
    register_native("ENDSWITH", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, p = args[1].as_string()->data;
        return Value::make_bool(s.size() >= p.size() && s.substr(s.size() - p.size()) == p);
    });
    register_native("LTRIM$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t p = s.find_first_not_of(" \t\r\n");
        return Value::make_string(p == std::string::npos ? "" : s.substr(p));
    });
    register_native("RTRIM$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t p = s.find_last_not_of(" \t\r\n");
        return Value::make_string(p == std::string::npos ? "" : s.substr(0, p + 1));
    });
    register_native("SPACE$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(std::string((size_t)args[0].to_int(), ' '));
    });
    register_native("REPEAT$", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        if (n <= 0 || s.empty()) return Value::make_string("");
        std::string r; r.reserve(s.size() * (size_t)n);
        for (int64_t i = 0; i < n; i++) r += s;
        return Value::make_string(r);
    });
    register_native("LPAD$", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        std::string pad = (args.size() >= 3) ? args[2].as_string()->data : " ";
        if (pad.empty()) pad = " ";
        if ((int64_t)s.size() >= n) return Value::make_string(s);
        size_t need = (size_t)n - s.size();
        std::string prefix; prefix.reserve(need);
        while (prefix.size() < need) prefix += pad;
        prefix.resize(need);
        return Value::make_string(prefix + s);
    });
    register_native("RPAD$", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        std::string pad = (args.size() >= 3) ? args[2].as_string()->data : " ";
        if (pad.empty()) pad = " ";
        if ((int64_t)s.size() >= n) return Value::make_string(s);
        size_t need = (size_t)n - s.size();
        std::string suffix; suffix.reserve(need);
        while (suffix.size() < need) suffix += pad;
        suffix.resize(need);
        return Value::make_string(s + suffix);
    });
    register_native("HEX$", [](const std::vector<Value>& args) -> Value {
        char buf[32]; snprintf(buf, sizeof(buf), "%llX", (long long)args[0].to_int());
        return Value::make_string(buf);
    });
    register_native("BIN$", [](const std::vector<Value>& args) -> Value {
        int64_t v = args[0].to_int(); std::string r;
        if (v == 0) return Value::make_string("0");
        uint64_t u = (uint64_t)v; bool started = false;
        for (int i = 63; i >= 0; i--) {
            if (u & (1ULL << i)) { started = true; r += '1'; }
            else if (started) r += '0';
        }
        return Value::make_string(r);
    });
    register_native("OCT$", [](const std::vector<Value>& args) -> Value {
        char buf[32]; snprintf(buf, sizeof(buf), "%llo", (long long)args[0].to_int());
        return Value::make_string(buf);
    });
#ifndef JDB_LEAN
    register_native("REGEX_MATCH", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, pat = args[1].as_string()->data;
        Value r = Value::make_array();
        try {
            std::regex re(pat);
            std::sregex_iterator it(s.begin(), s.end(), re), end;
            for (; it != end; ++it)
                r.as_array()->elements.push_back(Value::make_string((*it)[0].str()));
        } catch (...) { }
        return r;
    });
#endif
#ifndef JDB_LEAN
    register_native("REGEX_REPLACE$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, pat = args[1].as_string()->data, repl = args[2].as_string()->data;
        try { return Value::make_string(std::regex_replace(s, std::regex(pat), repl)); }
        catch (...) { return Value::make_string(s); }
    });
#endif

    // REGEX.MATCH(pattern, text) → TRUE/FALSE or array of captures
#ifndef JDB_LEAN
    register_native("REGEX.MATCH", [](const std::vector<Value>& args) -> Value {
        const std::string& pat = args[0].as_string()->data;
        std::string text = args[1].as_string()->data;
        try {
            static std::unordered_map<std::string, std::regex> cache;
            auto cit = cache.find(pat);
            if (cit == cache.end()) cit = cache.emplace(pat, std::regex(pat)).first;
            const std::regex& re = cit->second;
            std::smatch m;
            if (!std::regex_match(text, m, re)) return Value::make_bool(false);
            // No capture groups → just TRUE
            if (m.size() <= 1) return Value::make_bool(true);
            // Has capture groups → return array of captures
            Value r = Value::make_array();
            for (size_t i = 1; i < m.size(); i++)
                r.as_array()->elements.push_back(Value::make_string(m[i].str()));
            return r;
        } catch (...) { return Value::make_bool(false); }
    });
#endif

    // REGEX.FINDALL(pattern, text) → 1D array of matches or 2D if groups
#ifndef JDB_LEAN
    register_native("REGEX.FINDALL", [](const std::vector<Value>& args) -> Value {
        const std::string& pat = args[0].as_string()->data;
        std::string text = args[1].as_string()->data;
        Value r = Value::make_array();
        try {
            static std::unordered_map<std::string, std::regex> cache;
            auto cit = cache.find(pat);
            if (cit == cache.end()) cit = cache.emplace(pat, std::regex(pat)).first;
            const std::regex& re = cit->second;
            std::sregex_iterator it(text.begin(), text.end(), re), end;
            bool has_groups = false;
            for (; it != end; ++it) {
                auto& m = *it;
                if (m.size() > 1) {
                    // Has capture groups → row per match
                    has_groups = true;
                    Value row = Value::make_array();
                    for (size_t i = 1; i < m.size(); i++)
                        row.as_array()->elements.push_back(Value::make_string(m[i].str()));
                    r.as_array()->elements.push_back(std::move(row));
                } else {
                    r.as_array()->elements.push_back(Value::make_string(m[0].str()));
                }
            }
        } catch (...) { }
        return r;
    });
#endif

    // REGEX.REPLACE(pattern, text, replacement) → string
#ifndef JDB_LEAN
    register_native("REGEX.REPLACE", [](const std::vector<Value>& args) -> Value {
        const std::string& pat = args[0].as_string()->data;
        const std::string& text = args[1].as_string()->data;
        const std::string& repl = args[2].as_string()->data;
        try {
            // Cache compiled patterns - std::regex construction is the dominant
            // cost when the same pattern runs over many lines (e.g. log triage).
            static std::unordered_map<std::string, std::regex> cache;
            auto it = cache.find(pat);
            if (it == cache.end()) it = cache.emplace(pat, std::regex(pat)).first;
            return Value::make_string(std::regex_replace(text, it->second, repl));
        }
        catch (...) { return Value::make_string(text); }
    });
#endif
}
