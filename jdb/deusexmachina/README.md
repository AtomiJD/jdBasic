# deusexmachina

Verteilter persönlicher AI-Agent ("Jarvis-Klasse") gebaut **vollständig in jdBasic**. Demo-Projekt für CHAN-Channels, MCP-Server-Integration, User-Tool-Maker, AI/LLM/RAG, FFI-DLL-Bridges und verteilte HTTP-Workflows.

> Beweis dass BASIC nie tot war.

## Status

**Phase A — Foundation: ✅ komplett** (Sprints 1-3, läuft auf Kernel allein)

| Sprint | Module | Asserts |
|---|---|---:|
| 1 | bus, config, persist | 12 + 8 + 9 |
| 2 | llm_brain (+ Phi-3-mini Integration) | 8 |
| 2+ | persist RAG smoke (+ bge-m3 Embeddings) | 12 |
| 3 | MCP-Server User-Tool-Registry + tools.jdb | C++ patch + 8 (E2E) |
| Polish | dispatch (HTTP-Receiver + Bus-Routing) | 10 |

**Total: 67 Asserts grün.**

**Phase B — Communication-Loop** beginnt 2026-05-09 parallel zur ARM-Maschinen-Arbeit (Telegram-Webhook + Outlook-Source + dispatch-Verteilung).

## Komponenten

```
jdb/deusexmachina/
  bus.jdb          # Pub/Sub-Topic-Bus (CHAN-basiert)
  config.jdb       # JSON-Loader mit mtime-Cache
  persist.jdb      # SQLite + Event-Log + RAG-Hook
  llm_brain.jdb    # AI.LOAD_LLM + AI.CHAT_TOKENS-Streaming + RAG-augmented ask
  dispatch.jdb     # HTTP-Receiver, mappt POST-Pfade auf Bus-Topics
  tools.jdb        # MCP-Tool-Handler-FUNCs
  sqlite.jdb       # FFI-Wrapper für sqlitebridge.dll (Kopie von jdb/sqlite.jdb)

  conf/            # JSON-Konfigurationen (per-host) — kommt Phase D mit Verteilung
  data/            # Laufzeit-Dateien (DB, Caches) — gitignored
  tools/           # MCP-Tool-Manifeste
    deus_echo.json   ' Connectivity-Smoke
    deus_health.json ' Runtime-Status JSON
    deus_now.json    ' Wallclock + Tick

  test_<mod>.jdb       # Unit-Tests
  test_persist_rag.jdb # Integration: SQLite + RAG (lädt bge-m3, ~5s)
  test_llm_brain.jdb   # Integration: Phi-3 + Streaming (~10s)
  test_mcp.sh          # End-to-End: MCP-Server-Roundtrip
  test_dispatch.jdb    # End-to-End: HTTP→Bus
  demo.jdb             # Phase-A-Foundation-Demo (alles zusammen, ~30s)
  run_unit_tests.sh    # Wrapper: cd + sqlitebridge.dll + alle Suites
```

## Build

Volles Flag-Set nötig:
```
./build.bat GFX IMGUI NATIVEC LLM ONNX HTTP MCPSERVER
```

`Features: HTTP, GFX, ImGui, ONNX, LLM, MCP` muss in `jdBasic --version` erscheinen.

## Vorbedingungen

- `bridges/sqlitebridge/sqlitebridge.dll` (build via `bridges/sqlitebridge/build.bat`)
- `models/Phi-3-mini-4k-instruct-q4.gguf` (~2.2 GB) — für llm_brain
- `models/bge-m3-Q4_K_M.gguf` (~700 MB) — für RAG-Embeddings
- CUDA-fähige GPU empfohlen (Phi-3 läuft auch CPU, ist aber deutlich langsamer)

## Schnell-Demos

### Foundation-Showcase (alle 4 Module zusammen, ~30s)
```bash
cd jdb/deusexmachina
cp ../bridges/sqlitebridge/sqlitebridge.dll .
../build/jdBasic.exe demo.jdb
```
Zeigt Persist+RAG+LLM+Bus+Dispatch im Zusammenspiel — ingestet 3 Wissens-Chunks, fährt einen ASYNC-Producer + sync-Consumer über Bus, fragt das LLM mit RAG-Kontext und streamt die Antwort token-für-token zurück.

### MCP-Server für Claude Code
```bash
./build/jdBasic.exe --mcp --tools jdb/deusexmachina/tools/
```
Exposiert `deus_echo`, `deus_health`, `deus_now` zusätzlich zu den eingebauten `jdb_*` Tools. Drop ein neues `tools/foo.json` rein, schreib `EXPORT FUNC tool_foo(args$) AS STRING` in irgendein .jdb das die Manifest referenziert, neustarten — Claude sieht das neue Tool sofort.

### Unit-Tests
```bash
jdb/deusexmachina/run_unit_tests.sh    # Sprint 1+3 Unit-Suites + MCP-E2E
```

### Integration-Tests (LLM-/RAG-Modelle, manuell)
```bash
cd jdb/deusexmachina
../build/jdBasic.exe test_persist_rag.jdb   # ~5s
../build/jdBasic.exe test_llm_brain.jdb     # ~10s
../build/jdBasic.exe test_dispatch.jdb      # ~3s, belegt Port 8765
```

## Architektur-Notizen

**Module-Globals überleben den ASYNC-FUNC-Fork nicht.** Jede ASYNC FUNC startet in einer frischen VM, in die nur `func_map` reinkopiert wird — Module-level `DIM g_topics AS MAP` ist im Async-Kontext nicht initialisiert. Konsequenz: cross-task State läuft über **CHAN-Handles als Argumente**, nicht über Module-Globals. Bus ist daher **main-VM-only** als Topic-Verzeichnis; Async-Producer/Consumer bekommen das Channel-Handle direkt übergeben.

**Dotted Natives brauchen Klammer-Form.** `AI.SET id, k, v` (SUB-Stil ohne Klammern) parst als Method-Access auf der Value `AI` und wirft "Cannot call method 'SET' on value". In Modulen die Native-Wrapper schreiben, immer `DIM rc = AI.X(...)` Form verwenden — schadet bei FUNCs nichts, rettet vor dem Wurf bei SUBs.

**HTTP-Server-Handler reden mit der Main-VM.** `HTTP.SERVER.START` pinnt `g_server_vm` auf die VM die `START` aufgerufen hat. Spätere POSTs laufen auf einem Background-Thread, holen sich aber unter Mutex die Main-VM für die Handler-Dispatch — Module-Globals sind also sichtbar (anders als bei ASYNC FUNCs). Die Handler-FUNC-Namen müssen GROSSGESCHRIEBEN ans HTTP.SERVER.ON_POST übergeben werden weil call_function exact-match auf dem Stringschlüssel macht.

**Rich-HTTP-Response via `__http_*`-Keys.** Handler returnt eine MAP mit `__http_status`, `__http_body`, `__http_headers`, `__http_content_type` → diese werden 1:1 auf die Response gemappt. Sonst wird ein zurückgegebener Map als JSON serialisiert oder ein String als text/html gerendert.

**TICK statt NOW_EPOCH.** `NOW_EPOCH` ist im no-vec-Set der VM aufgeführt, aber nicht als Native registriert. Bis zur Nachregistrierung nutzt persist.jdb `TICK()` (ms seit Programmstart, monotonic). Phase B/C kann eine Wallclock-Spalte nachziehen wenn echtes UNIX-epoch gebraucht wird.
