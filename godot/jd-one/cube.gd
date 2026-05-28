extends Node3D

# E2 demo - jdBasic drives the cube's rotation per frame.
#
# The full integration loop lives in the embedded jdBasic VM:
#
#     DIM angle = 0.0
#     DIM rot_speed = 1.0
#     SUB on_process(delta)
#         angle = angle + rot_speed * delta
#     ENDSUB
#
# Per frame Godot calls on_process(delta) then reads `angle` back.
# Press FAST to bump rot_speed live - no scene reload, no recompile.

const JDB_SOURCE := """
DIM angle = 0.0
DIM rot_speed = 1.0

SUB on_process(delta)
	angle = angle + rot_speed * delta
ENDSUB
"""

@onready var cube:        MeshInstance3D = $Cube
@onready var speed_label: Label          = $UI/Panel/VBox/SpeedLabel
@onready var angle_label: Label          = $UI/Panel/VBox/AngleLabel

var vm: JDBasicVM
var current_speed := 1.0

func _ready() -> void:
	vm = JDBasicVM.new()
	# Boot the script into the persistent VM. After this the FUNC + globals
	# are resident; subsequent eval calls only run the snippet we hand them.
	var boot := vm.eval(JDB_SOURCE)
	if not boot.is_empty():
		print("[jdBasic boot] ", boot)
	_refresh_speed_label()

func _process(delta: float) -> void:
	# Two evals per frame. The first runs the FUNC body; the second
	# captures the global as a PRINTed string we parse. E3 will replace
	# this with a direct get_double() shim.
	vm.eval("on_process(%f)" % delta)
	var s := vm.eval("PRINT angle")
	var a := s.strip_edges().to_float()
	cube.rotation.y = a
	angle_label.text = "angle = %.2f rad" % a

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

func _refresh_speed_label() -> void:
	speed_label.text = "rot_speed = %.1f" % current_speed
