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

@onready var name_label: Label = $Panel/VBox/HeaderRow/NameLabel
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
	$Panel/VBox/HeaderRow/CloseButton.pressed.connect(close)

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
		_drain_act_events()
		_set_status("")
		awaiting_reply = false
		input_line.editable = true
		send_button.disabled = false
		input_line.grab_focus()
		_refresh_quest_row()
		return
	# Safety net: if no reply has landed in 20 seconds, assume the
	# brain-side call threw silently (eval_expr returned null and
	# g_task is still 0) and recover the UI so the player isn't
	# stuck at "thinking".
	if Time.get_ticks_msec() - _await_start_ms > 20000:
		var g_task_v: Variant = vm.eval_expr('g_task')
		if str(g_task_v) == "0":
			_append_system_line(_system_line("no_reply", {}))
			_set_status("")
			awaiting_reply = false
			input_line.editable = true
			send_button.disabled = false
			input_line.grab_focus()

var _await_start_ms: int = 0

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
	_await_start_ms = Time.get_ticks_msec()
	_set_status("...thinking")
	# jdBasic's inline-string lexer chokes on non-ASCII bytes inside
	# eval_expr literals. Map common German umlauts to ASCII before
	# the call; replace anything else non-ASCII with '?'.
	# Double quotes inside the string literal also confuse the parser
	# even with \" escapes, so we swap them for single quotes - loses
	# nothing semantically for the LLM and avoids a parser crash.
	var safe := _ascii_safe(text)
	var sanitized := safe.replace("\\", "\\\\").replace("\"", "'")
	vm.eval_expr('continue_dialog("%s", "%s")' % [npc_id, sanitized])

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
				if item_name == "":
					continue
				# Validate against the NPC's whitelist. The brain
				# returns an Array of allowed item names; anything
				# else is treated as model hallucination and dropped.
				var allowed: Variant = vm.eval_expr('available_items("%s")' % npc_id)
				var ok := false
				if allowed is Array:
					for a in allowed:
						if str(a) == item_name:
							ok = true
							break
				if not ok:
					print("[%s] dropped hallucinated give_item: %s (allowed=%s)" % [npc_name, item_name, allowed])
					continue
				var esc := _ascii_safe(item_name).replace("\"", "\\\"")
				var src := _ascii_safe(npc_name).replace("\"", "\\\"")
				vm.eval_expr('add_inventory_item("%s", "%s")' % [esc, src])
				_append_system_line(_system_line("received_item", {"name": item_name}))
				print("[%s gave] %s" % [npc_name, item_name])
			"accept_item":
				var item_name: String = str(args.get("name", ""))
				if item_name != "":
					var esc := _ascii_safe(item_name).replace("\"", "\\\"")
					vm.eval_expr('remove_player_inventory("%s")' % esc)
					_append_system_line(_system_line("gave_item", {"name": item_name}))
					print("[%s took] %s" % [npc_name, item_name])
			"offer_quest":
				vm.eval_expr('accept_quest("%s")' % npc_id)
				_append_system_line(_system_line("quest_accepted", {"npc": npc_name}))
				print("[QUEST offered+accepted] from %s" % npc_name)
			"complete_quest":
				var qid: String = str(args.get("id", ""))
				if qid != "":
					var esc := _ascii_safe(qid).replace("\"", "\\\"")
					vm.eval_expr('complete_quest_by_id("%s")' % esc)
					_append_system_line(_system_line("quest_completed", {"id": qid}))
					print("[QUEST completed] %s by %s" % [qid, npc_name])
			"set_flag":
				var fkey: String = str(args.get("key", ""))
				var fval: Variant = args.get("value", true)
				# LLM frequently emits stray set_flag with value:false or
				# with a hallucinated key. We only accept true transitions
				# on keys that actually exist in world_state.flags.
				var fval_str := str(fval).to_lower()
				if fkey == "":
					continue
				if fval_str != "true" and fval_str != "1":
					print("[world flag] dropped non-true: %s -> %s" % [fkey, fval])
					continue
				var fkey_esc := _ascii_safe(fkey).replace("\"", "'")
				var res: Variant = vm.eval_expr('set_world_flag("%s", true)' % fkey_esc)
				if str(res) == "set":
					print("[world flag] %s -> true" % fkey)
					_append_system_line(_system_line("world_shifts", {"key": fkey}))
				else:
					print("[world flag] dropped unknown key: %s" % fkey)
			"shift_affinity":
				var delta_raw: Variant = args.get("delta", 0)
				var delta: int = int(delta_raw)
				if delta == 0:
					continue
				if delta < -10:
					delta = -10
				elif delta > 10:
					delta = 10
				var new_score: Variant = vm.eval_expr('shift_npc_affinity("%s", %d)' % [npc_id, delta])
				if new_score is float or new_score is int:
					var score_int: int = int(new_score)
					if score_int == -9999:
						print("[affinity] unknown npc_id: %s" % npc_id)
						continue
					var key := "affinity_up" if delta > 0 else "affinity_down"
					var sign_str := ("+" + str(delta)) if delta > 0 else str(delta)
					var line := _system_line(key, {
						"npc": npc_name,
						"delta": sign_str,
						"score": str(score_int),
					})
					_append_system_line(line)
					print("[affinity] %s %s -> %d" % [npc_name, sign_str, score_int])

# Story-arc state changes accumulate in brain-side g_act_events. We
# drain after each dispatched reply so the player sees "A new chapter
# begins" / "Chapter complete" inline in the dialog history.
func _drain_act_events() -> void:
	var events: Variant = vm.eval_expr('drain_act_events()')
	if not (events is Array):
		return
	for e_v in events:
		if not (e_v is Dictionary):
			continue
		var e: Dictionary = e_v
		var kind: String = str(e.get("kind", ""))
		var title: String = str(e.get("title", ""))
		var line_key := "act_started" if kind == "act_started" else "act_completed"
		var line := _system_line(line_key, {"title": title})
		var col := "[color=#d8c280][b]"
		history_text.append_text("%s%s[/b][/color]\n\n" % [col, line])
		print("[story] %s: %s" % [kind, title])

func _append_system_line(line: String) -> void:
	history_text.append_text("[color=#bbe0a0][i]%s[/i][/color]\n\n" % line)

# Render a one-line message from dialog_rules.system_lines via the
# brain-side rule() helper. Keeps all user-visible text out of the
# code so it can be tweaked / translated in JSON.
func _system_line(key: String, vars: Dictionary) -> String:
	var setup := "DIM __rv AS MAP = {}\n"
	for k in vars:
		var v := _ascii_safe(str(vars[k])).replace("\\", "\\\\").replace("\"", "'")
		setup += "__rv{\"%s\"} = \"%s\"\n" % [k, v]
	var captured := vm.eval(setup + "PRINT rule(\"system_lines\", \"%s\", __rv)\n" % key)
	return captured.strip_edges()

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
