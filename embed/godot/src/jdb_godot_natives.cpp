// Tier 4 - GODOT.* native function suite. See header.

#ifdef GODOT

#include "jdb_godot_natives.h"
#include "jdb_script_instance.h"
#include "jdb_script_resource.h"
#include "jdb_embed_api.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/timer.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
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

// ── Signal relay ─────────────────────────────────────────────────
//
// A CallableCustom we hand to Object::connect. When the signal fires Godot
// calls call(); we forward the args into the jdBasic VM via the bridge. The
// shared BridgeAlive token lets us survive the bridge being torn down (hot
// reload) without dereferencing freed memory - a stale fire is a no-op.

namespace {

class JdbSignalCallable : public CallableCustom {
public:
    JdbSignalCallable(std::shared_ptr<BridgeAlive> link, const String& sub)
        : m_link(std::move(link)), m_sub(sub) {}

    uint32_t hash() const override {
        // Combine the sub name with the bridge identity so two callables
        // into different scripts don't collide.
        uint32_t h = m_sub.hash();
        BridgeAlive* p = m_link.get();
        return h ^ (uint32_t)(reinterpret_cast<uintptr_t>(p));
    }

    String get_as_text() const override {
        return String("jdBasic::") + m_sub;
    }

    static bool _equal(const CallableCustom* a, const CallableCustom* b) {
        const JdbSignalCallable* x = static_cast<const JdbSignalCallable*>(a);
        const JdbSignalCallable* y = static_cast<const JdbSignalCallable*>(b);
        return x->m_link.get() == y->m_link.get() && x->m_sub == y->m_sub;
    }
    static bool _less(const CallableCustom* a, const CallableCustom* b) {
        const JdbSignalCallable* x = static_cast<const JdbSignalCallable*>(a);
        const JdbSignalCallable* y = static_cast<const JdbSignalCallable*>(b);
        if (x->m_link.get() != y->m_link.get())
            return x->m_link.get() < y->m_link.get();
        return x->m_sub < y->m_sub;
    }
    CompareEqualFunc get_compare_equal_func() const override { return &_equal; }
    CompareLessFunc  get_compare_less_func()  const override { return &_less; }

    // Validity is tied to the bridge, not to any Godot Object, so report no
    // associated object and gate on the alive token instead.
    ObjectID get_object() const override { return ObjectID(); }
    bool is_valid() const override { return m_link && m_link->alive; }

    void call(const Variant** p_arguments, int p_argcount,
              Variant& r_return_value, GDExtensionCallError& r_call_error) const override {
        r_return_value = Variant();
        r_call_error.error = GDEXTENSION_CALL_OK;
        if (m_link && m_link->alive && m_link->bridge) {
            m_link->bridge->dispatch_signal(m_sub, p_arguments, p_argcount);
        }
    }

private:
    std::shared_ptr<BridgeAlive> m_link;
    String                       m_sub;
};

}  // namespace

// ── GodotBridge ──────────────────────────────────────────────────

GodotBridge::GodotBridge(JdbEmbed* vm, JdbScriptInstance* owner)
    : m_vm(vm), m_owner(owner) {
    m_alive = std::make_shared<BridgeAlive>();
    m_alive->bridge = this;
    m_alive->alive  = true;
}

GodotBridge::~GodotBridge() {
    // Drop every live signal connection so a Node that outlives this bridge
    // (hot reload, or the script detaching while the source stays in the
    // tree) doesn't keep a callable pointing at a dead VM. Only touch
    // sources still registered in ObjectDB.
    for (ConnRec& rec : m_connections) {
        Object* src = ObjectDB::get_instance((uint64_t)rec.src);
        if (src && src->is_connected(rec.sig, rec.cb)) {
            src->disconnect(rec.sig, rec.cb);
        }
    }
    m_connections.clear();
    // Belt-and-suspenders: any callable that still fires after this becomes
    // an inert no-op via the shared token.
    if (m_alive) {
        m_alive->alive  = false;
        m_alive->bridge = nullptr;
    }
}

Object* GodotBridge::lookup(int64_t handle) const {
    auto it = m_table.find(handle);
    if (it == m_table.end()) return nullptr;
    // Resolve through ObjectDB every time: an object behind a handle may
    // have been freed (a one-shot timer, a queue_freed node) since we
    // issued it. Return null rather than a dangling pointer.
    return ObjectDB::get_instance(it->second);
}

int64_t GodotBridge::store(Object* obj) {
    if (!obj) return 0;
    uint64_t id = (uint64_t)obj->get_instance_id();
    // Re-use a handle if we've already issued one for this object.
    for (auto& kv : m_table) {
        if (kv.second == id) return kv.first;
    }
    int64_t h = m_next_handle++;
    m_table[h] = id;
    return h;
}

// ── Signals ──────────────────────────────────────────────────────

void GodotBridge::prune_dead_() {
    for (size_t i = 0; i < m_connections.size(); ) {
        if (!ObjectDB::get_instance((uint64_t)m_connections[i].src)) {
            m_connections.erase(m_connections.begin() + i);
        } else {
            ++i;
        }
    }
}

int GodotBridge::connect_signal(int64_t obj_handle, const String& sig,
                                const String& sub, uint32_t flags) {
    prune_dead_();
    Object* src = lookup(obj_handle);
    if (!src) return -1;

    StringName sig_n(sig);
    // Skip an exact duplicate so re-running _ready (or a manual reconnect)
    // doesn't stack identical handlers on the same signal.
    for (ConnRec& rec : m_connections) {
        if ((uint64_t)rec.src == (uint64_t)src->get_instance_id()
                && rec.sig == sig_n && rec.sub == sub) {
            return 0;
        }
    }

    Callable cb(memnew(JdbSignalCallable(m_alive, sub)));
    Error err = src->connect(sig_n, cb, flags);
    if (err != OK) {
        UtilityFunctions::push_error(
            String("[GODOT.CONNECT] connect '") + sig + String("' failed: ")
            + String::num_int64((int64_t)err));
        return -1;
    }

    ConnRec rec;
    rec.src = src->get_instance_id();
    rec.sig = sig_n;
    rec.sub = sub;
    rec.cb  = cb;
    m_connections.push_back(rec);
    return 0;
}

bool GodotBridge::disconnect_signal(int64_t obj_handle, const String& sig,
                                    const String& sub) {
    Object* src = lookup(obj_handle);
    if (!src) return false;
    StringName sig_n(sig);
    for (size_t i = 0; i < m_connections.size(); ++i) {
        ConnRec& rec = m_connections[i];
        if ((uint64_t)rec.src == (uint64_t)src->get_instance_id()
                && rec.sig == sig_n && rec.sub == sub) {
            if (src->is_connected(sig_n, rec.cb)) {
                src->disconnect(sig_n, rec.cb);
            }
            m_connections.erase(m_connections.begin() + i);
            return true;
        }
    }
    return false;
}

void GodotBridge::enter_callback() { ++m_callback_depth; }

void GodotBridge::leave_callback() {
    if (m_callback_depth > 0) --m_callback_depth;
    // Outermost callback returned - flush any signals that fired while we
    // were inside the VM. Each drained call may itself queue more, so loop.
    if (m_callback_depth == 0) {
        while (!m_deferred.empty()) {
            std::vector<std::string> batch;
            batch.swap(m_deferred);
            for (const std::string& code : batch) {
                run_call_(code);
            }
        }
    }
}

std::string GodotBridge::arg_literal_(const Variant& v) {
    switch (v.get_type()) {
        case Variant::BOOL:  return bool(v) ? "1" : "0";
        case Variant::INT: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)int64_t(v));
            return std::string(buf);
        }
        case Variant::FLOAT: {
            char buf[40];
            snprintf(buf, sizeof(buf), "%.7f", (double)v);
            return std::string(buf);
        }
        case Variant::STRING:
        case Variant::STRING_NAME:
        case Variant::NODE_PATH: {
            std::string raw = String(v).utf8().get_data();
            std::string out = "\"";
            for (char c : raw) {
                if (c == '"') out += "\\\"";
                else out += c;
            }
            out += "\"";
            return out;
        }
        case Variant::VECTOR2: {
            Vector2 vv = v;
            char buf[96];
            snprintf(buf, sizeof(buf), "[%.7f, %.7f]", vv.x, vv.y);
            return std::string(buf);
        }
        case Variant::VECTOR3: {
            Vector3 vv = v;
            char buf[128];
            snprintf(buf, sizeof(buf), "[%.7f, %.7f, %.7f]", vv.x, vv.y, vv.z);
            return std::string(buf);
        }
        case Variant::COLOR: {
            Color c = v;
            char buf[160];
            snprintf(buf, sizeof(buf), "[%.7f, %.7f, %.7f, %.7f]", c.r, c.g, c.b, c.a);
            return std::string(buf);
        }
        case Variant::OBJECT: {
            Object* o = v;
            int64_t h = o ? store(o) : 0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)h);
            return std::string(buf);
        }
        default:
            // Unsupported payloads (Dictionary, packed arrays, ...) degrade
            // to numeric zero rather than producing a broken source literal.
            return "0";
    }
}

void GodotBridge::run_call_(const std::string& code) {
    if (!m_vm) return;
    char* out = jdb_embed_eval(m_vm, code.c_str());
    if (!out) {
        const char* err = jdb_embed_last_error(m_vm);
        if (err) UtilityFunctions::push_error(String("[jdBasic signal] ") + String(err));
        return;
    }
    String s = String::utf8(out).strip_edges();
    jdb_embed_free(out);
    if (!s.is_empty()) UtilityFunctions::print(s);
}

void GodotBridge::dispatch_signal(const String& sub, const Variant** args, int argc) {
    std::string code;
    code.reserve(32 + argc * 16);
    code += String(sub).utf8().get_data();
    code += "(";
    for (int i = 0; i < argc; ++i) {
        if (i > 0) code += ", ";
        code += arg_literal_(args[i] ? *args[i] : Variant());
    }
    code += ")\n";

    // If we're already inside a VM callback, defer rather than nest
    // jdb_embed_eval (the interpreter is not re-entrant). leave_callback
    // drains the queue once the outer eval unwinds.
    if (m_callback_depth > 0) {
        m_deferred.push_back(code);
        return;
    }
    run_call_(code);
}

int64_t GodotBridge::make_timer(double secs, const String& sub, bool repeat) {
    Node* owner_node = Object::cast_to<Node>(m_owner ? m_owner->get_owner() : nullptr);
    if (!owner_node) return 0;

    Timer* t = memnew(Timer);
    t->set_wait_time(secs > 0.0 ? secs : 0.0001);
    t->set_one_shot(!repeat);
    t->set_autostart(false);
    owner_node->add_child(t);

    int64_t h = store(t);
    if (!sub.is_empty()) connect_signal(h, String("timeout"), sub, 0);

    if (!repeat) {
        // Self-clean: a one-shot timer queue_frees itself once it fires so
        // repeated GODOT.TIMER calls (cooldowns, delays) don't pile up
        // stopped Timer nodes. Deferred so it frees after the timeout has
        // been fully processed. lookup() resolves the now-dead handle to
        // null, so a stale GODOT.CALL on it is a safe no-op rather than a
        // dangling deref.
        t->connect("timeout", Callable(t, "queue_free"),
                   Object::CONNECT_ONE_SHOT | Object::CONNECT_DEFERRED);
    }

    t->start();
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
    if (argc < 3) {
        UtilityFunctions::push_error(String("[GODOT.SET] argc < 3"));
        return jdb_embed_make_bool(vm, 0);
    }
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) {
        UtilityFunctions::push_error(String("[GODOT.SET] no bridge"));
        return jdb_embed_make_bool(vm, 0);
    }
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    Object* obj = bridge->lookup(handle);
    if (!obj) {
        UtilityFunctions::push_error(String("[GODOT.SET] no obj for handle ") + String::num_int64(handle));
        return jdb_embed_make_bool(vm, 0);
    }
    const char* prop_name = jdb_embed_value_string(vm, args[1]);
    Variant v = jdb_value_to_variant(bridge, args[2]);
    String name(prop_name ? prop_name : "");

    int colon = name.find(":");
    if (colon < 0) {
        obj->set(StringName(name), v);
        return jdb_embed_make_bool(vm, 1);
    }
    // Sub-property write. set_indexed exists but doesn't reliably refresh
    // derived properties on Node3D (e.g. rotation:y -> the rendered
    // transform). Do an explicit read-modify-write: get the parent
    // property as a Variant, mutate the component via Variant::set, and
    // write the whole thing back so the parent setter fires its
    // recompute hook (transform basis, material cache, etc.).
    String parent_name = name.substr(0, colon);
    String child_name  = name.substr(colon + 1, name.length() - colon - 1);
    // For deeper paths like "a:b:c" we'd recurse - skip for now and
    // fall through to set_indexed since one-level is the common case.
    if (child_name.contains(":")) {
        obj->set_indexed(NodePath(name), v);
        return jdb_embed_make_bool(vm, 1);
    }
    Variant parent = obj->get(StringName(parent_name));
    bool valid = false;
    // Variant::set takes (key, value, &r_valid). Key as plain String works
    // for Vector3.x/y/z, Color.r/g/b/a, etc.
    parent.set(Variant(child_name), v, &valid);
    if (!valid) {
        // Couldn't reach the child via Variant::set; fall back.
        obj->set_indexed(NodePath(name), v);
        return jdb_embed_make_bool(vm, 1);
    }
    obj->set(StringName(parent_name), parent);
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

// GODOT.EMIT(handle, "signal_name", arg1, arg2, ...) -> emit a signal
// on the target Object. The first arg is the bridge handle (typically
// GODOT.SELF for emitting from self). Returns 1 on success.
static int64_t native_emit(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 2) return jdb_embed_make_bool(vm, 0);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_bool(vm, 0);
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    Object* obj = bridge->lookup(handle);
    if (!obj) return jdb_embed_make_bool(vm, 0);
    const char* signal_name = jdb_embed_value_string(vm, args[1]);
    int extra = argc - 2;
    Array emit_args;
    for (int i = 0; i < extra; ++i) {
        emit_args.append(jdb_value_to_variant(bridge, args[2 + i]));
    }
    // emit_signalp on Object expects a Variant** array. Easier: use callv
    // on the engine-side `emit_signal` method, passing signal name first.
    Array call_args;
    call_args.append(StringName(signal_name ? signal_name : ""));
    for (int i = 0; i < emit_args.size(); ++i) call_args.append(emit_args[i]);
    obj->callv(StringName("emit_signal"), call_args);
    return jdb_embed_make_bool(vm, 1);
}

// GODOT.CONNECT(handle, "signal_name", "sub_name" [, flags]) -> 1 on success
//
// Wires a Godot signal on the target object to a jdBasic SUB. When the
// signal fires, sub_name(arg, ...) runs in this script's VM; signal args
// marshal the same way GODOT.GET return values do (Object args arrive as
// bridge handles). Optional flags map to Godot's CONNECT_* bitmask
// (1=DEFERRED, 4=ONE_SHOT).
static int64_t native_connect(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 3) return jdb_embed_make_bool(vm, 0);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_bool(vm, 0);
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    const char* sig = jdb_embed_value_string(vm, args[1]);
    const char* sub = jdb_embed_value_string(vm, args[2]);
    uint32_t flags = (argc > 3) ? (uint32_t)jdb_embed_value_int(vm, args[3]) : 0;
    if (!sig || !sub) return jdb_embed_make_bool(vm, 0);
    int rc = bridge->connect_signal(handle, String::utf8(sig), String::utf8(sub), flags);
    return jdb_embed_make_bool(vm, rc == 0 ? 1 : 0);
}

// GODOT.DISCONNECT(handle, "signal_name", "sub_name") -> 1 if a matching
// connection was found and removed.
static int64_t native_disconnect(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 3) return jdb_embed_make_bool(vm, 0);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_bool(vm, 0);
    int64_t handle = jdb_embed_value_int(vm, args[0]);
    const char* sig = jdb_embed_value_string(vm, args[1]);
    const char* sub = jdb_embed_value_string(vm, args[2]);
    if (!sig || !sub) return jdb_embed_make_bool(vm, 0);
    bool ok = bridge->disconnect_signal(handle, String::utf8(sig), String::utf8(sub));
    return jdb_embed_make_bool(vm, ok ? 1 : 0);
}

// GODOT.TIMER(secs, "sub" [, repeat]) -> Timer handle
//
// Fires sub() after `secs` seconds. repeat (default false / 0) makes it a
// one-shot that frees itself after firing; a truthy repeat keeps it ticking
// every `secs`. The returned handle works with GODOT.CALL(h, "stop"/"start")
// and GODOT.QUEUE_FREE(h) for a repeating timer.
static int64_t native_timer(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 2) return jdb_embed_make_nil(vm);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_nil(vm);
    double secs = jdb_embed_value_double(vm, args[0]);
    const char* sub = jdb_embed_value_string(vm, args[1]);
    bool repeat = (argc > 2) ? (jdb_embed_value_int(vm, args[2]) != 0) : false;
    if (!sub) return jdb_embed_make_nil(vm);
    int64_t h = bridge->make_timer(secs, String::utf8(sub), repeat);
    return h ? jdb_embed_make_int(vm, h) : jdb_embed_make_nil(vm);
}

// GODOT.LOAD("res://path.png") -> Resource handle (Texture2D, Mesh, etc.)
static int64_t native_load(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 1) return jdb_embed_make_nil(vm);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_nil(vm);
    const char* path = jdb_embed_value_string(vm, args[0]);
    Ref<Resource> res = ResourceLoader::get_singleton()->load(String::utf8(path ? path : ""));
    if (!res.is_valid()) return jdb_embed_make_nil(vm);
    int64_t handle = bridge->store(res.ptr());
    return jdb_embed_make_int(vm, handle);
}

// GODOT.INSTANTIATE("res://prefab.tscn") -> Node handle (instantiated)
static int64_t native_instantiate(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 1) return jdb_embed_make_nil(vm);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_nil(vm);
    const char* path = jdb_embed_value_string(vm, args[0]);
    Ref<PackedScene> scene = ResourceLoader::get_singleton()->load(String::utf8(path ? path : ""));
    if (!scene.is_valid()) return jdb_embed_make_nil(vm);
    Node* node = scene->instantiate();
    if (!node) return jdb_embed_make_nil(vm);
    int64_t handle = bridge->store(node);
    return jdb_embed_make_int(vm, handle);
}

// GODOT.NEW("ClassName") -> Object handle (e.g. "StandardMaterial3D")
static int64_t native_new(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 1) return jdb_embed_make_nil(vm);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_nil(vm);
    const char* class_name = jdb_embed_value_string(vm, args[0]);
    Variant v = ClassDBSingleton::get_singleton()->instantiate(StringName(class_name ? class_name : ""));
    Object* obj = v;
    if (!obj) return jdb_embed_make_nil(vm);
    int64_t handle = bridge->store(obj);
    return jdb_embed_make_int(vm, handle);
}

// GODOT.ADD_CHILD(parent_handle, child_handle) - parent.add_child(child)
static int64_t native_add_child(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 2) return jdb_embed_make_bool(vm, 0);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_bool(vm, 0);
    Object* parent = bridge->lookup(jdb_embed_value_int(vm, args[0]));
    Object* child  = bridge->lookup(jdb_embed_value_int(vm, args[1]));
    if (!parent || !child) return jdb_embed_make_bool(vm, 0);
    Node* p = Object::cast_to<Node>(parent);
    Node* c = Object::cast_to<Node>(child);
    if (!p || !c) return jdb_embed_make_bool(vm, 0);
    p->add_child(c);
    return jdb_embed_make_bool(vm, 1);
}

// GODOT.QUEUE_FREE(handle) - obj.queue_free()
static int64_t native_queue_free(JdbEmbed* vm, int argc, const int64_t* args, void* ud) {
    if (argc < 1) return jdb_embed_make_bool(vm, 0);
    GodotBridge* bridge = bridge_of(ud);
    if (!bridge) return jdb_embed_make_bool(vm, 0);
    Object* o = bridge->lookup(jdb_embed_value_int(vm, args[0]));
    if (!o) return jdb_embed_make_bool(vm, 0);
    Node* n = Object::cast_to<Node>(o);
    if (!n) return jdb_embed_make_bool(vm, 0);
    n->queue_free();
    return jdb_embed_make_bool(vm, 1);
}

// GODOT.TIME.MS() -> int64 millisecond ticks since process start
static int64_t native_time_ms(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* /*ud*/) {
    uint64_t ms = Time::get_singleton()->get_ticks_msec();
    return jdb_embed_make_int(vm, (int64_t)ms);
}

// GODOT.TIME.SEC() -> double seconds since process start (high precision)
static int64_t native_time_sec(JdbEmbed* vm, int /*argc*/, const int64_t* /*args*/, void* /*ud*/) {
    uint64_t us = Time::get_singleton()->get_ticks_usec();
    return jdb_embed_make_double(vm, (double)us / 1'000'000.0);
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
    jdb_embed_register_native(m_vm, "GODOT.EMIT",        2, -1, &native_emit,        this);
    jdb_embed_register_native(m_vm, "GODOT.CONNECT",     3, 4,  &native_connect,     this);
    jdb_embed_register_native(m_vm, "GODOT.DISCONNECT",  3, 3,  &native_disconnect,  this);
    jdb_embed_register_native(m_vm, "GODOT.TIMER",       2, 3,  &native_timer,       this);
    jdb_embed_register_native(m_vm, "GODOT.LOAD",        1, 1,  &native_load,        this);
    jdb_embed_register_native(m_vm, "GODOT.INSTANTIATE", 1, 1,  &native_instantiate, this);
    jdb_embed_register_native(m_vm, "GODOT.NEW",         1, 1,  &native_new,         this);
    jdb_embed_register_native(m_vm, "GODOT.ADD_CHILD",   2, 2,  &native_add_child,   this);
    jdb_embed_register_native(m_vm, "GODOT.QUEUE_FREE",  1, 1,  &native_queue_free,  this);
    jdb_embed_register_native(m_vm, "GODOT.TIME_MS",     0, 0,  &native_time_ms,     nullptr);
    jdb_embed_register_native(m_vm, "GODOT.TIME_SEC",    0, 0,  &native_time_sec,    nullptr);
    jdb_embed_register_native(m_vm, "GODOT.PRINT",       1, -1, &native_print,       this);
}

#endif  // GODOT
