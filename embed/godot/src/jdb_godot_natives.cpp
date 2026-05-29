// Tier 4 - GODOT.* native function suite. See header.

#ifdef GODOT

#include "jdb_godot_natives.h"
#include "jdb_script_instance.h"
#include "jdb_script_resource.h"
#include "jdb_embed_api.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <vector>

using namespace godot;

// ── Variant <-> JdbValue marshalling ──────────────────────────────

int64_t godot::variant_to_jdb_value(GodotBridge* bridge, const Variant& v) {
    if (!bridge) return 0;
    JdbEmbed* vm = bridge->vm();
    if (!vm) return 0;
    switch (v.get_type()) {
        case Variant::NIL:    return jdb_embed_make_nil(vm);
        case Variant::BOOL:   return jdb_embed_make_bool(vm, bool(v) ? 1 : 0);
        case Variant::INT:    return jdb_embed_make_int(vm, int64_t(v));
        case Variant::FLOAT:  return jdb_embed_make_double(vm, double(v));
        case Variant::STRING:
        case Variant::STRING_NAME:
        case Variant::NODE_PATH: {
            CharString s = String(v).utf8();
            return jdb_embed_make_string(vm, s.get_data());
        }
        case Variant::VECTOR2: {
            Vector2 vv = v;
            int64_t e[2] = {
                jdb_embed_make_double(vm, vv.x),
                jdb_embed_make_double(vm, vv.y),
            };
            int64_t arr = jdb_embed_make_array(vm, e, 2);
            jdb_embed_value_release(vm, e[0]);
            jdb_embed_value_release(vm, e[1]);
            return arr;
        }
        case Variant::VECTOR3: {
            Vector3 vv = v;
            int64_t e[3] = {
                jdb_embed_make_double(vm, vv.x),
                jdb_embed_make_double(vm, vv.y),
                jdb_embed_make_double(vm, vv.z),
            };
            int64_t arr = jdb_embed_make_array(vm, e, 3);
            for (int i = 0; i < 3; ++i) jdb_embed_value_release(vm, e[i]);
            return arr;
        }
        case Variant::COLOR: {
            Color c = v;
            int64_t e[4] = {
                jdb_embed_make_double(vm, c.r),
                jdb_embed_make_double(vm, c.g),
                jdb_embed_make_double(vm, c.b),
                jdb_embed_make_double(vm, c.a),
            };
            int64_t arr = jdb_embed_make_array(vm, e, 4);
            for (int i = 0; i < 4; ++i) jdb_embed_value_release(vm, e[i]);
            return arr;
        }
        case Variant::OBJECT: {
            Object* o = v;
            if (!o) return jdb_embed_make_nil(vm);
            int64_t h = bridge->store(o);
            return jdb_embed_make_int(vm, h);
        }
        default:
            // Fallback: stringify so the user at least sees something.
            CharString s = String(v).utf8();
            return jdb_embed_make_string(vm, s.get_data());
    }
}

// Reverse direction: jdBasic value -> Godot Variant. Numeric arrays of
// length 2/3/4 are interpreted as Vector2 / Vector3 / Color so user
// scripts can write `GODOT.SET(self, "position", [1.0, 2.0, 3.0])`.
Variant godot::jdb_value_to_variant(GodotBridge* bridge, int64_t h) {
    if (!bridge || !h) return Variant();
    JdbEmbed* vm = bridge->vm();
    if (!vm) return Variant();
    int tag = jdb_embed_value_tag(vm, h);
    switch (tag) {
        case JDB_T_NONE:   return Variant();
        case JDB_T_BOOL:   return jdb_embed_value_bool(vm, h) != 0;
        case JDB_T_INT:    return (int64_t)jdb_embed_value_int(vm, h);
        case JDB_T_DOUBLE: return jdb_embed_value_double(vm, h);
        case JDB_T_STRING: {
            const char* s = jdb_embed_value_string(vm, h);
            return String::utf8(s ? s : "");
        }
        case JDB_T_ARRAY: {
            int n = jdb_embed_array_len(vm, h);
            if (jdb_embed_array_is_numeric(vm, h)) {
                if (n == 2) {
                    int64_t a = jdb_embed_array_get(vm, h, 0);
                    int64_t b = jdb_embed_array_get(vm, h, 1);
                    Vector2 r((float)jdb_embed_value_double(vm, a),
                              (float)jdb_embed_value_double(vm, b));
                    jdb_embed_value_release(vm, a);
                    jdb_embed_value_release(vm, b);
                    return r;
                }
                if (n == 3) {
                    int64_t a = jdb_embed_array_get(vm, h, 0);
                    int64_t b = jdb_embed_array_get(vm, h, 1);
                    int64_t c = jdb_embed_array_get(vm, h, 2);
                    Vector3 r((float)jdb_embed_value_double(vm, a),
                              (float)jdb_embed_value_double(vm, b),
                              (float)jdb_embed_value_double(vm, c));
                    jdb_embed_value_release(vm, a);
                    jdb_embed_value_release(vm, b);
                    jdb_embed_value_release(vm, c);
                    return r;
                }
                if (n == 4) {
                    int64_t a = jdb_embed_array_get(vm, h, 0);
                    int64_t b = jdb_embed_array_get(vm, h, 1);
                    int64_t c = jdb_embed_array_get(vm, h, 2);
                    int64_t d = jdb_embed_array_get(vm, h, 3);
                    Color r((float)jdb_embed_value_double(vm, a),
                            (float)jdb_embed_value_double(vm, b),
                            (float)jdb_embed_value_double(vm, c),
                            (float)jdb_embed_value_double(vm, d));
                    jdb_embed_value_release(vm, a);
                    jdb_embed_value_release(vm, b);
                    jdb_embed_value_release(vm, c);
                    jdb_embed_value_release(vm, d);
                    return r;
                }
            }
            // Generic array passthrough.
            Array arr;
            for (int i = 0; i < n; ++i) {
                int64_t el = jdb_embed_array_get(vm, h, i);
                arr.append(jdb_value_to_variant(bridge, el));
                jdb_embed_value_release(vm, el);
            }
            return arr;
        }
    }
    return Variant();
}

// ── GodotBridge ──────────────────────────────────────────────────

GodotBridge::GodotBridge(JdbEmbed* vm, JdbScriptInstance* owner)
    : m_vm(vm), m_owner(owner) {}

GodotBridge::~GodotBridge() {}

Object* GodotBridge::lookup(int64_t handle) const {
    auto it = m_table.find(handle);
    return (it == m_table.end()) ? nullptr : it->second;
}

int64_t GodotBridge::store(Object* obj) {
    if (!obj) return 0;
    // Re-use a handle if we've already issued one for this object.
    for (auto& kv : m_table) {
        if (kv.second == obj) return kv.first;
    }
    int64_t h = m_next_handle++;
    m_table[h] = obj;
    return h;
}

// ── Native callbacks ─────────────────────────────────────────────

static GodotBridge* bridge_of(void* userdata) {
    return reinterpret_cast<GodotBridge*>(userdata);
}

// GODOT.SELF() -> int handle to the Node this script is attached to.
static int64_t native_self(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* ud) {
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge || !bridge->owner()) return 0;
    int64_t handle = bridge->store(bridge->owner()->get_owner());
    return jdb_embed_make_int(vm, handle);
}

// GODOT.GET(handle, "property") -> variant
//
// Both flat names ("position") and sub-property paths ("position:x")
// are supported by routing through Object::get_indexed (which falls
// through to Object::get for flat NodePaths).
static int64_t native_get(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 2) return jdb_embed_make_nil(vm);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_nil(vm);
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    Object* obj = bridge->lookup(handle);
    if (!obj) return jdb_embed_make_nil(vm);
    const char* prop_name = jdb_embed_value_string(vm, args[1]);
    String name(prop_name ? prop_name : "");
    Variant v;
    if (name.contains(":")) {
        v = obj->get_indexed(NodePath(name));
    } else {
        v = obj->get(StringName(name));
    }
    return variant_to_jdb_value(bridge, v);
}

// GODOT.SET(handle, "property", value)
//
// Property paths like "rotation:y" go through set_indexed so the script
// can poke individual Vector3 / Color / etc. components without first
// having to read-modify-write the whole struct.
static int64_t native_set(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 3) return jdb_embed_make_bool(vm, 0);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_bool(vm, 0);
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    Object* obj = bridge->lookup(handle);
    if (!obj) return jdb_embed_make_bool(vm, 0);
    const char* prop_name = jdb_embed_value_string(vm, args[1]);
    Variant v = jdb_value_to_variant(bridge, args[2]);
    String name(prop_name ? prop_name : "");
    if (name.contains(":")) {
        obj->set_indexed(NodePath(name), v);
    } else {
        obj->set(StringName(name), v);
    }
    return jdb_embed_make_bool(vm, 1);
}

// GODOT.CALL(handle, "method", args...) -> return value
static int64_t native_call(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 2) return jdb_embed_make_nil(vm);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_nil(vm);
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    Object* obj = bridge->lookup(handle);
    if (!obj) return jdb_embed_make_nil(vm);
    const char* method_name = jdb_embed_value_string(vm, args[1]);
    int extra = argc - 2;
    Array call_args;
    for (int i = 0; i < extra; ++i) {
        call_args.append(jdb_value_to_variant(bridge, args[2 + i]));
    }
    Variant ret = obj->callv(StringName(method_name ? method_name : ""), call_args);
    return variant_to_jdb_value(bridge, ret);
}

// GODOT.VEC3(x, y, z) -> 3-element numeric array (which marshals as Vector3
// on the way back into Godot)
static int64_t native_vec3(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    double x = (argc > 0) ? jdb_embed_value_double(vm, args[0]) : 0.0;
    double y = (argc > 1) ? jdb_embed_value_double(vm, args[1]) : 0.0;
    double z = (argc > 2) ? jdb_embed_value_double(vm, args[2]) : 0.0;
    int64_t e[3] = {
        jdb_embed_make_double(vm, x),
        jdb_embed_make_double(vm, y),
        jdb_embed_make_double(vm, z),
    };
    int64_t arr = jdb_embed_make_array(vm, e, 3);
    for (int i = 0; i < 3; ++i) jdb_embed_value_release(vm, e[i]);
    return arr;
}

// GODOT.VEC2(x, y) -> 2-element numeric array
static int64_t native_vec2(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    double x = (argc > 0) ? jdb_embed_value_double(vm, args[0]) : 0.0;
    double y = (argc > 1) ? jdb_embed_value_double(vm, args[1]) : 0.0;
    int64_t e[2] = {
        jdb_embed_make_double(vm, x),
        jdb_embed_make_double(vm, y),
    };
    int64_t arr = jdb_embed_make_array(vm, e, 2);
    for (int i = 0; i < 2; ++i) jdb_embed_value_release(vm, e[i]);
    return arr;
}

// GODOT.COLOR(r, g, b, a) -> 4-element numeric array (a defaults to 1.0)
static int64_t native_color(JdbEmbed* vm, int argc, const int64_t* args, void* /*ud*/) {
    double r = (argc > 0) ? jdb_embed_value_double(vm, args[0]) : 1.0;
    double g = (argc > 1) ? jdb_embed_value_double(vm, args[1]) : 1.0;
    double b = (argc > 2) ? jdb_embed_value_double(vm, args[2]) : 1.0;
    double a = (argc > 3) ? jdb_embed_value_double(vm, args[3]) : 1.0;
    int64_t e[4] = {
        jdb_embed_make_double(vm, r),
        jdb_embed_make_double(vm, g),
        jdb_embed_make_double(vm, b),
        jdb_embed_make_double(vm, a),
    };
    int64_t arr = jdb_embed_make_array(vm, e, 4);
    for (int i = 0; i < 4; ++i) jdb_embed_value_release(vm, e[i]);
    return arr;
}

// GODOT.PRINT(value) -> print into Godot's output panel
static int64_t native_print(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    GodotBridge* bridge = bridge_of(ud);
    String out;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) out += String(" ");
        Variant v = bridge ? jdb_value_to_variant(bridge, args[i]) : Variant();
        out += String(v);
    }
    UtilityFunctions::print(out);
    return jdb_embed_make_nil(vm);
}

void GodotBridge::register_all() {
    jdb_embed_register_native(m_vm, "GODOT.SELF",   0, 0,  &native_self,  this);
    jdb_embed_register_native(m_vm, "GODOT.GET",    2, 2,  &native_get,   this);
    jdb_embed_register_native(m_vm, "GODOT.SET",    3, 3,  &native_set,   this);
    jdb_embed_register_native(m_vm, "GODOT.CALL",   2, -1, &native_call,  this);
    jdb_embed_register_native(m_vm, "GODOT.VEC2",   2, 2,  &native_vec2,  this);
    jdb_embed_register_native(m_vm, "GODOT.VEC3",   3, 3,  &native_vec3,  this);
    jdb_embed_register_native(m_vm, "GODOT.COLOR",  3, 4,  &native_color, this);
    jdb_embed_register_native(m_vm, "GODOT.PRINT",  1, -1, &native_print, this);
}

#endif  // GODOT
