# deusexmachina

Verteilter persönlicher AI-Agent ("Jarvis-Klasse") gebaut **vollständig in jdBasic**. Demo-Projekt für CHAN-Channels, MCP-Server-Integration, Tool-Maker, AI/LLM/RAG, FFI-DLL-Bridges und verteilte HTTP-Workflows.

> Beweis dass BASIC nie tot war.

## Status

**Phase A — Foundation (in Arbeit)**

| Sprint | Module | Tests |
|---|---|---|
| 1 | bus, config, persist | 12 + 8 + 9 = 29 grün |
| 2 | llm_brain | — |
| 3 | mcp_server-Erweiterung + Phase-A-Demo | — |

## Layout

```
deusexmachina/
  bus.jdb          # Pub/Sub Topic-Bus (CHAN-basiert)
  config.jdb       # JSON-Loader mit mtime-Cache
  persist.jdb      # SQLite + Event-Log + RAG-Hook
  sqlite.jdb       # Kopie von jdb/sqlite.jdb (FFI-Wrapper für sqlitebridge.dll)
  conf/            # JSON-Konfigurationen (per-host)
  data/            # Laufzeit-Dateien (DB, Caches) — ignored
  tools/           # MCP-Tool-Manifeste (kommt Sprint 3)
  tests/           # Cross-modul-Integrationstests (kommt später)
  test_<mod>.jdb   # Per-Modul-Unit-Tests
```

## Voraussetzungen

- jdBasic gebaut mit `GFX IMGUI NATIVEC LLM` Flags
- `bridges/sqlitebridge/sqlitebridge.dll` muss neben `jdBasic.exe` liegen (oder im cwd)
- Phase A nutzt nur Kernel — keine Distribution bis Sprint 4+

## Testen

Aus `deusexmachina/`:
```
../build/jdBasic.exe test_bus.jdb
../build/jdBasic.exe test_config.jdb
../build/jdBasic.exe test_persist.jdb
```

Pre-commit-Gate (aus Repo-Root):
```
build/jdBasic.exe deusexmachina/test_bus.jdb
build/jdBasic.exe deusexmachina/test_config.jdb
build/jdBasic.exe deusexmachina/test_persist.jdb
```

## Architektur-Notizen

**Module-Globals überleben den ASYNC-FUNC-Fork nicht.** Jede ASYNC FUNC startet in einer frischen VM, in die nur `func_map` reinkopiert wird — Module-level `DIM g_topics AS MAP` ist im Async-Kontext nicht initialisiert. Konsequenz: cross-task State läuft über **CHAN-Handles als Argumente**, nicht über Module-Globals. Bus ist daher **main-VM-only** als Topic-Verzeichnis; Async-Producer/Consumer bekommen das Channel-Handle direkt übergeben.

**TICK statt NOW_EPOCH.** `NOW_EPOCH` ist im no-vec-Set der VM aufgeführt, aber nicht als Native registriert. Bis zur Nachregistrierung nutzt persist.jdb `TICK()` (ms seit Programmstart, monotonic). Phase B/C kann eine Wallclock-Spalte nachziehen wenn echtes UNIX-epoch gebraucht wird.
