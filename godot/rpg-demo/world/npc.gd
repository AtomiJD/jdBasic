extends CharacterBody3D

# NPC body - the brain lives in npc_brain.jdb under the world-level
# JDBasicVM. This script just loads the right character GLB into the
# Visual slot and exposes id / character_type to the world ticker.

@export var npc_id: String = ""
@export var character_type: String = "Knight"
@export var display_name: String = ""

@onready var visual: Node3D = $Visual
@onready var name_label: Label3D = $NameLabel

func _ready() -> void:
	_load_character()
	if display_name != "":
		name_label.text = display_name

func _load_character() -> void:
	var glb := "res://assets/characters/%s.glb" % character_type
	var scene := load(glb) as PackedScene
	if scene == null:
		push_warning("NPC %s: %s didn't import" % [npc_id, glb])
		return
	visual.add_child(scene.instantiate())
