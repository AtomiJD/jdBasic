// Image metadata on the bytes of a file (see image_meta.h). Every offset,
// count and length read from the file is checked against the bytes there
// are before it is used; a damaged part is skipped, not trusted.

#include "image_meta.h"
#include "pdf_extract.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace jdb_image {

namespace {

constexpr size_t kMaxTextOut = (size_t)1 << 20;
constexpr size_t kMaxTextTotal = (size_t)8 << 20;
constexpr uint32_t kMaxEntries = 1000;

// ---- small byte helpers ----------------------------------------------------

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void put16(Bytes& b, uint16_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
void put32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24)); b.push_back((uint8_t)(v >> 16));
    b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v);
}
bool starts(const uint8_t* p, size_t n, const char* lit, size_t len) {
    return n >= len && memcmp(p, lit, len) == 0;
}
bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string latin1_to_utf8(const uint8_t* p, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; i++) append_utf8(out, p[i]);
    return out;
}

std::string utf16_to_utf8(const uint8_t* p, size_t n, bool little) {
    std::string out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t u = little ? le16(p + i) : be16(p + i);
        if (u == 0) break;
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
            uint32_t lo = little ? le16(p + i + 2) : be16(p + i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                append_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                i += 2;
                continue;
            }
        }
        append_utf8(out, (u >= 0xD800 && u < 0xE000) ? 0xFFFD : u);
    }
    return out;
}

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && s[b] == ' ') b++;
    return s.substr(b);
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---- EXIF (a TIFF structure) -----------------------------------------------

struct Tag {
    uint16_t tag = 0, type = 0;
    uint32_t count = 0;
    Bytes data;  // big-endian, element by element
};

struct Exif {
    std::vector<Tag> ifd0, exif, gps;
    bool empty() const { return ifd0.empty() && exif.empty() && gps.empty(); }
};

int type_size(uint16_t t) {
    switch (t) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

int element_size(uint16_t t) {
    switch (t) {
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: case 5: case 10: return 4;
        case 12: return 8;
        default: return 1;
    }
}

struct TiffReader {
    const uint8_t* p;
    size_t n;
    bool little;
    // The bytes all values may copy together: a value is stored once in
    // the TIFF, so more than its size means entries that share bytes.
    mutable uint64_t budget;
    bool fits(uint64_t off, uint64_t len) const { return off <= n && len <= n - off; }
    uint16_t u16(size_t o) const { return little ? le16(p + o) : be16(p + o); }
    uint32_t u32(size_t o) const { return little ? le32(p + o) : be32(p + o); }

    void read_ifd(uint32_t off, std::vector<Tag>& out) const {
        if (!fits(off, 2)) return;
        uint32_t count = u16(off);
        if (count > kMaxEntries) count = kMaxEntries;
        if (!fits(off + 2ull, 12ull * count)) count = (uint32_t)((n - off - 2) / 12);
        for (uint32_t i = 0; i < count; i++) {
            size_t e = off + 2 + 12 * (size_t)i;
            Tag t;
            t.tag = u16(e);
            t.type = u16(e + 2);
            t.count = u32(e + 4);
            int ts = type_size(t.type);
            if (ts == 0) continue;
            uint64_t total = (uint64_t)t.count * (uint64_t)ts;
            if (total > n || total > budget) continue;
            uint64_t src = total <= 4 ? e + 8 : u32(e + 8);
            if (!fits(src, total)) continue;
            budget -= total;
            t.data.assign(p + src, p + src + total);
            int es = element_size(t.type);
            if (little && es > 1)
                for (size_t k = 0; k + es <= t.data.size(); k += es)
                    std::reverse(t.data.begin() + k, t.data.begin() + k + es);
            out.push_back(std::move(t));
        }
    }
};

const Tag* find_tag(const std::vector<Tag>& v, uint16_t tag) {
    for (auto& t : v) if (t.tag == tag) return &t;
    return nullptr;
}

uint32_t tag_offset(const Tag* t) {
    if (!t || t->count != 1) return 0;
    if (t->type == 4 || t->type == 13) return be32(t->data.data());
    if (t->type == 3) return be16(t->data.data());
    return 0;
}

bool parse_exif(const uint8_t* p, size_t n, Exif& e, bool* little_out = nullptr) {
    if (n < 8) return false;
    bool little;
    if (p[0] == 'I' && p[1] == 'I') little = true;
    else if (p[0] == 'M' && p[1] == 'M') little = false;
    else return false;
    TiffReader r{ p, n, little, n };
    if (r.u16(2) != 42) return false;
    uint32_t at0 = r.u32(4);
    r.read_ifd(at0, e.ifd0);
    uint32_t at_exif = tag_offset(find_tag(e.ifd0, 0x8769));
    uint32_t at_gps = tag_offset(find_tag(e.ifd0, 0x8825));
    if (at_exif && at_exif != at0) r.read_ifd(at_exif, e.exif);
    if (at_gps && at_gps != at0 && at_gps != at_exif) r.read_ifd(at_gps, e.gps);
    // A comment in UTF-16 is kept big-endian like every other value here.
    for (auto& t : e.exif)
        if (t.tag == 0x9286 && little && t.data.size() >= 8 && memcmp(t.data.data(), "UNICODE\0", 8) == 0)
            for (size_t k = 8; k + 1 < t.data.size(); k += 2) std::swap(t.data[k], t.data[k + 1]);
    if (little_out) *little_out = little;
    return true;
}

std::string ascii_of(const Tag& t) {
    std::string s(t.data.begin(), t.data.end());
    while (!s.empty() && s.back() == '\0') s.pop_back();
    // Copyright may hold the photographer and the editor apart by a NUL.
    for (auto& c : s) if (c == '\0') c = '/';
    return trimmed(s);
}

std::string user_comment_of(const Tag& t) {
    if (t.data.size() < 8) return "";
    const uint8_t* p = t.data.data();
    if (memcmp(p, "UNICODE\0", 8) == 0) return trimmed(utf16_to_utf8(p + 8, t.data.size() - 8, false));
    if (memcmp(p, "ASCII\0\0\0", 8) == 0 || memcmp(p, "\0\0\0\0\0\0\0\0", 8) == 0)
        return trimmed(std::string(p + 8, p + t.data.size()));
    return "";
}

bool rational(const Tag* t, size_t i, double& v) {
    if (!t || (t->type != 5 && t->type != 10) || t->data.size() < 8 * (i + 1)) return false;
    const uint8_t* p = t->data.data() + 8 * i;
    double num = t->type == 5 ? (double)be32(p) : (double)(int32_t)be32(p);
    double den = t->type == 5 ? (double)be32(p + 4) : (double)(int32_t)be32(p + 4);
    if (den == 0) return false;
    v = num / den;
    return true;
}

bool degrees(const Tag* t, double& v) {
    double d, m = 0, s = 0;
    if (!rational(t, 0, d)) return false;
    rational(t, 1, m);
    rational(t, 2, s);
    v = d + m / 60.0 + s / 3600.0;
    return true;
}

int orientation_of(const Exif& e) {
    const Tag* t = find_tag(e.ifd0, 0x0112);
    if (!t || t->type != 3 || t->data.size() < 2) return 0;
    int o = be16(t->data.data());
    return (o >= 1 && o <= 8) ? o : 0;
}

void meta_from_exif(const Exif& e, Meta& m) {
    struct Name { uint16_t tag; const char* name; bool in_exif; };
    static const Name names[] = {
        { 0x010F, "make", false }, { 0x0110, "model", false }, { 0x0131, "software", false },
        { 0x0132, "datetime", false }, { 0x013B, "artist", false }, { 0x8298, "copyright", false },
        { 0x010E, "description", false }, { 0x9003, "datetime_original", true },
        { 0xA430, "owner", true }, { 0xA431, "serial", true }, { 0xA435, "lens_serial", true },
    };
    for (auto& nm : names) {
        const Tag* t = find_tag(nm.in_exif ? e.exif : e.ifd0, nm.tag);
        if (!t || t->type != 2) continue;
        std::string s = ascii_of(*t);
        if (!s.empty()) m.texts.push_back({ nm.name, s });
    }
    if (const Tag* t = find_tag(e.exif, 0x9286)) {
        std::string s = user_comment_of(*t);
        if (!s.empty()) m.texts.push_back({ "user_comment", s });
    }
    // The fields Windows writes from the file's properties, always UTF-16LE.
    static const Name xp[] = {
        { 0x9C9B, "xp_title", false }, { 0x9C9C, "xp_comment", false }, { 0x9C9D, "xp_author", false },
        { 0x9C9E, "xp_keywords", false }, { 0x9C9F, "xp_subject", false },
    };
    for (auto& nm : xp) {
        const Tag* t = find_tag(e.ifd0, nm.tag);
        if (!t || (t->type != 1 && t->type != 7)) continue;
        std::string s = trimmed(utf16_to_utf8(t->data.data(), t->data.size(), true));
        if (!s.empty()) m.texts.push_back({ nm.name, s });
    }
    if (int o = orientation_of(e)) m.orientation = o;
    double lat, lon;
    if (degrees(find_tag(e.gps, 0x0002), lat) && degrees(find_tag(e.gps, 0x0004), lon)) {
        const Tag* lr = find_tag(e.gps, 0x0001);
        const Tag* gr = find_tag(e.gps, 0x0003);
        if (lr && !lr->data.empty() && lr->data[0] == 'S') lat = -lat;
        if (gr && !gr->data.empty() && gr->data[0] == 'W') lon = -lon;
        m.has_gps = true;
        m.latitude = lat;
        m.longitude = lon;
    }
    double alt;
    if (rational(find_tag(e.gps, 0x0006), 0, alt)) {
        const Tag* ar = find_tag(e.gps, 0x0005);
        if (ar && !ar->data.empty() && ar->data[0] == 1) alt = -alt;
        m.has_altitude = true;
        m.altitude = alt;
    }
}

bool dropped_on_rewrite(uint16_t tag, bool in_exif) {
    if (in_exif) return tag == 0x927C || tag == 0xA005 || tag == 0xEA1C;
    switch (tag) {
        case 0x8769: case 0x8825: case 0xA005: case 0x0111: case 0x0117:
        case 0x0201: case 0x0202: case 0x0144: case 0x0145: case 0x014A: case 0xEA1C:
            return true;
        default:
            return false;
    }
}

size_t ifd_bytes(const std::vector<Tag>& v) {
    size_t n = 2 + 12 * v.size() + 4;
    for (auto& t : v) if (t.data.size() > 4) n += (t.data.size() + 1) & ~(size_t)1;
    return n;
}

void write_ifd(Bytes& out, std::vector<Tag> v) {
    std::sort(v.begin(), v.end(), [](const Tag& a, const Tag& b) { return a.tag < b.tag; });
    size_t base = out.size();
    size_t data_at = base + 2 + 12 * v.size() + 4;
    Bytes data;
    put16(out, (uint16_t)v.size());
    for (auto& t : v) {
        put16(out, t.tag);
        put16(out, t.type);
        put32(out, t.count);
        if (t.data.size() <= 4) {
            Bytes four(t.data);
            four.resize(4, 0);
            out.insert(out.end(), four.begin(), four.end());
        } else {
            // Offsets count from the start of the TIFF header, which the
            // caller placed at the start of out.
            put32(out, (uint32_t)(data_at + data.size()));
            data.insert(data.end(), t.data.begin(), t.data.end());
            if (data.size() & 1) data.push_back(0);
        }
    }
    put32(out, 0);
    out.insert(out.end(), data.begin(), data.end());
}

Tag long_tag(uint16_t tag, uint32_t v) {
    Tag t;
    t.tag = tag;
    t.type = 4;
    t.count = 1;
    put32(t.data, v);
    return t;
}

// A big-endian TIFF of the three IFDs; empty when they hold nothing.
Bytes build_tiff(const Exif& e) {
    std::vector<Tag> i0, ex, gp;
    for (auto& t : e.ifd0) if (!dropped_on_rewrite(t.tag, false)) i0.push_back(t);
    for (auto& t : e.exif) if (!dropped_on_rewrite(t.tag, true)) ex.push_back(t);
    gp = e.gps;
    if (i0.empty() && ex.empty() && gp.empty()) return {};
    if (!ex.empty()) i0.push_back(long_tag(0x8769, 0));
    if (!gp.empty()) i0.push_back(long_tag(0x8825, 0));
    size_t off_exif = 8 + ifd_bytes(i0);
    size_t off_gps = off_exif + (ex.empty() ? 0 : ifd_bytes(ex));
    for (auto& t : i0) {
        if (t.tag == 0x8769) t = long_tag(0x8769, (uint32_t)off_exif);
        if (t.tag == 0x8825) t = long_tag(0x8825, (uint32_t)off_gps);
    }
    Bytes out = { 'M', 'M', 0, 42 };
    put32(out, 8);
    write_ifd(out, i0);
    if (!ex.empty()) write_ifd(out, ex);
    if (!gp.empty()) write_ifd(out, gp);
    return out;
}

Exif orientation_only(int orientation) {
    Exif e;
    Tag t;
    t.tag = 0x0112;
    t.type = 3;
    t.count = 1;
    put16(t.data, (uint16_t)orientation);
    e.ifd0.push_back(t);
    return e;
}

void set_ascii(std::vector<Tag>& v, uint16_t tag, const std::string& value) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Tag& t) { return t.tag == tag; }), v.end());
    if (value.empty()) return;
    Tag t;
    t.tag = tag;
    t.type = 2;
    t.data.assign(value.begin(), value.end());
    t.data.push_back(0);
    t.count = (uint32_t)t.data.size();
    v.push_back(std::move(t));
}

struct FieldName { const char* name; uint16_t exif_tag; const char* png_key; };
const FieldName kFields[] = {
    { "artist", 0x013B, "Author" },
    { "copyright", 0x8298, "Copyright" },
    { "description", 0x010E, "Description" },
    { "software", 0x0131, "Software" },
    { "datetime", 0x0132, "Creation Time" },
};

const FieldName* field_named(const std::string& n) {
    for (auto& f : kFields) if (n == f.name) return &f;
    return nullptr;
}

// ---- JPEG --------------------------------------------------------------------

struct Seg {
    uint8_t marker;
    size_t start;  // the 0xFF of the marker
    size_t size;   // marker, length and payload
};

struct Jpeg {
    std::vector<Seg> segs;
    size_t rest = 0;  // from the scan (or whatever could not be read) on, copied as it is
};

bool parse_jpeg(const Bytes& b, Jpeg& j) {
    size_t n = b.size();
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return false;
    size_t pos = 2;
    j.rest = n;
    while (pos < n) {
        if (b[pos] != 0xFF) { j.rest = pos; break; }
        size_t start = pos;
        while (pos < n && b[pos] == 0xFF) pos++;
        if (pos >= n) { j.rest = start; break; }
        uint8_t marker = b[pos++];
        if (marker == 0xD9 || marker == 0xDA) { j.rest = start; break; }
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            j.segs.push_back({ marker, start, pos - start });
            continue;
        }
        if (pos + 2 > n) { j.rest = start; break; }
        size_t len = be16(&b[pos]);
        if (len < 2 || pos + len > n) { j.rest = start; break; }
        pos += len;
        j.segs.push_back({ marker, start, pos - start });
    }
    return true;
}

const uint8_t* payload(const Bytes& b, const Seg& s, size_t& len) {
    len = 0;
    // The fill bytes before a marker are counted in size; the length field
    // follows the marker byte after them.
    size_t len_at = s.start;
    while (len_at < s.start + s.size && b[len_at] == 0xFF) len_at++;
    len_at++;
    if (len_at + 2 > s.start + s.size) return nullptr;
    size_t l = be16(&b[len_at]);
    if (l < 2 || len_at + l > s.start + s.size) return nullptr;
    len = l - 2;
    return &b[len_at + 2];
}

const char kExifId[] = "Exif\0\0";
const char kXmpId[] = "http://ns.adobe.com/xap/1.0/";       // followed by a NUL
const char kXmpExtId[] = "http://ns.adobe.com/xmp/extension/";
const char kIccId[] = "ICC_PROFILE";

bool seg_is(const Bytes& b, const Seg& s, uint8_t marker, const char* id, size_t idlen) {
    if (s.marker != marker) return false;
    size_t len;
    const uint8_t* p = payload(b, s, len);
    return p && starts(p, len, id, idlen);
}
bool is_exif(const Bytes& b, const Seg& s) { return seg_is(b, s, 0xE1, kExifId, 6); }
bool is_xmp(const Bytes& b, const Seg& s) {
    return seg_is(b, s, 0xE1, kXmpId, sizeof kXmpId) || seg_is(b, s, 0xE1, kXmpExtId, sizeof kXmpExtId);
}
bool is_icc(const Bytes& b, const Seg& s) { return seg_is(b, s, 0xE2, kIccId, sizeof kIccId); }
bool is_sof(uint8_t m) { return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC; }

std::string xmp_text(const Bytes& b, const Seg& s) {
    size_t len;
    const uint8_t* p = payload(b, s, len);
    size_t skip = starts(p, len, kXmpId, sizeof kXmpId) ? sizeof kXmpId : sizeof kXmpExtId;
    // An extension also carries a GUID, the full length and an offset.
    if (skip == sizeof kXmpExtId) skip += 40;
    if (len <= skip) return "";
    return std::string(p + skip, p + len);
}

bool exif_of_seg(const Bytes& b, const Seg& s, Exif& e, bool* little = nullptr) {
    size_t len;
    const uint8_t* p = payload(b, s, len);
    return p && len > 6 && parse_exif(p + 6, len - 6, e, little);
}

bool app1_of(const Bytes& tiff, Bytes& seg) {
    size_t len = 2 + 6 + tiff.size();
    if (len > 0xFFFF) return false;
    seg = { 0xFF, 0xE1 };
    put16(seg, (uint16_t)len);
    seg.insert(seg.end(), kExifId, kExifId + 6);
    seg.insert(seg.end(), tiff.begin(), tiff.end());
    return true;
}

void append_seg(Bytes& out, const Bytes& b, const Seg& s) {
    out.insert(out.end(), b.begin() + s.start, b.begin() + s.start + s.size);
}

// out = SOI, the kept segments with `extra` placed after a leading APP0 (or
// first), and the rest of the file.
struct JpegOut {
    Bytes out{ 0xFF, 0xD8 };
    bool placed = false;
    Bytes extra;
    void flush_extra() {
        if (!placed && !extra.empty()) out.insert(out.end(), extra.begin(), extra.end());
        placed = true;
    }
};

bool jpeg_read(const Bytes& b, Meta& m) {
    Jpeg j;
    if (!parse_jpeg(b, j)) return false;
    m.format = "jpeg";
    bool exif_done = false;
    for (auto& s : j.segs) {
        if (!exif_done && is_exif(b, s)) {
            Exif e;
            if (exif_of_seg(b, s, e)) meta_from_exif(e, m);
            exif_done = true;
        } else if (is_xmp(b, s)) {
            m.xmp += xmp_text(b, s);
        } else if (s.marker == 0xED) {
            m.iptc = true;
        } else if (s.marker == 0xFE) {
            size_t len;
            const uint8_t* p = payload(b, s, len);
            if (!p) continue;
            if (!m.comment.empty()) m.comment += "\n";
            m.comment += trimmed(std::string(p, p + len));
            m.has_comment = true;
        }
    }
    return true;
}

int jpeg_strip(const Bytes& b, bool gps_only, Bytes& result) {
    Jpeg j;
    if (!parse_jpeg(b, j)) return RC_SOURCE;
    JpegOut o;
    int orientation = 1;
    for (auto& s : j.segs) {
        if (!is_exif(b, s)) continue;
        Exif e;
        if (exif_of_seg(b, s, e)) orientation = std::max(1, orientation_of(e));
        break;
    }
    if (!gps_only && orientation != 1 && !app1_of(build_tiff(orientation_only(orientation)), o.extra))
        return RC_WRITE;
    for (size_t i = 0; i < j.segs.size(); i++) {
        const Seg& s = j.segs[i];
        bool app = s.marker >= 0xE0 && s.marker <= 0xEF;
        if (gps_only) {
            if (is_exif(b, s)) {
                Exif e;
                if (exif_of_seg(b, s, e) && !e.gps.empty()) {
                    e.gps.clear();
                    Bytes tiff = build_tiff(e), seg;
                    if (!tiff.empty()) {
                        if (!app1_of(tiff, seg)) return RC_WRITE;
                        o.out.insert(o.out.end(), seg.begin(), seg.end());
                    }
                    continue;
                }
            }
            if (is_xmp(b, s) && contains(xmp_text(b, s), "GPS")) continue;
            append_seg(o.out, b, s);
            continue;
        }
        bool keep = !app ? s.marker != 0xFE
                         : s.marker == 0xE0 || s.marker == 0xEE || is_icc(b, s);
        if (!keep) continue;
        if (s.marker != 0xE0) o.flush_extra();
        append_seg(o.out, b, s);
    }
    o.flush_extra();
    o.out.insert(o.out.end(), b.begin() + j.rest, b.end());
    result.swap(o.out);
    return RC_OK;
}

int jpeg_set(const Bytes& b, const Fields& fields, Bytes& result) {
    Jpeg j;
    if (!parse_jpeg(b, j)) return RC_SOURCE;
    Exif e;
    long at = -1;
    for (size_t i = 0; i < j.segs.size(); i++) {
        if (!is_exif(b, j.segs[i])) continue;
        exif_of_seg(b, j.segs[i], e);
        at = (long)i;
        break;
    }
    for (auto& f : fields) set_ascii(e.ifd0, field_named(f.first)->exif_tag, f.second);
    Bytes seg;
    Bytes tiff = build_tiff(e);
    if (!tiff.empty() && !app1_of(tiff, seg)) return RC_WRITE;
    JpegOut o;
    if (at < 0) o.extra = seg;
    for (size_t i = 0; i < j.segs.size(); i++) {
        const Seg& s = j.segs[i];
        if ((long)i == at) {
            o.out.insert(o.out.end(), seg.begin(), seg.end());
            continue;
        }
        if (s.marker != 0xE0) o.flush_extra();
        append_seg(o.out, b, s);
    }
    o.flush_extra();
    o.out.insert(o.out.end(), b.begin() + j.rest, b.end());
    result.swap(o.out);
    return RC_OK;
}

// ---- PNG ---------------------------------------------------------------------

const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

struct Chunk {
    size_t start;  // the length field
    size_t len;    // of the data
    char type[5];
    const uint8_t* data(const Bytes& b) const { return &b[start + 8]; }
    size_t size() const { return 12 + len; }
};

struct Png {
    std::vector<Chunk> chunks;
    size_t rest = 0;
};

bool parse_png(const Bytes& b, Png& p) {
    size_t n = b.size();
    if (n < 8 || memcmp(b.data(), kPngSig, 8) != 0) return false;
    size_t pos = 8;
    p.rest = n;
    while (pos < n) {
        if (n - pos < 12) { p.rest = pos; break; }
        uint32_t len = be32(&b[pos]);
        if (len > n - pos - 12) { p.rest = pos; break; }
        Chunk c;
        c.start = pos;
        c.len = len;
        memcpy(c.type, &b[pos + 4], 4);
        c.type[4] = 0;
        p.chunks.push_back(c);
        pos += c.size();
        if (strcmp(c.type, "IEND") == 0) { p.rest = pos; break; }
    }
    return true;
}

bool is_text_chunk(const Chunk& c) {
    return !strcmp(c.type, "tEXt") || !strcmp(c.type, "zTXt") || !strcmp(c.type, "iTXt");
}

// The keyword and text of a text chunk; false when it is damaged.
bool png_text(const Bytes& b, const Chunk& c, std::string& key, std::string& text) {
    const uint8_t* d = c.data(b);
    size_t n = c.len;
    size_t k = 0;
    while (k < n && k < 80 && d[k]) k++;
    if (k == 0 || k >= n || d[k]) return false;
    key = latin1_to_utf8(d, k);
    size_t at = k + 1;
    auto inflated = [&](const uint8_t* p, size_t len, std::string& out) {
        std::vector<uint8_t> v;
        if (!pdf_extract::tinfl::inflate(p, len, v, true, nullptr, kMaxTextOut)) return false;
        out.assign(v.begin(), v.end());
        return true;
    };
    if (!strcmp(c.type, "tEXt")) {
        text = latin1_to_utf8(d + at, n - at);
        return true;
    }
    if (!strcmp(c.type, "zTXt")) {
        if (at >= n || d[at] != 0) return false;
        std::string raw;
        if (!inflated(d + at + 1, n - at - 1, raw)) return false;
        text = latin1_to_utf8((const uint8_t*)raw.data(), raw.size());
        return true;
    }
    if (n - at < 2) return false;
    bool compressed = d[at] != 0;
    at += 2;
    for (int skip = 0; skip < 2; skip++) {
        while (at < n && d[at]) at++;
        if (at >= n) return false;
        at++;
    }
    if (!compressed) {
        text.assign(d + at, d + n);
        return true;
    }
    return inflated(d + at, n - at, text);
}

void put_chunk(Bytes& out, const char* type, const Bytes& data) {
    put32(out, (uint32_t)data.size());
    size_t from = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(&out[from], 4 + data.size()));
}

void append_chunk(Bytes& out, const Bytes& b, const Chunk& c) {
    out.insert(out.end(), b.begin() + c.start, b.begin() + c.start + c.size());
}

bool png_read(const Bytes& b, Meta& m) {
    Png p;
    if (!parse_png(b, p)) return false;
    m.format = "png";
    bool exif_done = false;
    // Each text is inflated to at most kMaxTextOut; all of them together stop
    // at kMaxTextTotal, past which further text chunks are left out.
    size_t text_total = 0;
    for (auto& c : p.chunks) {
        if (is_text_chunk(c)) {
            if (text_total > kMaxTextTotal) continue;
            std::string key, text;
            if (!png_text(b, c, key, text)) continue;
            text_total += key.size() + text.size();
            if (text_total > kMaxTextTotal) continue;
            if (key == "XML:com.adobe.xmp") m.xmp += text;
            else m.png_text.push_back({ key, text });
        } else if (!exif_done && !strcmp(c.type, "eXIf")) {
            Exif e;
            if (parse_exif(c.data(b), c.len, e)) meta_from_exif(e, m);
            exif_done = true;
        }
    }
    return true;
}

int png_strip(const Bytes& b, bool gps_only, Bytes& result) {
    Png p;
    if (!parse_png(b, p)) return RC_SOURCE;
    Bytes out(kPngSig, kPngSig + 8);
    int orientation = 1;
    for (auto& c : p.chunks) {
        if (strcmp(c.type, "eXIf")) continue;
        Exif e;
        if (parse_exif(c.data(b), c.len, e)) orientation = std::max(1, orientation_of(e));
        break;
    }
    for (auto& c : p.chunks) {
        if (gps_only) {
            if (!strcmp(c.type, "eXIf")) {
                Exif e;
                if (parse_exif(c.data(b), c.len, e) && !e.gps.empty()) {
                    e.gps.clear();
                    Bytes tiff = build_tiff(e);
                    if (!tiff.empty()) put_chunk(out, "eXIf", tiff);
                    continue;
                }
            }
            if (is_text_chunk(c)) {
                std::string key, text;
                if (png_text(b, c, key, text) && key == "XML:com.adobe.xmp" && contains(text, "GPS")) continue;
            }
            append_chunk(out, b, c);
            continue;
        }
        if (is_text_chunk(c) || !strcmp(c.type, "eXIf") || !strcmp(c.type, "tIME")) continue;
        append_chunk(out, b, c);
        if (!strcmp(c.type, "IHDR") && orientation != 1)
            put_chunk(out, "eXIf", build_tiff(orientation_only(orientation)));
    }
    out.insert(out.end(), b.begin() + p.rest, b.end());
    result.swap(out);
    return RC_OK;
}

Bytes text_chunk_data(const std::string& key, const std::string& value, bool& itxt) {
    itxt = std::any_of(value.begin(), value.end(), [](char ch) { return (unsigned char)ch >= 0x80; });
    Bytes d(key.begin(), key.end());
    d.push_back(0);
    if (itxt) { d.push_back(0); d.push_back(0); d.push_back(0); d.push_back(0); }
    d.insert(d.end(), value.begin(), value.end());
    return d;
}

int png_set(const Bytes& b, const Fields& fields, Bytes& result) {
    Png p;
    if (!parse_png(b, p)) return RC_SOURCE;
    std::vector<std::string> keys;
    for (auto& f : fields) keys.push_back(field_named(f.first)->png_key);
    Bytes out(kPngSig, kPngSig + 8);
    bool added = false;
    auto add_fields = [&]() {
        if (added) return;
        added = true;
        for (auto& f : fields) {
            if (f.second.empty()) continue;
            bool itxt;
            Bytes d = text_chunk_data(field_named(f.first)->png_key, f.second, itxt);
            put_chunk(out, itxt ? "iTXt" : "tEXt", d);
        }
    };
    for (auto& c : p.chunks) {
        if (is_text_chunk(c)) {
            std::string key, text;
            if (png_text(b, c, key, text) && std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
        }
        if (!strcmp(c.type, "IDAT") || !strcmp(c.type, "IEND")) add_fields();
        append_chunk(out, b, c);
    }
    add_fields();
    out.insert(out.end(), b.begin() + p.rest, b.end());
    result.swap(out);
    return RC_OK;
}

}  // namespace

// ---- public ------------------------------------------------------------------

bool read_file(const std::string& path, Bytes& out) {
    std::error_code ec;
    auto p = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(p, ec)) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

bool write_file(const std::string& path, const Bytes& data) {
    if (path.empty()) return false;
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)data.data(), (std::streamsize)data.size());
    f.close();
    return !f.fail();
}

bool read_meta(const Bytes& file, Meta& m) {
    return jpeg_read(file, m) || png_read(file, m);
}

int jpeg_orientation(const Bytes& file) {
    Jpeg j;
    if (!parse_jpeg(file, j)) return 1;
    for (auto& s : j.segs) {
        if (!is_exif(file, s)) continue;
        Exif e;
        if (exif_of_seg(file, s, e)) return std::max(1, orientation_of(e));
        break;
    }
    return 1;
}

bool display_size(const Bytes& b, int& w, int& h) {
    w = h = 0;
    size_t n = b.size();
    Jpeg j;
    if (parse_jpeg(b, j)) {
        for (auto& s : j.segs) {
            if (!is_sof(s.marker)) continue;
            size_t len;
            const uint8_t* p = payload(b, s, len);
            if (!p || len < 5) return false;
            h = be16(p + 1);
            w = be16(p + 3);
            if (jpeg_orientation(b) >= 5) std::swap(w, h);
            return w > 0 && h > 0;
        }
        return false;
    }
    if (n >= 24 && memcmp(b.data(), kPngSig, 8) == 0 && memcmp(&b[12], "IHDR", 4) == 0) {
        uint32_t pw = be32(&b[16]), ph = be32(&b[20]);
        if (pw == 0 || ph == 0 || pw > 0x7FFFFFFF || ph > 0x7FFFFFFF) return false;
        w = (int)pw;
        h = (int)ph;
        return true;
    }
    if (n >= 10 && (starts(b.data(), n, "GIF87a", 6) || starts(b.data(), n, "GIF89a", 6))) {
        w = le16(&b[6]);
        h = le16(&b[8]);
        return w > 0 && h > 0;
    }
    if (n >= 26 && b[0] == 'B' && b[1] == 'M') {
        uint32_t hs = le32(&b[14]);
        if (hs == 12) {
            w = le16(&b[18]);
            h = le16(&b[20]);
        } else if (hs >= 40) {
            int32_t bw = (int32_t)le32(&b[18]), bh = (int32_t)le32(&b[22]);
            if (bw <= 0 || bh == 0 || bh == INT32_MIN) return false;
            w = bw;
            h = bh < 0 ? -bh : bh;
        } else {
            return false;
        }
        return w > 0 && h > 0;
    }
    return false;
}

int strip(const Bytes& file, const std::string& what, Bytes& out) {
    if (what != "all" && what != "gps") return RC_ARG;
    bool gps = what == "gps";
    Jpeg j;
    if (parse_jpeg(file, j)) return jpeg_strip(file, gps, out);
    Png p;
    if (parse_png(file, p)) return png_strip(file, gps, out);
    return RC_SOURCE;
}

int set_meta(const Bytes& file, const Fields& fields, Bytes& out) {
    for (auto& f : fields) if (!field_named(f.first)) return RC_ARG;
    Jpeg j;
    if (parse_jpeg(file, j)) return jpeg_set(file, fields, out);
    Png p;
    if (parse_png(file, p)) return png_set(file, fields, out);
    return RC_SOURCE;
}

}  // namespace jdb_image
