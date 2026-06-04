// JDBasicVM - Godot Object that wraps a persistent jdBasic VM via the
// jdb_embed C-ABI from jdbrt.dll.
//
// GDScript usage:
//     var vm = JDBasicVM.new()
//     print(vm.eval("PRINT 3 * 7"))   # -> "21"
//     vm.eval("DIM x = 10")
//     print(vm.eval("PRINT x * 2"))   # -> "20"  (state survives)
//
// All Godot-specific code in this folder is fenced behind `#ifdef GODOT`
// per the convention in embed/godot/README.md. The SConstruct defines GODOT;
// the generic jdbrt.dll build never does.

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

#include "jdb_godot_input.h"

extern "C" {
struct JdbEmbed;
}

namespace godot {

class JDBasicVM : public RefCounted {
    GDCLASS(JDBasicVM, RefCounted)

protected:
    static void _bind_methods();

public:
    JDBasicVM();
    ~JDBasicVM();

    // Push an InputEvent description into the per-VM queue. GDScript
    // wires its own _input(event) hook to this. From jdBasic, drain
    // via GDX.INPUT.POLL_EVENT().
    void push_input_event(const String& kind,
                          const String& action,
                          const String& type,
                          double strength);
    void clear_input_events();
    int  pending_input_events() const;

    // Run a snippet against the persistent VM. Returns the captured PRINT
    // output as a String. Empty on success-with-no-output; check last_error()
    // for failure detail.
    String eval(const String& code);

    // Read + execute a .jdb file. Same return convention as eval().
    String load(const String& path);

    // Live-coding primitive: re-parse source / file, merge any FUNC/SUB
    // bodies into the running VM. Top-level DIMs / statements are dropped
    // (the running script is mid-flight; re-running its boot would clobber
    // state). Returns a summary like "added=0 updated=1" on success or an
    // empty String on failure (check last_error()).
    String recompile_source(const String& source);
    String recompile(const String& path);

    // E3 typed access. eval_expr evaluates a jdBasic expression and returns
    // the result as a typed Godot Variant (numeric arrays become
    // PackedFloat64Array; maps become Dictionary; etc.). get_var fetches
    // a top-level global by name.
    Variant eval_expr(const String& expr);
    Variant get_var (const String& name);

    // Last embed-side error message (empty string if none).
    String last_error() const;

private:
    JdbEmbed*       m_vm;
    InputEventQueue m_input_queue;
};

}  // namespace godot

#endif  // GODOT
