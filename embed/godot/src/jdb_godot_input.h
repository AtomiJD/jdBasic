// GDX.INPUT.* native function suite for jdBasic scripts running inside
// the JDBasicVM GDExtension.
//
// Two halves:
//
//   1. Stateless polling. Each native function reads Godot's Input
//      singleton on demand. Cheap, no setup required.
//        GDX.INPUT.IS_ACTION_PRESSED("name") -> bool
//        GDX.INPUT.IS_ACTION_JUST_PRESSED("name") -> bool
//        GDX.INPUT.IS_ACTION_JUST_RELEASED("name") -> bool
//        GDX.INPUT.GET_ACTION_STRENGTH("name") -> double
//        GDX.INPUT.GET_AXIS("neg_action", "pos_action") -> double
//        GDX.INPUT.GET_VECTOR("l", "r", "u", "d") -> [x, y]
//        GDX.INPUT.IS_KEY_PRESSED(keycode) -> bool
//        GDX.INPUT.MOUSE_POSITION() -> [x, y]
//        GDX.INPUT.IS_MOUSE_BUTTON_PRESSED(button) -> bool
//
//   2. Event queue. GDScript-side _input(event) calls
//      vm.push_input_event(...) to enqueue discrete events. jdBasic
//      side drains them via GDX.INPUT.POLL_EVENT() and gets a MAP:
//        { "kind": "action"|"key"|"mouse",
//          "action": "ui_accept" (only for action events),
//          "type":   "pressed" | "released",
//          "strength": 0.0..1.0 (action) or button index (mouse) }
//      or NIL when the queue is empty.
//
// Pure polling is enough for movement / sprint / camera. The event
// queue is the better fit for discrete actions (talk, jump, fire) so
// the script doesn't have to manually de-bounce.

#pragma once

#ifdef GODOT

#include <godot_cpp/variant/string.hpp>

#include <deque>
#include <mutex>

extern "C" {
struct JdbEmbed;
}

namespace godot {

struct InputEventRecord {
    String  kind;       // "action" | "key" | "mouse"
    String  action;     // empty unless kind == "action"
    String  type;       // "pressed" | "released"
    double  strength;   // 0..1 for actions, button index for mouse,
                        // keycode for "key"
};

class InputEventQueue {
public:
    void push(const InputEventRecord& rec);
    bool pop(InputEventRecord& out);
    void clear();
    size_t size() const;

private:
    mutable std::mutex      m_mu;
    std::deque<InputEventRecord> m_q;
};

// Register the GDX.INPUT.* natives on the given embed VM. queue may
// be null - polling natives work without it; POLL_EVENT just returns NIL.
void register_godot_input_natives(JdbEmbed* vm, InputEventQueue* queue);

}  // namespace godot

#endif  // GODOT
