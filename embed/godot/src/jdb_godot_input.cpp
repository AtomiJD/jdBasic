// GODOT.INPUT.* native suite - implementation.

#ifdef GODOT

#include "jdb_godot_input.h"
#include "jdb_embed_api.h"

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

using namespace godot;

// ── Event queue ───────────────────────────────────────────────────

void InputEventQueue::push(const InputEventRecord& rec) {
    std::lock_guard<std::mutex> g(m_mu);
    m_q.push_back(rec);
}

bool InputEventQueue::pop(InputEventRecord& out) {
    std::lock_guard<std::mutex> g(m_mu);
    if (m_q.empty()) return false;
    out = m_q.front();
    m_q.pop_front();
    return true;
}

void InputEventQueue::clear() {
    std::lock_guard<std::mutex> g(m_mu);
    m_q.clear();
}

size_t InputEventQueue::size() const {
    std::lock_guard<std::mutex> g(m_mu);
    return m_q.size();
}

// ── Helpers ───────────────────────────────────────────────────────

static String arg_to_string(JdbEmbed* vm, int64_t h) {
    const char* s = jdb_embed_value_string(vm, h);
    return s ? String::utf8(s) : String();
}

static int64_t make_vec2_array(JdbEmbed* vm, double x, double y) {
    int64_t elems[2] = {
        jdb_embed_make_double(vm, x),
        jdb_embed_make_double(vm, y),
    };
    int64_t arr = jdb_embed_make_array(vm, elems, 2);
    jdb_embed_value_release(vm, elems[0]);
    jdb_embed_value_release(vm, elems[1]);
    return arr;
}

// ── Action polling ────────────────────────────────────────────────

static int64_t native_is_action_pressed(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 1) return jdb_embed_make_bool(vm, 0);
    String name = arg_to_string(vm, args[0]);
    bool v = Input::get_singleton()->is_action_pressed(StringName(name));
    return jdb_embed_make_bool(vm, v ? 1 : 0);
}

static int64_t native_is_action_just_pressed(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 1) return jdb_embed_make_bool(vm, 0);
    String name = arg_to_string(vm, args[0]);
    bool v = Input::get_singleton()->is_action_just_pressed(StringName(name));
    return jdb_embed_make_bool(vm, v ? 1 : 0);
}

static int64_t native_is_action_just_released(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 1) return jdb_embed_make_bool(vm, 0);
    String name = arg_to_string(vm, args[0]);
    bool v = Input::get_singleton()->is_action_just_released(StringName(name));
    return jdb_embed_make_bool(vm, v ? 1 : 0);
}

static int64_t native_get_action_strength(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 1) return jdb_embed_make_double(vm, 0.0);
    String name = arg_to_string(vm, args[0]);
    double v = Input::get_singleton()->get_action_strength(StringName(name));
    return jdb_embed_make_double(vm, v);
}

static int64_t native_get_axis(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 2) return jdb_embed_make_double(vm, 0.0);
    String neg = arg_to_string(vm, args[0]);
    String pos = arg_to_string(vm, args[1]);
    double v = Input::get_singleton()->get_axis(StringName(neg), StringName(pos));
    return jdb_embed_make_double(vm, v);
}

static int64_t native_get_vector(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 4) return make_vec2_array(vm, 0.0, 0.0);
    String l = arg_to_string(vm, args[0]);
    String r = arg_to_string(vm, args[1]);
    String u = arg_to_string(vm, args[2]);
    String d = arg_to_string(vm, args[3]);
    Vector2 v = Input::get_singleton()->get_vector(StringName(l), StringName(r),
                                                   StringName(u), StringName(d));
    return make_vec2_array(vm, v.x, v.y);
}

// ── Key + mouse polling ───────────────────────────────────────────

static int64_t native_is_key_pressed(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 1) return jdb_embed_make_bool(vm, 0);
    int64_t keycode = jdb_embed_value_int(vm, args[0]);
    bool v = Input::get_singleton()->is_key_pressed(static_cast<Key>(keycode));
    return jdb_embed_make_bool(vm, v ? 1 : 0);
}

static int64_t native_mouse_position(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* /*ud*/) {
    // Viewport-relative mouse position (what a 2D game actually wants).
    // Reach the main viewport via SceneTree->root. DisplayServer's
    // mouse_get_position returns global screen coords, which is wrong
    // for in-game drawing on multi-monitor setups.
    SceneTree* tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree && tree->get_root()) {
        Vector2 p = tree->get_root()->get_mouse_position();
        return make_vec2_array(vm, p.x, p.y);
    }
    // Fallback to screen coords if no scene tree (CLI / tools case).
    Vector2i sp = DisplayServer::get_singleton()->mouse_get_position();
    return make_vec2_array(vm, double(sp.x), double(sp.y));
}

static int64_t native_mouse_velocity(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* /*ud*/) {
    Vector2 v = Input::get_singleton()->get_last_mouse_velocity();
    return make_vec2_array(vm, v.x, v.y);
}

static int64_t native_is_mouse_button_pressed(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    if (argc < 1) return jdb_embed_make_bool(vm, 0);
    int64_t btn = jdb_embed_value_int(vm, args[0]);
    bool v = Input::get_singleton()->is_mouse_button_pressed(static_cast<MouseButton>(btn));
    return jdb_embed_make_bool(vm, v ? 1 : 0);
}

// ── Event queue drain ─────────────────────────────────────────────

static int64_t native_poll_event(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* ud) {
    InputEventQueue* q = static_cast<InputEventQueue*>(ud);
    if (!q) return jdb_embed_make_nil(vm);
    InputEventRecord rec;
    if (!q->pop(rec)) return jdb_embed_make_nil(vm);

    // Stash the utf8 buffers so the .get_data() pointers stay alive
    // until jdb_embed_make_map has copied them.
    CharString kind_buf   = rec.kind.utf8();
    CharString action_buf = rec.action.utf8();
    CharString type_buf   = rec.type.utf8();

    const char* keys[4] = { "kind", "action", "type", "strength" };
    int64_t vals[4];
    vals[0] = jdb_embed_make_string(vm, kind_buf.get_data());
    vals[1] = jdb_embed_make_string(vm, action_buf.get_data());
    vals[2] = jdb_embed_make_string(vm, type_buf.get_data());
    vals[3] = jdb_embed_make_double(vm, rec.strength);

    int64_t map = jdb_embed_make_map(vm, keys, vals, 4);
    for (int i = 0; i < 4; ++i) {
        jdb_embed_value_release(vm, vals[i]);
    }
    return map;
}

static int64_t native_pending_events(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* ud) {
    InputEventQueue* q = static_cast<InputEventQueue*>(ud);
    return jdb_embed_make_int(vm, q ? static_cast<int64_t>(q->size()) : 0);
}

// ── Registration ──────────────────────────────────────────────────

void godot::register_godot_input_natives(JdbEmbed* vm, InputEventQueue* queue) {
    if (!vm) return;
    void* q = static_cast<void*>(queue);
    jdb_embed_register_native(vm, "GODOT.INPUT.IS_ACTION_PRESSED",       1, 1, &native_is_action_pressed,       nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.IS_ACTION_JUST_PRESSED",  1, 1, &native_is_action_just_pressed,  nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.IS_ACTION_JUST_RELEASED", 1, 1, &native_is_action_just_released, nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.GET_ACTION_STRENGTH",     1, 1, &native_get_action_strength,     nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.GET_AXIS",                2, 2, &native_get_axis,                nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.GET_VECTOR",              4, 4, &native_get_vector,              nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.IS_KEY_PRESSED",          1, 1, &native_is_key_pressed,          nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.MOUSE_POSITION",          0, 0, &native_mouse_position,          nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.MOUSE_VELOCITY",          0, 0, &native_mouse_velocity,          nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.IS_MOUSE_BUTTON_PRESSED", 1, 1, &native_is_mouse_button_pressed, nullptr);
    jdb_embed_register_native(vm, "GODOT.INPUT.POLL_EVENT",              0, 0, &native_poll_event,              q);
    jdb_embed_register_native(vm, "GODOT.INPUT.PENDING_EVENTS",          0, 0, &native_pending_events,          q);
}

#endif  // GODOT
