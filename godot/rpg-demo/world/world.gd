extends Node3D

# World root. Owns the JDBasicVM that hosts the NPC brain, spawns NPC
# bodies from npcs.json, and ticks them every physics frame.

const NPCS_JSON     := "res://assets/data/npcs.json"
const NPC_BRAIN     := "res://world/npc_brain.jdb"
const DIALOG_BRAIN  := "res://world/dialog_brain.jdb"
const NPC_SCENE     := preload("res://world/npc.tscn")
const DIALOG_UI     := preload("res://world/dialog_ui.tscn")
const JOURNAL_PANEL := preload("res://world/journal_panel.tscn")

@export var model_path: String = "D:/usr/dev/cc/models/Phi-3-mini-4k-instruct-q4.gguf"
@export var prefetch_radius: float = 30.0
@export var talk_radius: float = 3.5

@onready var terrain: Node3D = $Terrain
@onready var player:  Node3D = $Player

var vm: JDBasicVM
var npc_bodies: Array[Node3D] = []
var npc_by_id: Dictionary = {}
var llm_ready: bool = false
var _last_busy: String = ""
var _ready_ids: Dictionary = {}
var _frame_count: int = 0

var talk_hint: CanvasLayer
var talk_hint_label: Label
var nearest_talkable: Node = null
var dialog_ui: CanvasLayer = null
var journal_panel: CanvasLayer = null

func _ready() -> void:
	# Wait one frame so Terrain._ready has populated the heightmap.
	await get_tree().process_frame
	var spawn_h := (terrain as Node).call("sample_height", 0.0, 0.0) as float
	player.global_position = Vector3(0.0, spawn_h + 3.0, 0.0)

	_boot_brain()
	_spawn_npcs()
	_boot_llm()
	_build_talk_hint()

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

func _boot_llm() -> void:
	# dialog_brain.jdb is already concatenated into the VM by
	# _boot_brain(). Here we only have to bring up the LLM itself.
	if not FileAccess.file_exists(model_path):
		push_warning("LLM model not found at %s - prefetch disabled" % model_path)
		return

	var has_llm: Variant = vm.eval_expr('OS.FEATURE("LLM")')
	print("[llm] OS.FEATURE(\"LLM\") = ", has_llm)
	if has_llm != true:
		push_error("jdbrt.dll was built without LLM support - prefetch disabled")
		return

	# Try GPU first; if it fails, fall back to CPU so we can at least
	# see if the model file itself is readable. Either path is fine for
	# a demo - Phi-3 Q4 on CPU does ~10 tok/s, still usable for
	# prefetched greetings.
	print("[llm] === GPU attempt (n_gpu_layers=99) ===")
	var path := model_path.replace("\\", "/")
	var t0 := Time.get_ticks_msec()
	var output := vm.eval('PRINT "id="; dialog_init_with("%s", 99)\n' % path)
	var dt := Time.get_ticks_msec() - t0
	print("[llm] GPU eval took ", dt, " ms, output: ", output.strip_edges())

	if vm.eval_expr('g_llm') == 0:
		print("[llm] === GPU failed, trying CPU (n_gpu_layers=0) ===")
		t0 = Time.get_ticks_msec()
		output = vm.eval('PRINT "id="; dialog_init_with("%s", 0)\n' % path)
		dt = Time.get_ticks_msec() - t0
		print("[llm] CPU eval took ", dt, " ms, output: ", output.strip_edges())

	var verify: Variant = vm.eval_expr('g_llm')
	print("[llm] g_llm after init = ", verify, " (type=", typeof(verify), ")")
	if typeof(verify) == TYPE_INT and int(verify) > 0:
		llm_ready = true
	elif typeof(verify) == TYPE_FLOAT and float(verify) > 0.0:
		llm_ready = true
	print("[llm] llm_ready = ", llm_ready)

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
		add_child(npc)
		var spawn: Array = entry.get("spawn", [0.0, 0.0, 0.0])
		var sx := float(spawn[0])
		var sz := float(spawn[2])
		var sy := (terrain as Node).call("sample_height", sx, sz) as float
		npc.global_position = Vector3(sx, sy, sz)
		npc_bodies.append(npc)
		npc_by_id[npc.npc_id] = npc
		print("[world] spawned %s (%s) at %s" % [npc.display_name, npc.character_type, npc.global_position])

func _build_talk_hint() -> void:
	talk_hint = CanvasLayer.new()
	talk_hint.layer = 5
	add_child(talk_hint)
	talk_hint_label = Label.new()
	talk_hint_label.text = ""
	talk_hint_label.add_theme_font_size_override("font_size", 20)
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
	if dialog_ui != null:
		talk_hint_label.text = ""
		nearest_talkable = null
		return
	var ppos := player.global_position
	var best: Node = null
	var best_d2 := talk_radius * talk_radius
	for npc in npc_bodies:
		var d := npc.global_position - ppos
		var d2 := d.x * d.x + d.z * d.z
		if d2 < best_d2:
			best_d2 = d2
			best = npc
	nearest_talkable = best
	if best != null:
		talk_hint_label.text = "[E] Talk to %s" % best.display_name
	else:
		talk_hint_label.text = ""

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("talk") and nearest_talkable != null and dialog_ui == null and journal_panel == null:
		_open_dialog(nearest_talkable)
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

func _physics_process(delta: float) -> void:
	if vm == null:
		return
	_update_talk_hint()
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
		(npc.get_node("Visual") as Node3D).rotation.y = result[3]
		npc.set_moving(moved)

	if llm_ready:
		var ppos := player.global_position
		var finished: Variant = vm.eval_expr('prefetch_tick(%f, %f, %f)'
			% [ppos.x, ppos.z, prefetch_radius])
		if _frame_count == 0 or _frame_count == 60:
			var g_task: Variant = vm.eval_expr('g_task')
			var diag: Variant = vm.eval_expr('g_diag_log')
			print("[tick %s] g_task=%s diag=%s" % [_frame_count, g_task, diag])
		_frame_count += 1
		if finished is String and finished != "":
			_on_dialog_ready(finished)
		# busy_for is cheap and only matters when it changes
		var busy: Variant = vm.eval_expr('busy_for()')
		if busy is String and busy != _last_busy:
			_last_busy = busy
			_refresh_busy_label(busy)

func _on_dialog_ready(npc_id: String) -> void:
	var npc: Node = npc_by_id.get(npc_id)
	if npc == null:
		return
	_ready_ids[npc_id] = true
	(npc.get_node("NameLabel") as Label3D).text = "%s  ✓" % npc.display_name
	var preview: Variant = vm.eval_expr('get_dialog("%s")' % npc_id)
	if preview is String:
		print("[dialog] %s ready: %s" % [npc_id, preview])

func _refresh_busy_label(busy_id: String) -> void:
	# Reset any previous spinner that isn't ready yet
	for npc in npc_bodies:
		if _ready_ids.has(npc.npc_id):
			continue
		var label: Label3D = npc.get_node("NameLabel") as Label3D
		if npc.npc_id == busy_id:
			label.text = "%s  …" % npc.display_name
		else:
			label.text = npc.display_name
