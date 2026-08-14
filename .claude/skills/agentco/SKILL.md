---
name: agentco
description: Semantic code search over any repo via a local AI co-processor (the CO_* toolkit on a jdBasic MCP server). Use when you need to FIND code by meaning ("where is X handled", "the code that does Y") rather than exact text - it augments grep with embeddings retrieval and returns file:line. Local, offline, no API key. Works for any project / language.
---

# Agent Co-Processor - semantic code search (generic)

Find code by *concept* instead of guessing keywords or reading many files. The
co-processor embeds a repo's files and answers natural-language queries with
`file:line` spans - saving the read-many-files token cost. It **augments** grep
(great on descriptive/commented code + docs), it does not replace it (terse
uncommented math still searches weakly - grep those). Local, offline, project- and
language-agnostic.

## Safety - never shell out from inside the VM
**Do NOT call `OS.EXEC` / `OS.LOAD` (or any subprocess) from `jdb_eval`.** The MCP
server speaks over stdio; `_popen` inside it **deadlocks** (even `git --version`
hangs forever). YOU run shell commands with your own **Bash tool**, outside the VM,
and hand jdBasic a file list. If an eval ever hangs anyway: `jdb_stop` → `jdb_status`
→ retry (no server restart needed).

## Prerequisites
1. The jdBasic MCP server must be the **LLM build** (has `AI.*`). Check:
   `jdb_eval` → `PRINT AI.EMBED_LLM(AI.LOAD_EMBEDDINGS("<embed.gguf>",2048,99),"x")`
   returns a 768-float array, not "Undefined function". If it's a NONE-feature
   build, it needs rebuilding with LLM (ask the user; for Atomi see `project_agent_coprocessor`).
2. An **embedding GGUF** model on disk (e.g. `nomic-embed-text-v1.5`, `bge-m3`).
   `jdb/demos/ai/coproc.jdb` defaults `CO_EMB_PATH$` to Atomi's box; on any other machine
   set it first: `jdb_eval CO_EMB_PATH$ = "/path/to/nomic-embed-text.gguf"`.

## Workflow (any repo)
```
1. Load the toolkit once:
   jdb_load  path="<.../jdb/demos/ai/coproc.jdb>"

2. Build the index (first time, or after big changes). YOU build the file list with
   Bash (git ls-files respects .gitignore -> no node_modules/build/vendor), then
   jdBasic embeds it:

   Bash:  git -C <ROOT> ls-files \
            | grep -iE '\.(c|cc|cpp|cxx|h|hpp|py|js|jsx|ts|tsx|go|rs|java|cs|rb|php|swift|kt|sh|lua|jdb|md|txt|sql|vue|svelte|html|css)$' \
            | grep -vE '(^|/)(libs|godot-cpp|build|node_modules|dist|vendor|third_party)/' \
            | sed 's|^|<ROOT>/|' > <ROOT>/.jdco_files.txt
   jdb_eval  CO_INDEX_FROM_LIST("<ROOT>", "<ROOT>/.jdco_files.txt")
             -> "N files indexed, M chunks -> <ROOT>/.jdco_index.rag"  (~80 s for ~900 files)

3. Later sessions - just reload the saved index (fast, no re-embed):
   jdb_load  path="<.../jdb/demos/ai/coproc.jdb>"
   jdb_eval  CO_OPEN("<ROOT>")

4. Search:
   jdb_eval  CO_SEARCH("where is X handled", 5)
             -> [score] file:line_start-line_end + 150-char preview
   Then Read the file at that range for full context.
```

## Persistence & staleness
The index is **persisted to `<ROOT>/.jdco_index.rag`** - build once, `CO_OPEN` each
session (the MCP VM loses in-memory state on restart; the file survives). It is a
**snapshot**: after meaningful code changes, re-run `CO_INDEX_FROM_LIST`. (Incremental
git-diff reindex is on the backlog.) `.jdco*` is gitignored.

## CO_* surface

**Tier 0 - semantic search (embeddings, no LLM, no token cost beyond the hits):**
- `CO_INDEX_FROM_LIST(root, listfile)` - embed the listed files, build HNSW, save. (primary)
- `CO_OPEN(root)` - reload the saved index.
- `CO_SEARCH(query, k)` - top-k `{file:line, score, preview}` (retrieve-only).
- `CO_INDEX(dir)` - convenience: index one dir tree by extension (`AI.RAG_ADD_DIR`). Replaces the active index.

**Tier 1 - bulk LLM offload (loads `CO_LLM` = qwen2.5-3b on first use; the local
model does volume, you stay the reasoner).** Measured ~15x token reduction vs reading.
- `CO_DIGEST$(path)` / `CO_DIGEST path` - summarize one file in 2-4 lines. **Strong** - accurate "what does this do + key symbols". The win: map N files for ~1/15th the read cost.
- `CO_CLASSIFY$(item, labels)` - label one item into one of `labels` (comma list). **Strong** - e.g. triage error lines (4/4 correct).
- `CO_EXTRACT(path, instruction)` - grammar-constrained JSON, returns an OBJECT. **Strong for fixed-schema** ("pull fields X,Y as {x,y}"). **Weak for enumerate-all** - the 3b under-enumerates.

**The division of labour:** ENUMERATE with grep / `CO_SEARCH` (reliable); JUDGE each
item with the LLM (`CO_DIGEST`/`CO_CLASSIFY`/`CO_EXTRACT`). Never ask the small model to
"list all X" - that is grep's job. Files are clipped to `CO_MAX_CHARS` (9000) to fit context.
- State (`CO_EID`, `CO_RAG`, `CO_LLM`) stays warm across `jdb_eval` calls - load once, many calls.

## API facts (validated, non-obvious)
- Embedding vector = **`AI.EMBED_LLM(emb_id, text)`** → 768-dim. NOT `AI.EMBED` (returns per-token `[[word,score]]`).
- `AI.RAG_CREATE(0, chunk, overlap, emb_id)` - llm_id **0** = retrieve-only (you reason, no generation).
- `AI.RAG_SEARCH(rag, query, k)` → `[{text, source, line_start, line_end, score}]`.
- `AI.RAG_BUILD_INDEX(rag, 16, 200)` needs parens. `AI.RAG_SAVE`/`AI.RAG_LOAD` persist (binary v3 = with line numbers).

## Limits & reading the scores
- ~0.75+ strong, ~0.6-0.7 plausible, <0.5 likely noise.
- Strong on descriptive/commented code + docs; weak on terse uncommented math - grep those.
- Only the indexed files are searched - index the whole repo (the git-ls-files recipe), not one subdir, for good recall.

## When NOT to use
- Exact symbol/string you already know → grep is faster and exact.
- A file you've already located → just Read it.

## Sibling skill
agentco is ONE warm co-processor VM. For **independent compute over many items in
parallel** (batch analysis, per-dataset crunching), fan out `jdbasic-worker`
processes via the **`jdbswarm`** skill instead. Common pattern: agentco to
find/triage the work-list, jdbswarm to process it in parallel, you synthesize.
