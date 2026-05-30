extends Node3D

# Data-driven dungeon builder. world.gd loads dungeons.json and calls
# build(spec, terrain) once per dungeon entry. The spec is a dict
# matching the dungeons.json schema (rooms, doorways, torches, props,
# wall_decor). 4m tile grid, KayKit Dungeon Remastered GLTFs.

const ASSET_DIR := "res://assets/dungeon/"
const TILE := 4.0
const WALL_DEPTH := 1.0
const DOOR_HEIGHT := 3.5
const DOOR_WIDTH := 1.2

enum Side { N, E, S, W }
const SIDE_FROM_STR := {"n": Side.N, "e": Side.E, "s": Side.S, "w": Side.W}

var dungeon_id: String = ""
var origin: Vector3 = Vector3.ZERO
var floor_mask: Array = []
var grid_w: int = 0
var grid_h: int = 0
var doorway_overrides: Dictionary = {}     # "col,row,side" -> true
var doors: Array = []                       # door StaticBody3D refs for E-toggle

signal trap_triggered    # world.gd connects -> red damage vignette

var wall_scene: PackedScene
var wall_door_scene: PackedScene
var floor_scene: PackedScene
var torch_scene: PackedScene

func build(spec: Dictionary, terrain: Node3D) -> Array:
	dungeon_id = spec.get("id", "dungeon")
	var ox: float = float(spec["origin"][0])
	var oz: float = float(spec["origin"][2])

	# Footprint bounds: walk all rooms to learn grid_w / grid_h
	var rooms: Array = spec.get("rooms", [])
	for r: Dictionary in rooms:
		var rc: int = int(r["col"])
		var rr: int = int(r["row"])
		var rw: int = int(r["w"])
		var rh: int = int(r["h"])
		grid_w = maxi(grid_w, rc + rw)
		grid_h = maxi(grid_h, rr + rh)
	# Add a 1-tile border so wall tiles fit
	grid_w += 1
	grid_h += 1

	# Sample terrain at 9 points across the footprint, take max-0.4
	# so the floor sinks into hills instead of floating.
	var foot_w := grid_w * TILE
	var foot_h := grid_h * TILE
	var max_h := -1e9
	for sx in [0.1, 0.5, 0.9]:
		for sz in [0.1, 0.5, 0.9]:
			var th := (terrain as Node).call("sample_height", ox + sx * foot_w, oz + sz * foot_h) as float
			if th > max_h:
				max_h = th
	var oy := max_h - 0.3
	origin = Vector3(ox, oy, oz)

	# Init floor mask
	floor_mask.resize(grid_h)
	for r in grid_h:
		var row: Array = []
		row.resize(grid_w)
		row.fill(false)
		floor_mask[r] = row
	for r: Dictionary in rooms:
		_mark_room(int(r["col"]), int(r["row"]), int(r["w"]), int(r["h"]))

	# Doorways
	for dw: Dictionary in spec.get("doorways", []):
		var side := SIDE_FROM_STR.get(dw.get("side", "n"), Side.N) as int
		_set_doorway(int(dw["col"]), int(dw["row"]), side)

	# Load shared scenes
	wall_scene      = load(ASSET_DIR + "wall.gltf")
	wall_door_scene = load(ASSET_DIR + "wall_doorway.gltf")
	floor_scene     = load(ASSET_DIR + "floor_dirt_large.gltf")
	torch_scene     = load(ASSET_DIR + "torch_mounted.gltf")
	if wall_scene == null or floor_scene == null:
		push_warning("[dungeon %s] dungeon GLTFs missing - reopen project once" % dungeon_id)
		return []

	_build_floors()
	_build_walls()
	_place_torches(spec.get("torches", []))
	_place_props(spec.get("props", []))
	_place_wall_decor(spec.get("wall_decor", []))
	_place_traps(spec.get("traps", []))

	# Return (footprint_rect, dungeon_y) so world.gd can carve the
	# terrain and exclude trees.
	return [Rect2(ox, oz, foot_w, foot_h), oy]

func _mark_room(c: int, r: int, w: int, h: int) -> void:
	for rr in range(r, r + h):
		for cc in range(c, c + w):
			if rr >= 0 and rr < grid_h and cc >= 0 and cc < grid_w:
				floor_mask[rr][cc] = true

func _set_doorway(c: int, r: int, side: int) -> void:
	doorway_overrides["%d,%d,%d" % [c, r, side]] = true

func _build_floors() -> void:
	for r in grid_h:
		for c in grid_w:
			if not floor_mask[r][c]:
				continue
			var inst: Node3D = floor_scene.instantiate()
			add_child(inst)
			inst.global_position = origin + Vector3(c * TILE, 0, r * TILE)

func _build_walls() -> void:
	for r in grid_h:
		for c in grid_w:
			if not floor_mask[r][c]:
				continue
			_maybe_wall(c, r, Side.N, 0, -1)
			_maybe_wall(c, r, Side.S, 0,  1)
			_maybe_wall(c, r, Side.E, 1,  0)
			_maybe_wall(c, r, Side.W, -1, 0)

func _maybe_wall(col: int, row: int, side: int, dx: int, dr: int) -> void:
	var nc := col + dx
	var nr := row + dr
	if nr >= 0 and nr < grid_h and nc >= 0 and nc < grid_w and floor_mask[nr][nc]:
		return
	var pos := origin + Vector3(col * TILE, 0, row * TILE)
	var yaw := 0.0
	match side:
		Side.N: pos += Vector3(0, 0, -TILE * 0.5); yaw = 0.0
		Side.S: pos += Vector3(0, 0,  TILE * 0.5); yaw = PI
		Side.E: pos += Vector3( TILE * 0.5, 0, 0); yaw = -PI * 0.5
		Side.W: pos += Vector3(-TILE * 0.5, 0, 0); yaw =  PI * 0.5

	var is_door: bool = doorway_overrides.has("%d,%d,%d" % [col, row, side])
	var scene: PackedScene = wall_door_scene if is_door else wall_scene
	var body := StaticBody3D.new()
	var visual: Node3D = scene.instantiate()
	body.add_child(visual)
	if is_door:
		# Two narrow flanking colliders + a thin centre collider that
		# represents the actual door panel; toggle the centre off when
		# the player opens it via E.
		_add_collider(body, Vector3(-TILE * 0.4, 2.0, 0), Vector3(TILE * 0.2, 4.0, WALL_DEPTH))
		_add_collider(body, Vector3( TILE * 0.4, 2.0, 0), Vector3(TILE * 0.2, 4.0, WALL_DEPTH))
		var door_collider := _add_collider(body, Vector3(0, 1.75, 0), Vector3(DOOR_WIDTH, DOOR_HEIGHT, 0.2))
		# Find the GLB's animated door subnode for the open/close swing.
		var door_visual := _find_node_containing(visual, "doorway_door")
		body.set_meta("is_door", true)
		body.set_meta("open", false)
		body.set_meta("door_visual", door_visual)
		body.set_meta("door_collider", door_collider)
		doors.append(body)
	else:
		_add_collider(body, Vector3(0, 2.0, 0), Vector3(TILE, 4.0, WALL_DEPTH))
	add_child(body)
	body.global_position = pos
	body.rotation.y = yaw

func _add_collider(body: StaticBody3D, offset: Vector3, size: Vector3) -> CollisionShape3D:
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	shape.position = offset
	body.add_child(shape)
	return shape

func _place_torches(spots: Array) -> void:
	for spot: Dictionary in spots:
		var col := int(spot["col"])
		var row := int(spot["row"])
		var side: int = SIDE_FROM_STR.get(spot.get("side", "n"), Side.N)
		if col < 0 or col >= grid_w or row < 0 or row >= grid_h:
			continue
		if not floor_mask[row][col]:
			continue
		var pos := origin + Vector3(col * TILE, 1.2, row * TILE)
		var yaw := 0.0
		match side:
			Side.N: pos += Vector3(0, 0, -TILE * 0.45); yaw = 0.0
			Side.S: pos += Vector3(0, 0,  TILE * 0.45); yaw = PI
			Side.E: pos += Vector3( TILE * 0.45, 0, 0); yaw = -PI * 0.5
			Side.W: pos += Vector3(-TILE * 0.45, 0, 0); yaw =  PI * 0.5

		var torch_root := Node3D.new()
		add_child(torch_root)
		torch_root.global_position = pos
		torch_root.rotation.y = yaw
		torch_root.add_child(torch_scene.instantiate())
		var flame := _make_flame_particles()
		torch_root.add_child(flame)
		flame.position = Vector3(0, 0.7, 0.05)
		var light := OmniLight3D.new()
		light.light_color = Color(1.0, 0.55, 0.25)
		light.light_energy = 2.0
		light.omni_range = 9.0
		light.shadow_enabled = false
		torch_root.add_child(light)
		light.position = Vector3(0, 0.8, 0.1)
		_start_flicker(light)

func _place_props(props: Array) -> void:
	for p: Dictionary in props:
		var path := ASSET_DIR + str(p["model"]) + ".gltf"
		var scene := load(path) as PackedScene
		if scene == null:
			push_warning("[dungeon] prop %s not staged" % p["model"])
			continue
		# Wrap the visual GLB in a StaticBody3D with a BoxShape3D sized
		# from the mesh's AABB so the player + NPCs collide with it.
		var inst: Node3D = scene.instantiate()
		var body := StaticBody3D.new()
		body.add_child(inst)
		var mi := _find_mesh_instance(inst)
		if mi != null:
			var aabb := mi.get_aabb()
			var shape := CollisionShape3D.new()
			var box := BoxShape3D.new()
			box.size = aabb.size
			shape.shape = box
			shape.position = aabb.position + aabb.size * 0.5
			body.add_child(shape)
		add_child(body)
		var col := int(p["col"])
		var row := int(p["row"])
		var pos := origin + Vector3(col * TILE, 0, row * TILE)
		if p.has("offset"):
			var off: Array = p["offset"]
			pos += Vector3(float(off[0]), float(off[1]), float(off[2]))
		body.global_position = pos
		body.rotation.y = deg_to_rad(float(p.get("yaw", 0.0)))

func _place_traps(traps: Array) -> void:
	var spike_scene := load(ASSET_DIR + "floor_tile_big_spikes.gltf") as PackedScene
	if spike_scene == null:
		return
	for t: Dictionary in traps:
		var col := int(t["col"])
		var row := int(t["row"])
		var root := Node3D.new()
		add_child(root)
		root.global_position = origin + Vector3(col * TILE, 0, row * TILE)
		var inst: Node3D = spike_scene.instantiate()
		root.add_child(inst)
		var spikes := _find_node_containing(inst, "spikes")
		# Hide spikes below the floor by default.
		if spikes != null:
			spikes.position.y = -2.2
		var area := Area3D.new()
		var sh := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(TILE * 0.8, 0.6, TILE * 0.8)
		sh.shape = box
		sh.position = Vector3(0, 0.3, 0)
		area.add_child(sh)
		root.add_child(area)
		root.set_meta("triggered", false)
		root.set_meta("spikes", spikes)
		area.body_entered.connect(_on_trap_entered.bind(root))

func _on_trap_entered(body: Node, trap_root: Node3D) -> void:
	if not (body is CharacterBody3D):
		return
	if trap_root.get_meta("triggered", false):
		return
	trap_root.set_meta("triggered", true)
	var spikes := trap_root.get_meta("spikes") as Node3D
	emit_signal("trap_triggered")
	if spikes == null:
		# Reset cooldown anyway after a delay so a missing model
		# doesn't permanently disable the trap.
		var t2 := create_tween()
		t2.tween_interval(1.5)
		t2.tween_callback(func(): trap_root.set_meta("triggered", false))
		return
	var tween := create_tween()
	tween.tween_property(spikes, "position:y", 0.0, 0.14).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	tween.tween_interval(1.6)
	tween.tween_property(spikes, "position:y", -2.2, 0.45).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_IN)
	tween.tween_callback(func(): trap_root.set_meta("triggered", false))

func _find_mesh_instance(n: Node) -> MeshInstance3D:
	if n is MeshInstance3D:
		return n
	for c in n.get_children():
		var r := _find_mesh_instance(c)
		if r != null:
			return r
	return null

func _place_wall_decor(decor: Array) -> void:
	for d: Dictionary in decor:
		var path := ASSET_DIR + str(d["model"]) + ".gltf"
		var scene := load(path) as PackedScene
		if scene == null:
			continue
		var col := int(d["col"])
		var row := int(d["row"])
		var side: int = SIDE_FROM_STR.get(d.get("side", "n"), Side.N)
		# y=0 anchors at floor; the KayKit banner mesh already extends
		# upward 0.5 - 3.7 from its origin, so this hangs the banner
		# naturally on the wall without sticking out the top.
		var pos := origin + Vector3(col * TILE, 0.0, row * TILE)
		var yaw := 0.0
		match side:
			Side.N: pos += Vector3(0, 0, -TILE * 0.48); yaw = 0.0
			Side.S: pos += Vector3(0, 0,  TILE * 0.48); yaw = PI
			Side.E: pos += Vector3( TILE * 0.48, 0, 0); yaw = -PI * 0.5
			Side.W: pos += Vector3(-TILE * 0.48, 0, 0); yaw =  PI * 0.5
		var inst: Node3D = scene.instantiate()
		add_child(inst)
		inst.global_position = pos
		inst.rotation.y = yaw

# Public API for world.gd: nearest closed door within reach + toggle.
func door_at(world_pos: Vector3, radius: float) -> StaticBody3D:
	var r2 := radius * radius
	var best: StaticBody3D = null
	var best_d2 := r2
	for d: StaticBody3D in doors:
		var dx := d.global_position.x - world_pos.x
		var dz := d.global_position.z - world_pos.z
		var d2 := dx * dx + dz * dz
		if d2 < best_d2:
			best_d2 = d2
			best = d
	return best

func toggle_door(door: StaticBody3D) -> void:
	var is_open: bool = door.get_meta("open")
	is_open = not is_open
	door.set_meta("open", is_open)
	var visual := door.get_meta("door_visual") as Node3D
	var collider := door.get_meta("door_collider") as CollisionShape3D
	if collider != null:
		collider.disabled = is_open
	if visual != null:
		# No real hinge axis - tween the visual yaw 90deg so the panel
		# swings sideways. Pivot is the panel's own origin, which sits
		# inside the doorway - not a perfect hinge but reads cleanly
		# enough at gameplay distance.
		var target := -PI * 0.5 if is_open else 0.0
		var tween := create_tween()
		tween.tween_property(visual, "rotation:y", target, 0.35).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)

func _find_node_containing(n: Node, needle: String) -> Node3D:
	if n is Node3D and (n.name as String).to_lower().contains(needle):
		return n
	for c in n.get_children():
		var r := _find_node_containing(c, needle)
		if r != null:
			return r
	return null

func _make_flame_particles() -> GPUParticles3D:
	var p := GPUParticles3D.new()
	var mat := ParticleProcessMaterial.new()
	mat.direction = Vector3(0, 1, 0)
	mat.spread = 18.0
	mat.initial_velocity_min = 0.9
	mat.initial_velocity_max = 1.4
	mat.gravity = Vector3(0, -0.4, 0)
	mat.scale_min = 0.18
	mat.scale_max = 0.32
	mat.color = Color(1.0, 0.7, 0.25, 1.0)
	var grad := Gradient.new()
	grad.add_point(0.0, Color(1.0, 0.95, 0.45, 1.0))
	grad.add_point(0.5, Color(1.0, 0.45, 0.15, 0.85))
	grad.add_point(1.0, Color(0.55, 0.10, 0.0, 0.0))
	var grad_tex := GradientTexture1D.new()
	grad_tex.gradient = grad
	mat.color_ramp = grad_tex
	p.process_material = mat
	p.amount = 24
	p.lifetime = 0.55
	p.preprocess = 1.0
	var quad := QuadMesh.new()
	quad.size = Vector2(0.28, 0.28)
	var quad_mat := StandardMaterial3D.new()
	quad_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	quad_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	quad_mat.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	quad_mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	quad_mat.albedo_color = Color(1, 0.7, 0.3, 1)
	quad.material = quad_mat
	p.draw_pass_1 = quad
	return p

func _start_flicker(light: OmniLight3D) -> void:
	var tween := create_tween().set_loops()
	tween.tween_property(light, "light_energy", 2.4, 0.18)
	tween.tween_property(light, "light_energy", 1.6, 0.13)
	tween.tween_property(light, "light_energy", 2.1, 0.22)
	tween.tween_property(light, "light_energy", 1.8, 0.16)
