extends StaticBody3D

# Interactive chest. KayKit's chest GLB has a `chest_lid` subnode we
# can rotate independently for the "open" animation. Once opened the
# contents go straight into the player inventory (via accept_loot()
# on world.gd) and the chest stays open + non-interactable.

@export var chest_id: String = ""
@export var label: String = ""
@export var contents: Array = []   # [{item, name}, ...]
@export var open_angle: float = -1.6   # ~-92deg, swings lid back
@export var open_duration: float = 0.45

var opened: bool = false
var lid: Node3D = null
var loot_callback: Callable

func setup(model_path: String, cb: Callable) -> void:
	loot_callback = cb
	var scene := load(model_path) as PackedScene
	if scene == null:
		push_warning("[chest] %s missing" % model_path)
		return
	var inst: Node3D = scene.instantiate()
	add_child(inst)
	# KayKit names the lid "chest_lid", "chest_gold_lid", etc. -
	# match anything that contains "lid".
	lid = _find_node_containing(inst, "lid")
	if lid == null:
		push_warning("[chest %s] no *lid* node in %s" % [chest_id, model_path])
	# Collision: a flat box matching the chest footprint.
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(0.9, 0.7, 0.7)
	shape.shape = box
	shape.position = Vector3(0, 0.35, 0)
	add_child(shape)

func try_open() -> bool:
	if opened or lid == null:
		return false
	opened = true
	var tween := create_tween()
	tween.tween_property(lid, "rotation:x", open_angle, open_duration).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	if loot_callback.is_valid():
		loot_callback.call(contents, label)
	_spawn_loot_visuals()
	return true

# Spawn each contents item's GLB above the chest and tween it up +
# spin, then shrink-and-free, so the player sees what came out
# instead of relying on the chest mesh alone (chest_gold has built-in
# gold that's just decoration, chest is empty inside, etc.).
func _spawn_loot_visuals() -> void:
	for i in contents.size():
		var entry: Dictionary = contents[i]
		var item_id: String = entry.get("item", "")
		if item_id == "":
			continue
		var path := "res://assets/dungeon/%s.gltf" % item_id
		var scene := load(path) as PackedScene
		if scene == null:
			continue
		var inst: Node3D = scene.instantiate()
		add_child(inst)
		inst.position = Vector3(0, 0.5, 0)
		var target := Vector3(randf_range(-0.35, 0.35), 1.5 + i * 0.25, randf_range(-0.35, 0.35))
		var float_tween := create_tween().set_parallel(true)
		float_tween.tween_property(inst, "position", target, 0.7).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
		float_tween.tween_property(inst, "rotation:y", TAU, 0.7)
		# Chained shrink after the float so the item feels picked up.
		var shrink := create_tween()
		shrink.tween_interval(0.6)
		shrink.tween_property(inst, "scale", Vector3.ZERO, 0.25)
		shrink.tween_callback(inst.queue_free)

func _find_node_containing(n: Node, needle: String) -> Node3D:
	if n is Node3D and (n.name as String).to_lower().contains(needle):
		return n
	for c in n.get_children():
		var r := _find_node_containing(c, needle)
		if r != null:
			return r
	return null
