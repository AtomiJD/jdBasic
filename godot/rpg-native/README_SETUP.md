# jdRPG (native) - Setup auf einem neuen Windows-Rechner

Dieses Projekt ist ein Godot-Spiel, dessen ganze Logik in **jdBasic** geschrieben
ist (die `.jdb`-Dateien). Du brauchst **nichts zu kompilieren** - die fertigen
DLLs liegen schon in `addons/jdb_godot/bin/`.

## Was du brauchst

1. **Godot 4.6.3 (stable, Windows 64-bit)** - liegt auf dem Stick als
   `Godot_v4.6.3-stable_win64.exe`. Einfach irgendwohin kopieren, kein Installer.
2. **NVIDIA-Treiber** - aktuell halten. Die CUDA-Runtime-DLLs sind schon dabei,
   du musst CUDA *nicht* extra installieren.
3. **Visual C++ Redistributable 2015-2022 (x64)** - falls Godot beim Laden
   "Fehler 126 / Modul nicht gefunden" zeigt, installier das von Microsoft
   (`vc_redist.x64.exe`). Meist ist es schon da.
4. **Claude Code** - installierst du selbst; im Projektordner starten.

## Einrichten (einmalig)

1. Den ganzen Ordner `rpg-native/` vom Stick auf die Platte kopieren
   (z.B. nach `C:\Spiele\rpg-native\`). **Pfad egal** - das Projekt ist portabel.
2. Das LLM-Modell vom Stick in den Projekt-Unterordner `models/` legen:
   ```
   rpg-native\models\qwen2.5-7b-instruct-q4_k_m.gguf
   ```
   (Den `models`-Ordner ggf. anlegen.) Das ist das Gehirn der NPCs.
3. Godot starten -> **Import** -> die `project.godot` im `rpg-native`-Ordner
   wählen -> **Edit**.
4. **F5** (oder Play) drücken. Beim ersten Start lädt das Modell kurz (Splash),
   dann läuft die Welt.

## Wenn's auf deiner GPU klemmt

In `dialog.jdb` ganz oben (Abschnitt "LLM config") kannst du das einstellen:

```basic
DIM g_model_file = "qwen2.5-7b-instruct-q4_k_m.gguf"  ' Dateiname in models/
DIM g_gpu_layers = 99    ' 99 = alles auf die GPU (NVIDIA). 0 = nur CPU (langsam).
```

- **GTX 1660 Ti (6 GB):** `qwen2.5-7b` mit `g_gpu_layers = 99` passt gut.
- Lädt es nicht / Out-of-memory: `g_gpu_layers` kleiner setzen (z.B. `20`),
  oder ein kleineres Modell (z.B. `Phi-3-mini-4k-instruct-q4.gguf`) in `models/`
  legen und `g_model_file` darauf zeigen lassen.
- Kein NVIDIA: `g_gpu_layers = 0` (CPU, läuft, aber langsam).

## Sprache / Thema umschalten (Englisch-Fantasy <-> Deutsch-Piraten)

Das Spiel hat zwei Inhalts-Pakete:
- `assets/data/`      - Original (englisches Low-Fantasy-Koenigreich)
- `assets/data_ger/`  - Deutsch + Piraten-Thema (Bucht, Crew, Dublonen)

Umschalten ohne Code: **Project -> Project Settings -> Game -> Data Dir**
(oder in `project.godot` der Wert `data_dir`):
- `res://assets/data`      = Original
- `res://assets/data_ger`  = Deutsch-Piraten

Alle NPCs/Quests/Lore/Musik/Karten + der LLM-System-Prompt kommen aus diesem
Ordner. Die NPCs nutzen vorerst die vorhandenen Modelle (Piraten-Modelle aus
dem KayKit Pirate Kit kann man spaeter dazulegen). Ein eigenes Thema baust du,
indem du `assets/data/` kopierst, die Texte aenderst und `data_dir` darauf
zeigen laesst.

## Womit du arbeitest

- **Spiel-Logik:** die `.jdb`-Dateien (player, npc, dialog, dungeon, scatter,
  terrain, chest, daynight, music). Das ist jdBasic - Claude Code kennt die
  Sprache; frag es einfach.
- **3D-Objekte / Texturen:** in `assets/` (KayKit-Packs, `.glb` + `.png`).
- **Szenen:** `world.tscn` ist die Hauptszene.
- Du musst die DLLs in `addons/jdb_godot/bin/` **nie anfassen** - die sind das
  fertige jdBasic-Laufzeit-Modul. (Neue Versionen bringt dir Papa per Stick.)

Viel Spass! 🎮
