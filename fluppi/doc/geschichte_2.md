# Vallys Reise — Kapitel 2: Der Weg zum Meer

## Überblick

Nach dem Dorf zieht Vallys Gruppe weiter Richtung Küste. Auf dem **Küstenweg** trifft Vally die Prinzessin **Hasi**, die vor dem Hofstaat geflohen ist. Gemeinsam erreichen sie den **Hafen von Muschelbucht**, wo der finstere Fischhändler **Tentakel-Toni** den kleinen Tintenfisch **Kraki** in einem Käfig gefangen hält. Die Gruppe muss Kraki befreien, zwei Schatzkarten-Fragmente finden und Vally muss den Bootsführerschein bestehen, um mit einem Boot zur nächsten Insel aufbrechen zu können.

---

## Map 1: Küstenweg (beach_path.tmx)

### Stimmung & Layout
- **Größe**: 40×30 Tiles (1280×960 px)
- **Stil**: Übergang von Grasland zu Sandstrand, Klippen rechts, Meer links sichtbar
- **Bereiche**:
  - Oben links: Waldrand-Eingang (Portal vom Dorf)
  - Mitte: Küstenweg mit Gras → Sand-Übergang, einzelne Palmen
  - Links: Klippen mit Meerblick (dekorative Tiles, nicht begehbar)
  - Rechts oben: Kleine Höhle (2 Räume tief? oder als Bereich auf der Map)
  - Unten: Strandabschnitt mit Portal zum Hafen
  - Mitte-rechts: Händler-Zelt/Karren

### Map Properties (TMX)
```
map_id = beach_path
music = beach
battle_bg = sprites/battle_beach.png
```

### Tilesets
- Bestehende Gras/Erde-Tiles für den oberen Bereich
- Sand-Tiles für den Strand
- Klippen/Wasser-Tiles für den Meerblick
- Palmen, Büsche als Dekoration

### Layer-Aufbau
```
ground_base      — Sand/Gras Grundfläche
ground_path      — Weg-Tiles (Trampelpfad)
collision        — Klippen, Wasser, Felsen, dichtes Gebüsch
decor_below      — Blumen, kleine Steine, Muscheln
entities         — NPCs, Monster, Chests, Portale (Object Layer)
decor_above      — Palmenkronen, Klippenüberhänge
```

---

### NPCs

#### 1. Hasi (Rekrutierung)
- **Position**: Mitte der Map, auf einem Felsen sitzend am Klippenrand
- **Sprite**: hasi.png (bereits vorhanden)
- **Typ**: `npc` mit `can_recruit=true`, `recruit_id=hasi`
- **Properties**:
  ```
  name = Hasi
  type = npc
  sprite = hasi.png
  can_recruit = true
  recruit_id = hasi
  dialog = Hallo! Ich bin Hasi. Ich bin vor dem langweiligen Hofstaat weggelaufen. Hier draußen ist es viel aufregender!
  dialog_joined = Endlich echte Freundinnen! Auf geht's zum Meer!
  ```
- **Quest-Voraussetzung**: Hasi wird erst rekrutierbar, nachdem der Quest `hasi_rescue` abgeschlossen ist (sie muss erst aus einer brenzligen Situation befreit werden).

#### 2. Wanderhändler "Korwin"
- **Position**: Mitte-rechts, neben einem Karren
- **Sprite**: npc.png (oder eigener Händler-Sprite)
- **Typ**: `npc` mit `is_shop=true`
- **Properties**:
  ```
  name = Korwin
  type = npc
  sprite = npc.png
  is_shop = true
  shop_items = iron_sword,steel_sword,chain_armor,mega_potion,health_potion,torch
  dialog = Willkommen bei Korwin's Küstenhandel! Die besten Waren zwischen Dorf und Hafen!
  ```

#### 3. Alter Fischer "Oskar"
- **Position**: Unten links am Strand, neben einem kleinen Boot
- **Sprite**: npc.png
- **Typ**: `npc`
- **Properties**:
  ```
  name = Oskar
  type = npc
  dialog = Früher war ich der beste Seemann der Küste. Jetzt sind die Gewässer voller Schleimmonster... Wenn du zum Hafen willst, nimm den Landweg. Aber pass auf die Strandkrabben auf!
  ```
- **Funktion**: Gibt Hinweis auf Hafen und neue Monster. Gibt Quest `map_fragment_1`.

#### 4. Verletzte Hasi (Quest-Trigger)
- **Hinweis**: Hasi steht anfangs nicht normal herum — sie wird von 2 Strandkrabben umzingelt. Der Spieler muss die Krabben besiegen, bevor Hasi ansprechbar wird.
- **Umsetzung**: Hasi-NPC hat Property `prereq_defeat=crab_beach_01,crab_beach_02`. Solange diese Monster existieren, zeigt Hasi den Dialog: *"Hilfe! Diese Krabben lassen mich nicht in Ruhe!"*. Nach dem Besiegen ändert sich der Dialog und die Rekrutierung wird möglich.

---

### Monster

#### Strandkrabbe (crab)
- **Neu in enemies.json hinzufügen**
- **Sprite**: Muss noch erstellt werden (crab.png, 64×64, 3 Frames)
- **Stats**:
  ```json
  {
      "id": "crab",
      "name": "Strandkrabbe",
      "sprite": "crab.png",
      "frame_w": 64, "frame_h": 64,
      "anim_frames": [0, 1, 2],
      "anim_fps": 3,
      "hp": 45, "atk": 15, "def": 8,
      "xp": 15, "gold": 12,
      "drop_item": "crab_shell",
      "drop_chance": 0.4
  }
  ```

#### Sandblobb (sand_blobb)
- **Stärkere Blobb-Variante**
- **Stats**:
  ```json
  {
      "id": "sand_blobb",
      "name": "Sandblobb",
      "sprite": "sand_blobb.png",
      "frame_w": 64, "frame_h": 64,
      "anim_frames": [0, 1, 2],
      "anim_fps": 4,
      "hp": 50, "atk": 14, "def": 5,
      "xp": 18, "gold": 10,
      "drop_item": "slime_gel",
      "drop_chance": 0.6
  }
  ```

### Monster-Platzierung (entities Layer)
```
3× crab       — Strand-Bereich (2 davon bei Hasi)
3× sand_blobb — Küstenweg-Bereich
1× crab       — Höhleneingang bewachend
```

---

### Truhen

#### Truhe 1: Höhle
- **Position**: In der kleinen Höhle rechts oben
- **Inhalt**: `treasure_map_1` (Schatzkarten-Fragment 1)
- **Properties**:
  ```
  name = chest_cave_01
  type = chest
  item = treasure_map_1
  ```

#### Truhe 2: Versteckter Strandbereich
- **Position**: Hinter Felsen am unteren Strandrand (etwas versteckt)
- **Inhalt**: `mega_potion`
- **Properties**:
  ```
  name = chest_beach_01
  type = chest
  item = mega_potion
  ```

---

### Portale

#### Portal zum Dorf (Rückweg)
```
name = portal_to_village
type = portal
target_map = maps/village.tmx
target_spawn = south_exit
x, y = oben links
```

#### Portal zum Hafen
```
name = portal_to_harbor
type = portal
target_map = maps/harbor_city.tmx
target_spawn = west_entrance
x, y = unten rechts
```

---

### Quests (neue Einträge in quests.json)

#### Quest: hasi_rescue
```json
{
    "id": "hasi_rescue",
    "title": "Prinzessin in Not",
    "desc": "Befreie Hasi von den Strandkrabben auf dem Küstenweg.",
    "giver": "Hasi",
    "turn_in": "Hasi",
    "goal": "defeat:crab",
    "goal_count": 2,
    "room": "",
    "prereq_quest": "",
    "prereq_item": "",
    "reward_gold": 30,
    "reward_item": "",
    "dialog_offer": "Hilfe! Diese Krabben haben mich eingekreist! Bitte verjage sie, {PLAYER}!",
    "dialog_active": "Sind die Krabben schon weg? Ich trau mich nicht hinzuschauen!",
    "dialog_complete": "Danke, {PLAYER}! Du bist mutig. Darf ich mitkommen? Alleine ist es hier gruselig..."
}
```
**Hinweis**: Nach Abschluss wird Hasi rekrutierbar. Der `on_complete`-Hook oder die Rekrutierungs-Logik kann das automatisch freischalten.

#### Quest: map_fragment_1
```json
{
    "id": "map_fragment_1",
    "title": "Oskars Geheimnis",
    "desc": "Der alte Fischer Oskar hat eine Schatzkarte erwähnt. Finde das Fragment in der Küstenhöhle.",
    "giver": "Oskar",
    "turn_in": "Oskar",
    "goal": "find:treasure_map_1",
    "goal_count": 1,
    "room": "",
    "prereq_quest": "",
    "prereq_item": "",
    "reward_gold": 20,
    "reward_item": "",
    "dialog_offer": "Ich hab mal eine halbe Schatzkarte in der Höhle oben versteckt... Wenn du sie findest, erzähl ich dir mehr, {PLAYER}.",
    "dialog_active": "Die Höhle ist oben am Klippenrand. Sei vorsichtig, da hausen Krabben!",
    "dialog_complete": "Du hast es gefunden! Die andere Hälfte... die hat ein gewisser Toni im Hafen. Pass auf den auf, {PLAYER}."
}
```

#### Quest: korwin_arms_deal
```json
{
    "id": "korwin_arms_deal",
    "title": "Korwins Bitte",
    "desc": "Bringe Korwin 3 Krabbenschalen für einen Rabatt auf seine Waren.",
    "giver": "Korwin",
    "turn_in": "Korwin",
    "goal": "collect:crab_shell",
    "goal_count": 3,
    "room": "",
    "prereq_quest": "",
    "prereq_item": "",
    "reward_gold": 50,
    "reward_item": "lucky_charm",
    "dialog_offer": "Hey {PLAYER}, ich brauch Krabbenschalen für meine Panzerung. Bring mir 3 Stück und ich geb dir was Feines!",
    "dialog_active": "Schon Krabbenschalen gesammelt? Die Strandkrabben lassen die manchmal fallen.",
    "dialog_complete": "Perfekt! Hier, nimm diesen Glücksbringer. Der hat mir auf See immer Glück gebracht, {PLAYER}!"
}
```

---

### Neue Items (items.json ergänzen)

```json
"crab_shell": {
    "name": "Krabbenschale",
    "desc": "Harter Panzer einer Strandkrabbe",
    "type": "material",
    "price": 8,
    "effects": {}
},
"treasure_map_1": {
    "name": "Schatzkarte (links)",
    "desc": "Die linke Hälfte einer alten Schatzkarte",
    "type": "key",
    "price": 0,
    "effects": {}
},
"treasure_map_2": {
    "name": "Schatzkarte (rechts)",
    "desc": "Die rechte Hälfte einer alten Schatzkarte",
    "type": "key",
    "price": 0,
    "effects": {}
},
"boat_license": {
    "name": "Bootsführerschein",
    "desc": "Offizielle Erlaubnis zum Führen eines Bootes",
    "type": "key",
    "price": 0,
    "effects": {}
},
"anchor_pendant": {
    "name": "Anker-Amulett",
    "desc": "Schützt vor Seekrankheit. +5 DEF, +3 SPD",
    "type": "accessory",
    "price": 80,
    "effects": {"def": 5, "spd": 3}
},
"coral_blade": {
    "name": "Korallenschwert",
    "desc": "Aus versteinerten Korallen geschmiedet",
    "type": "weapon",
    "price": 120,
    "effects": {"atk": 20}
},
"pearl_staff": {
    "name": "Perlenstab",
    "desc": "Verstärkt magische Fähigkeiten",
    "type": "weapon",
    "price": 100,
    "effects": {"atk": 8, "mp": 20}
},
"sea_armor": {
    "name": "Meeresrüstung",
    "desc": "Aus Fischschuppen gefertigt, erstaunlich robust",
    "type": "armor",
    "price": 90,
    "effects": {"def": 14}
}
```

---

## Map 2: Hafen von Muschelbucht (harbor_city.tmx)

### Stimmung & Layout
- **Größe**: 50×40 Tiles (1600×1280 px)
- **Stil**: Kleiner Hafenort mit Holzstegen, Lagerhäusern, Marktplatz, Kaimauer
- **Bereiche**:
  - Links: Eingang vom Küstenweg (Portal)
  - Mitte-oben: Marktplatz mit Brunnen
  - Rechts-oben: Fischhändler "Tentakel-Toni" — Krakis Käfig
  - Mitte: Wohnhäuser, Taverne "Zur salzigen Möwe"
  - Unten-links: Hafenmeisterei (Bootsführerschein)
  - Unten-rechts: Anleger mit Booten (Portal zur nächsten Insel — gesperrt bis Führerschein)
  - Mitte-rechts: Lagerhäuser (eines davon betretbar für Quest)

### Map Properties (TMX)
```
map_id = harbor_city
music = harbor
battle_bg = sprites/battle_harbor.png
```

### Layer-Aufbau
```
ground_base      — Holzplanken, Stein, Wasser
ground_path      — Pflasterwege
collision        — Wasser, Hauswände, Kaimauer
decor_below      — Kisten, Fässer, Netze, Anker
entities         — NPCs, Monster, Portale
decor_above      — Hausdächer, Laternen, Segel
```

---

### NPCs

#### 1. Tentakel-Toni (Antagonist)
- **Position**: Rechts oben, vor einem Käfig mit Kraki
- **Sprite**: npc.png (oder eigener Sprite)
- **Typ**: `npc`
- **Properties**:
  ```
  name = Tentakel-Toni
  type = npc
  dialog = Was willst du? Der Tintenfisch gehört MIR! Ich hab ihn fair gefangen. Na gut... Wenn du mir 200 Gold und meinen gestohlenen Vertrag wiederbringst, VIELLEICHT lass ich ihn dann gehen.
  ```
- **Funktion**: Gibt den Quest `free_kraki`. Will 200 Gold + den gestohlenen Vertrag.

#### 2. Kraki (im Käfig)
- **Position**: Direkt neben Toni, in einem Käfig-Tile-Bereich
- **Sprite**: Noch nicht vorhanden — kraki.png muss erstellt werden (blauer Tintenfisch)
- **Typ**: `npc` (nicht rekrutierbar bis Quest abgeschlossen)
- **Properties**:
  ```
  name = Kraki
  type = npc
  dialog = *blubb blubb* Hilfe! Der böse Toni will mich an ein Restaurant verkaufen!
  ```
- **Nach Befreiung**: wird rekrutierbar (`can_recruit=true, recruit_id=kraki`)
  ```
  dialog_joined = *blubb!* Danke! Ich bin Kraki. Meine Tentakel sind zwar etwas tollpatschig, aber ich will euch helfen!
  ```

#### 3. Hafenmeisterin "Kapitänin Britta"
- **Position**: Unten links, Hafenmeisterei
- **Sprite**: npc.png
- **Typ**: `npc`
- **Properties**:
  ```
  name = Kapitänin Britta
  type = npc
  dialog = Ohne Bootsführerschein fährt hier niemand raus! Ich kann dir die Prüfung abnehmen — aber du musst erst den Theorie-Teil bestehen und eine praktische Aufgabe erledigen.
  ```
- **Funktion**: Quest `boat_license_theory` (Wissensfragen) und `boat_license_practical` (Monster im Hafenbecken besiegen).

#### 4. Tavernenwirtin "Gerda"
- **Position**: Mitte, in/vor der Taverne
- **Sprite**: npc.png
- **Typ**: `npc` mit `is_shop=true`
- **Properties**:
  ```
  name = Gerda
  type = npc
  is_shop = true
  shop_items = health_potion,mega_potion,anchor_pendant
  dialog = Willkommen in der Salzigen Möwe! Hier gibts Heiltränke und Seefahrer-Ausrüstung.
  ```

#### 5. Schmied "Hammerhai-Heinz"
- **Position**: Marktplatz
- **Sprite**: npc.png
- **Typ**: `npc` mit `is_shop=true`
- **Properties**:
  ```
  name = Hammerhai-Heinz
  type = npc
  is_shop = true
  shop_items = coral_blade,pearl_staff,sea_armor,steel_sword,iron_shield
  dialog = Meine Waffen sind aus dem Meer geschmiedet! Stärker als alles, was du bisher gesehen hast.
  ```

#### 6. Verdächtiger Matrose "Schleich-Sven"
- **Position**: Hinter einem Lagerhaus, halb versteckt
- **Sprite**: npc.png
- **Typ**: `npc`
- **Properties**:
  ```
  name = Schleich-Sven
  type = npc
  dialog = Psst! Ich hab gehört, du suchst Tonis Vertrag? Der liegt in seinem Lagerhaus hinterm Markt. Aber die Tür ist mit einem Schloss gesichert. Ich könnte dir einen Schlüssel... verkaufen. 50 Gold.
  ```
- **Funktion**: Verkauft den `warehouse_key` für 50 Gold (einmalige Interaktion, kein Shop).

---

### Monster

#### Hafenratte (harbor_rat)
```json
{
    "id": "harbor_rat",
    "name": "Hafenratte",
    "sprite": "harbor_rat.png",
    "frame_w": 64, "frame_h": 64,
    "anim_frames": [0, 1, 2],
    "anim_fps": 5,
    "hp": 35, "atk": 12, "def": 4,
    "xp": 12, "gold": 8,
    "drop_item": "",
    "drop_chance": 0
}
```

#### Giftqualle (jellyfish)
```json
{
    "id": "jellyfish",
    "name": "Giftqualle",
    "sprite": "jellyfish.png",
    "frame_w": 64, "frame_h": 64,
    "anim_frames": [0, 1, 2],
    "anim_fps": 3,
    "hp": 60, "atk": 20, "def": 3,
    "xp": 22, "gold": 15,
    "drop_item": "",
    "drop_chance": 0
}
```

#### Riesenkrabbe (giant_crab) — Mini-Boss im Hafenbecken
```json
{
    "id": "giant_crab",
    "name": "Riesenkrabbe",
    "sprite": "giant_crab.png",
    "frame_w": 64, "frame_h": 64,
    "anim_frames": [0, 1, 2],
    "anim_fps": 2,
    "hp": 120, "atk": 25, "def": 15,
    "xp": 50, "gold": 40,
    "drop_item": "crab_shell",
    "drop_chance": 1.0
}
```

### Monster-Platzierung
```
3× harbor_rat  — Lagerhausbereich
2× jellyfish   — Am Wasser/Steg
1× giant_crab  — Hafenbecken (Bootsführerschein-Prüfung)
```

---

### Truhen

#### Truhe im Lagerhaus (Tonis Vertrag)
- **Position**: Im betretbaren Lagerhaus (erfordert `warehouse_key`)
- **Properties**:
  ```
  name = chest_warehouse
  type = chest
  item = toni_contract
  prereq_item = warehouse_key
  ```

#### Truhe am Steg (Schatzkarte Fragment 2)
- **Position**: Am Ende eines langen Stegs, hinter Fässern versteckt
- **Properties**:
  ```
  name = chest_dock_secret
  type = chest
  item = treasure_map_2
  ```

---

### Portale

#### Eingang vom Küstenweg
```
name = west_entrance
type = player_spawn    (+ portal zurück)
target_map = maps/beach_path.tmx
target_spawn = south_exit
```

#### Bootsanleger (zur nächsten Insel)
```
name = portal_boat
type = portal
target_map = maps/island_01.tmx
target_spawn = dock
prereq_item = boat_license
```
**Wichtig**: Dieses Portal ist blockiert bis der Spieler den `boat_license` besitzt. In der Engine muss eine Prüfung eingebaut werden:
```
IF MAP.EXISTS(props, "prereq_item") THEN
    IF NOT RPG_INVENTORY.HAS_ITEM(props{"prereq_item"}) THEN
        RPG_DIALOG.SAY "System", "Du brauchst: " + props{"prereq_item"}
        ' Block portal
    ENDIF
ENDIF
```

#### Lagerhaus-Eingang
```
name = portal_warehouse
type = portal
target_map = maps/toni_warehouse.tmx
target_spawn = entrance
prereq_item = warehouse_key
```
(Optional: eigene Mini-Map für das Lagerhaus, 8×6 Tiles)

---

### Quests

#### Quest: free_kraki
```json
{
    "id": "free_kraki",
    "title": "Befreit Kraki!",
    "desc": "Tentakel-Toni will 200 Gold und seinen Vertrag zurück. Finde beides.",
    "giver": "Tentakel-Toni",
    "turn_in": "Tentakel-Toni",
    "goal": "collect:toni_contract",
    "goal_count": 1,
    "room": "",
    "prereq_quest": "",
    "prereq_item": "",
    "reward_gold": 0,
    "reward_item": "",
    "dialog_offer": "200 Gold UND meinen gestohlenen Vertrag. Dann, VIELLEICHT, lass ich den Tintenfisch frei. Deal, {PLAYER}?",
    "dialog_active": "Wo ist mein Vertrag?! Und vergiss die 200 Gold nicht!",
    "dialog_complete": "Hmmpf. Ein Deal ist ein Deal. Nimm den stinkenden Tintenfisch. RAUS hier!",
    "on_complete": "RPG_INVENTORY.SPEND_GOLD 200"
}
```
**Ablauf**: Der Spieler muss:
1. 200 Gold haben (aus Kämpfen/Verkäufen ansparen)
2. Den Vertrag aus Tonis Lagerhaus holen (braucht `warehouse_key` von Sven)
3. Beides zu Toni bringen → Kraki wird frei und rekrutierbar

**Prüfung bei Turn-In**: Die Engine muss prüfen ob der Spieler 200 Gold hat BEVOR der Quest abgeschlossen wird. Dies kann über `prereq_item = toni_contract` + eine Gold-Prüfung im `on_complete`-Hook gelöst werden. Alternativ: Der Quest-Turn-In-Dialog prüft das Gold:
```
dialog_complete mit Bedingung: IF gold < 200 → "Du hast nicht genug Gold! Komm wieder wenn du 200 hast."
```

#### Quest: boat_license_theory
```json
{
    "id": "boat_license_theory",
    "title": "Bootsführerschein: Theorie",
    "desc": "Bestehe die Theorie-Prüfung bei Kapitänin Britta.",
    "giver": "Kapitänin Britta",
    "turn_in": "Kapitänin Britta",
    "goal": "talk:Kapitänin Britta",
    "goal_count": 1,
    "room": "",
    "prereq_quest": "",
    "prereq_item": "",
    "reward_gold": 0,
    "reward_item": "",
    "dialog_offer": "Willst du den Bootsführerschein machen, {PLAYER}? Dann beantworte meine 3 Fragen!",
    "dialog_active": "Bereit für die Prüfung?",
    "dialog_complete": "Theorie bestanden! Jetzt noch die praktische Prüfung.",
    "on_start": ""
}
```
**Besonderheit**: Bei Annahme startet ein Quiz-Dialog:
1. "Was tust du bei Sturm?" → Richtig: "Segel einholen" (Choice: Segel einholen|Schneller fahren|Anker werfen)
2. "Welche Seite ist Backbord?" → Richtig: "Links" (Choice: Links|Rechts|Oben)
3. "Was bedeutet SOS?" → Richtig: "Hilferuf" (Choice: Hilferuf|Schnell oder sterben|Suche ohne Sinn)

Bei 2/3 richtig → bestanden. Bei Fehler → "Nicht bestanden, versuch's nochmal."

**Umsetzung**: `on_start`-Hook oder spezielle NPC-Interaktion mit CHOICE-Dialogen.

#### Quest: boat_license_practical
```json
{
    "id": "boat_license_practical",
    "title": "Bootsführerschein: Praxis",
    "desc": "Besiege die Riesenkrabbe im Hafenbecken.",
    "giver": "Kapitänin Britta",
    "turn_in": "Kapitänin Britta",
    "goal": "defeat:giant_crab",
    "goal_count": 1,
    "room": "",
    "prereq_quest": "boat_license_theory",
    "prereq_item": "",
    "reward_gold": 50,
    "reward_item": "boat_license",
    "dialog_offer": "Für die Praxis musst du beweisen, dass du dich auf See verteidigen kannst. Besieg die Riesenkrabbe im Hafenbecken, {PLAYER}!",
    "dialog_active": "Die Riesenkrabbe ist unten am Becken. Zeig was du kannst!",
    "dialog_complete": "Beeindruckend! Hier ist dein Bootsführerschein, {PLAYER}. Gute Reise!"
}
```

#### Quest: map_fragment_2
```json
{
    "id": "map_fragment_2",
    "title": "Die zweite Hälfte",
    "desc": "Finde das zweite Schatzkarten-Fragment im Hafen.",
    "giver": "Oskar",
    "turn_in": "Oskar",
    "goal": "find:treasure_map_2",
    "goal_count": 1,
    "room": "",
    "prereq_quest": "map_fragment_1",
    "prereq_item": "",
    "reward_gold": 30,
    "reward_item": "",
    "dialog_offer": "Die zweite Hälfte der Karte... Toni hat sie bestimmt irgendwo am Hafen versteckt. Vielleicht am alten Steg ganz hinten?",
    "dialog_active": "Such am Steg, hinter den Fässern!",
    "dialog_complete": "Beide Hälften! Die Karte zeigt eine Insel im Osten. Da liegt der Schatz von Kapitän Drachenzahn!"
}
```

---

### Neue Items für Lagerhaus-Quest

```json
"warehouse_key": {
    "name": "Lagerhausschlüssel",
    "desc": "Öffnet Tonis Lagerhaus",
    "type": "key",
    "price": 50,
    "effects": {}
},
"toni_contract": {
    "name": "Tonis Vertrag",
    "desc": "Ein offizieller Fischereivertrag mit Tonis Unterschrift",
    "type": "key",
    "price": 0,
    "effects": {}
}
```

---

## Gesamter Quest-Ablauf (Spieler-Perspektive)

### Küstenweg
1. **Ankunft** vom Dorf → Küstenweg
2. **Oskar** treffen → Quest "Oskars Geheimnis" (Fragment 1 suchen)
3. **Krabben besiegen** bei Hasi → Quest "Prinzessin in Not" wird angeboten/direkt aktiv
4. **Hasi befreit** → Rekrutierung → Party ist jetzt Vally + Möhrchen + Hasi
5. **Höhle** erkunden → Schatzkarten-Fragment 1 finden → Quest bei Oskar abgeben
6. **Korwin** besuchen → Waffen kaufen, optional Quest "Korwins Bitte" (Krabbenschalen)
7. **Weiter zum Hafen**

### Hafen
8. **Kraki im Käfig** sehen → Mit Toni reden → Quest "Befreit Kraki!" (200 Gold + Vertrag)
9. **Schleich-Sven** finden → Lagerhausschlüssel kaufen (50 Gold)
10. **Lagerhaus** betreten → Vertrag aus Truhe holen
11. **Gerda / Heinz** besuchen → Ausrüstung upgraden
12. **200 Gold ansparen** (Kämpfe, Verkauf von Krabbenschalen/Schleim-Gel)
13. **Toni** → Vertrag + 200 Gold abgeben → Kraki frei → Rekrutierung → volle Party!
14. **Oskar** → Quest "Die zweite Hälfte" → Fragment 2 am Steg finden
15. **Kapitänin Britta** → Theorie-Quiz bestehen → Praxis: Riesenkrabbe besiegen → Bootsführerschein
16. **Anleger** → Mit Bootsführerschein + voller Party → Überfahrt zur Insel!

---

## Musik (music.json ergänzen)

```json
"beach": {
    "file": "music/beach.ogg",
    "loop": true
},
"harbor": {
    "file": "music/harbor.ogg",
    "loop": true
}
```

---

## Zusammenfassung: Was muss erstellt werden

### Sprites (müssen gezeichnet/generiert werden)
- [ ] `crab.png` — Strandkrabbe (64×64, 3 Frames)
- [ ] `sand_blobb.png` — Sandblobb (64×64, 3 Frames)
- [ ] `harbor_rat.png` — Hafenratte (64×64, 3 Frames)
- [ ] `jellyfish.png` — Giftqualle (64×64, 3 Frames)
- [ ] `giant_crab.png` — Riesenkrabbe (64×64, 3 Frames)
- [ ] `kraki.png` — Kraki Charakter (64×64, 3×4 Spritesheet wie andere Chars)
- [ ] `battle_beach.png` — Battle-Hintergrund Strand
- [ ] `battle_harbor.png` — Battle-Hintergrund Hafen

### Maps (in Tiled erstellen)
- [ ] `beach_path.tmx` — Küstenweg (40×30)
- [ ] `harbor_city.tmx` — Hafen von Muschelbucht (50×40)
- [ ] `toni_warehouse.tmx` — Tonis Lagerhaus (8×6, optional)

### Tilesets
- [ ] Sand/Strand-Tiles
- [ ] Hafen/Holzsteg-Tiles
- [ ] Wasser/Klippen-Tiles

### JSON-Dateien aktualisieren
- [ ] `enemies.json` — 5 neue Monster
- [ ] `items.json` — 8 neue Items
- [ ] `quests.json` — 7 neue Quests
- [ ] `music.json` — 2 neue Themes
- [ ] `characters.json` — Kraki ist bereits vorhanden

### Code-Änderungen
- [ ] Portal-Blocking: `prereq_item` Property auf Portalen auswerten
- [ ] Quiz-Dialog: Spezialbehandlung für boat_license_theory
- [ ] Schlüsselkauf bei Sven: Einmalige NPC-Interaktion mit Gold-Abzug
- [ ] Gold-Prüfung beim Kraki-Quest Turn-In
- [ ] `find:item_id` als neuer Quest-Goal-Type (Truhe öffnen zählt als Progress)

### Village Map erweitern
- [ ] Neues Portal in `village.tmx`: Ausgang nach Süden/Osten zum Küstenweg
