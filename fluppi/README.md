# Vallys Reise - Pirate Island RPG

A complete top-down JRPG written in **pure jdBasic** (~9,000 lines): Tiled maps, party system, turn-based battles, quests, an inventory, shops, dialogue and a music engine. It doubles as the repo's biggest real-world jdBasic program and as the GUI smoke test of the pre-commit gate.

## Run it

Needs a jdBasic build with `GFX IMGUI` (see [doc/BUILD.md](../doc/BUILD.md)). From anywhere:

```bash
./build/jdBasic.exe fluppi/rpg_demo.jdb
```

The game resolves all data relative to its own folder, so the working directory does not matter. It also compiles to a standalone EXE:

```bash
./build/jdBasic.exe -c fluppi/rpg_demo.jdb
```

**Controls:** arrow keys to move, Enter to confirm, Escape for the menu (saving lives there). The game speaks German - it started as a story for the author's family and keeps that charm.

## Layout

| File | Role |
|---|---|
| `rpg_demo.jdb` | Entry point - input wiring, boot, main loop |
| `RPG_ENGINE.jdb` | World engine: Tiled maps, movement, scenes, menus |
| `RPG_BATTLE.jdb` / `rpg_combat.jdb` | Turn-based battle system |
| `RPG_PARTY.jdb` / `rpg_inventory.jdb` | Party, items, equipment |
| `rpg_dialog.jdb` / `rpg_quest.jdb` | Dialogue and quest state machines |
| `rpg_music.jdb` / `sq_part15.jdb` | Music driven by the `SOUND.*` sequencer |
| `RPG_TYPES.jdb` / `rpg_assets.jdb` | Shared types and asset loading |
| `rpg_data/` | All content as JSON: characters, enemies, items, skills, quests, shops - plus the `.tmx` Tiled maps |
| `sprite_gen.jdb` / `transi.jdb` | Dev tools: AI sprite-sheet generation (needs an OpenAI key) |
| `doc/` | Design docs (German): project structure, tilemap guide, the story bible |

## Modding

The whole game is data-driven: add an enemy in `rpg_data/enemies.json`, a quest in `rpg_data/quests.json`, or open the maps in [Tiled](https://www.mapeditor.org/) - no engine changes needed. `doc/TILEMAP_GUIDE.md` explains the map conventions.
