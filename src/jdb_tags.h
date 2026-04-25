#pragma once
// Unified type tags shared by codegen, VM bridge, and native runtime.
//
// Three layers used these as magic numbers before and their meanings
// diverged by layer (case 4 meant JdbMap* in the runtime but VM-handle
// in the bridge). This header forces a single numbering scheme so the
// compiler flags mismatches instead of silently mis-dispatching.
//
// Which layer observes which tag:
//
//   I64        codegen, wire, JdbMap field
//   F64        codegen, wire, JdbMap field, runtime return
//   STR        codegen, wire, JdbMap field, runtime return
//   ARR        codegen, wire, JdbMap field, runtime return  (JdbArray*)
//   NATIVE_MAP codegen, JdbMap field, runtime return        (JdbMap*, not on the bridge wire)
//   FUNCREF    codegen only                                 (LLVM fn ptr)
//   VM_HANDLE  codegen, wire, runtime return                (value_store key)
//   RUNTIME    codegen only                                 (marker for "tag lives in a companion alloca")
//
// Bridge wire = the tags array passed to jdrt_call_typed_*.
// That array never carries NATIVE_MAP (codegen downgrades to I64) nor
// RUNTIME (codegen unpacks to the actual runtime tag before sending).

#include <cstdint>

#ifdef __cplusplus
enum class JdTag : int32_t {
    I64        = 0,
    F64        = 1,
    STR        = 2,
    ARR        = 3,
    NATIVE_MAP = 4,
    FUNCREF    = 5,
    VM_HANDLE  = 6,
    RUNTIME    = 7,
    // Codegen-internal: backed by i64 0/1, never appears on the wire.
    BOOL       = 8,
};

constexpr int32_t jd_tag(JdTag t) { return static_cast<int32_t>(t); }
#endif

// C-style aliases — usable from both C and C++. Keeps plain integer
// compares (`m->tags[idx] == JD_TAG_STR`) readable without having to
// cast away enum-class scoping at every site.
#define JD_TAG_I64        0
#define JD_TAG_F64        1
#define JD_TAG_STR        2
#define JD_TAG_ARR        3
#define JD_TAG_NATIVE_MAP 4
#define JD_TAG_FUNCREF    5
#define JD_TAG_VM_HANDLE  6
#define JD_TAG_RUNTIME    7
// Codegen-only tag for Boolean values. Backed by i64 0/1 (no ABI
// difference), lets PRINT / STR$ / array-print route through the bool
// helpers so users see TRUE/FALSE instead of 1/0. Codegen downgrades
// this to JD_TAG_I64 on bridge marshalling so the wire never carries it.
#define JD_TAG_BOOL       8
