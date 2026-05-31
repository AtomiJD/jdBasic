# jdBasic RPG Demo

A Godot 4.6 sandbox showcasing **LLM-driven NPCs that actually live in a world**. Four characters with persistent memory, mood, affinity scores, story arcs and cross-NPC rumour-spread, all powered by a single local Qwen3-14B running in `jdBasic`-as-script.

It is meant as a tech demo, not a finished game. About 30 minutes of play loops through the full storyline.

![status: Phase 6 complete - Living World layer shipped 2026-05-31](https://img.shields.io/badge/status-Phase%206%20complete-brightgreen)

## What it can do

- **Procedural 3D terrain** generated from APL-style vector math in `jdBasic` (192x192 heightmap, 4 octaves of layered sin/cos, zero `FOR` loops).
- **Dungeon** with spike-traps, GPU particle fire, looted chests.
- **Day/Night cycle** with proper sun/moon, ambient colour shifts and a configurable day-to-night ratio.
- **Four NPCs** with full personas: Sir Gareth (knight), Maelyn (mage), Tylen (ranger), Vex (rogue). Each has a legend, lore-tags, a quest, and unique knowledge.
- **Quests** that auto-complete from world flags (no LLM grading needed). Mark complete via Journal or let the model emit `complete_quest`.
- **RAG-grounded dialog**: 6 markdown lore files indexed via `nomic-embed`, NPC answers cite king names, place names and history verbatim instead of hallucinating.
- **Affinity / Reputation system**: -100 to +100 per NPC, shifts from keyword matching (compliments, insults, deliveries) + LLM `shift_affinity` tool calls. Reputation tab in the Journal shows progress bars.
- **Story Arc system**: 3-act structure (`story.json`). Acts activate when flag combinations are satisfied; NPCs receive the current narrative beat in their system prompt and colour their responses accordingly.
- **Rumour propagation**: when you tell Tylen about strange tracks, Gareth and Vex already know about it next time you talk to them, with source attribution (*"I heard Tylen speak of them"*).
- **All text in JSON, not in code**: dialog rules, world lore, story acts, npc data, flag triggers, affinity descriptors. Adding a new character or quest is a JSON edit, not a code change.

## Requirements

| Item | Minimum | Recommended |
|------|---------|-------------|
| OS | Windows 10/11 64-bit | Windows 11 |
| GPU VRAM | 12 GB | 16 GB (full Qwen3-14B in VRAM) |
| RAM | 16 GB | 32 GB |
| Disk | 15 GB free | (model + assets) |
| Godot | 4.6.x | 4.6.3 stable |

CPU fallback works but is slow (a single reply takes 30-60 seconds instead of 1-2). An RTX 3060 12 GB or better is comfortable for the full 14B model; smaller cards can swap in Qwen3-7B Q4 (~4.5 GB) with no other code changes.

## Installation step-by-step

### 1. Clone the repository

```bat
git clone https://github.com/AtomiJD/cc.git D:\dev\cc
cd D:\dev\cc
```

Path is up to you, but the brain scripts use absolute paths in a few places, so simpler is better.

### 2. Install Godot 4.6

Download from <https://godotengine.org/download> (the Standard build, not the .NET one).

### 3. Build the jdBasic runtime DLL

The Godot addon needs `jdbrt.dll` compiled with the LLM flag, plus the bundled llama.cpp / CUDA runtime DLLs.

From the repo root:

```bat
build_rt.bat LLM
```

This requires Visual Studio 2022 Build Tools (MSVC v143 toolset). If you do not have them, install them via the Visual Studio Installer.

Output lands in `build/`. Now copy the DLLs into the addon folder:

```bat
copy build\jdbrt.dll                       godot\rpg-demo\addons\jdb_godot\bin\
copy build\llama.dll                       godot\rpg-demo\addons\jdb_godot\bin\
copy build\ggml*.dll                       godot\rpg-demo\addons\jdb_godot\bin\
copy build\cublas64_12.dll                 godot\rpg-demo\addons\jdb_godot\bin\
copy build\cublasLt64_12.dll               godot\rpg-demo\addons\jdb_godot\bin\
copy build\cudart64_12.dll                 godot\rpg-demo\addons\jdb_godot\bin\
```

The whole `bin/` folder is gitignored on purpose. If you ever change `src/llm.cpp` you have to repeat the copy step or Godot keeps loading the old binary.

### 4. Download the character pack (KayKit)

Free, but not redistributable through this repo. Grab it here:

  <https://kaylousberg.itch.io/kaykit-adventurers>

Extract into `godot/Assets/KayKit_Adventurers_2.0_FREE/`, then move the files Godot expects:

```bat
copy "godot\Assets\KayKit_Adventurers_2.0_FREE\Characters\gltf\*.glb" ^
     "godot\rpg-demo\assets\characters\"

copy "godot\Assets\KayKit_Adventurers_2.0_FREE\Animations\gltf\Rig_Medium\*.glb" ^
     "godot\rpg-demo\assets\animations\"
```

You also need the **Dungeon Remastered** pack from the same author for the dungeon section:

  <https://kaylousberg.itch.io/kaykit-dungeon-remastered>

Extract into `godot/Assets/KayKit_DungeonRemastered_1.1_FREE/`.

### 5. Download the LLM models

Two GGUF files, both via Hugging Face:

**Main dialog model** - Qwen3-14B Q4_K_M (~9 GB):

```bat
mkdir D:\dev\cc\models
curl -L -o D:\dev\cc\models\Qwen_Qwen3-14B-Q4_K_M.gguf ^
  https://huggingface.co/bartowski/Qwen_Qwen3-14B-GGUF/resolve/main/Qwen_Qwen3-14B-Q4_K_M.gguf
```

**Embedding model** - nomic-embed-text v1.5 Q4_K_M (~85 MB):

```bat
curl -L -o D:\dev\cc\models\nomic-embed-text-v1.5.Q4_K_M.gguf ^
  https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf
```

If you put them somewhere other than `D:\dev\cc\models\`, open `godot/rpg-demo/world/world.gd` and adjust the two `@export var` paths at the top of the file (or override them in the Godot inspector after opening the project).

### 6. First launch

1. Open `godot/rpg-demo/project.godot` in Godot 4.6.
2. Godot will import about 200 assets on first open; this takes ~2 minutes.
3. Press **F5** to run.
4. On first run the LLM is loaded into VRAM (~5 seconds on a 4070 Ti SUPER, much longer on CPU). You will see `[llm] GPU load took ... ms` in the console.
5. The 6 lore markdown files get indexed by `nomic-embed` (another ~5 seconds), then the world is ready.

If anything goes wrong, scroll the **Troubleshooting** section at the bottom of this README.

## Playing

Controls:

| Key | Action |
|-----|--------|
| `W A S D` | walk |
| `Shift` | sprint |
| `Space` | jump |
| Mouse | look around |
| `E` | open dialog with the nearest NPC |
| `Q` | open Quest journal (Quests tab) |
| `I` | open Inventory tab |
| `J` | open Journal (full tabs view incl. Reputation) |
| `Esc` | close any open panel |

NPCs are scattered around the spawn area:

- **Sir Gareth** (knight, sky blue), east of spawn, by the wolf path.
- **Maelyn** (mage, lavender), south-west of spawn, sketching plants.
- **Tylen** (ranger, green), north-east of spawn, near the river bend.
- **Vex** (rogue, amber), south-east of spawn, between the trees.

There are three chests with light loot scattered around, and a small dungeon north-west of spawn with a fire-trap corridor.

### What the storyline looks like

The demo is a 3-act mystery. The hills are uneasy. Wolves are bold. Something is happening near the old keep on the northern ridge.

**Act 1: Arrival in the Hills.** Talk to everyone. Pick up the three quests:
- Vex wants you to deliver a sealed parcel to Maelyn.
- Maelyn wants three moon-herbs from the southern slopes.
- Tylen wants you to scout the northern ridge.
- Gareth wants three wolf pelts.

**Act 2: Tracks Lead North** activates the moment you tell *any* NPC that you walked the ridge **and** that you saw strange tracks at the river bend. Both the local mood and the conversations of every other NPC will shift; they start mentioning the keep instead of their own quests.

**Act 3: Into the Keep** activates after you tell someone you investigated the old keep. From that point on Gareth and Maelyn read your wolf-pelt and herb-gathering quests as preparation for what is in the keep.

NPCs do not know your name unless you tell them. They do not know about the tracks unless someone told someone. Rumours propagate in source-attributed form.

### Tricks that work

- Be polite ("thanks", "danke", "ich helfe gerne") - affinity goes up.
- Be rude ("Pappnase", "Alter") - affinity goes down.
- Hand items over ("hier ist dein Paket") - delivery is auto-detected even if the LLM forgets to emit `accept_item`.
- German, English or mixed: NPCs will mostly reply in English but greetings get language-mirrored.

### Tricks that do not (yet) work

- Combat. There is no combat. Wolves are decorative.
- Inventory drag-and-drop. The Journal is read-only for inventory.
- Persistence across sessions. World state and affinity reset every launch.

## Configuration

All gameplay text lives in JSON files under `godot/rpg-demo/assets/data/` and `godot/rpg-demo/assets/lore/`. The code holds the logic, never the strings.

| File | What you can edit |
|------|-------------------|
| `data/npcs.json` | NPC names, personas, legends, quests, lore tags |
| `data/world_lore.json` | World facts every NPC knows about |
| `data/world_state.json` | Starting flags, mood descriptors, **flag-trigger keyword tables** |
| `data/dialog_rules.json` | All system lines, prompt labels, affinity descriptors, rumour templates |
| `data/story.json` | Story arc definitions (act titles, triggers, completion flags) |
| `data/chests.json` | Chest positions and contents |
| `data/dungeons.json` | Dungeon layout templates |
| `lore/*.md` | Free-form world lore indexed via RAG |
| `prompts/dialog_system.tmpl` | The master prompt template (Mustache-style placeholders) |

To add a new NPC: edit `npcs.json`, add a row to `world_map.txt` or set a spawn position, done. No code change.

To add a new flag: add it to `world_state.json` under `flags`, add a trigger entry under `flag_triggers` with `keywords` + `ctx` arrays, add a rumour template under `dialog_rules.json` `rumor_templates`. Done.

## Architecture overview

```
                              jdBasic VM (in-process)
   Godot 4.6   <-- vm.eval -->    |
                                  ├── npc_brain.jdb     (FSM, inventory, quests, affinity)
                                  ├── dialog_brain.jdb  (LLM dispatch, RAG, prompt build,
                                  │                      story progression, rumours)
                                  └── terrain_gen.jdb   (procedural heightmap, APL math)

                              llama.cpp (embedded in jdbrt.dll)
                                  ├── Qwen3-14B-Q4 K_M    (dialog + summary)
                                  └── nomic-embed-text    (RAG embeddings)
```

The two brain files are concatenated and loaded as a single eval block at boot so they share scope (`npcs` and `world_state` are global maps both files mutate).

The brain runs an ASYNC FUNC + CHAN prefetch loop: as soon as you walk within 30 m of an NPC, their greeting is generated in a worker thread. By the time you press `E`, the first reply is ready in cache.

## Troubleshooting

**`Error 126: The specified module could not be found` on launch.**
A required DLL is missing from `godot/rpg-demo/addons/jdb_godot/bin/`. Re-run step 3 from the install guide. If you built `jdbrt.dll` with `GFX IMGUI NATIVEC` instead of just `LLM`, you also need SDL3 and LLVM-C DLLs in there. Easier fix: rebuild with `build_rt.bat LLM` only.

**`AI.SET: unknown key 'repeat_penalty'`.**
You are on an older `jdbrt.dll`. Repeat step 3.

**LLM load fails or generates nothing.**
Verify the GGUF path. The console prints `[llm] GPU load took ...` only on success. If you see `[llm] GPU load took 0 ms` immediately followed by nothing, the model path is wrong.

**Out of VRAM.**
Edit `world/dialog_brain.jdb` line `g_llm = AI.LOAD_LLM(model_path, 8192, n_gpu)` and lower the second argument (context window) to 4096. Or swap Qwen3-14B for a smaller Q4 like Qwen3-7B - both run on the same code path.

**Conversation is stuck on "...thinking" forever.**
The LLM crashed mid-decode (rare on Qwen3, sometimes on extremely long histories). Press `Esc` to close the dialog, then `E` to retry. The brain has a 20-second safety net that auto-recovers the UI.

**NPCs answer with `<think>` blocks visible.**
The defensive parser strips these, but if you see them in the dialog itself you are on an older `dialog_brain.jdb`. Pull the latest from git.

**No goldene "A new chapter begins" lines when you trigger an act.**
Check the Godot console - if you see `[story] act_started: ...` there but no UI line, the dialog UI is on a stale build. Reload the scene with `F5`.

## License notes

- Code (`*.gd`, `*.jdb`, `*.cpp`, `*.h`, JSON, .md): MIT, see repo root `LICENSE`.
- KayKit assets: free per their license, **not redistributed** in this repo. You must download them from kaylousberg.itch.io.
- Qwen3-14B: Apache 2.0 (see Hugging Face model card).
- nomic-embed-text v1.5: Apache 2.0.
- Phi-3 and Llama-3 paths exist in the code as legacy fallbacks but are not the primary target anymore.

## Credits

Built by Atomi (AtomiJD on GitHub) with extensive AI-assisted iteration. The jdBasic language itself, its compiler, the embedded llama.cpp wrapper, the Godot GDExtension and the RAG engine are all part of the same parent `cc` repository.

Feedback, bug reports, ideas: open an issue on the parent repo.
