extends Node3D

# World root. Owns the JDBasicVM that hosts the NPC brain, spawns NPC
# bodies from npcs.json, and ticks them every physics frame.

const NPCS_JSON     := "res://assets/data/npcs.json"
const WORLD_LORE    := "res://assets/data/world_lore.json"
const CHESTS_JSON   := "res://assets/data/chests.json"
const DUNGEONS_JSON := "res://assets/data/dungeons.json"
const NPC_BRAIN     := "res://world/npc_brain.jdb"
const DIALOG_BRAIN  := "res://world/dialog_brain.jdb"
const NPC_SCENE     := preload("res://world/npc.tscn")
const DIALOG_UI     := preload("res://world/dialog_ui.tscn")
const JOURNAL_PANEL := preload("res://world/journal_panel.tscn")
const ScatterScript := preload("res://world/scatter.gd")
const ChestScript   := preload("res://world/chest.gd")
const DungeonScript := preload("res://world/dungeon.gd")

@export var model_path: String = "D:/usr/dev/cc/models/qwen2.5-7b-instruct-q4_k_m.gguf"
@export var prefetch_radius: float = 30.0
@export var talk_radius: float = 3.5
@export var chest_radius: float = 2.5
@export var door_radius: float = 2.5

# Day/Night cycle. time_of_day is 0..1 where 0=midnight, 0.25=sunrise,
# 0.5=noon, 0.75=sunset. day_seconds is the wall-clock duration of one
# full cycle. day_night_ratio biases the cycle so daylight stretches
# longer than night (3.0 = 3:1, summer-feel; 1.0 = equinox).
@export_range(0.0, 1.0, 0.001) var time_of_day: float = 0.35
@export_range(30.0, 1800.0, 1.0) var day_seconds: float = 180.0
@export_range(0.5, 5.0, 0.1) var day_night_ratio: float = 3.0

const NIGHT_TOP    := Color(0.02, 0.03, 0.10)
const DAY_TOP      := Color(0.35, 0.55, 0.85)
const NIGHT_HORIZ  := Color(0.05, 0.05, 0.12)
const DAY_HORIZ    := Color(0.75, 0.85, 0.95)
const SUNSET_HORIZ := Color(0.95, 0.55, 0.25)
const NIGHT_AMBIENT := Color(0.20, 0.25, 0.45)
const DAY_AMBIENT   := Color(0.60, 0.70, 0.85)

@onready var terrain: Node3D = $Terrain
@onready var player:  Node3D = $Player
@onready var sun: DirectionalLight3D = $Sun
@onready var world_env: WorldEnvironment = $WorldEnvironment

var time_label: Label = null
var compass_label: Label = null
var sky_material: ProceduralSkyMaterial
var quest_hud: VBoxContainer = null
var _last_quest_signature: String = ""
var _quest_refresh_timer: float = 0.0

var vm: JDBasicVM
var npc_bodies: Array[Node3D] = []
var npc_by_id: Dictionary = {}
var llm_ready: bool = false
var _last_busy: String = ""
var _ready_ids: Dictionary = {}
var _diag_timer: float = 0.0
var _last_raw_seen: String = ""

var talk_hint: CanvasLayer
var talk_hint_label: Label
var nearest_talkable: Node = null
var dialog_ui: CanvasLayer = null
var journal_panel: CanvasLayer = null
var chests: Array[Node3D] = []
var nearest_chest: Node = null
var dungeons: Array[Node3D] = []
var nearest_door: Node = null
var dungeon_bounds_list: Array = []

func _ready() -> void:
	# Wait one frame so Terrain._ready has populated the heightmap.
	await get_tree().process_frame
	var spawn_h := (terrain as Node).call("sample_height", 0.0, 0.0) as float
	player.global_position = Vector3(0.0, spawn_h + 3.0, 0.0)

	_boot_brain()
	_spawn_npcs()
	_build_dungeons()     # before scatter so bounds are known
	_scatter_world()
	_spawn_chests()
	_boot_llm()
	_build_talk_hint()
	_build_time_hud()
	_build_compass_hud()
	_build_quest_hud()
	_build_damage_vignette()
	_refresh_npc_quest_badges()
	sky_material = world_env.environment.sky.sky_material as ProceduralSkyMaterial
	_apply_time_of_day()

func _boot_brain() -> void:
	vm = JDBasicVM.new()
	# EXPORT DIM creates a per-eval-unit scope, so functions in
	# dialog_brain.jdb can't see the `npcs` map populated from
	# npc_brain.jdb. Easiest fix: concatenate both sources into a
	# single eval call so everything lives in one scope.
	var brain_src   := FileAccess.get_file_as_string(NPC_BRAIN)
	var dialog_src  := FileAccess.get_file_as_string(DIALOG_BRAIN)
	if brain_src.is_empty():
		push_error("npc_brain.jdb missing at %s" % NPC_BRAIN)
		return
	if dialog_src.is_empty():
		push_error("dialog_brain.jdb missing at %s" % DIALOG_BRAIN)
		return
	var src := brain_src + "\n" + dialog_src
	var boot := vm.eval(src)
	if not boot.is_empty():
		print("[brain boot] ", boot)

	# Tell the brain to read the same JSON file from disk - it uses
	# TXTREADER$ + JSON.PARSE$ so we just need to give it the OS path.
	var os_path := ProjectSettings.globalize_path(NPCS_JSON).replace("\\", "/")
	var n: Variant = vm.eval_expr('load_npcs("%s")' % os_path)
	print("[brain] loaded %s NPC definitions" % n)

	var lore_path := ProjectSettings.globalize_path(WORLD_LORE).replace("\\", "/")
	var k: Variant = vm.eval_expr('load_world_lore("%s")' % lore_path)
	print("[brain] loaded world_lore with %s top-level fields" % k)

	# Hand the prompt template's OS path to dialog_brain so its
	# build_sys can TXTREADER$ it - relative paths don't resolve
	# from Godot's runtime CWD.
	var tmpl_path := ProjectSettings.globalize_path("res://prompts/dialog_system.tmpl").replace("\\", "/")
	vm.eval('g_template_path = "%s"' % tmpl_path)
	print("[brain] template path set to %s" % tmpl_path)

	# Pre-flight: verify the brain's section builders work. Each
	# eval returns null on jdBasic exception, so we can pinpoint
	# which step throws.
	print("[probe] template file size: ", vm.eval_expr('LEN(TXTREADER$(g_template_path))'))
	print("[probe] build_quest_block(gareth): ", vm.eval_expr('build_quest_block("gareth")'))
	print("[probe] build_world_context: ", vm.eval_expr('LEN(build_world_context())'))
	print("[probe] build_cross_facts(gareth): ", vm.eval_expr('LEN(build_cross_facts("gareth"))'))
	print("[probe] build_player_state: ", vm.eval_expr('LEN(build_player_state())'))
	print("[probe] build_sys(gareth) total length: ", vm.eval_expr('LEN(build_sys("gareth"))'))

func _boot_llm() -> void:
	# dialog_brain.jdb is already concatenated into the VM by
	# _boot_brain(). Here we only have to bring up the LLM itself.
	if not FileAccess.file_exists(model_path):
		push_warning("LLM model not found at %s - prefetch disabled" % model_path)
		return

	var has_llm: Variant = vm.eval_expr('OS.FEATURE("LLM")')
	if has_llm != true:
		push_error("jdbrt.dll was built without LLM support - prefetch disabled")
		return

	# Show a splash so the GPU model-load (5-30s on first launch,
	# depending on disk cache + model size) doesn't look like a hang.
	var splash := _make_splash("Loading %s ..." % model_path.get_file())
	await get_tree().process_frame   # let the splash render before we block

	var path := model_path.replace("\\", "/")
	var t0 := Time.get_ticks_msec()
	var output := vm.eval('PRINT "id="; dialog_init_with("%s", 99)\n' % path)
	var dt := Time.get_ticks_msec() - t0
	print("[llm] GPU load took %s ms, output: %s" % [dt, output.strip_edges()])

	if vm.eval_expr('g_llm') == 0:
		(splash.get_child(0).get_child(0) as Label).text = "GPU failed, retrying on CPU..."
		await get_tree().process_frame
		t0 = Time.get_ticks_msec()
		output = vm.eval('PRINT "id="; dialog_init_with("%s", 0)\n' % path)
		dt = Time.get_ticks_msec() - t0
		print("[llm] CPU load took %s ms, output: %s" % [dt, output.strip_edges()])

	splash.queue_free()

	var verify: Variant = vm.eval_expr('g_llm')
	if typeof(verify) == TYPE_INT and int(verify) > 0:
		llm_ready = true
	elif typeof(verify) == TYPE_FLOAT and float(verify) > 0.0:
		llm_ready = true
	print("[llm] ready=%s (g_llm=%s)" % [llm_ready, verify])

func _make_splash(text: String) -> CanvasLayer:
	var layer := CanvasLayer.new()
	layer.layer = 50
	add_child(layer)
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.07, 0.1, 0.92)
	bg.anchor_right = 1.0
	bg.anchor_bottom = 1.0
	layer.add_child(bg)
	var lbl := Label.new()
	lbl.text = text
	lbl.add_theme_font_size_override("font_size", 32)
	lbl.add_theme_color_override("font_color", Color(0.9, 0.95, 1.0))
	lbl.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	lbl.add_theme_constant_override("outline_size", 6)
	lbl.anchor_left = 0.5
	lbl.anchor_right = 0.5
	lbl.anchor_top = 0.5
	lbl.anchor_bottom = 0.5
	lbl.offset_left = -500
	lbl.offset_right = 500
	lbl.offset_top = -40
	lbl.offset_bottom = 40
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	bg.add_child(lbl)
	return layer

func _spawn_npcs() -> void:
	# Read the same file on the GDScript side so we know which
	# character GLB to instantiate per NPC.
	var raw := FileAccess.get_file_as_string(NPCS_JSON)
	var data: Variant = JSON.parse_string(raw)
	if not (data is Array):
		push_error("npcs.json: expected array, got %s" % typeof(data))
		return

	for entry: Dictionary in data:
		var npc: Node3D = NPC_SCENE.instantiate()
		npc.npc_id         = entry.get("id", "")
		npc.character_type = entry.get("character", "Knight")
		npc.display_name   = entry.get("name", "")
		npc.tint           = Color(entry.get("color", "#ffffff"))
		npc.has_quest      = entry.get("quests", []).size() > 0
		add_child(npc)
		var spawn: Array = entry.get("spawn", [0.0, 0.0, 0.0])
		var sx := float(spawn[0])
		var sz := float(spawn[2])
		var sy := (terrain as Node).call("sample_height", sx, sz) as float
		npc.global_position = Vector3(sx, sy, sz)
		npc_bodies.append(npc)
		npc_by_id[npc.npc_id] = npc
		print("[world] spawned %s (%s) at %s" % [npc.display_name, npc.character_type, npc.global_position])

func _scatter_world() -> void:
	# Forest + path tiles from the ASCII map, keeping a small clear
	# zone around each NPC spawn so they don't end up inside a trunk.
	var scatter := Node3D.new()
	scatter.name = "Scatter"
	scatter.set_script(ScatterScript)
	add_child(scatter)
	var avoid: Array = []
	for npc in npc_bodies:
		avoid.append(Vector2(npc.global_position.x, npc.global_position.z))
	# Avoid every dungeon footprint (called after _build_dungeons).
	scatter.set("avoid_rects", dungeon_bounds_list)
	scatter.call("build", terrain, avoid)

func _build_dungeons() -> void:
	# Read dungeons.json, instantiate each entry, ask the builder to
	# return its footprint + ground-y, then carve the terrain under
	# every dungeon so floors sit flush with the heightmap collider.
	var raw := FileAccess.get_file_as_string(DUNGEONS_JSON)
	if raw.is_empty():
		return
	var data: Variant = JSON.parse_string(raw)
	if not (data is Array):
		push_error("dungeons.json: expected array")
		return
	for spec: Dictionary in data:
		var d := Node3D.new()
		d.name = "Dungeon_" + str(spec.get("id", "?"))
		d.set_script(DungeonScript)
		add_child(d)
		var result: Variant = d.call("build", spec, terrain)
		if result is Array and result.size() == 2:
			var rect: Rect2 = result[0]
			var dy: float = result[1]
			(terrain as Node).call("set_carve_zone", rect, dy - 0.05)
			# 2m padded box so scatter keeps trees away from walls.
			var pad := 2.0
			dungeon_bounds_list.append(Rect2(rect.position - Vector2(pad, pad), rect.size + Vector2(pad * 2, pad * 2)))
			print("[dungeon] %s built at origin %s" % [spec.get("id", "?"), d.get("origin")])
		# Wire the trap_triggered signal to our damage vignette.
		if d.has_signal("trap_triggered"):
			d.connect("trap_triggered", _on_trap_hit)
		dungeons.append(d)

var damage_vignette: ColorRect = null

func _build_damage_vignette() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 6
	add_child(layer)
	damage_vignette = ColorRect.new()
	damage_vignette.color = Color(0.8, 0.0, 0.0, 0.0)
	damage_vignette.anchor_right = 1.0
	damage_vignette.anchor_bottom = 1.0
	damage_vignette.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(damage_vignette)

func _on_trap_hit() -> void:
	if damage_vignette == null:
		return
	var tween := create_tween()
	tween.tween_property(damage_vignette, "color:a", 0.45, 0.08)
	tween.tween_property(damage_vignette, "color:a", 0.0, 0.55)

func _spawn_chests() -> void:
	var raw := FileAccess.get_file_as_string(CHESTS_JSON)
	if raw.is_empty():
		return
	var data: Variant = JSON.parse_string(raw)
	if not (data is Array):
		push_error("chests.json: expected array")
		return
	for entry: Dictionary in data:
		var chest := StaticBody3D.new()
		chest.set_script(ChestScript)
		chest.chest_id  = entry.get("id", "")
		chest.label     = entry.get("label", "Chest")
		chest.contents  = entry.get("contents", [])
		add_child(chest)
		var pos: Array = entry.get("pos", [0.0, 0.0, 0.0])
		var cx := float(pos[0])
		var cz := float(pos[2])
		var cy := (terrain as Node).call("sample_height", cx, cz) as float
		chest.global_position = Vector3(cx, cy, cz)
		var model_file := "res://assets/dungeon/%s.gltf" % entry.get("model", "chest")
		chest.call("setup", model_file, Callable(self, "_on_chest_loot"))
		chests.append(chest)
		print("[chest] spawned %s at %s" % [chest.label, chest.global_position])

func _on_chest_loot(loot: Array, source_label: String) -> void:
	var names: Array = []
	for entry in loot:
		var nm: String = entry.get("name", "trinket")
		vm.eval_expr('add_inventory_item("%s", "%s")' % [nm, source_label])
		names.append(nm)
	print("[chest] %s opened, looted: %s" % [source_label, ", ".join(names)])

func _build_quest_hud() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 4
	add_child(layer)
	var panel := PanelContainer.new()
	panel.anchor_left = 0.0
	panel.anchor_right = 0.0
	panel.anchor_top = 0.0
	panel.anchor_bottom = 0.0
	panel.offset_left = 12.0
	panel.offset_top = 12.0
	panel.offset_right = 480.0
	panel.offset_bottom = 220.0
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.07, 0.08, 0.1, 0.75)
	style.border_color = Color(0.4, 0.5, 0.7, 0.8)
	style.border_width_left = 1
	style.border_width_top = 1
	style.border_width_right = 1
	style.border_width_bottom = 1
	style.corner_radius_top_left = 4
	style.corner_radius_top_right = 4
	style.corner_radius_bottom_right = 4
	style.corner_radius_bottom_left = 4
	style.content_margin_left = 10
	style.content_margin_top = 8
	style.content_margin_right = 10
	style.content_margin_bottom = 8
	panel.add_theme_stylebox_override("panel", style)
	layer.add_child(panel)
	quest_hud = VBoxContainer.new()
	quest_hud.add_theme_constant_override("separation", 4)
	panel.add_child(quest_hud)
	_refresh_quest_hud()

func _refresh_quest_hud() -> void:
	if quest_hud == null or vm == null:
		return
	for c in quest_hud.get_children():
		c.queue_free()
	var title := Label.new()
	title.text = "Active Quests"
	title.add_theme_font_size_override("font_size", 22)
	title.add_theme_color_override("font_color", Color(0.95, 0.85, 0.55))
	quest_hud.add_child(title)
	var n: Variant = vm.eval_expr('quest_count_active()')
	if int(n) == 0:
		var none := Label.new()
		none.text = " (none yet)"
		none.add_theme_color_override("font_color", Color(0.6, 0.6, 0.7))
		none.add_theme_font_size_override("font_size", 18)
		quest_hud.add_child(none)
		return
	for i in int(n):
		var giver: Variant = vm.eval_expr('active_quest_field(%d, "giver_name")' % i)
		var desc:  Variant = vm.eval_expr('active_quest_field(%d, "description")' % i)
		var line := Label.new()
		var short := str(desc)
		if short.length() > 90:
			short = short.substr(0, 87) + "..."
		line.text = "%s - %s" % [giver, short]
		line.add_theme_font_size_override("font_size", 18)
		line.add_theme_color_override("font_color", Color(0.92, 0.92, 0.85))
		line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		line.custom_minimum_size = Vector2(440, 0)
		quest_hud.add_child(line)

# Combined signature of active+completed counts + each npc's quest
# state so we can detect any change cheaply via string compare.
func _compute_quest_signature() -> String:
	var parts: Array = []
	parts.append(str(vm.eval_expr('quest_count_active()')))
	parts.append(str(vm.eval_expr('quest_count_completed()')))
	for npc in npc_bodies:
		var off: Variant = vm.eval_expr('npcs{"%s"}{"quest_offered"}' % npc.npc_id)
		var done: Variant = vm.eval_expr('npcs{"%s"}{"quest_completed"}' % npc.npc_id)
		parts.append("%s:%s/%s" % [npc.npc_id, str(off), str(done)])
	return "|".join(parts)

func _build_compass_hud() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 4
	add_child(layer)
	compass_label = Label.new()
	compass_label.add_theme_font_size_override("font_size", 28)
	compass_label.add_theme_color_override("font_color", Color(1, 1, 1))
	compass_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	compass_label.add_theme_constant_override("outline_size", 6)
	compass_label.anchor_left = 0.5
	compass_label.anchor_right = 0.5
	compass_label.anchor_top = 0.0
	compass_label.anchor_bottom = 0.0
	compass_label.offset_left = -120
	compass_label.offset_right = 120
	compass_label.offset_top = 12
	compass_label.offset_bottom = 50
	compass_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	layer.add_child(compass_label)

func _build_time_hud() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 4
	add_child(layer)
	time_label = Label.new()
	time_label.add_theme_font_size_override("font_size", 26)
	time_label.add_theme_color_override("font_color", Color(1, 1, 1))
	time_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	time_label.add_theme_constant_override("outline_size", 5)
	time_label.anchor_left = 1.0
	time_label.anchor_right = 1.0
	time_label.anchor_top = 0.0
	time_label.anchor_bottom = 0.0
	time_label.offset_left = -180.0
	time_label.offset_top = 12.0
	time_label.offset_right = -12.0
	time_label.offset_bottom = 40.0
	time_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	layer.add_child(time_label)

func _apply_time_of_day() -> void:
	# Geometric model: sun travels a circle in the YZ plane. elevation
	# = -1 below horizon (midnight), 0 at horizons (dawn/dusk), 1
	# overhead (noon). rotation.x = -PI/2 * elevation rotates the
	# DirectionalLight so its -Z (light direction) points down at noon
	# and up at midnight.
	var elevation := -cos(time_of_day * TAU)
	sun.rotation = Vector3(-PI * 0.5 * elevation, 0.0, 0.0)
	var day_factor := clampf(elevation, 0.0, 1.0)
	# dusk_factor peaks when elevation is near 0 (horizon) and the sun
	# is on its way up or down, falling off into night and midday.
	var dusk_factor := clampf(1.0 - abs(elevation) * 4.0, 0.0, 1.0)
	sun.light_energy = day_factor * 1.4 + dusk_factor * 0.3
	sun.light_color = Color(1, 1, 1).lerp(Color(1.0, 0.7, 0.45), dusk_factor)

	if sky_material != null:
		sky_material.sky_top_color = NIGHT_TOP.lerp(DAY_TOP, day_factor)
		var horiz_day := NIGHT_HORIZ.lerp(DAY_HORIZ, day_factor)
		sky_material.sky_horizon_color = horiz_day.lerp(SUNSET_HORIZ, dusk_factor * 0.85)

	var env := world_env.environment
	env.ambient_light_energy = 0.10 + day_factor * 0.45
	env.ambient_light_color = NIGHT_AMBIENT.lerp(DAY_AMBIENT, day_factor)

	if time_label != null:
		var minutes_in_day := 24.0 * 60.0
		var t := time_of_day * minutes_in_day
		var hh := int(t / 60.0) % 24
		var mm := int(t) % 60
		time_label.text = "%02d:%02d" % [hh, mm]

func _build_talk_hint() -> void:
	talk_hint = CanvasLayer.new()
	talk_hint.layer = 5
	add_child(talk_hint)
	talk_hint_label = Label.new()
	talk_hint_label.text = ""
	talk_hint_label.add_theme_font_size_override("font_size", 26)
	talk_hint_label.add_theme_color_override("font_color", Color(1, 1, 1))
	talk_hint_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	talk_hint_label.add_theme_constant_override("outline_size", 6)
	talk_hint_label.anchor_left = 0.5
	talk_hint_label.anchor_right = 0.5
	talk_hint_label.anchor_top = 0.85
	talk_hint_label.anchor_bottom = 0.85
	talk_hint_label.grow_horizontal = Control.GROW_DIRECTION_BOTH
	talk_hint_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	talk_hint.add_child(talk_hint_label)

func _update_talk_hint() -> void:
	if dialog_ui != null or journal_panel != null:
		talk_hint_label.text = ""
		nearest_talkable = null
		nearest_chest = null
		nearest_door = null
		return
	var ppos := player.global_position
	# Three interact targets: NPC / Chest / Door. Pick whichever sits
	# closest relative to its own reach radius.
	var best_npc: Node = null
	var best_npc_d2 := talk_radius * talk_radius
	for npc in npc_bodies:
		var d := npc.global_position - ppos
		var d2 := d.x * d.x + d.z * d.z
		if d2 < best_npc_d2:
			best_npc_d2 = d2
			best_npc = npc
	var best_chest: Node = null
	var best_chest_d2 := chest_radius * chest_radius
	for ch in chests:
		if ch.opened:
			continue
		var d := ch.global_position - ppos
		var d2 := d.x * d.x + d.z * d.z
		if d2 < best_chest_d2:
			best_chest_d2 = d2
			best_chest = ch
	var best_door: Node = null
	var best_door_d2 := door_radius * door_radius
	for dungeon in dungeons:
		var door: Node = dungeon.call("door_at", ppos, door_radius) as Node
		if door != null:
			var d_pos: Vector3 = (door as Node3D).global_position
			var dx: float = d_pos.x - ppos.x
			var dz: float = d_pos.z - ppos.z
			var d2: float = dx * dx + dz * dz
			if d2 < best_door_d2:
				best_door_d2 = d2
				best_door = door

	var npc_t: float = best_npc_d2 / (talk_radius * talk_radius) if best_npc != null else 999.0
	var chest_t: float = best_chest_d2 / (chest_radius * chest_radius) if best_chest != null else 999.0
	var door_t: float = best_door_d2 / (door_radius * door_radius) if best_door != null else 999.0
	if best_npc != null and npc_t <= chest_t and npc_t <= door_t:
		nearest_talkable = best_npc
		nearest_chest = null
		nearest_door = null
		talk_hint_label.text = "[E] Talk to %s" % best_npc.display_name
	elif best_chest != null and chest_t <= door_t:
		nearest_talkable = null
		nearest_chest = best_chest
		nearest_door = null
		talk_hint_label.text = "[E] Open %s" % best_chest.label
	elif best_door != null:
		nearest_talkable = null
		nearest_chest = null
		nearest_door = best_door
		var is_open: bool = best_door.get_meta("open", false)
		talk_hint_label.text = "[E] Close door" if is_open else "[E] Open door"
	else:
		nearest_talkable = null
		nearest_chest = null
		nearest_door = null
		talk_hint_label.text = ""

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("talk") and dialog_ui == null and journal_panel == null:
		if nearest_talkable != null:
			_open_dialog(nearest_talkable)
		elif nearest_chest != null:
			nearest_chest.call("try_open")
		elif nearest_door != null:
			# Find which dungeon owns this door so we can call toggle.
			for dungeon in dungeons:
				if (dungeon as Node).has_method("toggle_door"):
					(dungeon as Node).call("toggle_door", nearest_door)
					break
	elif event.is_action_pressed("open_quests") and dialog_ui == null and journal_panel == null:
		_open_journal(0)
	elif event.is_action_pressed("open_inventory") and dialog_ui == null and journal_panel == null:
		_open_journal(1)

func _open_dialog(npc: Node) -> void:
	dialog_ui = DIALOG_UI.instantiate()
	add_child(dialog_ui)
	dialog_ui.closed.connect(_on_dialog_closed)
	dialog_ui.open(vm, npc.npc_id, npc.display_name, npc.tint)

func _on_dialog_closed() -> void:
	dialog_ui = null

func _open_journal(initial_tab: int) -> void:
	journal_panel = JOURNAL_PANEL.instantiate()
	add_child(journal_panel)
	journal_panel.closed.connect(_on_journal_closed)
	journal_panel.open(vm, initial_tab)

func _on_journal_closed() -> void:
	journal_panel = null

func _process(delta: float) -> void:
	if day_seconds > 0.0:
		# Bias the per-second advance so day:night spans day_night_ratio:1
		# of wall-clock. The two halves of the cycle each cover 0.5 of
		# time_of_day, so we slow the day half and speed up the night
		# half such that the totals add up to one day_seconds.
		var elevation := -cos(time_of_day * TAU)
		var sum_weight := day_night_ratio + 1.0
		var multiplier := (2.0 / sum_weight) if elevation > 0.0 \
			else (2.0 * day_night_ratio / sum_weight)
		time_of_day = fposmod(time_of_day + delta * multiplier / day_seconds, 1.0)
		_apply_time_of_day()
	if compass_label != null:
		_update_compass()

func _update_compass() -> void:
	# Compass reads the camera's forward direction so the player sees
	# where they are LOOKING, not where the body is facing. Spring-arm
	# camera lives at YawPivot under the player.
	var yaw_pivot: Node3D = player.get_node("YawPivot") as Node3D
	if yaw_pivot == null:
		return
	# Camera forward in world space is -Z of YawPivot's basis.
	var fwd := -yaw_pivot.global_transform.basis.z
	var angle := atan2(fwd.x, -fwd.z)   # 0=N (-Z), PI/2=E (+X)
	if angle < 0.0:
		angle += TAU
	var deg := rad_to_deg(angle)
	# 8-way compass
	var label := "N"
	if deg < 22.5 or deg >= 337.5:
		label = "N"
	elif deg < 67.5:
		label = "NE"
	elif deg < 112.5:
		label = "E"
	elif deg < 157.5:
		label = "SE"
	elif deg < 202.5:
		label = "S"
	elif deg < 247.5:
		label = "SW"
	elif deg < 292.5:
		label = "W"
	else:
		label = "NW"
	compass_label.text = "%s  %d°" % [label, int(deg)]

func _physics_process(delta: float) -> void:
	if vm == null:
		return
	_update_talk_hint()
	_quest_refresh_timer += delta
	if _quest_refresh_timer >= 0.5:
		_quest_refresh_timer = 0.0
		var sig := _compute_quest_signature()
		if sig != _last_quest_signature:
			_last_quest_signature = sig
			_refresh_quest_hud()
			_refresh_npc_quest_badges()

	for npc in npc_bodies:
		var pos := npc.global_position
		var ground_y := (terrain as Node).call("sample_height", pos.x, pos.z) as float
		var result: Variant = vm.eval_expr('npc_tick("%s", %f, %f)'
			% [npc.npc_id, delta, ground_y])
		if not (result is PackedFloat64Array) or result.size() < 4:
			continue
		var new_pos := Vector3(result[0], result[1], result[2])
		# Idle/Walk decision: did we actually move on the xz plane?
		var dx := new_pos.x - pos.x
		var dz := new_pos.z - pos.z
		var moved := (dx * dx + dz * dz) > 0.0001
		npc.global_position = new_pos
		npc.set_moving(moved)
		npc.update_facing(result[3], moved, player.global_position, delta)

	if llm_ready:
		var ppos := player.global_position
		var finished: Variant = vm.eval_expr('prefetch_tick(%f, %f, %f)'
			% [ppos.x, ppos.z, prefetch_radius])
		if finished is String and finished != "":
			_on_dialog_ready(finished)
		# busy_for is cheap and only matters when it changes
		var busy: Variant = vm.eval_expr('busy_for()')
		if busy is String and busy != _last_busy:
			_last_busy = busy
			_refresh_busy_label(busy)

		# Every 2s: dump g_task + the last raw LLM string we saw.
		# Lets us see whether the worker is stuck OR whether the
		# parse is rejecting non-JSON output.
		_diag_timer += delta
		if _diag_timer >= 2.0:
			_diag_timer = 0.0
			var g_task_v: Variant = vm.eval_expr('g_task')
			var raw: Variant = vm.eval_expr('g_last_raw')
			var who: Variant = vm.eval_expr('g_last_who')
			var n_npcs: Variant = vm.eval_expr('LEN(MAP.KEYS(npcs))')
			var g_ready: Variant = vm.eval_expr('npcs{"gareth"}{"dialog_ready"}')
			var g_pos_x: Variant = vm.eval_expr('npcs{"gareth"}{"pos_x"}')
			var g_pos_z: Variant = vm.eval_expr('npcs{"gareth"}{"pos_z"}')
			print("[diag] npcs=%s gareth(ready=%s,pos=%s,%s) g_task=%s who=%s raw=%s"
				% [n_npcs, g_ready, g_pos_x, g_pos_z, g_task_v, who, str(raw).substr(0, 200)])

func _refresh_npc_quest_badges() -> void:
	for npc in npc_bodies:
		var off: Variant = vm.eval_expr('npcs{"%s"}{"quest_offered"}' % npc.npc_id)
		var done: Variant = vm.eval_expr('npcs{"%s"}{"quest_completed"}' % npc.npc_id)
		# eval_expr may surface a "[jdb error] ..." String on failure, so
		# squash anything non-bool to false instead of comparing types.
		var off_bool := off is bool and bool(off)
		var done_bool := done is bool and bool(done)
		npc.set_quest_state(off_bool, done_bool)

func _on_dialog_ready(npc_id: String) -> void:
	var npc: Node = npc_by_id.get(npc_id)
	if npc == null:
		return
	_ready_ids[npc_id] = true
	npc.set_dialog_ready(true)
	npc.set_dialog_busy(false)
	var preview: Variant = vm.eval_expr('get_dialog("%s")' % npc_id)
	if preview is String:
		print("[dialog] %s ready: %s" % [npc_id, preview])

func _refresh_busy_label(busy_id: String) -> void:
	for npc in npc_bodies:
		if _ready_ids.has(npc.npc_id):
			continue
		npc.set_dialog_busy(npc.npc_id == busy_id)
