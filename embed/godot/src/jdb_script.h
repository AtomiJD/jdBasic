// JDBScript - a Godot Node that hosts a jdBasic script.
//
// Drop a JDBScript as a child of any Node, set `script_path` to a .jdb
// file, and the script's globals/FUNCs become resident. By convention,
// jdBasic FUNC/SUB names matching Godot lifecycle hooks fire automatically:
//
//     SUB on_ready          -- once when the Node enters the scene tree
//     SUB on_process(delta) -- every frame (matched against Godot's _process)
//     SUB on_exit           -- when the Node leaves the tree
//
// From the parent GDScript:
//     $JDBScript.call("display_angle", [])    -> Variant (parses PRINT'd return)
//     $JDBScript.set_var("rot_speed", 10.0)   -> assigns into a VM global
//     $JDBScript.recompile()                  -> reloads the file, merges FUNC bodies
//     $JDBScript.eval("PRINT 1 + 1")          -> generic escape hatch
//
// This is Tier 2 of the embedding: jdBasic owns the frame loop, GDScript
// becomes thin glue. Tier 3 (full ScriptLanguageExtension) is a separate,
// much larger project.

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>

extern "C" {
struct JdbEmbed;
}

namespace godot {

class JDBScript : public Node {
    GDCLASS(JDBScript, Node)

protected:
    static void _bind_methods();

public:
    JDBScript();
    ~JDBScript();

    void _ready()                   override;
    void _process(double delta)     override;
    void _exit_tree()               override;

    // Inspector property: res:// path to the .jdb file.
    void   set_script_path(const String& p);
    String get_script_path() const;

    // Invoke a jdBasic FUNC and parse its PRINT'd return. Returns
    // Variant() (NIL) if the FUNC is undefined or output cannot be parsed.
    Variant call(const String& func, const Array& args);

    // Invoke a SUB (no return value).
    void call_sub(const String& sub, const Array& args);

    // Assign to a jdBasic top-level global. Numeric and string args
    // supported in this MVP.
    void set_var(const String& name, const Variant& value);

    // Convenience: read a global by PRINT'ing it and parsing the result.
    Variant get_var(const String& name);

    // Re-read script_path from disk and merge any FUNC/SUB bodies into
    // the running VM. Top-level DIMs are dropped (live-coding semantic).
    // Returns "added=N updated=M" on success, empty on error.
    String recompile();

    // Generic eval (REPL-style). Returns the captured PRINT output.
    String eval(const String& code);

    // Last error from any of the above (empty if none).
    String last_error() const;

private:
    String m_script_path;
    JdbEmbed* m_vm = nullptr;
    String m_last_error;

    // Cached after the initial load: which lifecycle FUNCs the script
    // actually defines. Avoids a per-frame "does on_process exist" eval.
    bool m_has_ready   = false;
    bool m_has_process = false;
    bool m_has_exit    = false;

    void load_source_();
    static bool scan_for_def_(const String& source, const String& name);
    static String format_arg_(const Variant& v);
    String build_call_(const String& func, const Array& args);
};

}  // namespace godot

#endif  // GODOT
