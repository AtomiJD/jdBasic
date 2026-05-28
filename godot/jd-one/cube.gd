extends Node3D

# Tier 2 cube demo. The JDBScript child node owns the jdBasic VM and
# auto-dispatches on_process(delta) every frame. This script is reduced
# to thin glue: read display_angle() / hue() from jdBasic, apply to the
# Godot mesh; on button press, mutate `rot_speed` in the running VM.
#
# Cube3D has process_priority = 1 so JDBScript (default 0) runs first
# every frame - we read the freshly-updated values, not stale ones.

@onready var cube:         MeshInstance3D = $Cube
@onready var jdb:          JDBScript      = $JDBScript
@onready var speed_label:  Label          = $UI/Panel/VBox/SpeedLabel
@onready var angle_label:  Label          = $UI/Panel/VBox/AngleLabel
@onready var status_label: Label          = $UI/Panel/VBox/StatusLabel

var current_speed := 1.0
var cube_material: StandardMaterial3D

func _ready() -> void:
	cube_material = cube.material_override.duplicate()
	cube.material_override = cube_material
	_refresh_speed_label()
	if jdb.last_error().is_empty():
		status_label.text = "loaded " + jdb.get_script_path()
	else:
		status_label.text = "ERR: " + jdb.last_error()

func _process(_delta: float) -> void:
	var a: float = float(jdb.call("display_angle", []))
	cube.rotation.y = a
	angle_label.text = "angle = %.2f rad" % a
	var h: float = float(jdb.call("hue", []))
	cube_material.albedo_color = Color.from_hsv(h, 0.7, 0.95)

func _on_slow_pressed() -> void:
	current_speed = 2.0
	jdb.set_var("rot_speed", 2.0)
	_refresh_speed_label()

func _on_fast_pressed() -> void:
	current_speed = 10.0
	jdb.set_var("rot_speed", 10.0)
	_refresh_speed_label()

func _on_reverse_pressed() -> void:
	current_speed = -current_speed
	jdb.set_var("rot_speed", current_speed)
	_refresh_speed_label()

func _on_recompile_pressed() -> void:
	var summary := jdb.recompile()
	if summary.is_empty():
		status_label.text = "ERR: " + jdb.last_error()
	else:
		status_label.text = "recompiled - " + summary

func _refresh_speed_label() -> void:
	speed_label.text = "rot_speed = %.1f" % current_speed
