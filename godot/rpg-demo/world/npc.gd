extends CharacterBody3D

# NPC body - the brain lives in npc_brain.jdb under the world-level
# JDBasicVM. This script just loads the right character GLB into the
# Visual slot and exposes id / character_type to the world ticker.

@export var npc_id: String = ""
@export var character_type: String = "Knight"
@export var display_name: String = ""
@export var tint: Color = Color(1, 1, 1)
@export var look_radius: float = 8.0
@export var turn_speed: float = 5.5  # rad/sec when slerping toward target yaw

@onready var visual: Node3D = $Visual
@onready var name_label: Label3D = $NameLabel

var anim_player: AnimationPlayer = null
var anim_idle: String = ""
var anim_walk: String = ""
var current_anim: String = ""

# Nameplate state - combined into a single Label3D below the head so
# we don't fight a separate badge node for vertical real estate.
var has_quest: bool = true
var _ready_dialog: bool = false
var _busy: bool = false
var _offered: bool = false
var _completed: bool = false

func _ready() -> void:
	_load_character()
	if display_name != "":
		name_label.text = display_name
	name_label.modulate = tint

func _load_character() -> void:
	var glb := "res://assets/characters/%s.glb" % character_type
	var scene := load(glb) as PackedScene
	if scene == null:
		push_warning("NPC %s: %s didn't import" % [npc_id, glb])
		return
	var inst: Node3D = scene.instantiate()
	visual.add_child(inst)
	anim_player = AnimLoader.attach_to(inst)
	anim_idle = AnimLoader.best_name("Idle")
	anim_walk = AnimLoader.best_name("Walk")
	_play(anim_idle)

func set_moving(moving: bool) -> void:
	if moving:
		_play(anim_walk)
	else:
		_play(anim_idle)

# Nameplate update - composes Name + dialog-ready tick + quest tag
# in a single Label3D so they all stay close to the head.
func set_dialog_ready(is_ready: bool) -> void:
	_ready_dialog = is_ready
	_refresh_name_label()

func set_dialog_busy(busy: bool) -> void:
	_busy = busy
	_refresh_name_label()

func set_quest_state(offered: bool, completed: bool) -> void:
	_offered = offered
	_completed = completed
	_refresh_name_label()

func _refresh_name_label() -> void:
	var s := display_name
	if _ready_dialog:
		s += "  ✓"
	elif _busy:
		s += "  …"
	if has_quest and not _completed:
		s += "  (?)" if _offered else "  (!)"
	name_label.text = s

# Decide where the visual mesh should face this frame: while moving,
# follow the FSM's heading; while idle and the player is close, slerp
# toward the player so the NPC "notices" them.
func update_facing(fsm_yaw: float, moving: bool, player_pos: Vector3, dt: float) -> void:
	var dx := player_pos.x - global_position.x
	var dz := player_pos.z - global_position.z
	var dist_sq := dx * dx + dz * dz
	var target_yaw: float
	if not moving and dist_sq < look_radius * look_radius:
		target_yaw = atan2(dx, dz)
	else:
		target_yaw = fsm_yaw
	var step := clampf(turn_speed * dt, 0.0, 1.0)
	visual.rotation.y = lerp_angle(visual.rotation.y, target_yaw, step)

func _play(anim_name: String) -> void:
	if anim_name == "" or anim_player == null or current_anim == anim_name:
		return
	anim_player.play(anim_name, 0.2)
	current_anim = anim_name
