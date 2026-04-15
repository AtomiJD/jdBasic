#pragma once
// vm_bridge.h — C-API for the jdBasic VM runtime DLL (jdbrt.dll)
// Native-compiled executables call through this bridge to access
// all 230+ VM builtins (MAP.*, JSON.*, GFX, etc.)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef JDRT_EXPORTS
#define JDRT_API __declspec(dllexport)
#else
#define JDRT_API __declspec(dllimport)
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

// Type-aware call: each arg has a type tag.
// Tags: 0=i64(as double), 1=f64, 2=string(char*), 3=array(JdbArray*)
// Args are passed as i64 values (doubles bitcast, pointers as intptr).
JDRT_API double jdrt_call_typed_f64(JdRT rt, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs);

JDRT_API char*  jdrt_call_typed_str(JdRT rt, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs);

JDRT_API void   jdrt_call_typed_void(JdRT rt, const char* name,
                                      const int64_t* args, const int32_t* tags, int nargs);

// Get last error message (NULL if no error)
JDRT_API const char* jdrt_last_error(JdRT rt);

#ifdef __cplusplus
}
#endif
