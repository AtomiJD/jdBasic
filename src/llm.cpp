#include "llm.h"
#include "vm.h"
#include "errors.h"
#include "channels.h"

#ifndef LLM
void register_llm_builtins(VM& vm) {
    vm.register_native("AI.LOAD_LLM", 1, 1, [](const std::vector<Value>&) -> Value {
        throw jdError(ErrCode::RUNTIME_ERROR, "AI.LOAD_LLM requires LLM build (build.bat LLM)");
    });
}
#else // LLM

#include "llama.h"
#include "ggml-backend.h"
#include "pdf_extract.h"
#include "hnsw.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <functional>
#include <cstring>
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <set>
#include <map>
#include <filesystem>
#include <thread>

// ── Text Embedding Engine (TF-IDF) ─────────────────────────────

struct EmbedEngine {
    // Global document frequency for IDF
    std::map<std::string, int> doc_freq;
    int total_docs = 0;

    static std::vector<std::string> split_words(const std::string& text) {
        std::vector<std::string> words;
        std::string word;
        for (char c : text) {
            if (std::isalnum((unsigned char)c)) {
                word += std::tolower((unsigned char)c);
            } else {
                if (!word.empty() && word.size() >= 2) words.push_back(word);
                word.clear();
            }
        }
        if (!word.empty() && word.size() >= 2) words.push_back(word);
        return words;
    }

    // Build word frequency map for a text
    static std::map<std::string, double> word_freqs(const std::vector<std::string>& words) {
        std::map<std::string, double> freq;
        for (auto& w : words) freq[w] += 1.0;
        // Normalize by total words
        double n = (double)words.size();
        if (n > 0) for (auto& [k, v] : freq) v /= n;
        return freq;
    }

    void add_document(const std::vector<std::string>& words) {
        std::set<std::string> unique(words.begin(), words.end());
        for (auto& w : unique) doc_freq[w]++;
        total_docs++;
    }

    // Compute TF-IDF embedding for a text
    std::vector<std::pair<std::string, double>> embed(const std::string& text) {
        auto words = split_words(text);
        auto tf = word_freqs(words);
        std::vector<std::pair<std::string, double>> result;
        for (auto& [word, freq] : tf) {
            double idf = 1.0;
            auto it = doc_freq.find(word);
            if (it != doc_freq.end() && total_docs > 0) {
                idf = std::log((double)(total_docs + 1) / (it->second + 1)) + 1.0;
            }
            result.push_back({word, freq * idf});
        }
        return result;
    }

    // Cosine similarity between two sparse TF-IDF vectors
    static double similarity(const std::vector<std::pair<std::string, double>>& a,
                             const std::vector<std::pair<std::string, double>>& b) {
        std::map<std::string, double> ma(a.begin(), a.end());
        std::map<std::string, double> mb(b.begin(), b.end());
        double dot = 0, na = 0, nb = 0;
        for (auto& [k, v] : ma) {
            na += v * v;
            auto it = mb.find(k);
            if (it != mb.end()) dot += v * it->second;
        }
        for (auto& [k, v] : mb) nb += v * v;
        double denom = std::sqrt(na) * std::sqrt(nb);
        return denom > 0 ? dot / denom : 0;
    }
};

// ── RAG Store ──────────────────────────────────────────────────

struct RagChunk {
    std::string text;
    std::string source;                                  // filename or label
    std::vector<std::pair<std::string, double>> sparse;  // TF-IDF (sparse mode)
    std::vector<float> dense;                            // dense embedding (LLM mode)
};

struct LlmModel; // forward
// Dense embedding via llama.cpp — implementiert weiter unten nach LlmModel-Definition
static std::vector<float> compute_dense_embedding(LlmModel* m, const std::string& text);
// Lookup eines LLM via id — implementiert nach g_llms-Deklaration
static LlmModel* rag_lookup_llm(int id);

struct RagStore {
    int llm_id = 0;          // associated LLM for generation / embeddings
    int embed_llm_id = 0;    // optional separate embedding model id (0 = TF-IDF)
    EmbedEngine engine;      // TF-IDF fallback
    std::vector<RagChunk> chunks;
    int chunk_size = 500;    // chars per chunk
    int chunk_overlap = 50;  // overlap between chunks
    int embed_dim = 0;       // dimension der dense vectors (0 wenn TF-IDF)

    // Optionaler HNSW-Index für schnelle Suche bei großen Indizes.
    // Wird via AI.RAG_BUILD_INDEX aufgebaut. Der Index wird bei add_text NICHT
    // automatisch aktualisiert — neue Chunks erfordern einen erneuten Build.
    std::unique_ptr<hnsw::HnswIndex> hnsw_index;
    bool hnsw_dirty = false;  // true wenn add_text seit letztem Build aufgerufen wurde

    bool dense_mode() const { return embed_llm_id != 0; }
    bool has_hnsw() const { return hnsw_index && !hnsw_dirty; }

    void add_text(const std::string& text, const std::string& source) {
        // Split into chunks
        std::vector<size_t> new_chunk_indices;
        for (size_t i = 0; i < text.size(); i += (chunk_size - chunk_overlap)) {
            std::string chunk = text.substr(i, chunk_size);
            if (chunk.empty()) continue;
            RagChunk rc;
            rc.text = std::move(chunk);
            rc.source = source;
            new_chunk_indices.push_back(chunks.size());
            chunks.push_back(std::move(rc));
        }

        if (dense_mode()) {
            // Echte Embeddings via llama.cpp — pro Chunk
            auto* m = rag_lookup_llm(embed_llm_id);
            if (!m) throw std::runtime_error("RAG: embedding model not loaded");
            for (auto idx : new_chunk_indices) {
                chunks[idx].dense = compute_dense_embedding(m, chunks[idx].text);
                if (embed_dim == 0) embed_dim = (int)chunks[idx].dense.size();
            }
        } else {
            // TF-IDF: nur die neuen Chunks der Engine hinzufügen, dann ALLE neu einbetten
            for (auto idx : new_chunk_indices) {
                auto words = EmbedEngine::split_words(chunks[idx].text);
                engine.add_document(words);
            }
            for (auto& c : chunks) c.sparse = engine.embed(c.text);
        }

        // HNSW-Index ist veraltet wenn vorhanden — User muss BUILD_INDEX neu aufrufen
        if (hnsw_index) hnsw_dirty = true;
    }

    // Baut einen HNSW-Index über die aktuellen dichten Chunks. Funktioniert
    // nur im dense_mode (TF-IDF ist sparse, HNSW braucht dichte Vektoren).
    void build_hnsw(int M = 16, int ef_construction = 200) {
        if (!dense_mode())
            throw std::runtime_error("HNSW braucht dense embeddings — RAG_CREATE mit embed_llm_id aufrufen");
        if (chunks.empty())
            throw std::runtime_error("HNSW: keine Chunks vorhanden");
        if (embed_dim == 0) embed_dim = (int)chunks[0].dense.size();
        hnsw_index = std::make_unique<hnsw::HnswIndex>(embed_dim, M, ef_construction);
        for (size_t i = 0; i < chunks.size(); i++) {
            if ((int)chunks[i].dense.size() != embed_dim) continue;
            hnsw_index->add(chunks[i].dense.data(), (int64_t)i);
        }
        hnsw_dirty = false;
    }

    std::vector<std::pair<double, int>> search(const std::string& query, int top_k) {
        std::vector<std::pair<double, int>> scores;
        if (dense_mode()) {
            auto* m = rag_lookup_llm(embed_llm_id);
            if (!m) throw std::runtime_error("RAG: embedding model not loaded");
            auto q = compute_dense_embedding(m, query);

            // Wenn ein HNSW-Index existiert und aktuell ist: nutzen
            if (has_hnsw()) {
                // ef = max(top_k * 4, 50) für gute Recall/Speed Balance
                int ef = std::max(top_k * 4, 50);
                auto hits = hnsw_index->search(q.data(), top_k, ef);
                for (auto& [sim, id] : hits) scores.push_back({(double)sim, (int)id});
                return scores; // bereits sortiert
            }

            // Fallback: lineare Suche
            // (Vektoren sollten bereits L2-normalisiert sein, also reicht das Skalarprodukt)
            double qn = 0; for (float v : q) qn += (double)v * v; qn = std::sqrt(qn);
            for (int i = 0; i < (int)chunks.size(); i++) {
                auto& c = chunks[i].dense;
                if (c.size() != q.size()) { scores.push_back({0, i}); continue; }
                double dot = 0, cn = 0;
                for (size_t k = 0; k < q.size(); k++) {
                    dot += (double)q[k] * c[k];
                    cn  += (double)c[k] * c[k];
                }
                cn = std::sqrt(cn);
                double sim = (qn > 0 && cn > 0) ? dot / (qn * cn) : 0;
                scores.push_back({sim, i});
            }
        } else {
            auto q_emb = engine.embed(query);
            for (int i = 0; i < (int)chunks.size(); i++) {
                double sim = EmbedEngine::similarity(q_emb, chunks[i].sparse);
                scores.push_back({sim, i});
            }
        }
        std::sort(scores.begin(), scores.end(), [](auto& a, auto& b) { return a.first > b.first; });
        if ((int)scores.size() > top_k) scores.resize(top_k);
        return scores;
    }

    // ── Persistierung ────────────────────────────────────────────
    // Format: ASCII-Magic + Version + Felder + Engine-Daten + Chunks
    //
    //   "JRAG" u32_version=1
    //   i32 chunk_size, i32 chunk_overlap
    //   u8 dense_mode, i32 embed_dim
    //   --- TF-IDF (nur wenn dense_mode == 0) ---
    //   i32 doc_freq_count
    //   für jeden Eintrag: i32 word_len, bytes word, i32 doc_count
    //   i32 total_docs
    //   --- Chunks ---
    //   i32 chunk_count
    //   für jeden Chunk:
    //     i32 text_len,   bytes
    //     i32 source_len, bytes
    //     wenn dense_mode == 0:
    //       i32 sparse_count
    //       für jeden: i32 word_len, bytes word, f64 weight
    //     sonst:
    //       i32 dim, dim*f32 floats

    static void w_u8 (std::ostream& o, uint8_t v)  { o.write((const char*)&v, 1); }
    static void w_i32(std::ostream& o, int32_t v)  { o.write((const char*)&v, 4); }
    static void w_u32(std::ostream& o, uint32_t v) { o.write((const char*)&v, 4); }
    static void w_f32(std::ostream& o, float v)    { o.write((const char*)&v, 4); }
    static void w_f64(std::ostream& o, double v)   { o.write((const char*)&v, 8); }
    static void w_str(std::ostream& o, const std::string& s) {
        w_i32(o, (int32_t)s.size());
        o.write(s.data(), s.size());
    }

    static uint8_t  r_u8 (std::istream& i) { uint8_t v=0;  i.read((char*)&v, 1); return v; }
    static int32_t  r_i32(std::istream& i) { int32_t v=0;  i.read((char*)&v, 4); return v; }
    static uint32_t r_u32(std::istream& i) { uint32_t v=0; i.read((char*)&v, 4); return v; }
    static float    r_f32(std::istream& i) { float v=0;    i.read((char*)&v, 4); return v; }
    static double   r_f64(std::istream& i) { double v=0;   i.read((char*)&v, 8); return v; }
    static std::string r_str(std::istream& i) {
        int32_t n = r_i32(i);
        if (n < 0 || n > 100*1024*1024) return {};
        std::string s(n, '\0');
        i.read(s.data(), n);
        return s;
    }

    void save(const std::string& path) const {
        std::ofstream o(path, std::ios::binary);
        if (!o) throw std::runtime_error("RAG_SAVE: cannot write " + path);
        o.write("JRAG", 4);
        w_u32(o, 2); // version 2 = mit optionalem HNSW
        w_i32(o, chunk_size);
        w_i32(o, chunk_overlap);
        bool dense = dense_mode();
        w_u8(o, dense ? 1 : 0);
        w_i32(o, embed_dim);

        if (!dense) {
            // TF-IDF Engine-Daten
            w_i32(o, (int32_t)engine.doc_freq.size());
            for (auto& [word, cnt] : engine.doc_freq) {
                w_str(o, word);
                w_i32(o, cnt);
            }
            w_i32(o, engine.total_docs);
        }

        // Chunks
        w_i32(o, (int32_t)chunks.size());
        for (auto& c : chunks) {
            w_str(o, c.text);
            w_str(o, c.source);
            if (dense) {
                w_i32(o, (int32_t)c.dense.size());
                for (float v : c.dense) w_f32(o, v);
            } else {
                w_i32(o, (int32_t)c.sparse.size());
                for (auto& [w, val] : c.sparse) {
                    w_str(o, w);
                    w_f64(o, val);
                }
            }
        }

        // Optionaler HNSW-Index (Version 2)
        bool save_hnsw = (hnsw_index != nullptr) && !hnsw_dirty;
        w_u8(o, save_hnsw ? 1 : 0);
        if (save_hnsw) hnsw_index->save(o);
    }

    void load(const std::string& path) {
        std::ifstream i(path, std::ios::binary);
        if (!i) throw std::runtime_error("RAG_LOAD: cannot open " + path);
        char magic[4]; i.read(magic, 4);
        if (std::string(magic, 4) != "JRAG")
            throw std::runtime_error("RAG_LOAD: bad magic in " + path);
        uint32_t ver = r_u32(i);
        if (ver != 1 && ver != 2)
            throw std::runtime_error("RAG_LOAD: unsupported version " + std::to_string(ver));
        chunk_size = r_i32(i);
        chunk_overlap = r_i32(i);
        bool dense = r_u8(i) != 0;
        embed_dim = r_i32(i);

        chunks.clear();
        engine.doc_freq.clear();
        engine.total_docs = 0;

        if (!dense) {
            int32_t df_count = r_i32(i);
            for (int k = 0; k < df_count; k++) {
                std::string w = r_str(i);
                int32_t cnt = r_i32(i);
                engine.doc_freq[w] = cnt;
            }
            engine.total_docs = r_i32(i);
        }

        int32_t ch_count = r_i32(i);
        chunks.reserve(ch_count);
        for (int k = 0; k < ch_count; k++) {
            RagChunk c;
            c.text = r_str(i);
            c.source = r_str(i);
            if (dense) {
                int32_t dim = r_i32(i);
                c.dense.resize(dim);
                for (int j = 0; j < dim; j++) c.dense[j] = r_f32(i);
            } else {
                int32_t sz = r_i32(i);
                c.sparse.resize(sz);
                for (int j = 0; j < sz; j++) {
                    std::string w = r_str(i);
                    double v = r_f64(i);
                    c.sparse[j] = {w, v};
                }
            }
            chunks.push_back(std::move(c));
        }

        // Optionaler HNSW (nur in Version >= 2)
        if (ver >= 2) {
            uint8_t has_hn = r_u8(i);
            if (has_hn) {
                hnsw_index = std::make_unique<hnsw::HnswIndex>();
                hnsw_index->load(i);
                hnsw_dirty = false;
            }
        }
    }
};

static int g_next_rag_id = 1;
static std::unordered_map<int, std::unique_ptr<RagStore>> g_rags;

// ── Text Classifier Store ──────────────────────────────────────
// k-NN-Klassifikation auf dichten Embeddings.
// Trainingsdaten: Liste von (text, label, embedding).
// Prediction: für einen neuen Text das Embedding berechnen, Top-k nächste
// Nachbarn im Trainingsdatensatz finden, Majority Vote über die Labels.

struct ClassifierSample {
    std::string text;
    std::string label;
    std::vector<float> embedding;
};

struct ClassifierStore {
    int embed_llm_id = 0;           // ID des Embedding-Modells
    std::vector<ClassifierSample> samples;
    int embed_dim = 0;
    // Optional: HNSW-Index für große Datensätze
    std::unique_ptr<hnsw::HnswIndex> hnsw_index;
    bool hnsw_dirty = false;

    bool has_hnsw() const { return hnsw_index && !hnsw_dirty; }

    void add(const std::string& text, const std::string& label) {
        if (embed_llm_id == 0) throw std::runtime_error("CLASSIFIER: no embedding model");
        auto* m = rag_lookup_llm(embed_llm_id);
        if (!m) throw std::runtime_error("CLASSIFIER: embedding model not loaded");
        ClassifierSample s;
        s.text = text;
        s.label = label;
        s.embedding = compute_dense_embedding(m, text);
        if (embed_dim == 0) embed_dim = (int)s.embedding.size();
        samples.push_back(std::move(s));
        if (hnsw_index) hnsw_dirty = true;
    }

    void build_hnsw(int M = 16, int ef_construction = 200) {
        if (samples.empty()) throw std::runtime_error("CLASSIFIER: no samples");
        if (embed_dim == 0) embed_dim = (int)samples[0].embedding.size();
        hnsw_index = std::make_unique<hnsw::HnswIndex>(embed_dim, M, ef_construction);
        for (size_t i = 0; i < samples.size(); i++) {
            if ((int)samples[i].embedding.size() != embed_dim) continue;
            hnsw_index->add(samples[i].embedding.data(), (int64_t)i);
        }
        hnsw_dirty = false;
    }

    // Ermittelt die k nächsten Nachbarn für ein Query-Embedding.
    // Rückgabe: Vektor von (similarity, sample_index) absteigend nach sim sortiert.
    std::vector<std::pair<double, int>> knn(const std::vector<float>& q, int k) const {
        std::vector<std::pair<double, int>> scores;
        if (samples.empty()) return scores;

        if (hnsw_index && !hnsw_dirty) {
            int ef = std::max(k * 4, 50);
            auto hits = hnsw_index->search(q.data(), k, ef);
            for (auto& [sim, id] : hits) scores.push_back({(double)sim, (int)id});
            return scores;
        }

        // Lineare Suche mit Cosine-Similarity
        for (size_t i = 0; i < samples.size(); i++) {
            auto& c = samples[i].embedding;
            if (c.size() != q.size()) { scores.push_back({0, (int)i}); continue; }
            double dot = 0, na = 0, nb = 0;
            for (size_t j = 0; j < q.size(); j++) {
                dot += (double)q[j] * c[j];
                na  += (double)q[j] * q[j];
                nb  += (double)c[j] * c[j];
            }
            double sim = (na > 0 && nb > 0) ? dot / (std::sqrt(na) * std::sqrt(nb)) : 0;
            scores.push_back({sim, (int)i});
        }
        std::sort(scores.begin(), scores.end(), [](auto& a, auto& b) { return a.first > b.first; });
        if ((int)scores.size() > k) scores.resize(k);
        return scores;
    }

    // Predict: gibt Gewinner-Label, confidence (0..1) und die Top-k neighbors zurück.
    struct Prediction {
        std::string label;
        double confidence;
        std::vector<std::pair<double, int>> neighbors; // (sim, idx)
        std::vector<std::pair<std::string, int>> votes; // (label, count)
    };

    Prediction predict(const std::string& text, int k) const {
        Prediction p;
        if (samples.empty()) { p.label = ""; p.confidence = 0; return p; }
        auto* m = rag_lookup_llm(embed_llm_id);
        if (!m) throw std::runtime_error("CLASSIFIER: embedding model not loaded");
        auto q = compute_dense_embedding(m, text);
        p.neighbors = knn(q, k);

        // Gewichtete Votes: jeder Nachbar trägt mit seiner Similarity bei.
        // So werden nähere Nachbarn stärker berücksichtigt.
        std::map<std::string, double> weight;
        double total_weight = 0;
        for (auto& [sim, idx] : p.neighbors) {
            double w = std::max(0.0, sim); // negative Werte clampen
            weight[samples[idx].label] += w;
            total_weight += w;
        }

        // Gewinner
        std::string winner;
        double best_w = -1;
        for (auto& [lbl, w] : weight) {
            if (w > best_w) { best_w = w; winner = lbl; }
        }
        p.label = winner;
        p.confidence = (total_weight > 0) ? (best_w / total_weight) : 0;

        // Sortierte Vote-Liste (für die Rückgabe als Array)
        std::vector<std::pair<std::string, int>> sorted_votes;
        std::map<std::string, int> counts;
        for (auto& [sim, idx] : p.neighbors) counts[samples[idx].label]++;
        for (auto& [lbl, cnt] : counts) sorted_votes.push_back({lbl, cnt});
        std::sort(sorted_votes.begin(), sorted_votes.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        p.votes = std::move(sorted_votes);
        return p;
    }

    // Statistik: Anzahl Samples pro Label
    std::map<std::string, int> label_counts() const {
        std::map<std::string, int> counts;
        for (auto& s : samples) counts[s.label]++;
        return counts;
    }

    // ── Persistierung: "JCLF" Magic + Version 1 ───────────────
    void save(const std::string& path) const {
        std::ofstream o(path, std::ios::binary);
        if (!o) throw std::runtime_error("CLASSIFIER_SAVE: cannot write " + path);
        o.write("JCLF", 4);
        RagStore::w_u32(o, 1);                     // version
        RagStore::w_i32(o, embed_dim);
        RagStore::w_i32(o, (int32_t)samples.size());
        for (auto& s : samples) {
            RagStore::w_str(o, s.text);
            RagStore::w_str(o, s.label);
            RagStore::w_i32(o, (int32_t)s.embedding.size());
            for (float v : s.embedding) RagStore::w_f32(o, v);
        }
        // Optional: HNSW-Index
        bool save_hnsw = hnsw_index && !hnsw_dirty;
        RagStore::w_u8(o, save_hnsw ? 1 : 0);
        if (save_hnsw) hnsw_index->save(o);
    }

    void load(const std::string& path) {
        std::ifstream i(path, std::ios::binary);
        if (!i) throw std::runtime_error("CLASSIFIER_LOAD: cannot open " + path);
        char magic[4]; i.read(magic, 4);
        if (std::string(magic, 4) != "JCLF")
            throw std::runtime_error("CLASSIFIER_LOAD: bad magic");
        uint32_t ver = RagStore::r_u32(i);
        if (ver != 1) throw std::runtime_error("CLASSIFIER_LOAD: unsupported version");
        embed_dim = RagStore::r_i32(i);
        int32_t n = RagStore::r_i32(i);
        samples.clear();
        samples.reserve(n);
        for (int k = 0; k < n; k++) {
            ClassifierSample s;
            s.text = RagStore::r_str(i);
            s.label = RagStore::r_str(i);
            int32_t dim = RagStore::r_i32(i);
            s.embedding.resize(dim);
            for (int j = 0; j < dim; j++) s.embedding[j] = RagStore::r_f32(i);
            samples.push_back(std::move(s));
        }
        uint8_t has_hn = RagStore::r_u8(i);
        if (has_hn) {
            hnsw_index = std::make_unique<hnsw::HnswIndex>();
            hnsw_index->load(i);
            hnsw_dirty = false;
        }
    }
};

static int g_next_cls_id = 1;
static std::unordered_map<int, std::unique_ptr<ClassifierStore>> g_classifiers;

// ── LLM Model State ────────────────────────────────────────────

struct LlmModel {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    llama_sampler* sampler = nullptr;
    const llama_vocab* vocab = nullptr;

    // True wenn das Modell ausschließlich für Embeddings genutzt wird
    // (cparams.embeddings = true beim Init).
    bool is_embedding_model = false;
    int embed_dim = 0;

    // Generation parameters
    float temperature = 0.8f;
    float top_p = 0.95f;
    int top_k = 40;
    float min_p = 0.05f;
    int max_tokens = 512;
    uint32_t seed = 0;
    std::string system_prompt;

    // Optional GBNF Grammar (für strukturiertes Output / JSON Mode).
    // Wenn nicht leer, wird ein grammar-Sampler vor die Chain gehängt.
    std::string grammar_gbnf;

    // Chat history for multi-turn conversations
    struct ChatMsg { std::string role; std::string content; };
    std::vector<ChatMsg> history;

    // Tool/Function calling
    struct Tool {
        std::string name;
        std::string description;
        std::string param_desc; // e.g. "city_name" or "x, y"
        Value funcref;          // jdBasic function reference
    };
    std::vector<Tool> tools;

    std::string build_tool_system_prompt() const {
        if (tools.empty()) return "";
        std::string tp = "\n\nYou have access to the following tools:\n";
        for (auto& t : tools) {
            tp += "- " + t.name + "(" + t.param_desc + "): " + t.description + "\n";
        }
        tp += "\nTo call a tool, write EXACTLY this format on its own line:\n";
        tp += "<TOOL>" + tools[0].name + "|argument1|argument2</TOOL>\n";
        tp += "Wait for the tool result before continuing your answer.\n";
        tp += "Only call a tool if needed. Do not invent tool results.\n";
        return tp;
    }

    // Parse tool calls from LLM output: <TOOL>name|arg1|arg2</TOOL>
    struct ToolCall {
        std::string name;
        std::vector<std::string> args;
        size_t start_pos, end_pos; // positions in the text
    };

    static std::vector<ToolCall> parse_tool_calls(const std::string& text) {
        std::vector<ToolCall> calls;
        size_t pos = 0;
        while (true) {
            size_t start = text.find("<TOOL>", pos);
            if (start == std::string::npos) break;
            size_t end = text.find("</TOOL>", start);
            if (end == std::string::npos) break;
            std::string body = text.substr(start + 6, end - start - 6);
            ToolCall tc;
            tc.start_pos = start;
            tc.end_pos = end + 7;
            // Parse name|arg1|arg2
            std::stringstream ss(body);
            std::string part;
            bool first = true;
            while (std::getline(ss, part, '|')) {
                // Trim whitespace
                while (!part.empty() && part.front() == ' ') part.erase(0, 1);
                while (!part.empty() && part.back() == ' ') part.pop_back();
                if (first) { tc.name = part; first = false; }
                else tc.args.push_back(part);
            }
            calls.push_back(std::move(tc));
            pos = end + 7;
        }
        return calls;
    }

    // Build a prompt from history using the model's chat template
    std::string build_chat_prompt(const std::string& user_msg) const {
        // Detect template style from vocab
        bool has_im_start = false;
        // Try common chat templates
        // Phi-3 style: <|system|>\n...<|end|>\n<|user|>\n...<|end|>\n<|assistant|>\n
        // Llama/Mistral style: [INST] ... [/INST]
        // ChatML style: <|im_start|>system\n...<|im_end|>

        std::string prompt;

        // Simple auto-detect: check if vocab has special tokens
        // For now, use Phi-3 / ChatML-like format as default
        std::string sys = system_prompt + build_tool_system_prompt();
        if (!sys.empty()) {
            prompt += "<|system|>\n" + sys + "<|end|>\n";
        }
        for (auto& msg : history) {
            prompt += "<|" + msg.role + "|>\n" + msg.content + "<|end|>\n";
        }
        prompt += "<|user|>\n" + user_msg + "<|end|>\n<|assistant|>\n";
        return prompt;
    }

    // Strip common assistant prefixes from response
    static std::string clean_response(const std::string& raw) {
        std::string s = raw;
        // Strip leading whitespace/newlines
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start != std::string::npos && start > 0) s = s.substr(start);
        // Strip common prefixes
        const char* prefixes[] = {"<|assistant|>", "<|assistant|>\n", nullptr};
        for (int i = 0; prefixes[i]; i++) {
            if (s.substr(0, strlen(prefixes[i])) == prefixes[i])
                s = s.substr(strlen(prefixes[i]));
        }
        // Strip leading whitespace again
        start = s.find_first_not_of(" \t\n\r");
        if (start != std::string::npos && start > 0) s = s.substr(start);
        // Strip trailing <|end|> or <|endoftext|>
        for (auto& suffix : {"<|end|>", "<|endoftext|>", "</s>"}) {
            size_t pos = s.rfind(suffix);
            if (pos != std::string::npos && pos > s.size() - strlen(suffix) - 5)
                s = s.substr(0, pos);
        }
        return s;
    }

    ~LlmModel() {
        if (sampler) llama_sampler_free(sampler);
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }
};

static int g_next_llm_id = 1;
static std::unordered_map<int, std::unique_ptr<LlmModel>> g_llms;

// Helper für Forward-Decl in RagStore (siehe oben)
static LlmModel* rag_lookup_llm(int id) {
    auto it = g_llms.find(id);
    return (it == g_llms.end()) ? nullptr : it->second.get();
}
static bool g_llama_backend_init = false;
static std::string g_llama_log_buf;

// Embed hosts (Godot, etc.) live in a directory the Windows DLL loader
// doesn't search by default. We let jdb_embed_api.cpp drop the runtime
// dll's directory in here, and ensure_backend() then walks it for
// ggml-*.dll entries via ggml_backend_load() so we don't depend on
// ggml_backend_load_all()'s implicit EXE-dir scan.
std::string g_jdb_embed_dll_dir;

// Capture llama / ggml log lines so they show up in jdBasic stdout
// instead of vanishing into the host process's stderr (which Godot's
// editor console doesn't show for plugin-side prints).
static void llama_log_cb(ggml_log_level level, const char* text, void* /*ud*/) {
    if (!text) return;
    g_llama_log_buf += text;
}

// ── Helpers ─────────────────────────────────────────────────────

static void ensure_backend() {
    if (g_llama_backend_init) return;
    llama_log_set(llama_log_cb, nullptr);
    ggml_backend_load_all();

    // Embed fallback: if load_all came up empty (host process directory
    // doesn't have the backend DLLs), explicitly load each ggml-*.dll
    // from the directory jdb_embed_api.cpp gave us.
    if (ggml_backend_reg_count() == 0 && !g_jdb_embed_dll_dir.empty()) {
        const char* names[] = {
            "ggml-cpu-alderlake.dll", "ggml-cpu-haswell.dll",
            "ggml-cpu-icelake.dll", "ggml-cpu-skylakex.dll",
            "ggml-cpu-sandybridge.dll", "ggml-cpu-sapphirerapids.dll",
            "ggml-cpu-cascadelake.dll", "ggml-cpu-cooperlake.dll",
            "ggml-cpu-cannonlake.dll", "ggml-cpu-zen4.dll",
            "ggml-cpu-piledriver.dll", "ggml-cpu-ivybridge.dll",
            "ggml-cpu-sse42.dll", "ggml-cpu-x64.dll",
            "ggml-cuda.dll",
        };
        for (const char* n : names) {
            std::string full = g_jdb_embed_dll_dir + "\\" + n;
            ggml_backend_load(full.c_str());
        }
    }
    llama_backend_init();
    g_llama_backend_init = true;
}

static LlmModel* get_llm(int id) {
    auto it = g_llms.find(id);
    if (it == g_llms.end())
        throw jdError(ErrCode::RUNTIME_ERROR, "Invalid LLM model id: " + std::to_string(id));
    return it->second.get();
}

static void rebuild_sampler(LlmModel* m) {
    if (m->sampler) llama_sampler_free(m->sampler);
    auto sparams = llama_sampler_chain_default_params();
    m->sampler = llama_sampler_chain_init(sparams);

    // Grammar-Sampler MUSS vor den anderen Samplern stehen, damit er
    // ungültige Tokens maskiert bevor top-k/top-p sie filtern.
    if (!m->grammar_gbnf.empty() && m->vocab) {
        auto* gs = llama_sampler_init_grammar(m->vocab, m->grammar_gbnf.c_str(), "root");
        if (gs) llama_sampler_chain_add(m->sampler, gs);
    }

    llama_sampler_chain_add(m->sampler, llama_sampler_init_top_k(m->top_k));
    llama_sampler_chain_add(m->sampler, llama_sampler_init_top_p(m->top_p, 1));
    llama_sampler_chain_add(m->sampler, llama_sampler_init_min_p(m->min_p, 1));
    llama_sampler_chain_add(m->sampler, llama_sampler_init_temp(m->temperature));
    llama_sampler_chain_add(m->sampler, llama_sampler_init_dist(m->seed));
}

static std::string token_to_string(const llama_vocab* vocab, llama_token token) {
    char buf[256];
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, true);
    if (n < 0) return "";
    return std::string(buf, n);
}

static std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text, bool add_bos) {
    int n = llama_tokenize(vocab, text.c_str(), (int)text.size(), nullptr, 0, add_bos, true);
    std::vector<llama_token> tokens(std::abs(n));
    llama_tokenize(vocab, text.c_str(), (int)text.size(), tokens.data(), (int)tokens.size(), add_bos, true);
    return tokens;
}

// Generate text from a prompt
static std::string generate(LlmModel* m, const std::string& prompt,
                            std::function<bool(const std::string&)> on_token = nullptr) {
    auto tokens = tokenize(m->vocab, prompt, true);

    int n_ctx = llama_n_ctx(m->ctx);
    if ((int)tokens.size() > n_ctx - 4) {
        throw jdError(ErrCode::RUNTIME_ERROR, "Prompt too long (" +
            std::to_string(tokens.size()) + " tokens, max " + std::to_string(n_ctx) + ")");
    }

    // KV-Cache vor jeder neuen Generierung leeren. Sonst sammelt sich
    // bei wiederholten Aufrufen der komplette Verlauf und der Cache
    // läuft über ("failed to find a memory slot").
    llama_memory_t mem = llama_get_memory(m->ctx);
    if (mem) llama_memory_clear(mem, true);

    // Evaluate prompt
    llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
    if (llama_decode(m->ctx, batch) != 0) {
        throw jdError(ErrCode::RUNTIME_ERROR, "LLM decode failed on prompt");
    }

    // Generate tokens
    std::string result;
    llama_token eos = llama_vocab_eos(m->vocab);

    for (int i = 0; i < m->max_tokens; i++) {
        llama_token new_token = llama_sampler_sample(m->sampler, m->ctx, -1);

        // Check for end of generation
        if (llama_vocab_is_eog(m->vocab, new_token)) break;

        std::string piece = token_to_string(m->vocab, new_token);
        result += piece;

        // Streaming callback
        if (on_token && !on_token(piece)) break;

        // Prepare next batch with the new token
        llama_batch next = llama_batch_get_one(&new_token, 1);
        if (llama_decode(m->ctx, next) != 0) {
            throw jdError(ErrCode::RUNTIME_ERROR, "LLM decode failed during generation");
        }
    }

    return result;
}

// ── Standard JSON Grammar (GBNF) ────────────────────────────────
// Vollständige RFC 8259 JSON-Grammar. Erzwingt valides JSON als Output.
// Quelle: llama.cpp grammars/json.gbnf (public domain).

static const char* JSON_GBNF = R"GBNF(
root   ::= object
value  ::= object | array | string | number | ("true" | "false" | "null") ws
object ::=
  "{" ws (
            string ":" ws value
    ("," ws string ":" ws value)*
  )? "}" ws
array  ::=
  "[" ws (
            value
    ("," ws value)*
  )? "]" ws
string ::=
  "\"" (
    [^"\\\x7F\x00-\x1F] |
    "\\" (["\\bfnrt] | "u" [0-9a-fA-F]{4})
  )* "\"" ws
number ::= ("-"? ([0-9] | [1-9] [0-9]{0,15})) ("." [0-9]+)? ([eE] [-+]? [0-9] [1-9]{0,15})? ws
ws ::= | " " | "\n" [ \t]{0,20}
)GBNF";

// ── RAG Prompt Builder ─────────────────────────────────────────
// Baut Prompt aus:
//   1. m->system_prompt (per AI.SET llm_id, "system", "..." setzbar)
//   2. Automatisch formatierter Context mit Source-Markern
//   3. User-Frage
// Die *inhaltliche* Instruktion kommt vollständig aus dem system_prompt —
// der C++-Code schreibt keine eigene Policy.

static std::string build_rag_prompt(LlmModel* m,
                                    const std::vector<std::pair<double, int>>& results,
                                    const std::vector<RagChunk>& chunks,
                                    const std::string& question)
{
    std::string context;
    int n = 0;
    for (auto& [score, idx] : results) {
        if (score < 0.01) continue;
        n++;
        context += "[Document " + std::to_string(n) + " — " + chunks[idx].source + "]\n";
        context += chunks[idx].text + "\n\n";
    }

    // System-Prompt: wenn der User via AI.SET einen eigenen gesetzt hat, diesen
    // verwenden. Sonst ein minimaler neutraler Fallback — keine Meinung zur
    // Quellenverwendung, keine Ablehnungs-Formulierung.
    std::string sys = m ? m->system_prompt : std::string();
    if (sys.empty()) sys = "You are a helpful assistant.";

    return
        "<|system|>\n" + sys + "\n\n"
        "Context:\n" + context + "<|end|>\n"
        "<|user|>\n" + question + "<|end|>\n<|assistant|>\n";
}

// ── Dense Embeddings via llama.cpp ──────────────────────────────
//
// Berechnet ein L2-normalisiertes Embedding für einen Text.
// Funktioniert mit Modellen die im Embedding-Modus initialisiert wurden
// (cparams.embeddings = true). Pooling: nutzt die llama.cpp-eigene Pooling-
// Strategie wenn gesetzt; sonst Mean-Pool über alle Token-Embeddings.

static std::vector<float> compute_dense_embedding(LlmModel* m, const std::string& text) {
    if (!m || !m->ctx) return {};
    auto tokens = tokenize(m->vocab, text, true);
    int n_ctx = llama_n_ctx(m->ctx);
    if ((int)tokens.size() > n_ctx) tokens.resize(n_ctx);
    if (tokens.empty()) return {};

    // Cache leeren — Embeddings sind stateless, wir wollen kein Carry-Over
    llama_memory_t mem = llama_get_memory(m->ctx);
    if (mem) llama_memory_clear(mem, true);

    // Encoder-only-Modelle (BERT/nomic/bge) brauchen llama_encode.
    // Decoder-Modelle (Phi-3, Llama etc.) brauchen llama_decode auch im Embedding-Modus.
    //
    // llama.cpp's llama_model_has_encoder() ist nicht immer zuverlässig für
    // BERT-ähnliche Modelle (z.B. nomic-embed). Daher probieren wir: wenn das
    // Modell explizit als Embedding-Modell geladen wurde UND einen Decoder hat,
    // nutze decode; sonst encode als Default.
    bool has_decoder = llama_model_has_decoder(m->model);
    bool use_encode;
    if (m->is_embedding_model) {
        // Embedding-Modell: nur decode verwenden wenn es einen echten Decoder gibt
        use_encode = !has_decoder;
    } else {
        // Normales Generierungs-Modell — immer decode
        use_encode = false;
    }

    llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
    if (use_encode) {
        if (llama_encode(m->ctx, batch) != 0) {
            throw std::runtime_error("Embedding encode failed");
        }
    } else {
        if (llama_decode(m->ctx, batch) != 0) {
            throw std::runtime_error("Embedding decode failed");
        }
    }

    int n_embd = llama_model_n_embd(m->model);
    if (m->embed_dim == 0) m->embed_dim = n_embd;

    // Pooling-Strategie ermitteln
    enum llama_pooling_type pt = llama_pooling_type(m->ctx);

    std::vector<float> out(n_embd, 0.0f);
    if (pt != LLAMA_POOLING_TYPE_NONE) {
        // Modell macht Pooling automatisch — sequence-Embedding holen
        const float* emb = llama_get_embeddings_seq(m->ctx, 0);
        if (emb) std::copy(emb, emb + n_embd, out.begin());
    } else {
        // Manuelles Mean-Pooling über alle Token-Embeddings
        int n = (int)tokens.size();
        int counted = 0;
        for (int i = 0; i < n; i++) {
            const float* emb = llama_get_embeddings_ith(m->ctx, i);
            if (!emb) continue;
            for (int k = 0; k < n_embd; k++) out[k] += emb[k];
            counted++;
        }
        if (counted > 0) for (auto& v : out) v /= (float)counted;
    }

    // L2-Normalisierung
    double norm = 0;
    for (float v : out) norm += (double)v * v;
    norm = std::sqrt(norm);
    if (norm > 0) for (auto& v : out) v = (float)((double)v / norm);

    return out;
}

// ── Register builtins ───────────────────────────────────────────

void register_llm_builtins(VM& vm) {

    // ── AI.LOAD_LLM(path$, [n_ctx], [n_gpu_layers]) -> id ──────

    vm.register_native("AI.LOAD_LLM", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_backend();
        std::string path = args[0].as_string()->data;
        int n_ctx = (args.size() >= 2) ? (int)args[1].to_int() : 2048;
        int n_gpu = (args.size() >= 3) ? (int)args[2].to_int() : 99; // 99 = all layers on GPU

        auto m = std::make_unique<LlmModel>();

        // Load model
        auto mparams = llama_model_default_params();
        mparams.n_gpu_layers = n_gpu;

        std::cerr << "Loading LLM: " << path << " (ctx=" << n_ctx << ", gpu_layers=" << n_gpu << ")..." << std::endl;

        // Diagnostics: how many backends did ggml find?
        size_t n_backends = ggml_backend_reg_count();
        std::string backend_list;
        for (size_t i = 0; i < n_backends; ++i) {
            ggml_backend_reg_t reg = ggml_backend_reg_get(i);
            const char* name = ggml_backend_reg_name(reg);
            if (i) backend_list += ", ";
            backend_list += (name ? name : "?");
        }

        g_llama_log_buf.clear();
        m->model = llama_model_load_from_file(path.c_str(), mparams);
        if (!m->model) {
            std::string err = "Failed to load LLM: " + path
                + " | backends(" + std::to_string(n_backends) + ")=[" + backend_list + "]"
                + " | llama_log:\n" + g_llama_log_buf;
            throw jdError(ErrCode::RUNTIME_ERROR, err);
        }

        m->vocab = llama_model_get_vocab(m->model);

        // Create context
        // n_batch == n_ctx, damit lange RAG-Prompts (System + Context-Chunks +
        // Question) in einem einzigen Decode passen, statt einen Assert auszulösen.
        auto cparams = llama_context_default_params();
        cparams.n_ctx = n_ctx;
        cparams.n_batch = n_ctx;
        cparams.n_ubatch = 512;

        m->ctx = llama_init_from_model(m->model, cparams);
        if (!m->ctx) {
            llama_model_free(m->model);
            throw jdError(ErrCode::RUNTIME_ERROR, "Failed to create LLM context");
        }

        // Create sampler
        rebuild_sampler(m.get());

        int id = g_next_llm_id++;
        std::cerr << "LLM loaded: id=" << id << std::endl;
        g_llms[id] = std::move(m);
        return Value::make_i64(id);
    });

    // ── AI.LOAD_EMBEDDINGS(path$, [n_ctx], [n_gpu_layers]) -> id ──
    // Lädt ein dediziertes Embedding-Modell (z.B. nomic-embed-text,
    // all-MiniLM-L6-v2). Diese ID kann an AI.RAG_CREATE als embed_llm_id
    // übergeben werden für echte dichte Embeddings statt TF-IDF.

    vm.register_native("AI.LOAD_EMBEDDINGS", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_backend();
        std::string path = args[0].as_string()->data;
        int n_ctx = (args.size() >= 2) ? (int)args[1].to_int() : 512;
        int n_gpu = (args.size() >= 3) ? (int)args[2].to_int() : 99;

        auto m = std::make_unique<LlmModel>();
        m->is_embedding_model = true;

        auto mparams = llama_model_default_params();
        mparams.n_gpu_layers = n_gpu;

        std::cerr << "Loading embedding model: " << path << " (ctx=" << n_ctx << ")..." << std::endl;
        m->model = llama_model_load_from_file(path.c_str(), mparams);
        if (!m->model)
            throw jdError(ErrCode::RUNTIME_ERROR, "Failed to load embedding model: " + path);

        m->vocab = llama_model_get_vocab(m->model);

        auto cparams = llama_context_default_params();
        cparams.n_ctx        = n_ctx;
        cparams.n_batch      = n_ctx;       // Embedding-Modelle: ganze Sequenz in einem Batch
        cparams.n_ubatch     = n_ctx;
        cparams.embeddings   = true;        // ← Embedding-Modus aktivieren
        cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;  // Mean-Pool über Sequenz
        cparams.no_perf      = true;

        m->ctx = llama_init_from_model(m->model, cparams);
        if (!m->ctx) {
            llama_model_free(m->model);
            throw jdError(ErrCode::RUNTIME_ERROR, "Failed to create embedding context");
        }
        m->embed_dim = llama_model_n_embd(m->model);

        int id = g_next_llm_id++;
        std::cerr << "Embedding model loaded: id=" << id << " dim=" << m->embed_dim << std::endl;
        g_llms[id] = std::move(m);
        return Value::make_i64(id);
    });

    // ── AI.SET_GRAMMAR(llm_id, gbnf$) ──────────────────────────
    // Setzt eine GBNF-Grammar als Constraint. Alle nachfolgenden CHAT/CHAT_RAW
    // Aufrufe produzieren nur Ausgaben die der Grammar entsprechen.
    // Leerer String entfernt die Grammar wieder.

    vm.register_native("AI.SET_GRAMMAR", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string gbnf = args[1].as_string()->data;
        auto* m = get_llm(id);
        m->grammar_gbnf = gbnf;
        rebuild_sampler(m);
        return Value::make_none();
    });

    // ── AI.CLEAR_GRAMMAR(llm_id) ───────────────────────────────

    vm.register_native("AI.CLEAR_GRAMMAR", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        m->grammar_gbnf.clear();
        rebuild_sampler(m);
        return Value::make_none();
    });

    // ── AI.SET_JSON_MODE(llm_id) ───────────────────────────────
    // Convenience: erzwingt valides JSON als Output (keine Schema-Constraints).

    vm.register_native("AI.SET_JSON_MODE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        m->grammar_gbnf = JSON_GBNF;
        rebuild_sampler(m);
        return Value::make_none();
    });

    // ── AI.CHAT_JSON(llm_id, prompt$) -> object ────────────────
    // Aktiviert JSON-Mode, generiert eine Antwort, parst sie als JSON, gibt
    // ein jdBasic-Objekt zurück (oder NONE bei Parse-Fehler). Die vorherige
    // Grammar wird nach dem Aufruf wiederhergestellt.

    vm.register_native("AI.CHAT_JSON", 2, 2, [&vm](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string user_msg = args[1].as_string()->data;
        auto* m = get_llm(id);

        // Grammar temporär auf JSON umschalten
        std::string saved_grammar = m->grammar_gbnf;
        m->grammar_gbnf = JSON_GBNF;
        rebuild_sampler(m);

        std::string raw, clean;
        try {
            std::string prompt = m->build_chat_prompt(user_msg);
            raw = generate(m, prompt);
            clean = LlmModel::clean_response(raw);
        } catch (...) {
            m->grammar_gbnf = saved_grammar;
            rebuild_sampler(m);
            throw;
        }

        // Grammar zurücksetzen
        m->grammar_gbnf = saved_grammar;
        rebuild_sampler(m);

        // History aktualisieren
        m->history.push_back({"user", user_msg});
        m->history.push_back({"assistant", clean});

        // JSON parsen via JSON.PARSE$ (existiert bereits in jdBasic)
        try {
            return vm.call_function("JSON.PARSE$", { Value::make_string(clean) });
        } catch (...) {
            // Fallback: rohen String zurück
            return Value::make_string(clean);
        }
    });

    // ── AI.EMBED_LLM(llm_id, text$) -> array of float ──────────
    // Berechnet das L2-normalisierte Embedding via geladenem LLM/Embedding-Modell.
    // (Die Sparse-TF-IDF-Variante mit nur einem String-Argument heißt weiterhin AI.EMBED.)

    vm.register_native("AI.EMBED_LLM", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string text = args[1].as_string()->data;
        auto* m = get_llm(id);
        auto vec = compute_dense_embedding(m, text);
        Value arr = Value::make_array();
        for (float v : vec) arr.as_array()->elements.push_back(Value::make_f64((double)v));
        return arr;
    });

    // ── AI.CHAT(id, prompt$) -> response$ ───────────────────────
    // Uses chat template and history for multi-turn conversations

    vm.register_native("AI.CHAT", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string user_msg = args[1].as_string()->data;
        auto* m = get_llm(id);

        std::string full_prompt = m->build_chat_prompt(user_msg);
        std::string raw = generate(m, full_prompt);
        std::string clean = LlmModel::clean_response(raw);

        // Add to history
        m->history.push_back({"user", user_msg});
        m->history.push_back({"assistant", clean});

        return Value::make_string(clean);
    });

    // ── AI.CHAT_STREAM(id, prompt$, callback) ───────────────────

    vm.register_native("AI.CHAT_STREAM", 3, 3, [&vm](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string user_msg = args[1].as_string()->data;
        Value callback = args[2];
        auto* m = get_llm(id);

        std::string full_prompt = m->build_chat_prompt(user_msg);
        std::string full_result;
        bool collecting_prefix = true;
        std::string prefix_buf;

        generate(m, full_prompt, [&](const std::string& token) -> bool {
            full_result += token;
            // Skip the <|assistant|> prefix before streaming to user
            if (collecting_prefix) {
                prefix_buf += token;
                if (prefix_buf.find("<|assistant|>") != std::string::npos ||
                    prefix_buf.find("\n") != std::string::npos ||
                    prefix_buf.size() > 20) {
                    collecting_prefix = false;
                    // Send any content after prefix
                    std::string cleaned = LlmModel::clean_response(prefix_buf);
                    if (!cleaned.empty()) {
                        try { vm.call_funcref(callback, {Value::make_string(cleaned)}); }
                        catch (...) { return false; }
                    }
                }
                return true;
            }
            try {
                Value result = vm.call_funcref(callback, {Value::make_string(token)});
                if (result.type == ValueType::BOOLEAN && !result.boolean) return false;
                return true;
            } catch (...) { return false; }
        });

        std::string clean = LlmModel::clean_response(full_result);
        m->history.push_back({"user", user_msg});
        m->history.push_back({"assistant", clean});
        return Value::make_string(clean);
    });

    // ── AI.CHAT_TOKENS(id, prompt$, [capacity]) -> chan handle ──
    //
    // Channel-flavoured streaming. Spawns a detached generation thread
    // that pushes each cleaned token into a freshly-opened channel and
    // closes it when generation completes (or the user closes the
    // channel from outside, which cancels the run). Returns the handle
    // immediately — caller drains the channel with the usual DO/RECV
    // /IS_EOF idiom.

    vm.register_native("AI.CHAT_TOKENS", 2, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string user_msg = args[1].as_string()->data;
        int64_t capacity = (args.size() >= 3) ? args[2].to_int() : 64;
        if (capacity < 0) capacity = 0;
        auto* m = get_llm(id);
        if (!m) throw std::runtime_error("AI.CHAT_TOKENS: invalid llm id " + std::to_string(id));

        auto ch = std::make_shared<Channel>();
        ch->capacity = capacity;
        int64_t handle = chan_register(ch);

        std::thread([ch, m, user_msg]() {
            try {
                std::string full_prompt = m->build_chat_prompt(user_msg);
                std::string full_result;
                bool collecting_prefix = true;
                std::string prefix_buf;

                // Helper: enqueue a token, blocking on capacity, returning
                // false if the channel was closed by the consumer (= cancel).
                auto push_token = [&ch](const std::string& tok) -> bool {
                    if (tok.empty()) return true;
                    std::unique_lock<std::mutex> lock(ch->mtx);
                    ch->cv_send.wait(lock, [&]() {
                        return (ch->capacity == 0) ||
                               (ch->buffer.size() < (size_t)ch->capacity) ||
                                ch->closed.load();
                    });
                    if (ch->closed.load()) return false;
                    ch->buffer.push_back(Value::make_string(tok));
                    ch->cv_recv.notify_one();
                    if (ch->capacity == 0) {
                        // Unbuffered rendezvous: wait until receiver drains.
                        ch->cv_send.wait(lock, [&]() {
                            return ch->buffer.empty() || ch->closed.load();
                        });
                        if (ch->closed.load()) return false;
                    }
                    return true;
                };

                generate(m, full_prompt, [&](const std::string& token) -> bool {
                    full_result += token;
                    if (collecting_prefix) {
                        prefix_buf += token;
                        if (prefix_buf.find("<|assistant|>") != std::string::npos ||
                            prefix_buf.find("\n") != std::string::npos ||
                            prefix_buf.size() > 20) {
                            collecting_prefix = false;
                            std::string cleaned = LlmModel::clean_response(prefix_buf);
                            return push_token(cleaned);
                        }
                        return true;
                    }
                    return push_token(token);
                });

                std::string clean = LlmModel::clean_response(full_result);
                m->history.push_back({"user", user_msg});
                m->history.push_back({"assistant", clean});
            } catch (...) {
                // Generation threw — fall through to close so the consumer
                // unsticks from RECV with EOF instead of hanging forever.
            }
            chan_close(*ch);
        }).detach();

        return Value::make_i64(handle);
    });

    // ── AI.CHAT_RAW(id, prompt$) -> response$ ───────────────────
    // Raw generation without chat template (for custom prompts)

    vm.register_native("AI.CHAT_RAW", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string prompt = args[1].as_string()->data;
        auto* m = get_llm(id);
        return Value::make_string(generate(m, prompt));
    });

    // ── AI.SET(id, key$, value) ─────────────────────────────────

    vm.register_native("AI.SET", 3, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string key = args[1].as_string()->data;
        auto* m = get_llm(id);

        if (key == "TEMPERATURE" || key == "temperature") {
            m->temperature = (float)args[2].to_double();
            rebuild_sampler(m);
        } else if (key == "TOP_P" || key == "top_p") {
            m->top_p = (float)args[2].to_double();
            rebuild_sampler(m);
        } else if (key == "TOP_K" || key == "top_k") {
            m->top_k = (int)args[2].to_int();
            rebuild_sampler(m);
        } else if (key == "MIN_P" || key == "min_p") {
            m->min_p = (float)args[2].to_double();
            rebuild_sampler(m);
        } else if (key == "MAX_TOKENS" || key == "max_tokens") {
            m->max_tokens = (int)args[2].to_int();
        } else if (key == "SEED" || key == "seed") {
            m->seed = (uint32_t)args[2].to_int();
            rebuild_sampler(m);
        } else if (key == "SYSTEM" || key == "system") {
            m->system_prompt = args[2].as_string()->data;
        } else {
            throw jdError(ErrCode::RUNTIME_ERROR, "AI.SET: unknown key '" + key + "'");
        }
        return Value::make_none();
    });

    // ── AI.TOKENIZE(id, text$) -> array of token ids ────────────

    vm.register_native("AI.TOKENIZE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string text = args[1].as_string()->data;
        auto* m = get_llm(id);
        auto tokens = tokenize(m->vocab, text, false);
        Value result = Value::make_array();
        for (auto t : tokens) result.as_array()->elements.push_back(Value::make_i64(t));
        return result;
    });

    // ── AI.DETOKENIZE(id, tokens_array) -> string ───────────────

    vm.register_native("AI.DETOKENIZE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        auto* arr = args[1].as_array();
        std::string result;
        for (auto& e : arr->elements) {
            result += token_to_string(m->vocab, (llama_token)e.to_int());
        }
        return Value::make_string(result);
    });

    // ── AI.LLM_INFO(id) -> object ──────────────────────────────

    vm.register_native("AI.LLM_INFO", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        Value info = Value::make_object();
        info.as_object()->set("n_ctx", Value::make_i64(llama_n_ctx(m->ctx)));
        info.as_object()->set("n_vocab", Value::make_i64(llama_vocab_n_tokens(m->vocab)));
        info.as_object()->set("temperature", Value::make_f64(m->temperature));
        info.as_object()->set("top_p", Value::make_f64(m->top_p));
        info.as_object()->set("top_k", Value::make_i64(m->top_k));
        info.as_object()->set("max_tokens", Value::make_i64(m->max_tokens));
        return info;
    });

    // ── AI.CLEAR_HISTORY(id) ───────────────────────────────────

    vm.register_native("AI.CLEAR_HISTORY", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        get_llm(id)->history.clear();
        return Value::make_none();
    });

    // ── AI.GET_HISTORY(id) -> array of {role, content} ──────────

    vm.register_native("AI.GET_HISTORY", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        Value result = Value::make_array();
        for (auto& msg : m->history) {
            Value entry = Value::make_object();
            entry.as_object()->set("role", Value::make_string(msg.role));
            entry.as_object()->set("content", Value::make_string(msg.content));
            result.as_array()->elements.push_back(std::move(entry));
        }
        return result;
    });

    // ── AI.TOKEN_COUNT(id, text$) -> number ─────────────────────

    vm.register_native("AI.TOKEN_COUNT", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string text = args[1].as_string()->data;
        auto* m = get_llm(id);
        auto tokens = tokenize(m->vocab, text, false);
        return Value::make_i64((int64_t)tokens.size());
    });

    // ── AI.COSINE_SIM(vec1, vec2) -> number ─────────────────────
    // Works with any two arrays of equal length

    vm.register_native("AI.COSINE_SIM", 2, 2, [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        if (a->elements.empty() || b->elements.empty())
            return Value::make_f64(0.0);
        size_t n = std::min(a->elements.size(), b->elements.size());
        double dot = 0, na = 0, nb = 0;
        for (size_t i = 0; i < n; i++) {
            double va = a->elements[i].to_double();
            double vb = b->elements[i].to_double();
            dot += va * vb;
            na += va * va;
            nb += vb * vb;
        }
        double denom = std::sqrt(na) * std::sqrt(nb);
        return Value::make_f64(denom > 0 ? dot / denom : 0.0);
    });

    // ── AI.NORMALIZE(vec) -> normalized vector ──────────────────

    vm.register_native("AI.NORMALIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array();
        double sum_sq = 0;
        for (auto& e : a->elements) { double v = e.to_double(); sum_sq += v * v; }
        double norm = std::sqrt(sum_sq);
        Value result = Value::make_array();
        if (norm > 0) {
            for (auto& e : a->elements)
                result.as_array()->elements.push_back(Value::make_f64(e.to_double() / norm));
        }
        return result;
    });

    // ══════════════════════════════════════════════════════════════
    // ── Function Calling / Tool Use ──────────────────────────────
    // ══════════════════════════════════════════════════════════════

    // ── AI.TOOL_ADD(id, name$, params$, description$, funcref) ──

    vm.register_native("AI.TOOL_ADD", 5, 5, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        LlmModel::Tool tool;
        tool.name = args[1].as_string()->data;
        tool.param_desc = args[2].as_string()->data;
        tool.description = args[3].as_string()->data;
        tool.funcref = args[4];
        // Remove existing tool with same name
        m->tools.erase(std::remove_if(m->tools.begin(), m->tools.end(),
            [&](auto& t) { return t.name == tool.name; }), m->tools.end());
        m->tools.push_back(std::move(tool));
        return Value::make_none();
    });

    // ── AI.TOOL_REMOVE(id, name$) ───────────────────────────────

    vm.register_native("AI.TOOL_REMOVE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        std::string name = args[1].as_string()->data;
        m->tools.erase(std::remove_if(m->tools.begin(), m->tools.end(),
            [&](auto& t) { return t.name == name; }), m->tools.end());
        return Value::make_none();
    });

    // ── AI.TOOL_CHAT(id, prompt$, [max_rounds]) -> response$ ────
    // Chat with automatic tool execution

    vm.register_native("AI.TOOL_CHAT", 2, 3, [&vm](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string user_msg = args[1].as_string()->data;
        int max_rounds = (args.size() >= 3) ? (int)args[2].to_int() : 5;
        auto* m = get_llm(id);

        if (m->tools.empty()) {
            // No tools — just do regular chat
            std::string prompt = m->build_chat_prompt(user_msg);
            std::string raw = generate(m, prompt);
            std::string clean = LlmModel::clean_response(raw);
            m->history.push_back({"user", user_msg});
            m->history.push_back({"assistant", clean});
            return Value::make_string(clean);
        }

        // Generate with tool support
        std::string prompt = m->build_chat_prompt(user_msg);
        std::string accumulated;

        for (int round = 0; round < max_rounds; round++) {
            std::string raw = generate(m, prompt);
            std::string response = LlmModel::clean_response(raw);

            // Check for tool calls
            auto calls = LlmModel::parse_tool_calls(response);
            if (calls.empty()) {
                // No tool calls — done
                accumulated += response;
                break;
            }

            // Execute each tool call
            std::string tool_results;
            for (auto& tc : calls) {
                // Find the tool
                LlmModel::Tool* found = nullptr;
                for (auto& t : m->tools) {
                    if (t.name == tc.name) { found = &t; break; }
                }
                if (!found) {
                    tool_results += "[Tool '" + tc.name + "' not found]\n";
                    continue;
                }

                // Build args as jdBasic values
                std::vector<Value> call_args;
                for (auto& a : tc.args) {
                    // Try number, but only if the *entire* string is numeric
                    // (otherwise "42 * 17" would be parsed as 42 and lose the rest)
                    bool is_number = false;
                    double d = 0;
                    if (!a.empty()) {
                        try {
                            size_t consumed = 0;
                            d = std::stod(a, &consumed);
                            // Allow trailing whitespace only
                            while (consumed < a.size() && std::isspace((unsigned char)a[consumed])) consumed++;
                            if (consumed == a.size()) is_number = true;
                        } catch (...) {}
                    }
                    if (is_number) call_args.push_back(Value::make_f64(d));
                    else           call_args.push_back(Value::make_string(a));
                }

                // Call the jdBasic function
                try {
                    Value result = vm.call_funcref(found->funcref, call_args);
                    tool_results += "[" + tc.name + " result: " + result.to_string() + "]\n";
                } catch (const std::exception& e) {
                    tool_results += "[" + tc.name + " error: " + std::string(e.what()) + "]\n";
                }
            }

            // Remove tool call markers from the response text
            std::string text_before;
            size_t last = 0;
            for (auto& tc : calls) {
                text_before += response.substr(last, tc.start_pos - last);
                last = tc.end_pos;
            }
            text_before += response.substr(last);
            accumulated += text_before;

            // Re-prompt with tool results
            prompt = m->build_chat_prompt(user_msg);
            // Inject the partial response and tool results
            prompt += accumulated + "\n" + tool_results + "\nContinue your answer using the tool results above.\n";
        }

        // Clean up and store in history
        std::string final_response = LlmModel::clean_response(accumulated);
        m->history.push_back({"user", user_msg});
        m->history.push_back({"assistant", final_response});
        return Value::make_string(final_response);
    });

    // ── AI.TOOL_LIST(id) -> array of tool names ─────────────────

    vm.register_native("AI.TOOL_LIST", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* m = get_llm(id);
        Value result = Value::make_array();
        for (auto& t : m->tools) {
            Value info = Value::make_object();
            info.as_object()->set("name", Value::make_string(t.name));
            info.as_object()->set("params", Value::make_string(t.param_desc));
            info.as_object()->set("description", Value::make_string(t.description));
            result.as_array()->elements.push_back(std::move(info));
        }
        return result;
    });

    // ── AI.FREE_LLM(id) ────────────────────────────────────────

    vm.register_native("AI.FREE_LLM", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        g_llms.erase(id);
        return Value::make_none();
    });

    // ══════════════════════════════════════════════════════════════
    // ── RAG (Retrieval Augmented Generation) ─────────────────────
    // ══════════════════════════════════════════════════════════════

    // ── AI.RAG_CREATE(llm_id, [chunk_size], [overlap], [embed_llm_id]) -> rag_id ─
    //
    // llm_id      : Modell für die Antwort-Generierung in RAG_QUERY
    // embed_llm_id: optional dediziertes Embedding-Modell (von AI.LOAD_EMBEDDINGS)
    //               — wenn 0/weggelassen, fällt der Store auf TF-IDF zurück.

    vm.register_native("AI.RAG_CREATE", 1, 4, [](const std::vector<Value>& args) -> Value {
        auto store = std::make_unique<RagStore>();
        store->llm_id = (int)args[0].to_int();
        if (args.size() >= 2) store->chunk_size = (int)args[1].to_int();
        if (args.size() >= 3) store->chunk_overlap = (int)args[2].to_int();
        if (args.size() >= 4) store->embed_llm_id = (int)args[3].to_int();
        int id = g_next_rag_id++;
        g_rags[id] = std::move(store);
        return Value::make_i64(id);
    });

    // ── AI.RAG_SAVE(rag_id, path$) ─────────────────────────────
    // Speichert Index, Chunks und Embeddings in eine Binärdatei.

    vm.register_native("AI.RAG_SAVE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string path = args[1].as_string()->data;
        try { it->second->save(path); }
        catch (const std::exception& e) { throw jdError(ErrCode::RUNTIME_ERROR, e.what()); }
        std::cout << "[RAG] saved " << it->second->chunks.size() << " chunks to " << path << std::endl;
        return Value::make_none();
    });

    // ── AI.RAG_BUILD_INDEX(rag_id, [M], [ef_construction]) ─────
    // Baut einen HNSW-Index über die Chunks eines dense-RAG. Beschleunigt
    // die Suche bei großen Indizes (>10K Chunks) drastisch.
    //
    //   M               : max Kanten pro Knoten (default 16, mehr = mehr Speicher + bessere Recall)
    //   ef_construction : Suchweite beim Aufbau (default 200, mehr = langsamer + besser)

    vm.register_native("AI.RAG_BUILD_INDEX", 1, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        int M = (args.size() >= 2) ? (int)args[1].to_int() : 16;
        int efc = (args.size() >= 3) ? (int)args[2].to_int() : 200;
        try { it->second->build_hnsw(M, efc); }
        catch (const std::exception& e) { throw jdError(ErrCode::RUNTIME_ERROR, e.what()); }
        std::cout << "[RAG] HNSW index built: M=" << M << " ef_construction=" << efc
                  << " elements=" << it->second->chunks.size() << std::endl;
        return Value::make_none();
    });

    // ── AI.RAG_LOAD(path$, [llm_id], [embed_llm_id]) -> rag_id ──
    // Lädt einen vorher gespeicherten Index. llm_id wird für die Antwort-
    // Generierung gebraucht; embed_llm_id muss bei dense-Indizes das
    // ursprüngliche Embedding-Modell sein (sonst sind Suchen unsinnig).

    vm.register_native("AI.RAG_LOAD", 1, 3, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
        auto store = std::make_unique<RagStore>();
        try { store->load(path); }
        catch (const std::exception& e) { throw jdError(ErrCode::RUNTIME_ERROR, e.what()); }
        if (args.size() >= 2) store->llm_id = (int)args[1].to_int();
        if (args.size() >= 3) store->embed_llm_id = (int)args[2].to_int();
        int id = g_next_rag_id++;
        std::cout << "[RAG] loaded " << store->chunks.size() << " chunks from " << path << std::endl;
        g_rags[id] = std::move(store);
        return Value::make_i64(id);
    });

    // ── AI.RAG_ADD(rag_id, text$, [source$]) ────────────────────

    vm.register_native("AI.RAG_ADD", 2, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string text = args[1].as_string()->data;
        std::string source = (args.size() >= 3) ? args[2].as_string()->data : "inline";
        it->second->add_text(text, source);
        return Value::make_i64((int64_t)it->second->chunks.size());
    });

    // ── AI.RAG_ADD_FILE(rag_id, filepath$) ──────────────────────
    // Erkennt automatisch PDFs (.pdf-Endung) und extrahiert deren Text.

    vm.register_native("AI.RAG_ADD_FILE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string path = args[1].as_string()->data;

        // PDF-Erkennung (case-insensitive)
        std::string lower_path = path;
        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        bool is_pdf = lower_path.size() > 4 && lower_path.substr(lower_path.size() - 4) == ".pdf";

        std::string content;
        if (is_pdf) {
            content = pdf_extract::extract_text(path);
            if (content.empty())
                throw jdError(ErrCode::RUNTIME_ERROR,
                    "PDF: konnte keinen Text extrahieren (komprimiert/verschlüsselt?): " + path);
        } else {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) throw jdError(ErrCode::FILE_NOT_FOUND, "Cannot open: " + path);
            std::stringstream ss;
            ss << file.rdbuf();
            content = ss.str();
        }
        it->second->add_text(content, path);
        return Value::make_i64((int64_t)it->second->chunks.size());
    });

    // ── AI.RAG_ADD_DIR(rag_id, dirpath$, [pattern$], [recursive]) ──
    // Indiziert ein ganzes Verzeichnis. pattern$ ist ein simpler Glob mit '*'
    // (z.B. "*.txt" oder "*.{md,txt,pdf}"). Default = alle Text-Dateien + PDFs.
    // recursive = true (default) durchläuft Unterverzeichnisse.

    vm.register_native("AI.RAG_ADD_DIR", 2, 4, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string dir = args[1].as_string()->data;
        std::string pattern = (args.size() >= 3) ? args[2].as_string()->data : std::string();
        bool recursive = (args.size() >= 4) ? args[3].to_int() != 0 : true;

        // Default: gängige Text-Endungen + PDF
        std::set<std::string> default_exts = {
            ".txt", ".md", ".markdown", ".rst", ".log", ".csv", ".tsv",
            ".json", ".xml", ".yaml", ".yml", ".toml", ".ini", ".cfg",
            ".html", ".htm", ".css", ".js", ".ts",
            ".cpp", ".hpp", ".cc", ".hh", ".c", ".h", ".cs", ".java", ".kt",
            ".py", ".rb", ".go", ".rs", ".swift", ".php", ".pl", ".lua",
            ".sh", ".bat", ".ps1", ".sql", ".jdb", ".pdf"
        };

        // Pattern-Auswertung: "*.ext" oder "*.{a,b,c}"
        // Wenn pattern leer ist, nutzen wir default_exts. Sonst extrahieren wir die Endungen.
        std::set<std::string> exts;
        if (pattern.empty()) {
            exts = default_exts;
        } else {
            // Sehr einfacher Parser: alles nach '.' bis '}' oder ',' oder Ende
            size_t dot = pattern.find('.');
            if (dot != std::string::npos) {
                std::string rest = pattern.substr(dot);
                // {ext1,ext2,ext3}
                size_t lb = rest.find('{');
                if (lb != std::string::npos) {
                    size_t rb = rest.find('}', lb);
                    if (rb != std::string::npos) {
                        std::string list = rest.substr(lb + 1, rb - lb - 1);
                        std::stringstream ls(list);
                        std::string item;
                        while (std::getline(ls, item, ',')) {
                            // Trim
                            while (!item.empty() && (item.front() == ' ' || item.front() == '.')) item.erase(0, 1);
                            while (!item.empty() && item.back() == ' ') item.pop_back();
                            if (!item.empty()) exts.insert("." + item);
                        }
                    }
                } else {
                    // Einzelne Endung wie "*.txt"
                    exts.insert(rest);
                }
            }
        }

        // Lowercase-Vergleich
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
            return s;
        };
        std::set<std::string> exts_lower;
        for (auto& e : exts) exts_lower.insert(lower(e));

        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
            throw jdError(ErrCode::FILE_NOT_FOUND, "Verzeichnis nicht gefunden: " + dir);

        int files_added = 0;
        int files_failed = 0;

        auto process_file = [&](const fs::path& p) {
            std::string ext = lower(p.extension().string());
            if (exts_lower.find(ext) == exts_lower.end()) return;

            std::string path_str = p.string();
            try {
                std::string content;
                if (ext == ".pdf") {
                    content = pdf_extract::extract_text(path_str);
                    if (content.empty()) {
                        std::cerr << "[RAG] PDF skipped (kein extrahierbarer Text): " << path_str << std::endl;
                        files_failed++;
                        return;
                    }
                } else {
                    std::ifstream f(path_str, std::ios::binary);
                    if (!f.is_open()) { files_failed++; return; }
                    std::stringstream ss;
                    ss << f.rdbuf();
                    content = ss.str();
                }
                if (content.empty()) return;
                it->second->add_text(content, path_str);
                files_added++;
            } catch (const std::exception& e) {
                std::cerr << "[RAG] " << path_str << ": " << e.what() << std::endl;
                files_failed++;
            }
        };

        if (recursive) {
            for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) { ec.clear(); continue; }
                if (entry.is_regular_file(ec)) process_file(entry.path());
            }
        } else {
            for (auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) { ec.clear(); continue; }
                if (entry.is_regular_file(ec)) process_file(entry.path());
            }
        }

        std::cout << "[RAG] " << files_added << " files added, "
                  << files_failed << " skipped, total chunks: "
                  << it->second->chunks.size() << std::endl;

        // Rückgabe: Map mit Statistiken
        Value result = Value::make_object();
        result.as_object()->set("files_added", Value::make_i64(files_added));
        result.as_object()->set("files_failed", Value::make_i64(files_failed));
        result.as_object()->set("total_chunks", Value::make_i64((int64_t)it->second->chunks.size()));
        return result;
    });

    // ── AI.RAG_SEARCH(rag_id, query$, [top_k]) -> array ────────

    vm.register_native("AI.RAG_SEARCH", 2, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string query = args[1].as_string()->data;
        int top_k = (args.size() >= 3) ? (int)args[2].to_int() : 3;

        auto results = it->second->search(query, top_k);
        Value arr = Value::make_array();
        for (auto& [score, idx] : results) {
            Value entry = Value::make_object();
            entry.as_object()->set("text", Value::make_string(it->second->chunks[idx].text));
            entry.as_object()->set("source", Value::make_string(it->second->chunks[idx].source));
            entry.as_object()->set("score", Value::make_f64(score));
            entry.as_object()->set("index", Value::make_i64(idx));
            arr.as_array()->elements.push_back(std::move(entry));
        }
        return arr;
    });

    // ── AI.RAG_QUERY(rag_id, question$, [top_k]) -> answer$ ────
    // Full RAG pipeline: search → build context → LLM generates answer

    vm.register_native("AI.RAG_QUERY", 2, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string question = args[1].as_string()->data;
        int top_k = (args.size() >= 3) ? (int)args[2].to_int() : 3;

        auto* store = it->second.get();
        auto* m = get_llm(store->llm_id);

        // Search for relevant chunks + build prompt
        auto results = store->search(question, top_k);
        std::string prompt = build_rag_prompt(m, results, store->chunks, question);

        std::string raw = generate(m, prompt);
        return Value::make_string(LlmModel::clean_response(raw));
    });

    // ── AI.RAG_QUERY_FULL(rag_id, q$, [top_k]) -> {answer, sources} ─
    // Wie RAG_QUERY, gibt aber zusätzlich die verwendeten Quellen zurück:
    //   { answer:  "…",
    //     sources: [ { source: "file.pdf", score: 0.42, text: "…" }, … ] }

    vm.register_native("AI.RAG_QUERY_FULL", 2, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string question = args[1].as_string()->data;
        int top_k = (args.size() >= 3) ? (int)args[2].to_int() : 3;

        auto* store = it->second.get();
        auto* m = get_llm(store->llm_id);

        auto results = store->search(question, top_k);

        Value src_arr = Value::make_array();
        for (auto& [score, idx] : results) {
            if (score < 0.01) continue;
            Value src = Value::make_object();
            src.as_object()->set("source", Value::make_string(store->chunks[idx].source));
            src.as_object()->set("score",  Value::make_f64(score));
            src.as_object()->set("text",   Value::make_string(store->chunks[idx].text));
            src.as_object()->set("index",  Value::make_i64(idx));
            src_arr.as_array()->elements.push_back(std::move(src));
        }

        std::string prompt = build_rag_prompt(m, results, store->chunks, question);

        std::string raw = generate(m, prompt);
        std::string answer = LlmModel::clean_response(raw);

        Value result = Value::make_object();
        result.as_object()->set("answer",  Value::make_string(answer));
        result.as_object()->set("sources", std::move(src_arr));
        return result;
    });

    // ── AI.RAG_QUERY_STREAM(rag_id, question$, callback, [top_k]) ─

    vm.register_native("AI.RAG_QUERY_STREAM", 3, 4, [&vm](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        std::string question = args[1].as_string()->data;
        Value callback = args[2];
        int top_k = (args.size() >= 4) ? (int)args[3].to_int() : 3;

        auto* store = it->second.get();
        auto* m = get_llm(store->llm_id);

        auto results = store->search(question, top_k);
        std::string prompt = build_rag_prompt(m, results, store->chunks, question);

        std::string full_result;
        bool skip_prefix = true;
        std::string prefix_buf;
        generate(m, prompt, [&](const std::string& token) -> bool {
            full_result += token;
            if (skip_prefix) {
                prefix_buf += token;
                if (prefix_buf.size() > 20 || prefix_buf.find("\n") != std::string::npos) {
                    skip_prefix = false;
                    std::string cleaned = LlmModel::clean_response(prefix_buf);
                    if (!cleaned.empty()) {
                        try { vm.call_funcref(callback, {Value::make_string(cleaned)}); }
                        catch (...) { return false; }
                    }
                }
                return true;
            }
            try {
                Value r = vm.call_funcref(callback, {Value::make_string(token)});
                if (r.type == ValueType::BOOLEAN && !r.boolean) return false;
                return true;
            } catch (...) { return false; }
        });
        return Value::make_string(LlmModel::clean_response(full_result));
    });

    // ── AI.RAG_INFO(rag_id) -> object ───────────────────────────

    vm.register_native("AI.RAG_INFO", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        auto* store = it->second.get();
        Value info = Value::make_object();
        info.as_object()->set("chunks", Value::make_i64((int64_t)store->chunks.size()));
        info.as_object()->set("vocab", Value::make_i64((int64_t)store->engine.doc_freq.size()));
        info.as_object()->set("docs", Value::make_i64(store->engine.total_docs));
        info.as_object()->set("llm_id", Value::make_i64(store->llm_id));
        info.as_object()->set("embed_llm_id", Value::make_i64(store->embed_llm_id));
        info.as_object()->set("mode", Value::make_string(store->dense_mode() ? "dense" : "tfidf"));
        info.as_object()->set("embed_dim", Value::make_i64(store->embed_dim));
        info.as_object()->set("chunk_size", Value::make_i64(store->chunk_size));
        info.as_object()->set("chunk_overlap", Value::make_i64(store->chunk_overlap));
        std::string idx = "linear";
        if (store->hnsw_index) idx = store->hnsw_dirty ? "hnsw_stale" : "hnsw";
        info.as_object()->set("index", Value::make_string(idx));
        return info;
    });

    // ── AI.RAG_CLEAR(rag_id) ────────────────────────────────────

    vm.register_native("AI.RAG_CLEAR", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_rags.find(id);
        if (it == g_rags.end()) throw jdError(ErrCode::RUNTIME_ERROR, "Invalid RAG id");
        it->second->chunks.clear();
        it->second->engine = EmbedEngine{};
        return Value::make_none();
    });

    // ── AI.RAG_FREE(rag_id) ─────────────────────────────────────

    vm.register_native("AI.RAG_FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        g_rags.erase(id);
        return Value::make_none();
    });

    // ── AI.EMBED(text$) -> array ────────────────────────────────
    // Standalone TF-IDF embedding (no model needed)

    vm.register_native("AI.EMBED", 1, 1, [](const std::vector<Value>& args) -> Value {
        static EmbedEngine standalone_engine;
        std::string text = args[0].as_string()->data;
        auto words = EmbedEngine::split_words(text);
        standalone_engine.add_document(words);
        auto emb = standalone_engine.embed(text);
        // Convert sparse to dense-ish: return [word, score, word, score, ...]
        Value result = Value::make_array();
        for (auto& [word, score] : emb) {
            Value pair = Value::make_array();
            pair.as_array()->elements.push_back(Value::make_string(word));
            pair.as_array()->elements.push_back(Value::make_f64(score));
            result.as_array()->elements.push_back(std::move(pair));
        }
        return result;
    });

    // ── AI.SIMILARITY(text1$, text2$) -> number ─────────────────
    // Quick text similarity without creating a RAG store

    vm.register_native("AI.SIMILARITY", 2, 2, [](const std::vector<Value>& args) -> Value {
        EmbedEngine engine;
        std::string t1 = args[0].as_string()->data;
        std::string t2 = args[1].as_string()->data;
        engine.add_document(EmbedEngine::split_words(t1));
        engine.add_document(EmbedEngine::split_words(t2));
        auto e1 = engine.embed(t1);
        auto e2 = engine.embed(t2);
        return Value::make_f64(EmbedEngine::similarity(e1, e2));
    });

    // ══════════════════════════════════════════════════════════════
    // ── Text Classifier (k-NN auf dichten Embeddings) ────────────
    // ══════════════════════════════════════════════════════════════

    auto cls_get = [](int id) -> ClassifierStore* {
        auto it = g_classifiers.find(id);
        if (it == g_classifiers.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "Invalid classifier id: " + std::to_string(id));
        return it->second.get();
    };

    // ── AI.CLASSIFIER_CREATE(embed_llm_id) -> cls_id ──────────
    // Erstellt einen leeren Text-Klassifikator. embed_llm_id muss auf ein
    // bereits via AI.LOAD_EMBEDDINGS geladenes Embedding-Modell zeigen.

    vm.register_native("AI.CLASSIFIER_CREATE", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto store = std::make_unique<ClassifierStore>();
        store->embed_llm_id = (int)args[0].to_int();
        int id = g_next_cls_id++;
        g_classifiers[id] = std::move(store);
        return Value::make_i64(id);
    });

    // ── AI.CLASSIFIER_ADD(cls_id, text$, label$) -> samples_count ──
    // Fügt ein einzelnes Trainingsbeispiel hinzu. Berechnet das Embedding.

    vm.register_native("AI.CLASSIFIER_ADD", 3, 3, [cls_get](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* s = cls_get(id);
        std::string text  = args[1].as_string()->data;
        std::string label = args[2].as_string()->data;
        s->add(text, label);
        return Value::make_i64((int64_t)s->samples.size());
    });

    // ── AI.CLASSIFIER_ADD_BATCH(cls_id, texts_arr, labels_arr) -> count ──
    // Fügt viele Trainingsbeispiele auf einmal hinzu. Schnell beim CSV-Import.

    vm.register_native("AI.CLASSIFIER_ADD_BATCH", 3, 3, [cls_get](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* s = cls_get(id);
        if (args[1].type != ValueType::ARRAY || args[2].type != ValueType::ARRAY)
            throw jdError(ErrCode::RUNTIME_ERROR, "CLASSIFIER_ADD_BATCH: args 2 and 3 must be arrays");
        auto& ta = args[1].as_array()->elements;
        auto& la = args[2].as_array()->elements;
        if (ta.size() != la.size())
            throw jdError(ErrCode::RUNTIME_ERROR, "CLASSIFIER_ADD_BATCH: texts and labels array length mismatch");

        // Fortschrittsanzeige für große Batches
        size_t total = ta.size();
        size_t next_report = total / 20; // alle 5%
        if (next_report < 50) next_report = 50;

        for (size_t i = 0; i < total; i++) {
            s->add(ta[i].to_string(), la[i].to_string());
            if ((i + 1) % next_report == 0 || i + 1 == total) {
                std::cerr << "[CLASSIFIER] " << (i + 1) << "/" << total
                          << " samples embedded (" << ((i + 1) * 100 / total) << "%)\r" << std::flush;
            }
        }
        std::cerr << std::endl;
        return Value::make_i64((int64_t)s->samples.size());
    });

    // ── AI.CLASSIFIER_PREDICT(cls_id, text$, [k]) -> object ───
    // Klassifiziert einen Text via k-NN. k default = 5.
    // Rückgabe: {
    //   label:       gewählte Klasse (Majority Vote, gewichtet mit Similarity)
    //   confidence:  Anteil der Gewinner-Klasse am Gesamtgewicht (0..1)
    //   neighbors:   Array der Top-k Nachbarn mit {score, label, text}
    //   votes:       Array der Klassen mit Zählungen {label, count}
    // }

    vm.register_native("AI.CLASSIFIER_PREDICT", 2, 3, [cls_get](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* s = cls_get(id);
        std::string text = args[1].as_string()->data;
        int k = (args.size() >= 3) ? (int)args[2].to_int() : 5;
        if (k < 1) k = 1;

        auto p = s->predict(text, k);

        Value result = Value::make_object();
        result.as_object()->set("label", Value::make_string(p.label));
        result.as_object()->set("confidence", Value::make_f64(p.confidence));

        Value nb_arr = Value::make_array();
        for (auto& [sim, idx] : p.neighbors) {
            Value nb = Value::make_object();
            nb.as_object()->set("score", Value::make_f64(sim));
            nb.as_object()->set("label", Value::make_string(s->samples[idx].label));
            nb.as_object()->set("text",  Value::make_string(s->samples[idx].text));
            nb.as_object()->set("index", Value::make_i64(idx));
            nb_arr.as_array()->elements.push_back(std::move(nb));
        }
        result.as_object()->set("neighbors", std::move(nb_arr));

        Value vt_arr = Value::make_array();
        for (auto& [lbl, cnt] : p.votes) {
            Value v = Value::make_object();
            v.as_object()->set("label", Value::make_string(lbl));
            v.as_object()->set("count", Value::make_i64(cnt));
            vt_arr.as_array()->elements.push_back(std::move(v));
        }
        result.as_object()->set("votes", std::move(vt_arr));
        return result;
    });

    // ── AI.CLASSIFIER_BUILD_INDEX(cls_id, [M], [ef_construction]) ──
    // Baut einen HNSW-Index für schnelle Prediction bei großen Trainings-Sets.

    vm.register_native("AI.CLASSIFIER_BUILD_INDEX", 1, 3, [cls_get](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* s = cls_get(id);
        int M = (args.size() >= 2) ? (int)args[1].to_int() : 16;
        int efc = (args.size() >= 3) ? (int)args[2].to_int() : 200;
        s->build_hnsw(M, efc);
        std::cout << "[CLASSIFIER] HNSW built: " << s->samples.size() << " samples, M=" << M << std::endl;
        return Value::make_none();
    });

    // ── AI.CLASSIFIER_SAVE(cls_id, path$) ─────────────────────

    vm.register_native("AI.CLASSIFIER_SAVE", 2, 2, [cls_get](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* s = cls_get(id);
        std::string path = args[1].as_string()->data;
        s->save(path);
        std::cout << "[CLASSIFIER] saved " << s->samples.size() << " samples to " << path << std::endl;
        return Value::make_none();
    });

    // ── AI.CLASSIFIER_LOAD(path$, embed_llm_id) -> cls_id ─────

    vm.register_native("AI.CLASSIFIER_LOAD", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
        auto store = std::make_unique<ClassifierStore>();
        store->load(path);
        store->embed_llm_id = (int)args[1].to_int();
        int id = g_next_cls_id++;
        std::cout << "[CLASSIFIER] loaded " << store->samples.size() << " samples from " << path << std::endl;
        g_classifiers[id] = std::move(store);
        return Value::make_i64(id);
    });

    // ── AI.CLASSIFIER_INFO(cls_id) -> object ──────────────────

    vm.register_native("AI.CLASSIFIER_INFO", 1, 1, [cls_get](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto* s = cls_get(id);
        Value info = Value::make_object();
        info.as_object()->set("samples", Value::make_i64((int64_t)s->samples.size()));
        info.as_object()->set("embed_dim", Value::make_i64(s->embed_dim));
        info.as_object()->set("embed_llm_id", Value::make_i64(s->embed_llm_id));
        info.as_object()->set("index", Value::make_string(s->has_hnsw() ? "hnsw" : "linear"));

        auto counts = s->label_counts();
        info.as_object()->set("num_labels", Value::make_i64((int64_t)counts.size()));
        Value labels = Value::make_array();
        for (auto& [lbl, cnt] : counts) {
            Value entry = Value::make_object();
            entry.as_object()->set("label", Value::make_string(lbl));
            entry.as_object()->set("count", Value::make_i64(cnt));
            labels.as_array()->elements.push_back(std::move(entry));
        }
        info.as_object()->set("labels", std::move(labels));
        return info;
    });

    // ── AI.CLASSIFIER_FREE(cls_id) ────────────────────────────

    vm.register_native("AI.CLASSIFIER_FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        g_classifiers.erase(id);
        return Value::make_none();
    });
}

#endif // LLM
