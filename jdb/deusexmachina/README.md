# deusexmachina

Distributed personal AI agent ("Jarvis class") built **entirely in jdBasic**. Demo project for CHAN channels, MCP server integration, user tool maker, AI/LLM/RAG, and distributed HTTP workflows.

> Proof that BASIC never died.

## Status

**Phase A - Foundation: complete** (Sprints 1-4, runs on the kernel alone)

| Sprint | Modules | Asserts |
|---|---|---:|
| 1 | bus, config, persist | 12 + 8 + 9 |
| 2 | llm_brain (+ Phi-3-mini integration) | 8 |
| 2+ | persist RAG smoke (+ bge-m3 embeddings) | 12 |
| 3 | MCP server user-tool registry + tools.jdb | C++ patch + 8 (E2E) |
| Polish | dispatch (HTTP receiver + bus routing) | 10 |
| 4 | modernization: SQL.* natives, NOW_EPOCH, path fixes | (same suites) |

**Phase B - Communication loop: first slice live**

| Sprint | Modules | Asserts |
|---|---|---:|
| B1 | telegram (long-poll connector), agent (loop core), llm_brain remote backend | 15 + 11 |

**Total: 93 asserts green.**

Sprint 4 replaced the FFI SQLite bridge with the built-in `SQL.*` natives
(SQLITE build flag), registered `NOW_EPOCH()` in the interpreter VM (the
event log now stores real wallclock epochs instead of `TICK()`), and fixed
all relative paths after the move to `jdb/deusexmachina/`.

Sprint B1 added the communication loop: `telegram.jdb` (Bot API via curl
long polling - no public URL or tunnel needed), `agent.jdb` (one loop
step: inbound -> event log -> optional RAG context -> injected brain FUNC
-> reply -> event log) and `LLM_BRAIN.ask_remote` (OpenAI-compatible
chat/completions endpoint, verified against a llama-server on the LAN).
deusexmachina stays a standalone demo project; voice channels come later.

## Components

```
jdb/deusexmachina/
  bus.jdb          # pub/sub topic bus (CHAN based)
  config.jdb       # JSON loader with mtime cache
  persist.jdb      # SQLite (SQL.* natives) + event log + RAG hook
  llm_brain.jdb    # AI.LOAD_LLM + AI.CHAT_TOKENS streaming + RAG ask + remote backend
  dispatch.jdb     # HTTP receiver, maps POST paths onto bus topics
  telegram.jdb     # Telegram Bot API connector (curl long polling)
  agent.jdb        # communication-loop core (brain injected as FUNC ref)
  tools.jdb        # MCP tool handler FUNCs

  conf/            # deus.example.json template; copy to deus.json (gitignored)
                   # and fill in the Telegram bot token / LLM backend
  data/            # runtime files (DB, caches) - gitignored
  tools/           # MCP tool manifests
    deus_echo.json   # connectivity smoke
    deus_health.json # runtime status JSON
    deus_now.json    # wallclock + tick

  test_<mod>.jdb       # unit tests
  test_persist_rag.jdb # integration: SQLite + RAG (loads bge-m3, ~5s)
  test_llm_brain.jdb   # integration: Phi-3 + streaming (~10s)
  test_mcp.sh          # end-to-end: MCP server roundtrip
  test_dispatch.jdb    # end-to-end: HTTP to bus
  demo.jdb             # Phase A foundation demo (everything together, ~30s)
  demo_phase_b.jdb     # Phase B loop: simulated chat, or real Telegram with token
  run_unit_tests.sh    # wrapper: cd + all suites
```

## Build

Full flag set required:
```
./build.bat HTTP GFX IMGUI NATIVEC MCPSERVER COM SERIAL LLM ONNX SQLITE
```

`jdBasic --version` must list at least `HTTP, GFX, ImGui, ONNX, LLM, MCP, SQLite`.

## Prerequisites

- `models/Phi-3-mini-4k-instruct-q4.gguf` (~2.2 GB) - for llm_brain
- `models/bge-m3-Q4_K_M.gguf` (~700 MB) - for RAG embeddings
- CUDA-capable GPU recommended (Phi-3 also runs on CPU, just much slower)

## Quick demos

### Foundation showcase (all 4 modules together, ~30s)
```bash
cd jdb/deusexmachina
../../build/jdBasic.exe demo.jdb
```
Shows persist+RAG+LLM+bus+dispatch working together - ingests 3 knowledge
chunks, runs an ASYNC producer + sync consumer over the bus, asks the LLM
with RAG context and streams the answer back token by token.

### MCP server for Claude Code
```bash
./build/jdBasic.exe --mcp --tools jdb/deusexmachina/tools/
```
Exposes `deus_echo`, `deus_health`, `deus_now` in addition to the built-in
`jdb_*` tools. Drop a new `tools/foo.json` in, write
`EXPORT FUNC tool_foo(args$) AS STRING` in any .jdb the manifest references,
restart - Claude sees the new tool immediately.

### Phase B communication loop
```bash
cd jdb/deusexmachina
../../build/jdBasic.exe demo_phase_b.jdb
```
Without config this runs a simulated conversation through
source -> bus -> agent -> sink with the stub brain. For the real thing:
`cp conf/deus.example.json conf/deus.json`, put a Telegram bot token in
(create one via @BotFather), optionally switch `llm.mode` to `remote`
(any OpenAI-compatible server) or `local` (GGUF via AI.LOAD_LLM), then
run again - the loop long-polls the bot and answers incoming chats.

### Unit tests
```bash
jdb/deusexmachina/run_unit_tests.sh    # unit suites + MCP E2E
```

### Integration tests (LLM/RAG models, manual)
```bash
cd jdb/deusexmachina
../../build/jdBasic.exe test_persist_rag.jdb   # ~5s
../../build/jdBasic.exe test_llm_brain.jdb     # ~10s
../../build/jdBasic.exe test_dispatch.jdb      # ~3s, binds port 8765
```

## Architecture notes

**Module globals do not survive the ASYNC FUNC fork.** Every ASYNC FUNC
starts in a fresh VM that only receives a copy of `func_map` - module-level
`DIM g_topics AS MAP` is not initialised in the async context. Consequence:
cross-task state travels via **CHAN handles passed as arguments**, not via
module globals. The bus is therefore **main-VM-only** as a topic directory;
async producers/consumers get the channel handle passed in directly.

**Dotted natives need the paren form.** `AI.SET id, k, v` (SUB style
without parens) parses as method access on the value `AI` and throws
"Cannot call method 'SET' on value". Modules wrapping natives always use
the `DIM rc = AI.X(...)` form - harmless for FUNCs, saves SUBs from the
throw.

**HTTP server handlers talk to the main VM.** `HTTP.SERVER.START` pins
`g_server_vm` to the VM that called `START`. Later POSTs run on a
background thread but grab the main VM under mutex for handler dispatch -
module globals are visible (unlike in ASYNC FUNCs). Handler FUNC names
must be passed UPPERCASE to `HTTP.SERVER.ON_POST` because `call_function`
does an exact match on the string key.

**Rich HTTP responses via `__http_*` keys.** A handler returning a MAP
with `__http_status`, `__http_body`, `__http_headers`,
`__http_content_type` gets those mapped 1:1 onto the response. Otherwise
a returned map is serialised as JSON and a string is rendered as
text/html.

**Timestamps are `NOW_EPOCH()`.** Registered in both runtimes (interpreter
and native `-c`); persist stores integer epoch seconds, comparable across
runs and hosts. `TICK()` remains the tool for intra-run relative timing.
