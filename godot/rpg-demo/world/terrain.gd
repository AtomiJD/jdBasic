extends StaticBody3D

# Terrain node - builds an ArrayMesh + HeightMapShape3D collision from
# a jdBasic-generated heightmap. The math lives in terrain_gen.jdb so
# we can hot-edit it without recompiling anything.
#
# World layout: 1 grid cell = 1 world unit. Terrain is centred on the
# origin and extends -N/2 to +N/2 in X and Z.

const TERRAIN_JDB := "res://world/terrain_gen.jdb"

@export var grid_size: int = 192
@export var frequency: float = 28.0
@export var amplitude: float = 2.5
@export var noise_seed: int = 7

var vm: JDBasicVM
var heights: PackedFloat64Array

func _ready() -> void:
	vm = JDBasicVM.new()
	var src := FileAccess.get_file_as_string(TERRAIN_JDB)
	if src.is_empty():
		push_error("terrain_gen.jdb not found at %s" % TERRAIN_JDB)
		return
	var boot := vm.eval(src)
	if not boot.is_empty():
		print("[terrain boot] ", boot)
	_rebuild()

func _rebuild() -> void:
	var t0 := Time.get_ticks_usec()
	var raw = vm.eval_expr("build_heightmap(%d, %f, %f, %d)"
		% [grid_size, frequency, amplitude, noise_seed])
	var t1 := Time.get_ticks_usec()

	if not (raw is PackedFloat64Array):
		push_error("jdBasic returned %s, expected PackedFloat64Array" % typeof(raw))
		return
	if raw.size() != grid_size * grid_size:
		push_error("expected %d heights, got %d" % [grid_size * grid_size, raw.size()])
		return

	heights = raw

	# Visual mesh
	var mesh_inst := $Mesh as MeshInstance3D
	mesh_inst.mesh = _build_mesh(grid_size, heights)
	var t2 := Time.get_ticks_usec()

	# Collision - HeightMapShape3D wants PackedFloat32Array, Z-row-major.
	# It also needs grid_size x grid_size samples spanning the same world
	# extents as the visual mesh.
	var f32 := PackedFloat32Array()
	f32.resize(heights.size())
	for i in range(heights.size()):
		f32[i] = heights[i]
	var shape := HeightMapShape3D.new()
	shape.map_width = grid_size
	shape.map_depth = grid_size
	shape.map_data = f32
	($Collision as CollisionShape3D).shape = shape
	var t3 := Time.get_ticks_usec()

	print("[terrain] jdBasic: %.2f ms | mesh: %.2f ms | collider: %.2f ms"
		% [(t1 - t0) / 1000.0, (t2 - t1) / 1000.0, (t3 - t2) / 1000.0])

func _build_mesh(n: int, h: PackedFloat64Array) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var half := n * 0.5
	for y in range(n - 1):
		for x in range(n - 1):
			var i00 := y * n + x
			var i10 := y * n + (x + 1)
			var i01 := (y + 1) * n + x
			var i11 := (y + 1) * n + (x + 1)
			var v00 := Vector3(x - half,     h[i00], y - half)
			var v10 := Vector3(x + 1 - half, h[i10], y - half)
			var v01 := Vector3(x - half,     h[i01], y + 1 - half)
			var v11 := Vector3(x + 1 - half, h[i11], y + 1 - half)
			st.add_vertex(v00)
			st.add_vertex(v10)
			st.add_vertex(v11)
			st.add_vertex(v00)
			st.add_vertex(v11)
			st.add_vertex(v01)
	st.generate_normals()
	return st.commit()

# Public sample for spawners. Returns the world-Y at (world_x, world_z).
func sample_height(world_x: float, world_z: float) -> float:
	if heights.is_empty():
		return 0.0
	var half := grid_size * 0.5
	var gx := world_x + half
	var gz := world_z + half
	var ix := int(gx)
	var iz := int(gz)
	if ix < 0 or iz < 0 or ix >= grid_size - 1 or iz >= grid_size - 1:
		return 0.0
	var fx := gx - ix
	var fz := gz - iz
	var h00 := heights[iz * grid_size + ix]
	var h10 := heights[iz * grid_size + (ix + 1)]
	var h01 := heights[(iz + 1) * grid_size + ix]
	var h11 := heights[(iz + 1) * grid_size + (ix + 1)]
	var h0 := h00 * (1.0 - fx) + h10 * fx
	var h1 := h01 * (1.0 - fx) + h11 * fx
	return h0 * (1.0 - fz) + h1 * fz
