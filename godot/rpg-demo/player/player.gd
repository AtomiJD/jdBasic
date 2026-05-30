extends CharacterBody3D

# Third-person player. WASD walks, space jumps, mouse orbits the camera
# around the body. Escape toggles mouse capture so the editor stays
# usable when we run from F5.

const PLAYER_GLB := "res://assets/characters/Rogue_Hooded.glb"

@export var walk_speed: float = 5.0
@export var run_speed: float = 9.0
@export var jump_velocity: float = 6.5
@export var mouse_sensitivity: float = 0.0025
@export var pitch_min: float = -1.2
@export var pitch_max: float = 0.4

@onready var visual: Node3D = $Visual
@onready var yaw_pivot: Node3D = $YawPivot
@onready var pitch_pivot: Node3D = $YawPivot/PitchPivot
@onready var spring_arm: SpringArm3D = $YawPivot/PitchPivot/SpringArm3D

var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)
var mouse_captured: bool = false
var character_yaw: float = 0.0

func _ready() -> void:
	_load_character()
	_capture_mouse(true)

func _load_character() -> void:
	var scene := load(PLAYER_GLB) as PackedScene
	if scene == null:
		push_warning("Player GLB didn't import yet - reopen project once")
		return
	var inst: Node3D = scene.instantiate()
	visual.add_child(inst)

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("mouse_capture_toggle"):
		_capture_mouse(not mouse_captured)
	elif event is InputEventMouseMotion and mouse_captured:
		yaw_pivot.rotate_y(-event.relative.x * mouse_sensitivity)
		pitch_pivot.rotate_x(-event.relative.y * mouse_sensitivity)
		pitch_pivot.rotation.x = clamp(pitch_pivot.rotation.x, pitch_min, pitch_max)

func _physics_process(delta: float) -> void:
	# Gravity
	if not is_on_floor():
		velocity.y -= gravity * delta

	# Jump
	if Input.is_action_just_pressed("jump") and is_on_floor():
		velocity.y = jump_velocity

	# Movement is in the yaw-pivot's local frame so WASD follows camera.
	var input_vec := Vector2(
		Input.get_axis("move_left", "move_right"),
		Input.get_axis("move_forward", "move_back")
	)
	var cam_basis := yaw_pivot.global_transform.basis
	var dir := (cam_basis.x * input_vec.x + cam_basis.z * input_vec.y).normalized()
	dir.y = 0.0
	var speed := run_speed if Input.is_key_pressed(KEY_SHIFT) else walk_speed

	velocity.x = dir.x * speed
	velocity.z = dir.z * speed

	# Rotate the visual mesh to face the movement direction so the
	# Rogue model doesn't moonwalk. Skip when not moving so the
	# character keeps its previous heading.
	if dir.length_squared() > 0.01:
		character_yaw = atan2(dir.x, dir.z)
		visual.rotation.y = character_yaw

	move_and_slide()

func _capture_mouse(capture: bool) -> void:
	mouse_captured = capture
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if capture else Input.MOUSE_MODE_VISIBLE
