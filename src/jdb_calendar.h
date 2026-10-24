// jdb_calendar.h - the civil calendar both runtimes count dates on.
//
// A date is seconds since 1970-01-01 UTC, and its wall clock is read in the
// machine's own zone. Everything here is proleptic Gregorian (Howard
// Hinnant's algorithms) and free of the CRT's range: MSVC's mktime and
// localtime answer nothing for a year before 1970, so the conversions work
// from the calendar and ask the CRT only for the zone's rules. The VM
// (vm_builtins_datetime.cpp, value.h) and the native runtime
// (jdb_runtime.cpp) include this and keep only their own value plumbing.
#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace jdb_cal {

// The wall clock of an instant: year, month 1-12, day 1-31, hour, minute,
// second and the weekday 0-6 with 0 a Sunday.
struct Civil {
    int64_t y = 1970, mo = 1, d = 1, h = 0, mi = 0, se = 0, wd = 4;
};

inline bool is_leap(int64_t y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

// How many days the month has; 30 for a month out of range.
inline int days_in_month(int64_t y, int64_t m) {
    static const int len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2) return is_leap(y) ? 29 : 28;
    if (m < 1 || m > 12) return 30;
    return len[m - 1];
}

// Days since 1970-01-01 of a civil date.
inline int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// The civil date of a day count since 1970-01-01.
inline void civil_from_days(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
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

// The UTC wall clock of an instant.
inline Civil civil_utc(double epoch) {
    Civil c;
    int64_t t = (int64_t)std::floor(epoch);
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    c.h = rem / 3600; c.mi = (rem % 3600) / 60; c.se = rem % 60;
    civil_from_days(days, c.y, c.mo, c.d);
    c.wd = (days + 4) % 7;           // epoch day 0 is a Thursday
    if (c.wd < 0) c.wd += 7;
    return c;
}

// Seconds east of UTC for a wall clock in the machine's own zone. The CRT
// cannot answer for a year outside its range, so the question is asked
// about the same date moved by whole 400 year cycles, which the Gregorian
// calendar repeats exactly: the year keeps its leap day and its weekdays,
// so a rule such as "the second Sunday in March" falls on the same date.
// The daylight rules of that later year are the best answer available for
// a date the rules never covered.
inline int64_t utc_offset_at(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t se) {
    int64_t py = 2000 + (((y - 2000) % 400) + 400) % 400;
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
    int64_t as_utc = days_from_civil(py, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return as_utc - (int64_t)t;
}

// A local wall clock as an instant, for any year.
inline double local_to_epoch(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t se) {
    int64_t as_utc = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return (double)(as_utc - utc_offset_at(y, mo, d, h, mi, se));
}

inline double local_to_epoch(const Civil& c) {
    return local_to_epoch(c.y, c.mo, c.d, c.h, c.mi, c.se);
}

// The local wall clock of an instant.
inline Civil civil_local(double epoch) {
    // Within the CRT's range its own conversion knows every switch.
    if (epoch >= 0.0 && epoch < 32503680000.0) {
        std::time_t t = (std::time_t)std::floor(epoch);
        std::tm tm{};
#ifdef _WIN32
        bool ok = localtime_s(&tm, &t) == 0;
#else
        bool ok = localtime_r(&t, &tm) != nullptr;
#endif
        if (ok) {
            Civil c;
            c.y = tm.tm_year + 1900; c.mo = tm.tm_mon + 1; c.d = tm.tm_mday;
            c.h = tm.tm_hour; c.mi = tm.tm_min; c.se = tm.tm_sec; c.wd = tm.tm_wday;
            return c;
        }
    }
    Civil c = civil_utc(epoch);
    // The offset was asked for the UTC clock read as local time; asked again
    // at the local time that answer gives, it settles near a switch.
    int64_t off = utc_offset_at(c.y, c.mo, c.d, c.h, c.mi, c.se);
    c = civil_utc(epoch + (double)off);
    off = utc_offset_at(c.y, c.mo, c.d, c.h, c.mi, c.se);
    return civil_utc(epoch + (double)off);
}

// The wall clock of an instant in the zone shift_hours east of UTC when
// shifted, otherwise in the machine's own zone.
inline Civil civil_at(double epoch, bool shifted, double shift_hours) {
    if (!shifted) return civil_local(epoch);
    return civil_utc(epoch + shift_hours * 3600.0);
}

// A date read as the clock on the wall: seconds on a timeline that has no
// zone and therefore no daylight saving time. Counting whole calendar days
// between two dates happens here, so that two midnights are a whole number
// of days apart on the days a clock change makes one of them 23 or 25
// hours.
inline double wall_seconds(double epoch) {
    Civil c = civil_local(epoch);
    return (double)(days_from_civil(c.y, c.mo, c.d) * 86400 + c.h * 3600 + c.mi * 60 + c.se) +
           (epoch - std::floor(epoch));
}

// Whole calendar months from a civil date, the day clamped to the length of
// the month it lands in.
inline void add_months(int64_t& y, int64_t& mo, int64_t& d, int64_t months) {
    int64_t index = y * 12 + (mo - 1) + months;
    int64_t ny = index >= 0 ? index / 12 : -((-index + 11) / 12);
    int64_t nm = index - ny * 12 + 1;
    int64_t last = days_in_month(ny, nm);
    if (d > last) d = last;
    y = ny; mo = nm;
}

// An instant moved by whole calendar months on the local calendar, the wall
// clock kept and the day clamped to the target month. Anchored on the date
// given, so a run of steps cannot drift.
inline double add_months(double epoch, int64_t months) {
    Civil c = civil_local(epoch);
    add_months(c.y, c.mo, c.d, months);
    return local_to_epoch(c) + (epoch - std::floor(epoch));
}

// An instant moved by days on the local calendar: the wall clock stays
// where it was across a change to or from daylight saving time. A fraction
// of a day is added as elapsed time.
inline double add_days(double epoch, double days) {
    Civil c = civil_local(epoch);
    double whole = std::trunc(days);
    civil_from_days(days_from_civil(c.y, c.mo, c.d) + (int64_t)whole, c.y, c.mo, c.d);
    return local_to_epoch(c) + (epoch - std::floor(epoch)) + (days - whole) * 86400.0;
}

// Midnight, local time, of the last day of the month offset_months away.
inline double end_of_month(double epoch, int64_t offset_months) {
    Civil c = civil_local(epoch);
    add_months(c.y, c.mo, c.d, offset_months);
    return local_to_epoch(c.y, c.mo, days_in_month(c.y, c.mo), 0, 0, 0);
}

// The unit letter of a DATEADD or DATEDIFF part in either case, 0 when the
// part is not exactly one of the letters in allowed.
inline char unit(const char* part, const char* allowed) {
    if (!part || !part[0] || part[1]) return 0;
    char p = (char)toupper((unsigned char)part[0]);
    return std::strchr(allowed, p) ? p : 0;
}

// DATEADD: D, W, M and Y move the local calendar and keep the wall clock;
// H, N and S add elapsed time, because the wall clock has no continuous
// hour to add across a clock change.
inline double date_add(char u, double amount, double epoch) {
    switch (u) {
    case 'H': return epoch + amount * 3600.0;
    case 'N': return epoch + amount * 60.0;
    case 'S': return epoch + amount;
    case 'W': return add_days(epoch, amount * 7.0);
    case 'M': return add_months(epoch, (int64_t)amount);
    case 'Y': return add_months(epoch, (int64_t)amount * 12);
    default:  return add_days(epoch, amount);
    }
}

// DATEDIFF: D counts days on the local wall clock, H, N and S elapsed time.
inline double date_diff(char u, double epoch1, double epoch2) {
    switch (u) {
    case 'D': return (wall_seconds(epoch2) - wall_seconds(epoch1)) / 86400.0;
    case 'H': return (epoch2 - epoch1) / 3600.0;
    case 'N': return (epoch2 - epoch1) / 60.0;
    default:  return epoch2 - epoch1;
    }
}

// The text a date is written as: YYYY-MM-DD or DD.MM.YYYY, with an
// optional time HH:MM[:SS] after a space or a T. False when the text is no
// date, or the month, day or time is out of range.
inline bool parse_text(const char* s, Civil& c) {
    if (!s) return false;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    int n = std::sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (n == 3) {
        int ty, tmo, td, th = 0, tmi = 0, tse = 0;
        if (std::sscanf(s, "%d-%d-%dT%d:%d:%d", &ty, &tmo, &td, &th, &tmi, &tse) > 3) {
            h = th; mi = tmi; se = tse;
        }
    }
    if (n < 3) {
        n = std::sscanf(s, "%d.%d.%d %d:%d:%d", &d, &mo, &y, &h, &mi, &se);
        if (n >= 3 && y < 100) return false;
    }
    if (n < 3) return false;
    if (!(mo >= 1 && mo <= 12 && d >= 1 && d <= days_in_month(y, mo) &&
          h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && se >= 0 && se <= 60)) return false;
    c.y = y; c.mo = mo; c.d = d; c.h = h; c.mi = mi; c.se = se;
    c.wd = ((days_from_civil(y, mo, d) + 4) % 7 + 7) % 7;
    return true;
}

// An ISO timestamp read as it stands, YYYY-MM-DD with an optional time
// after a space or a T, without checking the ranges.
inline bool parse_iso(const char* s, Civil& c) {
    if (!s) return false;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    int n = std::sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (n < 3) return false;
    if (n == 3) {
        int ty, tmo, td, th = 0, tmi = 0, tse = 0;
        if (std::sscanf(s, "%d-%d-%dT%d:%d:%d", &ty, &tmo, &td, &th, &tmi, &tse) > 3) {
            h = th; mi = tmi; se = tse;
        }
    }
    c.y = y; c.mo = mo; c.d = d; c.h = h; c.mi = mi; c.se = se;
    c.wd = ((days_from_civil(y, mo, d) + 4) % 7 + 7) % 7;
    return true;
}

// A wall clock read as UTC, as an instant.
inline double utc_to_epoch(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t se) {
    return (double)(days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se);
}

inline double utc_to_epoch(const Civil& c) {
    return utc_to_epoch(c.y, c.mo, c.d, c.h, c.mi, c.se);
}

// The day of the year, 0 for January 1st.
inline int year_day(const Civil& c) {
    return (int)(days_from_civil(c.y, c.mo, c.d) - days_from_civil(c.y, 1, 1));
}

// Whether the text starts the way an ISO timestamp does: four digits and
// a dash.
inline bool looks_iso(const char* s) {
    return s && isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
           isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) && s[4] == '-';
}

// YYYY-MM-DD, with HH:MM:SS after a space when with_time.
inline int format_iso(char* buf, int cap, const Civil& c, bool with_time) {
    if (with_time)
        return std::snprintf(buf, cap, "%04lld-%02lld-%02lld %02lld:%02lld:%02lld",
                             (long long)c.y, (long long)c.mo, (long long)c.d,
                             (long long)c.h, (long long)c.mi, (long long)c.se);
    return std::snprintf(buf, cap, "%04lld-%02lld-%02lld",
                         (long long)c.y, (long long)c.mo, (long long)c.d);
}

// HH:MM:SS.
inline int format_time(char* buf, int cap, const Civil& c) {
    return std::snprintf(buf, cap, "%02lld:%02lld:%02lld",
                         (long long)c.h, (long long)c.mi, (long long)c.se);
}

// A tm filled from the calendar, with the weekday and the day of the year,
// so strftime is never handed a struct a failed conversion left untouched.
inline void fill_tm(const Civil& c, std::tm& out) {
    std::memset(&out, 0, sizeof(out));
    out.tm_year = (int)(c.y - 1900);
    out.tm_mon  = (int)(c.mo - 1);
    out.tm_mday = (int)c.d;
    out.tm_hour = (int)c.h;
    out.tm_min  = (int)c.mi;
    out.tm_sec  = (int)c.se;
    out.tm_wday = (int)c.wd;
    out.tm_yday = (int)(days_from_civil(c.y, c.mo, c.d) - days_from_civil(c.y, 1, 1));
    out.tm_isdst = 0;
}

// strftime of an instant's wall clock, in the zone shift_hours east of UTC
// when shifted, otherwise in the machine's zone. An empty format is the
// standard timestamp.
inline int format_at(char* buf, int cap, const char* fmt, double epoch, bool shifted, double shift_hours) {
    if (!fmt || !*fmt) fmt = "%Y-%m-%d %H:%M:%S";
    std::tm tm;
    fill_tm(civil_at(epoch, shifted, shift_hours), tm);
    buf[0] = '\0';
    return (int)std::strftime(buf, (size_t)cap, fmt, &tm);
}

// The standard timestamp of an instant, local time.
inline int timestamp(char* buf, int cap, double epoch) {
    return format_iso(buf, cap, civil_local(epoch), true);
}

}  // namespace jdb_cal
