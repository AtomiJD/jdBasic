// VM builtins: clock, dates and calendar arithmetic. The calendar itself is
// jdb_calendar.h, shared with the native runtime; a date here is a FLOAT64
// Value of epoch seconds with the DATE subtype.

#include "vm_internal.h"
#include "jdb_calendar.h"

using jdb_cal::Civil;

double jdb_date_wall_seconds(double epoch) {
    return jdb_cal::wall_seconds(epoch);
}

// Arithmetic where at least one side is a date. Answers false when the
// operands are plain numbers and the caller should do the ordinary thing.
//
// A date is an instant, so the difference of two of them is elapsed seconds,
// the same number DATEDIFF("S", ...) answers. What that difference is not is
// a date: it is a duration, and it carries no DATE subtype. Nor does the
// result of an operator for which a date means nothing.
bool jdb_date_arith(const Value& a, const Value& b, OpCode op, Value& out) {
    const bool a_date = a.type == ValueType::FLOAT64 && a.subtype == ValueSubtype::DATE;
    const bool b_date = b.type == ValueType::FLOAT64 && b.subtype == ValueSubtype::DATE;
    if (!a_date && !b_date) return false;

    if (op == OpCode::SUB && a_date && b_date) {
        out = Value::make_f64(a.to_double() - b.to_double());
        return true;
    }
    if (op == OpCode::SUB && a_date) {
        out = Value::make_date(a.to_double() - b.to_double());
        return true;
    }
    if (op == OpCode::ADD && a_date != b_date) {
        out = Value::make_date(a.to_double() + b.to_double());
        return true;
    }
    return false;
}

static auto g_program_start = std::chrono::steady_clock::now();

static double now_epoch() {
    auto t = std::chrono::system_clock::now();
    return std::chrono::duration<double>(t.time_since_epoch()).count();
}

// A Value as an instant: a date or a number is its epoch seconds; an ISO
// timestamp, the form the native runtime stores dates in and hands over
// the bridge, is read as a local wall clock.
static double value_to_epoch(const Value& v) {
    if (v.type == ValueType::STRING) {
        Civil c;
        if (!jdb_cal::parse_iso(v.as_string()->data.c_str(), c)) return 0.0;
        return jdb_cal::local_to_epoch(c);
    }
    return v.to_double();
}

static bool is_numeric(const Value& v) {
    return v.type == ValueType::INT64 || v.type == ValueType::INT32 ||
           v.type == ValueType::INT16 || v.type == ValueType::BYTE ||
           v.type == ValueType::FLOAT64 || v.type == ValueType::FLOAT32 ||
           v.type == ValueType::FLOAT16 || v.type == ValueType::BOOLEAN;
}

void VM::register_datetime_builtins() {
    register_native("TICK", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - g_program_start).count();
        return Value::make_f64(ms);
    });

    // ── Date / Time ──────────────────────────────────────────

    register_native("DATE$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        char buf[32];
        jdb_cal::format_iso(buf, sizeof(buf), jdb_cal::civil_local(now_epoch()), false);
        return Value::make_string(buf);
    });

    register_native("TIME$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        char buf[32];
        jdb_cal::format_time(buf, sizeof(buf), jdb_cal::civil_local(now_epoch()));
        return Value::make_string(buf);
    });

    register_native("NOW", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_date(now_epoch());
    });

    register_native("NOW_EPOCH", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_f64(now_epoch());
    });

    register_native("CVDATE", 1, 2, [](const std::vector<Value>& args) -> Value {
        // CVDATE(text, [tz_hours]): with tz_hours the text is a wall clock in
        // the zone that many hours east of UTC, otherwise in the machine's.
        bool have_tz = args.size() >= 2;
        double tz_sec = have_tz ? args[1].to_double() * 3600.0 : 0.0;
        std::function<Value(const Value&)> one = [&](const Value& v) -> Value {
            if (is_numeric(v)) return Value::make_date(v.to_double());
            if (v.type == ValueType::ARRAY) {
                Value arr = Value::make_array();
                auto* out = arr.as_array();
                for (auto& e : v.as_array()->elements)
                    out->elements.push_back(one(e));
                return arr;
            }
            const std::string& s = v.as_string()->data;
            Civil c;
            if (!jdb_cal::parse_text(s.c_str(), c))
                throw std::runtime_error("CDATE: \"" + s +
                    "\" is not a date; write YYYY-MM-DD or DD.MM.YYYY, with HH:MM:SS after a space if needed");
            if (have_tz) return Value::make_date(jdb_cal::utc_to_epoch(c) - tz_sec);
            return Value::make_date(jdb_cal::local_to_epoch(c));
        };
        return one(args[0]);
    });
    // CDATE is CVDATE under a second name, arity and all.
    {
        const NativeEntry* cv = native_find("CVDATE");
        register_native("CDATE", cv->min_args, cv->max_args, cv->fn);
    }

    register_native("DATEADD", 3, 4, [](const std::vector<Value>& args) -> Value {
        // DATEADD(part$, num, date, [tz]). TZ is accepted for API symmetry
        // and unused.
        const std::string& part = args[0].as_string()->data;
        char u = jdb_cal::unit(part.c_str(), "YMWDHNS");
        if (!u) throw jdError(ErrCode::WRONG_ARG_TYPE,
            "DATEADD: unknown unit \"" + part + "\"; use Y, M, W, D, H, N or S");
        return Value::make_date(jdb_cal::date_add(u, args[1].to_double(), value_to_epoch(args[2])));
    });

    register_native("DATEDIFF", 3, 4, [](const std::vector<Value>& args) -> Value {
        // DATEDIFF(part$, date1, date2, [tz]).
        const std::string& part = args[0].as_string()->data;
        char u = jdb_cal::unit(part.c_str(), "DHNS");
        if (!u) throw jdError(ErrCode::WRONG_ARG_TYPE,
            "DATEDIFF: unknown unit \"" + part + "\"; use D, H, N or S");
        return Value::make_f64(jdb_cal::date_diff(u, value_to_epoch(args[1]), value_to_epoch(args[2])));
    });

    register_native("FORMAT_DATE", 1, 3, [](const std::vector<Value>& args) -> Value {
        // FORMAT_DATE(date, [fmt], [tz_hours]): the wall clock in the
        // machine's zone, or in the zone tz_hours east of UTC.
        double epoch = value_to_epoch(args[0]);
        std::string fmt = args.size() >= 2 ? args[1].as_string()->data : "";
        bool shifted = args.size() >= 3;
        char buf[256];
        if (jdb_cal::format_at(buf, sizeof(buf), fmt.c_str(), epoch, shifted,
                               shifted ? args[2].to_double() : 0.0) < 0)
            throw jdError(ErrCode::WRONG_ARG_TYPE,
                "FORMAT_DATE: the format has an unknown conversion, or the year cannot be shown");
        return Value::make_string(buf);
    });

    register_native("DATE.UTC", 3, 6, [](const std::vector<Value>& args) -> Value {
        // DATE.UTC(year, month, day, [hour, minute, second]): the instant of
        // that UTC wall clock.
        int64_t h  = args.size() >= 4 ? args[3].to_int() : 0;
        int64_t mi = args.size() >= 5 ? args[4].to_int() : 0;
        int64_t se = args.size() >= 6 ? args[5].to_int() : 0;
        return Value::make_date(jdb_cal::utc_to_epoch(args[0].to_int(), args[1].to_int(),
                                                      args[2].to_int(), h, mi, se));
    });

    register_native("DATE.PARTS", 1, 2, [](const std::vector<Value>& args) -> Value {
        // DATE.PARTS(date, [tz_hours]): a map of year, month, day, hour,
        // minute, second, weekday and yday, in the machine's zone or the
        // zone tz_hours east of UTC.
        bool shifted = args.size() >= 2;
        Civil c = jdb_cal::civil_at(value_to_epoch(args[0]), shifted,
                                    shifted ? args[1].to_double() : 0.0);
        Value m = Value::make_object();
        auto* o = m.as_object();
        o->set("year",    Value::make_i64(c.y));
        o->set("month",   Value::make_i64(c.mo));
        o->set("day",     Value::make_i64(c.d));
        o->set("hour",    Value::make_i64(c.h));
        o->set("minute",  Value::make_i64(c.mi));
        o->set("second",  Value::make_i64(c.se));
        o->set("weekday", Value::make_i64(c.wd));
        o->set("yday",    Value::make_i64(jdb_cal::year_day(c)));
        return m;
    });

    register_native("EOMONTH", 1, 2, [](const std::vector<Value>& args) -> Value {
        // EOMONTH(date, [offset_months]): midnight, local time, of the last
        // day of the month offset_months away; element-wise over an array.
        int64_t offset = args.size() >= 2 ? args[1].to_int() : 0;
        std::function<Value(const Value&)> one = [&](const Value& v) -> Value {
            if (v.type == ValueType::ARRAY) {
                Value arr = Value::make_array();
                for (auto& e : v.as_array()->elements)
                    arr.as_array()->elements.push_back(one(e));
                return arr;
            }
            return Value::make_date(jdb_cal::end_of_month(value_to_epoch(v), offset));
        };
        return one(args[0]);
    });

    register_native("DATERANGE", 2, 4, [](const std::vector<Value>& args) -> Value {
        // DATERANGE(start, end, [unit$="D"], [step=1]): the dates from start
        // to end inclusive, step units apart. Calendar units (D, W, M, Y)
        // move the local calendar from the start date each time, so a start
        // on the 31st is clamped in the short months and comes back on the
        // months that have a 31st; clock units (H, N, S) add elapsed time.
        double start = value_to_epoch(args[0]);
        double end   = value_to_epoch(args[1]);
        std::string unit_text = args.size() >= 3 ? args[2].as_string()->data : "D";
        for (auto& c : unit_text) c = (char)std::toupper((unsigned char)c);
        long step = args.size() >= 4 ? (long)args[3].to_int() : 1;
        if (step == 0) step = 1;
        Value r = Value::make_array();
        auto* out = r.as_array();
        if (unit_text == "D" || unit_text == "W" || unit_text == "M" || unit_text == "Y") {
            int64_t months_per_step = 0;
            int64_t days_per_step = 0;
            if (unit_text == "D")      days_per_step = step;
            else if (unit_text == "W") days_per_step = 7 * step;
            else if (unit_text == "M") months_per_step = step;
            else                       months_per_step = 12 * step;
            for (int64_t n = 0; ; n++) {
                double e = months_per_step != 0
                    ? jdb_cal::add_months(start, months_per_step * n)
                    : jdb_cal::add_days(start, (double)(days_per_step * n));
                if ((step > 0 && e > end) || (step < 0 && e < end)) break;
                out->elements.push_back(Value::make_date(e));
                if (out->elements.size() > 5000000) break;
            }
        } else {
            double sec = 3600.0;
            if (unit_text == "N")      sec = 60.0;
            else if (unit_text == "S") sec = 1.0;
            double inc = sec * step;
            if (inc > 0) for (double e = start; e <= end + 0.5; e += inc) out->elements.push_back(Value::make_date(e));
            else         for (double e = start; e >= end - 0.5; e += inc) out->elements.push_back(Value::make_date(e));
        }
        return r;
    });

    // ── The parts of a date's local wall clock ───────────────

    register_native("YEAR", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).y);
    });
    register_native("MONTH", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).mo);
    });
    register_native("DAY", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).d);
    });
    register_native("HOUR", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).h);
    });
    register_native("MINUTE", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).mi);
    });
    register_native("SECOND", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).se);
    });
    register_native("WEEKDAY", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(jdb_cal::civil_local(args[0].to_double()).wd);
    });
}
