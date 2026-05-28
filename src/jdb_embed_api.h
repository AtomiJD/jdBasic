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

#ifdef __cplusplus
}
#endif
