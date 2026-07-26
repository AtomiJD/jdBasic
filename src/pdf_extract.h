// Minimal PDF text extractor - header-only.
//
// Supported:
//  - uncompressed streams
//  - FlateDecode (zlib/deflate)         <-- via the built-in tinfl
//  - ASCIIHexDecode, ASCII85Decode      <-- simple filters
//  - text operators Tj, TJ, ', "
//  - hex strings <ABCD>
//  - literal strings (with escapes)
//
// Not supported: LZW, RunLength, CCITT, DCT (JPEG), JBIG2, encrypted PDFs,
// CMaps for CID fonts, complex encoding tables. For such files the extractor
// returns whatever it could decode and silently ignores the rest.
//
// No C++ language extensions - compiles with plain C++17.

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace pdf_extract {

// ─────────────────────────────────────────────────────────────────────────
// tinfl: compact inflate decoder (public domain, based on miniz/tinfl)
// Only the decoder half, boiled down for header-only use.
// ─────────────────────────────────────────────────────────────────────────

namespace tinfl {

constexpr int FLAG_PARSE_ZLIB_HEADER = 1;
constexpr int FLAG_HAS_MORE_INPUT    = 2;
constexpr int FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4;
constexpr int FLAG_COMPUTE_ADLER32   = 8;

// Very simple but complete inflate. No streaming, everything in memory.
// Input: deflate stream (or zlib with header). Output: decompressed bytes.
// Returns: true = ok, false = error.
inline bool inflate(const uint8_t* in, size_t in_size, std::vector<uint8_t>& out, bool zlib_header = true) {
    if (in_size < 2) return false;

    size_t ip = 0;
    if (zlib_header) {
        // CMF + FLG, optional FDICT/FCHECK - we only check for deflate (CM=8)
        if ((in[0] & 0x0F) != 8) return false;
        ip = 2;
        if (in[1] & 0x20) {
            // FDICT - preset dictionaries are not supported
            return false;
        }
    }

    // Bit-Reader
    uint64_t bit_buf = 0;
    int bit_count = 0;

    auto need_bits = [&](int n) -> bool {
        while (bit_count < n) {
            if (ip >= in_size) return false;
            bit_buf |= ((uint64_t)in[ip++]) << bit_count;
            bit_count += 8;
        }
        return true;
    };
    auto get_bits = [&](int n) -> uint32_t {
        if (!need_bits(n)) return 0xFFFFFFFFu;
        uint32_t v = (uint32_t)(bit_buf & ((1ULL << n) - 1));
        bit_buf >>= n;
        bit_count -= n;
        return v;
    };

    // Huffman table: code -> symbol via a simple lookup
    struct Huff {
        std::vector<int> count;   // count[len] = number of codes of len
        std::vector<int> symbol;  // sorted symbols
    };

    auto build_huff = [](Huff& h, const std::vector<int>& lens) -> bool {
        int max_len = 0;
        for (int l : lens) if (l > max_len) max_len = l;
        h.count.assign(max_len + 1, 0);
        for (int l : lens) if (l > 0) h.count[l]++;
        std::vector<int> offs(max_len + 2, 0);
        for (int l = 1; l <= max_len; l++) offs[l + 1] = offs[l] + h.count[l];
        h.symbol.assign((int)lens.size(), 0);
        for (int sym = 0; sym < (int)lens.size(); sym++) {
            int l = lens[sym];
            if (l > 0) h.symbol[offs[l]++] = sym;
        }
        return true;
    };

    auto decode_huff = [&](const Huff& h) -> int {
        int code = 0, first = 0, idx = 0;
        for (int len = 1; len < (int)h.count.size(); len++) {
            int b = (int)get_bits(1);
            if (b == (int)0xFFFFFFFFu) return -1;
            code |= b;
            int cnt = h.count[len];
            if (code - cnt < first) {
                return h.symbol[idx + (code - first)];
            }
            idx += cnt;
            first += cnt;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    };

    // Static Huffman tables (RFC 1951 §3.2.6)
    auto build_static = [&](Huff& lit, Huff& dist) {
        std::vector<int> ll(288), dl(30);
        for (int i = 0;   i <= 143; i++) ll[i] = 8;
        for (int i = 144; i <= 255; i++) ll[i] = 9;
        for (int i = 256; i <= 279; i++) ll[i] = 7;
        for (int i = 280; i <= 287; i++) ll[i] = 8;
        for (int i = 0; i < 30; i++) dl[i] = 5;
        build_huff(lit, ll);
        build_huff(dist, dl);
    };

    // Length and distance base tables (RFC 1951 section 3.2.5)
    static const int LEN_BASE[] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const int LEN_EXTRA[] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int DIST_BASE[] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,
        8193,12289,16385,24577
    };
    static const int DIST_EXTRA[] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    bool last_block = false;
    while (!last_block) {
        if (!need_bits(3)) return false;
        last_block = (get_bits(1) != 0);
        int btype = (int)get_bits(2);

        if (btype == 0) {
            // stored: byte-align, 2 bytes len, 2 bytes nlen, len bytes data
            bit_buf >>= bit_count & 7;
            bit_count &= ~7;
            if (ip + 4 > in_size) return false;
            uint16_t len  = (uint16_t)(in[ip] | (in[ip+1]<<8));
            uint16_t nlen = (uint16_t)(in[ip+2] | (in[ip+3]<<8));
            ip += 4;
            if ((uint16_t)~len != nlen) return false;
            if (ip + len > in_size) return false;
            out.insert(out.end(), in + ip, in + ip + len);
            ip += len;
            bit_buf = 0; bit_count = 0;
            continue;
        }

        Huff lit_h, dist_h;
        if (btype == 1) {
            build_static(lit_h, dist_h);
        } else if (btype == 2) {
            int hlit  = (int)get_bits(5) + 257;
            int hdist = (int)get_bits(5) + 1;
            int hclen = (int)get_bits(4) + 4;
            static const int CODE_ORDER[19] = {
                16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
            };
            std::vector<int> code_lens(19, 0);
            for (int i = 0; i < hclen; i++) code_lens[CODE_ORDER[i]] = (int)get_bits(3);
            Huff code_h;
            build_huff(code_h, code_lens);

            std::vector<int> lens(hlit + hdist, 0);
            int idx = 0;
            while (idx < (int)lens.size()) {
                int sym = decode_huff(code_h);
                if (sym < 0) return false;
                if (sym < 16) {
                    lens[idx++] = sym;
                } else if (sym == 16) {
                    if (idx == 0) return false;
                    int cnt = (int)get_bits(2) + 3;
                    int prev = lens[idx - 1];
                    while (cnt-- > 0 && idx < (int)lens.size()) lens[idx++] = prev;
                } else if (sym == 17) {
                    int cnt = (int)get_bits(3) + 3;
                    while (cnt-- > 0 && idx < (int)lens.size()) lens[idx++] = 0;
                } else { // 18
                    int cnt = (int)get_bits(7) + 11;
                    while (cnt-- > 0 && idx < (int)lens.size()) lens[idx++] = 0;
                }
            }
            std::vector<int> ll(lens.begin(), lens.begin() + hlit);
            std::vector<int> dl(lens.begin() + hlit, lens.end());
            build_huff(lit_h, ll);
            build_huff(dist_h, dl);
        } else {
            return false;
        }

        // Symbole dekodieren
        for (;;) {
            int sym = decode_huff(lit_h);
            if (sym < 0) return false;
            if (sym < 256) {
                out.push_back((uint8_t)sym);
            } else if (sym == 256) {
                break;
            } else {
                int len_idx = sym - 257;
                if (len_idx < 0 || len_idx >= 29) return false;
                int len = LEN_BASE[len_idx];
                if (LEN_EXTRA[len_idx]) len += (int)get_bits(LEN_EXTRA[len_idx]);

                int dsym = decode_huff(dist_h);
                if (dsym < 0 || dsym >= 30) return false;
                int dist = DIST_BASE[dsym];
                if (DIST_EXTRA[dsym]) dist += (int)get_bits(DIST_EXTRA[dsym]);

                if (dist > (int)out.size()) return false;
                size_t src = out.size() - dist;
                for (int i = 0; i < len; i++) out.push_back(out[src + i]);
            }
        }
    }
    return true;
}

} // namespace tinfl

// ─────────────────────────────────────────────────────────────────────────
// PDF-Stream-Parser
// ─────────────────────────────────────────────────────────────────────────

inline std::string read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Search the byte stream for the next occurrence of needle, starting at pos
inline size_t find_seq(const std::string& hay, const std::string& needle, size_t pos = 0) {
    return hay.find(needle, pos);
}

// Extract all stream...endstream blocks from the PDF
struct PdfStream {
    size_t dict_start;
    size_t dict_end;
    size_t data_start;
    size_t data_end;
    std::string filter;       // FlateDecode, ASCIIHexDecode, or empty
    bool has_filter = false;
};

inline std::vector<PdfStream> find_streams(const std::string& pdf) {
    std::vector<PdfStream> streams;
    size_t pos = 0;
    while (true) {
        size_t s = pdf.find("stream", pos);
        if (s == std::string::npos) break;
        // "stream" muss von einem Newline gefolgt werden
        size_t data_start = s + 6;
        if (data_start < pdf.size() && pdf[data_start] == '\r') data_start++;
        if (data_start < pdf.size() && pdf[data_start] == '\n') data_start++;

        // endstream finden
        size_t e = pdf.find("endstream", data_start);
        if (e == std::string::npos) break;

        // Vor dem endstream ggf. ein Newline strippen
        size_t data_end = e;
        if (data_end > data_start && pdf[data_end - 1] == '\n') data_end--;
        if (data_end > data_start && pdf[data_end - 1] == '\r') data_end--;

        // Find the dictionary before it: scan backwards for << ... >>
        size_t dict_end = pdf.rfind(">>", s);
        size_t dict_start = (dict_end != std::string::npos) ? pdf.rfind("<<", dict_end) : std::string::npos;

        PdfStream st;
        st.dict_start = dict_start;
        st.dict_end = dict_end;
        st.data_start = data_start;
        st.data_end = data_end;
        if (dict_start != std::string::npos && dict_end != std::string::npos) {
            std::string dict = pdf.substr(dict_start, dict_end - dict_start);
            // Filter erkennen
            size_t fpos = dict.find("/Filter");
            if (fpos != std::string::npos) {
                st.has_filter = true;
                // simple parse: find the first /XxxDecode
                size_t slash = dict.find('/', fpos + 7);
                if (slash != std::string::npos) {
                    size_t end = slash + 1;
                    while (end < dict.size() && (std::isalnum((unsigned char)dict[end]) || dict[end] == '8' || dict[end] == '5')) end++;
                    st.filter = dict.substr(slash + 1, end - slash - 1);
                }
            }
        }
        streams.push_back(st);
        pos = e + 9;
    }
    return streams;
}

// Parse a PDF string literal: (text with \\escapes)
// Returns the extracted text plus the position after the closing ')'
inline std::pair<std::string, size_t> parse_pdf_string(const std::string& s, size_t pos) {
    std::string out;
    int depth = 1;
    pos++; // skip opening (
    while (pos < s.size() && depth > 0) {
        char c = s[pos];
        if (c == '\\' && pos + 1 < s.size()) {
            char esc = s[pos + 1];
            switch (esc) {
                case 'n': out += '\n'; pos += 2; break;
                case 'r': out += '\r'; pos += 2; break;
                case 't': out += '\t'; pos += 2; break;
                case 'b': out += '\b'; pos += 2; break;
                case 'f': out += '\f'; pos += 2; break;
                case '(': out += '('; pos += 2; break;
                case ')': out += ')'; pos += 2; break;
                case '\\': out += '\\'; pos += 2; break;
                default:
                    if (esc >= '0' && esc <= '7') {
                        // Oktal-Escape \ddd
                        int v = 0, n = 0;
                        pos++;
                        while (n < 3 && pos < s.size() && s[pos] >= '0' && s[pos] <= '7') {
                            v = v * 8 + (s[pos] - '0');
                            pos++; n++;
                        }
                        out += (char)v;
                    } else {
                        pos += 2;
                    }
                    break;
            }
        } else if (c == '(') {
            depth++; out += c; pos++;
        } else if (c == ')') {
            depth--;
            if (depth > 0) out += c;
            pos++;
        } else {
            out += c; pos++;
        }
    }
    return {out, pos};
}

// PDF-Hex-String parsen: <ABCD>
inline std::pair<std::string, size_t> parse_hex_string(const std::string& s, size_t pos) {
    std::string out;
    pos++; // skip <
    std::string hex;
    while (pos < s.size() && s[pos] != '>') {
        char c = s[pos++];
        if (std::isxdigit((unsigned char)c)) hex += c;
    }
    if (pos < s.size()) pos++; // skip >
    if (hex.size() % 2 == 1) hex += '0';
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = std::isdigit((unsigned char)hex[i])   ? hex[i]   - '0' : (std::tolower((unsigned char)hex[i])   - 'a' + 10);
        int lo = std::isdigit((unsigned char)hex[i+1]) ? hex[i+1] - '0' : (std::tolower((unsigned char)hex[i+1]) - 'a' + 10);
        out += (char)((hi << 4) | lo);
    }
    return {out, pos};
}

// Extract all Tj/TJ texts from a decompressed content stream
inline std::string extract_text_from_content(const std::string& content) {
    std::string out;
    size_t i = 0;
    while (i < content.size()) {
        char c = content[i];
        if (c == '(') {
            auto [text, np] = parse_pdf_string(content, i);
            // Find the next operator: Tj, TJ, ', "
            size_t op = np;
            while (op < content.size() && (content[op] == ' ' || content[op] == '\t' || content[op] == '\r' || content[op] == '\n')) op++;
            // Accept the text unconditionally (a bit liberal, but works)
            out += text;
            // Newline after Tj-style operators
            if (op < content.size() && (content[op] == 'T' || content[op] == '\'' || content[op] == '"')) {
                out += ' ';
            }
            i = np;
        } else if (c == '<' && i + 1 < content.size() && content[i+1] != '<') {
            auto [text, np] = parse_hex_string(content, i);
            out += text;
            out += ' ';
            i = np;
        } else if (c == '[') {
            // TJ array: [(text) num (more) ...] TJ
            i++;
            std::string buf;
            while (i < content.size() && content[i] != ']') {
                if (content[i] == '(') {
                    auto [text, np] = parse_pdf_string(content, i);
                    buf += text;
                    i = np;
                } else if (content[i] == '<') {
                    auto [text, np] = parse_hex_string(content, i);
                    buf += text;
                    i = np;
                } else {
                    i++;
                }
            }
            if (i < content.size()) i++; // skip ]
            out += buf;
            out += ' ';
        } else {
            i++;
        }
    }
    return out;
}

// Collects text only from BT..ET blocks. Per spec, text-showing operators are
// only valid inside them, so binary streams (fonts, images) that happen to
// contain the byte pairs "BT"/"Tj" cannot leak garbage into the output.
inline std::string extract_text_blocks(const std::string& content) {
    std::string out;
    auto delim_after = [&](size_t idx) {
        if (idx >= content.size()) return true;
        unsigned char c = content[idx];
        return std::isspace(c) != 0 || c == '/' || c == '[' || c == '(' || c == '<';
    };
    size_t pos = 0;
    while (true) {
        size_t bt = content.find("BT", pos);
        if (bt == std::string::npos) break;
        bool ok_before = (bt == 0) || std::isspace((unsigned char)content[bt - 1]) != 0;
        if (!ok_before || !delim_after(bt + 2)) { pos = bt + 2; continue; }
        size_t et = content.find("ET", bt + 2);
        while (et != std::string::npos) {
            if (std::isspace((unsigned char)content[et - 1]) != 0 && delim_after(et + 2)) break;
            et = content.find("ET", et + 2);
        }
        if (et == std::string::npos) break;
        out += extract_text_from_content(content.substr(bt + 2, et - bt - 2));
        pos = et + 2;
    }
    return out;
}

// Main API: read a PDF file and extract its text
inline std::string extract_text(const std::string& filepath) {
    std::string pdf = read_file_bytes(filepath);
    if (pdf.empty()) return {};
    if (pdf.substr(0, 4) != "%PDF") return {};

    auto streams = find_streams(pdf);
    std::string all_text;

    for (auto& st : streams) {
        std::string raw = pdf.substr(st.data_start, st.data_end - st.data_start);
        std::string content;

        if (!st.has_filter) {
            content = raw;
        } else if (st.filter == "FlateDecode" || st.filter == "Fl") {
            std::vector<uint8_t> decoded;
            if (tinfl::inflate((const uint8_t*)raw.data(), raw.size(), decoded, true)) {
                content.assign((const char*)decoded.data(), decoded.size());
            } else {
                continue; // skip - cannot decompress this stream
            }
        } else if (st.filter == "ASCIIHexDecode" || st.filter == "AHx") {
            for (size_t i = 0; i + 1 < raw.size(); ) {
                while (i < raw.size() && std::isspace((unsigned char)raw[i])) i++;
                if (i + 1 >= raw.size() || raw[i] == '>') break;
                int hi = std::isdigit((unsigned char)raw[i])   ? raw[i]   - '0' : (std::tolower((unsigned char)raw[i])   - 'a' + 10);
                int lo = std::isdigit((unsigned char)raw[i+1]) ? raw[i+1] - '0' : (std::tolower((unsigned char)raw[i+1]) - 'a' + 10);
                content += (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            // unknown filter (LZW, DCT, JBIG2, ...) - skip
            continue;
        }

        // Pull the text from the (now decompressed) content, strictly from
        // BT..ET text blocks
        std::string txt = extract_text_blocks(content);
        if (!txt.empty()) {
            all_text += txt;
            all_text += '\n';
        }
    }

    return all_text;
}

} // namespace pdf_extract
