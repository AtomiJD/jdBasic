// JdbScriptInstance - per-Node attached-script runtime. See header.

#ifdef GODOT

#include "jdb_script_instance.h"
#include "jdb_script_resource.h"
#include "jdb_script_language.h"
#include "jdb_godot_natives.h"
#include "jdb_godot_input.h"
#include "jdb_embed_api.h"

#include "jdb_script_language.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/engine_debugger.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <direct.h>
#define jdb_chdir _chdir
#else
#include <unistd.h>
#define jdb_chdir chdir
#endif

using namespace godot;

// ── Helpers ────────────────────────────────────────────────────────

std::string JdbScriptInstance::variant_to_jdb_arg_(const Variant& v) {
    switch (v.get_type()) {
        case Variant::BOOL: {
            return bool(v) ? "1" : "0";
        }
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
        case Variant::STRING: {
            std::string raw = String(v).utf8().get_data();
            std::string out = "\"";
            for (char c : raw) {
                if (c == '"') out += "\\\"";
                else out += c;
            }
            out += "\"";
            return out;
        }
        default:
            return "0";  // unsupported types degrade to numeric zero
    }
}

void JdbScriptInstance::scan_methods_(const String& source) {
    // Simple FUNC / SUB regex-free scan. Look for line-starts (after
    // optional whitespace) matching "FUNC name" or "SUB name". The
    // jdBasic parser will give the authoritative answer at eval time;
    // this set is only the "is the method present?" hint Godot needs
    // for has_method dispatch.
    std::string s = source.utf8().get_data();
    size_t i = 0;
    while (i < s.size()) {
        size_t line_start = i;
        while (line_start < s.size() && (s[line_start] == ' ' || s[line_start] == '\t')) ++line_start;
        size_t line_end = s.find('\n', line_start);
        if (line_end == std::string::npos) line_end = s.size();

        // Match "FUNC " or "SUB " (case-insensitive) at line_start.
        auto match_kw = [&](const char* kw, size_t kw_len) -> bool {
            if (line_start + kw_len + 1 > line_end) return false;
            for (size_t k = 0; k < kw_len; ++k) {
                char c = s[line_start + k];
                char up = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
                if (up != kw[k]) return false;
            }
            char after = s[line_start + kw_len];
            return after == ' ' || after == '\t';
        };

        size_t name_start = 0;
        if (match_kw("FUNC", 4)) name_start = line_start + 5;
        else if (match_kw("SUB", 3)) name_start = line_start + 4;
        if (name_start > 0) {
            // Skip extra whitespace after the keyword.
            while (name_start < line_end && (s[name_start] == ' ' || s[name_start] == '\t')) ++name_start;
            size_t name_end = name_start;
            while (name_end < line_end) {
                char c = s[name_end];
                bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                       || (c >= '0' && c <= '9') || c == '_';
                if (!ok) break;
                ++name_end;
            }
            if (name_end > name_start) {
                std::string name(s.begin() + name_start, s.begin() + name_end);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c){ return (char)std::tolower(c); });
                m_method_set.insert(std::move(name));
            }
        }

        i = (line_end == s.size()) ? line_end : line_end + 1;
    }
}

void JdbScriptInstance::enable_node_processing_() {
    Node* node = Object::cast_to<Node>(m_owner);
    if (!node) return;
    auto has = [&](const char* n) {
        return m_method_set.find(n) != m_method_set.end();
    };
    node->set_process(has("_process"));
    node->set_physics_process(has("_physics_process"));
    node->set_process_input(has("_input"));
    node->set_process_unhandled_input(has("_unhandled_input"));
    node->set_process_shortcut_input(has("_shortcut_input"));
}

// ── T7 debugger plumbing ───────────────────────────────────────────

// Static trampolines registered on the embed VM; route back to the
// instance that owns the VM.
static void jdb_debug_break_tramp(JdbEmbed* /*e*/, int line, const char* reason, void* ud) {
    reinterpret_cast<JdbScriptInstance*>(ud)->on_debug_break(line, reason);
}
static int jdb_debug_line_tramp(JdbEmbed* /*e*/, int line, void* ud) {
    return reinterpret_cast<JdbScriptInstance*>(ud)->is_break_line(line);
}

// Wire the VM's debug hooks - but only when a debug session is live, so a
// shipped game (no editor attached) pays zero per-line overhead.
static void setup_debugger(JdbEmbed* vm, JdbScriptInstance* self) {
    EngineDebugger* dbg = EngineDebugger::get_singleton();
    if (!vm || !dbg || !dbg->is_active()) return;
    jdb_embed_debug_enable(vm);
    jdb_embed_debug_set_hook(vm, &jdb_debug_break_tramp, self);
    jdb_embed_debug_set_line_hook(vm, &jdb_debug_line_tramp, self);
}

// Per-line predicate: is there an editor breakpoint on this line of our
// script? Polled by the VM via the line hook.
int JdbScriptInstance::is_break_line(int line) {
    EngineDebugger* dbg = EngineDebugger::get_singleton();
    if (!dbg || !dbg->is_active() || dbg->is_skipping_breakpoints()) return 0;
    String src = m_script.is_valid() ? m_script->get_path() : String();
    return dbg->is_breakpoint(line, StringName(src)) ? 1 : 0;
}

// Called synchronously by the VM when it pauses. Enter Godot's debugger
// break (blocks + pumps the editor message loop, which queries our
// language's _debug_* virtuals), then translate the editor's continue/step
// choice back into a VM action.
void JdbScriptInstance::on_debug_break(int line, const char* /*reason*/) {
    if (!m_vm) return;
    JdbScriptLanguage* lang = JdbScriptLanguage::get_singleton();
    EngineDebugger* dbg = EngineDebugger::get_singleton();
    if (!lang || !dbg || !dbg->is_active()) {
        jdb_embed_debug_continue(m_vm);
        return;
    }
    lang->set_break_instance(this);
    dbg->script_debug(lang, true, false);
    int lines_left = dbg->get_lines_left();
    int depth      = dbg->get_depth();
    lang->set_break_instance(nullptr);

    if (lines_left < 0) {
        jdb_embed_debug_continue(m_vm);     // Continue
    } else if (depth < 0) {
        jdb_embed_debug_step_in(m_vm);      // Step Into
    } else {
        jdb_embed_debug_step_over(m_vm);    // Step Over (Next)
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────

static int g_alive_instances = 0;
int JdbScriptInstance::alive_count() { return g_alive_instances; }

JdbScriptInstance::JdbScriptInstance(Ref<JdbScriptResource> p_script, Object* p_owner)
    : m_script(p_script), m_owner(p_owner) {
    ++g_alive_instances;
    bool in_editor = Engine::get_singleton() && Engine::get_singleton()->is_editor_hint();
    // T5.4: a script can opt in to running in the editor via ' @tool /
    // OPTION TOOL. When that flag is set we drop the editor-hint guard
    // and let the VM init normally - _ready / _process will fire inside
    // the editor process the same as during play.
    bool is_tool = m_script.is_valid() && m_script->get_is_tool();

    if (in_editor && !is_tool) {
        // Don't spin up a VM for editor-side instances. Godot creates these
        // for Inspector previews, type discovery, etc.; running script code
        // there would tick _process inside the editor process and never
        // stop. Game-mode instances get a real VM via the same ctor path
        // because is_editor_hint is false in play.
        //
        // Mirror the INSPECTOR DIM metadata + seed the per-instance value
        // cache with the script's defaults so the Inspector shows real
        // numbers instead of zeroes. set_property writes back here when
        // the user edits in the Inspector; Godot then persists those
        // values in the .tscn override.
        if (m_script.is_valid()) {
            m_script->register_instance(this);
            const TypedArray<Dictionary>& src_vars = m_script->get_inspector_vars();
            for (int i = 0; i < src_vars.size(); ++i) {
                Dictionary d = src_vars[i];
                InspectorVar v;
                v.name = StringName(String(d[String("name")]));
                v.type = (Variant::Type)(int)d[String("type")];
                if (d.has(String("hint_name"))) {
                    String hn = String(d[String("hint_name")]).to_upper();
                    String ha = String(d[String("hint_args")]);
                    if      (hn == String("RANGE"))           { v.hint = 1;  v.hint_string = ha; }
                    else if (hn == String("ENUM"))            { v.hint = 2;  v.hint_string = ha; }
                    else if (hn == String("EXP_EASING"))      { v.hint = 4;  v.hint_string = ha; }
                    else if (hn == String("FLAGS"))           { v.hint = 6;  v.hint_string = ha; }
                    else if (hn == String("FILE"))            { v.hint = 13; v.hint_string = ha; }
                    else if (hn == String("DIR"))             { v.hint = 14; v.hint_string = ha; }
                    else if (hn == String("GLOBAL_FILE"))     { v.hint = 15; v.hint_string = ha; }
                    else if (hn == String("GLOBAL_DIR"))      { v.hint = 16; v.hint_string = ha; }
                    else if (hn == String("MULTILINE"))       { v.hint = 28; }
                    else if (hn == String("COLOR_NO_ALPHA"))  { v.hint = 9;  }
                    else if (hn == String("PASSWORD"))        { v.hint = 36; }
                }
                m_inspector_vars.push_back(v);
                m_editor_values[v.name] = d[String("default")];
            }
        }
        return;
    }

    m_vm = jdb_embed_init();
    if (!m_vm) {
        UtilityFunctions::push_error(String("[JdbScriptInstance] jdb_embed_init returned NULL"));
        return;
    }

    // Resolve IMPORT and relative file I/O (TXTREADER$, SFX.LOAD, ...) against
    // the project directory instead of wherever Godot was launched from.
    if (ProjectSettings::get_singleton()) {
        String proj = ProjectSettings::get_singleton()->globalize_path("res://");
        if (!proj.is_empty()) jdb_chdir(proj.utf8().get_data());
    }

    // Tier 4 - register GDX.* natives BEFORE the boot eval so the script
    // can use them at top level if it wants to. The bridge keeps a back-
    // pointer to this instance so GDX.SELF() works.
    m_bridge = new GodotBridge(m_vm, this);
    m_bridge->register_all();
    // Tier-3 scripts get discrete events via _input(event) directly,
    // so no event queue is needed; pass nullptr - POLL_EVENT will
    // just return NIL.
    register_godot_input_natives(m_vm, nullptr);
    setup_debugger(m_vm, this);

    // Bring the bundled GDX convenience library into scope via in-memory
    // IMPORT, so scripts can call GDX.MOVE_AND_SLIDE / GDX.TIME_MS etc. The
    // helpers are pure jdBasic over the GDX.* native primitives.
    {
        char* gout = jdb_embed_eval(m_vm, "IMPORT GDX\n");
        if (gout) {
            jdb_embed_free(gout);
        } else {
            const char* gerr = jdb_embed_last_error(m_vm);
            UtilityFunctions::push_error(String("[GDX] import failed: ") + String(gerr ? gerr : "?"));
        }
    }

    if (m_script.is_valid()) {
        String src = m_script->get_processed_source();
        if (src.is_empty()) src = m_script->_get_source_code();
        if (!src.is_empty()) {
            char* out = jdb_embed_eval(m_vm, src.utf8().get_data());
            if (out) {
                String s = String::utf8(out).strip_edges();
                if (!s.is_empty()) UtilityFunctions::print(String("[boot] ") + s);
                jdb_embed_free(out);
            } else {
                const char* err = jdb_embed_last_error(m_vm);
                UtilityFunctions::push_error(String("[JdbScriptInstance] boot eval failed: ") + String(err ? err : "?"));
            }
        }
        scan_methods_(m_script->_get_source_code());
        enable_node_processing_();

        // Mirror the script's INSPECTOR DIM metadata so the Godot Inspector
        // can enumerate properties without round-tripping into the Resource
        // every frame. Carry hint info so sliders / file pickers /
        // dropdowns render correctly.
        const TypedArray<Dictionary>& src_vars = m_script->get_inspector_vars();
        for (int i = 0; i < src_vars.size(); ++i) {
            Dictionary d = src_vars[i];
            InspectorVar v;
            v.name = StringName(String(d[String("name")]));
            v.type = (Variant::Type)(int)d[String("type")];
            if (d.has(String("hint_name"))) {
                String hn = String(d[String("hint_name")]).to_upper();
                String ha = String(d[String("hint_args")]).replace(String(" "), String());
                if      (hn == String("RANGE"))           { v.hint = 1;  v.hint_string = ha; }
                else if (hn == String("ENUM"))            { v.hint = 2;  v.hint_string = ha; }
                else if (hn == String("EXP_EASING"))      { v.hint = 4;  v.hint_string = ha; }
                else if (hn == String("FLAGS"))           { v.hint = 6;  v.hint_string = ha; }
                else if (hn == String("FILE"))            { v.hint = 13; v.hint_string = ha; }
                else if (hn == String("DIR"))             { v.hint = 14; v.hint_string = ha; }
                else if (hn == String("GLOBAL_FILE"))     { v.hint = 15; v.hint_string = ha; }
                else if (hn == String("GLOBAL_DIR"))      { v.hint = 16; v.hint_string = ha; }
                else if (hn == String("MULTILINE"))       { v.hint = 28; }
                else if (hn == String("COLOR_NO_ALPHA"))  { v.hint = 9;  }
                else if (hn == String("PASSWORD"))        { v.hint = 36; }
            }
            m_inspector_vars.push_back(v);
        }
    } else {
        UtilityFunctions::push_error(String("[JdbScriptInstance] script ref is invalid"));
    }

    // Register with the script so hot-reload can find us.
    if (m_script.is_valid()) m_script->register_instance(this);
}

JdbScriptInstance::~JdbScriptInstance() {
    if (m_script.is_valid()) m_script->unregister_instance(this);
    if (m_bridge) {
        delete m_bridge;
        m_bridge = nullptr;
    }
    if (m_vm) {
        jdb_embed_shutdown(m_vm);
        m_vm = nullptr;
    }
    --g_alive_instances;
}

bool JdbScriptInstance::hot_recompile(const String& processed_src) {
    bool in_editor = Engine::get_singleton()
        && Engine::get_singleton()->is_editor_hint();
    bool is_tool   = m_script.is_valid() && m_script->get_is_tool();

    if (in_editor) {
        if (is_tool && !m_vm)      return hard_reload(processed_src);
        if (!is_tool && m_vm)      return hard_reload(processed_src);
    }
    if (!m_vm) return false;

    char* out = jdb_embed_recompile_source(m_vm, processed_src.utf8().get_data());
    if (!out) {
        const char* err = jdb_embed_last_error(m_vm);
        UtilityFunctions::push_error(String("[jdBasic recompile] ") + String(err ? err : "?"));
        return false;
    }
    String summary = String::utf8(out);
    jdb_embed_free(out);

    // Re-scan FUNC/SUB names from the latest source so newly-added
    // engine callbacks become callable.
    m_method_set.clear();
    if (m_script.is_valid()) scan_methods_(m_script->_get_source_code());
    enable_node_processing_();

    UtilityFunctions::print(String("[jdBasic recompile] ") + summary);
    return true;
}

bool JdbScriptInstance::hard_reload(const String& processed_src) {
    // Tear down whatever's there.
    if (m_bridge) {
        delete m_bridge;
        m_bridge = nullptr;
    }
    if (m_vm) {
        jdb_embed_shutdown(m_vm);
        m_vm = nullptr;
    }

    // T5.4: in editor mode without @tool we stay inert (no VM, no bridge,
    // no callbacks fire). Same condition the ctor uses.
    bool in_editor = Engine::get_singleton()
        && Engine::get_singleton()->is_editor_hint();
    bool is_tool   = m_script.is_valid() && m_script->get_is_tool();
    if (in_editor && !is_tool) {
        m_method_set.clear();
        if (m_script.is_valid()) scan_methods_(m_script->_get_source_code());
        return true;
    }

    m_vm = jdb_embed_init();
    if (!m_vm) return false;
    m_bridge = new GodotBridge(m_vm, this);
    m_bridge->register_all();
    register_godot_input_natives(m_vm, nullptr);
    setup_debugger(m_vm, this);

    char* out = jdb_embed_eval(m_vm, processed_src.utf8().get_data());
    if (!out) {
        const char* err = jdb_embed_last_error(m_vm);
        UtilityFunctions::push_error(String("[jdBasic hard reload] ") + String(err ? err : "?"));
        return false;
    }
    jdb_embed_free(out);
    m_method_set.clear();
    if (m_script.is_valid()) scan_methods_(m_script->_get_source_code());
    enable_node_processing_();

    // hard_reload nukes any prior globals; Godot only fires _ready once
    // per Node lifecycle, so without a manual call here `DIM self_h = 0`
    // stays 0 and the next _process can't reach the Node. Fire it now
    // if the script defines one.
    if (m_method_set.find("_ready") != m_method_set.end()) {
        char* r = jdb_embed_eval(m_vm, "_ready()\n");
        if (r) jdb_embed_free(r);
    }
    return true;
}

bool JdbScriptInstance::has_method(const StringName& name) const {
    std::string n = String(name).utf8().get_data();
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return m_method_set.find(n) != m_method_set.end();
}

bool JdbScriptInstance::has_property(const StringName& name) const {
    for (const auto& v : m_inspector_vars) {
        if (v.name == name) return true;
    }
    return false;
}

Variant JdbScriptInstance::value_to_variant(JdbEmbed* vm, int64_t h) {
    if (!vm || !h) return Variant();
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
                // Fast path - homogeneous numeric arrays land as a typed
                // packed array Godot can render / mesh-build directly.
                PackedFloat64Array out;
                out.resize(n);
                double* w = out.ptrw();
                for (int i = 0; i < n; ++i) {
                    int64_t el = jdb_embed_array_get(vm, h, i);
                    w[i] = jdb_embed_value_double(vm, el);
                    jdb_embed_value_release(vm, el);
                }
                return out;
            }
            Array out;
            for (int i = 0; i < n; ++i) {
                int64_t el = jdb_embed_array_get(vm, h, i);
                out.append(value_to_variant(vm, el));
                jdb_embed_value_release(vm, el);
            }
            return out;
        }
        case JDB_T_OBJECT: {
            Dictionary d;
            int n = jdb_embed_map_size(vm, h);
            for (int i = 0; i < n; ++i) {
                const char* k = jdb_embed_map_key_at(vm, h, i);
                int64_t  v   = jdb_embed_map_value_at(vm, h, i);
                d[String::utf8(k ? k : "")] = value_to_variant(vm, v);
                jdb_embed_value_release(vm, v);
            }
            return d;
        }
    }
    return Variant();
}

Variant JdbScriptInstance::get_property(const StringName& name) {
    // Editor-mode path: no VM yet, serve from the per-instance cache that
    // we seeded with defaults at ctor time and that holds any user edits.
    if (!m_vm) {
        if (m_editor_values.has(name)) return m_editor_values[name];
        return Variant();
    }
    int64_t h = jdb_embed_get_global(m_vm, String(name).utf8().get_data());
    if (!h) return Variant();
    Variant out = value_to_variant(m_vm, h);
    jdb_embed_value_release(m_vm, h);
    return out;
}

bool JdbScriptInstance::set_property(const StringName& name, const Variant& value) {
    if (!has_property(name)) return false;
    // Editor-mode path: stash the value so the Inspector shows it on the
    // next get_property; the .tscn override Godot serialises also reads
    // back through the same get_property. When the game runs, the new
    // game-mode instance applies the override via this same set_property
    // (this time with a live VM).
    if (!m_vm) {
        m_editor_values[name] = value;
        return true;
    }
    String s_name = String(name);
    CharString c_name = s_name.utf8();
    switch (value.get_type()) {
        case Variant::BOOL:
            return jdb_embed_set_global_bool(m_vm, c_name.get_data(), bool(value) ? 1 : 0) != 0;
        case Variant::INT:
            return jdb_embed_set_global_int(m_vm, c_name.get_data(), int64_t(value)) != 0;
        case Variant::FLOAT:
            return jdb_embed_set_global_double(m_vm, c_name.get_data(), double(value)) != 0;
        case Variant::STRING: {
            CharString c_val = String(value).utf8();
            return jdb_embed_set_global_string(m_vm, c_name.get_data(), c_val.get_data()) != 0;
        }
        default:
            return false;
    }
}

Variant JdbScriptInstance::call_method(const StringName& name,
                                       const Variant** args, int64_t argc) {
    if (!m_vm) return Variant();
    std::string n = String(name).utf8().get_data();

    // Fast path: marshal the args straight to value handles and invoke the
    // already-compiled SUB via jdb_embed_call - no per-frame lex/parse/
    // compile and no source-text round-trip. variant_to_jdb_value yields the
    // same jdBasic values the old text path did, including bridge handles for
    // Object args (e.g. the InputEvent passed to _input).
    std::vector<int64_t> handles;
    handles.reserve((size_t)argc);
    for (int64_t i = 0; i < argc; ++i) {
        handles.push_back(m_bridge ? variant_to_jdb_value(m_bridge, *args[i]) : 0);
    }

    // Fence so a Godot signal firing while we're in the VM gets queued by the
    // bridge instead of nesting a VM call.
    if (m_bridge) m_bridge->enter_callback();
    int64_t ret = jdb_embed_call(m_vm, n.c_str(),
                                 handles.empty() ? nullptr : handles.data(),
                                 (int)handles.size());

    if (ret) {
        jdb_embed_value_release(m_vm, ret);
    } else {
        const char* err = jdb_embed_last_error(m_vm);
        if (err && err[0]) UtilityFunctions::push_error(String("[jdBasic] ") + String(err));
    }

    // Forward this callback's PRINT output to Godot's console BEFORE draining
    // deferred signal handlers: a deferred handler runs via jdb_embed_call,
    // which clears the shared output buffer and would otherwise swallow the
    // output this callback just produced.
    char* out = jdb_embed_take_output(m_vm);
    if (out) {
        String s = String::utf8(out).strip_edges();
        if (!s.is_empty()) UtilityFunctions::print(s);
        jdb_embed_free(out);
    }

    // Now safe to drain any signals that fired while we were inside the VM.
    if (m_bridge) m_bridge->leave_callback();

    for (int64_t h : handles) {
        if (h) jdb_embed_value_release(m_vm, h);
    }
    return Variant();
}

// ── C bouncers for GDExtensionScriptInstanceInfo3 ────────────────────
//
// Each callback receives a void* that we constructed in _instance_create
// and handed to script_instance_create3. We downcast it back and forward
// to the C++ method.

namespace {

JdbScriptInstance* self_of(GDExtensionScriptInstanceDataPtr p) {
    return reinterpret_cast<JdbScriptInstance*>(p);
}

GDExtensionBool bounce_set(GDExtensionScriptInstanceDataPtr p_instance,
                            GDExtensionConstStringNamePtr p_name,
                            GDExtensionConstVariantPtr p_value) {
    JdbScriptInstance* inst = self_of(p_instance);
    if (!inst || !p_name || !p_value) return 0;
    const StringName& name  = *reinterpret_cast<const StringName*>(p_name);
    const Variant&    value = *reinterpret_cast<const Variant*>(p_value);
    return inst->set_property(name, value) ? 1 : 0;
}

GDExtensionBool bounce_get(GDExtensionScriptInstanceDataPtr p_instance,
                            GDExtensionConstStringNamePtr p_name,
                            GDExtensionVariantPtr r_ret) {
    JdbScriptInstance* inst = self_of(p_instance);
    if (!inst || !p_name || !r_ret) return 0;
    const StringName& name = *reinterpret_cast<const StringName*>(p_name);
    if (!inst->has_property(name)) return 0;
    Variant v = inst->get_property(name);
    *reinterpret_cast<Variant*>(r_ret) = v;
    return 1;
}

const GDExtensionPropertyInfo* bounce_get_property_list(
        GDExtensionScriptInstanceDataPtr p_instance,
        uint32_t* r_count) {
    JdbScriptInstance* inst = self_of(p_instance);
    if (!inst) { *r_count = 0; return nullptr; }
    const auto& vars = inst->inspector_vars();
    if (vars.empty()) { *r_count = 0; return nullptr; }

    // Allocate fresh property-info array per call. The StringName pointers
    // inside reference data owned by the JdbScriptInstance (vars[i].name)
    // so they live as long as the instance does. bounce_free_property_list
    // only frees the array shell.
    auto* list = static_cast<GDExtensionPropertyInfo*>(
        memalloc(sizeof(GDExtensionPropertyInfo) * vars.size()));
    static StringName s_empty_sn;  // shared empty for class_name

    for (size_t i = 0; i < vars.size(); ++i) {
        list[i].type        = (GDExtensionVariantType)vars[i].type;
        list[i].name        = (GDExtensionStringNamePtr)&vars[i].name;
        list[i].class_name  = (GDExtensionStringNamePtr)&s_empty_sn;
        list[i].hint        = vars[i].hint;
        list[i].hint_string = (GDExtensionStringPtr)&vars[i].hint_string;
        list[i].usage       = 2 | 4 | 4096;  // STORAGE | EDITOR | SCRIPT_VARIABLE
    }
    *r_count = (uint32_t)vars.size();
    return list;
}

void bounce_free_property_list(GDExtensionScriptInstanceDataPtr /*p_instance*/,
                                const GDExtensionPropertyInfo* p_list,
                                uint32_t /*p_count*/) {
    if (p_list) memfree(const_cast<GDExtensionPropertyInfo*>(p_list));
}

GDExtensionBool bounce_property_can_revert(GDExtensionScriptInstanceDataPtr /*p*/,
                                            GDExtensionConstStringNamePtr /*n*/) {
    return 0;
}

GDExtensionBool bounce_property_get_revert(GDExtensionScriptInstanceDataPtr /*p*/,
                                            GDExtensionConstStringNamePtr /*n*/,
                                            GDExtensionVariantPtr /*r_ret*/) {
    return 0;
}

GDExtensionObjectPtr bounce_get_owner(GDExtensionScriptInstanceDataPtr p) {
    JdbScriptInstance* inst = self_of(p);
    Object* o = inst ? inst->get_owner() : nullptr;
    // godot-cpp's Object has _owner as the engine-side ptr.
    return o ? reinterpret_cast<GDExtensionObjectPtr>(o->_owner) : nullptr;
}

void bounce_get_property_state(GDExtensionScriptInstanceDataPtr /*p*/,
                                GDExtensionScriptInstancePropertyStateAdd /*p_add*/,
                                void* /*p_userdata*/) {}

const GDExtensionMethodInfo* bounce_get_method_list(
        GDExtensionScriptInstanceDataPtr /*p*/,
        uint32_t* r_count) {
    *r_count = 0;
    return nullptr;
}

void bounce_free_method_list(GDExtensionScriptInstanceDataPtr /*p*/,
                              const GDExtensionMethodInfo* /*list*/,
                              uint32_t /*count*/) {}

GDExtensionVariantType bounce_get_property_type(GDExtensionScriptInstanceDataPtr p,
                                                  GDExtensionConstStringNamePtr p_name,
                                                  GDExtensionBool* r_is_valid) {
    JdbScriptInstance* inst = self_of(p);
    if (!inst || !p_name) {
        if (r_is_valid) *r_is_valid = 0;
        return GDEXTENSION_VARIANT_TYPE_NIL;
    }
    const StringName& name = *reinterpret_cast<const StringName*>(p_name);
    for (const auto& v : inst->inspector_vars()) {
        if (v.name == name) {
            if (r_is_valid) *r_is_valid = 1;
            return (GDExtensionVariantType)v.type;
        }
    }
    if (r_is_valid) *r_is_valid = 0;
    return GDEXTENSION_VARIANT_TYPE_NIL;
}

GDExtensionBool bounce_validate_property(GDExtensionScriptInstanceDataPtr /*p*/,
                                          GDExtensionPropertyInfo* /*p_property*/) {
    return 0;
}

GDExtensionBool bounce_has_method(GDExtensionScriptInstanceDataPtr p,
                                   GDExtensionConstStringNamePtr p_name) {
    JdbScriptInstance* inst = self_of(p);
    if (!inst || !p_name) return 0;
    const StringName& sn = *reinterpret_cast<const StringName*>(p_name);
    return inst->has_method(sn) ? 1 : 0;
}

GDExtensionInt bounce_get_method_argument_count(GDExtensionScriptInstanceDataPtr /*p*/,
                                                  GDExtensionConstStringNamePtr /*p_name*/,
                                                  GDExtensionBool* r_is_valid) {
    if (r_is_valid) *r_is_valid = 0;
    return 0;
}

void bounce_call(GDExtensionScriptInstanceDataPtr p_self,
                  GDExtensionConstStringNamePtr p_method,
                  const GDExtensionConstVariantPtr* p_args,
                  GDExtensionInt p_argc,
                  GDExtensionVariantPtr r_return,
                  GDExtensionCallError* r_error) {
    JdbScriptInstance* inst = self_of(p_self);
    if (!inst || !p_method) {
        if (r_error) r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        return;
    }
    const StringName& name = *reinterpret_cast<const StringName*>(p_method);
    if (!inst->has_method(name)) {
        if (r_error) r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        return;
    }
    // Unpack Variants for our C++-side call_method. GDExtensionConstVariantPtr
    // is `const void*`; cast through the const-array decay manually.
    Variant result = inst->call_method(
        name,
        reinterpret_cast<const Variant**>(const_cast<GDExtensionConstVariantPtr*>(p_args)),
        (int64_t)p_argc);
    if (r_return) {
        *reinterpret_cast<Variant*>(r_return) = result;
    }
    if (r_error) r_error->error = GDEXTENSION_CALL_OK;
}

void bounce_notification(GDExtensionScriptInstanceDataPtr /*p*/,
                          int32_t /*what*/, GDExtensionBool /*reversed*/) {}

void bounce_to_string(GDExtensionScriptInstanceDataPtr /*p*/,
                       GDExtensionBool* r_is_valid, GDExtensionStringPtr /*r_out*/) {
    if (r_is_valid) *r_is_valid = 0;
}

void bounce_refcount_incremented(GDExtensionScriptInstanceDataPtr /*p*/) {}
GDExtensionBool bounce_refcount_decremented(GDExtensionScriptInstanceDataPtr /*p*/) { return 0; }

GDExtensionObjectPtr bounce_get_script(GDExtensionScriptInstanceDataPtr p) {
    JdbScriptInstance* inst = self_of(p);
    if (!inst) return nullptr;
    Ref<JdbScriptResource> s = inst->get_script();
    return s.is_valid() ? reinterpret_cast<GDExtensionObjectPtr>(s->_owner) : nullptr;
}

GDExtensionBool bounce_is_placeholder(GDExtensionScriptInstanceDataPtr /*p*/) { return 0; }

GDExtensionScriptLanguagePtr bounce_get_language(GDExtensionScriptInstanceDataPtr /*p*/) {
    JdbScriptLanguage* lang = JdbScriptLanguage::get_singleton();
    return lang ? reinterpret_cast<GDExtensionScriptLanguagePtr>(lang->_owner) : nullptr;
}

void bounce_free(GDExtensionScriptInstanceDataPtr p) {
    delete self_of(p);
}

}  // namespace

const GDExtensionScriptInstanceInfo3 JdbScriptInstance::s_info = {
    /* set_func                       */ bounce_set,
    /* get_func                       */ bounce_get,
    /* get_property_list_func         */ bounce_get_property_list,
    /* free_property_list_func        */ bounce_free_property_list,
    /* get_class_category_func        */ nullptr,
    /* property_can_revert_func       */ bounce_property_can_revert,
    /* property_get_revert_func       */ bounce_property_get_revert,
    /* get_owner_func                 */ bounce_get_owner,
    /* get_property_state_func        */ bounce_get_property_state,
    /* get_method_list_func           */ bounce_get_method_list,
    /* free_method_list_func          */ bounce_free_method_list,
    /* get_property_type_func         */ bounce_get_property_type,
    /* validate_property_func         */ bounce_validate_property,
    /* has_method_func                */ bounce_has_method,
    /* get_method_argument_count_func */ bounce_get_method_argument_count,
    /* call_func                      */ bounce_call,
    /* notification_func              */ bounce_notification,
    /* to_string_func                 */ bounce_to_string,
    /* refcount_incremented_func      */ bounce_refcount_incremented,
    /* refcount_decremented_func      */ bounce_refcount_decremented,
    /* get_script_func                */ bounce_get_script,
    /* is_placeholder_func            */ bounce_is_placeholder,
    /* set_fallback_func              */ nullptr,
    /* get_fallback_func              */ nullptr,
    /* get_language_func              */ bounce_get_language,
    /* free_func                      */ bounce_free,
};

#endif  // GODOT
