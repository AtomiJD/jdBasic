extends Node3D

# World scatter: reads a tile-grid ASCII map, populates the terrain
# with trees, bushes and dirt paths. Designed so the same map can
# later host buildings, NPC spawn hints etc. - just add new chars.
#
# Tile chars currently understood:
#   T  dense tree cluster (3-5 trees)
#   .  grass, sparse (0-2 trees + maybe a bush)
#   o  clearing (no trees, maybe a single bush)
#   #  dirt path (no plants, brown ground patch)
#   _  reserved for future village floor
#   H  reserved for future house anchor
#
# The map is square, NxN, mapped onto the terrain bounds
# [-grid_size/2 .. grid_size/2] from terrain.gd.

const TREES := [
	"res://assets/nature/Tree_1_A_Color1.gltf",
	"res://assets/nature/Tree_2_A_Color1.gltf",
	"res://assets/nature/Tree_2_C_Color1.gltf",
	"res://assets/nature/Tree_3_A_Color1.gltf",
]
const BUSHES := [
	"res://assets/nature/Bush_1_A_Color1.gltf",
	"res://assets/nature/Bush_2_A_Color1.gltf",
	"res://assets/nature/Bush_3_A_Color1.gltf",
]

@export var map_path: String = "res://assets/data/world_map.txt"
@export var npc_clear_radius: float = 2.5
@export var tree_trunk_radius: float = 0.45
@export var tree_trunk_height: float = 3.0

var tree_scenes: Array = []
var bush_scenes: Array = []
var npc_avoid: Array = []  # [(x, z), ...] - world coords to keep tree-free
var avoid_rects: Array = []  # [Rect2(x, z, w, h), ...] - world-aligned no-spawn boxes

func build(terrain: Node3D, npc_spawn_xz: Array) -> void:
	npc_avoid = npc_spawn_xz
	for p in TREES:
		var s := load(p) as PackedScene
		if s != null: tree_scenes.append(s)
	for p in BUSHES:
		var s := load(p) as PackedScene
		if s != null: bush_scenes.append(s)
	if tree_scenes.is_empty():
		push_warning("[scatter] no tree scenes loaded - check assets/nature")
		return

	var raw := FileAccess.get_file_as_string(map_path)
	var rows := raw.strip_edges(false, true).split("\n")
	for r in range(rows.size()):
		# pad/trim row strings; chars beyond expected width get dropped
		var line: String = rows[r]
		for c in range(line.length()):
			_place_tile(terrain, line[c], c, r, rows.size())

func _place_tile(terrain: Node3D, ch: String, col: int, row: int, n: int) -> void:
	# Map (col, row) onto world (x, z). Cell centre uses 0.5 offset.
	var grid_size_world := (terrain as Node).get("grid_size") as float
	var cell := grid_size_world / float(n)
	var wx := (float(col) + 0.5) * cell - grid_size_world * 0.5
	var wz := (float(row) + 0.5) * cell - grid_size_world * 0.5

	match ch:
		"#":
			# Path is painted by terrain.gd via vertex colours - we
			# just skip placing any greenery here.
			pass
		"T":
			_place_trees(terrain, wx, wz, cell, randi_range(3, 5))
		".":
			_place_trees(terrain, wx, wz, cell, randi_range(0, 2))
			if randf() < 0.15:
				_place_bush(terrain, wx, wz, cell)
		"o":
			if randf() < 0.4:
				_place_bush(terrain, wx, wz, cell)
		_:
			pass

func _place_trees(terrain: Node3D, cx: float, cz: float, cell: float, n: int) -> void:
	for i in n:
		var ox := randf_range(-cell * 0.4, cell * 0.4)
		var oz := randf_range(-cell * 0.4, cell * 0.4)
		var wx := cx + ox
		var wz := cz + oz
		if _too_close_to_npc(wx, wz):
			continue
		var ground := (terrain as Node).call("sample_height", wx, wz) as float
		var tree_scale := randf_range(0.85, 1.25)
		var body := _make_tree_body(tree_scale, ground)
		add_child(body)
		body.global_position = Vector3(wx, ground, wz)
		body.rotation.y = randf() * TAU

# Wrap the visual GLB instance under a StaticBody3D + CollisionShape3D
# so the player and the NPC FSMs can't walk through trunks.
func _make_tree_body(scale_mul: float, _ground: float) -> StaticBody3D:
	var scene: PackedScene = tree_scenes[randi() % tree_scenes.size()]
	var inst: Node3D = scene.instantiate()
	inst.scale = Vector3(scale_mul, scale_mul, scale_mul)
	var body := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var cyl := CylinderShape3D.new()
	cyl.radius = tree_trunk_radius * scale_mul
	cyl.height = tree_trunk_height * scale_mul
	shape.shape = cyl
	shape.position = Vector3(0, tree_trunk_height * scale_mul * 0.5, 0)
	body.add_child(shape)
	body.add_child(inst)
	return body

func _place_bush(terrain: Node3D, cx: float, cz: float, cell: float) -> void:
	var ox := randf_range(-cell * 0.35, cell * 0.35)
	var oz := randf_range(-cell * 0.35, cell * 0.35)
	var wx := cx + ox
	var wz := cz + oz
	if _too_close_to_npc(wx, wz):
		return
	var scene: PackedScene = bush_scenes[randi() % bush_scenes.size()]
	var inst: Node3D = scene.instantiate()
	# Wrap in a small StaticBody so the player and NPCs can push bushes
	# aside instead of walking through them. Cylinder fits the foliage
	# silhouette better than a box.
	var body := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var cyl := CylinderShape3D.new()
	cyl.radius = 0.35
	cyl.height = 0.7
	shape.shape = cyl
	shape.position = Vector3(0, 0.35, 0)
	body.add_child(shape)
	body.add_child(inst)
	add_child(body)
	var ground := (terrain as Node).call("sample_height", wx, wz) as float
	body.global_position = Vector3(wx, ground, wz)
	body.rotation.y = randf() * TAU

func _too_close_to_npc(wx: float, wz: float) -> bool:
	var r2 := npc_clear_radius * npc_clear_radius
	for p: Vector2 in npc_avoid:
		var dx := p.x - wx
		var dz := p.y - wz
		if dx * dx + dz * dz < r2:
			return true
	for r: Rect2 in avoid_rects:
		if r.has_point(Vector2(wx, wz)):
			return true
	return false
