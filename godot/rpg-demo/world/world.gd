extends Node3D

# World root. Owns the JDBasicVM that hosts the NPC brain, spawns NPC
# bodies from npcs.json, and ticks them every physics frame.

const NPCS_JSON     := "res://assets/data/npcs.json"
const NPC_BRAIN     := "res://world/npc_brain.jdb"
const DIALOG_BRAIN  := "res://world/dialog_brain.jdb"
const NPC_SCENE     := preload("res://world/npc.tscn")

@export var model_path: String = "D:/usr/dev/cc/models/Phi-3-mini-4k-instruct-q4.gguf"
@export var prefetch_radius: float = 30.0

@onready var terrain: Node3D = $Terrain
@onready var player:  Node3D = $Player

var vm: JDBasicVM
var npc_bodies: Array[Node3D] = []
var npc_by_id: Dictionary = {}
var llm_ready: bool = false
var _last_busy: String = ""
var _ready_ids: Dictionary = {}
var _frame_count: int = 0

func _ready() -> void:
	# Wait one frame so Terrain._ready has populated the heightmap.
	await get_tree().process_frame
	var spawn_h := (terrain as Node).call("sample_height", 0.0, 0.0) as float
	player.global_position = Vector3(0.0, spawn_h + 3.0, 0.0)

	_boot_brain()
	_spawn_npcs()
	_boot_llm()

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
		add_child(npc)
		var spawn: Array = entry.get("spawn", [0.0, 0.0, 0.0])
		var sx := float(spawn[0])
		var sz := float(spawn[2])
		var sy := (terrain as Node).call("sample_height", sx, sz) as float
		npc.global_position = Vector3(sx, sy, sz)
		npc_bodies.append(npc)
		npc_by_id[npc.npc_id] = npc
		print("[world] spawned %s (%s) at %s" % [npc.display_name, npc.character_type, npc.global_position])

func _physics_process(delta: float) -> void:
	if vm == null:
		return
	for npc in npc_bodies:
		var pos := npc.global_position
		var ground_y := (terrain as Node).call("sample_height", pos.x, pos.z) as float
		var result: Variant = vm.eval_expr('npc_tick("%s", %f, %f)'
			% [npc.npc_id, delta, ground_y])
		if not (result is PackedFloat64Array) or result.size() < 4:
			continue
		npc.global_position = Vector3(result[0], result[1], result[2])
		(npc.get_node("Visual") as Node3D).rotation.y = result[3]

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
