// Kompakte HNSW (Hierarchical Navigable Small Worlds) Implementation
// für approximate nearest neighbor search auf dichten Vektoren.
//
// Header-only, ~450 Zeilen. Unterstützt:
//   - Cosine-Similarity (Vektoren werden L2-normalisiert behandelt)
//   - Multi-Layer Graph wie im Original-Paper (Malkov & Yashunin, 2016)
//   - Inkrementelle Inserts
//   - Persistierung in/aus einem std::iostream
//
// API:
//   HnswIndex idx(dim, M=16, ef_construction=200, max_elements=0)
//   idx.add(vec, id)            -- vec ist L2-normalisiert; id ist beliebig
//   idx.search(query, k, ef)    -- gibt Top-k IDs mit Scores zurück
//   idx.save(stream)            -- Binär-Serialisierung
//   idx.load(stream)            -- Wiederherstellung
//
// Hinweis: Für korrekte Cosine-Similarity müssen alle Vektoren L2-normalisiert
// sein. compute_dense_embedding() in llm.cpp tut das bereits.

#pragma once

#include <vector>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <random>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace hnsw {

struct HnswIndex {
    // ── Konfiguration ────────────────────────────────────────────
    int dim = 0;
    int M = 16;
    int M_max0 = 32;          // doppelt so viel auf Layer 0
    int ef_construction = 200;
    double m_L = 1.0 / std::log(16.0); // 1/ln(M)
    std::mt19937 rng{42};

    // ── Daten ────────────────────────────────────────────────────
    // Pro Element: rohes Float-Array dim*N
    std::vector<float> data;        // size = dim * num_elements
    std::vector<int64_t> ids;       // externe IDs
    std::vector<int> levels;        // pro Element: höchste Layer
    // neighbors[layer][element] = vector<int> mit Indizes
    // Wir speichern als verschachtelten Container: neighbors[i] ist die Liste
    // pro Layer eines Elements.
    std::vector<std::vector<std::vector<int>>> neighbors; // [element][layer][nbrs]

    int entry_point = -1;
    int max_level = -1;

    HnswIndex() = default;
    HnswIndex(int dim_, int M_ = 16, int ef_construction_ = 200)
        : dim(dim_), M(M_), M_max0(M_ * 2), ef_construction(ef_construction_),
          m_L(1.0 / std::log((double)M_)) {}

    size_t size() const { return ids.size(); }

    // ── Distance: 1 - cosine similarity (für L2-normalisierte Vektoren ist das 1 - dot) ──
    // 4-fach unrolled für bessere ILP - Compiler kann das in SSE/AVX vektorisieren.
    static float cos_dist(const float* a, const float* b, int d) {
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        int i = 0;
        for (; i + 4 <= d; i += 4) {
            s0 += a[i+0] * b[i+0];
            s1 += a[i+1] * b[i+1];
            s2 += a[i+2] * b[i+2];
            s3 += a[i+3] * b[i+3];
        }
        float dot = s0 + s1 + s2 + s3;
        for (; i < d; i++) dot += a[i] * b[i];
        return 1.0f - dot;
    }

    const float* vec_at(int idx) const { return &data[(size_t)idx * dim]; }

    int random_level() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double r = dist(rng);
        int lvl = (int)std::floor(-std::log(r) * m_L);
        // Cap auf vernünftige Höhe - bei sehr großen Indizes wäre 32 ausreichend
        if (lvl > 16) lvl = 16;
        return lvl;
    }

    // ── Search auf einer Layer ──
    // Gibt die ef nächsten Nachbarn (sortiert nach Distanz aufsteigend) als
    // priority queue zurück.
    using DistIdx = std::pair<float, int>; // distance, idx

    struct DistIdxMaxCmp {
        bool operator()(const DistIdx& a, const DistIdx& b) const { return a.first < b.first; }
    };
    struct DistIdxMinCmp {
        bool operator()(const DistIdx& a, const DistIdx& b) const { return a.first > b.first; }
    };

    // Result: max-heap, sodass top() das schlechteste (weiteste) Element ist.
    // Visited-Tracking via vector<bool> statt unordered_set - ~10x schneller.
    std::priority_queue<DistIdx, std::vector<DistIdx>, DistIdxMaxCmp>
    search_layer(const float* q, int ep, int ef, int layer) const {
        std::priority_queue<DistIdx, std::vector<DistIdx>, DistIdxMaxCmp> top_results;
        std::priority_queue<DistIdx, std::vector<DistIdx>, DistIdxMinCmp> candidates;
        std::vector<bool> visited(ids.size(), false);

        float d_ep = cos_dist(q, vec_at(ep), dim);
        top_results.push({d_ep, ep});
        candidates.push({d_ep, ep});
        visited[ep] = true;

        while (!candidates.empty()) {
            auto [d_c, c] = candidates.top();
            candidates.pop();

            if (d_c > top_results.top().first) break;

            // Nachbarn von c auf der gegebenen Layer
            if (c >= (int)neighbors.size() || layer >= (int)neighbors[c].size()) continue;
            const auto& nbrs = neighbors[c][layer];
            for (int nb : nbrs) {
                if (visited[nb]) continue;
                visited[nb] = true;
                float d_nb = cos_dist(q, vec_at(nb), dim);
                if ((int)top_results.size() < ef || d_nb < top_results.top().first) {
                    candidates.push({d_nb, nb});
                    top_results.push({d_nb, nb});
                    if ((int)top_results.size() > ef) top_results.pop();
                }
            }
        }
        return top_results;
    }

    // Greedy-Search auf einer einzelnen Layer (für höhere Layer beim Insert/Search)
    int greedy_search_layer(const float* q, int ep, int layer) const {
        int curr = ep;
        float curr_d = cos_dist(q, vec_at(curr), dim);
        bool changed = true;
        while (changed) {
            changed = false;
            if (curr >= (int)neighbors.size() || layer >= (int)neighbors[curr].size()) break;
            for (int nb : neighbors[curr][layer]) {
                float d = cos_dist(q, vec_at(nb), dim);
                if (d < curr_d) {
                    curr_d = d;
                    curr = nb;
                    changed = true;
                }
            }
        }
        return curr;
    }

    // Heuristik zur Auswahl der M besten Nachbarn aus einer Kandidatenliste
    // (vereinfachte Variante: einfach die nächsten M nehmen)
    std::vector<int> select_neighbors(
        std::priority_queue<DistIdx, std::vector<DistIdx>, DistIdxMaxCmp>& candidates,
        int M_target) const
    {
        // Konvertiere zu sortierter Liste (nächste zuerst)
        std::vector<DistIdx> arr;
        while (!candidates.empty()) { arr.push_back(candidates.top()); candidates.pop(); }
        std::sort(arr.begin(), arr.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<int> result;
        for (int i = 0; i < (int)arr.size() && (int)result.size() < M_target; i++) {
            result.push_back(arr[i].second);
        }
        return result;
    }

    // ── Insert ──
    void add(const float* vec, int64_t id) {
        if (dim == 0) throw std::runtime_error("HNSW: dim not set");
        int new_idx = (int)ids.size();
        ids.push_back(id);
        // Daten anhängen
        data.insert(data.end(), vec, vec + dim);

        int lvl = random_level();
        levels.push_back(lvl);

        // Slot in neighbors anlegen
        neighbors.emplace_back();
        neighbors.back().resize(lvl + 1);

        // Erste Element?
        if (entry_point == -1) {
            entry_point = new_idx;
            max_level = lvl;
            return;
        }

        // Search von oberster Layer bis lvl+1: nur greedy
        int curr = entry_point;
        for (int l = max_level; l > lvl; l--) {
            curr = greedy_search_layer(vec, curr, l);
        }

        // Von min(lvl, max_level) bis 0: search_layer mit ef_construction, dann verbinden
        int start_layer = std::min(lvl, max_level);
        for (int l = start_layer; l >= 0; l--) {
            auto top = search_layer(vec, curr, ef_construction, l);
            // ep für nächste Layer = nächstes Element
            // (kopieren, weil select_neighbors leert die queue)
            auto top_copy = top;
            int M_target = (l == 0) ? M_max0 : M;
            auto chosen = select_neighbors(top, M_target);

            // Bidirektionale Kanten
            for (int nb : chosen) {
                neighbors[new_idx][l].push_back(nb);
                neighbors[nb][l].push_back(new_idx);
                // Pruning: zu viele Nachbarn?
                int max_nbrs = (l == 0) ? M_max0 : M;
                if ((int)neighbors[nb][l].size() > max_nbrs) {
                    // Behalte die M_target nächsten zu nb
                    std::vector<DistIdx> arr;
                    for (int x : neighbors[nb][l]) {
                        arr.push_back({cos_dist(vec_at(nb), vec_at(x), dim), x});
                    }
                    std::sort(arr.begin(), arr.end(), [](auto& a, auto& b) { return a.first < b.first; });
                    neighbors[nb][l].clear();
                    for (int i = 0; i < max_nbrs; i++) neighbors[nb][l].push_back(arr[i].second);
                }
            }

            // ep für nächste Layer: nimm den nächsten aus top_copy
            if (!top_copy.empty()) {
                // top_copy ist max-heap, der "nächste" ist das letzte Element nach pop_all
                // Wir wollen das mit kleinster Distanz - neu sortieren
                std::vector<DistIdx> all;
                while (!top_copy.empty()) { all.push_back(top_copy.top()); top_copy.pop(); }
                std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first < b.first; });
                if (!all.empty()) curr = all[0].second;
            }
        }

        if (lvl > max_level) {
            entry_point = new_idx;
            max_level = lvl;
        }
    }

    // ── Search ──
    // Gibt Top-k Treffer als Vektor von (similarity, id) zurück.
    // ef ist die Suchweite - größer = genauer, langsamer.
    std::vector<std::pair<float, int64_t>> search(const float* query, int k, int ef = 50) const {
        std::vector<std::pair<float, int64_t>> result;
        if (entry_point == -1) return result;
        if (ef < k) ef = k;

        int curr = entry_point;
        for (int l = max_level; l > 0; l--) {
            curr = greedy_search_layer(query, curr, l);
        }

        auto top = search_layer(query, curr, ef, 0);

        std::vector<DistIdx> arr;
        while (!top.empty()) { arr.push_back(top.top()); top.pop(); }
        std::sort(arr.begin(), arr.end(), [](auto& a, auto& b) { return a.first < b.first; });

        for (int i = 0; i < (int)arr.size() && i < k; i++) {
            float sim = 1.0f - arr[i].first; // distance -> similarity
            result.push_back({sim, ids[arr[i].second]});
        }
        return result;
    }

    // ── Persistierung ──
    static void w_i32(std::ostream& o, int32_t v)  { o.write((const char*)&v, 4); }
    static void w_i64(std::ostream& o, int64_t v)  { o.write((const char*)&v, 8); }
    static void w_f32(std::ostream& o, float v)    { o.write((const char*)&v, 4); }
    static int32_t r_i32(std::istream& i) { int32_t v=0; i.read((char*)&v, 4); return v; }
    static int64_t r_i64(std::istream& i) { int64_t v=0; i.read((char*)&v, 8); return v; }
    static float   r_f32(std::istream& i) { float v=0;   i.read((char*)&v, 4); return v; }

    void save(std::ostream& o) const {
        o.write("HNSW", 4);
        w_i32(o, 1); // version
        w_i32(o, dim);
        w_i32(o, M);
        w_i32(o, M_max0);
        w_i32(o, ef_construction);
        w_i32(o, entry_point);
        w_i32(o, max_level);
        w_i32(o, (int32_t)ids.size());
        for (auto id : ids) w_i64(o, id);
        for (auto lvl : levels) w_i32(o, lvl);
        // Daten
        o.write((const char*)data.data(), data.size() * sizeof(float));
        // Nachbarn
        for (size_t i = 0; i < neighbors.size(); i++) {
            w_i32(o, (int32_t)neighbors[i].size());
            for (auto& layer : neighbors[i]) {
                w_i32(o, (int32_t)layer.size());
                for (int n : layer) w_i32(o, n);
            }
        }
    }

    void load(std::istream& i) {
        char magic[4]; i.read(magic, 4);
        if (std::string(magic, 4) != "HNSW")
            throw std::runtime_error("HNSW: bad magic");
        int32_t ver = r_i32(i);
        if (ver != 1) throw std::runtime_error("HNSW: unsupported version");
        dim = r_i32(i);
        M = r_i32(i);
        M_max0 = r_i32(i);
        ef_construction = r_i32(i);
        entry_point = r_i32(i);
        max_level = r_i32(i);
        int32_t n = r_i32(i);
        ids.resize(n);
        for (int k = 0; k < n; k++) ids[k] = r_i64(i);
        levels.resize(n);
        for (int k = 0; k < n; k++) levels[k] = r_i32(i);
        data.resize((size_t)n * dim);
        i.read((char*)data.data(), data.size() * sizeof(float));
        neighbors.resize(n);
        for (int k = 0; k < n; k++) {
            int32_t lc = r_i32(i);
            neighbors[k].resize(lc);
            for (int l = 0; l < lc; l++) {
                int32_t nc = r_i32(i);
                neighbors[k][l].resize(nc);
                for (int x = 0; x < nc; x++) neighbors[k][l][x] = r_i32(i);
            }
        }
        m_L = 1.0 / std::log((double)M);
    }
};

} // namespace hnsw
