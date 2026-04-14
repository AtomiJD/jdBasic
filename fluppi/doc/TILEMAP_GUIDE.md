# jdRPG Tiled Map Leitfaden v1.0

## Übersicht

Jede Map im Spiel ist eine eigenständige TMX-Datei, die ALLE Informationen enthält:
Spieler-Startposition, NPCs, Monster, Portale, Truhen, Kollision.
Die Engine lädt jede konforme Map generisch — kein Code muss geändert werden.

---

## 1. Map-Eigenschaften (Map Properties)

In Tiled: Map → Map Properties (linke Seite)

| Property       | Typ    | Pflicht | Beschreibung                                        |
|----------------|--------|---------|-----------------------------------------------------|
| `map_id`       | string | Ja      | Eindeutige ID: `"vally_home"`, `"shop"`, `"cave_1"` |
| `music`        | string | Ja      | Musik-Track: `"overworld"`, `"battle"`, `"dungeon"`  |
| `display_name` | string | Nein    | Anzeigename im HUD: `"Vallys Insel"`                |
| `camera_fixed` | bool   | Nein    | `true` = Kamera scrollt nicht (kleine Innenräume)    |

---

## 2. Layer-Benennung

Die Engine erkennt Layer automatisch am **Namens-Präfix**:

### 2.1 Boden-Layer → `ground_*` (begehbar)

Ein Pixel ist begehbar, wenn MINDESTENS EIN `ground_`-Layer dort ein Tile hat.

| Layer-Name      | Zweck                        |
|-----------------|------------------------------|
| `ground_base`   | **Pflicht.** Hauptboden (Gras, Erde, Stein) |
| `ground_beach`  | Sand, Strand                 |
| `ground_dock`   | Stege, Planken               |
| `ground_path`   | Wege, Brücken                |
| `ground_*`      | Weitere begehbare Flächen    |

### 2.2 Kollisions-Layer → `collision`

| Layer-Name  | Zweck                                          |
|-------------|-------------------------------------------------|
| `collision`  | **Unsichtbar.** Tile > 0 = blockiert.          |

- Benutze ein eigenes kleines Tileset (1 rotes 32x32 Tile)
- In Tiled: Layer auf **unsichtbar** setzen (Auge-Symbol aus)
- Beim Bearbeiten: sichtbar schalten zum Malen, dann wieder aus

**Kollisionsregel:** Begehbar = `ground_*` hat Tile UND `collision` ist leer

### 2.3 Deko-Layer → `decor_*` (nur visuell)

| Layer-Name     | Zweck                                    |
|----------------|------------------------------------------|
| `decor_below`  | Boden-Deko unter dem Spieler (Blumen)    |
| `decor_above`  | Deko über dem Spieler (Baumkronen, Dächer) |

### 2.4 Umgebungs-Layer (alles andere)

Layer ohne Präfix (`water`, `props`, etc.) werden gerendert aber nicht für Kollision geprüft.

### 2.5 Empfohlene Layer-Reihenfolge (von unten nach oben)

```
water              ← Ozean-Hintergrund
ground_base        ← Hauptboden
ground_beach       ← Strand
ground_dock        ← Stege
ground_path        ← Wege
collision          ← UNSICHTBAR, Blockierung
decor_below        ← Boden-Dekoration
entities           ← OBJEKT-LAYER: NPCs, Monster, Spawns
portals            ← OBJEKT-LAYER: Türen, Übergänge
decor_above        ← Deko über Spieler
decor_objects      ← OBJEKT-LAYER: visuelle Props (Fässer etc.)
```

---

## 3. Objekt-Typen (Object Types)

Objekte werden in **Object Layers** platziert. Der **Type**-Feld bestimmt was es ist.

### 3.1 `player_spawn` — Spieler-Startpunkt

Wo Vally erscheint wenn sie die Map betritt.

| Eigenschaft  | Typ    | Pflicht | Beschreibung                      |
|--------------|--------|---------|-----------------------------------|
| `direction`  | string | Nein    | Blickrichtung: `down/up/left/right` |

- **Geometrie:** Punkt (x, y)
- **Name:** `"default_spawn"` für den Standard-Einstiegspunkt
- Weitere Spawns für Portale: `"from_shop"`, `"from_cave"`, etc.
- **Pro Map:** Genau EIN `default_spawn`, beliebig viele benannte Spawns

### 3.2 `npc` — Nicht-Spieler-Charakter

| Eigenschaft     | Typ    | Pflicht | Beschreibung                              |
|-----------------|--------|---------|-------------------------------------------|
| `dialog`        | string | Ja      | Dialogtext. `{PLAYER}` wird ersetzt.      |
| `sprite`        | string | Ja      | Dateiname: `"hasi.png"`                   |
| `frame_w`       | int    | Nein    | Spritesheet Frame-Breite (Standard: 64)   |
| `frame_h`       | int    | Nein    | Spritesheet Frame-Höhe (Standard: 64)     |
| `can_recruit`   | bool   | Nein    | Kann rekrutiert werden?                    |
| `recruit_id`    | string | Wenn recruit | ID aus `characters.json`            |
| `dialog_joined` | string | Nein    | Dialog nach Rekrutierung                   |

- **Name:** Anzeigename im Dialog: `"Hasi"`, `"Möhrchen"`
- **Geometrie:** Punkt an der Spawn-Position

### 3.3 `monster` — Overworld-Monster

| Eigenschaft     | Typ    | Pflicht | Beschreibung                        |
|-----------------|--------|---------|-------------------------------------|
| `enemy_id`      | string | Ja      | ID aus `enemies.json`: `"blobb"`    |
| `respawn`       | bool   | Nein    | Respawnt nach Niederlage? (Std: true) |

- **Name:** Eindeutige ID für Speicherstand: `"blobb_dock_01"`
- **Geometrie:** Punkt an der Spawn-Position

### 3.4 `portal` — Map-Übergang

| Eigenschaft       | Typ    | Pflicht | Beschreibung                           |
|-------------------|--------|---------|----------------------------------------|
| `target_map`      | string | Ja      | Ziel-TMX Datei: `"house.tmx"`         |
| `target_spawn`    | string | Nein    | Name des Ziel-Spawns (Std: `"default_spawn"`) |
| `target_x`        | float  | Nein    | Explizite Ziel-X (überschreibt Spawn)  |
| `target_y`        | float  | Nein    | Explizite Ziel-Y                       |
| `target_direction` | string | Nein   | Blickrichtung am Ziel                  |
| `transition`      | string | Nein    | Effekt: `"fade"` (Std), `"none"`       |
| `requires_key`    | string | Nein    | Benötigter Schlüssel-Item              |

- **Name:** Portal-ID: `"door_to_shop"`, `"cave_entrance"`
- **Geometrie:** **Rechteck** — Spieler löst Portal aus wenn er darüber läuft

**Bidirektionale Türen:**
```
Map A: Portal "door_to_B"  → target_map="B.tmx", target_spawn="from_A"
Map B: Spawn "from_A"      → Punkt wo Spieler erscheint
Map B: Portal "door_to_A"  → target_map="A.tmx", target_spawn="from_B"
Map A: Spawn "from_B"      → Punkt vor der Tür
```

### 3.5 `chest` — Truhe

| Eigenschaft      | Typ    | Pflicht | Beschreibung                       |
|------------------|--------|---------|------------------------------------|
| `content_type`   | string | Ja      | `"gold"`, `"item"`, `"key"`        |
| `content_id`     | string | Wenn item | Item-ID aus `items.json`        |
| `content_amount` | int    | Nein    | Menge (Std: 1)                     |

- **Name:** Eindeutige ID: `"chest_beach_01"`
- **Geometrie:** Punkt oder Tile-Objekt

### 3.6 `sign` — Schild

| Eigenschaft | Typ    | Pflicht | Beschreibung                     |
|-------------|--------|---------|----------------------------------|
| `text`      | string | Ja      | Angezeigter Text. `{PLAYER}` OK  |

### 3.7 `item_pickup` — Sammelgegenstand

| Eigenschaft | Typ    | Pflicht | Beschreibung                     |
|-------------|--------|---------|----------------------------------|
| `item_id`   | string | Ja      | Item-ID oder `"gold"`            |
| `amount`    | int    | Nein    | Menge (Std: 1)                   |

- **Name:** Eindeutige ID: `"coin_dock_03"`

---

## 4. Kollisions-Setup Schritt für Schritt

1. **Erstelle Collision-Tileset:** Neue TSX mit einem 32x32 roten Tile
2. **Füge `collision` Layer hinzu:** Über den ground-Layern
3. **Male Blockierungen:** Überall wo nicht gelaufen werden darf
   - Fässer, Felsen, Hauswände, Baumstämme
   - Wasser-Ränder (falls ground_base dort aufhört, automatisch blockiert)
4. **Layer unsichtbar machen:** Auge-Symbol aus
5. **Tipp:** Zum Bearbeiten: Collision-Layer sichtbar + halbtransparent

---

## 5. Neue Map erstellen — Checkliste

```
□ Map-Properties setzen (map_id, music, display_name)
□ ground_base Layer anlegen und Boden malen
□ Weitere ground_* Layer bei Bedarf
□ collision Layer anlegen (unsichtbar)
□ Kollision malen (Wände, Hindernisse)
□ entities Object Layer anlegen
   □ player_spawn "default_spawn" platzieren
   □ NPCs als type="npc" mit allen Properties
   □ Monster als type="monster" mit enemy_id
   □ Truhen als type="chest" mit content_type
□ portals Object Layer anlegen (falls Übergänge)
   □ Portal-Rechtecke mit target_map
   □ Passende player_spawn im Ziel
□ decor_below / decor_above für Dekoration
□ Alle veränderbaren Objekte haben eindeutigen Name
□ Speichern als .tmx
```

---

## 6. Beispiel-Map Struktur

```
meine_map.tmx
├── Map Properties: map_id="meine_map", music="village"
├── Tile Layers:
│   ├── water                  (Ozean-Hintergrund)
│   ├── ground_base            (Gras, Erde)
│   ├── ground_path            (Steinweg)
│   ├── collision              (unsichtbar, Blockierung)
│   ├── decor_below            (Blumen, Pfützen)
│   └── decor_above            (Baumkronen)
├── Object Layers:
│   ├── entities
│   │   ├── player_spawn "default_spawn" (160, 320)
│   │   ├── player_spawn "from_cave" (480, 256)
│   │   ├── npc "Hasi" (288, 224) → dialog, sprite, can_recruit
│   │   ├── monster "blobb_01" (600, 300) → enemy_id="blobb"
│   │   └── chest "chest_01" (384, 192) → content_type="gold", amount=20
│   └── portals
│       ├── portal "cave_entrance" (512, 128, 64x32) → target_map="cave.tmx"
│       └── portal "house_door" (320, 384, 32x16) → target_map="house.tmx"
└── Tilesets:
    ├── terrain.tsx
    ├── props.tsx
    └── collision_tile.tsx
```

---

## 7. Speicherstand-Struktur

Jede Map speichert ihren eigenen Zustand:

```json
{
    "current_map": "vally_home",
    "player": { "name": "Vally", "x": 1440, "y": 448, "direction": "down" },
    "map_states": {
        "vally_home": {
            "chests_opened": ["chest_dock_01"],
            "items_collected": ["coin_beach_03"],
            "monsters_defeated": ["blobb_dock_01"]
        }
    },
    "global": {
        "recruit_data": { "hasi": true, "moehrchen": true },
        "inventory": { ... },
        "party": { ... },
        "combat": { ... }
    }
}
```

---

## 8. Wichtige Regeln

1. **Jedes veränderbare Objekt braucht einen eindeutigen Namen** (für den Speicherstand)
2. **`ground_*` Prefix = begehbar** — die Engine erkennt das automatisch
3. **`collision` Layer = blockiert** — unsichtbar, malt Hindernisse
4. **Portale sind Rechtecke**, nicht Punkte — der Spieler muss drüber laufen
5. **Player Spawns sind Punkte** — genau wo Vally steht
6. **Type-Feld ist Pflicht** — ohne Type wird das Objekt ignoriert
7. **Keine externen JSON-Dateien für Map-Daten** — alles in der TMX
