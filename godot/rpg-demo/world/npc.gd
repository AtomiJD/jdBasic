extends CharacterBody3D

# NPC body - the brain lives in npc_brain.jdb under the world-level
# JDBasicVM. This script just loads the right character GLB into the
# Visual slot and exposes id / character_type to the world ticker.

const AnimLoader := preload("res://world/anim_loader.gd")

@export var npc_id: String = ""
@export var character_type: String = "Knight"
@export var display_name: String = ""
@export var tint: Color = Color(1, 1, 1)

@onready var visual: Node3D = $Visual
@onready var name_label: Label3D = $NameLabel

var anim_player: AnimationPlayer = null
var anim_idle: String = ""
var anim_walk: String = ""
var current_anim: String = ""

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
	print("[npc %s] anims resolved: idle=%s walk=%s (ap=%s)"
		% [npc_id, anim_idle, anim_walk, anim_player])
	_play(anim_idle)

func set_moving(moving: bool) -> void:
	if moving:
		_play(anim_walk)
	else:
		_play(anim_idle)

func _play(name: String) -> void:
	if name == "" or anim_player == null or current_anim == name:
		return
	anim_player.play(name, 0.2)
	current_anim = name
