# jdRPG Projekt-Struktur

## Verzeichnisbaum

```
fluppi/
├── rpg_demo.jdb                    ← Spielstart
├── rpg_engine.jdb                  ← Engine
├── rpg_*.jdb                       ← Module
│
├── rpg_data/                       ← Spieldaten (JSON)
│   ├── game.json                   ← Hauptkonfiguration
│   ├── characters.json             ← Vally, Hasi, Möhrchen
│   ├── enemies.json                ← Monster-Definitionen
│   ├── items.json                  ← Gegenstände
│   ├── skills.json                 ← Fähigkeitenbäume
│   └── savegame.json               ← Spielstand (auto-generiert)
│
├── maps/                           ← ALLE Spielkarten (NEU)
│   ├── vally_house.tmx             ← 1. Vallys Haus (klein, 10x8)
│   ├── village.tmx                 ← 2. Dorf (mittel, 30x30)
│   ├── beach_path.tmx              ← 3. Strandweg (schmal, 40x15)
│   └── harbor_city.tmx             ← 4. Hafenstadt (groß, 60x40)
│
├── sprites/                        ← Charakter-Sprites (NEU, umbenannt)
│   ├── vally.png                   ← Spieler-Spritesheet
│   ├── hasi.png                    ← Hasi-Spritesheet
│   ├── moehrchen.png               ← Möhrchen-Spritesheet
│   ├── blobb.png                   ← Monster
│   └── ...                         ← Weitere Charaktere
│
├── tiled/                          ← Tiled Roh-Assets (NICHT im Spiel)
│   ├── tilesets/                   ← Gemeinsame Tileset-Bilder
│   │   ├── terrain.png             ← Boden (Gras, Erde, Stein)
│   │   ├── terrain.tsx
│   │   ├── beach.png               ← Strand + Schaum
│   │   ├── beach.tsx
│   │   ├── water.png               ← Wasser/Ozean
│   │   ├── water.tsx
│   │   ├── interior.png            ← Möbel, Boden, Wände
│   │   ├── interior.tsx
│   │   ├── collision.png           ← 1 rotes 32x32 Tile
│   │   ├── collision.tsx
│   │   └── ...
│   ├── props/                      ← Prop-Bilder
│   │   ├── props.png
│   │   └── atlas-props-sprites/
│   ├── grass/                      ← Deine Gras-Region Assets
│   ├── pirat/                      ← Deine Piraten-Region Assets
│   ├── village/                    ← Deine Dorf-Region Assets
│   └── interior/                   ← Deine Interior-Assets
│
├── fonts/
│   └── yoster.ttf
└── doc/
    ├── TILEMAP_GUIDE.md
    └── PROJECT_STRUCTURE.md
```

## Warum diese Struktur?

### `maps/` — Fertige Spielkarten
- Alle TMX-Dateien an EINEM Ort
- Die Engine sucht Maps hier
- TSX-Referenzen in den TMX zeigen auf `../tiled/tilesets/`

### `sprites/` — Charakter-Spritesheets
- Nur die Sprites die im Spiel geladen werden
- Kein Durcheinander mit Tileset-Bildern

### `tiled/` — Arbeitsverzeichnis für Tiled
- Hier liegen alle Tileset-Quelldateien
- Die TMX-Dateien in `maps/` referenzieren diese per relativem Pfad
- Wird NICHT direkt vom Spiel geladen

## Die 4 Maps im Detail

### Map 1: `vally_house.tmx` (Vallys Haus)
- **Größe:** 10x8 Tiles (320x256 Pixel)
- **Kamera:** Fest (`camera_fixed=true`)
- **Tilesets:** Interior-Tileset
- **Layer:** ground_base (Holzboden), collision, decor_below (Teppich, Möbel)
- **Objekte:**
  - `player_spawn "default_spawn"` an der Tür
  - `portal "exit_door"` → `village.tmx`, spawn `"from_vally_house"`
  - `chest "chest_vally_01"` → Vallys Schatz
  - `sign "vallys_diary"` → "Heute fängt mein Abenteuer an!"
- **So lernst du:** Grundlagen — Layer, Collision, ein Portal

### Map 2: `village.tmx` (Dorf)
- **Größe:** 30x30 Tiles (960x960 Pixel)
- **Tilesets:** Grass-Terrain, Village-Buildings, Props
- **Layer:** water, ground_base, ground_path, collision, decor_below, decor_above
- **Objekte:**
  - `player_spawn "default_spawn"` im Dorfzentrum
  - `player_spawn "from_vally_house"` vor Vallys Haus
  - `player_spawn "from_beach"` am Südausgang
  - `npc "Möhrchen"` → Dialog, rekrutierbar
  - `npc "Händlerin"` → is_shop=true (Tränke, Waffen)
  - `portal "vally_house_door"` → `vally_house.tmx`
  - `portal "moehrchen_house_door"` → `moehrchen_house.tmx` (später)
  - `portal "to_beach"` → `beach_path.tmx`
  - `monster "blobb_village_01"` am Dorfrand
- **So lernst du:** Mehrere Portale, NPCs, Monster, Shop

### Map 3: `beach_path.tmx` (Strandweg)
- **Größe:** 40x15 Tiles (1280x480 Pixel)
- **Tilesets:** Beach, Wasser, Gras-Platform
- **Layer:** water, ground_base, ground_beach, collision, decor_below
- **Objekte:**
  - `player_spawn "from_village"` links
  - `player_spawn "from_harbor"` rechts
  - `portal "to_village"` links → `village.tmx`, spawn `"from_beach"`
  - `portal "to_harbor"` rechts → `harbor_city.tmx`, spawn `"from_beach"`
  - `npc "Hasi"` → Dialog, rekrutierbar (am Strand)
  - `monster "blobb_beach_01/02"` am Weg
- **So lernst du:** Verbindungskarte, Beach-Tileset, Wasser-Animation

### Map 4: `harbor_city.tmx` (Hafenstadt = vally_home)
- **Größe:** 60x40 Tiles (1920x1280 Pixel)
- **Tilesets:** Pirat-Tilesets (die bestehenden)
- **Basiert auf:** Deine bestehende `vally_home.tmx` — umbenannte Layer
- **So lernst du:** Große Map, viele Deko-Layer, Object-Layer Props

---

## Deine Tileset-Sammlung — Tipps

### Was du SOFORT verwenden kannst:

**Für Vallys Haus (Interior):**
```
tiled/interior/
├── Furniture and Decorations/ ← Möbel, Regale, Tische
├── Animated furniture/        ← Animierte Kamine, Kerzen
├── Window/                    ← Fenster mit/ohne Sonnenlicht
└── door/                      ← Türen (offen/geschlossen)
```
→ Erstelle EINE `interior.tsx` die alles zusammenfasst

**Für das Dorf:**
```
tiled/village/
├── TiledMap Editor/
│   ├── Premade_houses.tsx      ← Fertige Häuser! Perfekt zum Starten
│   ├── Free_Premade_houses.tsx ← Kostenlose Varianten
│   ├── walls1-5.tsx            ← Wände zum selbst bauen
│   ├── roofs1-2.tsx            ← Dächer
│   ├── house-decorations.tsx   ← Fensterläden, Blumenkästen
│   ├── props.tsx               ← Brunnen, Bänke, Laternen
│   └── tilesets-village.tsx    ← Dorf-Terrain
├── NPCs/                       ← Fertige NPC-Sprites!
│   ├── fisherman/              ← Fischer mit Varianten
│   ├── villager/               ← Dorfbewohner
│   ├── vendor/                 ← Händler
│   ├── blacksmith/             ← Schmied
│   └── wizard/                 ← Zauberer
└── Buildings/                   ← Fertige Gebäude als große PNGs
```
→ `Premade_houses.tsx` ist GOLD — fertige Häuser die du direkt platzieren kannst!

**Für den Strand/Weg:**
```
tiled/grass/TiledMap Editor/Tilesets/
├── Tileset-Terrain-new grass.tsx       ← Haupt-Terrain (Gras, Erde)
├── beach - with thick foam.tsx         ← Strand mit Schaum-Animation
├── Beach-transition tiles...tsx        ← Übergang Strand↔Plattform
├── platform - grass - coast.tsx        ← Küsten-Plattform
└── fence-straight.tsx / fence-curved   ← Zäune für Wege
```

**Für die Hafenstadt:**
→ Deine bestehenden Pirat-Tilesets — die funktionieren schon!

### Mein Tipp zum Anfangen:

1. **Öffne `tiled/village/TiledMap Editor/sample map.tmx`** — das ist ein fertiges Dorf-Beispiel!
2. **Schau dir die Layer-Struktur an** — wie haben die das gemacht?
3. **Kopiere die Struktur** für deine eigene `village.tmx`
4. **Premade_houses verwenden** — nicht selbst Pixel für Pixel Häuser bauen
5. **Collision-Layer als letztes** — erst die Map hübsch machen, dann blocken

### Was du NICHT brauchst:
- Die `-Godot` Varianten (die sind für eine andere Engine)
- Die Rule-TMX Dateien (für Terrain-Auto-Tiling, fortgeschritten)
- Die Transparency-Varianten (nur wenn du Layering-Probleme hast)
