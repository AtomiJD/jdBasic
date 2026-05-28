extends Node3D

# E4 demo - jdBasic drives the cube AND Atomi can edit the .jdb file
# while the scene runs.
#
# Workflow:
#   1. F5 starts the scene. cube.jdb gets loaded into the persistent VM.
#   2. Atomi edits cube.jdb (add wobble, change hue speed, anything).
#   3. Atomi clicks the RECOMPILE button.
#   4. The new FUNC bodies swap into the running VM. `angle`, `rot_speed`
#      etc. keep their current values - state survives the recompile.

const JDB_PATH := "res://cube.jdb"

@onready var cube:        MeshInstance3D = $Cube
@onready var speed_label: Label          = $UI/Panel/VBox/SpeedLabel
@onready var angle_label: Label          = $UI/Panel/VBox/AngleLabel
@onready var status_label: Label         = $UI/Panel/VBox/StatusLabel

var vm: JDBasicVM
var current_speed := 1.0
var cube_material: StandardMaterial3D

func _ready() -> void:
	vm = JDBasicVM.new()
	cube_material = cube.material_override.duplicate()
	cube.material_override = cube_material
	_load_script()
	_refresh_speed_label()

func _load_script() -> void:
	# Read the .jdb file via Godot's resource system, then hand the source
	# to the embedded VM. We avoid jdb_embed_load() here because that one
	# wants an OS path; res:// is Godot's virtual filesystem.
	var source := FileAccess.get_file_as_string(JDB_PATH)
	if source.is_empty():
		status_label.text = "ERR: could not read " + JDB_PATH
		return
	var out := vm.eval(source)
	status_label.text = "loaded cube.jdb"
	if not out.is_empty():
		print("[jdBasic boot] ", out)

func _process(delta: float) -> void:
	vm.eval("on_process(%f)" % delta)
	var a := vm.eval("PRINT angle").strip_edges().to_float()
	cube.rotation.y = a
	angle_label.text = "angle = %.2f rad" % a
	# Hue cycle - the cube colour reads from the jdBasic FUNC hue() so
	# editing that function in cube.jdb + Recompile changes the colour
	# behaviour live.
	var h := vm.eval("PRINT hue()").strip_edges().to_float()
	cube_material.albedo_color = Color.from_hsv(h, 0.7, 0.95)

func _on_slow_pressed() -> void:
	current_speed = 1.0
	vm.eval("rot_speed = 1.0")
	_refresh_speed_label()

func _on_fast_pressed() -> void:
	current_speed = 10.0
	vm.eval("rot_speed = 10.0")
	_refresh_speed_label()

func _on_reverse_pressed() -> void:
	current_speed = -current_speed
	vm.eval("rot_speed = %f" % current_speed)
	_refresh_speed_label()

func _on_recompile_pressed() -> void:
	# Read the file fresh from disk so the editor's latest save is what
	# gets merged. ProjectSettings.globalize_path turns res:// into an
	# absolute OS path that jdb_embed can open directly.
	var os_path := ProjectSettings.globalize_path(JDB_PATH)
	var summary := vm.recompile(os_path)
	if summary.is_empty():
		status_label.text = "ERR: " + vm.last_error()
	else:
		status_label.text = "recompiled - " + summary

func _refresh_speed_label() -> void:
	speed_label.text = "rot_speed = %.1f" % current_speed
