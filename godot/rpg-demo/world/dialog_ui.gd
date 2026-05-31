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
var prev_mouse_mode: Input.MouseMode = Input.MOUSE_MODE_CAPTURED
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
	var actions: Variant = vm.eval_expr('start_dialog("%s")' % npc_id)
	print("[diag] start_dialog returned: type=", typeof(actions), " value=", actions)
	if actions is Array and actions.size() > 0:
		_dispatch_actions(actions)
	else:
		_set_status("...not ready yet")
	_refresh_quest_row()
	input_line.grab_focus()

func _refresh_quest_row() -> void:
	var offerable: Variant = vm.eval_expr('has_offerable_quest("%s")' % npc_id)
	if offerable is bool and bool(offerable):
		var desc: Variant = vm.eval_expr('npcs{"%s"}{"quests"}[0]{"description"}' % npc_id)
		quest_text.text = "Quest: %s" % desc
		quest_row.visible = true
	else:
		quest_row.visible = false

func _on_accept() -> void:
	var res: Variant = vm.eval_expr('accept_quest("%s")' % npc_id)
	if res is String and str(res) == "accepted":
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
	# Stage directions come in two flavours: *like this* (Phi-3) and
	# |like this| (Qwen) - both get rendered bold.
	action_re.compile("\\*([^*]+)\\*|\\|([^|]+)\\|")
	send_button.pressed.connect(_on_send)
	input_line.text_submitted.connect(func(_t): _on_send())
	accept_button.pressed.connect(_on_accept)
	$Panel/CloseButton.pressed.connect(close)

# _input fires before GUI controls get a crack at it, so we close
# on the first Esc instead of having LineEdit eat it. Shift+Enter
# accepts the offered quest without leaving the input line.
func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_ESCAPE:
			close()
			get_viewport().set_input_as_handled()
		elif event.keycode == KEY_ENTER and event.shift_pressed and quest_row.visible:
			_on_accept()
			get_viewport().set_input_as_handled()

func _process(_delta: float) -> void:
	if not awaiting_reply or vm == null:
		return
	var actions: Variant = vm.eval_expr('poll_reply("%s")' % npc_id)
	if actions is Array and actions.size() > 0:
		print("[diag] poll_reply returned: ", actions)
		_dispatch_actions(actions)
		_set_status("")
		awaiting_reply = false
		input_line.editable = true
		send_button.disabled = false
		input_line.grab_focus()
		_refresh_quest_row()

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
	# jdBasic's inline-string lexer chokes on non-ASCII bytes inside
	# eval_expr literals. Map common German umlauts to ASCII before
	# the call; replace anything else non-ASCII with '?'.
	var safe := _ascii_safe(text)
	var escaped := safe.replace("\\", "\\\\").replace("\"", "\\\"")
	vm.eval_expr('continue_dialog("%s", "%s")' % [npc_id, escaped])

func _ascii_safe(s: String) -> String:
	var out := ""
	for i in s.length():
		var c := s[i]
		var code := c.unicode_at(0)
		if code < 128:
			out += c
			continue
		match c:
			"ä": out += "ae"
			"ö": out += "oe"
			"ü": out += "ue"
			"Ä": out += "Ae"
			"Ö": out += "Oe"
			"Ü": out += "Ue"
			"ß": out += "ss"
			"é", "è", "ê": out += "e"
			"á", "à", "â": out += "a"
			"í", "ì", "î": out += "i"
			"ó", "ò", "ô": out += "o"
			"ú", "ù", "û": out += "u"
			"ñ": out += "n"
			_:  out += "?"
	return out

# Walk the LLM's actions array and translate each tool call into UI
# + jdBasic side effects. "say" is the only one we directly render;
# the others either mutate game state via brain functions or emit a
# small system line in the history pane.
func _dispatch_actions(actions: Array) -> void:
	for action_v in actions:
		var action: Dictionary = action_v as Dictionary
		var tool: String = str(action.get("tool", ""))
		var args: Dictionary = action.get("args", {}) as Dictionary
		match tool:
			"say":
				var text: String = str(args.get("text", ""))
				if text != "":
					_append_npc(text)
					print("[%s] %s" % [npc_name, text])
			"give_item":
				var item_name: String = str(args.get("name", ""))
				if item_name != "":
					var esc := _ascii_safe(item_name).replace("\"", "\\\"")
					var src := _ascii_safe(npc_name).replace("\"", "\\\"")
					vm.eval_expr('add_inventory_item("%s", "%s")' % [esc, src])
					_append_system_line("+ %s" % item_name)
					print("[%s gave] %s" % [npc_name, item_name])
			"accept_item":
				var item_name: String = str(args.get("name", ""))
				if item_name != "":
					var esc := _ascii_safe(item_name).replace("\"", "\\\"")
					vm.eval_expr('remove_player_inventory("%s")' % esc)
					_append_system_line("- %s" % item_name)
					print("[%s took] %s" % [npc_name, item_name])
			"offer_quest":
				vm.eval_expr('accept_quest("%s")' % npc_id)
				_append_system_line("Quest accepted from %s" % npc_name)
				print("[QUEST offered+accepted] from %s" % npc_name)
			"complete_quest":
				var qid: String = str(args.get("id", ""))
				if qid != "":
					var esc := _ascii_safe(qid).replace("\"", "\\\"")
					vm.eval_expr('complete_quest_by_id("%s")' % esc)
					_append_system_line("Quest completed: %s" % qid)
					print("[QUEST completed] %s by %s" % [qid, npc_name])
			"set_flag":
				# World-state flags - placeholder for the next polish wave.
				pass
			"shift_affinity":
				# Affinity system - placeholder for the next polish wave.
				pass

func _append_system_line(line: String) -> void:
	history_text.append_text("[color=#bbe0a0][i]%s[/i][/color]\n\n" % line)

func _append_npc(line: String) -> void:
	# Stage directions come as *...* (Phi-3) or |...| (Qwen). Convert
	# either to bold so they read as action vs spoken text. Also
	# colourise the NPC name with the per-character color.
	var formatted := action_re.sub(line, "[b]$1$2[/b]", true)
	var hex := "#%02x%02x%02x" % [int(npc_color.r * 255), int(npc_color.g * 255), int(npc_color.b * 255)]
	history_text.append_text("[color=%s][b]%s:[/b][/color] %s\n\n" % [hex, npc_name, formatted])

func _append_player(line: String) -> void:
	history_text.append_text("[color=#9bb6ff][b]You:[/b] %s[/color]\n\n" % line)

func _set_status(text: String) -> void:
	status_label.text = text
