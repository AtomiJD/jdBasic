#pragma once
// vm_bridge.h - C-API for the jdBasic VM runtime DLL (jdbrt.dll)
// Native-compiled executables call through this bridge to access
// all 230+ VM builtins (MAP.*, JSON.*, GFX, etc.)

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #ifdef JDRT_EXPORTS
    #define JDRT_API __declspec(dllexport)
  #else
    #define JDRT_API __declspec(dllimport)
  #endif
#else
  // POSIX: default visibility is fine - symbols come from a shared library
  // (or static archive) without needing per-symbol annotations.
  #define JDRT_API
#endif

// Opaque handle to the VM runtime
typedef void* JdRT;

// Initialize the runtime (creates VM, registers all builtins)
JDRT_API JdRT  jdrt_init(void);

// Shutdown the runtime (destroys VM)
JDRT_API void  jdrt_shutdown(JdRT rt);

// Call a function by name, passing args as doubles.
// Returns the result as double. Strings are pointer-encoded.
JDRT_API double jdrt_call_f64(JdRT rt, const char* name,
                               const double* args, int nargs);

// Call a function by name, returning a string result.
// Caller must free the returned string with jdrt_free().
JDRT_API char*  jdrt_call_str(JdRT rt, const char* name,
                               const double* args, int nargs);

// Call a function by name, returning an opaque array handle.
// The handle can be passed back to other jdrt_call functions.
JDRT_API void*  jdrt_call_arr(JdRT rt, const char* name,
                               const double* args, int nargs);

// Call a void function (SUB) by name.
JDRT_API void   jdrt_call_void(JdRT rt, const char* name,
                                const double* args, int nargs);

// Free a string returned by jdrt_call_str
JDRT_API void   jdrt_free(void* ptr);

// Call returning an opaque VM value handle (for MAP, JSON objects etc.)
// The handle is valid until jdrt_release_value is called.
JDRT_API int64_t jdrt_call_typed_obj(JdRT rt, const char* name,
                                      const int64_t* args, const int32_t* tags, int nargs);

// Release a VM value handle returned by jdrt_call_typed_obj
JDRT_API void jdrt_release_value(JdRT rt, int64_t handle);

// Field access on a VM Value handle (Map / JSON object). The handle was
// returned by jdrt_call_typed_obj or jdrt_obj_get_obj. All return safe
// defaults (0 / "" / 0-handle / empty array) if the key is missing.
JDRT_API double      jdrt_obj_get_f64(JdRT rt, int64_t h, const char* key);
JDRT_API const char* jdrt_obj_get_str(JdRT rt, int64_t h, const char* key);
JDRT_API int64_t     jdrt_obj_get_obj(JdRT rt, int64_t h, const char* key);
JDRT_API void*       jdrt_obj_get_arr(JdRT rt, int64_t h, const char* key);
JDRT_API int64_t     jdrt_obj_exists (JdRT rt, int64_t h, const char* key);

// Dereference a VM Value handle - used when compiled code needs a
// concrete scalar (passing to a runtime function expecting f64/str).
JDRT_API double      jdrt_val_to_f64 (JdRT rt, int64_t h);
JDRT_API const char* jdrt_val_to_str (JdRT rt, int64_t h);
JDRT_API int64_t     jdrt_val_length (JdRT rt, int64_t h);
// Integer-indexed access on an array-typed handle; returns a fresh handle.
JDRT_API int64_t     jdrt_val_arr_get(JdRT rt, int64_t h, int64_t idx);

// Tagged field access: writes val to *out_val, returns tag.
JDRT_API int32_t jdrt_obj_get_tagged(JdRT rt, int64_t h, const char* key, int64_t* out_val);
// Unified dispatchers: auto-detect native map vs VM handle from val_tag.
JDRT_API int32_t jdrt_tagged_get(JdRT rt, int64_t val, int32_t val_tag,
                                  const char* key, int64_t* out_val);
JDRT_API int32_t jdrt_tagged_arr_get(JdRT rt, int64_t val, int32_t val_tag,
                                      int64_t idx, int64_t* out_val);

// Frame-based cleanup: save watermark at loop start, sweep at loop end.
JDRT_API int64_t     jdrt_frame_begin(JdRT rt);
JDRT_API void        jdrt_frame_end  (JdRT rt, int64_t watermark);
// Re-store a temp handle as a persistent one (negative key, never swept).
JDRT_API int64_t     jdrt_promote_handle(JdRT rt, int64_t h);

// Type-aware call: each arg has a JdTag (see jdb_tags.h). The wire only
// carries I64 / F64 / STR / ARR / VM_HANDLE; NATIVE_MAP and FUNCREF have
// no wire representation. Values are passed as i64 (f64 bitcast, char*
// and JdbArray* as intptr).
JDRT_API double jdrt_call_typed_f64(JdRT rt, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs);

JDRT_API char*  jdrt_call_typed_str(JdRT rt, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs);

JDRT_API void   jdrt_call_typed_void(JdRT rt, const char* name,
                                      const int64_t* args, const int32_t* tags, int nargs);

// Call returning an array (JdbArray*) - for SPLIT, KEYS, VALUES, etc.
JDRT_API void*  jdrt_call_typed_arr(JdRT rt, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs);

// Native-mode event dispatch. The bridge's VM holds the
// event_handlers map (set up by __EVENT_ON), but in native mode the
// handler bodies live as LLVM-IR in the .exe - the bridge VM has no
// way to call them. Instead, the .exe-side runtime registers a
// dispatcher function once at startup; whenever the bridge would have
// called vm.call_function(handler_name, ...) for an event, it now
// hands the event off to that dispatcher.
//
// args/tags use the same wire encoding as jdrt_call_typed_*:
//   I64 → raw int64
//   F64 → double bit-cast into int64
//   STR → const char* cast to int64
typedef void (*JdrtEventDispatch)(const char* event_name,
                                   const int64_t* args,
                                   const int32_t* tags,
                                   int nargs);
JDRT_API void jdrt_set_event_dispatcher(JdRT rt, JdrtEventDispatch fn);

// Get last error message (NULL if no error)
JDRT_API const char* jdrt_last_error(JdRT rt);

// Clear the bridge's last-error slot. Called by generated code once it
// has pulled the message into the native runtime's g_err_msg, so the
// next per-stmt propagation check doesn't re-read the same error.
JDRT_API void jdrt_clear_last_error(JdRT rt);

#ifdef __cplusplus
}
#endif
