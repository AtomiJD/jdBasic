extends CanvasLayer

# Journal panel - tabbed UI for active quests, completed quests, and
# inventory. Q opens it on the Quests tab, I on the Inventory tab.
# All data comes from npc_brain.jdb's player_state map; we pull it
# via small eval_expr calls when the panel opens or refreshes.

signal closed

enum Tab { QUESTS = 0, INVENTORY = 1, REPUTATION = 2 }

@onready var tabs: TabContainer    = $Panel/VBox/Tabs
@onready var active_list: VBoxContainer    = $Panel/VBox/Tabs/Quests/Scroll/Inner/Active
@onready var completed_list: VBoxContainer = $Panel/VBox/Tabs/Quests/Scroll/Inner/Completed
@onready var inventory_list: VBoxContainer = $Panel/VBox/Tabs/Inventory/Scroll/Items
@onready var reputation_list: VBoxContainer = $Panel/VBox/Tabs/Reputation/Scroll/Items

var vm: JDBasicVM
var prev_mouse_mode: Input.MouseMode = Input.MOUSE_MODE_CAPTURED

func open(p_vm: JDBasicVM, p_initial_tab: int) -> void:
	vm = p_vm
	tabs.current_tab = p_initial_tab
	prev_mouse_mode = Input.mouse_mode
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	get_tree().paused = true
	process_mode = Node.PROCESS_MODE_ALWAYS
	_refresh()

func close() -> void:
	get_tree().paused = false
	Input.mouse_mode = prev_mouse_mode
	closed.emit()
	queue_free()

func _ready() -> void:
	$Panel/VBox/HeaderRow/CloseButton.pressed.connect(close)

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		match event.keycode:
			KEY_ESCAPE, KEY_Q, KEY_I:
				close()
				get_viewport().set_input_as_handled()

func _refresh() -> void:
	_clear(active_list)
	_clear(completed_list)
	_clear(inventory_list)
	_clear(reputation_list)

	var n_active: Variant = vm.eval_expr('quest_count_active()')
	var n_done: Variant   = vm.eval_expr('quest_count_completed()')
	var n_inv: Variant    = vm.eval_expr('inventory_count()')

	if int(n_active) == 0:
		active_list.add_child(_dim_label("No active quests."))
	for i in int(n_active):
		active_list.add_child(_active_quest_row(i))

	if int(n_done) == 0:
		completed_list.add_child(_dim_label("No completed quests."))
	for i in int(n_done):
		completed_list.add_child(_completed_quest_row(i))

	if int(n_inv) == 0:
		inventory_list.add_child(_dim_label("Inventory empty."))
	for i in int(n_inv):
		inventory_list.add_child(_inventory_row(i))

	var ids: Variant = vm.eval_expr('list_ids()')
	if ids is Array and ids.size() > 0:
		for id_v in ids:
			reputation_list.add_child(_reputation_row(str(id_v)))
	else:
		reputation_list.add_child(_dim_label("No characters met yet."))

func _active_quest_row(idx: int) -> Control:
	var giver: Variant = vm.eval_expr('active_quest_field(%d, "giver_name")' % idx)
	var desc:  Variant = vm.eval_expr('active_quest_field(%d, "description")' % idx)
	var reward: Variant = vm.eval_expr('active_quest_field(%d, "reward")' % idx)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 2)
	var head := Label.new()
	head.text = "%s" % giver
	head.add_theme_font_size_override("font_size", 16)
	head.add_theme_color_override("font_color", Color(1.0, 0.95, 0.7))
	box.add_child(head)
	var body := Label.new()
	body.text = '"%s"' % desc
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(body)
	if str(reward) != "":
		var rew := Label.new()
		rew.text = "  Reward: %s" % reward
		rew.add_theme_color_override("font_color", Color(0.8, 0.85, 1))
		box.add_child(rew)
	var row := HBoxContainer.new()
	var btn := Button.new()
	btn.text = "Mark complete"
	btn.pressed.connect(_on_complete.bind(idx))
	row.add_child(btn)
	box.add_child(row)
	box.add_child(_separator())
	return box

func _completed_quest_row(idx: int) -> Control:
	var giver: Variant = vm.eval_expr('completed_quest_field(%d, "giver_name")' % idx)
	var desc:  Variant = vm.eval_expr('completed_quest_field(%d, "description")' % idx)
	var box := VBoxContainer.new()
	var head := Label.new()
	head.text = "%s (done)" % giver
	head.add_theme_color_override("font_color", Color(0.55, 0.75, 0.55))
	box.add_child(head)
	var body := Label.new()
	body.text = '"%s"' % desc
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.add_theme_color_override("font_color", Color(0.65, 0.7, 0.65))
	box.add_child(body)
	box.add_child(_separator())
	return box

func _reputation_row(id: String) -> Control:
	var npc_name: Variant = vm.eval_expr('npc_display_name("%s")' % id)
	var score_v: Variant  = vm.eval_expr('get_npc_affinity("%s")' % id)
	var label_v: Variant  = vm.eval_expr('affinity_label_for("%s")' % id)
	var score: int = int(score_v)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 2)

	var head := HBoxContainer.new()
	var nm := Label.new()
	nm.text = str(npc_name)
	nm.add_theme_font_size_override("font_size", 16)
	nm.add_theme_color_override("font_color", Color(1.0, 0.95, 0.7))
	nm.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(nm)
	var sc := Label.new()
	sc.text = "%+d  (%s)" % [score, str(label_v)]
	var hue := _affinity_color(score)
	sc.add_theme_color_override("font_color", hue)
	head.add_child(sc)
	box.add_child(head)

	var bar := ProgressBar.new()
	bar.min_value = -100
	bar.max_value = 100
	bar.value = score
	bar.show_percentage = false
	bar.custom_minimum_size = Vector2(0, 12)
	box.add_child(bar)
	box.add_child(_separator())
	return box

func _affinity_color(score: int) -> Color:
	if score >= 61: return Color(0.55, 0.95, 0.55)
	if score >= 21: return Color(0.75, 0.95, 0.55)
	if score >= -20: return Color(0.85, 0.85, 0.85)
	if score >= -60: return Color(0.95, 0.75, 0.45)
	return Color(0.95, 0.45, 0.45)

func _inventory_row(idx: int) -> Control:
	var name_: Variant   = vm.eval_expr('inventory_field(%d, "name")' % idx)
	var source: Variant  = vm.eval_expr('inventory_field(%d, "source")' % idx)
	var lbl := Label.new()
	lbl.text = "- %s   (from %s)" % [name_, source]
	return lbl

func _on_complete(idx: int) -> void:
	vm.eval_expr('complete_quest_by_idx(%d)' % idx)
	_refresh()

func _dim_label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", Color(0.5, 0.5, 0.55))
	return l

func _separator() -> HSeparator:
	return HSeparator.new()

func _clear(box: Node) -> void:
	for c in box.get_children():
		c.queue_free()
