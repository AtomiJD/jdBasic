// jdb_embed_api.h — embedder C-API for jdbrt.dll.
//
// Lets a host application (Godot GDExtension, REPL, CLI, etc.) drive a
// persistent jdBasic VM from C/C++. Inverse of vm_bridge.h: that one
// lets compiled-jdBasic-EXEs call BACK into the runtime DLL; this one
// lets a HOST app drive the runtime DLL forward.
//
// MVP surface (Day 1 of the Godot spike):
//   * jdb_embed_init / jdb_embed_shutdown - lifecycle
//   * jdb_embed_eval                       - run a snippet, capture PRINT
//   * jdb_embed_load                       - run a .jdb file
//   * jdb_embed_last_error / _free         - error inspection + cleanup
//
// Returned strings are malloc'd. Caller must release with jdb_embed_free.
// Returning NULL means the call failed; jdb_embed_last_error has details.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #ifdef JDRT_EXPORTS
    #define JDB_EMBED_API __declspec(dllexport)
  #else
    #define JDB_EMBED_API __declspec(dllimport)
  #endif
#else
  #define JDB_EMBED_API
#endif

typedef struct JdbEmbed JdbEmbed;

JDB_EMBED_API JdbEmbed*  jdb_embed_init(void);
JDB_EMBED_API void       jdb_embed_shutdown(JdbEmbed* e);

JDB_EMBED_API char*      jdb_embed_eval(JdbEmbed* e, const char* code);
JDB_EMBED_API char*      jdb_embed_load(JdbEmbed* e, const char* path);

// Re-parse `source` (or `path` contents) and merge any FUNC/SUB bodies
// into the running VM. Top-level statements in the new source are
// discarded - so DIM angle = 0.0 keeps its current value, but a same-named
// SUB body gets swapped in. Returns a malloc'd "added=N updated=M" summary,
// or NULL on parse/compile failure (last_error has the message).
//
// This is the live-coding primitive: the embedder edits a .jdb file, calls
// recompile, and the next FUNC invocation runs the new body against the
// existing state.
JDB_EMBED_API char*      jdb_embed_recompile(JdbEmbed* e, const char* path);
JDB_EMBED_API char*      jdb_embed_recompile_source(JdbEmbed* e, const char* source);

JDB_EMBED_API const char* jdb_embed_last_error(JdbEmbed* e);
JDB_EMBED_API void        jdb_embed_free(char* s);

// Standalone syntax check: lex + parse only. No VM, no state. Returns
// NULL when `source` parses cleanly, or a malloc'd error message (caller
// frees with jdb_embed_free) when it doesn't. The error message follows
// jdBasic's "Parse error at line N: ..." / "Lex error at line N: ..."
// convention - the host can scrape line numbers from it.
JDB_EMBED_API char*       jdb_embed_check_standalone(const char* source);

// ── Typed value access (E3 marshalling) ────────────────────────────
//
// A `JdbValue` is an opaque handle into the embed's per-VM value store.
// Handles are obtained by jdb_embed_eval_expr or jdb_embed_get_global;
// the caller frees them with jdb_embed_value_release. A handle of 0
// means "no value / lookup failed".
//
// Tag values match jdBasic's ValueType compactly:
typedef int64_t JdbValue;

enum {
    JDB_T_NONE   = 0,   // NIL / NONE
    JDB_T_BOOL   = 1,
    JDB_T_INT    = 2,   // any signed integer (BYTE through INT64)
    JDB_T_DOUBLE = 3,   // any float (FLOAT16 through FLOAT64)
    JDB_T_STRING = 4,
    JDB_T_OBJECT = 5,   // jdBasic MAP / object - JSON-style dict
    JDB_T_ARRAY  = 6,
};

// Evaluate an expression (NOT a statement), return a handle to its value.
// Internally synthesises `__jdb_embed_ret = (expr)` so the result is
// captured. Returns 0 if compilation or eval failed (check last_error).
JDB_EMBED_API JdbValue jdb_embed_eval_expr(JdbEmbed* e, const char* expr);

// Read a top-level global by name. Returns 0 if no such global exists.
JDB_EMBED_API JdbValue jdb_embed_get_global(JdbEmbed* e, const char* name);

// Inspect / extract.
JDB_EMBED_API int         jdb_embed_value_tag   (JdbEmbed* e, JdbValue h);
JDB_EMBED_API int64_t     jdb_embed_value_int   (JdbEmbed* e, JdbValue h);
JDB_EMBED_API double      jdb_embed_value_double(JdbEmbed* e, JdbValue h);
JDB_EMBED_API const char* jdb_embed_value_string(JdbEmbed* e, JdbValue h);  // owned by store; valid until release
JDB_EMBED_API int         jdb_embed_value_bool  (JdbEmbed* e, JdbValue h);

// Array (jdBasic ARRAY type) accessors.
JDB_EMBED_API int      jdb_embed_array_len        (JdbEmbed* e, JdbValue h);
JDB_EMBED_API JdbValue jdb_embed_array_get        (JdbEmbed* e, JdbValue h, int idx);
// Returns 1 if every element is numeric (INT/DOUBLE/BOOL). Lets the host
// fast-path to a typed packed array instead of a generic Array.
JDB_EMBED_API int      jdb_embed_array_is_numeric(JdbEmbed* e, JdbValue h);

// Map (jdBasic OBJECT type) accessors.
JDB_EMBED_API int         jdb_embed_map_size     (JdbEmbed* e, JdbValue h);
JDB_EMBED_API const char* jdb_embed_map_key_at   (JdbEmbed* e, JdbValue h, int idx);
JDB_EMBED_API JdbValue    jdb_embed_map_value_at (JdbEmbed* e, JdbValue h, int idx);
JDB_EMBED_API JdbValue    jdb_embed_map_get      (JdbEmbed* e, JdbValue h, const char* key);

// Write side - set a top-level global from a primitive. Useful for
// reverse-marshalling Inspector property values without synthesising
// `name = value` source. Returns 1 on success.
JDB_EMBED_API int jdb_embed_set_global_double(JdbEmbed* e, const char* name, double v);
JDB_EMBED_API int jdb_embed_set_global_int   (JdbEmbed* e, const char* name, int64_t v);
JDB_EMBED_API int jdb_embed_set_global_string(JdbEmbed* e, const char* name, const char* v);
JDB_EMBED_API int jdb_embed_set_global_bool  (JdbEmbed* e, const char* name, int v);

// Release a handle. After release the handle is invalid; do not pass it
// to any other accessor. Released slots may be re-used. Release-of-0 is OK.
JDB_EMBED_API void jdb_embed_value_release(JdbEmbed* e, JdbValue h);

// ── Tier 4: host-supplied native functions ─────────────────────────
//
// The host (e.g. the Godot GDExtension) registers callbacks that show
// up as named functions in jdBasic. From the script:
//
//     GODOT.SET(GODOT.SELF(), "rotation:y", angle)
//
// On the C side, each callback gets the arg-handles, returns a single
// result handle, and may store small state via the userdata pointer.
// Args are JdbValue handles owned by the VM - DO NOT release them inside
// the callback. The return handle (0 means "no value / NIL") is taken
// by the VM and released after the value lands in jdBasic.
typedef int64_t (*JdbNativeFunc)(JdbEmbed* vm,
                                 int argc,
                                 const int64_t* args,
                                 void* userdata);

// min_args / max_args are checked by the VM before the callback runs.
// max_args = -1 means "unlimited". Returns 1 on success.
JDB_EMBED_API int jdb_embed_register_native(JdbEmbed* e,
                                              const char* name,
                                              int min_args,
                                              int max_args,
                                              JdbNativeFunc fn,
                                              void* userdata);

// Helpers a native callback uses to construct return values without
// the surrounding host having to manage allocations directly.
JDB_EMBED_API JdbValue jdb_embed_make_int   (JdbEmbed* e, int64_t v);
JDB_EMBED_API JdbValue jdb_embed_make_double(JdbEmbed* e, double v);
JDB_EMBED_API JdbValue jdb_embed_make_string(JdbEmbed* e, const char* v);
JDB_EMBED_API JdbValue jdb_embed_make_bool  (JdbEmbed* e, int v);
JDB_EMBED_API JdbValue jdb_embed_make_nil   (JdbEmbed* e);
// Build an array from N pre-existing handles. The handles are copied;
// the caller still owns and must release them. Returns 0 on failure.
JDB_EMBED_API JdbValue jdb_embed_make_array (JdbEmbed* e,
                                              const JdbValue* elems,
                                              int n);

#ifdef __cplusplus
}
#endif
