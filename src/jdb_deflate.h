// jdb_deflate.h - a DEFLATE encoder (RFC 1951) with the zlib (RFC 1950) and
// gzip (RFC 1952) wrappers, plus CRC-32 and Adler-32. Decoding lives in
// pdf_extract.h (tinfl).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace jdb_deflate {

inline uint32_t crc32(uint32_t running, const uint8_t* data, size_t len) {
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
    uint32_t c = running ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline uint32_t adler32(uint32_t running, const uint8_t* data, size_t len) {
    uint32_t a = running & 0xFFFF, b = (running >> 16) & 0xFFFF;
    while (len > 0) {
        size_t n = len < 5552 ? len : 5552;
        len -= n;
        while (n--) { a += *data++; b += a; }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

class BitWriter {
public:
    std::string out;
    void put(uint32_t bits, int count) {
        acc |= (uint64_t)bits << fill;
        fill += count;
        while (fill >= 8) {
            out += (char)(acc & 0xFF);
            acc >>= 8;
            fill -= 8;
        }
    }
    // Huffman codes are defined most significant bit first.
    void put_code(uint32_t code, int len) {
        uint32_t rev = 0;
        for (int i = 0; i < len; i++) rev |= ((code >> i) & 1) << (len - 1 - i);
        put(rev, len);
    }
    void align() {
        if (fill > 0) put(0, 8 - fill);
    }
private:
    uint64_t acc = 0;
    int fill = 0;
};

constexpr int LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
constexpr int LEN_EXTRA[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
constexpr int DIST_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
constexpr int DIST_EXTRA[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

inline int length_code(int len) {
    int i = 28;
    while (LEN_BASE[i] > len) i--;
    return i;
}

inline int dist_code(int dist) {
    int i = 29;
    while (DIST_BASE[i] > dist) i--;
    return i;
}

// Code lengths of a Huffman code for freqs, no longer than max_len: the
// Huffman depths, capped with zlib's overflow repair, then handed out again
// so the most frequent symbols keep the shortest codes.
inline std::vector<int> code_lengths(const std::vector<uint32_t>& freqs, int max_len) {
    size_t n = freqs.size();
    std::vector<int> lens(n, 0);
    std::vector<int> used;
    for (size_t i = 0; i < n; i++) if (freqs[i] > 0) used.push_back((int)i);
    if (used.empty()) return lens;
    if (used.size() == 1) {
        lens[used[0]] = 1;
        lens[used[0] == 0 ? 1 : 0] = 1;
        return lens;
    }
    struct Node { uint64_t freq; int left, right, symbol; };
    std::vector<Node> nodes;
    std::vector<int> heap;
    for (int s : used) { nodes.push_back({freqs[s], -1, -1, s}); heap.push_back((int)nodes.size() - 1); }
    auto greater = [&](int a, int b) {
        if (nodes[a].freq != nodes[b].freq) return nodes[a].freq > nodes[b].freq;
        return a > b;
    };
    std::make_heap(heap.begin(), heap.end(), greater);
    while (heap.size() > 1) {
        std::pop_heap(heap.begin(), heap.end(), greater); int a = heap.back(); heap.pop_back();
        std::pop_heap(heap.begin(), heap.end(), greater); int b = heap.back(); heap.pop_back();
        nodes.push_back({nodes[a].freq + nodes[b].freq, a, b, -1});
        heap.push_back((int)nodes.size() - 1);
        std::push_heap(heap.begin(), heap.end(), greater);
    }
    std::vector<int> depth(nodes.size(), 0);
    std::vector<int> stack = {heap[0]};
    std::vector<int> bl_count(max_len + 1, 0);
    int overflow = 0;
    while (!stack.empty()) {
        int id = stack.back(); stack.pop_back();
        if (nodes[id].symbol >= 0) {
            int d = depth[id];
            if (d > max_len) { d = max_len; overflow++; }
            bl_count[d]++;
        } else {
            depth[nodes[id].left] = depth[id] + 1;
            depth[nodes[id].right] = depth[id] + 1;
            stack.push_back(nodes[id].left);
            stack.push_back(nodes[id].right);
        }
    }
    while (overflow > 0) {
        int bits = max_len - 1;
        while (bl_count[bits] == 0) bits--;
        bl_count[bits]--;
        bl_count[bits + 1] += 2;
        bl_count[max_len]--;
        overflow -= 2;
    }
    std::vector<int> order = used;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return freqs[a] < freqs[b]; });
    size_t k = 0;
    for (int len = max_len; len >= 1; len--)
        for (int c = 0; c < bl_count[len]; c++) lens[order[k++]] = len;
    return lens;
}

inline std::vector<uint32_t> canonical_codes(const std::vector<int>& lens) {
    int max_len = 0;
    for (int l : lens) max_len = std::max(max_len, l);
    std::vector<int> bl_count(max_len + 1, 0);
    for (int l : lens) if (l > 0) bl_count[l]++;
    std::vector<uint32_t> next(max_len + 2, 0);
    uint32_t code = 0;
    for (int bits = 1; bits <= max_len; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next[bits] = code;
    }
    std::vector<uint32_t> codes(lens.size(), 0);
    for (size_t i = 0; i < lens.size(); i++)
        if (lens[i] > 0) codes[i] = next[lens[i]]++;
    return codes;
}

// One LZ77 symbol: a literal (dist 0) or a match of len bytes dist back.
struct Symbol { uint16_t len; uint16_t dist; uint8_t literal; };

class Encoder {
public:
    Encoder(const uint8_t* data, size_t size, int level) : data_(data), size_(size), level_(level) {}

    std::string run() {
        if (level_ <= 0 || size_ == 0) return stored_only();
        static const int CHAIN[10] = {0, 4, 8, 16, 32, 64, 128, 256, 1024, 4096};
        int max_chain = CHAIN[std::min(level_, 9)];
        bool lazy = level_ >= 4;
        head_.assign(HASH_SIZE, -1);
        prev_.assign(WINDOW, -1);
        size_t pos = 0;
        size_t block_start = 0;
        std::vector<Symbol> syms;
        while (pos < size_) {
            int best_len = 0, best_dist = 0;
            find_match(pos, max_chain, best_len, best_dist);
            if (lazy && best_len >= 3 && best_len < 32 && pos + 1 < size_) {
                insert(pos);
                int next_len = 0, next_dist = 0;
                find_match(pos + 1, max_chain, next_len, next_dist);
                if (next_len > best_len) {
                    syms.push_back({0, 0, data_[pos]});
                    pos++;
                    best_len = next_len;
                    best_dist = next_dist;
                }
            }
            if (best_len >= 3) {
                syms.push_back({(uint16_t)best_len, (uint16_t)best_dist, 0});
                for (int i = 0; i < best_len; i++) insert(pos + i);
                pos += best_len;
            } else {
                syms.push_back({0, 0, data_[pos]});
                insert(pos);
                pos++;
            }
            if (syms.size() >= 32768) {
                write_block(syms, block_start, pos, false);
                syms.clear();
                block_start = pos;
            }
        }
        write_block(syms, block_start, pos, true);
        bits_.align();
        return bits_.out;
    }

private:
    static constexpr int WINDOW = 32768;
    static constexpr int HASH_SIZE = 1 << 15;
    const uint8_t* data_;
    size_t size_;
    int level_;
    std::vector<int32_t> head_, prev_;
    std::vector<int32_t> inserted_;
    BitWriter bits_;

    uint32_t hash_at(size_t p) const {
        return ((uint32_t)data_[p] << 10 ^ (uint32_t)data_[p + 1] << 5 ^ data_[p + 2]) & (HASH_SIZE - 1);
    }

    void insert(size_t p) {
        if (p + 2 >= size_) return;
        if (!inserted_.empty() && inserted_.back() >= (int32_t)p) return;
        uint32_t h = hash_at(p);
        prev_[p & (WINDOW - 1)] = head_[h];
        head_[h] = (int32_t)p;
        inserted_.assign(1, (int32_t)p);
    }

    void find_match(size_t p, int max_chain, int& best_len, int& best_dist) const {
        best_len = 0;
        best_dist = 0;
        if (p + 2 >= size_) return;
        int limit = (int)std::min<size_t>(258, size_ - p);
        int32_t cand = head_[hash_at(p)];
        int chain = max_chain;
        while (cand >= 0 && chain-- > 0) {
            size_t c = (size_t)cand;
            if (c >= p || p - c > (size_t)WINDOW - 1) break;
            if (data_[c + best_len] == data_[p + best_len] && data_[c] == data_[p]) {
                int len = 0;
                while (len < limit && data_[c + len] == data_[p + len]) len++;
                if (len > best_len) {
                    best_len = len;
                    best_dist = (int)(p - c);
                    if (len == limit) break;
                }
            }
            int32_t next = prev_[c & (WINDOW - 1)];
            if (next >= cand) break;
            cand = next;
        }
        if (best_len < 3) best_len = 0;
    }

    std::string stored_only() {
        size_t pos = 0;
        do {
            size_t n = std::min<size_t>(65535, size_ - pos);
            bool last = pos + n >= size_;
            write_stored(pos, n, last);
            pos += n;
        } while (pos < size_);
        return bits_.out;
    }

    void write_stored(size_t start, size_t n, bool last) {
        bits_.put(last ? 1 : 0, 1);
        bits_.put(0, 2);
        bits_.align();
        bits_.put((uint32_t)n, 16);
        bits_.put((uint32_t)(~n & 0xFFFF), 16);
        bits_.out.append((const char*)data_ + start, n);
    }

    void write_block(const std::vector<Symbol>& syms, size_t start, size_t end, bool last) {
        std::vector<uint32_t> lit_freq(286, 0), dist_freq(30, 0);
        uint64_t extra_bits = 0;
        for (auto& s : syms) {
            if (s.dist == 0) lit_freq[s.literal]++;
            else {
                int lc = length_code(s.len), dc = dist_code(s.dist);
                lit_freq[257 + lc]++;
                dist_freq[dc]++;
                extra_bits += LEN_EXTRA[lc] + DIST_EXTRA[dc];
            }
        }
        lit_freq[256] = 1;

        std::vector<int> lit_len = code_lengths(lit_freq, 15);
        std::vector<int> dist_len = code_lengths(dist_freq, 15);
        bool any_dist = false;
        for (int l : dist_len) if (l > 0) any_dist = true;
        if (!any_dist) dist_len[0] = 1;

        int hlit = 286;
        while (hlit > 257 && lit_len[hlit - 1] == 0) hlit--;
        int hdist = 30;
        while (hdist > 1 && dist_len[hdist - 1] == 0) hdist--;
        std::vector<int> all(lit_len.begin(), lit_len.begin() + hlit);
        all.insert(all.end(), dist_len.begin(), dist_len.begin() + hdist);

        struct Rle { int symbol; int extra; };
        std::vector<Rle> rle;
        for (size_t i = 0; i < all.size();) {
            int v = all[i];
            size_t run = 1;
            while (i + run < all.size() && all[i + run] == v) run++;
            if (v == 0 && run >= 3) {
                size_t r = run;
                while (r >= 11) { int n = (int)std::min<size_t>(138, r); rle.push_back({18, n - 11}); r -= n; }
                if (r >= 3) { rle.push_back({17, (int)r - 3}); r = 0; }
                while (r > 0) { rle.push_back({0, 0}); r--; }
                i += run;
            } else if (v != 0 && run >= 4) {
                rle.push_back({v, 0});
                size_t r = run - 1;
                while (r >= 3) { int n = (int)std::min<size_t>(6, r); rle.push_back({16, n - 3}); r -= n; }
                while (r > 0) { rle.push_back({v, 0}); r--; }
                i += run;
            } else {
                rle.push_back({v, 0});
                i++;
            }
        }
        std::vector<uint32_t> cl_freq(19, 0);
        for (auto& r : rle) cl_freq[r.symbol]++;
        std::vector<int> cl_len = code_lengths(cl_freq, 7);
        static const int CL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int hclen = 19;
        while (hclen > 4 && cl_len[CL_ORDER[hclen - 1]] == 0) hclen--;

        uint64_t dyn_bits = 3 + 5 + 5 + 4 + 3 * (uint64_t)hclen;
        for (auto& r : rle) {
            dyn_bits += cl_len[r.symbol];
            if (r.symbol == 16) dyn_bits += 2;
            else if (r.symbol == 17) dyn_bits += 3;
            else if (r.symbol == 18) dyn_bits += 7;
        }
        for (int s = 0; s < 286; s++) dyn_bits += (uint64_t)lit_freq[s] * lit_len[s];
        for (int s = 0; s < 30; s++) dyn_bits += (uint64_t)dist_freq[s] * dist_len[s];
        dyn_bits += extra_bits;

        std::vector<int> fix_lit(288), fix_dist(30, 5);
        for (int s = 0; s < 288; s++) fix_lit[s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
        uint64_t fix_bits = 3 + extra_bits;
        for (int s = 0; s < 286; s++) fix_bits += (uint64_t)lit_freq[s] * fix_lit[s];
        for (int s = 0; s < 30; s++) fix_bits += (uint64_t)dist_freq[s] * 5;

        size_t raw = end - start;
        uint64_t stored_bits = 8 * ((uint64_t)raw + 5 * ((raw + 65534) / 65535 + (raw == 0 ? 1 : 0))) + 8;

        if (stored_bits <= dyn_bits && stored_bits <= fix_bits && raw > 0) {
            size_t pos = start;
            while (pos < end) {
                size_t n = std::min<size_t>(65535, end - pos);
                write_stored(pos, n, last && pos + n >= end);
                pos += n;
            }
            return;
        }
        if (fix_bits <= dyn_bits) {
            bits_.put(last ? 1 : 0, 1);
            bits_.put(1, 2);
            write_symbols(syms, fix_lit, canonical_codes(fix_lit), fix_dist, canonical_codes(fix_dist));
            return;
        }
        bits_.put(last ? 1 : 0, 1);
        bits_.put(2, 2);
        bits_.put(hlit - 257, 5);
        bits_.put(hdist - 1, 5);
        bits_.put(hclen - 4, 4);
        for (int i = 0; i < hclen; i++) bits_.put(cl_len[CL_ORDER[i]], 3);
        std::vector<uint32_t> cl_codes = canonical_codes(cl_len);
        for (auto& r : rle) {
            bits_.put_code(cl_codes[r.symbol], cl_len[r.symbol]);
            if (r.symbol == 16) bits_.put(r.extra, 2);
            else if (r.symbol == 17) bits_.put(r.extra, 3);
            else if (r.symbol == 18) bits_.put(r.extra, 7);
        }
        write_symbols(syms, lit_len, canonical_codes(lit_len), dist_len, canonical_codes(dist_len));
    }

    void write_symbols(const std::vector<Symbol>& syms, const std::vector<int>& lit_len,
                       const std::vector<uint32_t>& lit_code, const std::vector<int>& dist_len,
                       const std::vector<uint32_t>& dist_code_bits) {
        for (auto& s : syms) {
            if (s.dist == 0) {
                bits_.put_code(lit_code[s.literal], lit_len[s.literal]);
            } else {
                int lc = length_code(s.len), dc = dist_code(s.dist);
                bits_.put_code(lit_code[257 + lc], lit_len[257 + lc]);
                if (LEN_EXTRA[lc]) bits_.put(s.len - LEN_BASE[lc], LEN_EXTRA[lc]);
                bits_.put_code(dist_code_bits[dc], dist_len[dc]);
                if (DIST_EXTRA[dc]) bits_.put(s.dist - DIST_BASE[dc], DIST_EXTRA[dc]);
            }
        }
        bits_.put_code(lit_code[256], lit_len[256]);
    }
};

// A raw DEFLATE stream at level 0 (stored) to 9.
inline std::string deflate_raw(const uint8_t* data, size_t size, int level) {
    return Encoder(data, size, level).run();
}

inline std::string zlib_wrap(const uint8_t* data, size_t size, int level) {
    uint8_t cmf = 0x78;
    int flevel = level <= 1 ? 0 : level <= 5 ? 1 : level == 6 ? 2 : 3;
    uint8_t flg = (uint8_t)(flevel << 6);
    flg = (uint8_t)(flg + (31 - ((cmf * 256 + flg) % 31)) % 31);
    std::string out;
    out += (char)cmf;
    out += (char)flg;
    out += deflate_raw(data, size, level);
    uint32_t a = adler32(1, data, size);
    out += (char)(a >> 24); out += (char)(a >> 16); out += (char)(a >> 8); out += (char)a;
    return out;
}

inline std::string gzip_wrap(const uint8_t* data, size_t size, int level) {
    std::string out;
    const uint8_t header[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0,
                                (uint8_t)(level >= 9 ? 2 : level == 1 ? 4 : 0), 0xff};
    out.append((const char*)header, 10);
    out += deflate_raw(data, size, level);
    uint32_t c = crc32(0, data, size);
    for (int i = 0; i < 4; i++) out += (char)(c >> (8 * i));
    uint32_t n = (uint32_t)size;
    for (int i = 0; i < 4; i++) out += (char)(n >> (8 * i));
    return out;
}

}  // namespace jdb_deflate
