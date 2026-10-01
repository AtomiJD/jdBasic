// VM builtins: clock, dates and calendar arithmetic.

#include "vm_internal.h"

// ── Civil calendar arithmetic (Howard Hinnant's algorithms) ──────
// Proleptic Gregorian, free of CRT range limits: MSVC's mktime/localtime
// reject pre-1970 dates (mktime returns -1, localtime returns nullptr),
// so date parsing and Y/M/D extraction fall back to these.

static void jdb_civil_from_days(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = yy + (m <= 2);
}

// Seconds east of UTC for a civil date in the machine's own zone. The CRT
// cannot answer for a year outside its range, so the question is asked about
// the same date moved by whole 400 year cycles, which the Gregorian calendar
// repeats exactly. The daylight rules of that later year are the best answer
// available for a date the rules never covered.
static int64_t jdb_utc_offset_at(int64_t y, int64_t mo, int64_t d,
                                 int64_t h, int64_t mi, int64_t se) {
    // The rules are asked about a year the CRT does cover, of the same
    // leapness so a 29 February stays a real date. Daylight rules are
    // political and did not exist for most of the years this reaches, so
    // the ones in force now are the only answer available.
    int64_t py = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 2000 : 2001;
    std::tm probe{};
    probe.tm_year = (int)(py - 1900);
    probe.tm_mon  = (int)(mo - 1);
    probe.tm_mday = (int)d;
    probe.tm_hour = (int)h;
    probe.tm_min  = (int)mi;
    probe.tm_sec  = (int)se;
    probe.tm_isdst = -1;
    std::time_t t = std::mktime(&probe);
    if (t == (std::time_t)-1) return 0;
    int64_t as_utc = jdb_days_from_civil(py, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return as_utc - (int64_t)t;
}

// A local wall clock as an epoch, for any year.
static double jdb_local_civil_to_epoch(int64_t y, int64_t mo, int64_t d,
                                       int64_t h, int64_t mi, int64_t se) {
    int64_t as_utc = jdb_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return (double)(as_utc - jdb_utc_offset_at(y, mo, d, h, mi, se));
}

// The same from a tm, which is how the parsers hold it.
static double jdb_local_tm_to_epoch(const std::tm& tm) {
    return jdb_local_civil_to_epoch(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                    tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// How many days the month has.
static int jdb_days_in_month(int64_t y, int64_t m) {
    static const int len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    if (m < 1 || m > 12) return 30;
    return len[m - 1];
}

// Split an epoch into UTC civil components. Only used when the CRT's
// localtime cannot represent the value (negative epochs on Windows).
static void jdb_epoch_to_civil_utc(double epoch, int64_t& y, int64_t& mo, int64_t& d,
                                   int64_t& h, int64_t& mi, int64_t& se, int64_t& wd) {
    int64_t t = (int64_t)std::floor(epoch);
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    h = rem / 3600; mi = (rem % 3600) / 60; se = rem % 60;
    jdb_civil_from_days(days, y, mo, d);
    wd = (days + 4) % 7;           // epoch day 0 = Thursday
    if (wd < 0) wd += 7;
}

// The local wall clock of an instant, as civil components.
static void jdb_epoch_to_civil_local(double epoch, int64_t& y, int64_t& mo, int64_t& d,
                                     int64_t& h, int64_t& mi, int64_t& se, int64_t& wd) {
    jdb_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    int64_t off = jdb_utc_offset_at(y, mo, d, h, mi, se);
    jdb_epoch_to_civil_utc(epoch + (double)off, y, mo, d, h, mi, se, wd);
}

// An instant moved by whole calendar months, the day of the month kept
// where the target month is long enough and clamped to its last day where
// it is not. Anchored on the date given, so a run of steps cannot drift.
static double jdb_add_months_local(double epoch, int64_t months) {
    int64_t y, mo, d, h, mi, se, wd;
    jdb_epoch_to_civil_local(epoch, y, mo, d, h, mi, se, wd);
    int64_t index = y * 12 + (mo - 1) + months;
    int64_t ny = index >= 0 ? index / 12 : -((-index + 11) / 12);
    int64_t nm = index - ny * 12 + 1;
    int64_t nd = d;
    int64_t last = jdb_days_in_month(ny, nm);
    if (nd > last) nd = last;
    return jdb_local_civil_to_epoch(ny, nm, nd, h, mi, se);
}

static auto g_program_start = std::chrono::steady_clock::now();

void VM::register_datetime_builtins() {
    register_native("TICK", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - g_program_start).count();
        return Value::make_f64(ms);
    });

    // ── Date / Time ──────────────────────────────────────────
    // DateTime is stored as f64 = seconds since Unix epoch

    register_native("DATE$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::time(nullptr);
        auto* tm = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        return Value::make_string(buf);
    });

    register_native("TIME$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::time(nullptr);
        auto* tm = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
        return Value::make_string(buf);
    });

    register_native("NOW", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::chrono::system_clock::now();
        double epoch = std::chrono::duration<double>(t.time_since_epoch()).count();
        return Value::make_date(epoch);
    });

    register_native("NOW_EPOCH", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::chrono::system_clock::now();
        double epoch = std::chrono::duration<double>(t.time_since_epoch()).count();
        return Value::make_f64(epoch);
    });

    // Helper: portable UTC tm → epoch (seconds since 1970-01-01 UTC).
    auto tm_to_utc_epoch = [](std::tm tm) -> double {
        // Computed from the calendar rather than through the CRT, whose
        // range starts at 1970 and whose answer for anything earlier is -1.
        return (double)(jdb_days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400
                        + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
    };

    // Coerce a Value to an epoch double. Accepts DATE-tagged FLOAT64 (used by
    // interpreter), ordinary numbers (treated as epoch seconds), and ISO
    // strings like "YYYY-MM-DD[ HH:MM:SS]" - the native runtime stores dates
    // as ISO strings, so VM-bridged calls see string inputs here.
    auto value_to_epoch = [](const Value& v) -> double {
        if (v.type == ValueType::STRING) {
            const std::string& s = v.as_string()->data;
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
            int matched = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d",
                                      &y, &mo, &d, &h, &mi, &se);
            if (matched < 3) {
                matched = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d",
                                      &y, &mo, &d, &h, &mi, &se);
            }
            if (matched < 3) return 0.0;
            return jdb_local_civil_to_epoch(y, mo, d, h, mi, se);
        }
        return v.to_double();
    };

    register_native("CVDATE", 1, 2, [tm_to_utc_epoch](const std::vector<Value>& args) -> Value {
        // Optional tz_hours: when provided, strings are parsed as being in UTC+tz.
        bool have_tz = (args.size() >= 2);
        double tz_sec = have_tz ? args[1].to_double() * 3600.0 : 0.0;
        std::function<Value(const Value&)> one = [&](const Value& v) -> Value {
            // Numeric input → treat as Unix epoch seconds (tz irrelevant).
            if (v.type == ValueType::INT64 || v.type == ValueType::INT32 ||
                v.type == ValueType::INT16 || v.type == ValueType::BYTE ||
                v.type == ValueType::FLOAT64 || v.type == ValueType::FLOAT32 ||
                v.type == ValueType::FLOAT16 || v.type == ValueType::BOOLEAN) {
                return Value::make_date(v.to_double());
            }
            // Array input → vectorize element-wise.
            if (v.type == ValueType::ARRAY) {
                Value arr = Value::make_array();
                auto* out = arr.as_array();
                for (auto& e : v.as_array()->elements)
                    out->elements.push_back(one(e));
                return arr;
            }
            // String input → parse ISO "YYYY-MM-DD[ HH:MM:SS]".
            std::string s = v.as_string()->data;
            std::tm tm = {};
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
            int matched = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d",
                                      &y, &mo, &d, &h, &mi, &se);
            if (matched < 3) {
                matched = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d",
                                      &y, &mo, &d, &h, &mi, &se);
            }
            if (matched < 3) return Value::make_date(0);
            tm.tm_year = y - 1900;
            tm.tm_mon  = mo - 1;
            tm.tm_mday = d;
            tm.tm_hour = h;
            tm.tm_min  = mi;
            tm.tm_sec  = se;
            if (have_tz) {
                // Treat parsed components as being in UTC+tz → convert to real UTC epoch.
                return Value::make_date(tm_to_utc_epoch(tm) - tz_sec);
            }
            return Value::make_date(jdb_local_tm_to_epoch(tm));
        };
        return one(args[0]);
    });
    // CDATE is CVDATE under a second name, arity and all.
    {
        const NativeEntry* cv = native_find("CVDATE");
        register_native("CDATE", cv->min_args, cv->max_args, cv->fn);
    }

    register_native("DATEADD", 3, 4, [value_to_epoch](const std::vector<Value>& args) -> Value {
        // DATEADD(part$, num, date_epoch, [tz]). TZ accepted for API symmetry
        // but D/H/N/S arithmetic is TZ-invariant on epoch, so it's unused here.
        std::string part = args[0].as_string()->data;
        double num = args[1].to_double();
        double epoch = value_to_epoch(args[2]);
        for (auto& c : part) c = (char)std::toupper((unsigned char)c);
        if (part == "D")      epoch += num * 86400;
        else if (part == "H") epoch += num * 3600;
        else if (part == "N") epoch += num * 60;
        else if (part == "S") epoch += num;
        else if (part == "W") epoch += num * 604800;
        else if (part == "M") epoch = jdb_add_months_local(epoch, (int64_t)num);
        else if (part == "Y") epoch = jdb_add_months_local(epoch, (int64_t)num * 12);
        return Value::make_date(epoch);
    });

    register_native("DATEDIFF", 3, 4, [value_to_epoch](const std::vector<Value>& args) -> Value {
        // DATEDIFF(part$, date1, date2, [tz]): D counts days on the local wall
        // clock, H/N/S count elapsed time.
        std::string part = args[0].as_string()->data;
        double d1 = value_to_epoch(args[1]);
        double d2 = value_to_epoch(args[2]);
        double diff = d2 - d1;
        if (part == "D") {
            auto wall = [](double epoch) {
                int64_t y, mo, d, h, mi, se, wd;
                jdb_epoch_to_civil_local(epoch, y, mo, d, h, mi, se, wd);
                return (double)(jdb_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se) +
                       (epoch - std::floor(epoch));
            };
            return Value::make_f64((wall(d2) - wall(d1)) / 86400);
        }
        else if (part == "H") return Value::make_f64(diff / 3600);
        else if (part == "N") return Value::make_f64(diff / 60);
        else if (part == "S") return Value::make_f64(diff);
        return Value::make_f64(diff);
    });

    register_native("FORMAT_DATE", 1, 3, [value_to_epoch](const std::vector<Value>& args) -> Value {
        // FORMAT_DATE(epoch, [fmt], [tz_hours]) - when tz given, format the
        // instant in UTC+tz instead of local time.
        double epoch = value_to_epoch(args[0]);
        std::string fmt = (args.size() >= 2) ? args[1].as_string()->data : "%Y-%m-%d %H:%M:%S";
        // Zero-init: if the (out-of-range) conversion below fails it leaves tm
        // untouched, and a garbage tm fed to strftime trips MSVC's invalid-
        // parameter handler -> __fastfail (an uncatchable crash). So we both
        // zero-init AND check the converter's result, returning "" on failure.
        // Filled from the calendar rather than through the CRT, which
        // answers nothing for a year before 1970 and leaves a garbage tm
        // that strftime turns into an uncatchable crash on MSVC.
        std::tm tm{};
        {
            int64_t y, mo, d, h, mi, se, wd;
            if (args.size() >= 3) {
                jdb_epoch_to_civil_utc(epoch + args[2].to_double() * 3600.0,
                                       y, mo, d, h, mi, se, wd);
            } else {
                jdb_epoch_to_civil_local(epoch, y, mo, d, h, mi, se, wd);
            }
            tm.tm_year = (int)(y - 1900);
            tm.tm_mon  = (int)(mo - 1);
            tm.tm_mday = (int)d;
            tm.tm_hour = (int)h;
            tm.tm_min  = (int)mi;
            tm.tm_sec  = (int)se;
            tm.tm_wday = (int)wd;
            tm.tm_yday = (int)(jdb_days_from_civil(y, mo, d) - jdb_days_from_civil(y, 1, 1));
            tm.tm_isdst = 0;
        }
        char buf[128];
        buf[0] = '\0';
        std::strftime(buf, sizeof(buf), fmt.c_str(), &tm);
        return Value::make_string(buf);
    });

    register_native("DATE.UTC", 3, 6, [tm_to_utc_epoch](const std::vector<Value>& args) -> Value {
        // DATE.UTC(year, month, day, [hour=0, minute=0, second=0]) → DATE epoch.
        std::tm tm = {};
        tm.tm_year = static_cast<int>(args[0].to_double()) - 1900;
        tm.tm_mon  = static_cast<int>(args[1].to_double()) - 1;
        tm.tm_mday = static_cast<int>(args[2].to_double());
        tm.tm_hour = (args.size() >= 4) ? static_cast<int>(args[3].to_double()) : 0;
        tm.tm_min  = (args.size() >= 5) ? static_cast<int>(args[4].to_double()) : 0;
        tm.tm_sec  = (args.size() >= 6) ? static_cast<int>(args[5].to_double()) : 0;
        return Value::make_date(tm_to_utc_epoch(tm));
    });

    register_native("DATE.PARTS", 1, 2, [value_to_epoch](const std::vector<Value>& args) -> Value {
        // DATE.PARTS(epoch, [tz_hours]) → map with year/month/day/hour/minute/
        // second/weekday/yday. When tz given, components are in UTC+tz; otherwise
        // they reflect local time.
        double epoch = value_to_epoch(args[0]);
        std::tm tm;
        if (args.size() >= 2) {
            double shifted = epoch + args[1].to_double() * 3600.0;
            std::time_t t = static_cast<std::time_t>(shifted);
        #ifdef _WIN32
            gmtime_s(&tm, &t);
        #else
            gmtime_r(&t, &tm);
        #endif
        } else {
            std::time_t t = static_cast<std::time_t>(epoch);
        #ifdef _WIN32
            localtime_s(&tm, &t);
        #else
            localtime_r(&t, &tm);
        #endif
        }
        Value m = Value::make_object();
        auto* o = m.as_object();
        o->set("year",    Value::make_i64(tm.tm_year + 1900));
        o->set("month",   Value::make_i64(tm.tm_mon + 1));
        o->set("day",     Value::make_i64(tm.tm_mday));
        o->set("hour",    Value::make_i64(tm.tm_hour));
        o->set("minute",  Value::make_i64(tm.tm_min));
        o->set("second",  Value::make_i64(tm.tm_sec));
        o->set("weekday", Value::make_i64(tm.tm_wday));
        o->set("yday",    Value::make_i64(tm.tm_yday));
        return m;
    });

    register_native("EOMONTH", 1, 2, [value_to_epoch](const std::vector<Value>& args) -> Value {
        // EOMONTH(date, [offset_months]) → DATE epoch of the last day (midnight,
        // local time) of the month `offset_months` away from `date`. Excel-style.
        // Vectorizes element-wise over a date array. `days-in-month(d)` =
        // DAY(EOMONTH(d)).
        int offset = (args.size() >= 2) ? (int)args[1].to_int() : 0;
        std::function<Value(const Value&)> one = [&](const Value& v) -> Value {
            if (v.type == ValueType::ARRAY) {
                Value arr = Value::make_array();
                for (auto& e : v.as_array()->elements)
                    arr.as_array()->elements.push_back(one(e));
                return arr;
            }
            int64_t y, mo, d, h, mi, se, wd;
            jdb_epoch_to_civil_local(value_to_epoch(v), y, mo, d, h, mi, se, wd);
            int64_t index = y * 12 + (mo - 1) + offset;
            int64_t ny = index >= 0 ? index / 12 : -((-index + 11) / 12);
            int64_t nm = index - ny * 12 + 1;
            return Value::make_date(jdb_local_civil_to_epoch(
                ny, nm, jdb_days_in_month(ny, nm), 0, 0, 0));
        };
        return one(args[0]);
    });

    register_native("DATERANGE", 2, 4, [value_to_epoch](const std::vector<Value>& args) -> Value {
        // DATERANGE(start, end, [unit$="D"], [step=1]) → array of DATE epochs from
        // start to end INCLUSIVE, stepping by `step` units. Calendar units D/W/M/Y
        // advance via tm (so a "day" stays a local calendar day across DST, never
        // drifts by an hour); clock units H/N/S advance by fixed epoch seconds.
        double start = value_to_epoch(args[0]);
        double end   = value_to_epoch(args[1]);
        std::string unit = (args.size() >= 3) ? args[2].as_string()->data : "D";
        for (auto& c : unit) c = (char)std::toupper((unsigned char)c);
        long step = (args.size() >= 4) ? (long)args[3].to_int() : 1;
        if (step == 0) step = 1;
        Value r = Value::make_array();
        auto* out = r.as_array();
        bool calendar = (unit == "D" || unit == "W" || unit == "M" || unit == "Y");
        if (calendar) {
            // Days and weeks move whole local days. Months and years are
            // counted from the start date each time rather than from the
            // one before, so a start of the 31st cannot walk forward
            // through the short months: it is clamped to each month's last
            // day and comes back on the months that have a 31st.
            int64_t months_per_step = 0;
            int64_t days_per_step = 0;
            if (unit == "D")      days_per_step = step;
            else if (unit == "W") days_per_step = 7 * step;
            else if (unit == "M") months_per_step = step;
            else                  months_per_step = 12 * step;
            for (int64_t n = 0; ; n++) {
                double e;
                if (months_per_step != 0) {
                    e = jdb_add_months_local(start, months_per_step * n);
                } else {
                    int64_t y, mo, d, h, mi, se, wd;
                    jdb_epoch_to_civil_local(start, y, mo, d, h, mi, se, wd);
                    int64_t day = jdb_days_from_civil(y, mo, d) + days_per_step * n;
                    int64_t ny, nmo, nd;
                    jdb_civil_from_days(day, ny, nmo, nd);
                    e = jdb_local_civil_to_epoch(ny, nmo, nd, h, mi, se);
                }
                if ((step > 0 && e > end) || (step < 0 && e < end)) break;
                out->elements.push_back(Value::make_date(e));
                if (out->elements.size() > 5000000) break;
            }
        } else {
            double sec = 3600.0;                 // H
            if (unit == "N")      sec = 60.0;
            else if (unit == "S") sec = 1.0;
            double inc = sec * step;
            if (inc > 0) for (double e = start; e <= end + 0.5; e += inc) out->elements.push_back(Value::make_date(e));
            else         for (double e = start; e >= end - 0.5; e += inc) out->elements.push_back(Value::make_date(e));
        }
        return r;
    });

    // ── 8. DateTime Extraction ───────────────────────────────

    // localtime returns nullptr for epochs outside the CRT's range
    // (pre-1970 on Windows) - fall back to UTC civil components then.
    register_native("YEAR", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_year + 1900);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(y);
    });
    register_native("MONTH", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_mon + 1);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(mo);
    });
    register_native("DAY", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_mday);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(d);
    });
    register_native("HOUR", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_hour);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(h);
    });
    register_native("MINUTE", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_min);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(mi);
    });
    register_native("SECOND", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_sec);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(se);
    });
    register_native("WEEKDAY", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        if (std::tm* p = std::localtime(&t)) return Value::make_i64(p->tm_wday);
        int64_t y, mo, d, h, mi, se, wd;
        jdb_epoch_to_civil_utc(args[0].to_double(), y, mo, d, h, mi, se, wd);
        return Value::make_i64(wd);
    });
}
