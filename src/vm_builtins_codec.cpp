// VM builtins: CODEC.* hashes and encodings, ZIP.*.

#include "vm_internal.h"

// ── SHA-256 (FIPS 180-4) ────────────────────────────────────
// Byte oriented, so a key or message carrying a NUL is hashed in full.

static void sha256_digest(const uint8_t* input, size_t len, uint8_t out[32]) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    auto ror = [](uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); };

    std::vector<uint8_t> data(input, input + len);
    uint64_t bitlen = (uint64_t)len * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; i--) data.push_back((uint8_t)(bitlen >> (i * 8)));

    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (data[chunk+i*4]<<24) | (data[chunk+i*4+1]<<16) | (data[chunk+i*4+2]<<8) | data[chunk+i*4+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15]>>3);
            uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e,6)^ror(e,11)^ror(e,25);
            uint32_t ch = (e&f)^(~e&g);
            uint32_t t1 = hh+S1+ch+K[i]+w[i];
            uint32_t S0 = ror(a,2)^ror(a,13)^ror(a,22);
            uint32_t maj = (a&b)^(a&c)^(b&c);
            uint32_t t2 = S0+maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

static std::string bytes_to_hex(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; i++) { out += d[b[i] >> 4]; out += d[b[i] & 0x0F]; }
    return out;
}

static std::string sha256_hex(const std::string& msg) {
    uint8_t digest[32];
    sha256_digest((const uint8_t*)msg.data(), msg.size(), digest);
    return bytes_to_hex(digest, 32);
}

// HMAC per RFC 2104: a key longer than the 64-byte block is hashed first,
// a shorter one is zero padded.
static std::string hmac_sha256_hex(const std::string& key, const std::string& msg) {
    const size_t BLOCK = 64;
    uint8_t k[BLOCK];
    memset(k, 0, BLOCK);
    if (key.size() > BLOCK) {
        sha256_digest((const uint8_t*)key.data(), key.size(), k);
    } else {
        memcpy(k, key.data(), key.size());
    }

    std::vector<uint8_t> inner(BLOCK + msg.size());
    for (size_t i = 0; i < BLOCK; i++) inner[i] = k[i] ^ 0x36;
    memcpy(inner.data() + BLOCK, msg.data(), msg.size());
    uint8_t inner_digest[32];
    sha256_digest(inner.data(), inner.size(), inner_digest);

    std::vector<uint8_t> outer(BLOCK + 32);
    for (size_t i = 0; i < BLOCK; i++) outer[i] = k[i] ^ 0x5c;
    memcpy(outer.data() + BLOCK, inner_digest, 32);
    uint8_t out_digest[32];
    sha256_digest(outer.data(), outer.size(), out_digest);
    return bytes_to_hex(out_digest, 32);
}

// ── ZIP archives ────────────────────────────────────────────
// Writing deflates each entry that shrinks and stores the rest. Reading
// handles stored and deflated entries; the deflate side borrows the tinfl
// decoder that FlateDecode in pdf_extract already uses.

static uint32_t crc32_bytes(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void put16(std::string& out, uint16_t v) {
    out += (char)(v & 0xFF);
    out += (char)((v >> 8) & 0xFF);
}

static void put32(std::string& out, uint32_t v) {
    out += (char)(v & 0xFF);
    out += (char)((v >> 8) & 0xFF);
    out += (char)((v >> 16) & 0xFF);
    out += (char)((v >> 24) & 0xFF);
}

static uint16_t get16(const std::string& b, size_t at) {
    return (uint16_t)((uint8_t)b[at] | ((uint8_t)b[at+1] << 8));
}

static uint32_t get32(const std::string& b, size_t at) {
    return (uint32_t)((uint8_t)b[at]) | ((uint32_t)(uint8_t)b[at+1] << 8) |
           ((uint32_t)(uint8_t)b[at+2] << 16) | ((uint32_t)(uint8_t)b[at+3] << 24);
}

// Current local time in the MS-DOS packing the format has used since 1989.
static void dos_stamp(uint16_t& dos_time, uint16_t& dos_date) {
    time_t now = time(nullptr);
    struct tm t;
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    dos_time = (uint16_t)((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
    dos_date = (uint16_t)(((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
}

static std::string zip_build(const std::vector<std::pair<std::string, std::string>>& entries) {
    uint16_t dos_time = 0, dos_date = 0;
    dos_stamp(dos_time, dos_date);

    std::string body;
    std::string central;
    for (auto& [name, content] : entries) {
        uint32_t crc = crc32_bytes((const uint8_t*)content.data(), content.size());
        uint32_t size = (uint32_t)content.size();
        uint32_t offset = (uint32_t)body.size();

        // Deflated when that is smaller, stored otherwise.
        std::string packed = jdb_deflate::deflate_raw((const uint8_t*)content.data(), content.size(), 6);
        bool deflated = !content.empty() && packed.size() < content.size();
        uint16_t method = deflated ? 8 : 0;
        uint32_t comp_size = deflated ? (uint32_t)packed.size() : size;

        put32(body, 0x04034b50);
        put16(body, 20);        // version needed
        put16(body, 0x0800);    // UTF-8 names
        put16(body, method);
        put16(body, dos_time);
        put16(body, dos_date);
        put32(body, crc);
        put32(body, comp_size);
        put32(body, size);
        put16(body, (uint16_t)name.size());
        put16(body, 0);
        body += name;
        body += deflated ? packed : content;

        put32(central, 0x02014b50);
        put16(central, 20);     // version made by
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, method);
        put16(central, dos_time);
        put16(central, dos_date);
        put32(central, crc);
        put32(central, comp_size);
        put32(central, size);
        put16(central, (uint16_t)name.size());
        put16(central, 0);      // extra
        put16(central, 0);      // comment
        put16(central, 0);      // disk
        put16(central, 0);      // internal attrs
        put32(central, 0);      // external attrs
        put32(central, offset);
        central += name;
    }

    std::string out = body;
    uint32_t central_offset = (uint32_t)out.size();
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, (uint16_t)entries.size());
    put16(out, (uint16_t)entries.size());
    put32(out, (uint32_t)central.size());
    put32(out, central_offset);
    put16(out, 0);
    return out;
}

struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t comp_size = 0;
    uint32_t size = 0;
    uint32_t local_offset = 0;
};

static std::string zip_slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("ZIP: cannot open '" + path + "'");
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The end-of-central-directory record sits at the tail, after a comment of
// unknown length, so it is found by scanning backwards for its signature.
static std::vector<ZipEntry> zip_scan(const std::string& buf) {
    if (buf.size() < 22) throw std::runtime_error("ZIP: file is too small to be an archive");
    size_t eocd = std::string::npos;
    size_t lowest = buf.size() >= 65557 ? buf.size() - 65557 : 0;
    for (size_t i = buf.size() - 22; ; i--) {
        if (get32(buf, i) == 0x06054b50) { eocd = i; break; }
        if (i == lowest) break;
    }
    if (eocd == std::string::npos)
        throw std::runtime_error("ZIP: no end-of-central-directory record - not a zip archive");

    uint16_t count = get16(buf, eocd + 10);
    uint32_t cd_offset = get32(buf, eocd + 16);

    std::vector<ZipEntry> entries;
    size_t p = cd_offset;
    for (uint16_t i = 0; i < count; i++) {
        if (p + 46 > buf.size() || get32(buf, p) != 0x02014b50)
            throw std::runtime_error("ZIP: damaged central directory");
        ZipEntry e;
        e.method = get16(buf, p + 10);
        e.comp_size = get32(buf, p + 20);
        e.size = get32(buf, p + 24);
        uint16_t name_len = get16(buf, p + 28);
        uint16_t extra_len = get16(buf, p + 30);
        uint16_t comment_len = get16(buf, p + 32);
        e.local_offset = get32(buf, p + 42);
        e.name = buf.substr(p + 46, name_len);
        entries.push_back(e);
        p += 46 + name_len + extra_len + comment_len;
    }
    return entries;
}

static std::string zip_extract(const std::string& buf, const ZipEntry& e) {
    size_t p = e.local_offset;
    if (p + 30 > buf.size() || get32(buf, p) != 0x04034b50)
        throw std::runtime_error("ZIP: damaged local header for '" + e.name + "'");
    uint16_t name_len = get16(buf, p + 26);
    uint16_t extra_len = get16(buf, p + 28);
    size_t data = p + 30 + name_len + extra_len;
    if (data + e.comp_size > buf.size())
        throw std::runtime_error("ZIP: entry '" + e.name + "' runs past the end of the file");

    if (e.method == 0) return buf.substr(data, e.comp_size);
    if (e.method == 8) {
        std::vector<uint8_t> out;
        if (!pdf_extract::tinfl::inflate((const uint8_t*)buf.data() + data, e.comp_size, out, false))
            throw std::runtime_error("ZIP: cannot inflate '" + e.name + "'");
        return std::string(out.begin(), out.end());
    }
    throw std::runtime_error("ZIP: entry '" + e.name + "' uses unsupported method " +
                             std::to_string(e.method));
}

// CODEC.INFLATE$: a raw deflate stream, a zlib stream or a gzip member. With
// no format named, the gzip magic and the zlib header check pick it.
static std::string codec_inflate(const std::string& in, std::string format) {
    const uint8_t* p = (const uint8_t*)in.data();
    size_t n = in.size();
    auto zlib_header_ok = [&]() {
        return n >= 2 && (p[0] & 0x0F) == 8 && (p[0] >> 4) <= 7 && ((p[0] << 8) | p[1]) % 31 == 0;
    };
    if (format.empty()) {
        if (n >= 2 && p[0] == 0x1F && p[1] == 0x8B) format = "gzip";
        else if (zlib_header_ok()) format = "zlib";
        else format = "raw";
    }
    std::vector<uint8_t> out;
    // Inflates one stream and answers how many input bytes it used.
    auto run = [&](const uint8_t* body, size_t len) -> size_t {
        size_t used = 0;
        if (!pdf_extract::tinfl::inflate(body, len, out, false, &used))
            throw std::runtime_error("damaged or truncated deflate data");
        return used;
    };
    if (format == "raw") {
        run(p, n);
    } else if (format == "zlib") {
        if (n < 6 || !zlib_header_ok())
            throw std::runtime_error("not a zlib stream");
        if (p[1] & 0x20)
            throw std::runtime_error("zlib streams with a preset dictionary are not supported");
        size_t at = 2 + run(p + 2, n - 2);
        if (at + 4 > n) throw std::runtime_error("truncated zlib stream");
        uint32_t want = ((uint32_t)p[at] << 24) | ((uint32_t)p[at+1] << 16) |
                        ((uint32_t)p[at+2] << 8) | (uint32_t)p[at+3];
        if (jdb_deflate::adler32(1, out.data(), out.size()) != want)
            throw std::runtime_error("zlib checksum mismatch");
    } else if (format == "gzip") {
        // Members concatenate (RFC 1952). Zero bytes after the last member are
        // padding and skipped, as gzip -d and Python's gzip module do.
        size_t at = 0;
        bool first = true;
        for (;;) {
            if (!first) {
                size_t k = at;
                while (k < n && p[k] == 0) k++;
                if (k == n) break;
                if (n - at < 2 || p[at] != 0x1F || p[at + 1] != 0x8B)
                    throw std::runtime_error("trailing data after the gzip stream");
            }
            if (n - at < 18 || p[at] != 0x1F || p[at + 1] != 0x8B || p[at + 2] != 8)
                throw std::runtime_error(first ? "not a gzip stream" : "damaged gzip member");
            uint8_t flags = p[at + 3];
            size_t h = at + 10;
            if (flags & 4) {
                if (h + 2 > n) throw std::runtime_error("truncated gzip header");
                h += 2 + (size_t)(p[h] | (p[h + 1] << 8));
            }
            if (flags & 8)  { while (h < n && p[h]) h++; h++; }
            if (flags & 16) { while (h < n && p[h]) h++; h++; }
            if (flags & 2) h += 2;
            if (h >= n) throw std::runtime_error("truncated gzip stream");
            size_t start = out.size();
            h += run(p + h, n - h);
            if (h + 8 > n) throw std::runtime_error("truncated gzip stream");
            uint32_t crc = (uint32_t)p[h] | ((uint32_t)p[h+1] << 8) |
                           ((uint32_t)p[h+2] << 16) | ((uint32_t)p[h+3] << 24);
            uint32_t isize = (uint32_t)p[h+4] | ((uint32_t)p[h+5] << 8) |
                             ((uint32_t)p[h+6] << 16) | ((uint32_t)p[h+7] << 24);
            size_t member_len = out.size() - start;
            if (jdb_deflate::crc32(0, out.data() + start, member_len) != crc || (uint32_t)member_len != isize)
                throw std::runtime_error("gzip checksum mismatch");
            at = h + 8;
            first = false;
            if (at == n) break;
        }
    } else {
        throw std::runtime_error("format must be raw, zlib or gzip");
    }
    return std::string(out.begin(), out.end());
}

static std::string ascii_lower(std::string s) {
    for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

void VM::register_codec_builtins() {
    // ── CODEC: Encoding & Hashing ───────────────────────────

    register_native("CODEC.BASE64_ENCODE$", [](const std::vector<Value>& args) -> Value {
        static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string& in = args[0].as_string()->data;
        std::string out;
        int val = 0, bits = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c; bits += 8;
            while (bits >= 0) { out += t[(val >> bits) & 0x3F]; bits -= 6; }
        }
        if (bits > -6) out += t[((val << 8) >> (bits + 8)) & 0x3F];
        while (out.size() % 4) out += '=';
        return Value::make_string(out);
    });

    register_native("CODEC.BASE64_DECODE$", [](const std::vector<Value>& args) -> Value {
        static const int d[] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51
        };
        const std::string& in = args[0].as_string()->data;
        std::string out;
        int val = 0, bits = -8;
        for (unsigned char c : in) {
            if (c == '=' || c >= 128) break;
            int v = d[c]; if (v < 0) continue;
            val = (val << 6) + v; bits += 6;
            if (bits >= 0) { out += (char)((val >> bits) & 0xFF); bits -= 8; }
        }
        return Value::make_string(out);
    });

    register_native("CODEC.SHA256$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(sha256_hex(args[0].as_string()->data));
    });

    register_native("CODEC.CRC32$", [](const std::vector<Value>& args) -> Value {
        const std::string& s = args[0].as_string()->data;
        char hex[9];
        snprintf(hex, sizeof(hex), "%08x", crc32_bytes((const uint8_t*)s.data(), s.size()));
        return Value::make_string(hex);
    });

    // CODEC.CRC32(data$, [running_crc]) -> the CRC-32 as a number; pass the
    // previous result to continue over the next piece.
    register_native("CODEC.CRC32", 1, 2, [](const std::vector<Value>& args) -> Value {
        const std::string& s = args[0].as_string()->data;
        uint32_t running = args.size() >= 2 ? (uint32_t)args[1].to_int() : 0;
        return Value::make_i64((int64_t)jdb_deflate::crc32(running, (const uint8_t*)s.data(), s.size()));
    });

    // CODEC.DEFLATE$(data$, [level], [format$]): level 0-9 (default 6),
    // format zlib (default), gzip or raw. A format may stand in for the level.
    register_native("CODEC.DEFLATE$", 1, 3, [](const std::vector<Value>& args) -> Value {
        const std::string& s = args[0].as_string()->data;
        int64_t level = 6;
        std::string format = "zlib";
        size_t next = 1;
        if (args.size() > next && args[next].type != ValueType::STRING) level = args[next++].to_int();
        if (args.size() > next) format = ascii_lower(args[next++].to_string());
        if (args.size() > next) throw std::runtime_error("CODEC.DEFLATE$: expects data$, [level], [format$]");
        if (level < 0 || level > 9) throw std::runtime_error("CODEC.DEFLATE$: level must be between 0 and 9");
        const uint8_t* p = (const uint8_t*)s.data();
        if (format == "zlib") return Value::make_string(jdb_deflate::zlib_wrap(p, s.size(), (int)level));
        if (format == "gzip") return Value::make_string(jdb_deflate::gzip_wrap(p, s.size(), (int)level));
        if (format == "raw")  return Value::make_string(jdb_deflate::deflate_raw(p, s.size(), (int)level));
        throw std::runtime_error("CODEC.DEFLATE$: format must be raw, zlib or gzip");
    });

    // CODEC.INFLATE$(data$, [format$]): raw, zlib or gzip; detected when omitted.
    register_native("CODEC.INFLATE$", 1, 2, [](const std::vector<Value>& args) -> Value {
        std::string format = args.size() >= 2 ? ascii_lower(args[1].to_string()) : "";
        return Value::make_string(codec_inflate(args[0].as_string()->data, format));
    });

    register_native("CODEC.HMAC$", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string algo = "SHA256";
        if (args.size() > 2) {
            algo = args[2].as_string()->data;
            for (auto& ch : algo) ch = (char)toupper((unsigned char)ch);
            if (algo == "SHA-256") algo = "SHA256";
        }
        if (algo != "SHA256")
            throw std::runtime_error("CODEC.HMAC$: unsupported algorithm '" + algo + "'");
        return Value::make_string(hmac_sha256_hex(args[0].as_string()->data,
                                                  args[1].as_string()->data));
    });

    register_native("CODEC.PBKDF2$", 4, 4, [](const std::vector<Value>& args) -> Value {
        const std::string& password = args[0].as_string()->data;
        const std::string& salt = args[1].as_string()->data;
        int64_t iterations = args[2].to_int();
        int64_t n_bytes = args[3].to_int();
        if (iterations < 1)
            throw std::runtime_error("CODEC.PBKDF2$: iterations must be at least 1");
        if (n_bytes < 1 || n_bytes > jdb_crypto::PBKDF2_BYTES_MAX)
            throw std::runtime_error("CODEC.PBKDF2$: bytes must be between 1 and 65536");
        std::vector<uint8_t> key((size_t)n_bytes);
        jdb_crypto::pbkdf2_hmac_sha256((const uint8_t*)password.data(), password.size(),
                                       (const uint8_t*)salt.data(), salt.size(),
                                       (uint64_t)iterations, key.data(), key.size());
        return Value::make_string(bytes_to_hex(key.data(), key.size()));
    });

    // ── ZIP archives ────────────────────────────────────────

    register_native("ZIP.WRITE", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
        auto* o = args[1].as_object();
        if (!o) throw std::runtime_error("ZIP.WRITE: second argument must be a map of name to content");
        std::vector<std::pair<std::string, std::string>> entries;
        for (auto& [k, v] : o->fields) entries.push_back({k, v.to_string()});
        std::string blob = zip_build(entries);
        std::ofstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("ZIP.WRITE: cannot write '" + path + "'");
        f.write(blob.data(), (std::streamsize)blob.size());
        return Value::make_i64((int64_t)entries.size());
    });

    register_native("ZIP.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string buf = zip_slurp(args[0].as_string()->data);
        Value m = Value::make_object();
        auto* o = m.as_object();
        for (auto& e : zip_scan(buf)) {
            if (!e.name.empty() && e.name.back() == '/') continue;  // directory marker
            o->set(e.name, Value::make_string(zip_extract(buf, e)));
        }
        return m;
    });

    register_native("ZIP.LIST", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string buf = zip_slurp(args[0].as_string()->data);
        Value r = Value::make_array();
        for (auto& e : zip_scan(buf))
            r.as_array()->elements.push_back(Value::make_string(e.name));
        return r;
    });

#ifndef JDB_LEAN
    register_native("CODEC.UUID$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        uint8_t bytes[16];
        if (!jdb_crypto::random_bytes(bytes, sizeof(bytes)))
            throw std::runtime_error("CODEC.UUID$: the system random source failed");
        char buf[37];
        jdb_crypto::uuid4_format(bytes, buf);
        return Value::make_string(buf);
    });

    register_native("CODEC.RANDOMBYTES$", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        if (n < 0 || n > jdb_crypto::RANDOM_BYTES_MAX)
            throw std::runtime_error("CODEC.RANDOMBYTES$: count must be between 0 and 1048576");
        std::string out((size_t)n, '\0');
        if (n > 0 && !jdb_crypto::random_bytes((uint8_t*)&out[0], out.size()))
            throw std::runtime_error("CODEC.RANDOMBYTES$: the system random source failed");
        return Value::make_string(std::move(out));
    });
#endif
}
