extends CanvasLayer

# Dialog UI - opens when the player presses E near an NPC. Reads the
# cached greeting via start_dialog(id), then ferries player messages
# into continue_dialog(id, text) and polls poll_reply(id) every frame
# until the worker lands the next NPC line.
#
# Mouse cursor is released and game input ignored while open. The
# panel itself uses pause_mode = WHEN_PAUSED so we can pause the game
# without freezing the dialog.

signal closed

@onready var name_label: Label = $Panel/VBox/NameLabel
@onready var history_text: RichTextLabel = $Panel/VBox/History
@onready var input_line: LineEdit = $Panel/VBox/InputRow/Input
@onready var send_button: Button = $Panel/VBox/InputRow/Send
@onready var status_label: Label = $Panel/VBox/StatusLabel
@onready var quest_row: HBoxContainer = $Panel/VBox/QuestRow
@onready var quest_text: Label = $Panel/VBox/QuestRow/QuestText
@onready var accept_button: Button = $Panel/VBox/QuestRow/AcceptButton

var action_re: RegEx

var vm: JDBasicVM
var npc_id: String = ""
var npc_name: String = ""
var npc_color: Color = Color(0.85, 0.92, 1)
var prev_mouse_mode: int = Input.MOUSE_MODE_CAPTURED
var awaiting_reply: bool = false

func open(p_vm: JDBasicVM, p_npc_id: String, p_npc_name: String, p_color: Color) -> void:
	vm = p_vm
	npc_id = p_npc_id
	npc_name = p_npc_name
	npc_color = p_color
	name_label.text = npc_name
	name_label.add_theme_color_override("font_color", npc_color)
	history_text.clear()
	prev_mouse_mode = Input.mouse_mode
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	get_tree().paused = true
	process_mode = Node.PROCESS_MODE_ALWAYS

	print("\n[dialog open] === %s ===" % npc_name)
	var greeting: Variant = vm.eval_expr('start_dialog("%s")' % npc_id)
	if greeting is String and greeting != "":
		_append_npc(greeting)
		print("[%s] %s" % [npc_name, greeting])
	else:
		_set_status("...not ready yet")
	_refresh_quest_row()
	input_line.grab_focus()

func _refresh_quest_row() -> void:
	var offerable: Variant = vm.eval_expr('has_offerable_quest("%s")' % npc_id)
	if offerable == true:
		var desc: Variant = vm.eval_expr('npcs{"%s"}{"quests"}[0]{"description"}' % npc_id)
		quest_text.text = "Quest: %s" % desc
		quest_row.visible = true
	else:
		quest_row.visible = false

func _on_accept() -> void:
	var res: Variant = vm.eval_expr('accept_quest("%s")' % npc_id)
	if res == "accepted":
		var desc: Variant = vm.eval_expr('player_state{"active"}[quest_count_active() - 1]{"description"}')
		history_text.append_text("[color=#e0d090][i]Quest accepted: %s[/i][/color]\n\n" % desc)
		print("[QUEST] Player accepted from %s: %s" % [npc_name, desc])
		_refresh_quest_row()

func close() -> void:
	print("[dialog close] === %s ===\n" % npc_name)
	get_tree().paused = false
	Input.mouse_mode = prev_mouse_mode
	closed.emit()
	queue_free()

func _ready() -> void:
	action_re = RegEx.new()
	action_re.compile("\\*([^*]+)\\*")
	send_button.pressed.connect(_on_send)
	input_line.text_submitted.connect(func(_t): _on_send())
	accept_button.pressed.connect(_on_accept)
	$Panel/CloseButton.pressed.connect(close)

# _input fires before GUI controls get a crack at it, so we close
# on the first Esc instead of having LineEdit eat it.
func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()

func _process(_delta: float) -> void:
	if not awaiting_reply or vm == null:
		return
	var reply: Variant = vm.eval_expr('poll_reply("%s")' % npc_id)
	if reply is String and reply != "":
		_append_npc(reply)
		print("[%s] %s" % [npc_name, reply])
		_set_status("")
		awaiting_reply = false
		input_line.editable = true
		send_button.disabled = false
		input_line.grab_focus()

func _on_send() -> void:
	if awaiting_reply or vm == null:
		return
	var text := input_line.text.strip_edges()
	if text == "":
		return
	_append_player(text)
	print("[Player] %s" % text)
	input_line.clear()
	input_line.editable = false
	send_button.disabled = true
	awaiting_reply = true
	_set_status("...thinking")
	var escaped := text.replace("\\", "\\\\").replace("\"", "\\\"")
	vm.eval_expr('continue_dialog("%s", "%s")' % [npc_id, escaped])

func _append_npc(line: String) -> void:
	# Phi-3 emits stage directions as *action text*. Convert to bold so
	# they read as action vs spoken text. Also colourise the NPC name
	# with the per-character color from npcs.json.
	var formatted := action_re.sub(line, "[b]$1[/b]", true)
	var hex := "#%02x%02x%02x" % [int(npc_color.r * 255), int(npc_color.g * 255), int(npc_color.b * 255)]
	history_text.append_text("[color=%s][b]%s:[/b][/color] %s\n\n" % [hex, npc_name, formatted])

func _append_player(line: String) -> void:
	history_text.append_text("[color=#9bb6ff][b]You:[/b] %s[/color]\n\n" % line)

func _set_status(text: String) -> void:
	status_label.text = text
