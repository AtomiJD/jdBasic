# jdRPG Tiled Map Guide v1.0

## Overview

Every map in the game is a self-contained TMX file that holds ALL of its
information: the player's start position, NPCs, monsters, portals, chests and
collision. The engine loads any conforming map generically, with no code
change.

---

## 1. Map properties

In Tiled: Map -> Map Properties (on the left)

| Property       | Type   | Required | Description                                         |
|----------------|--------|----------|-----------------------------------------------------|
| `map_id`       | string | yes      | Unique id: `"vally_home"`, `"shop"`, `"cave_1"`     |
| `music`        | string | yes      | Music track: `"overworld"`, `"battle"`, `"dungeon"` |
| `display_name` | string | no       | Name shown in the HUD: `"Vallys Insel"`             |
| `camera_fixed` | bool   | no       | `true` keeps the camera still (small interiors)     |

---

## 2. Layer naming

The engine recognises a layer by its **name prefix**.

### 2.1 Ground layers -> `ground_*` (walkable)

A pixel is walkable when AT LEAST ONE `ground_` layer has a tile there.

| Layer name      | Purpose                                     |
|-----------------|---------------------------------------------|
| `ground_base`   | **Required.** The main floor (grass, earth, stone) |
| `ground_beach`  | Sand, beach                                 |
| `ground_dock`   | Jetties, planks                             |
| `ground_path`   | Paths, bridges                              |
| `ground_*`      | Any further walkable surface                |

### 2.2 Collision layer -> `collision`

| Layer name  | Purpose                                 |
|-------------|-----------------------------------------|
| `collision` | **Invisible.** A tile above 0 blocks.   |

- Use a small tileset of its own: one red 32x32 tile
- In Tiled, set the layer **invisible** (the eye icon off)
- While editing, switch it visible to paint, then off again

**The collision rule:** walkable means a `ground_*` layer has a tile AND
`collision` is empty.

### 2.3 Decoration layers -> `decor_*` (visual only)

| Layer name     | Purpose                                      |
|----------------|----------------------------------------------|
| `decor_below`  | Floor decoration under the player (flowers)  |
| `decor_above`  | Decoration over the player (tree tops, roofs)|

### 2.4 Environment layers (everything else)

A layer without a prefix (`water`, `props` and so on) is rendered but never
tested for collision.

### 2.5 Recommended layer order (bottom to top)

```
water              <- ocean background
ground_base        <- main floor
ground_beach       <- beach
ground_dock        <- jetties
ground_path        <- paths
collision          <- INVISIBLE, blocking
decor_below        <- floor decoration
entities           <- OBJECT LAYER: NPCs, monsters, spawns
portals            <- OBJECT LAYER: doors, transitions
decor_above        <- decoration over the player
decor_objects      <- OBJECT LAYER: visual props (barrels and the like)
```

---

## 3. Object types

Objects are placed in **object layers**. The **Type** field decides what an
object is.

### 3.1 `player_spawn` - where the player starts

Where the player appears on entering the map.

| Property    | Type   | Required | Description                         |
|-------------|--------|----------|-------------------------------------|
| `direction` | string | no       | Facing: `down/up/left/right`        |

- **Geometry:** a point (x, y)
- **Name:** `"default_spawn"` for the standard entry point
- Further spawns for portals: `"from_shop"`, `"from_cave"` and so on
- **Per map:** exactly ONE `default_spawn`, any number of named spawns

### 3.2 `npc` - a non-player character

| Property        | Type   | Required      | Description                           |
|-----------------|--------|---------------|---------------------------------------|
| `dialog`        | string | yes           | Dialogue text. `{PLAYER}` is replaced.|
| `sprite`        | string | yes           | File name: `"hasi.png"`               |
| `frame_w`       | int    | no            | Spritesheet frame width (default 64)  |
| `frame_h`       | int    | no            | Spritesheet frame height (default 64) |
| `can_recruit`   | bool   | no            | Can this one be recruited?            |
| `recruit_id`    | string | when recruitable | The id from `characters.json`      |
| `dialog_joined` | string | no            | Dialogue after recruiting             |

- **Name:** the name shown in dialogue: `"Hasi"`, `"Moehrchen"`
- **Geometry:** a point at the spawn position

### 3.3 `monster` - an overworld monster

| Property   | Type   | Required | Description                              |
|------------|--------|----------|------------------------------------------|
| `enemy_id` | string | yes      | The id from `enemies.json`: `"blobb"`    |
| `respawn`  | bool   | no       | Respawn after defeat? (default true)     |

- **Name:** a unique id for the save file: `"blobb_dock_01"`
- **Geometry:** a point at the spawn position

### 3.4 `portal` - a map transition

| Property           | Type   | Required | Description                                   |
|--------------------|--------|----------|-----------------------------------------------|
| `target_map`       | string | yes      | The target TMX file: `"house.tmx"`            |
| `target_spawn`     | string | no       | Name of the target spawn (default `"default_spawn"`) |
| `target_x`         | float  | no       | An explicit target x, overriding the spawn    |
| `target_y`         | float  | no       | An explicit target y                          |
| `target_direction` | string | no       | Facing at the target                          |
| `transition`       | string | no       | Effect: `"fade"` (default), `"none"`          |
| `requires_key`     | string | no       | The key item the portal needs                 |

- **Name:** the portal id: `"door_to_shop"`, `"cave_entrance"`
- **Geometry:** a **rectangle** - the portal fires when the player walks over it

**A door that works both ways:**
```
Map A: portal "door_to_B"  -> target_map="B.tmx", target_spawn="from_A"
Map B: spawn  "from_A"      -> the point where the player appears
Map B: portal "door_to_A"  -> target_map="A.tmx", target_spawn="from_B"
Map A: spawn  "from_B"      -> the point in front of the door
```

### 3.5 `chest`

| Property         | Type   | Required   | Description                        |
|------------------|--------|------------|------------------------------------|
| `content_type`   | string | yes        | `"gold"`, `"item"`, `"key"`        |
| `content_id`     | string | when item  | The item id from `items.json`      |
| `content_amount` | int    | no         | How many (default 1)               |

- **Name:** a unique id: `"chest_beach_01"`
- **Geometry:** a point or a tile object

### 3.6 `sign`

| Property | Type   | Required | Description                          |
|----------|--------|----------|--------------------------------------|
| `text`   | string | yes      | The text shown. `{PLAYER}` is fine.  |

### 3.7 `item_pickup`

| Property  | Type   | Required | Description                      |
|-----------|--------|----------|----------------------------------|
| `item_id` | string | yes      | An item id, or `"gold"`          |
| `amount`  | int    | no       | How many (default 1)             |

- **Name:** a unique id: `"coin_dock_03"`

---

## 4. Setting up collision, step by step

1. **Make a collision tileset:** a new TSX holding one red 32x32 tile
2. **Add the `collision` layer:** above the ground layers
3. **Paint the blocking:** everywhere walking is not allowed
   - barrels, rocks, house walls, tree trunks
   - the edge of water, which blocks by itself where `ground_base` stops
4. **Make the layer invisible:** the eye icon off
5. **While editing:** the collision layer visible and half transparent

---

## 5. A new map, as a checklist

```
[ ] Map properties set (map_id, music, display_name)
[ ] a ground_base layer, with the floor painted
[ ] further ground_* layers where they are needed
[ ] a collision layer, invisible
[ ] collision painted (walls, obstacles)
[ ] an entities object layer
    [ ] player_spawn "default_spawn" placed
    [ ] NPCs as type="npc" with every property
    [ ] monsters as type="monster" with enemy_id
    [ ] chests as type="chest" with content_type
[ ] a portals object layer, where there are transitions
    [ ] portal rectangles with target_map
    [ ] a matching player_spawn at the target
[ ] decor_below / decor_above for decoration
[ ] every object that can change carries a unique name
[ ] saved as .tmx
```

---

## 6. What a map looks like

```
my_map.tmx
|-- Map properties: map_id="my_map", music="village"
|-- Tile layers:
|   |-- water                  (ocean background)
|   |-- ground_base            (grass, earth)
|   |-- ground_path            (stone path)
|   |-- collision              (invisible, blocking)
|   |-- decor_below            (flowers, puddles)
|   +-- decor_above            (tree tops)
|-- Object layers:
|   |-- entities
|   |   |-- player_spawn "default_spawn" (160, 320)
|   |   |-- player_spawn "from_cave" (480, 256)
|   |   |-- npc "Hasi" (288, 224) -> dialog, sprite, can_recruit
|   |   |-- monster "blobb_01" (600, 300) -> enemy_id="blobb"
|   |   +-- chest "chest_01" (384, 192) -> content_type="gold", amount=20
|   +-- portals
|       |-- portal "cave_entrance" (512, 128, 64x32) -> target_map="cave.tmx"
|       +-- portal "house_door" (320, 384, 32x16) -> target_map="house.tmx"
+-- Tilesets:
    |-- terrain.tsx
    |-- props.tsx
    +-- collision_tile.tsx
```

---

## 7. What a save file holds

Every map keeps its own state:

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
        "inventory": { },
        "party": { },
        "combat": { }
    }
}
```

---

## 8. The rules that matter

1. **Every object that can change needs a unique name**, for the save file
2. **A `ground_*` prefix means walkable** - the engine recognises it by itself
3. **The `collision` layer blocks** - invisible, and the obstacles are painted there
4. **Portals are rectangles**, not points: the player has to walk over one
5. **Player spawns are points**, exactly where the player stands
6. **The Type field is required** - an object without one is ignored
7. **No external JSON for map data** - everything lives in the TMX
