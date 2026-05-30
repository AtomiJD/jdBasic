extends StaticBody3D

# Terrain node - builds an ArrayMesh + HeightMapShape3D collision from
# a jdBasic-generated heightmap. The math lives in terrain_gen.jdb so
# we can hot-edit it without recompiling anything.
#
# World layout: 1 grid cell = 1 world unit. Terrain is centred on the
# origin and extends -N/2 to +N/2 in X and Z.

const TERRAIN_JDB := "res://world/terrain_gen.jdb"
const WORLD_MAP   := "res://assets/data/world_map.txt"

@export var grid_size: int = 192
@export var frequency: float = 28.0
@export var amplitude: float = 2.5
@export var noise_seed: int = 7

@export var grass_color: Color = Color(0.45, 0.6, 0.3)
@export var path_color:  Color = Color(0.50, 0.40, 0.28)
@export var clearing_color: Color = Color(0.55, 0.65, 0.40)

var vm: JDBasicVM
var heights: PackedFloat64Array
var map_rows: PackedStringArray = PackedStringArray()
var map_n: int = 0

# Carve zone: a rectangular area of the heightmap forced to a fixed
# y. Used by world.gd to flatten ground under the dungeon so the
# floor sits at terrain level instead of floating / clipping.
var carve_bounds: Rect2 = Rect2()
var carve_y: float = 0.0

func _ready() -> void:
	vm = JDBasicVM.new()
	var src := FileAccess.get_file_as_string(TERRAIN_JDB)
	if src.is_empty():
		push_error("terrain_gen.jdb not found at %s" % TERRAIN_JDB)
		return
	var boot := vm.eval(src)
	if not boot.is_empty():
		print("[terrain boot] ", boot)
	_load_map()
	_rebuild()

func _load_map() -> void:
	var raw := FileAccess.get_file_as_string(WORLD_MAP)
	map_rows = raw.strip_edges(false, true).split("\n")
	map_n = map_rows.size()
	if map_n == 0:
		push_warning("[terrain] world_map.txt empty - vertices stay grass-coloured")

# Public: which tile char sits at world (x, z)? '.' fallback when off-map.
func tile_at(world_x: float, world_z: float) -> String:
	if map_n == 0:
		return "."
	var half := grid_size * 0.5
	var cell := float(grid_size) / float(map_n)
	var col := int(floor((world_x + half) / cell))
	var row := int(floor((world_z + half) / cell))
	if col < 0 or row < 0 or col >= map_n or row >= map_n:
		return "."
	var line: String = map_rows[row]
	if col >= line.length():
		return "."
	return line[col]

func color_for_tile(ch: String) -> Color:
	match ch:
		"#": return path_color
		"o": return clearing_color
		_:   return grass_color

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
	_apply_carve()

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

# Public: world.gd calls this once the dungeon footprint is known so
# the heightmap (mesh + collider + sample_height) gets flattened
# inside the rect to a fixed y. Triggers a full rebuild.
func set_carve_zone(rect: Rect2, y: float) -> void:
	carve_bounds = rect
	carve_y = y
	_rebuild()

func _apply_carve() -> void:
	if carve_bounds.size.x <= 0 or carve_bounds.size.y <= 0:
		return
	var half := grid_size * 0.5
	# Heightmap is sampled at integer (x, z) cell coords centred on
	# origin. Find the cell range that overlaps the carve rect.
	var min_x := int(floor(carve_bounds.position.x + half))
	var max_x := int(ceil(carve_bounds.position.x + carve_bounds.size.x + half))
	var min_z := int(floor(carve_bounds.position.y + half))
	var max_z := int(ceil(carve_bounds.position.y + carve_bounds.size.y + half))
	min_x = clamp(min_x, 0, grid_size - 1)
	max_x = clamp(max_x, 0, grid_size - 1)
	min_z = clamp(min_z, 0, grid_size - 1)
	max_z = clamp(max_z, 0, grid_size - 1)
	for z in range(min_z, max_z + 1):
		for x in range(min_x, max_x + 1):
			heights[z * grid_size + x] = carve_y

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
			# Per-vertex colour from the tile map. Vertex coords are
			# already centred on the origin so we can pass them
			# straight to tile_at(). Linear interpolation across the
			# triangle gives a soft path edge for free.
			st.set_color(color_for_tile(tile_at(v00.x, v00.z)))
			st.add_vertex(v00)
			st.set_color(color_for_tile(tile_at(v10.x, v10.z)))
			st.add_vertex(v10)
			st.set_color(color_for_tile(tile_at(v11.x, v11.z)))
			st.add_vertex(v11)
			st.set_color(color_for_tile(tile_at(v00.x, v00.z)))
			st.add_vertex(v00)
			st.set_color(color_for_tile(tile_at(v11.x, v11.z)))
			st.add_vertex(v11)
			st.set_color(color_for_tile(tile_at(v01.x, v01.z)))
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
