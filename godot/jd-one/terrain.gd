extends Node3D

# E6 demo - procedural heightmap generated entirely by jdBasic vector
# operations, marshalled across the C-ABI as a PackedFloat64Array, then
# turned into an ArrayMesh on the Godot side.
#
# The compute side is a few lines of APL-flavoured BASIC (IOTA + SIN +
# COS + broadcast multiplication). The mesh-building side is generic
# GDScript that doesn't know anything about the math; it just walks the
# grid and stitches triangles.

@onready var mesh_inst:   MeshInstance3D = $Mesh
@onready var info_label:  Label          = $UI/Panel/VBox/InfoLabel
@onready var grid_label:  Label          = $UI/Panel/VBox/GridLabel

const JDB_SOURCE := """
' Procedural heightmap - APL-style vector math, no loops needed.
'
' IOTA(total) returns [0, 1, ..., total-1]. MOD and integer-divide
' against the grid size derive (x, y) coords per cell. Broadcasting
' arithmetic ops (+, -, *, /) over the array gives us an in-place
' coordinate transform; SIN / COS of an array are themselves arrays.
FUNC build_heightmap(n, frequency, amplitude)
\tDIM total = n * n
\tDIM idx = IOTA(total)
\tDIM gx = idx MOD n
\tDIM gy = (idx - gx) / n
\tDIM cx = (gx - n / 2.0) / frequency
\tDIM cy = (gy - n / 2.0) / frequency
\tRETURN SIN(cx) * COS(cy) * amplitude + SIN(cx * 2.0) * COS(cy * 2.0) * (amplitude * 0.25)
ENDFUNC
"""

var current_grid: int = 48
var current_freq: float = 6.0
var current_amp: float  = 1.4
var vm: JDBasicVM

func _ready() -> void:
	vm = JDBasicVM.new()
	var boot := vm.eval(JDB_SOURCE)
	if not boot.is_empty():
		print("[jdBasic boot] ", boot)
	_rebuild()

func _rebuild() -> void:
	var t0 := Time.get_ticks_usec()
	var heights = vm.eval_expr("build_heightmap(%d, %f, %f)"
		% [current_grid, current_freq, current_amp])
	var t1 := Time.get_ticks_usec()

	if not (heights is PackedFloat64Array):
		push_error("jdBasic returned %s instead of PackedFloat64Array" % typeof(heights))
		return
	if heights.size() != current_grid * current_grid:
		push_error("expected %d heights, got %d" % [current_grid * current_grid, heights.size()])
		return

	mesh_inst.mesh = _build_mesh(current_grid, heights)
	var t2 := Time.get_ticks_usec()
	info_label.text = "jdBasic compute: %.2f ms\nmesh build:     %.2f ms" \
		% [(t1 - t0) / 1000.0, (t2 - t1) / 1000.0]
	grid_label.text = "grid %d x %d  (%d vertices)" \
		% [current_grid, current_grid, heights.size()]

func _build_mesh(n: int, heights: PackedFloat64Array) -> ArrayMesh:
	# Plain triangle-soup terrain; each cell becomes two triangles.
	# A subdivided ArrayMesh would be slightly more efficient with shared
	# vertices, but for a demo this is fine and lets generate_normals
	# do its thing without per-vertex normal computation.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var half := n * 0.5
	for y in range(n - 1):
		for x in range(n - 1):
			var i00 := y * n + x
			var i10 := y * n + (x + 1)
			var i01 := (y + 1) * n + x
			var i11 := (y + 1) * n + (x + 1)
			var v00 := Vector3(x - half,     heights[i00], y - half)
			var v10 := Vector3(x + 1 - half, heights[i10], y - half)
			var v01 := Vector3(x - half,     heights[i01], y + 1 - half)
			var v11 := Vector3(x + 1 - half, heights[i11], y + 1 - half)
			st.add_vertex(v00)
			st.add_vertex(v10)
			st.add_vertex(v11)
			st.add_vertex(v00)
			st.add_vertex(v11)
			st.add_vertex(v01)
	st.generate_normals()
	return st.commit()

func _on_smaller_pressed() -> void:
	current_grid = maxi(8, current_grid - 8)
	_rebuild()

func _on_bigger_pressed() -> void:
	current_grid = mini(128, current_grid + 8)
	_rebuild()

func _on_rougher_pressed() -> void:
	current_freq = max(2.0, current_freq - 1.0)
	_rebuild()

func _on_smoother_pressed() -> void:
	current_freq = min(20.0, current_freq + 1.0)
	_rebuild()
