// VM builtins: type predicates and conversions, MAP.* and JSON.*.

#include "vm_internal.h"

// The number a conversion function reads from its argument. A string is
// parsed whole, surrounding spaces allowed; any other text is a type mismatch.
static double conversion_arg(const Value& v, const char* fn) {
    if (v.type != ValueType::STRING) return v.to_double();
    const std::string& s = v.as_string()->data;
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b != std::string::npos) {
        std::string t = s.substr(b, e - b + 1);
        char* end = nullptr;
        double d = std::strtod(t.c_str(), &end);
        if (end && *end == '\0') return d;
    }
    throw jdError(ErrCode::TYPE_MISMATCH,
        std::string(fn) + ": \"" + s + "\" is not a number");
}

void VM::register_data_builtins() {
    register_native("CDBL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(conversion_arg(args[0], "CDBL"));
    });
    register_native("CINT", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(static_cast<int32_t>(conversion_arg(args[0], "CINT")));
    });
    register_native("CLNG", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(static_cast<int64_t>(conversion_arg(args[0], "CLNG")));
    });
    register_native("CSNG", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(static_cast<double>(static_cast<float>(conversion_arg(args[0], "CSNG"))));
    });
    register_native("CBOOL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].to_bool());
    });
    register_native("IIF", 3, 3, [](const std::vector<Value>& args) -> Value {
        // Vectorized: IIF(cond_array, true_val, false_val)
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            auto* conds = args[0].as_array();
            bool t_arr = (args[1].type == ValueType::ARRAY);
            bool f_arr = (args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < conds->elements.size(); i++) {
                if (conds->elements[i].to_bool()) {
                    r.as_array()->elements.push_back(t_arr ? args[1].as_array()->elements[i % args[1].as_array()->elements.size()] : args[1]);
                } else {
                    r.as_array()->elements.push_back(f_arr ? args[2].as_array()->elements[i % args[2].as_array()->elements.size()] : args[2]);
                }
            }
            return r;
        }
        return args[0].to_bool() ? args[1] : args[2];
    });

    // Type checking
    auto typeof_one = [](const Value& v) -> Value {
        if (v.type == ValueType::FLOAT64 && v.subtype == ValueSubtype::DATE)
            return Value::make_string("DATE");
        switch (v.type) {
            case ValueType::NONE:    return Value::make_string("NONE");
            case ValueType::BOOLEAN: return Value::make_string("BOOLEAN");
            case ValueType::BYTE:    return Value::make_string("BYTE");
            case ValueType::INT16:   return Value::make_string("INT16");
            case ValueType::INT32:   return Value::make_string("INT32");
            case ValueType::INT64:   return Value::make_string("INT64");
            case ValueType::FLOAT16: return Value::make_string("FLOAT16");
            case ValueType::FLOAT32: return Value::make_string("FLOAT32");
            case ValueType::FLOAT64: return Value::make_string("FLOAT64");
            case ValueType::STRING:  return Value::make_string("STRING");
            case ValueType::OBJECT:  return Value::make_string("OBJECT");
            case ValueType::TENSOR:  return Value::make_string("TENSOR");
            case ValueType::ARRAY:   return Value::make_string("ARRAY");
        }
        return Value::make_string("UNKNOWN");
    };
    register_native("TYPEOF", 1, -1, [typeof_one](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) return typeof_one(args[0]);
        // Multiple args: return array of type strings
        Value result = Value::make_array();
        for (auto& a : args) result.as_array()->elements.push_back(typeof_one(a));
        return result;
    });

    // ── MAP functions ────────────────────────────────────────

    register_native("MAP.EXISTS", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        std::string key = args[1].as_string()->data;
        return Value::make_bool(o->get(key) != nullptr);
    });

    register_native("MAP.KEYS", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        Value r = Value::make_array();
        for (auto& [k, v] : o->fields) r.as_array()->elements.push_back(Value::make_string(k));
        return r;
    });

    register_native("MAP.VALUES", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        Value r = Value::make_array();
        for (auto& [k, v] : o->fields) r.as_array()->elements.push_back(v);
        return r;
    });

    register_native("MAP.ITEMS", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        Value r = Value::make_array();
        for (auto& [k, v] : o->fields) {
            Value pair = Value::make_array();
            pair.as_array()->elements.push_back(Value::make_string(k));
            pair.as_array()->elements.push_back(v);
            r.as_array()->elements.push_back(std::move(pair));
        }
        return r;
    });

    register_native("MAP.SIZE", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(args[0].as_object()->fields.size());
    });

    register_native("MAP.DELETE", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        std::string key = args[1].as_string()->data;
        auto& f = o->fields;
        f.erase(std::remove_if(f.begin(), f.end(), [&](auto& p) { return p.first == key; }), f.end());
        return Value::make_none();
    });

    register_native("MAP.CLEAR", [](const std::vector<Value>& args) -> Value {
        args[0].as_object()->fields.clear();
        return Value::make_none();
    });

    register_native("MAP.MERGE", [](const std::vector<Value>& args) -> Value {
        auto* dst = args[0].as_object();
        auto* src = args[1].as_object();
        for (auto& [k, v] : src->fields) dst->set(k, v);
        return Value::make_none();
    });

    register_native("MAP.FROM", [](const std::vector<Value>& args) -> Value {
        // Simple JSON object parser: {"key":"val", "key2": 123}
        std::string s = args[0].as_string()->data;
        Value m = Value::make_object();
        auto* o = m.as_object();
        size_t i = s.find('{');
        if (i == std::string::npos) return m;
        i++;
        auto skip_ws = [&]() { while (i < s.size() && std::isspace(s[i])) i++; };
        auto parse_str = [&]() -> std::string {
            skip_ws();
            if (i >= s.size() || s[i] != '"') return "";
            i++; // "
            std::string r;
            while (i < s.size() && s[i] != '"') { if (s[i] == '\\' && i+1 < s.size()) { i++; } r += s[i++]; }
            if (i < s.size()) i++; // closing "
            return r;
        };
        while (i < s.size()) {
            skip_ws();
            if (s[i] == '}') break;
            if (s[i] == ',') { i++; continue; }
            std::string key = parse_str();
            skip_ws();
            if (i < s.size() && s[i] == ':') i++;
            skip_ws();
            // Parse value
            if (i < s.size() && s[i] == '"') {
                o->set(key, Value::make_string(parse_str()));
            } else if (i < s.size() && (s[i] == 't' || s[i] == 'f')) {
                bool val = (s[i] == 't');
                while (i < s.size() && std::isalpha(s[i])) i++;
                o->set(key, Value::make_bool(val));
            } else if (i < s.size() && s[i] == 'n') {
                while (i < s.size() && std::isalpha(s[i])) i++;
                o->set(key, Value::make_none());
            } else {
                // Number
                size_t start = i;
                bool is_float = false;
                if (i < s.size() && s[i] == '-') i++;
                while (i < s.size() && (std::isdigit(s[i]) || s[i] == '.')) {
                    if (s[i] == '.') is_float = true;
                    i++;
                }
                std::string ns = s.substr(start, i - start);
                if (is_float) o->set(key, Value::make_f64(std::stod(ns)));
                else o->set(key, Value::make_i64(std::stoll(ns)));
            }
        }
        return m;
    });

    // ── JSON functions ───────────────────────────────────────

    // JSON.PARSE and JSON.PARSE$ are the same function. The $ says "answers a
    // string" everywhere else in the language, and this one answers a map or an
    // array, so the plain name is the honest one; the $ form stays for the code
    // that already uses it.
    auto json_parse = [](const std::vector<Value>& args) -> Value {
        // Reuse MAP.FROM for objects, also handle arrays
        std::string s = args[0].as_string()->data;
        size_t p = 0;
        // Skip UTF-8 BOM (EF BB BF). Files written by VBA's ADODB.Stream,
        // .NET StreamWriter, Notepad-on-save etc. all emit a BOM by default.
        // Without this skip the first byte (0xEF) falls through the parser's
        // if-chain and the "number" branch trips stoll on an empty string.
        if (s.size() >= 3 &&
            (unsigned char)s[0] == 0xEF &&
            (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF) {
            p = 3;
        }
        auto skip_ws = [&]() { while (p < s.size() && std::isspace((unsigned char)s[p])) p++; };

        // A JSON string from the opening quote through the closing one. Object
        // keys read through this too: they are strings and carry the same
        // escapes, and a key holding a quote ends where the escape says, not
        // at the first quote byte.
        auto parse_string = [&]() -> std::string {
            p++; std::string r;
            while (p < s.size() && s[p] != '"') {
                if (s[p] == '\\' && p+1 < s.size()) {
                    p++;
                    switch(s[p]) {
                        case 'n': r += '\n'; break;
                        case 't': r += '\t'; break;
                        case 'r': r += '\r'; break;
                        case 'b': r += '\b'; break;
                        case 'f': r += '\f'; break;
                        case '/': r += '/'; break;
                        case '\\': r += '\\'; break;
                        case '"': r += '"'; break;
                        case 'u': {
                            // \uXXXX → UTF-8
                            if (p + 4 < s.size()) {
                                std::string hex = s.substr(p+1, 4);
                                unsigned int cp = 0;
                                for (char h : hex) {
                                    cp <<= 4;
                                    if (h >= '0' && h <= '9') cp |= (h - '0');
                                    else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                                    else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                                }
                                p += 4;
                                if (cp < 0x80) { r += (char)cp; }
                                else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
                                else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
                            } else { r += s[p]; }
                            break;
                        }
                        default: r += s[p];
                    }
                }
                else r += s[p];
                p++;
            }
            if (p < s.size()) p++;
            return r;
        };

        std::function<Value()> parse_value = [&]() -> Value {
            skip_ws();
            if (p >= s.size()) return Value::make_none();
            char c = s[p];
            if (c == '"') return Value::make_string(parse_string());
            if (c == '{') {
                p++;
                Value m = Value::make_object();
                auto* o = m.as_object();
                skip_ws();
                if (p < s.size() && s[p] == '}') { p++; return m; }
                while (p < s.size()) {
                    skip_ws();
                    if (s[p] != '"') break;
                    std::string key = parse_string();
                    skip_ws(); if (p < s.size() && s[p] == ':') p++;
                    o->set(key, parse_value());
                    skip_ws(); if (p < s.size() && s[p] == ',') p++; else break;
                }
                skip_ws(); if (p < s.size() && s[p] == '}') p++;
                return m;
            }
            if (c == '[') {
                p++;
                Value a = Value::make_array();
                skip_ws();
                if (p < s.size() && s[p] == ']') { p++; return a; }
                while (p < s.size()) {
                    a.as_array()->elements.push_back(parse_value());
                    skip_ws(); if (p < s.size() && s[p] == ',') p++; else break;
                }
                skip_ws(); if (p < s.size() && s[p] == ']') p++;
                return a;
            }
            if (c == 't') { p += 4; return Value::make_bool(true); }
            if (c == 'f') { p += 5; return Value::make_bool(false); }
            if (c == 'n') { p += 4; return Value::make_none(); }
            // Number
            size_t start = p; bool is_f = false;
            if (p < s.size() && s[p] == '-') p++;
            while (p < s.size() && (std::isdigit(s[p]) || s[p] == '.' || s[p] == 'e' || s[p] == 'E' || s[p] == '+' || s[p] == '-')) {
                if (s[p] == '.' || s[p] == 'e' || s[p] == 'E') is_f = true;
                p++;
            }
            std::string ns = s.substr(start, p - start);
            if (ns.empty() || ns == "-") {
                throw jdError(ErrCode::RUNTIME_ERROR,
                    "JSON.PARSE$: unexpected character '" +
                    std::string(1, s[start]) + "' at offset " +
                    std::to_string(start));
            }
            try {
                if (is_f) return Value::make_f64(std::stod(ns));
                return Value::make_i64(std::stoll(ns));
            } catch (const std::exception&) {
                throw jdError(ErrCode::RUNTIME_ERROR,
                    "JSON.PARSE$: bad number '" + ns + "' at offset " +
                    std::to_string(start));
            }
        };

        return parse_value();
    };
    register_native("JSON.PARSE", json_parse);
    register_native("JSON.PARSE$", json_parse);

    register_native("JSON.STRINGIFY$", [](const std::vector<Value>& args) -> Value {
        // RFC 8259-compliant string escape: " \ and the C0 control range
        // must be escaped, otherwise the output is not valid JSON.
        auto escape_string = [](const std::string& s) -> std::string {
            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('"');
            for (unsigned char c : s) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (c < 0x20) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "\\u%04x", c);
                            out += buf;
                        } else {
                            // Pass through UTF-8 bytes >= 0x20 unchanged.
                            out.push_back((char)c);
                        }
                }
            }
            out.push_back('"');
            return out;
        };
        std::function<std::string(const Value&)> to_json = [&](const Value& v) -> std::string {
            switch (v.type) {
                case ValueType::NONE: return "null";
                case ValueType::BOOLEAN: return v.boolean ? "true" : "false";
                case ValueType::STRING: return escape_string(v.as_string()->data);
                // A date prints as a timestamp, which is text, so it is
                // quoted like text. Written bare it is not JSON at all.
                case ValueType::FLOAT64:
                    if (v.subtype == ValueSubtype::DATE)
                        return escape_string(v.to_string());
                    return v.to_string();
                case ValueType::ARRAY: {
                    std::string r = "[";
                    auto* a = v.as_array();
                    for (size_t i = 0; i < a->elements.size(); i++) {
                        if (i > 0) r += ",";
                        r += to_json(a->elements[i]);
                    }
                    return r + "]";
                }
                case ValueType::OBJECT: {
                    std::string r = "{";
                    auto* o = v.as_object();
                    for (size_t i = 0; i < o->fields.size(); i++) {
                        if (i > 0) r += ",";
                        r += escape_string(o->fields[i].first) + ":" + to_json(o->fields[i].second);
                    }
                    return r + "}";
                }
                default: return v.to_string();
            }
        };
        return Value::make_string(to_json(args[0]));
    });

    // ── 5. Type Checking ─────────────────────────────────────

    register_native("ISNUM", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(is_numeric(args[0].type));
    });
    register_native("ISSTR", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::STRING);
    });
    register_native("ISARR", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::ARRAY);
    });
    register_native("ISMAP", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::OBJECT);
    });
    register_native("ISBOOL", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::BOOLEAN);
    });
    register_native("ISNONE", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::NONE);
    });
    register_native("ISNULL", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::NONE);
    });
    register_native("TONUM", [](const std::vector<Value>& args) -> Value {
        // Mirror VAL: strings go through stod so "1.5" → 1.5, not 0.
        if (args[0].type == ValueType::STRING) {
            try { return Value::make_f64(std::stod(args[0].as_string()->data)); }
            catch (...) { return Value::make_f64(0); }
        }
        return Value::make_f64(args[0].to_double());
    });
    register_native("TOSTR", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("CSTR", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
}
