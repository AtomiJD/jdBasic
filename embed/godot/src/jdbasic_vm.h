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

    // Run a snippet against the persistent VM. Returns the captured PRINT
    // output as a String. Empty on success-with-no-output; check last_error()
    // for failure detail.
    String eval(const String& code);

    // Read + execute a .jdb file. Same return convention as eval().
    String load(const String& path);

    // Last embed-side error message (empty string if none).
    String last_error() const;

private:
    JdbEmbed* m_vm;
};

}  // namespace godot

#endif  // GODOT
