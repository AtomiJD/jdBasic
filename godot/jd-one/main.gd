extends Control

# First-look-at-it demo for the jdb_godot GDExtension.
#
# Press the button -> we call into the embedded jdBasic VM, eval an
# expression, and display the captured PRINT output. The VM is persistent
# across button presses, so DIM'd variables and modified maps survive.

@onready var output: RichTextLabel = $VBox/Output
@onready var input:  CodeEdit      = $VBox/Input

var vm: JDBasicVM

func _ready() -> void:
	vm = JDBasicVM.new()
	# Boot the VM with one canary eval so the first user click feels instant.
	var _boot := vm.eval("PRINT \"jdBasic VM up: \" + STR$(3 * 7)")
	output.append_text("[color=#88ddff]" + _boot + "[/color]\n")

func _on_run_pressed() -> void:
	var code := input.text
	output.append_text("[color=#ffd66e]> " + code.replace("\n", "\n> ") + "[/color]\n")
	var result := vm.eval(code)
	if result.is_empty():
		var err := vm.last_error()
		if err.is_empty():
			output.append_text("[color=#888](no output)[/color]\n")
		else:
			output.append_text("[color=#ff7070]ERR: " + err + "[/color]\n")
	else:
		output.append_text(result)
		if not result.ends_with("\n"):
			output.append_text("\n")

func _on_clear_pressed() -> void:
	output.clear()
