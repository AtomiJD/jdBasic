# Local AI Co-Processor for Coding Agents - plan

A jdBasic-powered local-AI "grunt worker" that an MCP-capable coding agent (Claude
& co.) commands to offload the work it is *bad at spending tokens on*: semantic
search, bulk extraction/classification, pre-digestion, recall, cheap verification,
and anything touching secrets. The agent keeps the hard reasoning; the local model
does volume, retrieval and privacy - free, parallel, offline.

Working name: **jdco** (provisional). Standalone offline TUI for air-gapped
admins/devs ("Schmerz 2") is a later second shell over the same engine.

## Guiding principle - division of labour
The local model is **reliable** at (1) embeddings/retrieval and (2) mechanical,
grammar-constrained extraction; **good enough** for (3) rough classification /
triage / first-pass summaries. Deep reasoning stays with the cloud agent.
Lean on it for **volume, search and privacy - keep the thinking.**

## Pain catalogue (first-hand, as the agent)

### A · Search & retrieval (daily, biggest lever)
- A1 only lexical search (grep); no semantic "find the bitcrush DSP". → semantic search (AI.EMBED + index) → spans + file:line. *reliable ★★★*
- A2 find the right file among thousands without reading 50 (burns context). → index returns spans, not full text. ★★★
- A3 "where is X used / who calls Y" conceptually, not text-match. → embedding neighbourhood + symbol graph. ★★★
- A4 find similar/duplicate code (refactor candidates). → cosine-similarity matrix (APL). ★★★
- A5 "did we already try this?" recall over git-log / sessions / memory. → semantic recall. ★★☆

### B · Volume (cheap, mechanical, high-token)
- B1 summarise many files (build a mental map) = N× tokens in my context. → summarize(paths). ★★☆ (quality scales with model)
- B2 classify many items (test failures by cause, logs by severity, files by purpose). → classify(items, labels) (AI.CLASSIFIER). ★★★
- B3 extract structured data (all signatures / SOUND.* calls / config keys / endpoints) as reliable JSON. → extract(paths, schema) grammar-constrained. ★★★
- B4 dedup/cluster (50 lint warnings → 8 root causes). → embedding cluster (APL). ★★★
- B5 triage/routing: what needs *me* (expensive) vs trivial. → triage(items) → buckets. ★★☆

### C · Context & memory
- C1 context-window pressure - forget early session parts. → recall(query) over transcript/memory. ★★★
- C2 pre-digest big files → read summary, drill in only where needed. → digest(path) with drill-down anchors. ★★☆
- C3 query a symbol/relationship graph instead of re-deriving. → persistent code index. ★★★

### D · Privacy boundary (what I must not ingest)
- D1 secrets/sensitive data must not enter cloud context; read locally, return only derived facts. → inspect_sensitive(path) → safe facts. ★★★ (privacy *is* the point)
- D2 secret/PII scan before I touch a file. → scan_secrets(paths). ★★★

### E · Verification (cheap checks before expensive reasoning)
- E1 cheap second opinion - local model critiques a finding before I spend tokens on false positives. → critique(claim, context). ★★☆
- E2 test-failure triage: cluster + guess root cause before digging (B2+B4). ★★☆

## Tool surface (the co-processor's MCP tools)
1. `index_build` / `index_update` - embed the repo, persistent, incremental
2. `search_semantic(query, k)` → spans + file:line  *(A1-A3, the heart)*
3. `find_similar(span|code)` → dedup/refactor  *(A4, B4)*
4. `extract(paths, json_schema)` → reliable structure  *(B3)*
5. `classify(items, labels)`  *(B2, E2)*
6. `summarize/digest(paths)`  *(B1, C2)*
7. `recall(query)` over memory/sessions/git  *(A5, C1)*
8. `critique(claim, context)`  *(E1)*
+ `inspect_sensitive` / `scan_secrets` privacy layer  *(D)*

## Priority
- **MVP (Tier 0):** `index_build` + `search_semantic` + `extract`. ~80% of the daily lever, dogfoodable on this repo immediately.
- **Tier 1:** `find_similar`, `classify`, `digest`.
- **Tier 2:** `recall`, `critique`, privacy layer.

## Architecture - RESOLVED 2026-06-07 (live-validated on this repo)
1. **Form** - a jdBasic `CO_*` module/workspace (VBASTACK pattern) + an `agentco` skill that documents the toolkit. Driven through the existing `jdbasic-stdio` MCP via `jdb_load`/`jdb_loadws` + `jdb_eval`; model + RAG stay warm in the persistent MCP VM. First-class MCP tools + a standalone TUI are later shells over the same engine.
2. **Models** - `nomic-embed-text-v1.5` (768-dim, fast) as the embedder, `bge-m3` as a quality fallback; `qwen2.5-3b` for extract/classify/critique, `qwen2.5-7b` to escalate. All local in `models/`, loaded in the LLM-MCP VM (offline).
3. **Index** - `AI.RAG_*` (dense embeddings + HNSW ANN + `RAG_SAVE`/`RAG_LOAD` persistence). No custom index. Retrieve-only via `AI.RAG_SEARCH` (no LLM, no generation).
4. **Chunking** - RAG built-in size+overlap (400/40 proven; found the bitcrush code at 0.71). Symbol-aware chunks + per-chunk line numbers are a refinement (`source` currently = file only; derive the line by locating the chunk text, or enhance chunking later).
5. **Persistence/scope** - one RAG per repo, `AI.RAG_SAVE` to `.jdco/<repo>.rag`, `AI.RAG_LOAD` at session start; incremental re-index on demand / git-diff-driven later.

## Validated jdBasic API (the building blocks)
- MCP server must be the LLM build: `build.bat HTTP GFX LLM ONNX NATIVEC MCPSERVER`, staged into `mcp-runtime/` (the `.mcp.json` path already points there). `OS.FEATURE` bare = NONE (needs an arg; ignore).
- `eid = AI.LOAD_EMBEDDINGS(path, ctx, gpu_layers)` → embedder id.
- `vec = AI.EMBED_LLM(eid, text)` → 768-dim vector. **NOT `AI.EMBED`** (that returns per-token `[[word,score]]`).
- `AI.COSINE_SIM(a, b)` → discriminates (NL query vs relevant code ~0.71 vs noise ~0.40).
- `rag = AI.RAG_CREATE(llm_id_or_0, chunk_size, chunk_overlap, eid)` - works with `llm_id=0` for retrieve-only.
- `AI.RAG_ADD_DIR(rag, dir, pattern, recursive)` → {files_added, total_chunks}; `AI.RAG_INFO(rag)`; `AI.RAG_ADD_FILE`.
- `AI.RAG_BUILD_INDEX(rag, M, ef)` - **needs parens** (dotted-call). Linear is fine under ~thousands of chunks.
- `hits = AI.RAG_SEARCH(rag, query, top_k)` → array of `{text, source, score}` - the retrieve-only core.
- `AI.RAG_SAVE(rag, path)` / `AI.RAG_LOAD(path, llm_id, eid)`; also `AI.RAG_QUERY_FULL` (retrieve+generate, needs LLM) and `AI.RAG_QUERY_STREAM`.

## The thin CO_* layer (MVP)
- `CO_OPEN(repo)` - load embedder + `RAG_LOAD .jdco/<repo>.rag` (or note "run CO_INDEX first").
- `CO_INDEX(dir, pattern)` - `RAG_CREATE(0,..,eid)` + `RAG_ADD_DIR` + `RAG_BUILD_INDEX` + `RAG_SAVE`.
- `CO_SEARCH(query, k)` - `RAG_SEARCH` → formatted hits (source + score + text, line derived).
- `CO_EXTRACT(paths, schema)` / `CO_CLASSIFY` / `CO_SIMILAR` / `CO_DIGEST` - Tier 1+.

(Status: catalogue + architecture agreed and live-validated 2026-06-07; MVP build next.)
