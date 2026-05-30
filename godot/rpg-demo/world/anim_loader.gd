extends RefCounted
class_name AnimLoader

# Shared loader for the KayKit Rig_Medium animation packs. The
# characters in assets/characters/*.glb come with a skeleton but no
# AnimationPlayer (KayKit ships animations in separate .glb files on
# the shared rig). We:
#
#   1. Load BOTH animation packs (MovementBasic + General) and merge
#      their clips into one AnimationLibrary.
#   2. attach_to() creates a fresh AnimationPlayer on the character
#      with root_node set so the bone-track paths resolve.
#   3. best_name() prefers prefix matches so "Idle" maps to "Idle_A"
#      and not "Jump_Idle".

const ANIM_GLBS := [
	"res://assets/animations/Rig_Medium_General.glb",
	"res://assets/animations/Rig_Medium_MovementBasic.glb",
]

static var _lib: AnimationLibrary = null
static var _names: PackedStringArray = PackedStringArray()

static func get_library() -> AnimationLibrary:
	if _lib != null:
		return _lib
	_lib = AnimationLibrary.new()
	for path in ANIM_GLBS:
		var scene := load(path) as PackedScene
		if scene == null:
			push_warning("[anim] %s not imported" % path)
			continue
		var inst: Node = scene.instantiate()
		var src_ap := _find_anim_player(inst)
		if src_ap == null:
			push_warning("[anim] no AnimationPlayer in %s" % path)
			inst.queue_free()
			continue
		for n in src_ap.get_animation_list():
			var clip := src_ap.get_animation(n)
			if clip != null and not _lib.has_animation(n):
				# KayKit's GLB doesn't set loop_mode, so by default each clip
				# plays once and the AnimationPlayer parks the rig in T-pose.
				# Force loop on the locomotion + idle clips; one-shots (death,
				# hit, pickup, throw, jump_*) stay non-loop.
				if _should_loop(n):
					clip.loop_mode = Animation.LOOP_LINEAR
				_lib.add_animation(n, clip)
				_names.append(n)
		inst.queue_free()
	print("[anim] merged library: %s clips" % _names.size())
	return _lib

static func attach_to(visual_root: Node3D) -> AnimationPlayer:
	var ap := _find_anim_player(visual_root)
	if ap == null:
		# KayKit characters ship a Skeleton3D but no AnimationPlayer.
		# Add our own and point root_node at the character so the
		# animation tracks (paths like Skeleton3D:Hips) resolve.
		ap = AnimationPlayer.new()
		ap.name = "AnimationPlayer"
		visual_root.add_child(ap)
		ap.root_node = ap.get_path_to(visual_root)
	var lib := get_library()
	if lib == null:
		push_warning("[anim] no library available")
		return ap
	if ap.has_animation_library(""):
		ap.remove_animation_library("")
	ap.add_animation_library("", lib)
	return ap

# Resolve a logical name ("Idle", "Walk", "Run") against the merged
# clip list. Prefer prefix matches so "Idle" -> "Idle_A" beats
# "Jump_Idle".
static func best_name(want: String) -> String:
	get_library()
	if _names.has(want):
		return want
	var lower := want.to_lower()
	# Pass 1: starts-with (case-insensitive)
	for n in _names:
		if n.to_lower().begins_with(lower):
			return n
	# Pass 2: word-boundary token match (split on "_")
	for n in _names:
		var tokens := n.to_lower().split("_")
		for t in tokens:
			if t == lower:
				return n
	# Pass 3: substring fallback
	for n in _names:
		if n.to_lower().contains(lower):
			return n
	return ""

static func _should_loop(name: String) -> bool:
	var lower := name.to_lower()
	return lower.begins_with("idle") \
		or lower.begins_with("walk") \
		or lower.begins_with("running") \
		or lower.begins_with("run")

static func _find_anim_player(n: Node) -> AnimationPlayer:
	if n is AnimationPlayer:
		return n
	for c in n.get_children():
		var r := _find_anim_player(c)
		if r != null:
			return r
	return null
