#ifdef LLVM_CODEGEN
#include "llvm_codegen.h"
#include "jdb_tags.h"
#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/Error.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <cstring>


// ── Constructor / Destructor ────────────────────────────────

LLVMCodegen::LLVMCodegen()
    : ctx(nullptr), module(nullptr), builder(nullptr), current_fn(nullptr) {}

LLVMCodegen::~LLVMCodegen() {
    if (builder) LLVMDisposeBuilder(builder);
    if (module) LLVMDisposeModule(module);
    if (ctx) LLVMContextDispose(ctx);
}

// ── Scope / Variable Management ─────────────────────────────

LLVMCodegen::VarInfo* LLVMCodegen::lookup_var(const std::string& name) {
    // Search from innermost scope outward
    for (int i = (int)scopes.size() - 1; i >= 0; i--) {
        auto it = scopes[i].vars.find(name);
        if (it != scopes[i].vars.end()) return &it->second;
    }
    return nullptr;
}

LLVMCodegen::VarInfo& LLVMCodegen::create_var(const std::string& name, int tag) {
    LLVMTypeRef var_type = (tag == JD_TAG_F64)        ? f64_type :
                           (tag == JD_TAG_STR)        ? i8_ptr_type :
                           (tag == JD_TAG_ARR)        ? i8_ptr_type :
                           (tag == JD_TAG_NATIVE_MAP) ? i8_ptr_type :
                           (tag == JD_TAG_FUNCREF)    ? i8_ptr_type : i64_type;

    LLVMValueRef storage;

    if (scopes.size() <= 1) {
        // Global scope — use LLVM module-level global variable
        storage = LLVMAddGlobal(module, var_type, name.c_str());
        LLVMSetInitializer(storage, LLVMConstNull(var_type));
        LLVMSetLinkage(storage, LLVMInternalLinkage);
    } else {
        // Local scope — use alloca in entry block of current function
        LLVMBasicBlockRef current_block = LLVMGetInsertBlock(builder);
        LLVMBasicBlockRef entry_block = LLVMGetEntryBasicBlock(current_fn);

        LLVMValueRef first_instr = LLVMGetFirstInstruction(entry_block);
        if (first_instr)
            LLVMPositionBuilderBefore(builder, first_instr);
        else
            LLVMPositionBuilderAtEnd(builder, entry_block);

        storage = LLVMBuildAlloca(builder, var_type, name.c_str());

        LLVMPositionBuilderAtEnd(builder, current_block);
    }

    auto& vi = scopes.back().vars[name];
    vi = { storage, tag };
    return vi;
}

// ── Setup ───────────────────────────────────────────────────

void LLVMCodegen::init_module() {
    ctx = LLVMContextCreate();
    module = LLVMModuleCreateWithNameInContext("jdbasic", ctx);
    builder = LLVMCreateBuilderInContext(ctx);
    LLVMSetTarget(module, "x86_64-pc-windows-msvc");

    i64_type = LLVMInt64TypeInContext(ctx);
    f64_type = LLVMDoubleTypeInContext(ctx);
    i32_type = LLVMInt32TypeInContext(ctx);
    i8_ptr_type = LLVMPointerTypeInContext(ctx, 0);
    void_type = LLVMVoidTypeInContext(ctx);
}

void LLVMCodegen::declare_runtime_functions() {
    // Helper to register a runtime function
    // Reuses existing LLVM function declaration if the C name was already added
    std::unordered_map<std::string, std::pair<LLVMValueRef, LLVMTypeRef>> declared;
    auto reg = [&](const std::string& name, const std::string& jdb_name,
                   LLVMTypeRef ret, std::vector<LLVMTypeRef> params, int ret_tag) {
        LLVMTypeRef ft = LLVMFunctionType(ret, params.empty() ? nullptr : params.data(),
                                           (unsigned)params.size(), 0);
        LLVMValueRef fn;
        auto it = declared.find(name);
        if (it != declared.end()) {
            fn = it->second.first;
            ft = it->second.second;
        } else {
            fn = LLVMAddFunction(module, name.c_str(), ft);
            declared[name] = {fn, ft};
        }
        runtime_funcs[jdb_name] = { fn, ft, ret_tag };
    };

    // I/O
    reg("jdb_print_int",    "__print_int",    void_type, {i64_type}, -1);
    reg("jdb_print_double", "__print_double",  void_type, {f64_type}, -1);
    reg("jdb_print_str",    "__print_str",     void_type, {i8_ptr_type}, -1);
    reg("jdb_print_bool",   "__print_bool",    void_type, {i64_type}, -1);
    reg("jdb_print_nl",     "__print_nl",      void_type, {}, -1);
    reg("jdb_print_space",  "__print_space",   void_type, {}, -1);
    reg("jdb_str_bool",     "__str_bool",      i8_ptr_type, {i64_type}, 2);
    reg("jdb_str_str",      "__str_str",       i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_map_str",      "__map_str",       i8_ptr_type, {i8_ptr_type}, 2);

    // String operations
    reg("jdb_str_concat",   "__str_concat",    i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_int_to_str",   "__int_to_str",    i8_ptr_type, {i64_type}, 2);
    reg("jdb_double_to_str","__double_to_str", i8_ptr_type, {f64_type}, 2);

    // Math (double -> double)
    reg("jdb_abs",    "ABS",    f64_type, {f64_type}, 1);
    reg("jdb_sqr",    "SQR",    f64_type, {f64_type}, 1);
    reg("jdb_sin",    "SIN",    f64_type, {f64_type}, 1);
    reg("jdb_cos",    "COS",    f64_type, {f64_type}, 1);
    reg("jdb_tan",    "TAN",    f64_type, {f64_type}, 1);
    reg("jdb_asin",   "ASIN",   f64_type, {f64_type}, 1);
    reg("jdb_acos",   "ACOS",   f64_type, {f64_type}, 1);
    reg("jdb_atan",   "ATAN",   f64_type, {f64_type}, 1);
    reg("jdb_log",    "LOG",    f64_type, {f64_type}, 1);
    reg("jdb_log10",  "LOG10",  f64_type, {f64_type}, 1);
    reg("jdb_exp",    "EXP",    f64_type, {f64_type}, 1);
    reg("jdb_floor",  "FLOOR",  f64_type, {f64_type}, 1);
    reg("jdb_ceil",   "CEIL",   f64_type, {f64_type}, 1);
    reg("jdb_pow",    "__pow",  f64_type, {f64_type, f64_type}, 1);

    // Math extended
    reg("jdb_sinh",   "SINH",   f64_type, {f64_type}, 1);
    reg("jdb_cosh",   "COSH",   f64_type, {f64_type}, 1);
    reg("jdb_tanh",   "TANH",   f64_type, {f64_type}, 1);
    reg("jdb_atan2",  "ATAN2",  f64_type, {f64_type, f64_type}, 1);
    reg("jdb_round",  "ROUND",  f64_type, {f64_type}, 1);
    reg("jdb_round_p","__round_p", f64_type, {f64_type, f64_type}, 1);
    reg("jdb_join_arr","JOIN",     i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_trunc",  "TRUNC",  f64_type, {f64_type}, 1);
    reg("jdb_sign",   "SIGN",   f64_type, {f64_type}, 1);
    reg("jdb_sign",   "SGN",    f64_type, {f64_type}, 1);
    reg("jdb_clamp",  "CLAMP",  f64_type, {f64_type, f64_type, f64_type}, 1);
    reg("jdb_fac",    "FAC",    f64_type, {f64_type}, 1);
    reg("jdb_fmod",   "FMOD",   f64_type, {f64_type, f64_type}, 1);
    reg("jdb_min2",   "MIN",    f64_type, {f64_type, f64_type}, 1);
    reg("jdb_max2",   "MAX",    f64_type, {f64_type, f64_type}, 1);
    reg("jdb_pi",     "PI",     f64_type, {}, 1);
    reg("jdb_e",      "E",      f64_type, {}, 1);

    // Math (special)
    reg("jdb_int",    "INT",    i64_type, {f64_type}, 0);
    reg("jdb_val",    "VAL",    f64_type, {i8_ptr_type}, 1);
    reg("jdb_rnd",    "RND",    f64_type, {}, 1);
    reg("jdb_rnd",    "RANDOM", f64_type, {}, 1);

    // System
    reg("jdb_tick",       "TICK",       f64_type, {}, 1);
    reg("jdb_sleep",      "SLEEP",      void_type, {i64_type}, -1);
    reg("jdb_randomseed", "RANDOMSEED", void_type, {i64_type}, -1);

    // Arrays (JdbArray* is opaque pointer = i8_ptr_type)
    reg("jdb_array_new",  "__array_new",  i8_ptr_type, {i64_type}, 3);
    reg("jdb_array_set",  "__array_set",  void_type, {i8_ptr_type, i64_type, f64_type}, -1);
    reg("jdb_array_get",  "__array_get",  f64_type, {i8_ptr_type, i64_type}, 1);
    // Fancy/vector indexing helper: arr[indices_array] → new array.
    reg("jdb_array_gather", "__array_gather", i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 1);
    reg("jdb_array_len",  "LEN",          i64_type, {i8_ptr_type}, 0);
    reg("jdb_iota",       "IOTA",         i8_ptr_type, {i64_type}, 3);
    reg("jdb_iota3",      "__iota3",      i8_ptr_type, {f64_type, f64_type, f64_type}, 3);
    reg("jdb_array_pop",    "__arr_pop",     f64_type,    {i8_ptr_type}, 1);
    reg("jdb_array_pop_str","__arr_pop_str", i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_zeros",      "ZEROS",        i8_ptr_type, {i64_type}, 3);
    reg("jdb_ones",       "ONES",         i8_ptr_type, {i64_type}, 3);
    reg("jdb_mean",       "MEAN",         f64_type, {i8_ptr_type}, 1);
    reg("jdb_stdev",      "STDEV",        f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_median","MEDIAN",      f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_variance","VARIANCE",  f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_sum",  "SUM",          f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_product","PRODUCT",    f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_min",  "__arr_min",    f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_max",  "__arr_max",    f64_type, {i8_ptr_type}, 1);
    reg("jdb_array_any",  "ANY",          i64_type, {i8_ptr_type}, 0);
    reg("jdb_array_all",  "ALL",          i64_type, {i8_ptr_type}, 0);
    reg("jdb_array_dot",  "DOT",          f64_type, {i8_ptr_type, i8_ptr_type}, 1);
    // Unary array math: bypass the function-pointer-callback applier so
    // the compiler can inline + vectorise the inner loop.
    reg("jdb_array_sin",   "__arr_sin",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_cos",   "__arr_cos",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_tan",   "__arr_tan",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_asin",  "__arr_asin",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_acos",  "__arr_acos",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_atan",  "__arr_atan",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_sinh",  "__arr_sinh",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_cosh",  "__arr_cosh",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_tanh",  "__arr_tanh",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_exp",   "__arr_exp",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_log",   "__arr_log",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_log10", "__arr_log10", i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_sqr",   "__arr_sqr",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_abs",   "__arr_abs",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_floor", "__arr_floor", i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_ceil",  "__arr_ceil",  i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_round", "__arr_round", i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_trunc", "__arr_trunc", i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_reverse","REVERSE",    i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_sort", "SORT",         i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_append","APPEND",      i8_ptr_type, {i8_ptr_type, f64_type}, 3);
    reg("jdb_array_append_arr","__append_arr", i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_fillv", "FILLV",       i8_ptr_type, {i8_ptr_type, f64_type}, 3);
    reg("jdb_array_copyv", "COPYV",       i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    // Tagged variants — preserve per-element JdTag so arr[i] reads return
    // the right RUNTIME-tag for downstream coerce_to / TYPEOF / FMT$.
    reg("jdb_array_append_tagged","__arr_append_tagged",
        i8_ptr_type, {i8_ptr_type, f64_type, i32_type}, -1);
    reg("jdb_array_get_tagged","__arr_get_tagged",
        f64_type, {i8_ptr_type, i64_type, i8_ptr_type}, -1);
    reg("jdb_array_count","COUNT",        i64_type, {i8_ptr_type, f64_type}, 0);
    reg("jdb_array_indexof","INDEXOF",    i64_type, {i8_ptr_type, f64_type}, 0);
    reg("jdb_array_has_str","__arr_has_str", i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_array_has_num","__arr_has_num", i64_type, {i8_ptr_type, f64_type}, 0);
    reg("jdb_array_unique","UNIQUE",      i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_unique_str","__unique_str", i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_cumsum","CUMSUM",      i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_cumprod","CUMPROD",    i8_ptr_type, {i8_ptr_type}, 3);
    // TAKE / DROP — interpreter signature is (n, arr); the (arr, n) variant
    // jdb_array_take / jdb_array_drop stays for any internal callers.
    reg("jdb_take_n",     "TAKE",         i8_ptr_type, {i64_type, i8_ptr_type}, 3);
    reg("jdb_drop_n",     "DROP",         i8_ptr_type, {i64_type, i8_ptr_type}, 3);
    reg("jdb_array_diff", "DIFF",         i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_flatten","FLATTEN",    i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_shuffle","SHUFFLE",    i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_linspace",   "LINSPACE",     i8_ptr_type, {f64_type, f64_type, i64_type}, 3);
    reg("jdb_range",      "RANGE",        i8_ptr_type, {i64_type, i64_type, i64_type}, 3);
    reg("jdb_grade",      "GRADE",        i8_ptr_type, {i8_ptr_type}, 3);

    // Array arithmetic (native, no VM bridge needed)
    reg("jdb_array_binop",       "__arr_binop",       i8_ptr_type, {i8_ptr_type, i8_ptr_type, i32_type}, 3);
    reg("jdb_array_scalar_op",   "__arr_scalar_op",   i8_ptr_type, {i8_ptr_type, f64_type, i32_type, i32_type}, 3);
    reg("jdb_array_cmp_scalar",  "__arr_cmp_scalar",  i8_ptr_type, {i8_ptr_type, f64_type, i32_type}, 3);
    reg("jdb_array_cmp_scalar_str","__arr_cmp_scalar_str", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i32_type}, 3);
    reg("jdb_array_cmp_arr",     "__arr_cmp_arr",     i8_ptr_type, {i8_ptr_type, i8_ptr_type, i32_type}, 3);
    reg("jdb_array_set_nested",  "__arr_set_nested",  void_type, {i8_ptr_type}, -1);
    reg("jdb_array_set_string_elems", "__arr_set_string_elems", void_type, {i8_ptr_type}, -1);
    reg("jdb_array_set_bool_elems", "__arr_set_bool_elems", void_type, {i8_ptr_type}, -1);
    reg("jdb_array_classify_elem", "__arr_classify", i32_type, {i8_ptr_type, f64_type}, 2);
    reg("jdb_str_repeat", "__str_repeat", i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_setlocale",  "SETLOCALE",    void_type, {i8_ptr_type}, -1);
    // Maps / Objects
    reg("jdb_map_new",    "__map_new",    i8_ptr_type, {}, 4);
    reg("jdb_map_set_f64","__map_set_f64",void_type,   {i8_ptr_type, i8_ptr_type, f64_type}, -1);
    reg("jdb_map_set_str","__map_set_str",void_type,   {i8_ptr_type, i8_ptr_type, i8_ptr_type}, -1);
    reg("jdb_map_set_tagged","__map_set_tagged",void_type, {i8_ptr_type, i8_ptr_type, f64_type, i32_type}, -1);
    reg("jdb_map_get_f64","__map_get_f64",f64_type,    {i8_ptr_type, i8_ptr_type}, 1);
    reg("jdb_map_get_str","__map_get_str",i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_map_has",    "__map_has",    i64_type,    {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_map_get_obj","__map_get_obj",i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 4);
    reg("jdb_str_sub",    "__str_sub",    i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 2);
    // Native generic vectorization helpers (avoid VM bridge overhead)
    reg("jdb_array_apply_ff",   "__arr_apply_ff",   i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_ss",   "__arr_apply_ss",   i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_ifs",  "__arr_apply_ifs",  i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_sfi",  "__arr_apply_sfi",  i8_ptr_type, {i8_ptr_type, i64_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_sfii", "__arr_apply_sfii", i8_ptr_type, {i8_ptr_type, i64_type, i64_type, i8_ptr_type}, 3);
    reg("jdb_array_len_shape",   "__arr_len_shape",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_print_array_elem",  "__print_arr_elem",  void_type, {i8_ptr_type, i64_type}, -1);
    reg("jdb_array_str_concat",  "__arr_str_concat",  i8_ptr_type, {i8_ptr_type, i8_ptr_type, i32_type}, 3);
    reg("jdb_trace",             "__trace",           void_type, {i8_ptr_type, i64_type}, -1);

    // Exception state — THROW stores msg/code, CATCH reads via ERRMSG$/ERR.
    reg("jdb_err_set",       "__err_set",   void_type,   {i8_ptr_type, i64_type}, -1);
    reg("jdb_err_clear",     "__err_clear", void_type,   {}, -1);
    reg("jdb_err_msg",          "ERRMSG$",  i8_ptr_type, {}, 2);
    // ERR → user-visible reader (with shadow fallback). The raw-code
    // getter below is what emit_err_check's propagation loop calls.
    reg("jdb_err_code_visible", "ERR",      i64_type,    {}, 0);
    reg("jdb_err_code",         "__err_rc", i64_type,    {}, 0);
    reg("jdb_throw_uncaught","__throw_uncaught", void_type, {}, -1);
    // Clears only g_err_code, leaves g_err_msg alone (so ERRMSG$ still
    // works inside a catch body).
    reg("jdb_err_code_clear","__err_code_clear", void_type, {}, -1);

    // Recursion guard — one enter at each user FUNC/SUB entry, one leave
    // on the common exit path. __rec_depth snapshots the counter on TRY
    // entry so CATCH can restore it after an error unwinds through
    // missed leaves.
    reg("jdb_recursion_enter",    "__rec_enter",   i32_type, {}, -1);
    reg("jdb_recursion_leave",    "__rec_leave",   void_type, {}, -1);
    reg("jdb_recursion_reset_to", "__rec_reset",   void_type, {i64_type}, -1);
    reg("jdb_recursion_depth",    "__rec_depth",   i64_type, {}, 0);

    // Event system
    reg("jdb_event_on",       "__event_on",      void_type, {i8_ptr_type, i8_ptr_type}, -1);
    reg("jdb_event_raise_str","__event_raise_s", void_type, {i8_ptr_type, i8_ptr_type}, -1);

    // Native-mode event dispatch trampoline (jdb_runtime.cpp). Each
    // ON "X" CALL H statement emits a jdrt_register_event_handler call;
    // the trampoline (jdrt_dispatch_event) is wired to the bridge once
    // at startup via jdrt_set_event_dispatcher.
    reg("jdrt_register_event_handler", "__jdrt_reg_evh",
        void_type, {i8_ptr_type, i8_ptr_type}, -1);
    reg("jdrt_dispatch_event", "__jdrt_dispatch_evt",
        void_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type}, -1);
    reg("jdrt_set_event_dispatcher", "__jdrt_set_evt_disp",
        void_type, {i8_ptr_type, i8_ptr_type}, -1);

    // OS.FEATURE — query whether a build feature is present in this binary
    reg("jdb_os_feature",     "OS.FEATURE",      i64_type, {i8_ptr_type}, 0);

    // OS
    reg("jdb_set_args",   "__set_args",   void_type, {i32_type, i8_ptr_type}, -1);
    reg("jdb_os_args",    "OS.ARGS",      i8_ptr_type, {}, 3);
    reg("jdb_array_get_str", "__array_get_str", i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_val_ptr",    "__val_ptr",    f64_type, {f64_type}, 1);

    // FORMAT$ (1-4 args)
    reg("jdb_format1", "__format1", i8_ptr_type, {i8_ptr_type, f64_type}, 2);
    reg("jdb_format2", "__format2", i8_ptr_type, {i8_ptr_type, f64_type, f64_type}, 2);
    reg("jdb_format3", "__format3", i8_ptr_type, {i8_ptr_type, f64_type, f64_type, f64_type}, 2);
    reg("jdb_format4", "__format4", i8_ptr_type, {i8_ptr_type, f64_type, f64_type, f64_type, f64_type}, 2);
    reg("jdb_format1_t", "__format1_t", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i64_type}, 2);
    reg("jdb_format2_t", "__format2_t", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i64_type, i64_type}, 2);
    reg("jdb_format3_t", "__format3_t", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i64_type, i64_type, i64_type}, 2);
    reg("jdb_format4_t", "__format4_t", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i64_type, i64_type, i64_type, i64_type}, 2);
    // Hand the VM-bridge handle over to jdb_runtime.obj so format-tagged
    // 'h' args (VM handles) can call into jdrt_val_to_f64 / val_to_str.
    reg("jdb_runtime_set_handle", "__runtime_set_handle", void_type, {i8_ptr_type}, 1);

    // String builtins
    reg("jdb_len_str",  "LEN$",     i64_type, {i8_ptr_type}, 0);
    reg("jdb_mid_lax",  "MID$",     i8_ptr_type, {i8_ptr_type, i64_type, i64_type}, 2);
    reg("jdb_mid",      "MID",      i8_ptr_type, {i8_ptr_type, i64_type, i64_type}, 2);
    reg("jdb_left",     "LEFT$",    i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_left",     "LEFT",     i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_right",    "RIGHT$",   i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_right",    "RIGHT",    i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_upper",    "UPPER$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_lower",    "LOWER$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_trim",     "TRIM$",    i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_chr",      "CHR$",     i8_ptr_type, {i64_type}, 2);
    reg("jdb_chr",      "CHR",      i8_ptr_type, {i64_type}, 2);
    reg("jdb_asc",      "ASC",      i64_type, {i8_ptr_type}, 0);
    reg("jdb_instr",    "INSTR",    i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_replace",  "REPLACE$", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_str",      "STR$",     i8_ptr_type, {f64_type}, 2);
    reg("jdb_array_str","__str_arr",i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_str",      "STR",      i8_ptr_type, {f64_type}, 2);
    reg("jdb_space",    "SPACE$",   i8_ptr_type, {i64_type}, 2);
    reg("jdb_str_eq",   "__str_eq",  i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_str_ne",   "__str_ne",  i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_ltrim",    "LTRIM$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_rtrim",    "RTRIM$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_startswith","STARTSWITH", i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_endswith", "ENDSWITH", i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_hex",      "HEX$",    i8_ptr_type, {i64_type}, 2);
    reg("jdb_bin",      "BIN$",    i8_ptr_type, {i64_type}, 2);
    reg("jdb_oct",      "OCT$",    i8_ptr_type, {i64_type}, 2);
    reg("jdb_insert_str","INSERT$", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i64_type}, 2);
    // Aliases
    reg("jdb_upper",    "UCASE$",  i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_upper",    "UCASE",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_lower",    "LCASE$",  i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_lower",    "LCASE",   i8_ptr_type, {i8_ptr_type}, 2);

    // File I/O
    reg("jdb_txtreader",       "TXTREADER$",  i8_ptr_type, {i8_ptr_type}, 2);
    // 3-arg TXTWRITER: 2-arg calls pad 0 → no append, 3-arg picks append
    reg("jdb_txtwriter3",      "TXTWRITER",   void_type, {i8_ptr_type, i8_ptr_type, i64_type}, -1);
    reg("jdb_txtwriter_append","TXTWRITER_APPEND", void_type, {i8_ptr_type, i8_ptr_type}, -1);
    // Codepage-aware variants. The codegen routes TXTREADER$/TXTWRITER calls
    // here when an encoding arg is present (see the upper-rewrite block in
    // codegen_call). Underscore prefix keeps them out of the user namespace.
    reg("jdb_txtreader_enc",   "__TXTREADER_ENC", i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_txtwriter_enc",   "__TXTWRITER_ENC", void_type,
        {i8_ptr_type, i8_ptr_type, i64_type, i8_ptr_type}, -1);
    reg("jdb_pwd",             "PWD",         i8_ptr_type, {}, 2);
    reg("jdb_cd",              "CD",          i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_mkdir_native",    "MKDIR",       void_type, {i8_ptr_type}, -1);
    reg("jdb_rmdir",           "RMDIR",       void_type, {i8_ptr_type}, -1);
    reg("jdb_kill",            "KILL",        void_type, {i8_ptr_type}, -1);
    reg("jdb_file_exists",     "FILE.EXISTS", i64_type, {i8_ptr_type}, 0);
    reg("jdb_file_size",       "FILE.SIZE",   i64_type, {i8_ptr_type}, 0);
    reg("jdb_file_isdir",      "FILE.ISDIR",  i64_type, {i8_ptr_type}, 0);
    reg("jdb_path_dirname",    "PATH.DIRNAME$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_path_normalize",  "PATH.NORMALIZE$", i8_ptr_type, {i8_ptr_type}, 2);

    // Date/Time
    reg("jdb_now",         "NOW",         i8_ptr_type, {}, 2);
    reg("jdb_now_epoch",   "NOW_EPOCH",   f64_type, {}, 1);
    reg("jdb_date_str",    "DATE$",       i8_ptr_type, {f64_type}, 2);
    reg("jdb_time_str",    "TIME$",       i8_ptr_type, {f64_type}, 2);
    reg("jdb_year",        "YEAR",        i64_type, {f64_type}, 0);
    reg("jdb_month",       "MONTH",       i64_type, {f64_type}, 0);
    reg("jdb_day",         "DAY",         i64_type, {f64_type}, 0);
    reg("jdb_hour",        "HOUR",        i64_type, {f64_type}, 0);
    reg("jdb_minute",      "MINUTE",      i64_type, {f64_type}, 0);
    reg("jdb_second",      "SECOND",      i64_type, {f64_type}, 0);
    reg("jdb_weekday",     "WEEKDAY",     i64_type, {f64_type}, 0);
    // String-based date accessors (for ISO strings from CVDATE/DATEADD)
    reg("jdb_year_str",    "__year_str",   i64_type, {i8_ptr_type}, 0);
    reg("jdb_month_str",   "__month_str",  i64_type, {i8_ptr_type}, 0);
    reg("jdb_day_str",     "__day_str",    i64_type, {i8_ptr_type}, 0);
    reg("jdb_hour_str",    "__hour_str",   i64_type, {i8_ptr_type}, 0);
    reg("jdb_minute_str",  "__minute_str", i64_type, {i8_ptr_type}, 0);
    reg("jdb_second_str",  "__second_str", i64_type, {i8_ptr_type}, 0);
    reg("jdb_format_date", "FORMAT_DATE", i8_ptr_type, {i8_ptr_type, i8_ptr_type, f64_type}, 2);

    // System
    reg("jdb_getenv",  "GETENV$",  i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_setenv",  "SETENV",   void_type,   {i8_ptr_type, i8_ptr_type}, -1);
    reg("jdb_mktemp",  "MKTEMP$",  i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_iif",     "IIF",      f64_type, {i64_type, f64_type, f64_type}, 1);
    reg("jdb_isnum",   "ISNUM",    i64_type, {f64_type}, 0);

    // Bit rotation (2-arg, implicit 64-bit width; 3-arg falls back to VM bridge)
    reg("jdb_rotl2",   "ROTL",     i64_type, {i64_type, i64_type}, 0);
    reg("jdb_rotr2",   "ROTR",     i64_type, {i64_type, i64_type}, 0);
    // GCD/LCM — variadic in VM, 2-arg native form reachable via VM bridge

    // String padding (jdb_str_repeat already declared above as __str_repeat)
    reg("jdb_str_repeat", "REPEAT$",  i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    // 3-arg bindings; 2-arg calls pad null for pad and jdb_lpad treats null as " "
    reg("jdb_lpad",       "LPAD$",    i8_ptr_type, {i8_ptr_type, i64_type, i8_ptr_type}, 2);
    reg("jdb_rpad",       "RPAD$",    i8_ptr_type, {i8_ptr_type, i64_type, i8_ptr_type}, 2);

    // Codec
    reg("jdb_base64_encode", "CODEC.BASE64_ENCODE$", i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_base64_decode", "CODEC.BASE64_DECODE$", i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_uuid",          "CODEC.UUID$",          i8_ptr_type, {}, 2);
    reg("jdb_sha256",        "CODEC.SHA256$",        i8_ptr_type, {i8_ptr_type}, 2);

    // UDT (User-Defined Types)
    reg("jdb_udt_new",     "__udt_new",     i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_udt_set_f64", "__udt_set_f64", void_type, {i8_ptr_type, i8_ptr_type, f64_type}, -1);
    reg("jdb_udt_get_f64", "__udt_get_f64", f64_type, {i8_ptr_type, i8_ptr_type}, 1);
    reg("jdb_udt_set_str", "__udt_set_str", void_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, -1);
    reg("jdb_udt_get_str", "__udt_get_str", i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 2);

    // Higher-order functions (take function pointers)
    // jdb_select_fn(fn_ptr, array) -> array
    reg("jdb_select_fn", "__select_fn", i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    // jdb_filter_fn(fn_ptr, array) -> array
    reg("jdb_filter_fn", "__filter_fn", i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    // jdb_reduce_fn(fn_ptr, array, init) -> double
    reg("jdb_reduce_fn", "__reduce_fn", f64_type, {i8_ptr_type, i8_ptr_type, f64_type}, 1);

    // VM Bridge (for builtins not in the static runtime)
    reg("jdrt_init",     "__jdrt_init",     i8_ptr_type, {}, -1);
    reg("jdrt_shutdown", "__jdrt_shutdown", void_type, {i8_ptr_type}, -1);
    // Typed calls: args as i64[], tags as i32[]
    reg("jdrt_call_typed_f64",  "__jdrt_call_typed_f64",  f64_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type}, 1);
    reg("jdrt_call_typed_str",  "__jdrt_call_typed_str",  i8_ptr_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type}, 2);
    reg("jdrt_call_typed_void", "__jdrt_call_typed_void", void_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type}, -1);
    reg("jdrt_call_typed_obj",  "__jdrt_call_typed_obj",  i64_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type}, 0);
    reg("jdrt_call_typed_arr",  "__jdrt_call_typed_arr",  i8_ptr_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type}, 3);
    // Field access on VM Value handles (objects from JSON.PARSE$, MAP.* etc.)
    reg("jdrt_obj_get_f64", "__jdrt_obj_get_f64", f64_type,
        {i8_ptr_type, i64_type, i8_ptr_type}, 1);
    reg("jdrt_obj_get_str", "__jdrt_obj_get_str", i8_ptr_type,
        {i8_ptr_type, i64_type, i8_ptr_type}, 2);
    reg("jdrt_obj_get_obj", "__jdrt_obj_get_obj", i64_type,
        {i8_ptr_type, i64_type, i8_ptr_type}, 0);
    reg("jdrt_obj_get_arr", "__jdrt_obj_get_arr", i8_ptr_type,
        {i8_ptr_type, i64_type, i8_ptr_type}, 3);
    reg("jdrt_obj_exists",  "__jdrt_obj_exists",  i64_type,
        {i8_ptr_type, i64_type, i8_ptr_type}, 0);
    reg("jdrt_map_to_handle", "__jdrt_map_to_handle", i64_type,
        {i8_ptr_type, i8_ptr_type}, 0);
    // ASYNC FUNC dispatch — handle, fn ptr, args ptr (f64*), nargs (i32),
    // return_tag (i32). Returns task id (i64).
    reg("jdrt_async_spawn", "__jdrt_async_spawn", i64_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type, i32_type, i32_type}, 0);
    reg("jdrt_val_to_f64",  "__jdrt_val_to_f64",  f64_type,
        {i8_ptr_type, i64_type}, 1);
    reg("jdrt_val_to_str",  "__jdrt_val_to_str",  i8_ptr_type,
        {i8_ptr_type, i64_type}, 2);
    reg("jdrt_val_arr_get", "__jdrt_val_arr_get", i64_type,
        {i8_ptr_type, i64_type, i64_type}, 0);
    reg("jdrt_val_length",  "__jdrt_val_length",  i64_type,
        {i8_ptr_type, i64_type}, 0);
    // Tagged value getters: return tag (i32), write val to i64* out param.
    reg("jdb_map_get_tagged",  "__map_get_tagged",  i32_type,
        {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 0);
    reg("jdrt_obj_get_tagged", "__jdrt_obj_get_tagged", i32_type,
        {i8_ptr_type, i64_type, i8_ptr_type, i8_ptr_type}, 0);
    // Unified tagged dispatchers (handle both native map + VM handles)
    reg("jdrt_tagged_get",     "__jdrt_tagged_get",     i32_type,
        {i8_ptr_type, i64_type, i32_type, i8_ptr_type, i8_ptr_type}, 0);
    reg("jdrt_tagged_arr_get", "__jdrt_tagged_arr_get", i32_type,
        {i8_ptr_type, i64_type, i32_type, i64_type, i8_ptr_type}, 0);
    reg("jdrt_promote_handle", "__jdrt_promote_handle", i64_type,
        {i8_ptr_type, i64_type}, 0);
    reg("jdrt_frame_begin", "__jdrt_frame_begin", i64_type,
        {i8_ptr_type}, 0);
    reg("jdrt_frame_end",   "__jdrt_frame_end",   void_type,
        {i8_ptr_type, i64_type}, -1);
    reg("jdrt_last_error",  "__jdrt_last_error",  i8_ptr_type,
        {i8_ptr_type}, 2);
    reg("jdrt_clear_last_error", "__jdrt_clear_last_error", void_type,
        {i8_ptr_type}, -1);

    // Date Add/Diff
    // Dates are ISO strings in the native runtime (not epochs like the VM).
    reg("jdb_dateadd",  "DATEADD",  i8_ptr_type, {i8_ptr_type, f64_type, i8_ptr_type}, 2);
    reg("jdb_datediff", "DATEDIFF", f64_type,    {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 1);
    reg("jdb_datediff_vec", "__datediff_vec", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_cvdate",     "CVDATE",       i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_cvdate",     "CDATE",        i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_cvdate_num", "__cvdate_num", i8_ptr_type, {f64_type},    2);
    reg("jdb_cvdate_arr", "__cvdate_arr", i8_ptr_type, {i8_ptr_type}, 3);

    // Regex
    reg("jdb_regex_match",   "REGEX.MATCH",   i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    // Note: REGEX_MATCH (legacy name) returns an array in the VM, so it must
    // go through the VM bridge — don't register it as the boolean native fn.
    reg("jdb_regex_replace", "REGEX.REPLACE",  i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_regex_replace", "REGEX_REPLACE$", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_regex_findall", "REGEX.FINDALL",  i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);

    // TYPEOF (compile-time tag)
    reg("jdb_typeof_tag", "__typeof_tag", i8_ptr_type, {i64_type}, 2);
    reg("jdb_typeof_f64", "__typeof_f64", i8_ptr_type, {f64_type}, 2);

    // FRMV$ (format array)
    reg("jdb_frmv", "FRMV$", i8_ptr_type, {i8_ptr_type}, 2);

    // Misc
    reg("jdb_cdbl",     "CDBL",       f64_type, {f64_type}, 1);
    reg("jdb_cint",     "CINT",       i64_type, {f64_type}, 0);
    reg("jdb_clng",     "CLNG",       i64_type, {f64_type}, 0);
    reg("jdb_csng",     "CSNG",       f64_type, {f64_type}, 1);
    reg("jdb_cbool",    "CBOOL",      i64_type, {f64_type}, 0);
    reg("jdb_tostr",    "TOSTR",      i8_ptr_type, {f64_type}, 2);
    reg("jdb_cstr",     "CSTR",       i8_ptr_type, {f64_type}, 2);
    reg("jdb_tonum",    "TONUM",      f64_type, {i8_ptr_type}, 1);
    reg("jdb_byteat",   "BYTEAT",     i64_type, {i8_ptr_type, i64_type}, 0);
    reg("jdb_os_getos", "OS.GETOS",   i8_ptr_type, {}, 2);
    reg("jdb_os_getos", "OS.GETOS$",  i8_ptr_type, {}, 2);
    reg("jdb_os_hostname","OS.HOSTNAME$", i8_ptr_type, {}, 2);
}

void LLVMCodegen::create_main_function() {
    // int main(int argc, char** argv)
    LLVMTypeRef main_params[] = { i32_type, i8_ptr_type };  // argv is char** = ptr
    LLVMTypeRef main_ft = LLVMFunctionType(i32_type, main_params, 2, 0);
    current_fn = LLVMAddFunction(module, "main", main_ft);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, current_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);
    scopes.push_back(Scope{});  // global scope

    // Call jdb_set_args(argc, argv) at program start
    auto set_args_it = runtime_funcs.find("__set_args");
    if (set_args_it != runtime_funcs.end()) {
        LLVMValueRef args[] = { LLVMGetParam(current_fn, 0), LLVMGetParam(current_fn, 1) };
        LLVMBuildCall2(builder, set_args_it->second.fn_type, set_args_it->second.fn, args, 2, "");
    }

    // Initialize VM bridge: g_jdrt_handle = jdrt_init()
    LLVMValueRef jdrt_global = LLVMAddGlobal(module, i8_ptr_type, "__jdrt_handle");
    LLVMSetInitializer(jdrt_global, LLVMConstNull(i8_ptr_type));
    LLVMSetLinkage(jdrt_global, LLVMInternalLinkage);

    auto init_it = runtime_funcs.find("__jdrt_init");
    if (init_it != runtime_funcs.end()) {
        LLVMValueRef handle = LLVMBuildCall2(builder, init_it->second.fn_type,
                                              init_it->second.fn, nullptr, 0, "jdrt");
        LLVMBuildStore(builder, handle, jdrt_global);

        // Hand the same handle to jdb_runtime.obj so its FORMAT$ 'h' tag
        // can materialise VM_HANDLE args via jdrt_val_to_{f64,str}.
        auto rsh_it = runtime_funcs.find("__runtime_set_handle");
        if (rsh_it != runtime_funcs.end()) {
            LLVMValueRef args[] = { handle };
            LLVMBuildCall2(builder, rsh_it->second.fn_type, rsh_it->second.fn,
                           args, 1, "");
        }
    }

    // Wire the event-dispatcher trampoline into the bridge so that
    // ON-handlers raised by the bridge VM (KEYDOWN, QUIT, ...) reach
    // the LLVM-compiled handler bodies in this .exe.
    auto sed_it = runtime_funcs.find("__jdrt_set_evt_disp");
    auto disp_it = runtime_funcs.find("__jdrt_dispatch_evt");
    if (sed_it != runtime_funcs.end() && disp_it != runtime_funcs.end()) {
        LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, jdrt_global, "rt");
        LLVMValueRef dispatcher_ptr = LLVMBuildBitCast(builder, disp_it->second.fn,
                                                        i8_ptr_type, "disp_ptr");
        LLVMValueRef args[] = { rt, dispatcher_ptr };
        LLVMBuildCall2(builder, sed_it->second.fn_type, sed_it->second.fn, args, 2, "");
    }
}

// ── Pre-pass: declare all FUNC/SUB signatures ───────────────

void LLVMCodegen::declare_functions(const std::vector<StmtPtr>& program) {
    // Phase 1: collect function signatures with initial param types from name convention
    struct FuncDecl {
        const Stmt* stmt;
        std::vector<int> tags;  // per-param: 1=f64, 2=string
        int return_tag;
        // True when the function returns an array whose elements are
        // i8* string pointers. The callee's return_tag is JD_TAG_ARR
        // either way; this carries the per-cell hint that the codegen
        // INDEX/iter paths need to decode the punned-f64 cells back as
        // strings on the caller side.
        bool returns_string_array = false;
        bool is_async = false;
    };
    std::unordered_map<std::string, FuncDecl> decls;

    // Pre-pre-pass: walk the program collecting names of SUBs registered
    // as event handlers (`ON "X" CALL Handler` becomes a CALL to
    // __EVENT_ON("X", "Handler")). Their first param will be forced to
    // tag=3 (JdbArray*) so RAISEEVENT can pass packed args.
    std::function<void(const Expr&)> scan_event = [&](const Expr& e) {
        if (e.kind == ExprKind::CALL && e.func_name == "__EVENT_ON" &&
            e.args.size() >= 2 && e.args[1] &&
            e.args[1]->kind == ExprKind::LITERAL_STRING) {
            event_handler_subs.insert(e.args[1]->str_val);
        }
        if (e.left) scan_event(*e.left);
        if (e.right) scan_event(*e.right);
        for (auto& a : e.args) if (a) scan_event(*a);
    };
    std::function<void(const Stmt&)> scan_stmt_event = [&](const Stmt& s) {
        if (s.expr) scan_event(*s.expr);
        for (auto& b : s.body) if (b) scan_stmt_event(*b);
        for (auto& br : s.branches) for (auto& b : br.body) if (b) scan_stmt_event(*b);
        for (auto& c : s.catch_body) if (c) scan_stmt_event(*c);
    };
    for (auto& stmt : program) if (stmt) scan_stmt_event(*stmt);

    // Pre-pass: walk the program collecting FFI declarations
    // (`DECLARE FUNC name LIB ... AS rettype`, lowered by the parser to
    // `__FFI_DECLARE("NAME", "lib", "alias", [...], [...], "RET_TYPE")`).
    // The dispatch site at the bottom of codegen_call uses these sets to
    // route the call through jdrt_call_typed_arr / _str / _void; without
    // them every FFI call would be dispatched as f64-returning, which
    // collapses ARRAY-returning calls (RETURN buffers, packed results)
    // to 0.0 and STRING-returning calls to garbage.
    std::function<void(const Expr&)> scan_ffi = [&](const Expr& e) {
        if (e.kind == ExprKind::CALL && e.func_name == "__FFI_DECLARE" &&
            e.args.size() >= 6 &&
            e.args[0] && e.args[0]->kind == ExprKind::LITERAL_STRING &&
            e.args[5] && e.args[5]->kind == ExprKind::LITERAL_STRING) {
            std::string fn = e.args[0]->str_val;
            std::string ret = e.args[5]->str_val;
            std::transform(fn.begin(), fn.end(), fn.begin(),
                [](unsigned char c){ return (char)std::toupper(c); });
            std::transform(ret.begin(), ret.end(), ret.begin(),
                [](unsigned char c){ return (char)std::toupper(c); });
            if (ret == "ARRAY")        ffi_array_returners.insert(fn);
            else if (ret == "STRING")  ffi_string_returners.insert(fn);
            else if (ret == "VOID")    ffi_void_returners.insert(fn);
        }
        if (e.left) scan_ffi(*e.left);
        if (e.right) scan_ffi(*e.right);
        for (auto& a : e.args) if (a) scan_ffi(*a);
    };
    std::function<void(const Stmt&)> scan_stmt_ffi = [&](const Stmt& s) {
        if (s.expr) scan_ffi(*s.expr);
        for (auto& b : s.body) if (b) scan_stmt_ffi(*b);
        for (auto& br : s.branches) for (auto& b : br.body) if (b) scan_stmt_ffi(*b);
        for (auto& c : s.catch_body) if (c) scan_stmt_ffi(*c);
    };
    for (auto& stmt : program) if (stmt) scan_stmt_ffi(*stmt);

    for (auto& stmt : program) {
        if (!stmt) continue;
        if (stmt->kind != StmtKind::FUNCTION && stmt->kind != StmtKind::SUB) continue;
        bool is_sub = (stmt->kind == StmtKind::SUB);
        bool returns_string = (!is_sub && !stmt->func_name.empty() &&
                               stmt->func_name.back() == '$');
        int ret_tag = is_sub ? -1 : (returns_string ? 2 : 1);
        // Explicit `FUNC name(...) AS <type>` is authoritative — skip the
        // Phase 3 heuristic guesses entirely. Maps every VarType to its
        // JD_TAG_*. This is the supported way for FUNCs that return non-
        // numeric values to advertise their shape; without it the codegen
        // falls back to f64 and the caller bit-puns through a double.
        // INT/BOOL types reuse the f64 numeric default (jdBasic's native
        // numeric ABI is f64; the i64 promote path needs more codegen
        // plumbing — caller's `i64_local + i64_call` ends up `add i64 +
        // f64` because the FUNC sig stays f64. Tracked separately.).
        if (!is_sub) {
            switch (stmt->return_type) {
                case VarType::STRING:  ret_tag = JD_TAG_STR;  break;
                case VarType::ARRAY:
                case VarType::TENSOR:  ret_tag = JD_TAG_ARR;  break;
                case VarType::OBJECT:  ret_tag = JD_TAG_VM_HANDLE; break;
                default: break;  // numeric / BOOLEAN / NONE → keep heuristic
            }
        }
        std::vector<int> tags;
        bool is_event_handler = is_sub && event_handler_subs.count(stmt->func_name);
        for (size_t pi = 0; pi < stmt->params.size(); pi++) {
            auto& p = stmt->params[pi];
            bool sp = (!p.name.empty() && p.name.back() == '$');
            int t = sp ? 2 : 1;
            // Honour explicit `AS <type>` annotations on params. Without this,
            // `FUNC walk(path AS STRING)` decoded `path` as f64 garbage —
            // only `$`-suffixed names got the string slot. Same idea covers
            // ARRAY / OBJECT / MAP so a typed param doesn't silently down-
            // grade to a number.
            switch (p.type) {
                case VarType::STRING:                            t = 2; break;
                case VarType::ARRAY:                             t = 3; break;
                case VarType::OBJECT:                            t = 4; break;
                case VarType::ANY:                               t = 3; break;
                default: break;
            }
            if (is_event_handler && pi == 0) t = 3;  // event data: JdbArray*
            tags.push_back(t);
            if (p.type == VarType::ANY) mixed_array_vars.insert(p.name);
        }
        decls[stmt->func_name] = { stmt.get(), tags, ret_tag,
                                    /*returns_string_array=*/false,
                                    stmt->is_async_func };
    }

    // Phase 2: scan all call sites to infer string params from arguments
    // An arg is string if it's a LITERAL_STRING, a VARIABLE ending in $, or a CALL ending in $
    std::function<bool(const Expr&)> is_str_expr = [&](const Expr& e) -> bool {
        if (e.kind == ExprKind::LITERAL_STRING) return true;
        if (e.kind == ExprKind::VARIABLE && !e.str_val.empty() && e.str_val.back() == '$') return true;
        if (e.kind == ExprKind::CALL && !e.func_name.empty() && e.func_name.back() == '$') return true;
        // String concat (any + with a string operand)
        if (e.kind == ExprKind::BINARY && e.op == TokenType::PLUS) {
            if ((e.left && is_str_expr(*e.left)) || (e.right && is_str_expr(*e.right)))
                return true;
        }
        return false;
    };

    // Static pre-pass: derive a tag for each top-level global variable from
    // its DIM/LET RHS. declare_functions runs BEFORE codegen_program, so
    // lookup_var would otherwise return nullptr for globals on their first
    // scan, leaving FUNC(xs) parameters stuck at f64 even when xs is a
    // known array. Without this a callee's RESHAPE/MATMUL/SHIFT on the
    // param returns garbage natively.
    std::unordered_map<std::string, int> pre_var_tags;
    std::function<int(const Expr&)> infer_expr_tag = [&](const Expr& e) -> int {
        if (e.kind == ExprKind::ARRAY_LITERAL) return JD_TAG_ARR;
        if (e.kind == ExprKind::MAP_LITERAL)   return JD_TAG_NATIVE_MAP;
        if (e.kind == ExprKind::LITERAL_STRING) return JD_TAG_STR;
        if (e.kind == ExprKind::VARIABLE) {
            if (!e.str_val.empty() && e.str_val.back() == '$') return JD_TAG_STR;
            auto it = pre_var_tags.find(e.str_val);
            if (it != pre_var_tags.end()) return it->second;
            return JD_TAG_F64;
        }
        if (e.kind == ExprKind::CALL) {
            if (!e.func_name.empty() && e.func_name.back() == '$') return JD_TAG_STR;
            auto rit = runtime_funcs.find(e.func_name);
            if (rit != runtime_funcs.end()) return rit->second.return_tag;
            auto fit = decls.find(e.func_name);
            if (fit != decls.end()) {
                // ASYNC FUNC native call returns the task id (i64) regardless
                // of the user-declared return type — AWAIT yields the actual
                // value later. Without this, `DIM p = mini_prod(...)` typed
                // the slot from mini_prod's body return tag and stored the
                // task id's bits in the wrong slot shape.
                if (fit->second.is_async) return JD_TAG_I64;
                return fit->second.return_tag;
            }
            // Names like ZEROS/IOTA/RANGE/SHIFT/OUTER etc. are array-returners
            // known to the static native whitelist. Use the same list the
            // call-site uses later to tag VM-bridge returns.
            static const std::unordered_set<std::string> arr_fns = {
                "ZEROS", "ONES", "IOTA", "RANGE", "LINSPACE",
                "TAKE", "DROP", "UNIQUE", "REVERSE", "FLATTEN", "SHUFFLE",
                "APPEND", "DIFF", "CUMSUM", "CUMPROD", "GRADE",
                "SHIFT", "OUTER", "ROTATE", "INVERT", "CONVOLVE", "PLACE",
                "MATMUL", "RESHAPE", "SLICE", "STACK", "MVLET",
                "ZIP", "TRANSPOSE", "SOLVE", "HISTOGRAM", "INTEGRATE",
                "FFT", "IFFT",
                "XSORT", "SPLIT", "LINES", "WORDS", "CHARS",
                "KEYS", "VALUES", "CHUNK", "ENUMERATE",
                "REGEX_FINDALL", "REGEX.FINDALL",
                "MAP.KEYS", "MAP.VALUES", "MAP.ITEMS"
            };
            if (arr_fns.count(e.func_name)) return JD_TAG_ARR;
        }
        return JD_TAG_F64;
    };
    std::function<void(const Stmt&)> scan_top_decls = [&](const Stmt& s) {
        if ((s.kind == StmtKind::LET || s.kind == StmtKind::DIM ||
             s.kind == StmtKind::ASSIGN) && !s.var_name.empty()) {
            int t = JD_TAG_F64;
            // s.label carries the UDT type name for `DIM x AS T` and
            // also the `__EXPORT__` marker for top-level EXPORT DIMs;
            // only treat it as a UDT when it's neither empty nor the
            // export sentinel — otherwise `EXPORT DIM thing_count = 0`
            // would be mis-tagged as JD_TAG_ARR.
            bool is_udt_label = !s.label.empty() && s.label != "__EXPORT__";
            // $-suffix on the variable name is a string-by-convention hint
            // even without an AS clause — match how infer_expr_tag treats
            // VARIABLE references and how user-FUNC return types respect
            // the $ suffix. Without this, `DIM r$ = AWAIT p` would land
            // in the f64 fallback below and the i8* string handle would
            // get bit-pun'd into a numeric slot.
            bool dollar_string = !s.var_name.empty() && s.var_name.back() == '$';
            if (s.var_type == VarType::ARRAY || is_udt_label) t = JD_TAG_ARR;
            else if (s.var_type == VarType::STRING) t = JD_TAG_STR;
            else if (s.var_type == VarType::OBJECT) t = JD_TAG_NATIVE_MAP;
            else if (dollar_string) t = JD_TAG_STR;
            else if (s.expr) t = infer_expr_tag(*s.expr);
            auto it = pre_var_tags.find(s.var_name);
            // Promote only — don't downgrade from ARR to F64 via later
            // scalar reassignment.
            if (it == pre_var_tags.end() ||
                (it->second == JD_TAG_F64 && t != JD_TAG_F64))
                pre_var_tags[s.var_name] = t;
        }
    };
    for (auto& stmt : program) {
        if (!stmt) continue;
        if (stmt->kind == StmtKind::FUNCTION || stmt->kind == StmtKind::SUB) continue;
        scan_top_decls(*stmt);
    }

    // Pre-pass: any `arr[i] = some_array_value` statement marks `arr`
    // as holding nested arrays. Lets classify_return tag
    // `RETURN arr[i]` as ARR so the caller gets a proper array
    // pointer instead of an f64-punned one. Tracks local var kinds
    // per SUB/FUNC scope so `glyph = ZEROS(...); cache[i] = glyph`
    // also marks cache.
    std::function<void(const Stmt&,
                      std::unordered_map<std::string,int>&)> scan_idx_assigns;
    // Helper: classify RHS expr in local scope. For a VARIABLE, only
    // consult the local `kinds` map — never fall through to the global
    // pre_var_tags, since a SUB param can shadow a same-named global
    // (e.g. top-level `DIM v AS T` + `SUB Foo(v)` would otherwise leak
    // the UDT-ARR tag onto the param). For non-VARIABLE exprs, defer to
    // infer_expr_tag (literals + array-returning calls).
    auto local_rhs_tag = [&](const Expr& e,
                             const std::unordered_map<std::string,int>& kinds) -> int {
        if (e.kind == ExprKind::VARIABLE) {
            auto it = kinds.find(e.str_val);
            return (it != kinds.end()) ? it->second : JD_TAG_F64;
        }
        return infer_expr_tag(e);
    };
    scan_idx_assigns = [&](const Stmt& s,
                           std::unordered_map<std::string,int>& kinds) {
        // Track local var kinds: `x = ARRAY_THING` makes x hold ARR.
        if ((s.kind == StmtKind::LET || s.kind == StmtKind::ASSIGN ||
             s.kind == StmtKind::DIM) &&
            !s.var_name.empty() && s.expr) {
            int rt = local_rhs_tag(*s.expr, kinds);
            if (rt == JD_TAG_ARR || rt == JD_TAG_NATIVE_MAP)
                kinds[s.var_name] = rt;
        }
        if (s.kind == StmtKind::INDEX_ASSIGN && !s.var_name.empty() &&
            s.index_chain.size() == 1 && s.expr) {
            int rt = local_rhs_tag(*s.expr, kinds);
            if (rt == JD_TAG_ARR || rt == JD_TAG_NATIVE_MAP) {
                array_array_vars.insert(s.var_name);
            }
        }
        // SUB/FUNC body: recurse with a fresh kinds map (params and
        // locals are scope-isolated; the outer caller's `glyph` and
        // the callee's `glyph` are different slots).
        if (s.kind == StmtKind::FUNCTION || s.kind == StmtKind::SUB) {
            std::unordered_map<std::string,int> inner;
            for (auto& b : s.body) if (b) scan_idx_assigns(*b, inner);
            return;
        }
        for (auto& b : s.body)         if (b) scan_idx_assigns(*b, kinds);
        for (auto& b : s.catch_body)   if (b) scan_idx_assigns(*b, kinds);
        for (auto& b : s.finally_body) if (b) scan_idx_assigns(*b, kinds);
        for (auto& br : s.branches)
            for (auto& b : br.body) if (b) scan_idx_assigns(*b, kinds);
    };
    {
        std::unordered_map<std::string,int> top_kinds;
        for (auto& stmt : program) if (stmt) scan_idx_assigns(*stmt, top_kinds);
    }

    std::function<void(const Expr&)> scan_expr = [&](const Expr& e) {
        if (e.kind == ExprKind::CALL) {
            auto it = decls.find(e.func_name);
            if (it != decls.end()) {
                for (size_t i = 0; i < e.args.size() && i < it->second.tags.size(); i++) {
                    // Funcref-literal arg (`name@`) → callee receives a
                    // function pointer, not a string. Without this the
                    // param keeps its STR tag from the LITERAL_STRING
                    // path below and the callee's `fn(...)` indirect
                    // call falls through to a static "FN" CALL that the
                    // VM then rejects with "Undefined function: FN".
                    if (e.args[i] && e.args[i]->kind == ExprKind::LITERAL_STRING &&
                        e.args[i]->is_funcref_lit) {
                        it->second.tags[i] = JD_TAG_FUNCREF;
                        continue;
                    }
                    if (it->second.tags[i] != JD_TAG_STR && e.args[i] && is_str_expr(*e.args[i]))
                        it->second.tags[i] = JD_TAG_STR;
                    // Promote param types from the call-site so the callee's
                    // LLVM signature matches and INDEX inside the body
                    // dispatches against the right kind of value.
                    if (e.args[i] && it->second.tags[i] == JD_TAG_F64) {
                        auto& a = *e.args[i];
                        if (a.kind == ExprKind::ARRAY_LITERAL) it->second.tags[i] = JD_TAG_ARR;
                        else if (a.kind == ExprKind::MAP_LITERAL) it->second.tags[i] = JD_TAG_NATIVE_MAP;
                        else if (a.kind == ExprKind::VARIABLE) {
                            VarInfo* v = lookup_var(a.str_val);
                            if (v && (v->tag == JD_TAG_ARR || v->tag == JD_TAG_NATIVE_MAP ||
                                      v->tag == JD_TAG_VM_HANDLE || v->tag == JD_TAG_RUNTIME))
                                // Callee expects a concrete kind, not RUNTIME —
                                // pass tagged values as VM_HANDLE so the
                                // callee's INDEX dispatch works uniformly.
                                it->second.tags[i] = (v->tag == JD_TAG_RUNTIME) ? JD_TAG_VM_HANDLE : v->tag;
                            else {
                                // Fall back to the static pre-pass: if we
                                // statically inferred the global as ARR/MAP,
                                // propagate it into the callee's signature.
                                // Param shadowing is handled by scan_stmt
                                // hiding the names while inside SUB/FUNC.
                                auto pit = pre_var_tags.find(a.str_val);
                                if (pit != pre_var_tags.end() &&
                                    (pit->second == JD_TAG_ARR ||
                                     pit->second == JD_TAG_NATIVE_MAP))
                                    it->second.tags[i] = pit->second;
                            }
                        }
                        // Array-returning calls at the call site → promote
                        // the callee's param to match. Catches things like
                        // FUNC f(xs) called as `f(IOTA(n))` or
                        // `f(OUTER(a,b,"*"))`.
                        else if (a.kind == ExprKind::CALL) {
                            int rt = infer_expr_tag(a);
                            if (rt == JD_TAG_ARR || rt == JD_TAG_NATIVE_MAP ||
                                rt == JD_TAG_STR)
                                it->second.tags[i] = rt;
                        }
                        // INDEX (string-keyed like game{"stats"} or
                        // int-keyed like elist[i]) can return any boxed
                        // type. Only promote to VM_HANDLE when the callee
                        // actually drills into the param (uses it as INDEX
                        // source). If the callee treats it as a scalar,
                        // leave the signature as f64 and let call-site
                        // coerce_to(tag 7 → f64) materialise the number;
                        // otherwise a literal `ADD_GOLD 10` would get
                        // coerced to i64 and mis-read as a VM handle ID.
                        else if (a.kind == ExprKind::INDEX) {
                            const std::string& pname = it->second.stmt->params[i].name;
                            std::function<bool(const Expr&)> param_used_as_index =
                                [&](const Expr& x) -> bool {
                                if (x.kind == ExprKind::INDEX && x.left &&
                                    x.left->kind == ExprKind::VARIABLE &&
                                    x.left->str_val == pname)
                                    return true;
                                if (x.left && param_used_as_index(*x.left)) return true;
                                if (x.right && param_used_as_index(*x.right)) return true;
                                for (auto& arg : x.args)
                                    if (arg && param_used_as_index(*arg)) return true;
                                return false;
                            };
                            std::function<bool(const Stmt&)> body_drills_param =
                                [&](const Stmt& s) -> bool {
                                if (s.expr && param_used_as_index(*s.expr)) return true;
                                if (s.loop_cond && param_used_as_index(*s.loop_cond)) return true;
                                if (s.end_expr && param_used_as_index(*s.end_expr)) return true;
                                if (s.step_expr && param_used_as_index(*s.step_expr)) return true;
                                for (auto& pe : s.print_exprs)
                                    if (pe && param_used_as_index(*pe)) return true;
                                for (auto& b : s.body)
                                    if (b && body_drills_param(*b)) return true;
                                for (auto& br : s.branches) {
                                    if (br.condition && param_used_as_index(*br.condition)) return true;
                                    for (auto& [lo, hi] : br.case_labels) {
                                        if (lo && param_used_as_index(*lo)) return true;
                                        if (hi && param_used_as_index(*hi)) return true;
                                    }
                                    for (auto& b : br.body)
                                        if (b && body_drills_param(*b)) return true;
                                }
                                for (auto& c : s.catch_body)
                                    if (c && body_drills_param(*c)) return true;
                                for (auto& f : s.finally_body)
                                    if (f && body_drills_param(*f)) return true;
                                return false;
                            };
                            if (it->second.stmt && body_drills_param(*it->second.stmt)) {
                                // The param is used as an INDEX source. The
                                // value could be EITHER a VM Value handle
                                // (e.g. JSON.PARSE$ result) or a NATIVE
                                // JdbArray* (e.g. map_get_tagged result). VM
                                // dispatches via jdrt_val_arr_get, native via
                                // jdb_array_get — promoting to RUNTIME lets
                                // the tag-aware ABI carry the real type per
                                // call instead of forcing VM_HANDLE.
                                it->second.tags[i] = JD_TAG_RUNTIME;
                            }
                            // Note: the TYPEOF-driven JD_TAG_RUNTIME promotion
                            // is done as a separate pre-Phase-4 pass (covers
                            // FUNCs called with non-INDEX args too).
                        }
                    }
                }
            }
        }
        if (e.left) scan_expr(*e.left);
        if (e.right) scan_expr(*e.right);
        for (auto& a : e.args) if (a) scan_expr(*a);
    };

    std::function<void(const Stmt&)> scan_stmt = [&](const Stmt& s) {
        // When entering a SUB/FUNC body, hide its parameter names from
        // pre_var_tags lookup so a top-level `DIM v AS UDT` doesn't get
        // misread as the local `v` parameter inside the function. The
        // SUB/FUNC body has already been visited by Phase 1 — we need
        // it again here only to walk inner CALL expressions.
        if (s.kind == StmtKind::FUNCTION || s.kind == StmtKind::SUB) {
            std::vector<std::pair<std::string, int>> saved;
            for (auto& p : s.params) {
                auto it = pre_var_tags.find(p.name);
                if (it != pre_var_tags.end()) {
                    saved.push_back({p.name, it->second});
                    pre_var_tags.erase(it);
                }
            }
            for (auto& b : s.body) if (b) scan_stmt(*b);
            for (auto& [n, t] : saved) pre_var_tags[n] = t;
            return;
        }
        if (s.expr) scan_expr(*s.expr);
        if (s.loop_cond) scan_expr(*s.loop_cond);
        if (s.end_expr) scan_expr(*s.end_expr);
        if (s.step_expr) scan_expr(*s.step_expr);
        for (auto& pe : s.print_exprs) if (pe) scan_expr(*pe);
        for (auto& ic : s.index_chain) if (ic) scan_expr(*ic);
        for (auto& b : s.body) if (b) scan_stmt(*b);
        for (auto& br : s.branches) {
            if (br.condition) scan_expr(*br.condition);
            for (auto& [lo, hi] : br.case_labels) {
                if (lo) scan_expr(*lo);
                if (hi) scan_expr(*hi);
            }
            for (auto& b : br.body) if (b) scan_stmt(*b);
        }
        for (auto& c : s.catch_body) if (c) scan_stmt(*c);
        for (auto& f : s.finally_body) if (f) scan_stmt(*f);
    };

    for (auto& stmt : program) {
        if (stmt) scan_stmt(*stmt);
    }

    // Phase 2.5: warn when a mixed-element array literal is passed to a
    // non-DYNAMIC FUNC param.
    std::function<bool(const Expr&)> arg_is_mixed_literal = [&](const Expr& e) -> bool {
        if (e.kind != ExprKind::ARRAY_LITERAL) return false;
        for (auto& a : e.args)
            if (a && a->kind == ExprKind::INDEX) return true;
        return false;
    };
    std::function<void(const Stmt&)> warn_calls;
    std::function<void(const Expr&)> warn_expr_calls = [&](const Expr& e) {
        if (e.left)  warn_expr_calls(*e.left);
        if (e.right) warn_expr_calls(*e.right);
        for (auto& a : e.args) if (a) warn_expr_calls(*a);
        if (e.kind == ExprKind::CALL) {
            auto fit = decls.find(e.func_name);
            if (fit != decls.end() && fit->second.stmt) {
                auto& callee = *fit->second.stmt;
                size_t n = std::min(e.args.size(), callee.params.size());
                for (size_t i = 0; i < n; i++) {
                    if (!e.args[i]) continue;
                    if (!arg_is_mixed_literal(*e.args[i])) continue;
                    auto& p = callee.params[i];
                    if (p.type != VarType::ANY) {
                        std::cerr << "[warn] " << e.func_name << "(): arg #"
                                  << (i + 1) << " is a mixed-element array "
                                  << "literal but parameter '" << p.name
                                  << "' is not declared AS DYNAMIC. Reads "
                                  << "via '" << p.name << "[i]' will lose "
                                  << "per-cell types (strings format as 0).\n";
                    }
                }
            }
        }
    };
    warn_calls = [&](const Stmt& s) {
        if (s.expr) warn_expr_calls(*s.expr);
        if (s.loop_cond) warn_expr_calls(*s.loop_cond);
        if (s.end_expr) warn_expr_calls(*s.end_expr);
        if (s.step_expr) warn_expr_calls(*s.step_expr);
        for (auto& pe : s.print_exprs) if (pe) warn_expr_calls(*pe);
        for (auto& ic : s.index_chain) if (ic) warn_expr_calls(*ic);
        for (auto& b : s.body) if (b) warn_calls(*b);
        for (auto& br : s.branches) {
            if (br.condition) warn_expr_calls(*br.condition);
            for (auto& b : br.body) if (b) warn_calls(*b);
        }
        for (auto& c : s.catch_body) if (c) warn_calls(*c);
        for (auto& f : s.finally_body) if (f) warn_calls(*f);
    };
    for (auto& stmt : program) {
        if (stmt) warn_calls(*stmt);
    }

    // Phase 3: infer return types from RETURN statements in the body.
    // (Function name suffix `$` already forces tag=2 in Phase 1; this catches
    // the case where the suffix was omitted but a RETURN expression is a
    // string. Only RETURN exprs count — string locals or string-typed sub
    // calls inside the body do NOT make the function string-returning.)
    std::function<bool(const Stmt&)> any_return_returns_string =
        [&](const Stmt& s) -> bool {
        if (s.kind == StmtKind::RETURN && s.expr && expr_involves_strings(*s.expr))
            return true;
        for (auto& b : s.body) if (b && any_return_returns_string(*b)) return true;
        for (auto& br : s.branches)
            for (auto& b : br.body) if (b && any_return_returns_string(*b)) return true;
        for (auto& c : s.catch_body) if (c && any_return_returns_string(*c)) return true;
        for (auto& f : s.finally_body) if (f && any_return_returns_string(*f)) return true;
        return false;
    };
    for (auto& [name, decl] : decls) {
        if (decl.return_tag == JD_TAG_F64 && decl.stmt &&
            any_return_returns_string(*decl.stmt))
            decl.return_tag = JD_TAG_STR;
    }

    // Phase 3b: infer map/array returns. FUNC's returning maps or arrays
    // must be typed as i8* (ptr) so callers don't truncate through f64 and
    // lose the handle type. This tracks per-FUNC assignments `var = {}` /
    // `var = []` and classifies RETURN expressions based on:
    //   RETURN {}             — map literal, tag 4
    //   RETURN []             — array literal, tag 3
    //   RETURN var            — look up local's inferred kind from body
    //   RETURN arr[i]         — inherits arr[]'s element kind (rare)
    //   RETURN other_func()   — inherit callee's return_tag (after Phase 3)
    std::function<int(const Stmt&, std::unordered_map<std::string,int>&)> classify_return =
        [&](const Stmt& s, std::unordered_map<std::string,int>& local_kinds) -> int {
        // Track `var = <literal>` (or DIM with init) so RETURN var resolves.
        // DIM was missing here originally — `DIM out AS ARRAY = []` left out
        // unclassified, so a FUNC building a list with rec APPENDs and
        // returning `out` came back tagged f64 and the caller's APPEND took
        // the single-cell path, dropping all but one element per call.
        if ((s.kind == StmtKind::LET || s.kind == StmtKind::ASSIGN ||
             s.kind == StmtKind::DIM) && !s.var_name.empty() && s.expr) {
            if (s.expr->kind == ExprKind::MAP_LITERAL)
                local_kinds[s.var_name] = JD_TAG_NATIVE_MAP;
            else if (s.expr->kind == ExprKind::ARRAY_LITERAL)
                local_kinds[s.var_name] = JD_TAG_ARR;
            else if (s.expr->kind == ExprKind::CALL) {
                auto cit = decls.find(s.expr->func_name);
                if (cit != decls.end() &&
                    (cit->second.return_tag == JD_TAG_ARR ||
                     cit->second.return_tag == JD_TAG_NATIVE_MAP))
                    local_kinds[s.var_name] = cit->second.return_tag;
            } else if (s.expr->kind == ExprKind::VARIABLE) {
                auto lit = local_kinds.find(s.expr->str_val);
                if (lit != local_kinds.end()) local_kinds[s.var_name] = lit->second;
            }
        }
        // DIM x AS ARRAY (with or without init) tags the slot as ARR even if
        // the initializer is a non-array shape (e.g. `DIM out AS ARRAY = []`
        // where the empty literal is parsed as ARRAY_LITERAL anyway, or
        // future shapes that don't infer cleanly).
        if (s.kind == StmtKind::DIM && !s.var_name.empty() &&
            s.var_type == VarType::ARRAY) {
            local_kinds[s.var_name] = JD_TAG_ARR;
        }
        if (s.kind == StmtKind::RETURN && s.expr) {
            const Expr& e = *s.expr;
            if (e.kind == ExprKind::MAP_LITERAL) return JD_TAG_NATIVE_MAP;
            if (e.kind == ExprKind::ARRAY_LITERAL) return JD_TAG_ARR;
            if (e.kind == ExprKind::VARIABLE) {
                auto lit = local_kinds.find(e.str_val);
                if (lit != local_kinds.end()) return lit->second;
            }
            // `RETURN arr[i]` where arr is known to hold nested arrays.
            // Without this the return tag stays f64 and the caller
            // can't deref the result.
            if (e.kind == ExprKind::INDEX && e.left &&
                e.left->kind == ExprKind::VARIABLE) {
                if (array_array_vars.count(e.left->str_val)) return JD_TAG_ARR;
            }
            if (e.kind == ExprKind::CALL) {
                auto cit = decls.find(e.func_name);
                if (cit != decls.end() &&
                    (cit->second.return_tag == JD_TAG_ARR ||
                     cit->second.return_tag == JD_TAG_NATIVE_MAP))
                    return cit->second.return_tag;
                // Native runtime function — pick up its declared return tag.
                auto rit = runtime_funcs.find(e.func_name);
                if (rit != runtime_funcs.end() &&
                    (rit->second.return_tag == JD_TAG_ARR ||
                     rit->second.return_tag == JD_TAG_NATIVE_MAP))
                    return rit->second.return_tag;
                // VM-bridge array returners (no native runtime, but known
                // to produce arrays): SHIFT, OUTER, MATMUL, etc.
                static const std::unordered_set<std::string> arr_calls = {
                    "SHIFT", "OUTER", "ROTATE", "INVERT", "CONVOLVE", "PLACE",
                    "MATMUL", "RESHAPE", "SLICE", "STACK", "MVLET",
                    "ZIP", "TRANSPOSE", "SOLVE", "HISTOGRAM", "INTEGRATE",
                    "FFT", "IFFT",
                    "XSORT"
                };
                if (arr_calls.count(e.func_name)) return JD_TAG_ARR;
            }
        }
        int kind = 0;
        for (auto& b : s.body) if (b) { int k = classify_return(*b, local_kinds); if (k && !kind) kind = k; }
        for (auto& br : s.branches)
            for (auto& b : br.body) if (b) { int k = classify_return(*b, local_kinds); if (k && !kind) kind = k; }
        for (auto& c : s.catch_body) if (c) { int k = classify_return(*c, local_kinds); if (k && !kind) kind = k; }
        for (auto& f : s.finally_body) if (f) { int k = classify_return(*f, local_kinds); if (k && !kind) kind = k; }
        return kind;
    };
    // Fixpoint: callee kinds may unlock caller kinds.
    bool rt_changed = true;
    int rt_guard = 0;
    while (rt_changed && rt_guard++ < 4) {
        rt_changed = false;
        for (auto& [name, decl] : decls) {
            if (decl.return_tag != JD_TAG_F64 || !decl.stmt) continue;
            std::unordered_map<std::string,int> local_kinds;
            // Seed with param tags so `result = arr_param; RETURN result`
            // can flow ARR through. Without this, AMAP-style HOFs that
            // return an array built from an array param come back as f64
            // and the caller never sees the array.
            for (size_t pi = 0; pi < decl.stmt->params.size() && pi < decl.tags.size(); pi++) {
                if (decl.tags[pi] == JD_TAG_ARR || decl.tags[pi] == JD_TAG_NATIVE_MAP)
                    local_kinds[decl.stmt->params[pi].name] = decl.tags[pi];
            }
            int k = classify_return(*decl.stmt, local_kinds);
            if (k == JD_TAG_ARR || k == JD_TAG_NATIVE_MAP) {
                decl.return_tag = k;
                rt_changed = true;
            }
        }
    }

    // Phase 3.5: among the FUNCs that return ARR, determine which ones
    // return a *string*-array specifically. Scan the body assignments and
    // RETURNs, flowing string-array-ness through known string builtins,
    // APPEND, and recursive callee tags. Without this, a user FUNC like
    //   FUNC walk(p AS STRING)
    //     DIM out AS ARRAY = []
    //     ...
    //     out = APPEND(out, [full_path])
    //     RETURN out
    //   ENDFUNC
    // returns ARR but its caller (`DIM files = walk(root)`) wouldn't know
    // the cells are string ptrs, so `files[i]` decoded as f64 garbage.
    //
    // We do a fixpoint over FUNCs because callee flags inform caller body
    // classification (e.g. recursive walk needs walk's own flag set on
    // pass 2 before APPEND(out, walk(...)) can mark the LHS).
    auto scan_for_str_arr = [&](const Stmt& body, const FuncDecl& fd) -> bool {
        std::unordered_set<std::string> local_str_arr;
        std::unordered_set<std::string> local_str_var;
        // Seed scalar-string locals from string-typed params.
        for (size_t pi = 0; pi < fd.stmt->params.size(); pi++) {
            auto& p = fd.stmt->params[pi];
            if (p.type == VarType::STRING ||
                (!p.name.empty() && p.name.back() == '$'))
                local_str_var.insert(p.name);
        }
        bool returns_str = false;
        // Recognise expressions that produce a scalar string.
        std::function<bool(const Expr&)> is_string_scalar = [&](const Expr& e) -> bool {
            if (e.kind == ExprKind::LITERAL_STRING) return true;
            if (e.kind == ExprKind::VARIABLE) {
                if (!e.str_val.empty() && e.str_val.back() == '$') return true;
                return local_str_var.count(e.str_val) != 0;
            }
            if (e.kind == ExprKind::CALL) {
                if (!e.func_name.empty() && e.func_name.back() == '$') return true;
            }
            if (e.kind == ExprKind::BINARY && e.op == TokenType::PLUS) {
                // BASIC convention: string + anything → string concat. Be
                // liberal — any operand being string is enough evidence.
                if (e.left && is_string_scalar(*e.left)) return true;
                if (e.right && is_string_scalar(*e.right)) return true;
            }
            if (e.kind == ExprKind::INDEX && e.left &&
                e.left->kind == ExprKind::VARIABLE &&
                local_str_arr.count(e.left->str_val))
                return true;  // arr[i] where arr is string-array
            return false;
        };
        // Recognise expressions that produce a string-array.
        std::function<bool(const Expr&)> is_str_arr_expr = [&](const Expr& e) -> bool {
            if (e.kind == ExprKind::VARIABLE) return local_str_arr.count(e.str_val) != 0;
            if (e.kind == ExprKind::ARRAY_LITERAL) {
                bool any = false;
                for (auto& a : e.args) {
                    if (!a) continue;
                    any = true;
                    if (!is_string_scalar(*a)) return false;
                }
                return any;
            }
            if (e.kind == ExprKind::CALL) {
                std::string u = e.func_name;
                std::transform(u.begin(), u.end(), u.begin(), ::toupper);
                if (u == "SPLIT" || u == "TILED.LAYERS$" || u == "LINES" ||
                    u == "WORDS" || u == "CHARS" || u == "STR$" ||
                    u == "OS.ARGS")
                    return true;
                if (u == "DIR$") {
                    bool extended = false;
                    if (e.args.size() >= 2 && e.args[1]) {
                        auto& a = *e.args[1];
                        if (a.kind == ExprKind::LITERAL_BOOL) extended = a.bool_val;
                        else if (a.kind == ExprKind::LITERAL_INT) extended = (a.int_val != 0);
                        else extended = true;
                    }
                    return !extended;
                }
                if (u == "APPEND" && e.args.size() >= 2 && e.args[0] && e.args[1]) {
                    // Liberal: either side proves string-array-ness. A scalar
                    // string as the appended value (APPEND(arr, s$)) also makes
                    // the result a string array, not just a string-array RHS.
                    return is_str_arr_expr(*e.args[0]) || is_str_arr_expr(*e.args[1]) ||
                           is_string_scalar(*e.args[1]);
                }
                auto cit = decls.find(e.func_name);
                if (cit != decls.end() && cit->second.returns_string_array) return true;
            }
            return false;
        };
        std::function<void(const Stmt&)> rec = [&](const Stmt& s) {
            if ((s.kind == StmtKind::LET || s.kind == StmtKind::ASSIGN ||
                 s.kind == StmtKind::DIM) && !s.var_name.empty() && s.expr) {
                if (is_str_arr_expr(*s.expr)) local_str_arr.insert(s.var_name);
                if (is_string_scalar(*s.expr)) local_str_var.insert(s.var_name);
                // DIM ... AS STRING (with or without RHS) is also a scalar
                // string, regardless of what the RHS infers to.
                if (s.kind == StmtKind::DIM && s.var_type == VarType::STRING)
                    local_str_var.insert(s.var_name);
                if (!s.var_name.empty() && s.var_name.back() == '$')
                    local_str_var.insert(s.var_name);
            }
            if (s.kind == StmtKind::RETURN && s.expr) {
                if (is_str_arr_expr(*s.expr)) returns_str = true;
            }
            for (auto& b : s.body) if (b) rec(*b);
            for (auto& br : s.branches)
                for (auto& b : br.body) if (b) rec(*b);
            for (auto& c : s.catch_body) if (c) rec(*c);
            for (auto& f : s.finally_body) if (f) rec(*f);
        };
        rec(body);
        return returns_str;
    };
    bool sa_changed = true;
    int sa_guard = 0;
    while (sa_changed && sa_guard++ < 4) {
        sa_changed = false;
        for (auto& [name, decl] : decls) {
            if (decl.return_tag != JD_TAG_ARR || !decl.stmt || decl.returns_string_array)
                continue;
            if (scan_for_str_arr(*decl.stmt, decl)) {
                decl.returns_string_array = true;
                sa_changed = true;
            }
        }
    }
    // Mirror into the class-scope set so caller-side codegen (codegen_dim /
    // codegen_let_or_assign) can mark `DIM x = func(...)` as a string array.
    for (auto& [name, decl] : decls)
        if (decl.returns_string_array) string_array_returning_funcs.insert(name);

    // Pre-Phase-4: tag-aware FUNC ABI promotion. A FUNC param that the
    // body passes to TYPEOF (and hasn't already been promoted to a more
    // specific type via call-site analysis) becomes JD_TAG_RUNTIME — the
    // LLVM signature then takes (i64 val, i32 tag) for that slot so the
    // body can recover the dynamic type at runtime. Without this pre-pass
    // the promotion was conditional on the call site passing an INDEX arg
    // (the only path that ran my detection earlier), which missed cases
    // like `q$("foo")` and `q$(42)` where the existing str-promotion
    // had already locked the param at JD_TAG_STR and a later int call
    // got bit-pun'd into the str slot — TYPEOF then said "STRING".
    {
        std::function<bool(const Expr&, const std::string&)> uses_typeof_param =
            [&](const Expr& x, const std::string& pname) -> bool {
            if (x.kind == ExprKind::CALL) {
                std::string fn = x.func_name;
                std::transform(fn.begin(), fn.end(), fn.begin(), ::toupper);
                if (fn == "TYPEOF" || fn == "TYPEOF$") {
                    for (auto& a : x.args)
                        if (a && a->kind == ExprKind::VARIABLE &&
                            a->str_val == pname)
                            return true;
                }
            }
            if (x.left && uses_typeof_param(*x.left, pname)) return true;
            if (x.right && uses_typeof_param(*x.right, pname)) return true;
            for (auto& a : x.args)
                if (a && uses_typeof_param(*a, pname)) return true;
            return false;
        };
        std::function<bool(const Stmt&, const std::string&)> body_uses_typeof_param =
            [&](const Stmt& s, const std::string& pname) -> bool {
            if (s.expr && uses_typeof_param(*s.expr, pname)) return true;
            if (s.loop_cond && uses_typeof_param(*s.loop_cond, pname)) return true;
            if (s.end_expr && uses_typeof_param(*s.end_expr, pname)) return true;
            if (s.step_expr && uses_typeof_param(*s.step_expr, pname)) return true;
            for (auto& pe : s.print_exprs)
                if (pe && uses_typeof_param(*pe, pname)) return true;
            for (auto& b : s.body)
                if (b && body_uses_typeof_param(*b, pname)) return true;
            for (auto& br : s.branches) {
                if (br.condition && uses_typeof_param(*br.condition, pname)) return true;
                for (auto& b : br.body)
                    if (b && body_uses_typeof_param(*b, pname)) return true;
            }
            for (auto& c : s.catch_body)
                if (c && body_uses_typeof_param(*c, pname)) return true;
            return false;
        };
        for (auto& [name, decl] : decls) {
            if (!decl.stmt) continue;
            for (size_t pi = 0; pi < decl.stmt->params.size() && pi < decl.tags.size(); pi++) {
                // Don't override $-suffixed params (already STRING-typed)
                // or AS-typed declarations — those have a real declared
                // intent that should win.
                const auto& p = decl.stmt->params[pi];
                if (!p.name.empty() && p.name.back() == '$') continue;
                if (p.type != VarType::NONE) continue;
                if (body_uses_typeof_param(*decl.stmt, p.name)) {
                    decl.tags[pi] = JD_TAG_RUNTIME;
                }
            }
        }
    }

    // Phase 4: create LLVM functions with inferred types
    for (auto& [name, decl] : decls) {
        std::vector<LLVMTypeRef> param_types;
        for (int t : decl.tags) {
            if (t == JD_TAG_RUNTIME) {
                // Tag-aware ABI: each runtime-tagged param gets two LLVM
                // args — i64 raw bits + i32 JdTag. The body recovers the
                // real type at runtime so TYPEOF / coerce_to / Q$ dispatch
                // correctly. Without this a string passed as `q$(arr[i])`
                // arrived as f64 bits and TYPEOF said "FLOAT64".
                param_types.push_back(i64_type);
                param_types.push_back(i32_type);
            } else {
                param_types.push_back((t == 2 || t == 3 || t == 4 || t == 5) ? i8_ptr_type :
                                      (t == 6) ? i64_type : f64_type);
            }
        }

        LLVMTypeRef ret_type;
        if (decl.return_tag == -1) ret_type = void_type;
        else if (decl.return_tag == JD_TAG_STR || decl.return_tag == JD_TAG_ARR ||
                 decl.return_tag == JD_TAG_NATIVE_MAP) ret_type = i8_ptr_type;
        else if (decl.return_tag == JD_TAG_VM_HANDLE) ret_type = i64_type;
        else ret_type = f64_type;

        LLVMTypeRef fn_type = LLVMFunctionType(ret_type,
            param_types.empty() ? nullptr : param_types.data(),
            (unsigned)param_types.size(), 0);
        LLVMValueRef fn = LLVMAddFunction(module, name.c_str(), fn_type);
        user_functions[name] = { fn, decl.return_tag, decl.tags, decl.is_async };
    }
}

// ── Phase 2: StaticType + type environment ─────────────────

std::string LLVMCodegen::StaticType::describe() const {
    switch (kind) {
        case Kind::UNKNOWN: return "UNKNOWN";
        case Kind::ANY:     return "ANY";
        case Kind::INTEGER: return "INTEGER";
        case Kind::NUMBER:  return "NUMBER";
        case Kind::STRING:  return "STRING";
        case Kind::BOOLEAN: return "BOOLEAN";
        case Kind::DATE:    return "DATE";
        case Kind::MAP:     return "MAP";
        case Kind::TENSOR:  return "TENSOR";
        case Kind::FUNCREF: return "FUNCREF";
        case Kind::UDT:     return name.empty() ? "UDT" : name;
        case Kind::ARRAY: {
            std::string inner = elem ? elem->describe() : "UNKNOWN";
            return inner + "[]";
        }
    }
    return "?";
}

LLVMCodegen::StaticType LLVMCodegen::StaticType::from_vartype(
        VarType vt, VarType et, const std::string& udt_name) {
    StaticType st;
    auto bucket = [](VarType v, const std::string& n) -> StaticType {
        StaticType r;
        switch (v) {
            case VarType::BOOLEAN:
                r.kind = Kind::BOOLEAN; break;
            case VarType::BYTE: case VarType::CHAR:
            case VarType::INT16: case VarType::INT32: case VarType::INT64:
                r.kind = Kind::INTEGER; break;
            case VarType::FLOAT16: case VarType::FLOAT32: case VarType::FLOAT64:
                r.kind = Kind::NUMBER; break;
            case VarType::STRING:
                r.kind = Kind::STRING; break;
            case VarType::OBJECT:
                if (!n.empty()) { r.kind = Kind::UDT; r.name = n; }
                else r.kind = Kind::MAP;
                break;
            case VarType::TENSOR:
                r.kind = Kind::TENSOR; break;
            case VarType::ARRAY:
            case VarType::NONE:
            default:
                r.kind = Kind::UNKNOWN; break;
        }
        return r;
    };
    if (vt == VarType::ARRAY) {
        st.kind = Kind::ARRAY;
        st.name = udt_name;  // UDT element name if the array holds UDT instances
        // Element type: OBJECT+udt_name → UDT; otherwise scalar bucket.
        StaticType inner = bucket(et, udt_name);
        st.elem = std::make_shared<StaticType>(inner);
    } else {
        st = bucket(vt, udt_name);
    }
    return st;
}

void LLVMCodegen::populate_type_env(const std::vector<StmtPtr>& program) {
    // Built-in predeclared constants (EXPLICIT mode must not flag these).
    {
        StaticType num; num.kind = StaticType::Kind::NUMBER;
        type_env["PI"] = num;
        type_env["E"]  = num;
    }
    for (auto& stmt : program) {
        if (!stmt) continue;
        if (stmt->kind == StmtKind::DIM) {
            type_env[stmt->var_name] = StaticType::from_vartype(
                stmt->var_type, stmt->elem_type, stmt->label);
        } else if (stmt->kind == StmtKind::LET && !stmt->var_name.empty()) {
            // LET counts as a declaration under EXPLICIT (classical BASIC
            // idiom; matches interpreter behavior). Use declared AS-type
            // if present, else infer coarsely from RHS.
            StaticType t;
            if (stmt->var_type != VarType::NONE) {
                t = StaticType::from_vartype(
                    stmt->var_type, stmt->elem_type, stmt->label);
            } else if (stmt->expr) {
                t = infer_expr_type(*stmt->expr);
            }
            // Don't overwrite a prior DIM with a weaker LET inference.
            auto it = type_env.find(stmt->var_name);
            if (it == type_env.end() || it->second.is_unknown())
                type_env[stmt->var_name] = t;
        }
    }
}

LLVMCodegen::StaticType LLVMCodegen::infer_expr_type(const Expr& e) const {
    using K = StaticType::Kind;
    auto make = [](K k) { StaticType t; t.kind = k; return t; };
    switch (e.kind) {
        case ExprKind::LITERAL_INT:    return make(K::INTEGER);
        case ExprKind::LITERAL_FLOAT:  return make(K::NUMBER);
        case ExprKind::LITERAL_STRING: return make(K::STRING);
        case ExprKind::LITERAL_BOOL:   return make(K::BOOLEAN);
        case ExprKind::MAP_LITERAL:    return make(K::MAP);
        case ExprKind::LAMBDA_EXPR:    return make(K::FUNCREF);
        case ExprKind::ARRAY_LITERAL: {
            StaticType t = make(K::ARRAY);
            // Peek at the first element if all elements share a coarse type.
            if (!e.args.empty() && e.args[0]) {
                StaticType first = infer_expr_type(*e.args[0]);
                bool homogeneous = true;
                for (size_t i = 1; i < e.args.size() && homogeneous; ++i) {
                    if (!e.args[i]) continue;
                    StaticType other = infer_expr_type(*e.args[i]);
                    if (other.kind != first.kind) homogeneous = false;
                }
                if (homogeneous && first.kind != K::UNKNOWN)
                    t.elem = std::make_shared<StaticType>(first);
            }
            return t;
        }
        case ExprKind::VARIABLE: {
            // $-suffix is a string-by-convention hint even if not DIM'd.
            if (!e.str_val.empty() && e.str_val.back() == '$')
                return make(K::STRING);
            auto it = type_env.find(e.str_val);
            if (it != type_env.end()) return it->second;
            return make(K::UNKNOWN);
        }
        case ExprKind::UNARY:
            return e.right ? infer_expr_type(*e.right) : make(K::UNKNOWN);
        case ExprKind::BINARY: {
            // Arithmetic / comparison / string-concat rules. Comparisons
            // yield BOOLEAN; concat with any string yields STRING; numeric
            // mixes promote to NUMBER; anything else → UNKNOWN.
            StaticType l = e.left  ? infer_expr_type(*e.left)  : make(K::UNKNOWN);
            StaticType r = e.right ? infer_expr_type(*e.right) : make(K::UNKNOWN);
            switch (e.op) {
                case TokenType::EQ: case TokenType::NE:
                case TokenType::LT: case TokenType::LE:
                case TokenType::GT: case TokenType::GE:
                case TokenType::AND: case TokenType::OR:
                case TokenType::XOR: case TokenType::ANDALSO:
                case TokenType::ORELSE:
                    return make(K::BOOLEAN);
                default: break;
            }
            if (l.kind == K::STRING || r.kind == K::STRING)
                return make(K::STRING);
            auto is_num = [](K k) {
                return k == K::INTEGER || k == K::NUMBER ||
                       k == K::BOOLEAN || k == K::DATE;
            };
            if (is_num(l.kind) && is_num(r.kind)) {
                if (l.kind == K::NUMBER || r.kind == K::NUMBER) return make(K::NUMBER);
                return make(K::INTEGER);
            }
            return make(K::UNKNOWN);
        }
        case ExprKind::INDEX: {
            // Array-of-T indexed by int → T; everything else UNKNOWN.
            if (!e.left) return make(K::UNKNOWN);
            StaticType base = infer_expr_type(*e.left);
            if (base.kind == K::ARRAY && base.elem)
                return *base.elem;
            return make(K::UNKNOWN);
        }
        case ExprKind::CALL: {
            // User-defined functions: trust declared return tag.
            auto uit = user_functions.find(e.func_name);
            if (uit != user_functions.end()) {
                switch (uit->second.return_tag) {
                    case 0: return make(K::INTEGER);
                    case 1: return make(K::NUMBER);
                    case 2: return make(K::STRING);
                    case 3: return make(K::ARRAY);
                    case 4: return make(K::MAP);
                    case 5: return make(K::FUNCREF);
                    default: return make(K::UNKNOWN);
                }
            }
            auto rit = runtime_funcs.find(e.func_name);
            if (rit != runtime_funcs.end()) {
                switch (rit->second.return_tag) {
                    case 0: return make(K::INTEGER);
                    case 1: return make(K::NUMBER);
                    case 2: return make(K::STRING);
                    case 3: return make(K::ARRAY);
                    case 4: return make(K::MAP);
                    case 5: return make(K::FUNCREF);
                    default: break;
                }
            }
            // $-suffixed builtins return strings (SPLIT$, LCASE$, etc).
            if (!e.func_name.empty() && e.func_name.back() == '$')
                return make(K::STRING);
            return make(K::UNKNOWN);
        }
        case ExprKind::PIPE_EXPR: {
            if (e.right) return infer_expr_type(*e.right);
            return make(K::UNKNOWN);
        }
        default: return make(K::UNKNOWN);
    }
}

bool LLVMCodegen::types_compatible(const StaticType& src, const StaticType& dst) {
    using K = StaticType::Kind;
    // Escape hatches: missing info → no verdict.
    if (src.kind == K::UNKNOWN || dst.kind == K::UNKNOWN) return true;
    if (src.kind == K::ANY     || dst.kind == K::ANY)     return true;
    if (src.kind == dst.kind) {
        if (dst.kind == K::UDT)   return src.name == dst.name;
        // Element-level check for arrays is only when both sides have a
        // declared element type; otherwise leniently accept.
        if (dst.kind == K::ARRAY) {
            if (!src.elem || !dst.elem) return true;
            return types_compatible(*src.elem, *dst.elem);
        }
        return true;
    }
    // Numeric widening: INTEGER / BOOLEAN / DATE all fit into NUMBER, and
    // vice versa under BASIC's auto-conversion rules.
    auto is_num = [](K k) {
        return k == K::INTEGER || k == K::NUMBER ||
               k == K::BOOLEAN || k == K::DATE;
    };
    if (is_num(src.kind) && is_num(dst.kind)) return true;
    // MAP / OBJECT — jdBasic maps and UDT instances both stringify to
    // Object in user code, so a bare Object target accepts either.
    if (dst.kind == K::MAP && src.kind == K::UDT) return true;
    if (dst.kind == K::UDT && src.kind == K::MAP) return true;
    return false;
}

// ── Main Entry Points ───────────────────────────────────────

bool LLVMCodegen::compile(const std::vector<StmtPtr>& program,
                           const std::string& output_exe,
                           const std::string& source_path) {
    init_module();
    declare_runtime_functions();
    create_main_function();
    declare_functions(program);
    populate_type_env(program);
    // Pre-scan OPTION directives at top level so explicit_mode / strict_mode
    // are live before codegen_program's globals pre-pass runs (which emits
    // Phase 3 diagnostics conditional on explicit_mode). codegen_stmt still
    // handles OPTION at statement time so mid-program toggles work too.
    // Options are file-scoped: a STRICT main file can IMPORT a loose module
    // without forcing its migration. Group by stmt->source_file.
    for (auto& s : program) {
        if (s && s->kind == StmtKind::OPTION_STMT && s->expr &&
            s->expr->kind == ExprKind::LITERAL_STRING) {
            std::string opt = s->expr->str_val;
            std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
            if (opt == "EXPLICITOFF" || opt == "NOEXPLICIT") {
                explicit_mode = false;
                explicit_files.erase(s->source_file);
            } else if (opt == "EXPLICIT") {
                explicit_mode = true;
                explicit_files.insert(s->source_file);
            } else if (opt == "NOSTRICT" || opt == "STRICTOFF") {
                strict_mode = false;
                strict_files.erase(s->source_file);
            } else if (opt == "STRICT") {
                strict_mode = true;
                strict_files.insert(s->source_file);
            }
        }
    }
    codegen_program(program);

    // STRICT/EXPLICIT diagnostics accumulated during codegen_program are
    // fatal — print them all so the user sees every violation, not just
    // the first one to trip a codegen assertion.
    if (!diagnostics.empty()) {
        std::ostringstream oss;
        for (auto& d : diagnostics) {
            oss << "error at ";
            if (!d.file.empty()) oss << d.file << ":";
            oss << d.line << ": " << d.msg << "\n";
        }
        oss << (int)diagnostics.size() << " error(s). Compilation aborted.";
        error_msg = oss.str();
        std::cerr << error_msg << std::endl;
        return false;
    }

    // Shutdown VM bridge before exit
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
        // Dispose top-level UDT locals (main scope) before VM teardown.
        emit_dispose_cleanup();
        auto shut_it = runtime_funcs.find("__jdrt_shutdown");
        if (shut_it != runtime_funcs.end()) {
            LLVMValueRef handle_global = LLVMGetNamedGlobal(module, "__jdrt_handle");
            if (handle_global) {
                LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type, handle_global, "rt");
                LLVMValueRef args[] = { handle };
                LLVMBuildCall2(builder, shut_it->second.fn_type, shut_it->second.fn, args, 1, "");
            }
        }
        LLVMBuildRet(builder, LLVMConstInt(i32_type, 0, 0));
    }

    // Verify module
    char* err = nullptr;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &err)) {
        error_msg = "LLVM IR verification failed: " + std::string(err);
        // Dump IR for debugging
        char* ir = LLVMPrintModuleToString(module);
        std::cerr << ir << std::endl;
        LLVMDisposeMessage(ir);
        LLVMDisposeMessage(err);
        return false;
    }
    if (err) LLVMDisposeMessage(err);
    // DEBUG: dump IR when JDB_DUMP_IR env var is set
    if (std::getenv("JDB_DUMP_IR")) {
        std::string ll_path = output_exe;
        auto ld = ll_path.rfind('.');
        if (ld != std::string::npos) ll_path = ll_path.substr(0, ld);
        ll_path += ".ll";
        char* ir = LLVMPrintModuleToString(module);
        FILE* f = fopen(ll_path.c_str(), "w");
        if (f) { fputs(ir, f); fclose(f); }
        LLVMDisposeMessage(ir);
    }

    // Emit object file
    std::string obj_path = output_exe;
    auto dot = obj_path.rfind('.');
    if (dot != std::string::npos) obj_path = obj_path.substr(0, dot);
    obj_path += ".obj";

    if (!emit_object_file(obj_path)) return false;

    // Optional: <source>.props -> .res with VERSIONINFO + icon.
    std::string res_path = generate_version_resource(source_path, obj_path);

    if (!link_executable(obj_path, output_exe, res_path)) return false;

    std::remove(obj_path.c_str());
    if (!res_path.empty()) std::remove(res_path.c_str());
    return true;
}

bool LLVMCodegen::emit_ir(const std::vector<StmtPtr>& program) {
    init_module();
    declare_runtime_functions();
    create_main_function();
    declare_functions(program);
    codegen_program(program);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        LLVMBuildRet(builder, LLVMConstInt(i32_type, 0, 0));

    char* ir = LLVMPrintModuleToString(module);
    std::cout << ir << std::endl;
    LLVMDisposeMessage(ir);
    return true;
}

// ── Program / Statement Codegen ─────────────────────────────

void LLVMCodegen::codegen_program(const std::vector<StmtPtr>& program) {
    // Pre-scan: declare all global variables used in top-level code
    // so that FUNC/SUB bodies can reference them
    // Pre-scan TYPE_DECL names so we can recognize DIM x AS TypeName
    std::unordered_set<std::string> type_names;
    for (auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::TYPE_DECL)
            type_names.insert(stmt->func_name);
    }

    // Pre-scan DIM AS TypeName to know which variables are UDT objects
    std::unordered_set<std::string> udt_var_names;
    for (auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::DIM && !stmt->label.empty() &&
            type_names.count(stmt->label)) {
            udt_var_names.insert(stmt->var_name);
            var_udt_type[stmt->var_name] = stmt->label;
        }
        // Same for DIM arr[N] AS TypeName — register the element type so
        // infer_tag sees `arr[i]` as UDT (tag 3) during the var-type
        // inference pass. Without this, the LET of `arr[0]` below gets
        // stamped as f64, and later map INDEX on it stores a ptr into
        // an f64 slot (silent type punning → garbage on read).
        if (stmt && stmt->kind == StmtKind::DIM && stmt->expr &&
            stmt->expr->kind == ExprKind::CALL &&
            stmt->expr->func_name == "__MAKE_UDT_ARRAY__" &&
            stmt->expr->args.size() >= 2 &&
            stmt->expr->args[1]->kind == ExprKind::LITERAL_STRING) {
            var_udt_type[stmt->var_name + "[]"] = stmt->expr->args[1]->str_val;
        }
    }

    // Pre-scan for UDT propagation: PUSH list, udt_var marks list[]; assignments
    // propagate through `lhs = arr[i]` and `lhs = other_udt`. This must run
    // BEFORE FUNC bodies are compiled so FUNCs that access globals populated
    // in main still resolve field accesses (t2.n$) through the dotted-VARIABLE
    // path, which needs var_udt_type[obj] set.
    {
        // Fixpoint walk: a propagation may unlock another (arr[]->lhs->arr2[]).
        bool changed = true;
        int guard = 0;
        while (changed && guard++ < 8) {
            changed = false;
            std::function<void(const Stmt*)> scan = [&](const Stmt* s) {
                if (!s) return;
                // PUSH list, udt_var → var_udt_type[list[]] = type-of(udt_var)
                if ((s->kind == StmtKind::EXPR_STMT || s->kind == StmtKind::LET) &&
                    s->expr && s->expr->kind == ExprKind::CALL) {
                    const Expr* e = s->expr.get();
                    std::string up = e->func_name;
                    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                    if (up == "PUSH" && e->args.size() >= 2 &&
                        e->args[0]->kind == ExprKind::VARIABLE &&
                        e->args[1]->kind == ExprKind::VARIABLE) {
                        auto it = var_udt_type.find(e->args[1]->str_val);
                        if (it != var_udt_type.end()) {
                            std::string key = e->args[0]->str_val + "[]";
                            auto& slot = var_udt_type[key];
                            if (slot != it->second) { slot = it->second; changed = true; }
                        }
                    }
                }
                // LET/ASSIGN lhs = arr[i]  → var_udt_type[lhs] = var_udt_type[arr[]]
                // LET/ASSIGN lhs = var     → var_udt_type[lhs] = var_udt_type[var]
                if ((s->kind == StmtKind::LET || s->kind == StmtKind::ASSIGN) &&
                    !s->var_name.empty() && s->expr) {
                    if (s->expr->kind == ExprKind::INDEX && s->expr->left &&
                        s->expr->left->kind == ExprKind::VARIABLE) {
                        auto it = var_udt_type.find(s->expr->left->str_val + "[]");
                        if (it != var_udt_type.end()) {
                            auto& slot = var_udt_type[s->var_name];
                            if (slot != it->second) { slot = it->second; changed = true; }
                        }
                    } else if (s->expr->kind == ExprKind::VARIABLE) {
                        auto it = var_udt_type.find(s->expr->str_val);
                        if (it != var_udt_type.end()) {
                            auto& slot = var_udt_type[s->var_name];
                            if (slot != it->second) { slot = it->second; changed = true; }
                        }
                    }
                }
                // Recurse into nested bodies (FUNC/SUB/FOR/DO/TRY/IF-branches).
                for (auto& b : s->body) scan(b.get());
                for (auto& b : s->catch_body) scan(b.get());
                for (auto& b : s->finally_body) scan(b.get());
                for (auto& br : s->branches)
                    for (auto& bs : br.body) scan(bs.get());
            };
            for (auto& stmt : program) scan(stmt.get());
        }
    }

    for (auto& stmt : program) {
        if (!stmt) continue;
        if (stmt->kind == StmtKind::FUNCTION || stmt->kind == StmtKind::SUB) continue;
        // LET/DIM/ASSIGN at top level → declare global variable
        if ((stmt->kind == StmtKind::LET || stmt->kind == StmtKind::ASSIGN ||
             stmt->kind == StmtKind::DIM) && !stmt->var_name.empty()) {
            // Skip dotted names that are UDT field assignments (e.g. PLAYER1.NAME)
            size_t dp = stmt->var_name.find('.');
            if (dp != std::string::npos) {
                std::string prefix = stmt->var_name.substr(0, dp);
                if (udt_var_names.count(prefix)) continue;
            }
            // Don't shadow built-in constants PI/E
            {
                std::string up = stmt->var_name;
                std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                if (up == "PI" || up == "E") continue;
            }
            if (!lookup_var(stmt->var_name)) {
                // Phase 3 EXPLICIT: a bare top-level `x = ...` where x was
                // never DIM'd/LET'd is an error. The pre-pass would otherwise
                // auto-declare x globally (so codegen_let_or_assign's own
                // check misses it). DIM and LET both count as declarations;
                // only bare ASSIGN (x = ...) without prior binding errors.
                if (is_explicit_here(stmt->source_file) &&
                    stmt->kind == StmtKind::ASSIGN) {
                    report_error(stmt->source_file, stmt->line,
                        "undeclared variable '" + stmt->var_name + "'");
                }
                // Determine type from initial expression
                // Recursive helper to infer expression result type
                std::function<int(const Expr*)> infer_tag = [&](const Expr* e) -> int {
                    if (!e) return JD_TAG_I64;
                    if (e->kind == ExprKind::LITERAL_INT) return JD_TAG_I64;
                    if (e->kind == ExprKind::LITERAL_FLOAT) return JD_TAG_F64;
                    if (e->kind == ExprKind::LITERAL_STRING) return JD_TAG_STR;
                    if (e->kind == ExprKind::LITERAL_BOOL) return JD_TAG_I64;
                    if (e->kind == ExprKind::ARRAY_LITERAL) return JD_TAG_ARR;
                    if (e->kind == ExprKind::MAP_LITERAL) return JD_TAG_NATIVE_MAP;
                    if (e->kind == ExprKind::LAMBDA_EXPR) return JD_TAG_FUNCREF;
                    if (e->kind == ExprKind::CALL) {
                        if (e->func_name == "ZEROS" || e->func_name == "ONES" ||
                            e->func_name == "IOTA" || e->func_name == "RANGE" ||
                            e->func_name == "LINSPACE") return JD_TAG_ARR;
                        if (e->func_name == "POP") return JD_TAG_STR;
                        // User-defined functions: trust their declared return tag.
                        // (They aren't auto-vectorized.)
                        auto uit_pre = user_functions.find(e->func_name);
                        if (uit_pre != user_functions.end()) {
                            // ASYNC FUNC native call returns the task id (i64),
                            // not the body's return value — match infer_expr_tag.
                            if (uit_pre->second.is_async) return JD_TAG_I64;
                            return uit_pre->second.return_tag;
                        }
                        std::string upper = e->func_name;
                        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                        // Type-inference blocklist: functions that return a scalar
                        // even when given an array (SUM, LEN, …) — so infer_tag
                        // should NOT widen the return type to array for these.
                        // Distinct from the runtime auto-vec blocklist below.
                        static const std::unordered_set<std::string> no_vec_infer = {
                            "LEN","SUM","PRODUCT","MEAN","STDEV","MEDIAN","VARIANCE",
                            "MIN","MAX","ANY","ALL","COUNT","INDEXOF","REVERSE","SORT",
                            "TAKE","DROP","UNIQUE","APPEND","PUSH","POP","FLATTEN",
                            "TRANSPOSE","MATMUL","DOT","CROSS","CUMSUM","CUMPROD",
                            "SVD","QR","DET","EIG","FFT","IFFT",
                            "SCAN","SELECT","FILTER","REDUCE","TYPEOF","IIF",
                            "ZEROS","ONES","IOTA","RANGE","LINSPACE","TENSOR","RESHAPE",
                            "SPLIT","JOIN","FORMAT$","FRMV$","PACK$","UNPACK",
                            "REGEX_MATCH","REGEX.MATCH","REGEX.FINDALL",
                            "NOW","CVDATE","CDATE","DATE$","TIME$","TICK"
                        };
                        for (auto& a : e->args) {
                            if (a && infer_tag(a.get()) == JD_TAG_ARR && !no_vec_infer.count(upper)) {
                                return JD_TAG_ARR;
                            }
                        }
                        // Known array-returning functions (sync with bridge)
                        static const std::unordered_set<std::string> arr_returners = {
                            "SPLIT", "KEYS", "VALUES", "SORTBY", "GROUPBY",
                            "REGEX.FINDALL", "REGEX_MATCH", "REGEX_FINDALL",
                            "OS.LIST", "OS.ARGS",
                            "MAP.KEYS", "MAP.VALUES", "MAP.ITEMS",
                            "LINES", "WORDS", "CHARS", "UNPACK",
                            "TILED.SIZE", "TILED.TILE_SIZE", "TILED.LAYERS$",
                            "GFX.HSV_RGB", "GFX.TEXTSIZE", "SPRITE.COLLISIONS",
                            "DIR$"
                        };
                        if (arr_returners.count(upper)) return JD_TAG_ARR;
                        static const std::unordered_set<std::string> scalar_reducers = {
                            "SUM","PRODUCT","MIN","MAX","MEAN","STDEV","MEDIAN",
                            "VARIANCE","DOT","CROSS","REDUCE"
                        };
                        if (scalar_reducers.count(upper)) return JD_TAG_F64;
                        static const std::unordered_set<std::string> int_reducers = {
                            "LEN","COUNT","ANY","ALL","INDEXOF"
                        };
                        if (int_reducers.count(upper)) return JD_TAG_I64;
                        // VM-Value-handle returners (sync with bridge).
                        // TILED.OBJECTS returns an array of maps — the
                        // flat-JdbArray path can't carry OBJECT elements,
                        // so it goes through the VM-handle path instead.
                        static const std::unordered_set<std::string> obj_returners = {
                            "JSON.PARSE$", "TILED.PROPERTIES", "TILED.OBJECTS",
                            "MAP.FROM", "MAP.COPY", "FILE.STAT", "DATE.PARTS",
                            "HTTP.REQUEST",
                            "SVD", "QR", "EIG",
                            // MAT4.* returns a TENSOR Value; flat-array path
                            // can't carry the shape, so route through VM handle.
                            "MAT4.IDENTITY", "MAT4.PERSPECTIVE", "MAT4.LOOKAT",
                            "MAT4.TRANSLATE", "MAT4.ROTATE", "MAT4.SCALE", "MAT4.MUL"
                        };
                        if (obj_returners.count(upper) ||
                            (upper.size() > 4 && upper.substr(0, 4) == "MAP." &&
                             upper != "MAP.SIZE" && upper != "MAP.EXISTS" &&
                             upper != "MAP.KEYS" && upper != "MAP.VALUES"))
                            return JD_TAG_VM_HANDLE;
                        if (!e->func_name.empty() && e->func_name.back() == '$') return JD_TAG_STR;
                        // VM-bridged functions whose result is stored as an
                        // ISO string in native (dates without a $ suffix).
                        if (upper == "DATE.UTC") return JD_TAG_STR;
                        auto rit = runtime_funcs.find(upper);
                        if (rit != runtime_funcs.end()) return rit->second.return_tag;
                        auto uit = user_functions.find(e->func_name);
                        if (uit != user_functions.end()) return uit->second.return_tag;
                        return -1;
                    }
                    if (e->kind == ExprKind::VARIABLE) {
                        VarInfo* v = lookup_var(e->str_val);
                        if (v) return v->tag;
                        // Bare-identifier constants like PI, E
                        std::string up = e->str_val;
                        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                        auto rrit = runtime_funcs.find(up);
                        if (rrit != runtime_funcs.end() &&
                            LLVMCountParamTypes(rrit->second.fn_type) == 0)
                            return rrit->second.return_tag;
                        if (!e->str_val.empty() && e->str_val.back() == '$') return JD_TAG_STR;
                        return -1;
                    }
                    if (e->kind == ExprKind::BINARY) {
                        // Comparison / logical ops return BOOL (i64 0/1)
                        // regardless of operand types — without this branch
                        // a `DIM x = ("a" <> "b")` infers STR (from the
                        // operand types) and codegen creates a ptr-typed
                        // global, then stores the i64 BOOL result into it
                        // → segfault when later read as ptr.
                        switch (e->op) {
                            case TokenType::EQ:
                            case TokenType::NE:
                            case TokenType::ASSIGN:    // BASIC `=` doubles as comparison
                            case TokenType::LT:
                            case TokenType::GT:
                            case TokenType::LE:
                            case TokenType::GE:
                            case TokenType::AND:
                            case TokenType::OR:
                            case TokenType::ANDALSO:
                            case TokenType::ORELSE:
                            case TokenType::IN:
                                return JD_TAG_BOOL;
                            default: break;
                        }
                        int lt = infer_tag(e->left.get());
                        int rt = infer_tag(e->right.get());
                        if (lt == JD_TAG_ARR || rt == JD_TAG_ARR) return JD_TAG_ARR;
                        if (lt == JD_TAG_STR || rt == JD_TAG_STR) return JD_TAG_STR;
                        if (lt == JD_TAG_F64 || rt == JD_TAG_F64) return JD_TAG_F64;
                        return JD_TAG_I64;
                    }
                    if (e->kind == ExprKind::UNARY) return infer_tag(e->right.get());
                    if (e->kind == ExprKind::PIPE_EXPR) {
                        if (!e->right) return -1;
                        // LAMBDA right: scalar pipe returns f64, array pipe stays array.
                        if (e->right->kind == ExprKind::LAMBDA_EXPR) {
                            int lt = infer_tag(e->left.get());
                            if (lt == JD_TAG_ARR) return JD_TAG_ARR;
                            return JD_TAG_F64;
                        }
                        if (e->right->kind == ExprKind::VARIABLE ||
                            e->right->kind == ExprKind::LITERAL_STRING) {
                            auto uit = user_functions.find(e->right->str_val);
                            if (uit != user_functions.end())
                                return uit->second.return_tag;
                            return -1;
                        }
                        return infer_tag(e->right.get());
                    }
                    if (e->kind == ExprKind::PLACEHOLDER_EXPR) {
                        // codegen widens at runtime via the pipe's __PIPE_TMP__ tag.
                        return -1;
                    }
                    if (e->kind == ExprKind::INDEX) {
                        // Resolve the UDT type driving this INDEX, if any,
                        // so a string-keyed access can pick the right tag
                        // from the field schema. Two shapes:
                        //   v{"k"}       — v is a UDT instance
                        //   arr[i]{"k"}  — arr[] has known UDT element type
                        // Also catches `__TYPE__` which is always a string.
                        auto lookup_field_tag = [&](const std::string& type_name,
                                                    const Expr* key) -> int {
                            if (!key || key->kind != ExprKind::LITERAL_STRING) return -1;
                            const std::string& fname = key->str_val;
                            if (fname == "__TYPE__") return JD_TAG_STR;
                            if (!fname.empty() && fname.back() == '$') return JD_TAG_STR;
                            auto tit = udt_types.find(type_name);
                            if (tit == udt_types.end()) return -1;
                            for (auto& f : tit->second)
                                if (f.name == fname) return f.is_string ? JD_TAG_STR : JD_TAG_F64;
                            return -1;
                        };
                        if (e->left && e->left->kind == ExprKind::VARIABLE) {
                            if (var_udt_type.count(e->left->str_val + "[]"))
                                return JD_TAG_ARR;
                            VarInfo* lv = lookup_var(e->left->str_val);
                            if (lv && lv->tag == JD_TAG_VM_HANDLE) return JD_TAG_VM_HANDLE;
                            // UDT instance with string key — consult schema.
                            auto uit = var_udt_type.find(e->left->str_val);
                            if (uit != var_udt_type.end()) {
                                int ft = lookup_field_tag(uit->second, e->right.get());
                                if (ft >= 0) return ft;
                            }
                        }
                        // Nested INDEX: arr[i]{"key"} — drill into the
                        // inner INDEX and use the UDT-array's element type.
                        if (e->left && e->left->kind == ExprKind::INDEX &&
                            e->left->left && e->left->left->kind == ExprKind::VARIABLE) {
                            auto ait = var_udt_type.find(e->left->left->str_val + "[]");
                            if (ait != var_udt_type.end()) {
                                int ft = lookup_field_tag(ait->second, e->right.get());
                                if (ft >= 0) return ft;
                            }
                        }
                        // Unknown element type: return JD_TAG_F64 to match the legacy
                        // numeric-slot storage. Pointer-carrying elements are passed as
                        // punned f64 bits; downstream MAP_ACCESS / method-dispatch code
                        // pun-decodes back to a ptr. Returning JD_TAG_RUNTIME here would
                        // force method-dispatch sites to discover the receiver's true
                        // type from rtag, which the existing UDT call path doesn't do —
                        // breaking `nn.METHOD()`.
                        return JD_TAG_F64;
                    }
                    return -1;
                };
                int tag = JD_TAG_I64;
                bool tag_known = true;
                if (stmt->kind == StmtKind::DIM && !stmt->label.empty() &&
                    type_names.count(stmt->label))
                    tag = JD_TAG_ARR;   // UDT object (ptr)
                else if (stmt->kind == StmtKind::DIM && stmt->var_type == VarType::OBJECT)
                    tag = JD_TAG_NATIVE_MAP;
                else if (stmt->expr) {
                    int inferred = infer_tag(stmt->expr.get());
                    if (inferred >= 0) tag = inferred;
                    // Only skip for CALL expressions whose return type we
                    // couldn't resolve — these are typically VM-bridged
                    // scalars (GCD, LCM, …) where guessing i64 produces
                    // f64-bit-pun garbage. For other unknown RHS shapes
                    // (complex BINARY, etc.), keep the i64 default: that
                    // path is existing behavior and changing it here
                    // risks breaking unrelated codegen.
                    else if (stmt->expr->kind == ExprKind::CALL)
                        tag_known = false;
                }
                // Variables ending with $ are strings by convention,
                // regardless of what infer_tag picked up from the RHS.
                // EXCEPTION: `DIM names$[N] AS STRING` desugars to ZEROS([N])
                // and is a string ARRAY, not a string scalar. Forcing STR
                // here made codegen_index_assign early-return (its guard
                // requires tag == ARR/NATIVE_MAP/RUNTIME), silently dropping
                // every `names$[i] = "..."` write from inside SUB bodies.
                bool is_array_dim =
                    stmt->expr && stmt->expr->kind == ExprKind::CALL &&
                    stmt->expr->func_name == "ZEROS";
                if (stmt->var_name.size() > 1 && stmt->var_name.back() == '$' &&
                    !is_array_dim) {
                    tag = JD_TAG_STR;
                    tag_known = true;
                }
                // Skip pre-pass create_var when the type can't be inferred
                // statically. codegen_dim will create the var at codegen
                // time with the actual rhs.tag. Without this, VM-bridged
                // scalar calls like GCD/LCM return f64 but the pre-created
                // i64 slot caused the f64 bits to be read back as i64
                // garbage (see memory: project_native_dim_gcd_bug).
                if (tag_known)
                    create_var(stmt->var_name, tag);
                // Remember which source file this top-level DIM came from
                // so a SUB defined in the same file can write to the global
                // (the isolation rule in codegen_let_or_assign consults this).
                global_source_file[stmt->var_name] = stmt->source_file;
            }
        }
        // Also register destruct vars: [a, b, c] = expr
        if (stmt->kind == StmtKind::DESTRUCTURE) {
            for (auto& vn : stmt->destruct_vars) {
                if (!lookup_var(vn)) create_var(vn, JD_TAG_F64);  // f64 from array_get
            }
        }
    }

    // Pre-pass: register ENUM constants so functions can reference them.
    for (auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::ENUM_DECL)
            codegen_enum(*stmt);
    }

    // First pass: compile TYPE declarations (constructors + methods)
    for (auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::TYPE_DECL)
            codegen_type_decl(*stmt);
    }

    // Phase 5: the 16-iteration fixpoint pre-pass that propagated vm_array /
    // vm_handle flow across FUNC boundaries was retired here. It was made
    // redundant by Phase 2 (universal INDEX-arg tag=6 promotion) plus the
    // tag-6 unification in the VM bridge: VM-handle values now flow across
    // call boundaries via the tag system at runtime rather than needing a
    // static name-set pre-seed. The remaining per-codegen PUSH/DIM hints on
    // string_array_vars/map_array_vars/vm_array_vars still apply locally,
    // just without the cross-FUNC guesswork.

    // Pre-scan for tag-7 (runtime-tagged) assignments to globals — anywhere
    // in the program, including inside FUNC/SUB bodies. Without this,
    // codegen order matters: if SHOW() is compiled before INIT(), and INIT()
    // does `M.X = M.ARR[i]` (which stores tag-7), the global's in-memory
    // tag gets upgraded only when INIT is compiled. SHOW, already compiled,
    // loaded @M.X as i64 and skipped the rtag side-channel, producing
    // garbage at runtime. Pre-upgrade such globals now so every consumer
    // agrees on the tagged read path.
    {
        std::function<bool(const Expr*)> may_be_rtag = [&](const Expr* e) -> bool {
            if (!e) return false;
            // INDEX on anything: string-keyed map access and runtime-tagged
            // int-indexed reads both produce tag-7. Worst-case, assume yes.
            if (e->kind == ExprKind::INDEX) return true;
            if (e->kind == ExprKind::VARIABLE) {
                VarInfo* v = lookup_var(e->str_val);
                if (v && v->tag == JD_TAG_RUNTIME) return true;
                return false;
            }
            if (e->kind == ExprKind::CALL) {
                std::string upper = e->func_name;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                // Map/VM-handle returners produce containers whose indexed
                // reads are tag-7.
                static const std::unordered_set<std::string> vm_returners = {
                    "JSON.PARSE$","TILED.PROPERTIES","TILED.OBJECTS",
                    "MAP.FROM","MAP.COPY","MAP.GET"
                };
                if (vm_returners.count(upper)) return true;
                // User-defined functions: treat as unknown — be conservative
                // only when the return type is tag-7.
                auto uit = user_functions.find(e->func_name);
                if (uit != user_functions.end() &&
                    uit->second.return_tag == JD_TAG_RUNTIME) return true;
                return false;
            }
            return false;
        };
        std::function<void(const Stmt*)> scan = [&](const Stmt* s) {
            if (!s) return;
            if ((s->kind == StmtKind::LET || s->kind == StmtKind::ASSIGN ||
                 s->kind == StmtKind::DIM) && !s->var_name.empty() && s->expr) {
                VarInfo* g = nullptr;
                if (!scopes.empty()) {
                    auto it = scopes[0].vars.find(s->var_name);
                    if (it != scopes[0].vars.end()) g = &it->second;
                }
                if (g && g->tag != JD_TAG_RUNTIME && may_be_rtag(s->expr.get())) {
                    g->tag = JD_TAG_RUNTIME;
                    if (!g->runtime_tag_alloca) {
                        std::string rtag_name = s->var_name + ".rtag";
                        LLVMValueRef rg = LLVMAddGlobal(module, i32_type, rtag_name.c_str());
                        LLVMSetInitializer(rg, LLVMConstInt(i32_type, 0, 0));
                        LLVMSetLinkage(rg, LLVMInternalLinkage);
                        g->runtime_tag_alloca = rg;
                    }
                }
            }
            for (auto& b : s->body) scan(b.get());
            for (auto& b : s->catch_body) scan(b.get());
            for (auto& b : s->finally_body) scan(b.get());
            for (auto& br : s->branches)
                for (auto& bs : br.body) scan(bs.get());
        };
        // Fixpoint: propagation through several globals may need repeats
        // (e.g. A upgrades B, then B as RHS upgrades C).
        for (int iter = 0; iter < 6; ++iter) {
            size_t before = 0;
            if (!scopes.empty())
                for (auto& kv : scopes[0].vars)
                    if (kv.second.tag == JD_TAG_RUNTIME) ++before;
            for (auto& stmt : program) scan(stmt.get());
            size_t after = 0;
            if (!scopes.empty())
                for (auto& kv : scopes[0].vars)
                    if (kv.second.tag == JD_TAG_RUNTIME) ++after;
            if (after == before) break;
        }
    }

    // Second pass: compile all FUNC/SUB bodies
    for (auto& stmt : program) {
        if (stmt && (stmt->kind == StmtKind::FUNCTION || stmt->kind == StmtKind::SUB))
            codegen_function(*stmt);
    }

    // Third pass: compile top-level statements (skip FUNC/SUB/TYPE)
    for (auto& stmt : program) {
        if (stmt && stmt->kind != StmtKind::FUNCTION &&
            stmt->kind != StmtKind::SUB && stmt->kind != StmtKind::TYPE_DECL)
            codegen_stmt(*stmt);
    }
}

void LLVMCodegen::codegen_stmt(const Stmt& stmt) {
    // Skip if current block already has a terminator (unreachable code)
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        return;

    // Track the source file of the statement under codegen so diagnostics
    // raised from nested expressions can attribute "error at file:line".
    m_current_stmt_file = stmt.source_file;

    // Emit runtime trace if enabled (--trace flag)
    if (stmt.line > 0)
        emit_trace(stmt.line, stmt.source_file);

    try {
    switch (stmt.kind) {
        case StmtKind::LET:
        case StmtKind::ASSIGN:
            codegen_let_or_assign(stmt);
            break;
        case StmtKind::DIM:
            codegen_dim(stmt);
            break;
        case StmtKind::INDEX_ASSIGN:
            codegen_index_assign(stmt);
            break;
        case StmtKind::PRINT:
            codegen_print(stmt);
            break;
        case StmtKind::FOR_LOOP:
            codegen_for(stmt);
            break;
        case StmtKind::DO_LOOP:
            codegen_do_loop(stmt);
            break;
        case StmtKind::IF:
            codegen_if(stmt);
            break;
        case StmtKind::FUNCTION:
        case StmtKind::SUB:
            // Already compiled in first pass
            break;
        case StmtKind::RETURN:
            codegen_return(stmt);
            break;
        case StmtKind::EXPR_STMT:
            if (stmt.expr) codegen_expr(*stmt.expr);
            break;
        case StmtKind::SWITCH_STMT:
            codegen_switch(stmt);
            break;
        case StmtKind::FOR_EACH:
            codegen_for_each(stmt);
            break;
        case StmtKind::TRY_CATCH: {
            // TRY body: if a THROW fires (or guarded div-by-zero), control
            // branches to catch_bb. On normal completion, skip catch.
            LLVMBasicBlockRef try_bb    = LLVMAppendBasicBlock(current_fn, "try_body");
            LLVMBasicBlockRef catch_bb  = LLVMAppendBasicBlock(current_fn, "catch");
            LLVMBasicBlockRef after_bb  = LLVMAppendBasicBlock(current_fn, "after_try");

            // Snapshot the current recursion depth so the catch block can
            // restore it — the error-unwind path skips paired __rec_leave
            // calls, and we don't want that drift to leak out of the TRY.
            LLVMValueRef saved_depth = nullptr;
            {
                auto rd = runtime_funcs.find("__rec_depth");
                if (rd != runtime_funcs.end()) {
                    LLVMValueRef cur = LLVMBuildCall2(builder, rd->second.fn_type,
                                                     rd->second.fn, nullptr, 0, "rdepth");
                    saved_depth = LLVMBuildAlloca(builder, i64_type, "saved_depth");
                    LLVMBuildStore(builder, cur, saved_depth);
                }
            }

            LLVMBuildBr(builder, try_bb);
            LLVMPositionBuilderAtEnd(builder, try_bb);
            try_stack.push_back(catch_bb);
            for (auto& s : stmt.body) { if (s) codegen_stmt(*s); }
            try_stack.pop_back();
            // Only branch if the body didn't already terminate (RETURN/EXITDO/etc.)
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, after_bb);

            LLVMPositionBuilderAtEnd(builder, catch_bb);
            // Soft-clear: drop err_code only, so per-stmt checks inside
            // the catch body don't re-trip, but ERRMSG$ can still see the
            // caught message. Rewind the recursion counter in the same
            // spot — the unwind may have skipped paired __rec_leave calls.
            {
                auto& ec = runtime_funcs["__err_code_clear"];
                LLVMBuildCall2(builder, ec.fn_type, ec.fn, nullptr, 0, "");
                if (saved_depth) {
                    auto& rr = runtime_funcs["__rec_reset"];
                    LLVMValueRef d = LLVMBuildLoad2(builder, i64_type, saved_depth, "d");
                    LLVMValueRef args[] = { d };
                    LLVMBuildCall2(builder, rr.fn_type, rr.fn, args, 1, "");
                }
            }
            for (auto& s : stmt.catch_body) { if (s) codegen_stmt(*s); }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                // Hard-clear g_err_msg when leaving the catch body — a
                // THROW outside this TRY mustn't see the previously-caught
                // message leaking through ERRMSG$.
                auto& ec = runtime_funcs["__err_clear"];
                LLVMBuildCall2(builder, ec.fn_type, ec.fn, nullptr, 0, "");
                LLVMBuildBr(builder, after_bb);
            }

            LLVMPositionBuilderAtEnd(builder, after_bb);
            break;
        }
        case StmtKind::ENUM_DECL:
            // Already registered in the pre-pass — no-op here.
            break;
        case StmtKind::DESTRUCTURE: {
            // [a, b, c] = expr — evaluate expr, then assign each element.
            if (!stmt.expr) break;
            TypedValue arr = codegen_expr(*stmt.expr);
            if (arr.tag != JD_TAG_ARR) break;  // not an array, can't destructure
            auto& get_fn = runtime_funcs["__array_get"];
            for (size_t i = 0; i < stmt.destruct_vars.size(); i++) {
                LLVMValueRef idx = LLVMConstInt(i64_type, i, 0);
                LLVMValueRef args[] = { arr.val, idx };
                LLVMValueRef elem = LLVMBuildCall2(builder, get_fn.fn_type,
                    get_fn.fn, args, 2, "delem");
                // Store into variable (create if needed). Honour `$`-suffix
                // so `[bytes_written, json$] = ffi(...)` lands json$ in a
                // string slot — without this it would alloca a double, the
                // f64-bit-pun of the char* pointer survives the store but
                // any subsequent string use sees garbage.
                const std::string& vn = stmt.destruct_vars[i];
                bool is_str = !vn.empty() && vn.back() == '$';
                VarInfo* vi = lookup_var(vn);
                if (!vi) {
                    int tag = is_str ? JD_TAG_STR : JD_TAG_F64;
                    VarInfo& nv = create_var(vn, tag);
                    if (is_str) {
                        LLVMValueRef as_i = pun_f64_to_i64(elem);
                        LLVMValueRef ptr  = LLVMBuildIntToPtr(builder, as_i, i8_ptr_type, "ftoptr");
                        LLVMBuildStore(builder, ptr, nv.alloca_val);
                    } else {
                        LLVMBuildStore(builder, elem, nv.alloca_val);
                    }
                } else {
                    // Coerce to existing var's type
                    if (vi->tag == JD_TAG_I64) {
                        LLVMValueRef as_i = LLVMBuildFPToSI(builder, elem, i64_type, "ftoi");
                        LLVMBuildStore(builder, as_i, vi->alloca_val);
                    } else if (vi->tag == JD_TAG_STR) {
                        LLVMValueRef as_i = pun_f64_to_i64(elem);
                        LLVMValueRef ptr  = LLVMBuildIntToPtr(builder, as_i, i8_ptr_type, "ftoptr");
                        LLVMBuildStore(builder, ptr, vi->alloca_val);
                    } else {
                        LLVMBuildStore(builder, elem, vi->alloca_val);
                    }
                }
            }
            break;
        }
        case StmtKind::TYPE_DECL:
            codegen_type_decl(stmt);
            break;
        case StmtKind::THROW_STMT: {
            // Evaluate the error message (coerce to string), then either
            // jump to the enclosing catch or abort via jdb_throw_uncaught.
            LLVMValueRef msg_str;
            if (stmt.expr) {
                TypedValue mv = codegen_expr(*stmt.expr);
                msg_str = coerce_to(mv, i8_ptr_type);
            } else {
                msg_str = LLVMBuildGlobalStringPtr(builder, "", ".emsg");
            }
            auto& es = runtime_funcs["__err_set"];
            LLVMValueRef args[] = { msg_str, LLVMConstInt(i64_type, 1, 0) };
            LLVMBuildCall2(builder, es.fn_type, es.fn, args, 2, "");

            if (!try_stack.empty()) {
                LLVMBuildBr(builder, try_stack.back());
            } else {
                auto& uc = runtime_funcs["__throw_uncaught"];
                LLVMBuildCall2(builder, uc.fn_type, uc.fn, nullptr, 0, "");
                LLVMBuildUnreachable(builder);
            }
            // Any statements after a THROW are dead; give them a landing block
            // so subsequent codegen doesn't produce blocks with two terminators.
            LLVMBasicBlockRef dead = LLVMAppendBasicBlock(current_fn, "post_throw");
            LLVMPositionBuilderAtEnd(builder, dead);
            break;
        }
        case StmtKind::SLEEP_STMT:
            if (stmt.expr) {
                TypedValue sv = codegen_expr(*stmt.expr);
                if (sv.tag == JD_TAG_F64) sv.val = LLVMBuildFPToSI(builder, sv.val, i64_type, "ftoi");
                auto& fn = runtime_funcs["SLEEP"];
                LLVMValueRef args[] = { sv.val };
                LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "");
            }
            break;
        case StmtKind::CLS_STMT: {
            // CLS [r, g, b] — route through the VM bridge so graphics mode
            // clears the framebuffer (without this the back buffer keeps
            // old edge tiles between scrolls, producing flicker).
            Expr call;
            call.kind = ExprKind::CALL;
            call.func_name = "CLS";
            auto& exprs = const_cast<std::vector<ExprPtr>&>(stmt.print_exprs);
            for (auto& e : exprs)
                if (e) call.args.push_back(std::move(e));
            codegen_expr(call);
            for (size_t i = 0; i < call.args.size() && i < exprs.size(); i++)
                exprs[i] = std::move(call.args[i]);
            break;
        }
        case StmtKind::END_STMT: {
            // END exits the program — in main that's a real exit; inside a
            // SUB/FUNC it returns a default value. emit_fn_return chooses
            // the common exit block (user FUNC) or a direct ret (main).
            emit_fn_return(nullptr);
            break;
        }
        case StmtKind::EXIT_LOOP:
            // Parser overloads is_while=true to mean EXITFUNC (early return).
            if (stmt.is_while) {
                LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(current_fn));
                LLVMValueRef rv = nullptr;
                if (ret_ty == f64_type) {
                    // 0/0 = NaN, signalling NONE for TYPEOF(EarlyReturn(-1)).
                    union { uint64_t i; double d; } nan_bits;
                    nan_bits.i = 0xFFF8000000000000ULL;
                    rv = LLVMConstReal(f64_type, nan_bits.d);
                }
                emit_fn_return(rv);
                LLVMBasicBlockRef dead = LLVMAppendBasicBlock(current_fn, "post_exitfunc");
                LLVMPositionBuilderAtEnd(builder, dead);
            } else if (!loop_stack.empty()) {
                LLVMBuildBr(builder, loop_stack.top().break_bb);
            }
            break;
        case StmtKind::CONTINUE_LOOP:
            if (!loop_stack.empty())
                LLVMBuildBr(builder, loop_stack.top().continue_bb);
            break;
        case StmtKind::OPTION_STMT: {
            // Source directive: OPTION "NAME". The runtime VM handles colors
            // and other knobs via the __OPTION runtime call (interpreter);
            // here in native codegen we only intercept mode toggles that
            // affect static checking. Everything else is a no-op so the
            // statement compiles cleanly.
            if (stmt.expr && stmt.expr->kind == ExprKind::LITERAL_STRING) {
                std::string opt = stmt.expr->str_val;
                std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
                if (opt == "EXPLICITOFF" || opt == "NOEXPLICIT") explicit_mode = false;
                else if (opt == "EXPLICIT") explicit_mode = true;
                else if (opt == "NOSTRICT" || opt == "STRICTOFF") strict_mode = false;
                else if (opt == "STRICT") strict_mode = true;
                // Other options (NOCOLOR/NOPAUSE/...) are interpreter-only,
                // silently ignored by the native compiler.
            }
            break;
        }
        default:
            break;
    }
    } catch (const std::exception& e) {
        std::cerr << "[NATIVE] Warning: codegen error at line " << stmt.line
                  << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[NATIVE] Warning: unknown codegen error at line " << stmt.line << std::endl;
    }
    // After every completed statement, check whether an err was raised
    // during the statement's calls. Branches out to the enclosing TRY or
    // propagates up the call chain — this is the native analogue of the
    // VM's C++ exception unwinding.
    emit_err_check();
}

// ── FUNC / SUB ──────────────────────────────────────────────

void LLVMCodegen::emit_err_check() {
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) return;
    auto ec_it = runtime_funcs.find("__err_rc");
    if (ec_it == runtime_funcs.end()) return;

    // Pull bridge-side errors (set inside jdrt_call_typed_* via VM
    // exception handling) into g_err_msg so the per-stmt check can see
    // them uniformly with native-runtime errors. Clear the bridge slot
    // right after so the next stmt doesn't re-read the same error.
    auto le_it = runtime_funcs.find("__jdrt_last_error");
    auto cle_it = runtime_funcs.find("__jdrt_clear_last_error");
    auto es_it = runtime_funcs.find("__err_set");
    LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
    if (le_it != runtime_funcs.end() && es_it != runtime_funcs.end() && hg) {
        LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
        LLVMValueRef le_args[] = { rt };
        LLVMValueRef bmsg = LLVMBuildCall2(builder, le_it->second.fn_type,
                                           le_it->second.fn, le_args, 1, "blast");
        LLVMValueRef has_bmsg = LLVMBuildICmp(builder, LLVMIntNE, bmsg,
                                              LLVMConstNull(i8_ptr_type), "has_bmsg");
        LLVMBasicBlockRef pull_bb = LLVMAppendBasicBlock(current_fn, "bmsg_pull");
        LLVMBasicBlockRef post_bb = LLVMAppendBasicBlock(current_fn, "bmsg_post");
        LLVMBuildCondBr(builder, has_bmsg, pull_bb, post_bb);

        LLVMPositionBuilderAtEnd(builder, pull_bb);
        LLVMValueRef es_args[] = { bmsg, LLVMConstInt(i64_type, 1, 0) };
        LLVMBuildCall2(builder, es_it->second.fn_type, es_it->second.fn, es_args, 2, "");
        if (cle_it != runtime_funcs.end()) {
            LLVMBuildCall2(builder, cle_it->second.fn_type, cle_it->second.fn, le_args, 1, "");
        }
        LLVMBuildBr(builder, post_bb);

        LLVMPositionBuilderAtEnd(builder, post_bb);
    }

    LLVMValueRef err = LLVMBuildCall2(builder, ec_it->second.fn_type,
                                      ec_it->second.fn, nullptr, 0, "err_rc");
    LLVMValueRef has_err = LLVMBuildICmp(builder, LLVMIntNE, err,
                                         LLVMConstInt(i64_type, 0, 0), "has_err");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlock(current_fn, "call_err");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlock(current_fn, "call_ok");
    LLVMBuildCondBr(builder, has_err, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(builder, err_bb);
    if (!try_stack.empty()) {
        LLVMBuildBr(builder, try_stack.back());
    } else if (current_exit_bb) {
        emit_fn_return(nullptr);
    } else {
        auto& uc = runtime_funcs["__throw_uncaught"];
        LLVMBuildCall2(builder, uc.fn_type, uc.fn, nullptr, 0, "");
        LLVMBuildUnreachable(builder);
    }
    LLVMPositionBuilderAtEnd(builder, ok_bb);
}

void LLVMCodegen::emit_fn_return(LLVMValueRef val) {
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(current_fn));
    bool is_void = (ret_ty == void_type);
    if (current_exit_bb) {
        if (!is_void && current_retval_alloca) {
            LLVMValueRef v = val ? val : LLVMConstNull(ret_ty);
            LLVMBuildStore(builder, v, current_retval_alloca);
        }
        LLVMBuildBr(builder, current_exit_bb);
        return;
    }
    // Fallback for main / any frame without an exit block set up.
    if (is_void) LLVMBuildRetVoid(builder);
    else if (ret_ty == i32_type) LLVMBuildRet(builder,
        val ? val : LLVMConstInt(i32_type, 0, 0));
    else if (ret_ty == f64_type) LLVMBuildRet(builder,
        val ? val : LLVMConstReal(f64_type, 0.0));
    else LLVMBuildRet(builder, val ? val : LLVMConstNull(ret_ty));
}

// Emit `TypeName.DISPOSE(slot)` for each tracked local UDT in the current
// function. For array-typed slots, iterate the array elements and dispose
// every entry. Called from codegen_function's exit_bb just before
// __rec_leave / Ret.
void LLVMCodegen::emit_dispose_cleanup() {
    for (auto& d : dispose_locals) {
        VarInfo* vi = lookup_var(d.var_name);
        if (!vi) continue;
        auto dit = user_functions.find(d.type_name + ".DISPOSE");
        if (dit == user_functions.end()) continue;
        LLVMTypeRef dft = LLVMGlobalGetValueType(dit->second.fn);
        LLVMValueRef slot = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "dsp.slot");
        if (!d.is_array) {
            LLVMValueRef dargs[] = { slot };
            LLVMBuildCall2(builder, dft, dit->second.fn, dargs, 1, "");
            continue;
        }
        // Array of UDTs: iterate slots and call DISPOSE on each.
        auto& arr_len = runtime_funcs["LEN"];
        auto& arr_get = runtime_funcs["__array_get"];
        LLVMValueRef len_args[] = { slot };
        LLVMValueRef len = LLVMBuildCall2(builder, arr_len.fn_type, arr_len.fn, len_args, 1, "dsp.len");

        LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "dsp.loop");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "dsp.body");
        LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "dsp.end");
        LLVMValueRef i_alloca = LLVMBuildAlloca(builder, i64_type, "dsp.i");
        LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), i_alloca);
        LLVMBuildBr(builder, loop_bb);

        LLVMPositionBuilderAtEnd(builder, loop_bb);
        LLVMValueRef i = LLVMBuildLoad2(builder, i64_type, i_alloca, "i");
        LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, i, len, "cmp");
        LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(builder, body_bb);
        LLVMValueRef ga[] = { slot, i };
        LLVMValueRef elem_f = LLVMBuildCall2(builder, arr_get.fn_type, arr_get.fn, ga, 2, "elem.f");
        LLVMValueRef elem_i = pun_f64_to_i64(elem_f);
        LLVMValueRef elem_p = LLVMBuildIntToPtr(builder, elem_i, i8_ptr_type, "elem.p");
        LLVMValueRef da[] = { elem_p };
        LLVMBuildCall2(builder, dft, dit->second.fn, da, 1, "");
        LLVMValueRef next = LLVMBuildAdd(builder, i, LLVMConstInt(i64_type, 1, 0), "next");
        LLVMBuildStore(builder, next, i_alloca);
        LLVMBuildBr(builder, loop_bb);

        LLVMPositionBuilderAtEnd(builder, end_bb);
    }
}

void LLVMCodegen::codegen_function(const Stmt& stmt) {
    std::string fn_name = stmt.func_name;
    auto fit = user_functions.find(fn_name);
    if (fit == user_functions.end()) return;

    // Save current state
    LLVMValueRef saved_fn = current_fn;
    std::string saved_fn_src = current_fn_source_file;
    LLVMBasicBlockRef saved_exit = current_exit_bb;
    LLVMValueRef saved_retval = current_retval_alloca;
    auto saved_dispose = std::move(dispose_locals);
    dispose_locals.clear();
    current_fn = fit->second.fn;
    current_fn_source_file = stmt.source_file;

    // Push function scope
    scopes.push_back(Scope{});

    // Create entry block
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, current_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    // Retval alloca + common exit block — every RET site writes here and
    // branches to exit_bb. The exit block is responsible for the single
    // recursion_leave + ret.
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(current_fn));
    bool is_void_fn = (ret_ty == void_type);
    current_retval_alloca = is_void_fn ? nullptr
                                       : LLVMBuildAlloca(builder, ret_ty, "retval");
    current_exit_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "fn.exit");

    // Recursion guard — if over the limit, rec_enter has already set the
    // error state. We branch straight to exit_bb (which will NOT emit
    // leave when the error path reached it, but for simplicity always
    // calls leave — counter reset on catch compensates for drift).
    auto& re = runtime_funcs["__rec_enter"];
    LLVMValueRef rc = LLVMBuildCall2(builder, re.fn_type, re.fn, nullptr, 0, "rec_rc");
    LLVMValueRef too_deep = LLVMBuildICmp(builder, LLVMIntNE, rc,
                                          LLVMConstInt(i32_type, 0, 0), "too_deep");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "fn.body");
    LLVMBuildCondBr(builder, too_deep, current_exit_bb, body_bb);

    LLVMPositionBuilderAtEnd(builder, body_bb);

    // Zero-initialise retval so the overflow path returns a safe default.
    if (current_retval_alloca) {
        LLVMBuildStore(builder, LLVMConstNull(ret_ty), current_retval_alloca);
    }

    // Create allocas for parameters — use param_tags for types.
    // Tag-aware ABI: a JD_TAG_RUNTIME param consumes TWO LLVM args
    // (i64 val + i32 tag) so the body can recover the dynamic type at
    // runtime via lookup_var's runtime_tag_alloca path. Track the LLVM
    // argument index separately from the source param index because they
    // diverge whenever a param is RUNTIME-typed.
    bool is_evt_handler = (stmt.kind == StmtKind::SUB) &&
                          event_handler_subs.count(stmt.func_name);
    unsigned llvm_arg_idx = 0;
    for (size_t i = 0; i < stmt.params.size() && i < fit->second.param_tags.size(); i++) {
        int ptag = fit->second.param_tags[i];
        VarInfo& vi = create_var(stmt.params[i].name, ptag);
        LLVMBuildStore(builder, LLVMGetParam(current_fn, llvm_arg_idx), vi.alloca_val);
        llvm_arg_idx++;
        if (ptag == JD_TAG_RUNTIME) {
            // Allocate the tag slot here (create_var doesn't do it for us)
            // and store the second LLVM arg.
            std::string rtag_name = stmt.params[i].name + ".rtag";
            vi.runtime_tag_alloca = LLVMBuildAlloca(builder, i32_type, rtag_name.c_str());
            LLVMBuildStore(builder, LLVMGetParam(current_fn, llvm_arg_idx),
                           vi.runtime_tag_alloca);
            llvm_arg_idx++;
        }
        if (is_evt_handler && i == 0) {
            map_array_vars.insert(stmt.params[i].name);
        }
    }

    // Compile body
    for (auto& s : stmt.body) {
        if (s) codegen_stmt(*s);
    }

    // If no terminator yet, branch to the unified exit block. Implicit
    // return value is zero for numeric funcs and nullptr for ptr funcs —
    // matches the old behaviour.
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
        if (current_retval_alloca) {
            LLVMBuildStore(builder, LLVMConstNull(ret_ty), current_retval_alloca);
        }
        LLVMBuildBr(builder, current_exit_bb);
    }

    // exit_bb: dispose tracked UDT locals → recursion_leave → ret.
    LLVMPositionBuilderAtEnd(builder, current_exit_bb);
    emit_dispose_cleanup();
    auto& rl = runtime_funcs["__rec_leave"];
    LLVMBuildCall2(builder, rl.fn_type, rl.fn, nullptr, 0, "");
    if (is_void_fn) {
        LLVMBuildRetVoid(builder);
    } else {
        LLVMValueRef rv = LLVMBuildLoad2(builder, ret_ty, current_retval_alloca, "rv");
        LLVMBuildRet(builder, rv);
    }

    // Pop scope and restore
    scopes.pop_back();
    current_fn = saved_fn;
    current_fn_source_file = saved_fn_src;
    current_exit_bb = saved_exit;
    current_retval_alloca = saved_retval;
    dispose_locals = std::move(saved_dispose);

    // Position builder back at the end of the saved function's last block
    LLVMBasicBlockRef last_bb = LLVMGetLastBasicBlock(saved_fn);
    LLVMPositionBuilderAtEnd(builder, last_bb);
}

// ── RETURN ──────────────────────────────────────────────────

void LLVMCodegen::codegen_return(const Stmt& stmt) {
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(current_fn));
    if (stmt.expr && ret_ty != void_type) {
        TypedValue rv = codegen_expr(*stmt.expr);
        emit_fn_return(coerce_to(rv, ret_ty));
    } else {
        emit_fn_return(nullptr);
    }
}

// ── LET / DIM / ASSIGN ─────────────────────────────────────

void LLVMCodegen::codegen_let_or_assign(const Stmt& stmt) {
    if (!stmt.expr) return;

    // Phase 3 EXPLICIT: under OPTION "EXPLICIT", a bare `x = ...` for a
    // name that was never DIM'd (and isn't a FOR variable, parameter, or
    // dotted module/UDT write) is a compile error. Skip when kind == DIM
    // (DIMs are declarations, routed here for DIM-without-init); skip
    // dotted names (module writes / UDT fields, handled elsewhere).
    if (is_explicit_here(stmt.source_file) && stmt.kind == StmtKind::ASSIGN &&
        stmt.var_name.find('.') == std::string::npos) {
        VarInfo* prior = lookup_var(stmt.var_name);
        bool declared_global = type_env.count(stmt.var_name) > 0;
        if (!prior && !declared_global) {
            report_error(stmt.source_file, stmt.line,
                "undeclared variable '" + stmt.var_name + "'");
            // Fall through: let the normal codegen path run (auto-creates the
            // var) so downstream uses of this name don't cascade more errors
            // for the same root cause. The accumulated diagnostic still aborts
            // the compile cleanly in compile()'s flush pass.
        }
    }

    // Phase 4 STRICT: assignment to a declared global must have a
    // compatible RHS. Only checked when the LHS is a known global —
    // locals lack a StaticType table today (future phase extends).
    if (is_strict_here(stmt.source_file) && stmt.kind != StmtKind::DIM &&
        stmt.expr && stmt.var_name.find('.') == std::string::npos) {
        auto tit = type_env.find(stmt.var_name);
        if (tit != type_env.end() && !tit->second.is_unknown()) {
            StaticType actual = infer_expr_type(*stmt.expr);
            if (!actual.is_unknown() &&
                !types_compatible(actual, tit->second)) {
                report_error(stmt.source_file, stmt.line,
                    "Type Mismatch in assignment to '" + stmt.var_name +
                    "': expected " + tit->second.describe() +
                    ", got " + actual.describe());
            }
        }
    }

    // User-declared CONST: first LET registers; later assignments throw.
    {
        std::string up_name = stmt.var_name;
        std::transform(up_name.begin(), up_name.end(), up_name.begin(), ::toupper);
        // Track BOOLEAN-typed vars so TYPEOF reports "BOOLEAN" (storage is i64).
        if (stmt.var_type == VarType::BOOLEAN) bool_vars.insert(up_name);
        else if (stmt.expr && stmt.expr->kind == ExprKind::LITERAL_BOOL)
            bool_vars.insert(up_name);
        // Track date-producing initializers so TYPEOF reports "DATE".
        if (stmt.expr && stmt.expr->kind == ExprKind::CALL) {
            std::string fn_up = stmt.expr->func_name;
            std::transform(fn_up.begin(), fn_up.end(), fn_up.begin(), ::toupper);
            if (fn_up == "CVDATE" || fn_up == "CDATE" || fn_up == "DATEADD" || fn_up == "NOW")
                date_vars.insert(up_name);
        }
        if (stmt.is_const) {
            const_vars.insert(up_name);
            // Fall through to normal LET codegen so the value is stored.
        } else if (const_vars.count(up_name)) {
            std::string msg = "Cannot assign to constant '" + stmt.var_name + "'";
            LLVMValueRef msg_str = LLVMBuildGlobalStringPtr(builder, msg.c_str(), ".const_err");
            auto& es = runtime_funcs["__err_set"];
            LLVMValueRef args[] = { msg_str, LLVMConstInt(i64_type, 1, 0) };
            LLVMBuildCall2(builder, es.fn_type, es.fn, args, 2, "");
            if (!try_stack.empty()) {
                LLVMBuildBr(builder, try_stack.back());
            } else {
                auto& uc = runtime_funcs["__throw_uncaught"];
                LLVMBuildCall2(builder, uc.fn_type, uc.fn, nullptr, 0, "");
                LLVMBuildUnreachable(builder);
            }
            LLVMBasicBlockRef dead = LLVMAppendBasicBlock(current_fn, "post_const_throw");
            LLVMPositionBuilderAtEnd(builder, dead);
            return;
        }
    }

    // Protect built-in constants: assignments to PI/E (case-insensitive)
    // throw at runtime (matches interpreter) so TRY/CATCH can observe them.
    {
        std::string up = stmt.var_name;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
        if (up == "PI" || up == "E") {
            std::string msg = "Cannot assign to constant '" + stmt.var_name + "'";
            LLVMValueRef msg_str = LLVMBuildGlobalStringPtr(builder, msg.c_str(), ".const_err");
            auto& es = runtime_funcs["__err_set"];
            LLVMValueRef args[] = { msg_str, LLVMConstInt(i64_type, 1, 0) };
            LLVMBuildCall2(builder, es.fn_type, es.fn, args, 2, "");
            if (!try_stack.empty()) {
                LLVMBuildBr(builder, try_stack.back());
            } else {
                auto& uc = runtime_funcs["__throw_uncaught"];
                LLVMBuildCall2(builder, uc.fn_type, uc.fn, nullptr, 0, "");
                LLVMBuildUnreachable(builder);
            }
            LLVMBasicBlockRef dead = LLVMAppendBasicBlock(current_fn, "post_const_throw");
            LLVMPositionBuilderAtEnd(builder, dead);
            return;
        }
    }

    // Check for dotted UDT field assignment: Player1.Name = "Atomi"
    size_t dot_pos = stmt.var_name.find('.');
    if (dot_pos != std::string::npos) {
        std::string obj_name = stmt.var_name.substr(0, dot_pos);
        std::string field_name = stmt.var_name.substr(dot_pos + 1);
        auto tit = var_udt_type.find(obj_name);
        if (tit != var_udt_type.end()) {
            VarInfo* vi = lookup_var(obj_name);
            if (vi) {
                LLVMValueRef obj_ptr;
                if (vi->tag == JD_TAG_RUNTIME) {
                    LLVMValueRef bits = LLVMBuildLoad2(builder, i64_type, vi->alloca_val, "obj_i64");
                    obj_ptr = LLVMBuildIntToPtr(builder, bits, i8_ptr_type, "obj");
                } else {
                    obj_ptr = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "obj");
                }
                LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, field_name.c_str(), ".fld");
                TypedValue val = codegen_expr(*stmt.expr);
                bool is_str = val.tag == JD_TAG_STR || is_udt_string_field(obj_name, field_name);
                if (is_str) {
                    auto& set_fn = runtime_funcs["__udt_set_str"];
                    LLVMValueRef args[] = { obj_ptr, field_str, to_string_ptr(val) };
                    LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
                } else {
                    LLVMValueRef fval = coerce_to(val, f64_type);
                    auto& set_fn = runtime_funcs["__udt_set_f64"];
                    LLVMValueRef args[] = { obj_ptr, field_str, fval };
                    LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
                }
                return;
            }
        }
    }

    // Propagate UDT type through `lhs = arr[i]` when the array's element
    // type is known (registered as `arr[]` in var_udt_type). Same for
    // `lhs = other_udt_var`. Without this, `mc_c.SET_VAL` can't resolve
    // the method since mc_c has no UDT type binding.
    if (stmt.expr) {
        if (stmt.expr->kind == ExprKind::INDEX && stmt.expr->left &&
            stmt.expr->left->kind == ExprKind::VARIABLE) {
            auto it = var_udt_type.find(stmt.expr->left->str_val + "[]");
            if (it != var_udt_type.end())
                var_udt_type[stmt.var_name] = it->second;
        } else if (stmt.expr->kind == ExprKind::VARIABLE) {
            auto it = var_udt_type.find(stmt.expr->str_val);
            if (it != var_udt_type.end())
                var_udt_type[stmt.var_name] = it->second;
        }
    }

    // Funcref via `name@` — the parser flattens it to a LITERAL_STRING
    // holding the target name. Detect the match against a known user
    // function and bind the RHS to the LLVM function pointer (tag=5)
    // so later `fn_ref(args)` lands in the indirect-call path.
    // Hint: if the destination var has a known type, propagate it so an
    // INDEX leaf on a Map / VM object returns a value of the right type
    // instead of a stringified one (broken on numeric assignments).
    int rhs_leaf_hint = -1;
    {
        VarInfo* dest = lookup_var(stmt.var_name);
        if (dest && (dest->tag == JD_TAG_I64 || dest->tag == JD_TAG_F64 || dest->tag == JD_TAG_STR || dest->tag == JD_TAG_ARR))
            rhs_leaf_hint = dest->tag;
        else if (!stmt.var_name.empty() && stmt.var_name.back() == '$')
            rhs_leaf_hint = JD_TAG_STR;
        // Untyped new var: no hint → INDEX-on-VM-handle returns another
        // handle (tag 6). coerce_to materialises on use. This lets the
        // same `x = a{"k"}` work whether the value is a number, string,
        // array, or nested object.
    }
    // If the RHS is an array literal that contains any string element,
    // or a $-suffixed function call known to return an array of strings,
    // mark the LHS variable as string-holding so later INDEX access
    // returns tag=2 (string) instead of the default tag=1 (f64 punned).
    // If the literal mixes string and non-string elements (e.g. CSV-style
    // [1, "Alice", 90]), tag the variable as mixed instead — INDEX on a
    // mixed var calls the runtime classifier per cell and returns a
    // RUNTIME-tagged value, since tagging the whole var as string would
    // pun the numeric cells into bogus pointers and crash on use.
    // Track scalar-string vars so subsequent ARRAY_LITERALs that name them
    // can recognise [s1, s2, s3] as a string array (the literal-string-only
    // check would miss VARIABLE elements).
    {
        bool wants_str_slot =
            stmt.var_type == VarType::STRING ||
            (!stmt.var_name.empty() && stmt.var_name.back() == '$');
        bool rhs_is_str_lit = stmt.expr && stmt.expr->kind == ExprKind::LITERAL_STRING;
        bool rhs_is_str_call = stmt.expr && stmt.expr->kind == ExprKind::CALL &&
            !stmt.expr->func_name.empty() && stmt.expr->func_name.back() == '$';
        if (wants_str_slot || rhs_is_str_lit || rhs_is_str_call)
            string_scalar_vars.insert(stmt.var_name);
    }
    if (stmt.expr && stmt.expr->kind == ExprKind::ARRAY_LITERAL) {
        auto el_is_string = [&](const Expr* e) -> bool {
            if (!e) return false;
            if (e->kind == ExprKind::LITERAL_STRING) return true;
            if (e->kind == ExprKind::VARIABLE) {
                if (!e->str_val.empty() && e->str_val.back() == '$') return true;
                return string_scalar_vars.count(e->str_val) != 0;
            }
            if (e->kind == ExprKind::CALL) {
                if (!e->func_name.empty() && e->func_name.back() == '$') return true;
                // $-less builtins that nonetheless return strings.
                std::string u = e->func_name;
                std::transform(u.begin(), u.end(), u.begin(), ::toupper);
                if (u == "JOIN") return true;
            }
            return false;
        };
        // INDEX-expressions (map / VM-handle / UDT field reads) return a
        // INDEX expressions (map / VM-handle / UDT field reads) resolve to
        // RUNTIME-tagged values whose real type is execution-time only —
        // mark the destination as mixed so arr[i] reads dispatch per cell
        // via __arr_classify instead of punning every element as a number.
        auto el_is_runtime_typed = [&](const Expr* e) -> bool {
            if (!e) return false;
            return e->kind == ExprKind::INDEX;
        };
        bool has_str = false, has_non_str = false, has_runtime = false;
        for (auto& a : stmt.expr->args) {
            if (!a) continue;
            if (el_is_string(a.get())) has_str = true;
            else if (el_is_runtime_typed(a.get())) has_runtime = true;
            else has_non_str = true;
        }
        if (has_runtime)                 mixed_array_vars.insert(stmt.var_name);
        else if (has_str && has_non_str) mixed_array_vars.insert(stmt.var_name);
        else if (has_str)                string_array_vars.insert(stmt.var_name);
    }
    if (stmt.expr && stmt.expr->kind == ExprKind::CALL &&
        !stmt.expr->func_name.empty()) {
        std::string u = stmt.expr->func_name;
        std::transform(u.begin(), u.end(), u.begin(), ::toupper);
        if (u == "SPLIT" || u == "TILED.LAYERS$" || u == "LINES" ||
            u == "WORDS" || u == "CHARS")
            string_array_vars.insert(stmt.var_name);
        // STR$(arr) returns an array of strings — mark so arr[i] reads
        // back as STRING instead of the default-punned f64.
        if (u == "STR$" && !stmt.expr->args.empty()) {
            string_array_vars.insert(stmt.var_name);
        }
        // OS.ARGS() returns argv as a 1D string array — without this
        // tracking, args[i] decoded as f64 garbage (regression 2026-05-01).
        if (u == "OS.ARGS")
            string_array_vars.insert(stmt.var_name);
        // DIR$(wildcard$, [extended_info]) — flat form is 1D string array;
        // extended_info=TRUE produces a 2D mixed-type matrix (filename
        // strings + size integers + type strings + dates). Without this
        // tag, names[i] decoded as f64 garbage.
        if (u == "DIR$") {
            bool extended = false;
            if (stmt.expr->args.size() >= 2 && stmt.expr->args[1]) {
                auto& arg = *stmt.expr->args[1];
                if (arg.kind == ExprKind::LITERAL_BOOL)      extended = arg.bool_val;
                else if (arg.kind == ExprKind::LITERAL_INT)  extended = (arg.int_val != 0);
                else                                          extended = true;  // defensive
            }
            if (extended) mixed_array_vars.insert(stmt.var_name);
            else          string_array_vars.insert(stmt.var_name);
        }
        // SELECT(fn@, arr) inherits its element type from fn's return.
        // Detect string-returning callees: $-suffix on the funcref name,
        // or a user FUNC already known to return a string array (FUNCs
        // returning a single string also count — apply per element).
        if (u == "SELECT" && !stmt.expr->args.empty() && stmt.expr->args[0]) {
            auto& fn_arg = *stmt.expr->args[0];
            bool fn_returns_str = false;
            if (fn_arg.kind == ExprKind::LITERAL_STRING && fn_arg.is_funcref_lit &&
                !fn_arg.str_val.empty() && fn_arg.str_val.back() == '$') {
                fn_returns_str = true;
            }
            if (fn_returns_str) string_array_vars.insert(stmt.var_name);
        }
        // UNIQUE(string_arr) and similar 1D filters preserve element type.
        if ((u == "UNIQUE" || u == "REVERSE" || u == "SORT" || u == "TAKE" || u == "DROP")
            && !stmt.expr->args.empty() && stmt.expr->args[0] &&
            stmt.expr->args[0]->kind == ExprKind::VARIABLE &&
            string_array_vars.count(stmt.expr->args[0]->str_val)) {
            string_array_vars.insert(stmt.var_name);
        }
        // APPEND(arr, val) / APPEND(arr_a, arr_b) — propagates the element
        // tag from its inputs. The runtime memcpy-merges the bits regardless,
        // but the codegen needs to know the result holds strings so subscript
        // / FOR EACH on the result var still decode correctly. Without this,
        // walk()-style helpers that build a string list with rec APPENDs
        // produce a result that prints as zeros under -c.
        if (u == "APPEND" && stmt.expr->args.size() >= 2) {
            // Recognise scalar-string expressions inside a [scalar, scalar, ...]
            // literal — handles VARIABLE refs to string locals, $-suffix calls,
            // and known $-less string-returners like JOIN. Without this, an
            // APPEND(arr, [JOIN(...)]) wouldn't tag the result as a string
            // array because the inner CALL doesn't end in $.
            auto is_string_scalar_expr = [&](const Expr* e) -> bool {
                if (!e) return false;
                if (e->kind == ExprKind::LITERAL_STRING) return true;
                if (e->kind == ExprKind::VARIABLE) {
                    if (!e->str_val.empty() && e->str_val.back() == '$') return true;
                    return string_scalar_vars.count(e->str_val) != 0;
                }
                if (e->kind == ExprKind::CALL) {
                    if (!e->func_name.empty() && e->func_name.back() == '$') return true;
                    std::string u = e->func_name;
                    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
                    if (u == "JOIN") return true;
                }
                return false;
            };
            auto is_string_arr_expr = [&](const Expr* e) -> bool {
                if (!e) return false;
                if (e->kind == ExprKind::VARIABLE)
                    return string_array_vars.count(e->str_val) != 0;
                if (e->kind == ExprKind::ARRAY_LITERAL) {
                    bool any = false;
                    for (auto& a : e->args) {
                        if (!a) continue;
                        if (!is_string_scalar_expr(a.get())) return false;
                        any = true;
                    }
                    return any;
                }
                if (e->kind == ExprKind::LITERAL_STRING) return true;
                if (e->kind == ExprKind::CALL)
                    return string_array_returning_funcs.count(e->func_name) != 0;
                return false;
            };
            // Liberal OR: either side proving string-array-ness is enough.
            // Lets `g_paths = APPEND(g_paths, [JOIN(...)])` tag g_paths on
            // the first append even when g_paths started as an empty `[]`
            // and isn't yet in string_array_vars.
            if (is_string_arr_expr(stmt.expr->args[0].get()) ||
                is_string_arr_expr(stmt.expr->args[1].get()) ||
                is_string_scalar_expr(stmt.expr->args[1].get())) {
                string_array_vars.insert(stmt.var_name);
            }
        }
        // User FUNC whose RETURN is a string-array — Phase-1 figures this out
        // and populates string_array_returning_funcs. Without this, the
        // caller's `DIM files = walk(root)` would leave `files` untracked
        // and files[i] would decode as f64 garbage.
        if (string_array_returning_funcs.count(stmt.expr->func_name)) {
            string_array_vars.insert(stmt.var_name);
        }
    }
    TypedValue rhs;
    {
        // Apply the LHS-derived leaf hint ONLY for the RHS codegen scope.
        // RAII auto-restores the outer caller's hint on scope exit, so an
        // assignment buried inside an expression context can never clobber
        // an outer frame's pending hint.
        ScopedLeafTag _lt(this, rhs_leaf_hint);
        if (stmt.expr->kind == ExprKind::LITERAL_STRING) {
            auto fit = user_functions.find(stmt.expr->str_val);
            if (fit != user_functions.end()) {
                rhs = { fit->second.fn, JD_TAG_FUNCREF };
            } else {
                rhs = codegen_expr(*stmt.expr);
            }
        } else {
            rhs = codegen_expr(*stmt.expr);
        }
    }

    VarInfo* vi = lookup_var(stmt.var_name);
    // Cross-module isolation: a bare-name assignment inside a SUB/FUNC
    // imported from one file must not write to a same-named global DIM'd
    // in another file — a module's local `result` and main's `result` are
    // semantically separate. We force a fresh local in that case.
    //
    // Same-file writes DO pass through to the global though. The Assert
    // SUB pattern (comprehensive_test) relies on this: top-level DIM PASS
    // and an in-file SUB that does `PASS = PASS + 1` must share state.
    //
    // Module-prefixed names (containing a dot) always refer to the same
    // global regardless of caller.
    if (vi && scopes.size() > 1 && stmt.var_name.find('.') == std::string::npos) {
        bool in_local_scope = false;
        for (int i = (int)scopes.size() - 1; i >= 1; i--) {
            if (scopes[i].vars.find(stmt.var_name) != scopes[i].vars.end()) {
                in_local_scope = true; break;
            }
        }
        if (!in_local_scope) {
            auto gs = global_source_file.find(stmt.var_name);
            bool same_source = (gs != global_source_file.end() &&
                                gs->second == current_fn_source_file);
            if (!same_source) vi = nullptr;
        }
    }
    // Tag 7 (runtime-tagged): ALWAYS store as tag 7 with companion alloca.
    // Never coerce to a concrete type — the runtime tag carries the truth
    // about what the value IS (could be f64, string, array, VM handle).
    // Subsequent INDEX and coerce_to dispatch based on the runtime tag.
    if (rhs.tag == JD_TAG_RUNTIME && rhs.runtime_tag) {
        if (vi && vi->tag == JD_TAG_RUNTIME && vi->runtime_tag_alloca) {
            LLVMBuildStore(builder, rhs.val, vi->alloca_val);
            LLVMBuildStore(builder, rhs.runtime_tag, vi->runtime_tag_alloca);
            return;
        }
        if (vi) {
            vi->tag = JD_TAG_RUNTIME;
            if (!vi->runtime_tag_alloca) {
                // Determine if vi lives in global scope (scopes[0]).
                std::string rtag_name = stmt.var_name + ".rtag";
                bool is_global = false;
                if (!scopes.empty() && scopes[0].vars.count(stmt.var_name))
                    is_global = true;
                if (is_global) {
                    LLVMValueRef g = LLVMAddGlobal(module, i32_type, rtag_name.c_str());
                    LLVMSetInitializer(g, LLVMConstInt(i32_type, 0, 0));
                    LLVMSetLinkage(g, LLVMInternalLinkage);
                    vi->runtime_tag_alloca = g;
                } else {
                    // Alloca in entry block for dominance.
                    LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
                    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(current_fn);
                    LLVMValueRef first = LLVMGetFirstInstruction(entry);
                    if (first) LLVMPositionBuilderBefore(builder, first);
                    else LLVMPositionBuilderAtEnd(builder, entry);
                    vi->runtime_tag_alloca = LLVMBuildAlloca(builder, i32_type, rtag_name.c_str());
                    LLVMPositionBuilderAtEnd(builder, cur);
                }
            }
            LLVMBuildStore(builder, rhs.val, vi->alloca_val);
            LLVMBuildStore(builder, rhs.runtime_tag, vi->runtime_tag_alloca);
            return;
        }
        // New var — create_var already puts the val alloca in the entry block.
        // The companion rtag alloca is created right next to it.
        VarInfo& nv = create_var(stmt.var_name, JD_TAG_RUNTIME);
        LLVMBuildStore(builder, rhs.val, nv.alloca_val);
        // create_var for locals already positioned alloca in entry block;
        // runtime_tag_alloca is also created there by create_var's scope logic.
        // For globals, use a module-level global.
        std::string rtag_name = stmt.var_name + ".rtag";
        if (scopes.size() <= 1) {
            LLVMValueRef g = LLVMAddGlobal(module, i32_type, rtag_name.c_str());
            LLVMSetInitializer(g, LLVMConstInt(i32_type, 0, 0));
            LLVMSetLinkage(g, LLVMInternalLinkage);
            nv.runtime_tag_alloca = g;
        } else {
            LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(current_fn);
            LLVMValueRef first = LLVMGetFirstInstruction(entry);
            if (first) LLVMPositionBuilderBefore(builder, first);
            else LLVMPositionBuilderAtEnd(builder, entry);
            nv.runtime_tag_alloca = LLVMBuildAlloca(builder, i32_type, rtag_name.c_str());
            LLVMPositionBuilderAtEnd(builder, cur);
        }
        LLVMBuildStore(builder, rhs.runtime_tag, nv.runtime_tag_alloca);
        return;
    }
    // Stringify a numeric/bool TypedValue in place. Mirrors the implicit
    // coercion the interpreter does for `s$ = 42` / `DIM s AS STRING = 1.5`.
    // Without it, a numeric RHS would land bit-punned in an i8* slot and
    // any later read (PRINT, str-concat) would deref garbage and segfault.
    auto coerce_rhs_to_str = [&](TypedValue& v) {
        if (v.tag == JD_TAG_STR) return;
        if (v.tag == JD_TAG_VM_HANDLE) {
            // VM_HANDLE → string via the same materialiser FORMAT$ uses.
            // Lets `s$ = AWAIT chan_recv@(ch)` and similar VM-handle-yielding
            // calls land as a real char* in the string slot instead of an
            // i64 stored into i8* (which segfaults on the next read).
            auto* vts = get_runtime_func("__jdrt_val_to_str");
            if (vts) {
                LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef args[] = { rt, v.val };
                v.val = LLVMBuildCall2(builder, vts->fn_type, vts->fn, args, 2, "vmh2s");
                v.tag = JD_TAG_STR;
            }
            return;
        }
        if (v.tag == JD_TAG_F64) {
            auto& fn = runtime_funcs["__double_to_str"];
            LLVMValueRef args[] = { v.val };
            v.val = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "f2s");
            v.tag = JD_TAG_STR;
        } else if (v.tag == JD_TAG_I64 || v.tag == JD_TAG_BOOL) {
            auto& fn = runtime_funcs["__int_to_str"];
            LLVMValueRef args[] = { v.val };
            v.val = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "i2s");
            v.tag = JD_TAG_STR;
        }
    };

    if (vi) {
        // STRICT: silent slot-type changes hide bit-puns in native — `DIM x = 0`
        // gives x an i64 slot, a later `x = 3.0` SIToFP-converts the float, but
        // when the codegen path is something less innocent (a func returning a
        // ptr-typed handle stored in a numeric slot, say) the i64 slot ends up
        // holding garbage bits and downstream reads are uninterpretable.
        // Force the user to convert explicitly with CINT / CSNG / CDBL / STR$
        // and they'll see the type clash at the source.
        // BOOL ↔ I64 is exempt: BOOL is stored as i64 0/1 and the two carry
        // identical bit patterns — the existing codegen treats them inter-
        // changeably and STRICT-flagging them would force every test that
        // does `DIM ok = FALSE; ok = TRUE` to wrap in CBOOL/CINT for no
        // safety gain.
        bool bool_int_pair =
            (vi->tag == JD_TAG_BOOL && rhs.tag == JD_TAG_I64) ||
            (vi->tag == JD_TAG_I64  && rhs.tag == JD_TAG_BOOL);
        // INTEGER → DOUBLE is a lossless widening conversion that classic
        // BASIC always performed implicitly (e.g. `dim x as double : x = 1`).
        // STRICT-flagging it would force CDBL() everywhere a literal int meets
        // a double slot, which is more noise than safety. The codegen path
        // below picks up the slot's tag and promotes via SIToFP. The reverse
        // direction (DOUBLE → INTEGER) IS lossy and stays flagged.
        bool int_to_double_widen =
            (vi->tag == JD_TAG_F64 && rhs.tag == JD_TAG_I64);
        if (is_strict_here(stmt.source_file) && stmt.kind == StmtKind::ASSIGN &&
            vi->tag != rhs.tag && !bool_int_pair && !int_to_double_widen &&
            // Pointer-typed slot can legitimately be retagged (e.g. a MAP
            // var that later gets a JSON.PARSE$ handle assigned). The bit-
            // pun-shaped mismatches are between numeric / string / number-
            // shaped tags — STRICT-flag those. RUNTIME is the dynamic-tagged
            // value the codegen produces for UDT-field reads / map_get_tagged
            // — it carries its own runtime tag, so silently coercing into a
            // typed slot is safe (the runtime helper picks the right path).
            !(vi->tag == JD_TAG_NATIVE_MAP || vi->tag == JD_TAG_VM_HANDLE ||
              vi->tag == JD_TAG_ARR || vi->tag == JD_TAG_STR ||
              vi->tag == JD_TAG_RUNTIME ||
              rhs.tag == JD_TAG_RUNTIME)) {
            auto tag_name = [](int t) -> const char* {
                switch (t) {
                    case JD_TAG_I64: return "INTEGER";
                    case JD_TAG_F64: return "DOUBLE";
                    case JD_TAG_STR: return "STRING";
                    case JD_TAG_BOOL: return "BOOLEAN";
                    case JD_TAG_ARR: return "ARRAY";
                    case JD_TAG_NATIVE_MAP: return "MAP";
                    case JD_TAG_VM_HANDLE: return "OBJECT";
                    case JD_TAG_FUNCREF: return "FUNCREF";
                    default: return "?";
                }
            };
            std::string hint;
            if (vi->tag == JD_TAG_I64 && rhs.tag == JD_TAG_F64)
                hint = " — wrap with CINT() to assign explicitly";
            else if (vi->tag == JD_TAG_F64 && rhs.tag == JD_TAG_I64)
                hint = " — wrap with CDBL() to assign explicitly";
            else if (rhs.tag == JD_TAG_STR)
                hint = " — wrap with VAL() to parse the string";
            else if (vi->tag == JD_TAG_STR)
                hint = " — wrap with STR$() to stringify";
            report_error(stmt.source_file, stmt.line,
                "STRICT: cannot assign " + std::string(tag_name(rhs.tag)) +
                " to " + std::string(tag_name(vi->tag)) + " '" +
                stmt.var_name + "'" + hint);
        }
        if (vi->tag != rhs.tag) {
            // RUNTIME-tagged RHS into a typed slot: materialise via the
            // tag-dispatching helper so the slot gets a usable concrete
            // value (string ptr / f64 / i64) instead of an i64-into-i8*
            // bit-pun that segfaults on the next read. Also handles the
            // `kn$ = vstate{"items"}[kidx]` pattern where the indexed
            // string-array element comes back RUNTIME(rtag=STR).
            if (rhs.tag == JD_TAG_RUNTIME && rhs.runtime_tag) {
                if (vi->tag == JD_TAG_STR) {
                    rhs.val = coerce_to(rhs, i8_ptr_type);
                    rhs.tag = JD_TAG_STR;
                } else if (vi->tag == JD_TAG_F64 || vi->tag == JD_TAG_I64 ||
                           vi->tag == JD_TAG_BOOL) {
                    rhs.val = coerce_to(rhs, f64_type);
                    rhs.tag = JD_TAG_F64;
                    if (vi->tag == JD_TAG_I64 || vi->tag == JD_TAG_BOOL) {
                        rhs.val = LLVMBuildFPToSI(builder, rhs.val, i64_type, "ftoi");
                        rhs.tag = JD_TAG_I64;
                    }
                }
            }
            if (vi->tag == JD_TAG_F64 && rhs.tag == JD_TAG_I64) {
                rhs.val = LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof");
                rhs.tag = JD_TAG_F64;
            } else if (vi->tag == JD_TAG_I64 && rhs.tag == JD_TAG_F64) {
                rhs.val = LLVMBuildFPToSI(builder, rhs.val, i64_type, "ftoi");
                rhs.tag = JD_TAG_I64;
            } else if (vi->tag == JD_TAG_STR &&
                       (rhs.tag == JD_TAG_I64 || rhs.tag == JD_TAG_F64 ||
                        rhs.tag == JD_TAG_BOOL || rhs.tag == JD_TAG_VM_HANDLE)) {
                // VM_HANDLE → STR materialises through __jdrt_val_to_str,
                // not through the tag-overwrite branch below — the slot
                // is genuinely declared as i8* (ptr) and storing the
                // raw i64 handle into it would segfault on the next
                // PRINT/concat read.
                coerce_rhs_to_str(rhs);
            } else if ((rhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_ARR || rhs.tag == JD_TAG_NATIVE_MAP || rhs.tag == JD_TAG_FUNCREF || rhs.tag == JD_TAG_VM_HANDLE) &&
                       vi->tag != rhs.tag) {
                // String (2), array (3), map (4), funcref (5), or VM handle
                // (6) — replace the tag so subsequent INDEX/loads dispatch on
                // the new kind. This is what makes `game = {}` (native map)
                // + later `game = JSON.PARSE$(...)` (VM handle) Just Work,
                // and what makes `DIM s AS STRING = arr[i]` deliver a real
                // string when arr is a string-tracked array (the pre-pass
                // provisionally sized the slot for an i64-shaped tag).
                vi->tag = rhs.tag;
                LLVMBuildStore(builder, rhs.val, vi->alloca_val);
                return;
            }
        }
        LLVMBuildStore(builder, rhs.val, vi->alloca_val);
    } else {
        // $-suffix forces a string slot; coerce numeric RHS so the var
        // genuinely holds a string pointer, not bit-punned f64 garbage.
        bool wants_str_slot = !stmt.var_name.empty() && stmt.var_name.back() == '$';
        if (wants_str_slot && rhs.tag != JD_TAG_STR) {
            coerce_rhs_to_str(rhs);
        }
        // In functions, promote new numeric variables to f64 to avoid
        // type-upgrade issues when int vars later receive float values
        int var_tag = rhs.tag;
        if (var_tag == JD_TAG_I64 && scopes.size() > 1) {
            rhs.val = LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof");
            var_tag = JD_TAG_F64;
        }
        VarInfo& nv = create_var(stmt.var_name, var_tag);
        LLVMBuildStore(builder, rhs.val, nv.alloca_val);
    }
}

// ── DIM (array allocation) ──────────────────────────────────

void LLVMCodegen::codegen_static_dim(const Stmt& stmt) {
    if (scopes.size() <= 1) {
        report_error(stmt.source_file, stmt.line,
            "STATIC DIM is only allowed inside a FUNC or SUB");
        return;
    }
    if (scopes.back().vars.count(stmt.var_name)) {
        report_error(stmt.source_file, stmt.line,
            "STATIC DIM '" + stmt.var_name + "' redeclared");
        return;
    }

    // ── Pick slot tag/type up-front from the AS clause + initializer ──
    int tag = JD_TAG_I64;
    LLVMTypeRef slot_type = i64_type;
    switch (stmt.var_type) {
        case VarType::STRING:
            tag = JD_TAG_STR; slot_type = i8_ptr_type; break;
        case VarType::FLOAT16:
        case VarType::FLOAT32:
        case VarType::FLOAT64:
            tag = JD_TAG_F64; slot_type = f64_type; break;
        case VarType::ARRAY:
            tag = JD_TAG_ARR; slot_type = i8_ptr_type; break;
        case VarType::OBJECT:
            tag = JD_TAG_NATIVE_MAP; slot_type = i8_ptr_type; break;
        default:
            // No declared type — peek at the init expression's literal
            // shape so we pick a pointer-typed slot for collection inits.
            if (stmt.expr) {
                if (stmt.expr->kind == ExprKind::ARRAY_LITERAL) {
                    tag = JD_TAG_ARR; slot_type = i8_ptr_type;
                } else if (stmt.expr->kind == ExprKind::MAP_LITERAL) {
                    tag = JD_TAG_NATIVE_MAP; slot_type = i8_ptr_type;
                } else if (stmt.expr->kind == ExprKind::LITERAL_STRING) {
                    tag = JD_TAG_STR; slot_type = i8_ptr_type;
                } else if (stmt.expr->kind == ExprKind::LITERAL_FLOAT) {
                    tag = JD_TAG_F64; slot_type = f64_type;
                }
            }
            break;
    }
    if (!stmt.var_name.empty() && stmt.var_name.back() == '$') {
        tag = JD_TAG_STR; slot_type = i8_ptr_type;
    }

    static unsigned long st_counter = 0;
    std::string slot_name = "__st." + stmt.var_name + "." +
                             std::to_string(st_counter++);
    std::string guard_name = slot_name + ".init";

    LLVMValueRef slot_global = LLVMAddGlobal(module, slot_type, slot_name.c_str());
    LLVMSetInitializer(slot_global, LLVMConstNull(slot_type));
    LLVMSetLinkage(slot_global, LLVMInternalLinkage);

    LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx);
    LLVMValueRef guard_global = LLVMAddGlobal(module, i1_type, guard_name.c_str());
    LLVMSetInitializer(guard_global, LLVMConstInt(i1_type, 0, 0));
    LLVMSetLinkage(guard_global, LLVMInternalLinkage);

    // Register the slot before emitting init code so a recursive call from
    // inside the initializer resolves to the (still null) slot rather than
    // crashing on a missing var.
    auto& vi = scopes.back().vars[stmt.var_name];
    vi = { slot_global, tag };

    // ── Guard branch: if initialised, skip; else set guard FIRST + init ──
    LLVMBasicBlockRef do_init  = LLVMAppendBasicBlockInContext(ctx, current_fn, "static_init");
    LLVMBasicBlockRef done_blk = LLVMAppendBasicBlockInContext(ctx, current_fn, "static_done");
    LLVMValueRef guard_val = LLVMBuildLoad2(builder, i1_type, guard_global, "");
    LLVMBuildCondBr(builder, guard_val, done_blk, do_init);

    LLVMPositionBuilderAtEnd(builder, do_init);
    LLVMBuildStore(builder, LLVMConstInt(i1_type, 1, 0), guard_global);

    LLVMValueRef init_val = nullptr;
    if (stmt.expr) {
        TypedValue tv = codegen_expr(*stmt.expr);
        TypedValue coerced = coerce_to_tag(tv, tag);
        init_val = coerced.val;
    } else {
        switch (tag) {
            case JD_TAG_F64: init_val = LLVMConstReal(f64_type, 0.0); break;
            case JD_TAG_STR:
                init_val = LLVMBuildGlobalStringPtr(builder, "", ".st_s_init");
                break;
            case JD_TAG_ARR: {
                auto& arr_new = runtime_funcs["__array_new"];
                LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
                init_val = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn,
                                           &zero, 1, "");
                break;
            }
            case JD_TAG_NATIVE_MAP: {
                auto& map_new = runtime_funcs["__map_new"];
                init_val = LLVMBuildCall2(builder, map_new.fn_type, map_new.fn,
                                           nullptr, 0, "");
                break;
            }
            default: init_val = LLVMConstInt(i64_type, 0, 0); break;
        }
    }

    LLVMBuildStore(builder, init_val, slot_global);
    LLVMBuildBr(builder, done_blk);

    LLVMPositionBuilderAtEnd(builder, done_blk);
}

void LLVMCodegen::codegen_dim(const Stmt& stmt) {
    if (stmt.is_static) {
        codegen_static_dim(stmt);
        return;
    }
    // Phase 4 STRICT: DIM with declared type + initializer must match.
    // `DIM x AS INTEGER = "hello"` is the canonical bug this catches.
    // ARRAY and multi-dim shape inits are exempt — the initializer shape
    // doesn't infer cleanly as a StaticType yet, so let the legacy path
    // handle them (Phase 4 extends later).
    if (is_strict_here(stmt.source_file) && stmt.expr &&
        stmt.var_type != VarType::NONE &&
        stmt.var_type != VarType::ARRAY) {
        StaticType declared = StaticType::from_vartype(
            stmt.var_type, stmt.elem_type, stmt.label);
        StaticType actual = infer_expr_type(*stmt.expr);
        if (!declared.is_unknown() && !actual.is_unknown() &&
            !types_compatible(actual, declared)) {
            report_error(stmt.source_file, stmt.line,
                "Type Mismatch in DIM '" + stmt.var_name +
                "': expected " + declared.describe() +
                ", got " + actual.describe());
        }
    }
    // Track DIM ... AS BOOLEAN for TYPEOF.
    if (stmt.var_type == VarType::BOOLEAN) {
        std::string up = stmt.var_name;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
        bool_vars.insert(up);
    } else if (stmt.expr && stmt.expr->kind == ExprKind::LITERAL_BOOL) {
        std::string up = stmt.var_name;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
        bool_vars.insert(up);
    }
    // Track date-producing initializers so TYPEOF reports "DATE".
    if (stmt.expr && stmt.expr->kind == ExprKind::CALL) {
        std::string fn_up = stmt.expr->func_name;
        std::transform(fn_up.begin(), fn_up.end(), fn_up.begin(), ::toupper);
        if (fn_up == "CVDATE" || fn_up == "CDATE" || fn_up == "DATEADD" || fn_up == "NOW") {
            std::string up = stmt.var_name;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            date_vars.insert(up);
        }
    }

    // DATE has no token-level VarType, so the parser stuffs it into
    // stmt.label. Treat the slot as STRING since CVDATE returns char*.
    {
        std::string lbl_up = stmt.label;
        std::transform(lbl_up.begin(), lbl_up.end(), lbl_up.begin(), ::toupper);
        if (lbl_up == "DATE" && !stmt.expr) {
            LLVMValueRef init = LLVMBuildGlobalStringPtr(builder, "", ".dim_dt");
            VarInfo* vi = lookup_var(stmt.var_name);
            if (vi) {
                vi->tag = JD_TAG_STR;
                LLVMBuildStore(builder, init, vi->alloca_val);
            } else {
                VarInfo& nv = create_var(stmt.var_name, JD_TAG_STR);
                LLVMBuildStore(builder, init, nv.alloca_val);
            }
            std::string up = stmt.var_name;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            date_vars.insert(up);
            return;
        }
    }

    // DIM x AS <type> with no initializer — create a default-valued binding
    // (covers `DIM mv_y AS STRING` in multi-var DIM clauses).
    if (!stmt.expr && stmt.label.empty()) {
        int tag = JD_TAG_I64;
        LLVMValueRef init = nullptr;
        switch (stmt.var_type) {
            case VarType::STRING:
                tag = JD_TAG_STR;
                init = LLVMBuildGlobalStringPtr(builder, "", ".dim_s");
                break;
            case VarType::FLOAT16:
            case VarType::FLOAT32:
            case VarType::FLOAT64:
                tag = JD_TAG_F64;
                init = LLVMConstReal(f64_type, 0.0);
                break;
            case VarType::BOOLEAN:
            case VarType::BYTE:
            case VarType::CHAR:
            case VarType::INT16:
            case VarType::INT32:
            case VarType::INT64:
                tag = JD_TAG_I64;
                init = LLVMConstInt(i64_type, 0, 0);
                break;
            default:
                break;  // unknown — fall through to existing paths
        }
        if (init) {
            // DIM inside a FUNC/SUB shadows any outer binding — restrict the
            // lookup to the current scope so a top-level `DIM i AS INTEGER`
            // doesn't get reused by walk()'s `DIM i AS INTEGER` (which would
            // make recursive walk()'s loop counter clobber main's loop
            // counter and cause wedge / infinite-loop in the bottom for-loop).
            VarInfo* vi = nullptr;
            if (scopes.size() > 1) {
                auto it = scopes.back().vars.find(stmt.var_name);
                if (it != scopes.back().vars.end()) vi = &it->second;
            } else {
                vi = lookup_var(stmt.var_name);
            }
            if (vi) {
                vi->tag = tag;
                LLVMBuildStore(builder, init, vi->alloca_val);
            } else {
                VarInfo& nv = create_var(stmt.var_name, tag);
                LLVMBuildStore(builder, init, nv.alloca_val);
            }
            return;
        }
    }

    // DIM x AS OBJECT (or MAP) → empty native map
    if (stmt.var_type == VarType::OBJECT && stmt.label.empty() && !stmt.expr) {
        auto& map_new = runtime_funcs["__map_new"];
        LLVMValueRef m = LLVMBuildCall2(builder, map_new.fn_type, map_new.fn,
                                         nullptr, 0, "obj");
        VarInfo* vi = lookup_var(stmt.var_name);
        if (vi) { LLVMBuildStore(builder, m, vi->alloca_val); vi->tag = JD_TAG_NATIVE_MAP; }
        else {
            VarInfo& nv = create_var(stmt.var_name, JD_TAG_NATIVE_MAP);
            LLVMBuildStore(builder, m, nv.alloca_val);
        }
        return;
    }

    // DIM x AS TypeName (scalar) → call UDT constructor
    // But NOT if stmt.expr is __MAKE_UDT_ARRAY__ — that's handled below.
    bool is_udt_array = stmt.expr && stmt.expr->kind == ExprKind::CALL &&
                        stmt.expr->func_name == "__MAKE_UDT_ARRAY__";
    if (!stmt.label.empty() && !is_udt_array) {
        auto uit = user_functions.find(stmt.label);
        if (uit != user_functions.end()) {
            LLVMTypeRef fn_type = LLVMGlobalGetValueType(uit->second.fn);
            LLVMValueRef obj = LLVMBuildCall2(builder, fn_type, uit->second.fn,
                                               nullptr, 0, "obj");
            // Optional user INIT: TypeName.INIT(THIS, args...). Same back-
            // compat rules as the interpreter (compile_dim): only auto-call
            // when ctor_args are provided OR INIT() takes no user params.
            auto init_it = user_functions.find(stmt.label + ".INIT");
            if (init_it != user_functions.end()) {
                size_t expected = init_it->second.param_tags.size();
                size_t got = stmt.ctor_args.size() + 1; // +THIS
                bool emit_init = !stmt.ctor_args.empty() || expected == 1;
                if (emit_init && got != expected) {
                    report_error(stmt.source_file, stmt.line,
                        "SUB " + stmt.label + ".INIT expects " +
                        std::to_string(expected - 1) +
                        " argument(s), got " + std::to_string(stmt.ctor_args.size()));
                } else if (emit_init) {
                    std::vector<LLVMValueRef> args;
                    args.push_back(obj); // THIS as i8_ptr (param 0)
                    for (size_t i = 0; i < stmt.ctor_args.size(); i++) {
                        TypedValue av = codegen_expr(*stmt.ctor_args[i]);
                        int want_tag = (i + 1 < init_it->second.param_tags.size())
                            ? init_it->second.param_tags[i + 1] : JD_TAG_F64;
                        args.push_back(coerce_to_tag(av, want_tag).val);
                    }
                    LLVMTypeRef init_ft = LLVMGlobalGetValueType(init_it->second.fn);
                    LLVMBuildCall2(builder, init_ft, init_it->second.fn,
                                   args.data(), (unsigned)args.size(), "");
                }
            } else if (!stmt.ctor_args.empty()) {
                report_error(stmt.source_file, stmt.line,
                    "Type '" + stmt.label +
                    "' has no SUB INIT — cannot pass constructor arguments");
            }
            VarInfo* vi = lookup_var(stmt.var_name);
            if (vi) {
                LLVMBuildStore(builder, obj, vi->alloca_val);
                vi->tag = JD_TAG_ARR;
            } else {
                VarInfo& nv = create_var(stmt.var_name, JD_TAG_ARR);
                LLVMBuildStore(builder, obj, nv.alloca_val);
            }
            var_udt_type[stmt.var_name] = stmt.label;
            track_dispose_local(stmt.var_name, stmt.label, /*is_array=*/false);
            return;
        }
    }

    if (!stmt.expr) {
        // DIM without value — create default var
        codegen_let_or_assign(stmt);
        return;
    }

    // Mirror codegen_let_or_assign: DIM with array-literal RHS picks the
    // right INDEX-tag tracking set. All-string → string_array_vars (INDEX
    // returns tag=STR). Mixed string+number → mixed_array_vars (INDEX
    // calls the runtime classifier per cell). Pure-numeric falls through
    // to the default tag=F64 path.
    {
        bool wants_str_slot =
            stmt.var_type == VarType::STRING ||
            (!stmt.var_name.empty() && stmt.var_name.back() == '$');
        bool rhs_is_str_lit = stmt.expr && stmt.expr->kind == ExprKind::LITERAL_STRING;
        bool rhs_is_str_call = stmt.expr && stmt.expr->kind == ExprKind::CALL &&
            !stmt.expr->func_name.empty() && stmt.expr->func_name.back() == '$';
        if (wants_str_slot || rhs_is_str_lit || rhs_is_str_call)
            string_scalar_vars.insert(stmt.var_name);
    }
    if (stmt.expr->kind == ExprKind::ARRAY_LITERAL) {
        auto el_is_string = [&](const Expr* e) -> bool {
            if (!e) return false;
            if (e->kind == ExprKind::LITERAL_STRING) return true;
            if (e->kind == ExprKind::VARIABLE) {
                if (!e->str_val.empty() && e->str_val.back() == '$') return true;
                return string_scalar_vars.count(e->str_val) != 0;
            }
            if (e->kind == ExprKind::CALL) {
                if (!e->func_name.empty() && e->func_name.back() == '$') return true;
                // $-less builtins that nonetheless return strings.
                std::string u = e->func_name;
                std::transform(u.begin(), u.end(), u.begin(), ::toupper);
                if (u == "JOIN") return true;
            }
            return false;
        };
        // Same as the LET path: INDEX-typed elements force the destination
        // to mixed-storage so per-cell tags survive.
        auto el_is_runtime_typed = [&](const Expr* e) -> bool {
            if (!e) return false;
            return e->kind == ExprKind::INDEX;
        };
        bool has_str = false, has_non_str = false, has_runtime = false;
        for (auto& a : stmt.expr->args) {
            if (!a) continue;
            if (el_is_string(a.get())) has_str = true;
            else if (el_is_runtime_typed(a.get())) has_runtime = true;
            else has_non_str = true;
        }
        if (has_runtime)                 mixed_array_vars.insert(stmt.var_name);
        else if (has_str && has_non_str) mixed_array_vars.insert(stmt.var_name);
        else if (has_str)                string_array_vars.insert(stmt.var_name);
    }
    if (stmt.expr->kind == ExprKind::CALL &&
        !stmt.expr->func_name.empty()) {
        std::string u = stmt.expr->func_name;
        std::transform(u.begin(), u.end(), u.begin(), ::toupper);
        if (u == "SPLIT" || u == "TILED.LAYERS$" || u == "LINES" ||
            u == "WORDS" || u == "CHARS")
            string_array_vars.insert(stmt.var_name);
        if (u == "STR$" && !stmt.expr->args.empty()) {
            string_array_vars.insert(stmt.var_name);
        }
        if (u == "OS.ARGS")
            string_array_vars.insert(stmt.var_name);
        if (u == "DIR$") {
            bool extended = false;
            if (stmt.expr->args.size() >= 2 && stmt.expr->args[1]) {
                auto& arg = *stmt.expr->args[1];
                if (arg.kind == ExprKind::LITERAL_BOOL)      extended = arg.bool_val;
                else if (arg.kind == ExprKind::LITERAL_INT)  extended = (arg.int_val != 0);
                else                                          extended = true;  // defensive
            }
            if (extended) mixed_array_vars.insert(stmt.var_name);
            else          string_array_vars.insert(stmt.var_name);
        }
        // SELECT(fn@, arr) — see codegen_let_or_assign for rationale.
        if (u == "SELECT" && !stmt.expr->args.empty() && stmt.expr->args[0]) {
            auto& fn_arg = *stmt.expr->args[0];
            if (fn_arg.kind == ExprKind::LITERAL_STRING && fn_arg.is_funcref_lit &&
                !fn_arg.str_val.empty() && fn_arg.str_val.back() == '$') {
                string_array_vars.insert(stmt.var_name);
            }
        }
        if ((u == "UNIQUE" || u == "REVERSE" || u == "SORT" || u == "TAKE" || u == "DROP")
            && !stmt.expr->args.empty() && stmt.expr->args[0] &&
            stmt.expr->args[0]->kind == ExprKind::VARIABLE &&
            string_array_vars.count(stmt.expr->args[0]->str_val)) {
            string_array_vars.insert(stmt.var_name);
        }
        // APPEND(arr_a, arr_b) — propagate string-element tag if both args
        // resolve to known string arrays. Mirror codegen_let_or_assign.
        if (u == "APPEND" && stmt.expr->args.size() >= 2) {
            // Recognise scalar-string expressions inside a [scalar, scalar, ...]
            // literal — handles VARIABLE refs to string locals, $-suffix calls,
            // and known $-less string-returners like JOIN. Without this, an
            // APPEND(arr, [JOIN(...)]) wouldn't tag the result as a string
            // array because the inner CALL doesn't end in $.
            auto is_string_scalar_expr = [&](const Expr* e) -> bool {
                if (!e) return false;
                if (e->kind == ExprKind::LITERAL_STRING) return true;
                if (e->kind == ExprKind::VARIABLE) {
                    if (!e->str_val.empty() && e->str_val.back() == '$') return true;
                    return string_scalar_vars.count(e->str_val) != 0;
                }
                if (e->kind == ExprKind::CALL) {
                    if (!e->func_name.empty() && e->func_name.back() == '$') return true;
                    std::string u = e->func_name;
                    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
                    if (u == "JOIN") return true;
                }
                return false;
            };
            auto is_string_arr_expr = [&](const Expr* e) -> bool {
                if (!e) return false;
                if (e->kind == ExprKind::VARIABLE)
                    return string_array_vars.count(e->str_val) != 0;
                if (e->kind == ExprKind::ARRAY_LITERAL) {
                    bool any = false;
                    for (auto& a : e->args) {
                        if (!a) continue;
                        if (!is_string_scalar_expr(a.get())) return false;
                        any = true;
                    }
                    return any;
                }
                if (e->kind == ExprKind::LITERAL_STRING) return true;
                if (e->kind == ExprKind::CALL)
                    return string_array_returning_funcs.count(e->func_name) != 0;
                return false;
            };
            // Liberal OR: either side proving string-array-ness is enough.
            // Lets `g_paths = APPEND(g_paths, [JOIN(...)])` tag g_paths on
            // the first append even when g_paths started as an empty `[]`
            // and isn't yet in string_array_vars.
            if (is_string_arr_expr(stmt.expr->args[0].get()) ||
                is_string_arr_expr(stmt.expr->args[1].get()) ||
                is_string_scalar_expr(stmt.expr->args[1].get())) {
                string_array_vars.insert(stmt.var_name);
            }
        }
        // User FUNC return-tag (mirror codegen_let_or_assign).
        if (string_array_returning_funcs.count(stmt.expr->func_name)) {
            string_array_vars.insert(stmt.var_name);
        }
    }

    // DIM arr[N] AS TypeName → CALL("__MAKE_UDT_ARRAY__", [shape, "TypeName" [, vec1, vec2, ...]])
    if (stmt.expr->kind == ExprKind::CALL && stmt.expr->func_name == "__MAKE_UDT_ARRAY__" &&
        stmt.expr->args.size() >= 2 && stmt.expr->args[0]->kind == ExprKind::ARRAY_LITERAL &&
        stmt.expr->args[1]->kind == ExprKind::LITERAL_STRING) {
        auto& shape_args = stmt.expr->args[0]->args;
        std::string type_name = stmt.expr->args[1]->str_val;
        auto ctor_it = user_functions.find(type_name);
        if (!shape_args.empty() && ctor_it != user_functions.end()) {
            auto& arr_new = runtime_funcs["__array_new"];
            auto& arr_set = runtime_funcs["__array_set"];
            LLVMTypeRef ctor_ft = LLVMGlobalGetValueType(ctor_it->second.fn);

            // Resolve INIT target up front so we know its param tags. Vector
            // ctor args (stmt.expr->args[2..]) are codegen'd once before the
            // loop and stashed in alloca'd JdbArray*s; the loop body indexes
            // each per slot and calls INIT(inst, vec1[i], vec2[i], ...).
            auto init_it = user_functions.find(type_name + ".INIT");
            std::vector<LLVMValueRef> ctor_vec_allocas;
            std::vector<int> ctor_vec_tags;
            for (size_t k = 2; k < stmt.expr->args.size(); k++) {
                if (init_it == user_functions.end()) {
                    report_error(stmt.source_file, stmt.line,
                        "Type '" + type_name +
                        "' has no SUB INIT — cannot pass constructor argument vectors");
                    break;
                }
                TypedValue v = codegen_expr(*stmt.expr->args[k]);
                LLVMValueRef vec_alloca = LLVMBuildAlloca(builder, i8_ptr_type, "ctor_vec");
                LLVMBuildStore(builder, v.val, vec_alloca);
                ctor_vec_allocas.push_back(vec_alloca);
                int want_tag = (k - 1 < init_it->second.param_tags.size())
                    ? init_it->second.param_tags[k - 1] : JD_TAG_F64;
                ctor_vec_tags.push_back(want_tag);
            }

            TypedValue size_val = codegen_expr(*shape_args[0]);
            LLVMValueRef n = size_val.tag == JD_TAG_F64
                ? LLVMBuildFPToSI(builder, size_val.val, i64_type, "ftoi") : size_val.val;

            // Pre-size the array to N. Previously this allocated size 0 and
            // grew via APPEND per slot — APPEND is O(N) per call (full copy
            // realloc) so the old path was O(N²). For N=50000 that meant
            // ~2.5B copies; now we just write into a fixed-size buffer.
            LLVMValueRef size_args[] = { n };
            LLVMValueRef outer = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, size_args, 1, "outer");

            LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "udt_arr.loop");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "udt_arr.end");
            LLVMValueRef idx_alloca = LLVMBuildAlloca(builder, i64_type, "udt_i");
            LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), idx_alloca);
            LLVMBuildBr(builder, loop_bb);

            LLVMPositionBuilderAtEnd(builder, loop_bb);
            LLVMValueRef cur_idx = LLVMBuildLoad2(builder, i64_type, idx_alloca, "i");
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, cur_idx, n, "cmp");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "udt_arr.body");
            LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(builder, body_bb);
            // Create new UDT instance
            LLVMValueRef inst = LLVMBuildCall2(builder, ctor_ft, ctor_it->second.fn, nullptr, 0, "inst");
            // If we have vector ctor args, run INIT(inst, vec0[i], vec1[i], ...)
            if (!ctor_vec_allocas.empty() && init_it != user_functions.end()) {
                std::vector<LLVMValueRef> init_args;
                init_args.push_back(inst);
                for (size_t k = 0; k < ctor_vec_allocas.size(); k++) {
                    LLVMValueRef vec_ptr = LLVMBuildLoad2(builder, i8_ptr_type,
                        ctor_vec_allocas[k], "vp");
                    LLVMValueRef arg_val;
                    if (ctor_vec_tags[k] == JD_TAG_STR) {
                        auto& g = runtime_funcs["__array_get_str"];
                        LLVMValueRef getargs[] = { vec_ptr, cur_idx };
                        arg_val = LLVMBuildCall2(builder, g.fn_type, g.fn, getargs, 2, "vs");
                    } else {
                        auto& g = runtime_funcs["__array_get"];
                        LLVMValueRef getargs[] = { vec_ptr, cur_idx };
                        LLVMValueRef f = LLVMBuildCall2(builder, g.fn_type, g.fn, getargs, 2, "vn");
                        if (ctor_vec_tags[k] == JD_TAG_I64)
                            arg_val = LLVMBuildFPToSI(builder, f, i64_type, "ftoi");
                        else
                            arg_val = f;
                    }
                    init_args.push_back(arg_val);
                }
                LLVMTypeRef init_ft = LLVMGlobalGetValueType(init_it->second.fn);
                LLVMBuildCall2(builder, init_ft, init_it->second.fn,
                               init_args.data(), (unsigned)init_args.size(), "");
            }
            // Encode ptr as f64 and store at slot i (no resize)
            LLVMValueRef inst_i64 = LLVMBuildPtrToInt(builder, inst, i64_type, "ptoi");
            LLVMValueRef inst_f64 = pun_i64_to_f64(inst_i64);
            LLVMValueRef set_args[] = { outer, cur_idx, inst_f64 };
            LLVMBuildCall2(builder, arr_set.fn_type, arr_set.fn, set_args, 3, "");
            LLVMValueRef next_idx = LLVMBuildAdd(builder, cur_idx, LLVMConstInt(i64_type, 1, 0), "next");
            LLVMBuildStore(builder, next_idx, idx_alloca);
            LLVMBuildBr(builder, loop_bb);

            LLVMPositionBuilderAtEnd(builder, end_bb);
            LLVMValueRef final_outer = outer;
            // Mark nested so array arithmetic handles correctly
            auto& set_nested = runtime_funcs["__arr_set_nested"];
            LLVMValueRef sn[] = { final_outer };
            LLVMBuildCall2(builder, set_nested.fn_type, set_nested.fn, sn, 1, "");

            VarInfo* vi = lookup_var(stmt.var_name);
            if (vi) { LLVMBuildStore(builder, final_outer, vi->alloca_val); vi->tag = JD_TAG_ARR; }
            else { VarInfo& nv = create_var(stmt.var_name, JD_TAG_ARR); LLVMBuildStore(builder, final_outer, nv.alloca_val); }
            // Track element type so arr[i].field access works
            var_udt_type[stmt.var_name + "[]"] = type_name;
            track_dispose_local(stmt.var_name, type_name, /*is_array=*/true);
            return;
        }
    }

    // DIM arr[N] → parser generates CALL("ZEROS", [ARRAY_LITERAL([N])])
    // DIM arr[N, M] → CALL("ZEROS", [ARRAY_LITERAL([N, M])]) → 2D array
    if (stmt.expr->kind == ExprKind::CALL && stmt.expr->func_name == "ZEROS" &&
        !stmt.expr->args.empty() && stmt.expr->args[0]->kind == ExprKind::ARRAY_LITERAL) {
        auto& shape_args = stmt.expr->args[0]->args;
        if (!shape_args.empty()) {
            auto& arr_new = runtime_funcs["__array_new"];
            auto& arr_append = runtime_funcs["APPEND"];

            if (shape_args.size() == 1) {
                // 1D: jdb_array_new(N)
                TypedValue size_val = codegen_expr(*shape_args[0]);
                LLVMValueRef size_i64 = size_val.tag == JD_TAG_F64
                    ? LLVMBuildFPToSI(builder, size_val.val, i64_type, "ftoi")
                    : size_val.val;
                LLVMValueRef args[] = { size_i64 };
                LLVMValueRef arr = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, args, 1, "arr");
                VarInfo* vi = lookup_var(stmt.var_name);
                if (vi) { LLVMBuildStore(builder, arr, vi->alloca_val); vi->tag = JD_TAG_ARR; }
                else { VarInfo& nv = create_var(stmt.var_name, JD_TAG_ARR); LLVMBuildStore(builder, arr, nv.alloca_val); }
            } else {
                // 2D+: build array of arrays
                // Outer array with shape_args[0] elements, each is an inner array of shape_args[1] elements
                TypedValue rows_val = codegen_expr(*shape_args[0]);
                TypedValue cols_val = codegen_expr(*shape_args[1]);
                LLVMValueRef rows = rows_val.tag == JD_TAG_F64
                    ? LLVMBuildFPToSI(builder, rows_val.val, i64_type, "ftoi") : rows_val.val;
                LLVMValueRef cols = cols_val.tag == JD_TAG_F64
                    ? LLVMBuildFPToSI(builder, cols_val.val, i64_type, "ftoi") : cols_val.val;

                // Create outer array (empty, will be filled with inner arrays)
                LLVMValueRef zero_args[] = { LLVMConstInt(i64_type, 0, 0) };
                LLVMValueRef outer = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, zero_args, 1, "outer");

                // Loop: create inner arrays and append to outer
                LLVMBasicBlockRef pre_bb = LLVMGetInsertBlock(builder);
                LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "dim2d.loop");
                LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "dim2d.end");

                // Alloca for loop var and outer array accumulator
                LLVMValueRef idx_alloca = LLVMBuildAlloca(builder, i64_type, "dim_i");
                LLVMValueRef outer_alloca = LLVMBuildAlloca(builder, i8_ptr_type, "dim_outer");
                LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), idx_alloca);
                LLVMBuildStore(builder, outer, outer_alloca);
                LLVMBuildBr(builder, loop_bb);

                LLVMPositionBuilderAtEnd(builder, loop_bb);
                LLVMValueRef cur_idx = LLVMBuildLoad2(builder, i64_type, idx_alloca, "i");
                LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, cur_idx, rows, "cmp");
                LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "dim2d.body");
                LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

                LLVMPositionBuilderAtEnd(builder, body_bb);
                // Create inner array
                LLVMValueRef inner_args[] = { cols };
                LLVMValueRef inner = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, inner_args, 1, "inner");
                // Encode inner ptr as f64 for append
                LLVMValueRef inner_i64 = LLVMBuildPtrToInt(builder, inner, i64_type, "ptoi");
                LLVMValueRef inner_f64 = pun_i64_to_f64(inner_i64);
                // Append to outer
                LLVMValueRef cur_outer = LLVMBuildLoad2(builder, i8_ptr_type, outer_alloca, "out");
                LLVMValueRef append_args[] = { cur_outer, inner_f64 };
                LLVMValueRef new_outer = LLVMBuildCall2(builder, arr_append.fn_type, arr_append.fn, append_args, 2, "out");
                LLVMBuildStore(builder, new_outer, outer_alloca);
                // Increment
                LLVMValueRef next_idx = LLVMBuildAdd(builder, cur_idx, LLVMConstInt(i64_type, 1, 0), "next");
                LLVMBuildStore(builder, next_idx, idx_alloca);
                LLVMBuildBr(builder, loop_bb);

                LLVMPositionBuilderAtEnd(builder, end_bb);
                LLVMValueRef final_outer = LLVMBuildLoad2(builder, i8_ptr_type, outer_alloca, "arr2d");
                // Mark as nested (2D) so array arithmetic recurses correctly
                auto& set_nested = runtime_funcs["__arr_set_nested"];
                LLVMValueRef sn_args[] = { final_outer };
                LLVMBuildCall2(builder, set_nested.fn_type, set_nested.fn, sn_args, 1, "");

                VarInfo* vi = lookup_var(stmt.var_name);
                if (vi) { LLVMBuildStore(builder, final_outer, vi->alloca_val); vi->tag = JD_TAG_ARR; }
                else { VarInfo& nv = create_var(stmt.var_name, JD_TAG_ARR); LLVMBuildStore(builder, final_outer, nv.alloca_val); }
            }
            return;
        }
    }

    // Check if the expression produces an array (e.g. IOTA)
    TypedValue rhs = codegen_expr(*stmt.expr);
    if (rhs.tag == JD_TAG_ARR) {
        VarInfo* vi = lookup_var(stmt.var_name);
        if (vi) {
            LLVMBuildStore(builder, rhs.val, vi->alloca_val);
            vi->tag = JD_TAG_ARR;
        } else {
            VarInfo& nv = create_var(stmt.var_name, JD_TAG_ARR);
            LLVMBuildStore(builder, rhs.val, nv.alloca_val);
        }
        return;
    }

    // DIM inside a FUNC/SUB explicitly shadows any outer binding, so
    // DIM is restricted to the current scope. LET and plain ASSIGN
    // (e.g. `PASS = PASS + 1` inside a SUB) must see outer scopes —
    // that's the only way a SUB can touch a module-level global, and
    // the VM already works this way.
    bool dim_stmt = (stmt.kind == StmtKind::DIM);
    bool in_local = scopes.size() > 1;
    VarInfo* vi = nullptr;
    if (dim_stmt && in_local) {
        auto it = scopes.back().vars.find(stmt.var_name);
        if (it != scopes.back().vars.end()) vi = &it->second;
    } else {
        vi = lookup_var(stmt.var_name);
    }
    // Tag 7 (runtime-tagged): mirror codegen_let_or_assign — store
    // the value AND a companion runtime-tag alloca so later reads can
    // dispatch on the runtime tag. Without this, `DIM c = game{"k"}`
    // keeps the i64 slot but loses the tag, and subsequent `c{"k2"}`
    // passes i64 to functions expecting ptr (IR verification fails).
    if (rhs.tag == JD_TAG_RUNTIME && rhs.runtime_tag) {
        if (!vi) {
            VarInfo& nv = create_var(stmt.var_name, JD_TAG_RUNTIME);
            vi = &nv;
        }
        vi->tag = JD_TAG_RUNTIME;
        if (!vi->runtime_tag_alloca) {
            std::string rtag_name = stmt.var_name + ".rtag";
            bool is_global = (!scopes.empty() && scopes[0].vars.count(stmt.var_name));
            if (is_global) {
                LLVMValueRef g = LLVMAddGlobal(module, i32_type, rtag_name.c_str());
                LLVMSetInitializer(g, LLVMConstInt(i32_type, 0, 0));
                LLVMSetLinkage(g, LLVMInternalLinkage);
                vi->runtime_tag_alloca = g;
            } else {
                LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
                LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(current_fn);
                LLVMValueRef first = LLVMGetFirstInstruction(entry);
                if (first) LLVMPositionBuilderBefore(builder, first);
                else LLVMPositionBuilderAtEnd(builder, entry);
                vi->runtime_tag_alloca = LLVMBuildAlloca(builder, i32_type, rtag_name.c_str());
                LLVMPositionBuilderAtEnd(builder, cur);
            }
        }
        LLVMBuildStore(builder, rhs.val, vi->alloca_val);
        LLVMBuildStore(builder, rhs.runtime_tag, vi->runtime_tag_alloca);
        return;
    }
    // Pre-pass may have provisionally typed vi as RUNTIME (companion rtag
    // alloca + i64 value slot) when it couldn't statically infer the rhs
    // tag — typical for `DIM s AS STRING = arr[i]` where arr's element
    // tracking wasn't decided in time. By the time we reach this store we
    // know rhs.tag concretely; pun the value into the i64 slot, update
    // vi->tag, and bake the concrete tag into the rtag alloca so a stale
    // RUNTIME load wouldn't dispatch on garbage. Without this, an i8*
    // string lands punned-as-f64 and reads back as garbage like 1.04965e-311.
    if (vi && vi->tag == JD_TAG_RUNTIME && rhs.tag != JD_TAG_RUNTIME) {
        LLVMValueRef val_for_storage;
        if (rhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_ARR ||
            rhs.tag == JD_TAG_NATIVE_MAP || rhs.tag == JD_TAG_FUNCREF) {
            val_for_storage = LLVMBuildPtrToInt(builder, rhs.val, i64_type, "ptr2i");
        } else if (rhs.tag == JD_TAG_F64) {
            val_for_storage = pun_f64_to_i64(rhs.val);
        } else {
            val_for_storage = rhs.val;
        }
        LLVMBuildStore(builder, val_for_storage, vi->alloca_val);
        if (vi->runtime_tag_alloca) {
            LLVMBuildStore(builder, LLVMConstInt(i32_type, rhs.tag, 0),
                           vi->runtime_tag_alloca);
        }
        vi->tag = rhs.tag;
        return;
    }
    // VM_HANDLE → STR slot: materialise via __jdrt_val_to_str instead of
    // pun-storing the i64 handle into an i8* slot. Same shape as the
    // codegen_let_or_assign path; necessary for `DIM r$ = AWAIT task` and
    // similar VM-handle-yielding ASYNC FUNC results.
    if (vi && vi->tag == JD_TAG_STR && rhs.tag == JD_TAG_VM_HANDLE) {
        auto* vts = get_runtime_func("__jdrt_val_to_str");
        if (vts) {
            LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
            LLVMValueRef args[] = { rt, rhs.val };
            rhs.val = LLVMBuildCall2(builder, vts->fn_type, vts->fn, args, 2, "vmh2s_dim");
            rhs.tag = JD_TAG_STR;
        }
    }
    // `DIM x AS FLOAT16/32/64 = <int-literal>` — the AS clause must win,
    // otherwise the int RHS tag types the slot as i64. Later
    // `x = x + 0.00628` (with a CONST DOUBLE) then loads x as i64,
    // truncates 0.00628 to int 0, and the add is a no-op — the wavi.jdb
    // animation froze on exactly this. We coerce the RHS to f64 here so
    // the create_var call below sees rhs.tag == JD_TAG_F64. Mirror update
    // for an existing vi: upgrade its tag too so subsequent loads use
    // f64. See feedback_native_dim_init_array.md.
    if ((stmt.var_type == VarType::FLOAT16 ||
         stmt.var_type == VarType::FLOAT32 ||
         stmt.var_type == VarType::FLOAT64) &&
        rhs.tag == JD_TAG_I64) {
        rhs.val = LLVMBuildSIToFP(builder, rhs.val, f64_type, "dim_as_dbl");
        rhs.tag = JD_TAG_F64;
    }
    if (vi) {
        if (rhs.tag == JD_TAG_F64 && vi->tag == JD_TAG_I64) vi->tag = JD_TAG_F64;
        LLVMBuildStore(builder, rhs.val, vi->alloca_val);
    } else {
        VarInfo& nv = create_var(stmt.var_name, rhs.tag);
        LLVMBuildStore(builder, rhs.val, nv.alloca_val);
    }
}

// ── INDEX_ASSIGN: arr[i] = val ──────────────────────────────

void LLVMCodegen::codegen_index_assign(const Stmt& stmt) {
    // Propagate UDT element type when assigning a UDT object into an array
    // slot: `mc_arr[i] = mc_a` records var_udt_type["mc_arr[]"] = T_TestObj
    // so subsequent `mc_arr[j].field` and method calls can resolve the type.
    if (!stmt.var_name.empty() && stmt.expr &&
        stmt.expr->kind == ExprKind::VARIABLE) {
        auto src_it = var_udt_type.find(stmt.expr->str_val);
        if (src_it != var_udt_type.end()) {
            var_udt_type[stmt.var_name + "[]"] = src_it->second;
        }
    }

    // UDT field assignment: obj.field = val (print_exprs[0] = obj, label = field)
    if (!stmt.print_exprs.empty() && !stmt.label.empty()) {
        TypedValue obj = codegen_expr(*stmt.print_exprs[0]);
        TypedValue val = codegen_expr(*stmt.expr);
        LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, stmt.label.c_str(), ".fld");

        // Decode ptr from f64/i64 if needed (e.g. array element holding UDT)
        LLVMValueRef obj_ptr = obj.val;
        if (obj.tag == JD_TAG_F64) {
            LLVMValueRef as_i64 = pun_f64_to_i64(obj.val);
            obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
        } else if (obj.tag == JD_TAG_I64) {
            obj_ptr = LLVMBuildIntToPtr(builder, obj.val, i8_ptr_type, "itoptr");
        }

        // Determine field type
        bool is_str = (!stmt.label.empty() && stmt.label.back() == '$') || val.tag == JD_TAG_STR;
        // Also check UDT registry for all known types
        if (!is_str) {
            for (auto& [tn, flds] : udt_types) {
                for (auto& f : flds) {
                    if (f.name == stmt.label && f.is_string) { is_str = true; break; }
                }
                if (is_str) break;
            }
        }

        if (is_str) {
            auto& set_fn = runtime_funcs["__udt_set_str"];
            LLVMValueRef args[] = { obj_ptr, field_str, to_string_ptr(val) };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        } else {
            LLVMValueRef fval = coerce_to(val, f64_type);
            auto& set_fn = runtime_funcs["__udt_set_f64"];
            LLVMValueRef args[] = { obj_ptr, field_str, fval };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        }
        return;
    }

    VarInfo* vi = lookup_var(stmt.var_name);
    if (!vi || (vi->tag != JD_TAG_ARR && vi->tag != JD_TAG_NATIVE_MAP &&
                vi->tag != JD_TAG_RUNTIME)) return;

    if (!stmt.index_chain.empty() && stmt.index_chain[0]->kind == ExprKind::LITERAL_STRING) {
        std::string field_name = stmt.index_chain[0]->str_val;
        // RUNTIME alloca is i64 (the JdbMap* punned); NATIVE_MAP/ARR is i8_ptr.
        LLVMValueRef obj_ptr;
        if (vi->tag == JD_TAG_RUNTIME) {
            LLVMValueRef bits = LLVMBuildLoad2(builder, i64_type, vi->alloca_val, "obj_i64");
            obj_ptr = LLVMBuildIntToPtr(builder, bits, i8_ptr_type, "obj");
        } else {
            obj_ptr = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "obj");
        }
        LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, field_name.c_str(), ".fld");
        TypedValue val_tv = codegen_expr(*stmt.expr);

        if (vi->tag == JD_TAG_NATIVE_MAP || vi->tag == JD_TAG_RUNTIME) {
            // Map: route to native __map_set_*
            if (val_tv.tag == JD_TAG_STR) {
                auto& set_fn = runtime_funcs["__map_set_str"];
                LLVMValueRef args[] = { obj_ptr, field_str, val_tv.val };
                LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
            } else if (val_tv.tag == JD_TAG_ARR || val_tv.tag == JD_TAG_NATIVE_MAP || val_tv.tag == JD_TAG_VM_HANDLE) {
                // Preserve ptr-like tag (array, map, VM handle) so the
                // tagged getter can return the right type identity. For
                // VM_HANDLE specifically we promote the handle to a
                // persistent (negative) key first so the next DO-LOOP
                // frame_end sweep doesn't erase it — without this, a
                // pattern like `vstate{"list"} = SQLITE.QUERY(...)` (set
                // once during init inside the main loop) reads back
                // garbage from frame 2 onward and indexing segfaults.
                LLVMValueRef fval;
                if (val_tv.tag == JD_TAG_VM_HANDLE) {
                    auto* prom = get_runtime_func("__jdrt_promote_handle");
                    LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                    LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                    LLVMValueRef pa[] = { rt, val_tv.val };
                    LLVMValueRef persistent =
                        LLVMBuildCall2(builder, prom->fn_type, prom->fn, pa, 2, "ph");
                    fval = pun_i64_to_f64(persistent);
                } else {
                    fval = coerce_to(val_tv, f64_type);
                }
                auto& set_fn = runtime_funcs["__map_set_tagged"];
                LLVMValueRef args[] = { obj_ptr, field_str, fval,
                    LLVMConstInt(i32_type, val_tv.tag, 0) };
                LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 4, "");
            } else if (val_tv.tag == JD_TAG_RUNTIME && val_tv.runtime_tag) {
                // Runtime-tagged: pass the live runtime tag through so
                // the stored entry carries its real type identity. The
                // i64 bits' meaning depends on the tag — STR/ARR/VMH bits
                // are ptrs/handles (pun-as-f64); F64 bits are an f64
                // bit-pun (also pun back); I64/BOOL bits are a real int
                // and need SIToFP so the f64 storage cell holds 42.0
                // not the denormal pun of i64=42. Without the I64-aware
                // leg, `vstate{"id"} = row{"id"}` (where row is a JSON
                // object with int field) round-tripped to denormal junk
                // and PRINT formatted it as 9.88e-324.
                LLVMValueRef is_int_v = LLVMBuildICmp(builder, LLVMIntEQ,
                    val_tv.runtime_tag,
                    LLVMConstInt(i32_type, JD_TAG_I64, 0), "obj_isint");
                LLVMValueRef is_bool_v = LLVMBuildICmp(builder, LLVMIntEQ,
                    val_tv.runtime_tag,
                    LLVMConstInt(i32_type, JD_TAG_BOOL, 0), "obj_isbool");
                LLVMValueRef is_intish = LLVMBuildOr(builder, is_int_v, is_bool_v, "obj_isi");
                LLVMValueRef as_f_pun  = pun_i64_to_f64(val_tv.val);
                LLVMValueRef as_f_real = LLVMBuildSIToFP(builder, val_tv.val, f64_type, "obj_i2f");
                LLVMValueRef fval      = LLVMBuildSelect(builder, is_intish, as_f_real, as_f_pun, "obj_f");
                auto& set_fn = runtime_funcs["__map_set_tagged"];
                LLVMValueRef args[] = { obj_ptr, field_str, fval, val_tv.runtime_tag };
                LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 4, "");
            } else {
                LLVMValueRef fval = coerce_to(val_tv, f64_type);
                auto& set_fn = runtime_funcs["__map_set_f64"];
                LLVMValueRef args[] = { obj_ptr, field_str, fval };
                LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
            }
            return;
        }

        // UDT field assignment
        bool is_str = (!field_name.empty() && field_name.back() == '$') || val_tv.tag == JD_TAG_STR;
        if (!is_str)
            is_str = is_udt_string_field(stmt.var_name, field_name);

        if (is_str) {
            auto& set_fn = runtime_funcs["__udt_set_str"];
            LLVMValueRef args[] = { obj_ptr, field_str, to_string_ptr(val_tv) };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        } else {
            LLVMValueRef fval = coerce_to(val_tv, f64_type);
            auto& set_fn = runtime_funcs["__udt_set_f64"];
            LLVMValueRef args[] = { obj_ptr, field_str, fval };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        }
        return;
    }

    // RUNTIME alloca is i64 (ptr-bits punned); NATIVE_MAP/ARR is i8_ptr.
    LLVMValueRef arr_ptr;
    if (vi->tag == JD_TAG_RUNTIME) {
        LLVMValueRef bits = LLVMBuildLoad2(builder, i64_type, vi->alloca_val, "arr_i64");
        arr_ptr = LLVMBuildIntToPtr(builder, bits, i8_ptr_type, "arr");
    } else {
        arr_ptr = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "arr");
    }

    if (stmt.index_chain.empty()) return;

    // For multi-dimensional access (arr[i][j] = val), traverse the chain:
    // each index except the last does array_get / map_get_obj + ptr decode.
    // String indices route to map_get_obj (the target is dynamically a map).
    auto& arr_get = runtime_funcs["__array_get"];
    auto& map_get_obj = runtime_funcs["__map_get_obj"];
    for (size_t ic = 0; ic + 1 < stmt.index_chain.size(); ic++) {
        TypedValue idx_tv = codegen_expr(*stmt.index_chain[ic]);
        if (idx_tv.tag == JD_TAG_STR) {
            LLVMValueRef get_args[] = { arr_ptr, idx_tv.val };
            arr_ptr = LLVMBuildCall2(builder, map_get_obj.fn_type, map_get_obj.fn,
                                     get_args, 2, "inner");
        } else {
            LLVMValueRef idx = idx_tv.tag == JD_TAG_F64
                ? LLVMBuildFPToSI(builder, idx_tv.val, i64_type, "ftoi") : idx_tv.val;
            LLVMValueRef get_args[] = { arr_ptr, idx };
            LLVMValueRef elem = LLVMBuildCall2(builder, arr_get.fn_type, arr_get.fn, get_args, 2, "elem");
            LLVMValueRef as_i64 = pun_f64_to_i64(elem);
            arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "inner");
        }
    }

    // Last index: route to map_set_* if string-keyed, else array_set.
    TypedValue idx_tv = codegen_expr(*stmt.index_chain.back());
    TypedValue val_tv = codegen_expr(*stmt.expr);

    if (idx_tv.tag == JD_TAG_STR) {
        if (val_tv.tag == JD_TAG_STR) {
            auto& fn = runtime_funcs["__map_set_str"];
            LLVMValueRef args[] = { arr_ptr, idx_tv.val, val_tv.val };
            LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "");
        } else if (val_tv.tag == JD_TAG_ARR || val_tv.tag == JD_TAG_NATIVE_MAP || val_tv.tag == JD_TAG_VM_HANDLE) {
            auto& fn = runtime_funcs["__map_set_tagged"];
            LLVMValueRef args[] = { arr_ptr, idx_tv.val,
                coerce_to(val_tv, f64_type),
                LLVMConstInt(i32_type, val_tv.tag, 0) };
            LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "");
        } else if (val_tv.tag == JD_TAG_RUNTIME && val_tv.runtime_tag) {
            // RUNTIME val carries i64 bits whose meaning depends on rtag:
            //   STR / ARR / VMH / NATIVE_MAP — pun ptr-as-i64 → f64 cell
            //   F64 — already f64 bits (pun back)
            //   I64 / BOOL — real int → SIToFP so map storage is a true f64
            //                 (read-back via coerce_to does FPToSI / SIToFP
            //                 symmetric round-trip).
            // Without the I64-aware leg, `vstate{"id"} = row{"id"}` where
            // row{"id"} is an INT64 from JSON stored the int's bit pattern
            // as a denormal double; PRINT then formatted it as 9.88e-324.
            auto& fn = runtime_funcs["__map_set_tagged"];
            LLVMValueRef is_int_v = LLVMBuildICmp(builder, LLVMIntEQ,
                val_tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_I64, 0), "msv_isint");
            LLVMValueRef is_bool_v = LLVMBuildICmp(builder, LLVMIntEQ,
                val_tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_BOOL, 0), "msv_isbool");
            LLVMValueRef is_intish_v = LLVMBuildOr(builder, is_int_v, is_bool_v, "msv_isi");
            LLVMValueRef as_f_pun  = pun_i64_to_f64(val_tv.val);
            LLVMValueRef as_f_real = LLVMBuildSIToFP(builder, val_tv.val, f64_type, "msv_i2f");
            LLVMValueRef store_f   = LLVMBuildSelect(builder, is_intish_v, as_f_real, as_f_pun, "msv_f");
            LLVMValueRef args[] = { arr_ptr, idx_tv.val,
                store_f, val_tv.runtime_tag };
            LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "");
        } else {
            auto& fn = runtime_funcs["__map_set_f64"];
            LLVMValueRef args[] = { arr_ptr, idx_tv.val, coerce_to(val_tv, f64_type) };
            LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "");
        }
        return;
    }

    LLVMValueRef idx = idx_tv.tag == JD_TAG_F64
        ? LLVMBuildFPToSI(builder, idx_tv.val, i64_type, "ftoi") : idx_tv.val;
    LLVMValueRef val = coerce_to(val_tv, f64_type);

    auto& arr_set = runtime_funcs["__array_set"];
    LLVMValueRef args[] = { arr_ptr, idx, val };
    LLVMBuildCall2(builder, arr_set.fn_type, arr_set.fn, args, 3, "");

    // Track element-type for the outer array so subsequent reads can
    // pun the f64 slot back to the right tag. `arr[i] = some_matrix`
    // is the matrix-cache-of-matrices pattern — without this hint a
    // later `glyph_cache[ch]` would come back as f64 and PLOTRAW would
    // see a number where it expects an array.
    if (val_tv.tag == JD_TAG_ARR && stmt.var_name.size() &&
        stmt.index_chain.size() == 1) {
        array_array_vars.insert(stmt.var_name);
    }
    // Same trick for string elements: `names$[i] = "PLAYPAL"` punned the
    // char* through the f64 slot. Without the codegen-side string_array_vars
    // entry, later `names$[i]` reads returned the raw f64 (= integer 0 in
    // PRINT) instead of the original char*. The runtime flag matters too —
    // map/array-iteration paths consult it to know cells hold pointers.
    if (val_tv.tag == JD_TAG_STR && stmt.var_name.size() &&
        stmt.index_chain.size() == 1) {
        string_array_vars.insert(stmt.var_name);
        auto* mark = get_runtime_func("__arr_set_string_elems");
        if (mark) {
            LLVMValueRef margs[] = { arr_ptr };
            LLVMBuildCall2(builder, mark->fn_type, mark->fn, margs, 1, "");
        }
    }
}

// ── PRINT ───────────────────────────────────────────────────

void LLVMCodegen::codegen_print(const Stmt& stmt) {
    auto& pr_int    = runtime_funcs["__print_int"];
    auto& pr_double = runtime_funcs["__print_double"];
    auto& pr_str    = runtime_funcs["__print_str"];
    auto& pr_nl     = runtime_funcs["__print_nl"];
    auto& pr_space  = runtime_funcs["__print_space"];

    for (size_t i = 0; i < stmt.print_exprs.size(); i++) {
        if (i > 0 && i - 1 < stmt.print_seps.size()) {
            if (stmt.print_seps[i - 1] == 1)
                LLVMBuildCall2(builder, pr_space.fn_type, pr_space.fn, nullptr, 0, "");
        }

        // Special case: PRINT arr[idx] — use runtime-aware printer that
        // handles ptr-encoded strings in nested arrays (e.g. SPLIT results).
        const Expr& pe = *stmt.print_exprs[i];
        if (pe.kind == ExprKind::INDEX && pe.left && pe.right) {
            TypedValue arr = codegen_expr(*pe.left);
            if (arr.tag == JD_TAG_ARR) {
                TypedValue idx = codegen_expr(*pe.right);
                LLVMValueRef idx_i64 = idx.tag == JD_TAG_F64
                    ? LLVMBuildFPToSI(builder, idx.val, i64_type, "ftoi") : idx.val;
                auto* fn = get_runtime_func("__print_arr_elem");
                if (fn) {
                    LLVMValueRef args[] = { arr.val, idx_i64 };
                    LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 2, "");
                    continue;
                }
            }
        }

        TypedValue tv = codegen_expr(*stmt.print_exprs[i]);
        if (tv.tag == JD_TAG_BOOL) {
            auto& pr_bool = runtime_funcs["__print_bool"];
            LLVMValueRef args[] = { tv.val };
            LLVMBuildCall2(builder, pr_bool.fn_type, pr_bool.fn, args, 1, "");
        } else if (tv.tag == JD_TAG_NATIVE_MAP) {
            // Format the map via jdb_map_str — feeds {"k": v, ...} into pr_str.
            auto* mfn = get_runtime_func("__map_str");
            if (mfn) {
                LLVMValueRef margs[] = { tv.val };
                LLVMValueRef ms = LLVMBuildCall2(builder, mfn->fn_type, mfn->fn, margs, 1, "ms");
                LLVMValueRef pargs[] = { ms };
                LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, pargs, 1, "");
            }
        } else if (tv.tag == JD_TAG_I64) {
            LLVMValueRef args[] = { tv.val };
            LLVMBuildCall2(builder, pr_int.fn_type, pr_int.fn, args, 1, "");
        } else if (tv.tag == JD_TAG_F64) {
            LLVMValueRef args[] = { tv.val };
            LLVMBuildCall2(builder, pr_double.fn_type, pr_double.fn, args, 1, "");
        } else if (tv.tag == JD_TAG_ARR) {
            // Array → format via FRMV$ ("[a, b, c]") and print as string.
            auto* fmt = get_runtime_func("FRMV$");
            if (fmt) {
                LLVMValueRef fargs[] = { tv.val };
                LLVMValueRef fs = LLVMBuildCall2(builder, fmt->fn_type, fmt->fn, fargs, 1, "afmt");
                LLVMValueRef args[] = { fs };
                LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, args, 1, "");
            }
        } else if (tv.tag == JD_TAG_VM_HANDLE) {
            LLVMValueRef args[] = { to_string_ptr(tv) };
            LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, args, 1, "");
        } else if (tv.tag == JD_TAG_RUNTIME) {
            // Runtime-tagged: branch on the runtime tag at runtime so we
            // pick the right rendering. coerce_to(_, i8_ptr) for ARR now
            // returns the raw array ptr (so JOIN etc. work); PRINT must
            // therefore explicitly call FRMV$ for ARR cells, and use the
            // generic str-coerce only for STR / VMH / numeric cells.
            if (tv.runtime_tag) {
                LLVMValueRef is_arr = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                    LLVMConstInt(i32_type, JD_TAG_ARR, 0), "pr_isarr");
                LLVMBasicBlockRef bb_arr  = LLVMAppendBasicBlock(current_fn, "pr.arr");
                LLVMBasicBlockRef bb_else = LLVMAppendBasicBlock(current_fn, "pr.else");
                LLVMBasicBlockRef bb_join = LLVMAppendBasicBlock(current_fn, "pr.join");
                LLVMBuildCondBr(builder, is_arr, bb_arr, bb_else);

                LLVMPositionBuilderAtEnd(builder, bb_arr);
                auto* frmv = get_runtime_func("FRMV$");
                LLVMValueRef arr_ptr = LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "pr_aptr");
                LLVMValueRef args_a[] = { arr_ptr };
                LLVMValueRef str_a = frmv
                    ? LLVMBuildCall2(builder, frmv->fn_type, frmv->fn, args_a, 1, "pr_arr_s")
                    : arr_ptr;
                LLVMValueRef parr[] = { str_a };
                LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, parr, 1, "");
                LLVMBuildBr(builder, bb_join);

                LLVMPositionBuilderAtEnd(builder, bb_else);
                LLVMValueRef args_e[] = { coerce_to(tv, i8_ptr_type) };
                LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, args_e, 1, "");
                LLVMBuildBr(builder, bb_join);

                LLVMPositionBuilderAtEnd(builder, bb_join);
            } else {
                LLVMValueRef args[] = { coerce_to(tv, i8_ptr_type) };
                LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, args, 1, "");
            }
        } else {
            LLVMValueRef args[] = { tv.val };
            LLVMBuildCall2(builder, pr_str.fn_type, pr_str.fn, args, 1, "");
        }
    }

    if (stmt.print_newline)
        LLVMBuildCall2(builder, pr_nl.fn_type, pr_nl.fn, nullptr, 0, "");
}

// ── FOR Loop ────────────────────────────────────────────────

void LLVMCodegen::codegen_for(const Stmt& stmt) {
    TypedValue start_val = codegen_expr(*stmt.expr);
    TypedValue end_val = codegen_expr(*stmt.end_expr);

    LLVMValueRef step_val;
    if (stmt.step_expr) {
        TypedValue sv = codegen_expr(*stmt.step_expr);
        step_val = sv.tag == JD_TAG_F64
            ? LLVMBuildFPToSI(builder, sv.val, i64_type, "ftoi") : sv.val;
    } else {
        step_val = LLVMConstInt(i64_type, 1, 0);
    }

    VarInfo* vi = lookup_var(stmt.var_name);
    LLVMValueRef var_alloca;
    if (vi) {
        var_alloca = vi->alloca_val;
    } else {
        VarInfo& nv = create_var(stmt.var_name, JD_TAG_I64);
        var_alloca = nv.alloca_val;
    }

    LLVMValueRef start_i64 = start_val.tag == JD_TAG_F64
        ? LLVMBuildFPToSI(builder, start_val.val, i64_type, "ftoi") : start_val.val;
    LLVMBuildStore(builder, start_i64, var_alloca);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.body");
    LLVMBasicBlockRef inc_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.inc");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.end");

    loop_stack.push({ end_bb, inc_bb });

    LLVMBuildBr(builder, cond_bb);

    LLVMPositionBuilderAtEnd(builder, cond_bb);
    LLVMValueRef cur_val = LLVMBuildLoad2(builder, i64_type, var_alloca, "i");
    LLVMValueRef end_i64 = end_val.tag == JD_TAG_F64
        ? LLVMBuildFPToSI(builder, end_val.val, i64_type, "endtoi") : end_val.val;

    // Determine comparison direction based on step sign
    // If step is a known negative constant, use >= instead of <=
    bool negative_step = false;
    if (stmt.step_expr && stmt.step_expr->kind == ExprKind::LITERAL_INT && stmt.step_expr->int_val < 0)
        negative_step = true;
    if (stmt.step_expr && stmt.step_expr->kind == ExprKind::UNARY && stmt.step_expr->op == TokenType::MINUS)
        negative_step = true;

    LLVMValueRef cmp = LLVMBuildICmp(builder,
        negative_step ? LLVMIntSGE : LLVMIntSLE, cur_val, end_i64, "cmp");
    LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(builder, body_bb);
    for (auto& s : stmt.body) {
        if (s) codegen_stmt(*s);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        LLVMBuildBr(builder, inc_bb);

    LLVMPositionBuilderAtEnd(builder, inc_bb);
    LLVMValueRef cur_val2 = LLVMBuildLoad2(builder, i64_type, var_alloca, "i.load");
    LLVMValueRef next_val = LLVMBuildAdd(builder, cur_val2, step_val, "i.next");
    LLVMBuildStore(builder, next_val, var_alloca);
    LLVMBuildBr(builder, cond_bb);

    loop_stack.pop();
    LLVMPositionBuilderAtEnd(builder, end_bb);
}

// ── DO...LOOP ───────────────────────────────────────────────

void LLVMCodegen::codegen_do_loop(const Stmt& stmt) {
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "do.body");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "do.end");

    // Frame-based VM handle cleanup: save a watermark at loop-body start
    // and sweep all handles >= watermark at the end of each iteration.
    // This keeps value_store bounded without affecting long-lived handles
    // (those allocated before the loop — e.g. JSON.PARSE results at init).
    auto emit_frame_begin = [&]() -> LLVMValueRef {
        auto* fb = get_runtime_func("__jdrt_frame_begin");
        if (!fb) return nullptr;
        LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
        if (!hg) return nullptr;
        LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
        LLVMValueRef args[] = { rt };
        return LLVMBuildCall2(builder, fb->fn_type, fb->fn, args, 1, "wm");
    };
    auto emit_frame_end = [&](LLVMValueRef wm) {
        if (!wm) return;
        auto* fe = get_runtime_func("__jdrt_frame_end");
        if (!fe) return;
        LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
        if (!hg) return;
        LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
        LLVMValueRef args[] = { rt, wm };
        LLVMBuildCall2(builder, fe->fn_type, fe->fn, args, 2, "");
    };

    if (stmt.cond_at_top && stmt.loop_cond) {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "do.cond");
        loop_stack.push({ end_bb, cond_bb });

        LLVMBuildBr(builder, cond_bb);

        LLVMPositionBuilderAtEnd(builder, cond_bb);
        TypedValue cond = codegen_expr(*stmt.loop_cond);
        LLVMValueRef cond_i1 = to_i1(cond);
        if (stmt.is_while)
            LLVMBuildCondBr(builder, cond_i1, body_bb, end_bb);
        else
            LLVMBuildCondBr(builder, cond_i1, end_bb, body_bb);

        LLVMPositionBuilderAtEnd(builder, body_bb);
        LLVMValueRef wm = emit_frame_begin();
        for (auto& s : stmt.body) {
            if (s) codegen_stmt(*s);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
            emit_frame_end(wm);
            LLVMBuildBr(builder, cond_bb);
        }

        loop_stack.pop();
    } else {
        loop_stack.push({ end_bb, body_bb });

        LLVMBuildBr(builder, body_bb);

        LLVMPositionBuilderAtEnd(builder, body_bb);
        LLVMValueRef wm = emit_frame_begin();
        for (auto& s : stmt.body) {
            if (s) codegen_stmt(*s);
        }

        if (stmt.loop_cond) {
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                emit_frame_end(wm);
                TypedValue cond = codegen_expr(*stmt.loop_cond);
                LLVMValueRef cond_i1 = to_i1(cond);
                if (stmt.is_while)
                    LLVMBuildCondBr(builder, cond_i1, body_bb, end_bb);
                else
                    LLVMBuildCondBr(builder, cond_i1, end_bb, body_bb);
            }
        } else {
            // Infinite loop: DO ... LOOP (exits only via EXITDO)
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                emit_frame_end(wm);
                LLVMBuildBr(builder, body_bb);
            }
        }

        loop_stack.pop();
    }

    LLVMPositionBuilderAtEnd(builder, end_bb);
}

// ── IF ──────────────────────────────────────────────────────

void LLVMCodegen::codegen_if(const Stmt& stmt) {
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "if.end");

    for (size_t i = 0; i < stmt.branches.size(); i++) {
        auto& branch = stmt.branches[i];

        if (!branch.condition) {
            for (auto& s : branch.body) {
                if (s) codegen_stmt(*s);
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, merge_bb);
        } else {
            TypedValue cond = codegen_expr(*branch.condition);
            LLVMValueRef cond_i1 = to_i1(cond);

            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "if.then");
            LLVMBasicBlockRef else_bb = (i + 1 < stmt.branches.size())
                ? LLVMAppendBasicBlockInContext(ctx, current_fn, "if.else")
                : merge_bb;

            LLVMBuildCondBr(builder, cond_i1, then_bb, else_bb);

            LLVMPositionBuilderAtEnd(builder, then_bb);
            for (auto& s : branch.body) {
                if (s) codegen_stmt(*s);
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, merge_bb);

            if (else_bb != merge_bb)
                LLVMPositionBuilderAtEnd(builder, else_bb);
        }
    }

    LLVMPositionBuilderAtEnd(builder, merge_bb);
}

// ── SWITCH ──────────────────────────────────────────────────

void LLVMCodegen::codegen_switch(const Stmt& stmt) {
    // SWITCH expr / CASE labels / ... / DEFAULT / ENDSWITCH
    // Each branch carries case_labels (multi-value comma list, each entry
    // optionally a "low TO high" range). DEFAULT has both condition and
    // case_labels empty.
    TypedValue switch_val = codegen_expr(*stmt.expr);
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.end");

    // 0=eq, 1=ge, 2=le. Returns an i1 (or sign-equivalent) comparison
    // result. Strings only support equality; range bounds with strings is
    // a hard compile-time error, surfaced as an LLVM verification fail
    // upstream — left as a future ergonomic.
    auto build_cmp = [&](TypedValue sv, TypedValue cv, int op_kind) -> LLVMValueRef {
        if (sv.tag == JD_TAG_STR && cv.tag == JD_TAG_STR) {
            auto& fn = runtime_funcs["__str_eq"];
            LLVMValueRef args[] = { sv.val, cv.val };
            return LLVMBuildICmp(builder, LLVMIntNE,
                LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "eq"),
                LLVMConstInt(i64_type, 0, 0), "cmp");
        } else if (sv.tag == JD_TAG_F64 || cv.tag == JD_TAG_F64) {
            TypedValue svf = promote_to_f64(sv);
            TypedValue cvf = promote_to_f64(cv);
            LLVMRealPredicate p = op_kind == 0 ? LLVMRealOEQ
                                : op_kind == 1 ? LLVMRealOGE
                                               : LLVMRealOLE;
            return LLVMBuildFCmp(builder, p, svf.val, cvf.val, "cmp");
        } else {
            LLVMIntPredicate p = op_kind == 0 ? LLVMIntEQ
                                : op_kind == 1 ? LLVMIntSGE
                                               : LLVMIntSLE;
            return LLVMBuildICmp(builder, p, sv.val, cv.val, "cmp");
        }
    };

    for (size_t i = 0; i < stmt.branches.size(); i++) {
        auto& branch = stmt.branches[i];
        bool is_default = branch.case_labels.empty() && !branch.condition;

        if (is_default) {
            for (auto& s : branch.body) { if (s) codegen_stmt(*s); }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, merge_bb);
            continue;
        }

        // Cascade label tests. Body block is the common landing pad once
        // any label matches. After all labels fail we fall through to
        // next_bb (the next branch, or merge if last).
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.body");
        LLVMBasicBlockRef next_bb = (i + 1 < stmt.branches.size())
            ? LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.next")
            : merge_bb;

        // Legacy single-value condition (kept for safety; parser now uses
        // case_labels exclusively).
        if (branch.case_labels.empty() && branch.condition) {
            TypedValue case_val = codegen_expr(*branch.condition);
            LLVMValueRef cmp = build_cmp(switch_val, case_val, 0);
            LLVMBuildCondBr(builder, cmp, body_bb, next_bb);
        } else {
            for (size_t li = 0; li < branch.case_labels.size(); li++) {
                auto& [low_e, high_e] = branch.case_labels[li];
                bool last = (li + 1 == branch.case_labels.size());
                LLVMBasicBlockRef try_next_bb = last
                    ? next_bb
                    : LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.lbl");

                if (high_e) {
                    // Range: (switch_val >= low) && (switch_val <= high)
                    LLVMBasicBlockRef hi_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.rng_hi");
                    TypedValue lo = codegen_expr(*low_e);
                    LLVMValueRef cmp_lo = build_cmp(switch_val, lo, 1);
                    LLVMBuildCondBr(builder, cmp_lo, hi_bb, try_next_bb);
                    LLVMPositionBuilderAtEnd(builder, hi_bb);
                    TypedValue hi = codegen_expr(*high_e);
                    LLVMValueRef cmp_hi = build_cmp(switch_val, hi, 2);
                    LLVMBuildCondBr(builder, cmp_hi, body_bb, try_next_bb);
                } else {
                    TypedValue cv = codegen_expr(*low_e);
                    LLVMValueRef cmp = build_cmp(switch_val, cv, 0);
                    LLVMBuildCondBr(builder, cmp, body_bb, try_next_bb);
                }

                if (!last)
                    LLVMPositionBuilderAtEnd(builder, try_next_bb);
            }
        }

        LLVMPositionBuilderAtEnd(builder, body_bb);
        for (auto& s : branch.body) { if (s) codegen_stmt(*s); }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
            LLVMBuildBr(builder, merge_bb);

        if (next_bb != merge_bb)
            LLVMPositionBuilderAtEnd(builder, next_bb);
    }
    LLVMPositionBuilderAtEnd(builder, merge_bb);
}

// ── FOR EACH ────────────────────────────────────────────────

void LLVMCodegen::codegen_for_each(const Stmt& stmt) {
    // FOR EACH var IN collection ... NEXT
    TypedValue coll = codegen_expr(*stmt.expr);

    // Get collection pointer (may need conversion from f64 param encoding)
    LLVMValueRef arr_ptr = coll.val;
    if (coll.tag == JD_TAG_F64) {
        LLVMValueRef as_i64 = pun_f64_to_i64(coll.val);
        arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
    }

    // Determine the iter-var's element tag from Phase-2 tracking. Without
    // this, FOR EACH over a string array (e.g. SPLIT, DIR$(FALSE), OS.ARGS)
    // would store the punned-f64 pointer as a raw double — every read of
    // the iter-var then sees 0 instead of the string (regression 2026-05-01).
    bool source_is_string_arr = false;
    if (stmt.expr && stmt.expr->kind == ExprKind::VARIABLE &&
        string_array_vars.count(stmt.expr->str_val)) {
        source_is_string_arr = true;
    }
    int iter_tag = source_is_string_arr ? JD_TAG_STR : JD_TAG_F64;

    // Get length
    auto& len_fn = runtime_funcs["LEN"];
    LLVMValueRef len_args[] = { arr_ptr };
    LLVMValueRef len = LLVMBuildCall2(builder, len_fn.fn_type, len_fn.fn, len_args, 1, "len");

    // Index variable (hidden, unique name to avoid collisions)
    static int foreach_counter = 0;
    std::string idx_name = "__foreach_idx_" + std::to_string(foreach_counter++);
    VarInfo& idx_vi = create_var(idx_name, JD_TAG_I64);
    LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), idx_vi.alloca_val);

    // Loop variable — fresh local in current scope. Tag picked above based
    // on the source array, so the alloca is i8* for string-bearing arrays
    // and the body stores the decoded pointer rather than the raw f64.
    VarInfo& fe_var = create_var(stmt.var_name, iter_tag);
    VarInfo* var_vi = &fe_var;

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "each.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "each.body");
    LLVMBasicBlockRef inc_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "each.inc");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "each.end");

    loop_stack.push({ end_bb, inc_bb });
    LLVMBuildBr(builder, cond_bb);

    // Condition: idx < len
    LLVMPositionBuilderAtEnd(builder, cond_bb);
    LLVMValueRef idx = LLVMBuildLoad2(builder, i64_type, idx_vi.alloca_val, "idx");
    LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, idx, len, "cmp");
    LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

    // Body: var = arr[idx]
    LLVMPositionBuilderAtEnd(builder, body_bb);
    auto& get_fn = runtime_funcs["__array_get"];
    LLVMValueRef get_args[] = { arr_ptr, idx };
    LLVMValueRef elem = LLVMBuildCall2(builder, get_fn.fn_type, get_fn.fn, get_args, 2, "elem");
    if (source_is_string_arr) {
        // The runtime returns the cell as f64; the bits are an i8* pointer
        // for string-bearing arrays. Decode before storing into an i8*
        // alloca (instead of storing the raw f64 into an f64 slot).
        LLVMValueRef as_i64 = pun_f64_to_i64(elem);
        LLVMValueRef as_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "elem_fe_s");
        LLVMBuildStore(builder, as_ptr, var_vi->alloca_val);
    } else {
        LLVMBuildStore(builder, elem, var_vi->alloca_val);
    }

    for (auto& s : stmt.body) { if (s) codegen_stmt(*s); }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        LLVMBuildBr(builder, inc_bb);

    // Increment
    LLVMPositionBuilderAtEnd(builder, inc_bb);
    LLVMValueRef idx2 = LLVMBuildLoad2(builder, i64_type, idx_vi.alloca_val, "idx");
    LLVMBuildStore(builder, LLVMBuildAdd(builder, idx2, LLVMConstInt(i64_type, 1, 0), "inc"), idx_vi.alloca_val);
    LLVMBuildBr(builder, cond_bb);

    loop_stack.pop();
    LLVMPositionBuilderAtEnd(builder, end_bb);
}

// ── ENUM ────────────────────────────────────────────────────

void LLVMCodegen::codegen_enum(const Stmt& stmt) {
    // Register each enum member as TWO globals: bare name AND ENUM.name.
    // Interpreter exposes both: ENUM Direction { NORTH=0 } makes both
    // 'NORTH' and 'Direction.NORTH' resolve to 0.
    const std::string& enum_name = stmt.func_name;
    for (auto& [name, value] : stmt.enum_members) {
        // Bare name
        LLVMValueRef bare = LLVMGetNamedGlobal(module, name.c_str());
        if (!bare) {
            bare = LLVMAddGlobal(module, i64_type, name.c_str());
            LLVMSetLinkage(bare, LLVMInternalLinkage);
        }
        LLVMSetInitializer(bare, LLVMConstInt(i64_type, (uint64_t)value, 1));
        scopes[0].vars[name] = { bare, JD_TAG_I64 };

        // Dotted name: Direction.NORTH
        if (!enum_name.empty()) {
            std::string dotted = enum_name + "." + name;
            LLVMValueRef dot = LLVMGetNamedGlobal(module, dotted.c_str());
            if (!dot) {
                dot = LLVMAddGlobal(module, i64_type, dotted.c_str());
                LLVMSetLinkage(dot, LLVMInternalLinkage);
            }
            LLVMSetInitializer(dot, LLVMConstInt(i64_type, (uint64_t)value, 1));
            scopes[0].vars[dotted] = { dot, JD_TAG_I64 };
        }
    }
}

// ── TYPE_DECL ───────────────────────────────────────────────

void LLVMCodegen::codegen_type_decl(const Stmt& stmt) {
    // Register type fields for member access resolution
    // String if: explicitly AS STRING, or name ends with $ (convention)
    std::vector<UDTField> fields;
    for (auto& mem : stmt.type_members) {
        bool is_str = (mem.type == VarType::STRING) ||
                      (!mem.name.empty() && mem.name.back() == '$');
        fields.push_back({ mem.name, is_str });
    }
    udt_types[stmt.func_name] = fields;

    // Create constructor function: TYPENAME() → returns new object (ptr)
    std::string ctor_name = stmt.func_name;
    LLVMTypeRef ctor_ft = LLVMFunctionType(i8_ptr_type, nullptr, 0, 0);
    LLVMValueRef ctor_fn = LLVMAddFunction(module, ctor_name.c_str(), ctor_ft);

    LLVMValueRef saved_fn = current_fn;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(builder);
    current_fn = ctor_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, ctor_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    // Create new object: jdb_udt_new("TypeName")
    auto& new_fn = runtime_funcs["__udt_new"];
    LLVMValueRef type_str = LLVMBuildGlobalStringPtr(builder, stmt.func_name.c_str(), ".type");
    LLVMValueRef new_args[] = { type_str };
    LLVMValueRef obj = LLVMBuildCall2(builder, new_fn.fn_type, new_fn.fn, new_args, 1, "obj");

    // Set default values for each field
    for (auto& mem : stmt.type_members) {
        LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, mem.name.c_str(), ".field");
        bool is_str = (mem.type == VarType::STRING) ||
                      (!mem.name.empty() && mem.name.back() == '$');

        if (is_str) {
            auto& set_fn = runtime_funcs["__udt_set_str"];
            LLVMValueRef empty = LLVMBuildGlobalStringPtr(builder, "", ".empty");
            LLVMValueRef args[] = { obj, field_str, empty };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        } else {
            auto& set_fn = runtime_funcs["__udt_set_f64"];
            LLVMValueRef args[] = { obj, field_str, LLVMConstReal(f64_type, 0.0) };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        }
    }

    LLVMBuildRet(builder, obj);

    current_fn = saved_fn;
    LLVMPositionBuilderAtEnd(builder, saved_bb);

    // Register as user function (returns ptr, tag=3 for object)
    user_functions[ctor_name] = { ctor_fn, 3, {} };

    // Compile methods (SUB/FUNCTION in body)
    // Parser already renamed methods to TYPENAME.METHOD and inserted THIS as first param
    for (auto& method : stmt.body) {
        if (method && (method->kind == StmtKind::FUNCTION || method->kind == StmtKind::SUB)) {
            bool is_sub = (method->kind == StmtKind::SUB);
            bool returns_str = (!is_sub && !method->func_name.empty() && method->func_name.back() == '$');
            // Infer string return from body if name convention doesn't indicate it
            if (!returns_str && !is_sub) {
                for (auto& s : method->body) {
                    if (s && s->expr && expr_involves_strings(*s->expr)) {
                        returns_str = true;
                        break;
                    }
                }
            }
            int ret_tag = is_sub ? -1 : (returns_str ? 2 : 1);

            // Build parameter types from declared params
            // Parser already added THIS as first param with type OBJECT.
            // For other params, honour AS STRING explicitly (was previously
            // only inferred from the $-suffix convention — broke method
            // params like `n AS STRING`).
            std::vector<LLVMTypeRef> ptypes;
            std::vector<int> ptags;
            for (auto& p : method->params) {
                if (p.name == "THIS" || p.type == VarType::OBJECT) {
                    ptypes.push_back(i8_ptr_type);
                    ptags.push_back(3);  // ptr for UDT object
                } else {
                    bool sp = p.type == VarType::STRING ||
                              (!p.name.empty() && p.name.back() == '$');
                    ptypes.push_back(sp ? i8_ptr_type : f64_type);
                    ptags.push_back(sp ? 2 : 1);
                }
            }
            LLVMTypeRef mft = LLVMFunctionType(
                is_sub ? void_type : (returns_str ? i8_ptr_type : f64_type),
                ptypes.data(), (unsigned)ptypes.size(), 0);
            LLVMValueRef mfn = LLVMAddFunction(module, method->func_name.c_str(), mft);
            user_functions[method->func_name] = { mfn, ret_tag, ptags };

            // Set THIS UDT type mapping for the method body
            var_udt_type["THIS"] = stmt.func_name;
            codegen_function(*method);
            var_udt_type.erase("THIS");
        }
    }
}

// ── Expression Codegen ──────────────────────────────────────

LLVMCodegen::TypedValue LLVMCodegen::codegen_expr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::LITERAL_INT:
            return { LLVMConstInt(i64_type, (uint64_t)expr.int_val, 1), JD_TAG_I64 };

        case ExprKind::LITERAL_FLOAT:
            return { LLVMConstReal(f64_type, expr.float_val), JD_TAG_F64 };

        case ExprKind::LITERAL_STRING:
            return { LLVMBuildGlobalStringPtr(builder, expr.str_val.c_str(), ".str"), JD_TAG_STR };

        case ExprKind::LITERAL_BOOL:
            return { LLVMConstInt(i64_type, expr.bool_val ? 1 : 0, 0), JD_TAG_BOOL };

        case ExprKind::ARRAY_LITERAL: {
            // [] or [a, b, c] → create array
            auto& arr_new = runtime_funcs["__array_new"];
            LLVMValueRef size_arg[] = { LLVMConstInt(i64_type, 0, 0) };
            LLVMValueRef arr = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, size_arg, 1, "arr");
            bool has_ptr_elems = false;
            bool has_string_elems = false;
            bool all_bool_elems = !expr.args.empty();
            // Pre-scan: any INDEX-typed element forces tagged storage so
            // each cell carries its own JdTag and arr[i] reads can recover
            // the real type at runtime instead of statically guessing.
            bool any_runtime = false;
            for (auto& a : expr.args)
                if (a && (a->kind == ExprKind::INDEX)) { any_runtime = true; break; }
            if (!expr.args.empty()) {
                auto& arr_append = runtime_funcs["APPEND"];
                auto& arr_append_tg = runtime_funcs["__arr_append_tagged"];
                for (size_t i = 0; i < expr.args.size(); i++) {
                    TypedValue elem = codegen_expr(*expr.args[i]);
                    if (elem.tag != JD_TAG_BOOL) all_bool_elems = false;
                    LLVMValueRef fval = elem.val;
                    int store_tag = elem.tag;
                    if (elem.tag == JD_TAG_I64 || elem.tag == JD_TAG_BOOL) {
                        fval = LLVMBuildSIToFP(builder, fval, f64_type, "itof");
                        store_tag = JD_TAG_F64;  // f64-shaped after coerce
                    }
                    else if (elem.tag == JD_TAG_STR || elem.tag == JD_TAG_ARR) {
                        // ptr (string or array) → encode as f64
                        LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, fval, i64_type, "ptoi");
                        fval = pun_i64_to_f64(as_i64);
                        has_ptr_elems = true;
                        if (elem.tag == JD_TAG_STR) has_string_elems = true;
                    } else if (elem.tag == JD_TAG_NATIVE_MAP || elem.tag == JD_TAG_VM_HANDLE) {
                        // Map / VM handle ptr → encode as f64 bits
                        LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, fval, i64_type, "ptoi");
                        fval = pun_i64_to_f64(as_i64);
                        has_ptr_elems = true;
                    } else if (elem.tag == JD_TAG_RUNTIME) {
                        // Runtime-tagged: preserve the raw bits (val is i64).
                        fval = pun_i64_to_f64(elem.val);
                        has_ptr_elems = true;
                    }
                    if (any_runtime) {
                        // Tagged path: store value + concrete or runtime tag.
                        LLVMValueRef tag_v;
                        if (elem.tag == JD_TAG_RUNTIME && elem.runtime_tag)
                            tag_v = elem.runtime_tag;
                        else
                            tag_v = LLVMConstInt(i32_type, store_tag, 0);
                        LLVMValueRef append_args[] = { arr, fval, tag_v };
                        arr = LLVMBuildCall2(builder, arr_append_tg.fn_type,
                            arr_append_tg.fn, append_args, 3, "arr");
                    } else {
                        LLVMValueRef append_args[] = { arr, fval };
                        arr = LLVMBuildCall2(builder, arr_append.fn_type,
                            arr_append.fn, append_args, 2, "arr");
                    }
                }
                if (has_ptr_elems && !any_runtime) {
                    auto& set_nested = runtime_funcs["__arr_set_nested"];
                    LLVMValueRef sn[] = { arr };
                    LLVMBuildCall2(builder, set_nested.fn_type, set_nested.fn, sn, 1, "");
                    if (has_string_elems) {
                        auto* set_str = get_runtime_func("__arr_set_string_elems");
                        if (set_str) {
                            LLVMValueRef ss[] = { arr };
                            LLVMBuildCall2(builder, set_str->fn_type, set_str->fn, ss, 1, "");
                        }
                    }
                } else if (all_bool_elems) {
                    auto* set_bool = get_runtime_func("__arr_set_bool_elems");
                    if (set_bool) {
                        LLVMValueRef sb[] = { arr };
                        LLVMBuildCall2(builder, set_bool->fn_type, set_bool->fn, sb, 1, "");
                    }
                }
            }
            return { arr, JD_TAG_ARR };
        }

        case ExprKind::MAP_LITERAL: {
            // {"k1": v1, "k2": v2, ...} → __map_new() then __map_set_*
            auto& map_new = runtime_funcs["__map_new"];
            LLVMValueRef m = LLVMBuildCall2(builder, map_new.fn_type, map_new.fn,
                                             nullptr, 0, "map");
            for (size_t i = 0; i < expr.map_keys.size() && i < expr.args.size(); i++) {
                LLVMValueRef key = LLVMBuildGlobalStringPtr(builder,
                    expr.map_keys[i].c_str(), ".mk");
                TypedValue v = codegen_expr(*expr.args[i]);
                if (v.tag == JD_TAG_STR) {
                    auto& set_str = runtime_funcs["__map_set_str"];
                    LLVMValueRef args[] = { m, key, v.val };
                    LLVMBuildCall2(builder, set_str.fn_type, set_str.fn, args, 3, "");
                } else if (v.tag == JD_TAG_ARR || v.tag == JD_TAG_NATIVE_MAP || v.tag == JD_TAG_VM_HANDLE) {
                    // Nested ptrs — preserve tag so tagged getter
                    // returns the right type identity.
                    LLVMValueRef fv = coerce_to(v, f64_type);
                    auto& set_tg = runtime_funcs["__map_set_tagged"];
                    LLVMValueRef args[] = { m, key, fv,
                        LLVMConstInt(i32_type, v.tag, 0) };
                    LLVMBuildCall2(builder, set_tg.fn_type, set_tg.fn, args, 4, "");
                } else if (v.tag == JD_TAG_RUNTIME && v.runtime_tag) {
                    LLVMValueRef fv = pun_i64_to_f64(v.val);
                    auto& set_tg = runtime_funcs["__map_set_tagged"];
                    LLVMValueRef args[] = { m, key, fv, v.runtime_tag };
                    LLVMBuildCall2(builder, set_tg.fn_type, set_tg.fn, args, 4, "");
                } else if (v.tag == JD_TAG_BOOL) {
                    // Preserve the bool tag so jdb_map_str renders TRUE/FALSE
                    // instead of falling back to numeric formatting.
                    LLVMValueRef fv = coerce_to(v, f64_type);
                    auto& set_tg = runtime_funcs["__map_set_tagged"];
                    LLVMValueRef args[] = { m, key, fv,
                        LLVMConstInt(i32_type, JD_TAG_BOOL, 0) };
                    LLVMBuildCall2(builder, set_tg.fn_type, set_tg.fn, args, 4, "");
                } else {
                    LLVMValueRef fv = coerce_to(v, f64_type);
                    auto& set_f64 = runtime_funcs["__map_set_f64"];
                    LLVMValueRef args[] = { m, key, fv };
                    LLVMBuildCall2(builder, set_f64.fn_type, set_f64.fn, args, 3, "");
                }
            }
            return { m, JD_TAG_NATIVE_MAP };  // tag=4 means map/object
        }

        case ExprKind::VARIABLE: {
            VarInfo* vi = lookup_var(expr.str_val);
            if (!vi) {
                // Check if this is a dotted UDT field access (e.g. PLAYER1.NAME)
                size_t dp = expr.str_val.find('.');
                if (dp != std::string::npos) {
                    std::string obj_name = expr.str_val.substr(0, dp);
                    std::string field_name = expr.str_val.substr(dp + 1);
                    VarInfo* obj_vi = lookup_var(obj_name);
                    if (obj_vi && var_udt_type.count(obj_name)) {
                        LLVMValueRef obj_ptr = LLVMBuildLoad2(builder, i8_ptr_type,
                                                               obj_vi->alloca_val, "obj");
                        LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder,
                                                    field_name.c_str(), ".fld");
                        bool is_str = is_udt_string_field(obj_name, field_name);
                        if (is_str) {
                            auto& get_fn = runtime_funcs["__udt_get_str"];
                            LLVMValueRef args[] = { obj_ptr, field_str };
                            LLVMValueRef result = LLVMBuildCall2(builder, get_fn.fn_type,
                                                    get_fn.fn, args, 2, "fget");
                            return { result, JD_TAG_STR };
                        } else {
                            auto& get_fn = runtime_funcs["__udt_get_f64"];
                            LLVMValueRef args[] = { obj_ptr, field_str };
                            LLVMValueRef result = LLVMBuildCall2(builder, get_fn.fn_type,
                                                    get_fn.fn, args, 2, "fget");
                            return { result, JD_TAG_F64 };
                        }
                    }
                }
                // Bare-identifier constants like PI, E — call the 0-arg native fn.
                std::string upper = expr.str_val;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                auto rit = runtime_funcs.find(upper);
                if (rit != runtime_funcs.end()) {
                    unsigned pc = LLVMCountParamTypes(rit->second.fn_type);
                    if (pc == 0) {
                        LLVMValueRef result = LLVMBuildCall2(builder, rit->second.fn_type,
                            rit->second.fn, nullptr, 0, expr.str_val.c_str());
                        return { result, rit->second.return_tag };
                    }
                }
                // Phase 3 EXPLICIT: a bare read of an undeclared name is a
                // compile error. Dotted names (module.var) are skipped —
                // those land here when the module target isn't a known UDT
                // and are better reported at their specific use site.
                if (is_explicit_here(m_current_stmt_file) &&
                    expr.str_val.find('.') == std::string::npos) {
                    report_error(m_current_stmt_file, expr.line,
                        "undeclared variable '" + expr.str_val + "'");
                }
                return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
            }
            LLVMTypeRef load_type;
            int tag = vi->tag;
            if (tag == 1)       load_type = f64_type;
            else if (tag == 2)  load_type = i8_ptr_type;
            else if (tag == 3)  load_type = i8_ptr_type;
            else if (tag == 4)  load_type = i8_ptr_type;  // map ptr
            else                load_type = i64_type;  // covers 0, 6, 7
            if (tag == 7 && vi->runtime_tag_alloca) {
                LLVMValueRef val = LLVMBuildLoad2(builder, load_type, vi->alloca_val,
                                                   expr.str_val.c_str());
                LLVMValueRef rt = LLVMBuildLoad2(builder, i32_type,
                                                  vi->runtime_tag_alloca, "rtag");
                TypedValue r; r.val = val; r.tag = JD_TAG_RUNTIME; r.runtime_tag = rt;
                return r;
            }
            return { LLVMBuildLoad2(builder, load_type, vi->alloca_val,
                                    expr.str_val.c_str()), tag };
        }

        case ExprKind::BINARY:
            return codegen_binary(expr);

        case ExprKind::UNARY:
            return codegen_unary(expr);

        case ExprKind::MEMBER_ACCESS: {
            // obj.field — get field from UDT object
            TypedValue obj = codegen_expr(*expr.left);
            std::string field_name = expr.str_val;
            LLVMValueRef obj_ptr = obj.val;

            // Decode ptr from f64/i64 if needed (e.g. array element)
            if (obj.tag == JD_TAG_F64) {
                LLVMValueRef as_i64 = pun_f64_to_i64(obj.val);
                obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            } else if (obj.tag == JD_TAG_I64) {
                obj_ptr = LLVMBuildIntToPtr(builder, obj.val, i8_ptr_type, "itoptr");
            }

            // Determine if field is string-typed: check name convention AND UDT registry
            bool is_str_field = (!field_name.empty() && field_name.back() == '$');
            if (!is_str_field && expr.left) {
                std::string var_name;
                if (expr.left->kind == ExprKind::VARIABLE)
                    var_name = expr.left->str_val;
                if (!var_name.empty())
                    is_str_field = is_udt_string_field(var_name, field_name);
            }
            // Fallback: search all UDT types for this field name
            if (!is_str_field) {
                for (auto& [tn, flds] : udt_types) {
                    for (auto& f : flds) {
                        if (f.name == field_name && f.is_string) {
                            is_str_field = true;
                            break;
                        }
                    }
                    if (is_str_field) break;
                }
            }

            LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, field_name.c_str(), ".fld");

            if (is_str_field) {
                auto& get_fn = runtime_funcs["__udt_get_str"];
                LLVMValueRef args[] = { obj_ptr, field_str };
                LLVMValueRef result = LLVMBuildCall2(builder, get_fn.fn_type, get_fn.fn, args, 2, "fget");
                return { result, JD_TAG_STR };
            } else {
                auto& get_fn = runtime_funcs["__udt_get_f64"];
                LLVMValueRef args[] = { obj_ptr, field_str };
                LLVMValueRef result = LLVMBuildCall2(builder, get_fn.fn_type, get_fn.fn, args, 2, "fget");
                return { result, JD_TAG_F64 };
            }
        }

        case ExprKind::CALL:
            return codegen_call(expr);

        case ExprKind::PIPE_EXPR: {
            // value |> func  or  value |> expr(?)
            TypedValue left_val = codegen_expr(*expr.left);

            if (expr.right->kind == ExprKind::LAMBDA_EXPR) {
                // value |> lambda x -> body
                // Determine lambda signature based on input type
                bool array_input = (left_val.tag == JD_TAG_ARR);

                if (array_input) {
                    // Array PIPE: compile lambda body first to determine return type,
                    // then create function with correct signature
                    static int arr_lambda_counter = 0;
                    std::string name = "__arr_lambda_" + std::to_string(arr_lambda_counter++);
                    auto& lam = *expr.right;
                    int arity = (int)lam.lambda_params.size();

                    // Step 1: Create a preliminary ptr(ptr) function to compile body
                    std::vector<LLVMTypeRef> ptypes(arity, i8_ptr_type);
                    // We'll determine return type from body
                    LLVMTypeRef fn_type_ptr = LLVMFunctionType(i8_ptr_type, ptypes.data(), arity, 0);
                    LLVMTypeRef fn_type_f64 = LLVMFunctionType(f64_type, ptypes.data(), arity, 0);

                    LLVMValueRef saved_fn = current_fn;
                    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(builder);

                    // Probe: compile body in a temp scope to determine return tag
                    scopes.push_back(Scope{});
                    // Create temp params as array (tag=3)
                    for (int i = 0; i < arity; i++) {
                        // Don't emit LLVM — just register in scope for type tracking
                        scopes.back().vars[lam.lambda_params[i]] = { nullptr, JD_TAG_ARR };
                    }
                    // We can't compile the body without a proper function context,
                    // so use a heuristic: check if the body is a CALL to a known
                    // scalar-returning function (SUM, MEAN, PRODUCT, etc.)
                    bool returns_scalar = false;
                    if (lam.right->kind == ExprKind::CALL) {
                        std::string fn = lam.right->func_name;
                        std::transform(fn.begin(), fn.end(), fn.begin(), ::toupper);
                        if (fn == "SUM" || fn == "PRODUCT" || fn == "MEAN" ||
                            fn == "STDEV" || fn == "MEDIAN" || fn == "VARIANCE" ||
                            fn == "MIN" || fn == "MAX" || fn == "LEN" ||
                            fn == "ANY" || fn == "ALL" || fn == "COUNT")
                            returns_scalar = true;
                    }
                    scopes.pop_back();

                    LLVMTypeRef fn_type = returns_scalar ? fn_type_f64 : fn_type_ptr;
                    LLVMValueRef lambda_fn = LLVMAddFunction(module, name.c_str(), fn_type);

                    current_fn = lambda_fn;
                    scopes.push_back(Scope{});

                    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, lambda_fn, "entry");
                    LLVMPositionBuilderAtEnd(builder, entry);

                    for (int i = 0; i < arity; i++) {
                        VarInfo& vi = create_var(lam.lambda_params[i], JD_TAG_ARR);
                        LLVMBuildStore(builder, LLVMGetParam(lambda_fn, i), vi.alloca_val);
                    }

                    TypedValue body = codegen_expr(*lam.right);
                    LLVMBuildRet(builder, body.val);

                    scopes.pop_back();
                    current_fn = saved_fn;
                    LLVMPositionBuilderAtEnd(builder, saved_bb);

                    // Call the array lambda
                    LLVMValueRef args[] = { left_val.val };
                    LLVMValueRef result = LLVMBuildCall2(builder, fn_type, lambda_fn, args, 1, "apipe");
                    return { result, returns_scalar ? 1 : 3 };
                } else {
                    // Scalar PIPE: standard double(double) lambda
                    TypedValue lambda = codegen_expr(*expr.right);
                    LLVMTypeRef call_ft = LLVMFunctionType(f64_type, &f64_type, 1, 0);
                    LLVMValueRef arg = left_val.val;
                    if (left_val.tag == JD_TAG_I64) arg = LLVMBuildSIToFP(builder, arg, f64_type, "itof");
                    LLVMValueRef args[] = { arg };
                    LLVMValueRef result = LLVMBuildCall2(builder, call_ft, lambda.val, args, 1, "pipe");
                    return { result, JD_TAG_F64 };
                }
            } else if (expr.right->kind == ExprKind::VARIABLE ||
                       expr.right->kind == ExprKind::LITERAL_STRING) {
                // value |> FuncName     →  FuncName(value)
                // value |> Mod.FuncName@ →  Mod.FuncName(value)   (LITERAL_STRING is the funcref form)
                std::string fn_name = expr.right->str_val;
                auto uit = user_functions.find(fn_name);
                if (uit != user_functions.end()) {
                    auto& fi = uit->second;
                    LLVMValueRef arg = left_val.val;
                    if (left_val.tag == JD_TAG_I64) arg = LLVMBuildSIToFP(builder, arg, f64_type, "itof");
                    LLVMValueRef args[] = { arg };
                    LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
                    LLVMValueRef result = LLVMBuildCall2(builder, fn_type, fi.fn, args, 1, "pipe");
                    return { result, fi.return_tag };
                }
            } else if (expr.right->kind == ExprKind::CALL) {
                // value |> FUNC(?, other_args)  →  store value in temp, compile FUNC call
                // The ? placeholder in the call args should be replaced with left_val
                // For now: store left_val as __PIPE_TMP, then compile the call
                // (the PLACEHOLDER_EXPR will load __PIPE_TMP)
                VarInfo* pipe_var = lookup_var("__PIPE_TMP__");
                if (!pipe_var) {
                    VarInfo& nv = create_var("__PIPE_TMP__", left_val.tag);
                    pipe_var = &nv;
                }
                LLVMValueRef store_val = left_val.val;
                if (left_val.tag == JD_TAG_I64 && pipe_var->tag == JD_TAG_F64)
                    store_val = LLVMBuildSIToFP(builder, store_val, f64_type, "itof");
                LLVMBuildStore(builder, store_val, pipe_var->alloca_val);
                return codegen_expr(*expr.right);
            }

            // Fallback: just return left value
            return left_val;
        }

        case ExprKind::PLACEHOLDER_EXPR: {
            // ? in pipe context → load __PIPE_TMP__
            VarInfo* vi = lookup_var("__PIPE_TMP__");
            if (vi) {
                LLVMTypeRef load_type = (vi->tag == JD_TAG_F64) ? f64_type :
                                        (vi->tag == JD_TAG_STR) ? i8_ptr_type :
                                        (vi->tag == JD_TAG_ARR) ? i8_ptr_type : i64_type;
                return { LLVMBuildLoad2(builder, load_type, vi->alloca_val, "pipe_val"), vi->tag };
            }
            return { LLVMConstReal(f64_type, 0.0), JD_TAG_F64 };
        }

        case ExprKind::LAMBDA_EXPR: {
            static int lambda_counter = 0;
            std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);
            int arity = (int)expr.lambda_params.size();

            std::vector<LLVMTypeRef> param_types(arity, f64_type);
            LLVMTypeRef fn_type = LLVMFunctionType(f64_type,
                param_types.empty() ? nullptr : param_types.data(), arity, 0);
            LLVMValueRef lambda_fn = LLVMAddFunction(module, lambda_name.c_str(), fn_type);

            // Save current state — including the exact insert block
            LLVMValueRef saved_fn = current_fn;
            LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(builder);

            current_fn = lambda_fn;
            scopes.push_back(Scope{});

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, lambda_fn, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);

            for (int i = 0; i < arity; i++) {
                VarInfo& vi = create_var(expr.lambda_params[i], JD_TAG_F64);
                LLVMBuildStore(builder, LLVMGetParam(lambda_fn, i), vi.alloca_val);
            }

            TypedValue body = codegen_expr(*expr.right);

            LLVMValueRef ret_val = body.val;
            // Lambda is declared as returning f64. Comparison ops (=, <,
            // >, MOD, …) return JD_TAG_BOOL which is an i1-zext-to-i64;
            // letting that flow into LLVMBuildRet without coercion lands
            // an `i64` into a function whose signature is `double`, and
            // the IR verifier rejects the module ("return type does not
            // match"). Coerce both i64 and bool back to f64 before ret.
            if (body.tag == JD_TAG_I64 || body.tag == JD_TAG_BOOL)
                ret_val = LLVMBuildSIToFP(builder, ret_val, f64_type, "itof");
            LLVMBuildRet(builder, ret_val);

            // Restore state — position builder back at the EXACT block we were in
            scopes.pop_back();
            current_fn = saved_fn;
            LLVMPositionBuilderAtEnd(builder, saved_bb);

            return { lambda_fn, JD_TAG_FUNCREF };
        }

        case ExprKind::INDEX: {
            // Consume outer's "want ptr" hint (propagated through chains).
            bool want_ptr = m_want_ptr_result;

            // arr[i] — array element access or obj{"key"} map access.
            // Propagate "want ptr" to left subexpr ONLY when the current
            // index is a string key (chained map/UDT access like
            // `m1{"a"}{"b"}`). For an integer index (`vstate{"list"}[0]`)
            // the inner's stored value may be a VM_HANDLE — fetching it
            // via the raw __map_get_obj pointer-fast-path bit-pun's the
            // handle into a JdbArray* and the outer indexing segfaults.
            // The tag-aware path (__map_get_tagged) dispatches correctly.
            bool right_is_str_key =
                expr.right && (expr.right->kind == ExprKind::LITERAL_STRING ||
                               (expr.right->kind == ExprKind::VARIABLE &&
                                !expr.right->str_val.empty() &&
                                expr.right->str_val.back() == '$'));
            // Inner LEFT subexpr: clear leaf_tag (would be wrong scope) and
            // set want_ptr only when the current index is a string key.
            // RAII guards auto-restore the outer caller's saved values when
            // we leave this scope, so neither hint can leak past arr_tv.
            // Without this, e.g. `vn$ = vstate{"items"}[idx]` would leak the
            // STR leaf-hint into `vstate{"items"}` which takes the
            // jdb_map_get_str fast path, stringifies the stored ARRAY entry,
            // and the outer `[idx]` segfaults bit-punning a string ptr as
            // JdbArray*.
            TypedValue arr_tv;
            {
                ScopedLeafTag _lt(this, -1);
                ScopedPtrResult _pr(this, right_is_str_key);
                arr_tv = codegen_expr(*expr.left);
            }
            // Index expression: also no inherited hints — idx is its own
            // scope. (Without -1 here, an idx like `other{"i"}` would see
            // OUTER's STR hint and stringify the int it returned.)
            TypedValue idx_tv;
            {
                ScopedLeafTag _lt(this, -1);
                ScopedPtrResult _pr(this, false);
                idx_tv = codegen_expr(*expr.right);
            }

            // String key → map (tag=4 native, tag=6 VM handle) or UDT.
            if (idx_tv.tag == JD_TAG_STR) {
                // Read OUTER's leaf-type hint for our own dispatch, then
                // clear so nested runtime helper calls below don't see it.
                int leaf_hint = m_want_leaf_tag;
                ScopedLeafTag _lt(this, -1);
                // Runtime-tagged (tag 7): dispatch via unified C function
                // that handles both native map and VM handle.
                if (arr_tv.tag == JD_TAG_RUNTIME && arr_tv.runtime_tag) {
                    auto& gtag = runtime_funcs["__jdrt_tagged_get"];
                    LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                    LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                    LLVMValueRef out = LLVMBuildAlloca(builder, i64_type, "tv7out");
                    LLVMValueRef targs[] = { rt, arr_tv.val, arr_tv.runtime_tag,
                                             idx_tv.val, out };
                    LLVMValueRef tv_tag = LLVMBuildCall2(builder, gtag.fn_type,
                        gtag.fn, targs, 5, "tv7tag");
                    LLVMValueRef tv_val = LLVMBuildLoad2(builder, i64_type, out, "tv7val");
                    TypedValue r; r.val = tv_val; r.tag = JD_TAG_RUNTIME; r.runtime_tag = tv_tag;
                    // Honour leaf_hint from the dest var so a $-var stays
                    // tag=2 (string) instead of being promoted to tag=7 by
                    // assignment — a conditionally-skipped write would then
                    // leave the runtime_tag slot at 0 and turn later reads
                    // into garbage-formatted doubles.
                    if (leaf_hint == 2) return { coerce_to(r, i8_ptr_type), JD_TAG_STR };
                    if (leaf_hint == 1) return { coerce_to(r, f64_type), JD_TAG_F64 };
                    if (leaf_hint == 0) {
                        LLVMValueRef d = coerce_to(r, f64_type);
                        return { LLVMBuildFPToSI(builder, d, i64_type, "7toi"), JD_TAG_I64 };
                    }
                    return r;
                }
                if (arr_tv.tag == JD_TAG_NATIVE_MAP) {
                    if (want_ptr) {
                        auto& gobj = runtime_funcs["__map_get_obj"];
                        LLVMValueRef args[] = { arr_tv.val, idx_tv.val };
                        return { LLVMBuildCall2(builder, gobj.fn_type, gobj.fn, args, 2, "mgetobj"), JD_TAG_NATIVE_MAP };
                    }
                    // Known-type fast paths (when ASSIGN / call-arg set the hint).
                    if (leaf_hint == 0 || leaf_hint == 1) {
                        auto& gf = runtime_funcs["__map_get_f64"];
                        LLVMValueRef args[] = { arr_tv.val, idx_tv.val };
                        return { LLVMBuildCall2(builder, gf.fn_type, gf.fn, args, 2, "mgetf"), JD_TAG_F64 };
                    }
                    if (leaf_hint == 2) {
                        auto& gs = runtime_funcs["__map_get_str"];
                        LLVMValueRef args[] = { arr_tv.val, idx_tv.val };
                        return { LLVMBuildCall2(builder, gs.fn_type, gs.fn, args, 2, "mget"), JD_TAG_STR };
                    }
                    // No hint → TAGGED getter. Runtime tells us the type.
                    {
                        auto& gtag = runtime_funcs["__map_get_tagged"];
                        LLVMValueRef out = LLVMBuildAlloca(builder, i64_type, "tv_out");
                        LLVMValueRef targs[] = { arr_tv.val, idx_tv.val, out };
                        LLVMValueRef tv_tag = LLVMBuildCall2(builder, gtag.fn_type, gtag.fn, targs, 3, "mtag");
                        LLVMValueRef tv_val = LLVMBuildLoad2(builder, i64_type, out, "tv_val");
                        TypedValue r; r.val = tv_val; r.tag = JD_TAG_RUNTIME; r.runtime_tag = tv_tag; return r;
                    }
                }
                if (arr_tv.tag == JD_TAG_VM_HANDLE) {
                    LLVMValueRef handle_g = LLVMGetNamedGlobal(module, "__jdrt_handle");
                    LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, handle_g, "rt");
                    if (want_ptr) {
                        auto& go = runtime_funcs["__jdrt_obj_get_obj"];
                        LLVMValueRef args[] = { rt, arr_tv.val, idx_tv.val };
                        return { LLVMBuildCall2(builder, go.fn_type, go.fn, args, 3, "ogetobj"), JD_TAG_VM_HANDLE };
                    }
                    // Known-type fast paths.
                    if (leaf_hint == 0 || leaf_hint == 1) {
                        auto& gf = runtime_funcs["__jdrt_obj_get_f64"];
                        LLVMValueRef args[] = { rt, arr_tv.val, idx_tv.val };
                        return { LLVMBuildCall2(builder, gf.fn_type, gf.fn, args, 3, "ogetf"), JD_TAG_F64 };
                    }
                    if (leaf_hint == 2) {
                        auto& gs = runtime_funcs["__jdrt_obj_get_str"];
                        LLVMValueRef args[] = { rt, arr_tv.val, idx_tv.val };
                        return { LLVMBuildCall2(builder, gs.fn_type, gs.fn, args, 3, "ogets"), JD_TAG_STR };
                    }
                    // No hint → TAGGED getter.
                    {
                        auto& gtag = runtime_funcs["__jdrt_obj_get_tagged"];
                        LLVMValueRef out = LLVMBuildAlloca(builder, i64_type, "tv_out");
                        LLVMValueRef targs[] = { rt, arr_tv.val, idx_tv.val, out };
                        LLVMValueRef tv_tag = LLVMBuildCall2(builder, gtag.fn_type, gtag.fn, targs, 4, "otag");
                        LLVMValueRef tv_val = LLVMBuildLoad2(builder, i64_type, out, "tv_val");
                        TypedValue r; r.val = tv_val; r.tag = JD_TAG_RUNTIME; r.runtime_tag = tv_tag; return r;
                    }
                }
                // Decode ptr from f64 if needed
                LLVMValueRef obj_ptr = arr_tv.val;
                if (arr_tv.tag == JD_TAG_F64) {
                    LLVMValueRef as_i64 = pun_f64_to_i64(arr_tv.val);
                    obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
                } else if (arr_tv.tag == JD_TAG_I64) {
                    obj_ptr = LLVMBuildIntToPtr(builder, arr_tv.val, i8_ptr_type, "itoptr");
                }

                // Decide UDT vs native-map access. The punned ptr could be
                // a UDT instance or a JdbMap — same struct, different
                // registered getter names. Check both:
                //   v{"k"}       — v is a UDT variable
                //   arr[i]{"k"}  — arr[] has a UDT element type
                bool is_udt = false;
                if (expr.left && expr.left->kind == ExprKind::VARIABLE)
                    is_udt = var_udt_type.count(expr.left->str_val) > 0;
                else if (expr.left && expr.left->kind == ExprKind::INDEX &&
                         expr.left->left && expr.left->left->kind == ExprKind::VARIABLE)
                    is_udt = var_udt_type.count(expr.left->left->str_val + "[]") > 0;

                if (!is_udt) {
                    // Native map access (tag-4-like via punned f64).
                    if (want_ptr) {
                        auto& gobj = runtime_funcs["__map_get_obj"];
                        LLVMValueRef args[] = { obj_ptr, idx_tv.val };
                        return { LLVMBuildCall2(builder, gobj.fn_type, gobj.fn, args, 2, "mgetobj"), JD_TAG_NATIVE_MAP };
                    }
                    if (leaf_hint == 2) {
                        auto& gs = runtime_funcs["__map_get_str"];
                        LLVMValueRef args[] = { obj_ptr, idx_tv.val };
                        return { LLVMBuildCall2(builder, gs.fn_type, gs.fn, args, 2, "mget"), JD_TAG_STR };
                    }
                    if (leaf_hint == 0 || leaf_hint == 1) {
                        auto& gf = runtime_funcs["__map_get_f64"];
                        LLVMValueRef args[] = { obj_ptr, idx_tv.val };
                        return { LLVMBuildCall2(builder, gf.fn_type, gf.fn, args, 2, "mgetf"), JD_TAG_F64 };
                    }
                    // No hint → TAGGED getter preserves type info so
                    // PRINT and implicit string/number uses work without
                    // an explicit $-var round-trip (matches the tag=4
                    // and tag=6 default paths above).
                    auto& gtag = runtime_funcs["__map_get_tagged"];
                    LLVMValueRef out = LLVMBuildAlloca(builder, i64_type, "tv_out");
                    LLVMValueRef targs[] = { obj_ptr, idx_tv.val, out };
                    LLVMValueRef tv_tag = LLVMBuildCall2(builder, gtag.fn_type, gtag.fn, targs, 3, "mtag");
                    LLVMValueRef tv_val = LLVMBuildLoad2(builder, i64_type, out, "tv_val");
                    TypedValue r; r.val = tv_val; r.tag = JD_TAG_RUNTIME; r.runtime_tag = tv_tag; return r;
                }

                // UDT field access
                bool is_str_field = false;
                if (expr.right->kind == ExprKind::LITERAL_STRING) {
                    const std::string& fname = expr.right->str_val;
                    if (fname == "__TYPE__") is_str_field = true;
                    if (!fname.empty() && fname.back() == '$') is_str_field = true;
                    if (!is_str_field) {
                        for (auto& [tn, flds] : udt_types) {
                            for (auto& f : flds) {
                                if (f.name == fname) { is_str_field = f.is_string; break; }
                            }
                        }
                    }
                }
                if (is_str_field) {
                    auto& gs = runtime_funcs["__udt_get_str"];
                    LLVMValueRef args[] = { obj_ptr, idx_tv.val };
                    return { LLVMBuildCall2(builder, gs.fn_type, gs.fn, args, 2, "uget"), JD_TAG_STR };
                }
                auto& gf = runtime_funcs["__udt_get_f64"];
                LLVMValueRef args[] = { obj_ptr, idx_tv.val };
                return { LLVMBuildCall2(builder, gf.fn_type, gf.fn, args, 2, "ugetf"), JD_TAG_F64 };
            }

            // Int-indexed access on tag 7 via unified C dispatcher.
            if (arr_tv.tag == JD_TAG_RUNTIME && arr_tv.runtime_tag) {
                auto& ga = runtime_funcs["__jdrt_tagged_arr_get"];
                LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef idx = coerce_to(idx_tv, i64_type);
                LLVMValueRef out = LLVMBuildAlloca(builder, i64_type, "ia7out");
                LLVMValueRef targs[] = { rt, arr_tv.val, arr_tv.runtime_tag, idx, out };
                LLVMValueRef tv_tag = LLVMBuildCall2(builder, ga.fn_type, ga.fn, targs, 5, "ia7tag");
                LLVMValueRef tv_val = LLVMBuildLoad2(builder, i64_type, out, "ia7val");
                TypedValue r; r.val = tv_val; r.tag = JD_TAG_RUNTIME; r.runtime_tag = tv_tag;
                return r;
            }

            // Int-indexed access on a VM Value handle (lazy array/map element).
            if (arr_tv.tag == JD_TAG_VM_HANDLE) {
                LLVMValueRef handle = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, handle, "rt");
                LLVMValueRef idx = coerce_to(idx_tv, i64_type);
                auto& fn = runtime_funcs["__jdrt_val_arr_get"];
                LLVMValueRef args[] = { rt, arr_tv.val, idx };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "vget"), JD_TAG_VM_HANDLE };
            }

            // Get array pointer — may need to convert from encoded param
            LLVMValueRef arr_ptr = arr_tv.val;
            if (arr_tv.tag == JD_TAG_F64) {
                LLVMValueRef as_i64 = pun_f64_to_i64(arr_tv.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            } else if (arr_tv.tag == JD_TAG_I64) {
                arr_ptr = LLVMBuildIntToPtr(builder, arr_tv.val, i8_ptr_type, "itoptr");
            }

            // Fancy/vector indexing: `arr[indices_array]` returns a new
            // array with arr's elements gathered at the given positions
            // (APL/Numpy-style). Without this the codegen would pass a
            // ptr where __array_get expects an i64 — LLVM IR verifier
            // rejects the call. Dispatch to jdb_array_gather instead.
            if (idx_tv.tag == JD_TAG_ARR) {
                auto& gather = runtime_funcs["__array_gather"];
                LLVMValueRef gargs[] = { arr_ptr, idx_tv.val };
                LLVMValueRef gres = LLVMBuildCall2(builder, gather.fn_type, gather.fn, gargs, 2, "gather");
                return { gres, JD_TAG_ARR };
            }

            LLVMValueRef idx = idx_tv.val;
            if (idx_tv.tag == JD_TAG_F64)
                idx = LLVMBuildFPToSI(builder, idx, i64_type, "ftoi");
            else if (idx_tv.tag == JD_TAG_RUNTIME) {
                LLVMValueRef as_f64 = coerce_to(idx_tv, f64_type);
                idx = LLVMBuildFPToSI(builder, as_f64, i64_type, "rt_ftoi");
            }

            auto& arr_get = runtime_funcs["__array_get"];
            LLVMValueRef args[] = { arr_ptr, idx };
            LLVMValueRef result = LLVMBuildCall2(builder, arr_get.fn_type, arr_get.fn, args, 2, "elem");
            // Tagged-storage arrays (mixed_array_vars): per-cell JdTag
            // recovered via __arr_get_tagged, returned as RUNTIME so the
            // caller dispatches per element.
            if (expr.left && expr.left->kind == ExprKind::VARIABLE &&
                mixed_array_vars.count(expr.left->str_val)) {
                auto& gtg = runtime_funcs["__arr_get_tagged"];
                LLVMValueRef out_tag = LLVMBuildAlloca(builder, i32_type, "mix_gt_tag");
                LLVMValueRef getargs[] = { arr_ptr, idx, out_tag };
                LLVMValueRef val = LLVMBuildCall2(builder, gtg.fn_type, gtg.fn,
                    getargs, 3, "mix_gt");
                LLVMValueRef tag_v = LLVMBuildLoad2(builder, i32_type, out_tag, "mix_gt_tag");
                LLVMValueRef as_i64 = pun_f64_to_i64(val);
                return { as_i64, JD_TAG_RUNTIME, tag_v };
            }
            // If the source array is known to hold strings (e.g. event handler
            // data param), pun the f64-encoded ptr back to char* and tag as
            // string so concat / string-compare paths see it correctly.
            if (expr.left && expr.left->kind == ExprKind::VARIABLE &&
                string_array_vars.count(expr.left->str_val)) {
                LLVMValueRef as_i64 = pun_f64_to_i64(result);
                LLVMValueRef as_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "elem_s");
                return { as_ptr, JD_TAG_STR };
            }
            // Map-bearing array: decode the punned-f64 back to a JdbMap* and
            // return tag=4 so subsequent `q{"k"} = v` mutates the shared map.
            if (expr.left && expr.left->kind == ExprKind::VARIABLE &&
                map_array_vars.count(expr.left->str_val)) {
                LLVMValueRef as_i64 = pun_f64_to_i64(result);
                LLVMValueRef as_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "elem_m");
                return { as_ptr, JD_TAG_NATIVE_MAP };
            }
            // VM-handle-bearing array (PUSH'd a tag=6 or tag=7 element):
            // decode the punned-f64 back to an i64 handle and return tag=6
            // so MAP_ACCESS routes through __jdrt_obj_get_* / __jdrt_obj_exists
            // instead of treating the bits as a JdbMap*.
            if (expr.left && expr.left->kind == ExprKind::VARIABLE &&
                vm_array_vars.count(expr.left->str_val)) {
                LLVMValueRef as_i64 = pun_f64_to_i64(result);
                return { as_i64, JD_TAG_VM_HANDLE };
            }
            // Array-of-arrays (matrix cache pattern, e.g. glyph_cache[ch]
            // holding 8x7 RGB matrices). Pun the punned-f64 back to an
            // i8* so the outer caller (PLOTRAW, arr ops) sees an ARR.
            if (expr.left && expr.left->kind == ExprKind::VARIABLE &&
                array_array_vars.count(expr.left->str_val)) {
                LLVMValueRef as_i64 = pun_f64_to_i64(result);
                LLVMValueRef as_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "elem_a");
                return { as_ptr, JD_TAG_ARR };
            }
            // LHS type hint (e.g. `id$ = arr[i]` with $-suffixed target)
            // lets us decode the punned-f64 element without needing the
            // source array to be pre-tracked as string/map-bearing. Crucial
            // when the source is a FUNC param whose element type we didn't
            // propagate from the caller.
            if (m_want_leaf_tag == JD_TAG_STR) {
                LLVMValueRef as_i64 = pun_f64_to_i64(result);
                LLVMValueRef as_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "elem_h_s");
                return { as_ptr, JD_TAG_STR };
            }
            if (m_want_leaf_tag == JD_TAG_NATIVE_MAP) {
                LLVMValueRef as_i64 = pun_f64_to_i64(result);
                LLVMValueRef as_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "elem_h_m");
                return { as_ptr, JD_TAG_NATIVE_MAP };
            }
            // Plain numeric / untyped array: legacy f64 element.
            return { result, JD_TAG_F64 };
        }

        default:
            return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
    }
}

LLVMCodegen::TypedValue LLVMCodegen::codegen_binary(const Expr& expr) {
    // Handle AND/OR with short-circuit semantics (scalar only)
    // For arrays, fall through to the array arithmetic path below
    if (expr.op == TokenType::AND || expr.op == TokenType::OR ||
        expr.op == TokenType::ANDALSO || expr.op == TokenType::ORELSE) {
        // Quick check: if left operand is likely an array, skip short-circuit
        // We evaluate lhs first; if it's an array, use element-wise path
        TypedValue lhs_check = codegen_expr(*expr.left);
        if (lhs_check.tag == JD_TAG_ARR) {
            TypedValue rhs_check = codegen_expr(*expr.right);
            int32_t cmp_op = (expr.op == TokenType::AND || expr.op == TokenType::ANDALSO) ? 2 : 3;
            if (rhs_check.tag == JD_TAG_ARR) {
                auto& fn = runtime_funcs["__arr_cmp_arr"];
                LLVMValueRef args[] = { lhs_check.val, rhs_check.val, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), JD_TAG_ARR };
            } else {
                LLVMValueRef scalar = (rhs_check.tag == JD_TAG_I64 || rhs_check.tag == JD_TAG_BOOL)
                    ? LLVMBuildSIToFP(builder, rhs_check.val, f64_type, "itof") : rhs_check.val;
                auto& fn = runtime_funcs["__arr_cmp_scalar"];
                LLVMValueRef args[] = { lhs_check.val, scalar, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), JD_TAG_ARR };
            }
        }
        // Scalar short-circuit path (lhs already evaluated as lhs_check)
        {
        LLVMValueRef lhs_bool = to_i1(lhs_check);

        LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "logic.rhs");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "logic.merge");
        LLVMBasicBlockRef lhs_bb = LLVMGetInsertBlock(builder);

        if (expr.op == TokenType::AND || expr.op == TokenType::ANDALSO)
            LLVMBuildCondBr(builder, lhs_bool, rhs_bb, merge_bb);
        else  // OR / ORELSE
            LLVMBuildCondBr(builder, lhs_bool, merge_bb, rhs_bb);

        LLVMPositionBuilderAtEnd(builder, rhs_bb);
        TypedValue rhs = codegen_expr(*expr.right);
        LLVMValueRef rhs_bool = to_i1(rhs);
        LLVMValueRef rhs_i64 = LLVMBuildZExt(builder, rhs_bool, i64_type, "ext");
        LLVMBasicBlockRef rhs_end_bb = LLVMGetInsertBlock(builder);
        LLVMBuildBr(builder, merge_bb);

        LLVMPositionBuilderAtEnd(builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(builder, i64_type, "logic");
        LLVMValueRef incoming_vals[2];
        LLVMBasicBlockRef incoming_bbs[2];
        if (expr.op == TokenType::AND || expr.op == TokenType::ANDALSO) {
            incoming_vals[0] = LLVMConstInt(i64_type, 0, 0);
            incoming_bbs[0] = lhs_bb;
        } else {
            incoming_vals[0] = LLVMConstInt(i64_type, 1, 0);
            incoming_bbs[0] = lhs_bb;
        }
        incoming_vals[1] = rhs_i64;
        incoming_bbs[1] = rhs_end_bb;
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);

        return { phi, JD_TAG_I64 };
        } // end scalar short-circuit block
    }

    TypedValue lhs = codegen_expr(*expr.left);
    TypedValue rhs = codegen_expr(*expr.right);

    // VM_HANDLE materialisation for binary ops. A handle on either side
    // (e.g. a value pulled out of CHAN.RECV or a MAP-stored map field)
    // would otherwise feed the i64 catch-all below, which compares the
    // raw handle index instead of the underlying Value. Unwrap based on
    // the OTHER side's tag: STR vs anything-numeric.
    auto materialise_vmh = [&](TypedValue& tv, const TypedValue& other) {
        if (tv.tag != JD_TAG_VM_HANDLE) return;
        bool want_str = (other.tag == JD_TAG_STR);
        if (want_str) {
            auto* fn = get_runtime_func("__jdrt_val_to_str");
            if (!fn) return;
            LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
            LLVMValueRef args[] = { rt, tv.val };
            tv.val = LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 2, "vmh_s");
            tv.tag = JD_TAG_STR;
        } else {
            auto* fn = get_runtime_func("__jdrt_val_to_f64");
            if (!fn) return;
            LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
            LLVMValueRef args[] = { rt, tv.val };
            tv.val = LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 2, "vmh_f");
            tv.tag = JD_TAG_F64;
        }
    };
    materialise_vmh(lhs, rhs);
    materialise_vmh(rhs, lhs);

    // String concatenation: str + str, str + int, int + str, str + float, etc.
    // But NOT if one side is an array (tag=3) — that goes to array arithmetic
    if (expr.op == TokenType::PLUS && (lhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_STR) &&
        lhs.tag != JD_TAG_ARR && rhs.tag != JD_TAG_ARR) {
        // Convert non-string operand to string. Use to_string_ptr for
        // ptr/handle-typed operands (tag 3/4/6) so they materialise via
        // the appropriate bridge instead of bit-punning into an f64.
        auto to_str = [&](TypedValue tv) -> LLVMValueRef {
            if (tv.tag == JD_TAG_STR) return tv.val;
            if (tv.tag == JD_TAG_I64) {
                auto& fn = runtime_funcs["__int_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "itostr");
            }
            if (tv.tag == JD_TAG_F64) {
                auto& fn = runtime_funcs["__double_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "ftostr");
            }
            return to_string_ptr(tv);
        };
        auto& concat = runtime_funcs["__str_concat"];
        LLVMValueRef args[] = { to_str(lhs), to_str(rhs) };
        LLVMValueRef result = LLVMBuildCall2(builder, concat.fn_type, concat.fn, args, 2, "concat");
        return { result, JD_TAG_STR };
    }

    // String comparison with array element: arr[i] = "str" means arr[i]
    // is a ptr-encoded string. Decode the f64 back to ptr, then compare.
    if ((expr.op == TokenType::EQ || expr.op == TokenType::ASSIGN || expr.op == TokenType::NE) &&
        ((lhs.tag == JD_TAG_F64 && rhs.tag == JD_TAG_STR) || (lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_F64))) {
        // Decode the f64 side as ptr (it's likely a ptr-encoded string from an array)
        auto decode_ptr = [&](TypedValue tv) -> LLVMValueRef {
            if (tv.tag == JD_TAG_STR) return tv.val;
            LLVMValueRef as_i64 = pun_f64_to_i64(tv.val);
            return LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "ftoptr");
        };
        LLVMValueRef l = decode_ptr(lhs);
        LLVMValueRef r = decode_ptr(rhs);
        if (expr.op == TokenType::NE) {
            auto& fn = runtime_funcs["__str_ne"];
            LLVMValueRef args[] = { l, r };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "strne"), JD_TAG_BOOL };
        }
        auto& fn = runtime_funcs["__str_eq"];
        LLVMValueRef args[] = { l, r };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "streq"), JD_TAG_BOOL };
    }

    // String comparison: str = str, str <> str
    if ((lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_STR) &&
        (expr.op == TokenType::EQ || expr.op == TokenType::ASSIGN || expr.op == TokenType::NE)) {
        if (expr.op == TokenType::NE) {
            auto& fn = runtime_funcs["__str_ne"];
            LLVMValueRef args[] = { lhs.val, rhs.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "strne");
            return { result, JD_TAG_BOOL };
        } else {
            auto& fn = runtime_funcs["__str_eq"];
            LLVMValueRef args[] = { lhs.val, rhs.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "streq");
            return { result, JD_TAG_BOOL };
        }
    }

    // IN operator
    if (expr.op == TokenType::IN) {
        // String in string: substring search
        if (lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_STR) {
            auto& fn = runtime_funcs["INSTR"];
            LLVMValueRef args[] = { rhs.val, lhs.val };
            LLVMValueRef pos = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "instr");
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSGE, pos,
                                              LLVMConstInt(i64_type, 0, 0), "in");
            return { LLVMBuildZExt(builder, cmp, i64_type, "ext"), JD_TAG_I64 };
        }
        // String in map: key lookup
        if (lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_NATIVE_MAP) {
            auto& fn = runtime_funcs["__map_has"];
            LLVMValueRef args[] = { rhs.val, lhs.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "mhas"), JD_TAG_I64 };
        }
        // String in array: element-wise strcmp
        if (lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_ARR) {
            auto& fn = runtime_funcs["__arr_has_str"];
            LLVMValueRef args[] = { rhs.val, lhs.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "ahs"), JD_TAG_I64 };
        }
        // Number in array
        if ((lhs.tag == JD_TAG_I64 || lhs.tag == JD_TAG_F64) && rhs.tag == JD_TAG_ARR) {
            auto& fn = runtime_funcs["__arr_has_num"];
            LLVMValueRef num = coerce_to(lhs, f64_type);
            LLVMValueRef args[] = { rhs.val, num };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "ahn"), JD_TAG_I64 };
        }
        return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
    }

    // Power operator (^)
    if (expr.op == TokenType::CARET) {
        if (lhs.tag == JD_TAG_ARR || rhs.tag == JD_TAG_ARR) {
            const int32_t POW_OP = 10;
            if (lhs.tag == JD_TAG_ARR && rhs.tag == JD_TAG_ARR) {
                auto& fn = runtime_funcs["__arr_binop"];
                LLVMValueRef args[] = { lhs.val, rhs.val, LLVMConstInt(i32_type, POW_OP, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "apow"), JD_TAG_ARR };
            } else if (lhs.tag == JD_TAG_ARR) {
                LLVMValueRef scalar = rhs.tag == JD_TAG_I64
                    ? LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof") : rhs.val;
                auto& fn = runtime_funcs["__arr_scalar_op"];
                LLVMValueRef args[] = { lhs.val, scalar, LLVMConstInt(i32_type, POW_OP, 0),
                                         LLVMConstInt(i32_type, 0, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "aspow"), JD_TAG_ARR };
            } else {
                LLVMValueRef scalar = lhs.tag == JD_TAG_I64
                    ? LLVMBuildSIToFP(builder, lhs.val, f64_type, "itof") : lhs.val;
                auto& fn = runtime_funcs["__arr_scalar_op"];
                LLVMValueRef args[] = { rhs.val, scalar, LLVMConstInt(i32_type, POW_OP, 0),
                                         LLVMConstInt(i32_type, 1, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "aspow"), JD_TAG_ARR };
            }
        }
        lhs = promote_to_f64(lhs);
        rhs = promote_to_f64(rhs);
        auto& fn = runtime_funcs["__pow"];
        LLVMValueRef args[] = { lhs.val, rhs.val };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "pow");
        return { result, JD_TAG_F64 };
    }

    // Array + String or String + Array: native element-wise string concat
    if (expr.op == TokenType::PLUS &&
        ((lhs.tag == JD_TAG_ARR && rhs.tag == JD_TAG_STR) || (lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_ARR))) {
        bool scalar_left = (lhs.tag == JD_TAG_STR);
        LLVMValueRef arr_ptr = scalar_left ? rhs.val : lhs.val;
        LLVMValueRef str_ptr = scalar_left ? lhs.val : rhs.val;
        auto& fn = runtime_funcs["__arr_str_concat"];
        LLVMValueRef args[] = { arr_ptr, str_ptr, LLVMConstInt(i32_type, scalar_left ? 1 : 0, 0) };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "asc");
        return { result, JD_TAG_ARR };
    }

    // Array/ptr operands: use native array arithmetic functions
    if (lhs.tag == JD_TAG_ARR || rhs.tag == JD_TAG_ARR) {
        // Determine op code: 0=add, 1=sub, 2=mul, 3=div
        int32_t arith_op = -1;
        switch (expr.op) {
            case TokenType::PLUS:  arith_op = 0; break;
            case TokenType::MINUS: arith_op = 1; break;
            case TokenType::STAR:  arith_op = 2; break;
            case TokenType::SLASH: arith_op = 3; break;
            // Bitwise / shift — match the runtime's scalar_op codes 4-8
            // so element-wise `Masks BAND 1`, `IOTA(N,0) SHL 1` etc.
            // dispatch through the same array runtime as `+`/`-`.
            case TokenType::BAND:  arith_op = 4; break;
            case TokenType::BOR:   arith_op = 5; break;
            case TokenType::XOR:
            case TokenType::BXOR:  arith_op = 6; break;
            case TokenType::SHL:   arith_op = 7; break;
            case TokenType::SHR:   arith_op = 8; break;
            case TokenType::MOD:   arith_op = 9; break;
            default: break;
        }
        // Comparison ops: 0=eq, 1=ne, 2=and, 3=or, 4=lt, 5=le, 6=gt, 7=ge
        int32_t cmp_op = -1;
        switch (expr.op) {
            case TokenType::EQ:
            case TokenType::ASSIGN: cmp_op = 0; break;
            case TokenType::NE:     cmp_op = 1; break;
            case TokenType::AND:    cmp_op = 2; break;
            case TokenType::OR:     cmp_op = 3; break;
            case TokenType::LT:     cmp_op = 4; break;
            case TokenType::LE:     cmp_op = 5; break;
            case TokenType::GT:     cmp_op = 6; break;
            case TokenType::GE:     cmp_op = 7; break;
            default: break;
        }

        if (lhs.tag == JD_TAG_ARR && rhs.tag == JD_TAG_ARR) {
            // arr OP arr
            if (arith_op >= 0) {
                auto& fn = runtime_funcs["__arr_binop"];
                LLVMValueRef args[] = { lhs.val, rhs.val, LLVMConstInt(i32_type, arith_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "aop"), JD_TAG_ARR };
            }
            if (cmp_op >= 0) {
                auto& fn = runtime_funcs["__arr_cmp_arr"];
                LLVMValueRef args[] = { lhs.val, rhs.val, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), JD_TAG_ARR };
            }
        } else if (lhs.tag == JD_TAG_ARR) {
            // arr OP scalar — string-array-vs-string path takes precedence
            // when the lhs source is a known string array (the numeric
            // path would otherwise compare punned-pointer bits or, for
            // a JD_TAG_STR rhs, hand a ptr to a function expecting f64
            // and produce invalid IR).
            if (cmp_op >= 0 && rhs.tag == JD_TAG_STR && expr.left &&
                expr.left->kind == ExprKind::VARIABLE &&
                string_array_vars.count(expr.left->str_val)) {
                auto& fn = runtime_funcs["__arr_cmp_scalar_str"];
                LLVMValueRef args[] = { lhs.val, rhs.val,
                                        LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmps"), JD_TAG_ARR };
            }
            LLVMValueRef scalar = rhs.tag == JD_TAG_I64
                ? LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof") : rhs.val;
            if (arith_op >= 0) {
                auto& fn = runtime_funcs["__arr_scalar_op"];
                LLVMValueRef args[] = { lhs.val, scalar, LLVMConstInt(i32_type, arith_op, 0),
                                         LLVMConstInt(i32_type, 0, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "asop"), JD_TAG_ARR };
            }
            if (cmp_op >= 0) {
                auto& fn = runtime_funcs["__arr_cmp_scalar"];
                LLVMValueRef args[] = { lhs.val, scalar, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), JD_TAG_ARR };
            }
        } else {
            // scalar OP arr — same string-array dispatch with sides flipped.
            if (cmp_op >= 0 && lhs.tag == JD_TAG_STR && expr.right &&
                expr.right->kind == ExprKind::VARIABLE &&
                string_array_vars.count(expr.right->str_val)) {
                auto& fn = runtime_funcs["__arr_cmp_scalar_str"];
                LLVMValueRef args[] = { rhs.val, lhs.val,
                                        LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmps"), JD_TAG_ARR };
            }
            LLVMValueRef scalar = lhs.tag == JD_TAG_I64
                ? LLVMBuildSIToFP(builder, lhs.val, f64_type, "itof") : lhs.val;
            if (arith_op >= 0) {
                auto& fn = runtime_funcs["__arr_scalar_op"];
                LLVMValueRef args[] = { rhs.val, scalar, LLVMConstInt(i32_type, arith_op, 0),
                                         LLVMConstInt(i32_type, 1, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "asop"), JD_TAG_ARR };
            }
            if (cmp_op >= 0) {
                auto& fn = runtime_funcs["__arr_cmp_scalar"];
                LLVMValueRef args[] = { rhs.val, scalar, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), JD_TAG_ARR };
            }
        }
        // Fallback for unsupported array ops
        return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
    }

    // (String repeat is now handled below via native jdb_str_repeat)

    // String comparison: str = other, str <> other (when one side might not be str)
    if ((lhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_STR) &&
        (expr.op == TokenType::EQ || expr.op == TokenType::ASSIGN || expr.op == TokenType::NE)) {
        // Convert both to strings if needed
        auto to_str2 = [&](TypedValue tv) -> LLVMValueRef {
            if (tv.tag == JD_TAG_STR) return tv.val;
            if (tv.tag == JD_TAG_I64) {
                auto& fn = runtime_funcs["__int_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "itostr");
            }
            if (tv.tag == JD_TAG_F64) {
                auto& fn = runtime_funcs["__double_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "ftostr");
            }
            return to_string_ptr(tv);
        };
        if (expr.op == TokenType::NE) {
            auto& fn = runtime_funcs["__str_ne"];
            LLVMValueRef args[] = { to_str2(lhs), to_str2(rhs) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "strne"), JD_TAG_BOOL };
        } else {
            auto& fn = runtime_funcs["__str_eq"];
            LLVMValueRef args[] = { to_str2(lhs), to_str2(rhs) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "streq"), JD_TAG_BOOL };
        }
    }

    // String * int → repeat ("-" * 5 → "-----"), native fast path
    if (expr.op == TokenType::STAR && (lhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_STR)) {
        LLVMValueRef str_v = (lhs.tag == JD_TAG_STR) ? lhs.val : rhs.val;
        TypedValue n_tv = (lhs.tag == JD_TAG_STR) ? rhs : lhs;
        LLVMValueRef n = coerce_to(n_tv, i64_type);
        auto& fn = runtime_funcs["__str_repeat"];
        LLVMValueRef args[] = { str_v, n };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "rep"), JD_TAG_STR };
    }

    // String - String → remove all occurrences ("abcabc" - "bc" → "aa")
    if (expr.op == TokenType::MINUS && lhs.tag == JD_TAG_STR && rhs.tag == JD_TAG_STR) {
        auto& fn = runtime_funcs["__str_sub"];
        LLVMValueRef args[] = { lhs.val, rhs.val };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "ssub"), JD_TAG_STR };
    }

    // Tag 7 (runtime-tagged) must be materialised to f64 before arithmetic.
    if (lhs.tag == JD_TAG_RUNTIME) { lhs.val = coerce_to(lhs, f64_type); lhs.tag = JD_TAG_F64; }
    if (rhs.tag == JD_TAG_RUNTIME) { rhs.val = coerce_to(rhs, f64_type); rhs.tag = JD_TAG_F64; }
    bool use_float = (lhs.tag == JD_TAG_F64 || rhs.tag == JD_TAG_F64);
    // BASIC `/` is always float division (vs `\` which is integer);
    // `^` (power) also returns float even on integer inputs.
    if (expr.op == TokenType::SLASH || expr.op == TokenType::CARET) {
        use_float = true;
    }
    // Bitwise / shift ops are integer-only — coerce float operands
    // through an FPToSI so `1.0 SHL 2` matches the interpreter's
    // `to_int() << to_int()` semantics.
    if (expr.op == TokenType::SHL || expr.op == TokenType::SHR ||
        expr.op == TokenType::BAND || expr.op == TokenType::BOR ||
        expr.op == TokenType::XOR || expr.op == TokenType::BXOR) {
        if (lhs.tag == JD_TAG_F64) { lhs.val = LLVMBuildFPToSI(builder, lhs.val, i64_type, "ftoi"); lhs.tag = JD_TAG_I64; }
        if (rhs.tag == JD_TAG_F64) { rhs.val = LLVMBuildFPToSI(builder, rhs.val, i64_type, "ftoi"); rhs.tag = JD_TAG_I64; }
        use_float = false;
    }
    if (use_float) {
        lhs = promote_to_f64(lhs);
        rhs = promote_to_f64(rhs);
    } else {
        // Integer-branch path: coerce ptr-typed operands to i64 so ICmp /
        // BuildAdd etc. don't fail on mixed ptr↔int operands. This typically
        // arises when one side is a map/array (ptr) being compared to an
        // integer (e.g. `IF inv.GOLD > 0`).
        if (lhs.tag == JD_TAG_STR || lhs.tag == JD_TAG_ARR || lhs.tag == JD_TAG_NATIVE_MAP || lhs.tag == JD_TAG_FUNCREF)
            lhs.val = LLVMBuildPtrToInt(builder, lhs.val, i64_type, "lhsptoi");
        if (rhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_ARR || rhs.tag == JD_TAG_NATIVE_MAP || rhs.tag == JD_TAG_FUNCREF)
            rhs.val = LLVMBuildPtrToInt(builder, rhs.val, i64_type, "rhsptoi");
    }

    if (use_float) {
        switch (expr.op) {
            case TokenType::PLUS:  return { LLVMBuildFAdd(builder, lhs.val, rhs.val, "fadd"), JD_TAG_F64 };
            case TokenType::MINUS: return { LLVMBuildFSub(builder, lhs.val, rhs.val, "fsub"), JD_TAG_F64 };
            case TokenType::STAR:  return { LLVMBuildFMul(builder, lhs.val, rhs.val, "fmul"), JD_TAG_F64 };
            case TokenType::SLASH: emit_div_zero_check(rhs); return { LLVMBuildFDiv(builder, lhs.val, rhs.val, "fdiv"), JD_TAG_F64 };
            case TokenType::MOD:   emit_div_zero_check(rhs); return { LLVMBuildFRem(builder, lhs.val, rhs.val, "fmod"), JD_TAG_F64 };
            case TokenType::LT:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOLT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::GT:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOGT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::LE:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOLE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::GE:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOGE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::EQ:
            case TokenType::ASSIGN: return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOEQ, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::NE:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealONE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            default: break;
        }
    } else {
        switch (expr.op) {
            case TokenType::PLUS:      return { LLVMBuildAdd(builder, lhs.val, rhs.val, "add"), JD_TAG_I64 };
            case TokenType::MINUS:     return { LLVMBuildSub(builder, lhs.val, rhs.val, "sub"), JD_TAG_I64 };
            case TokenType::STAR:      return { LLVMBuildMul(builder, lhs.val, rhs.val, "mul"), JD_TAG_I64 };
            case TokenType::SLASH:     emit_div_zero_check(rhs); return { LLVMBuildSDiv(builder, lhs.val, rhs.val, "div"), JD_TAG_I64 };
            case TokenType::BACKSLASH: emit_div_zero_check(rhs); return { LLVMBuildSDiv(builder, lhs.val, rhs.val, "idiv"), JD_TAG_I64 };
            case TokenType::MOD:       emit_div_zero_check(rhs); return { LLVMBuildSRem(builder, lhs.val, rhs.val, "mod"), JD_TAG_I64 };
            case TokenType::LT:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSLT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::GT:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSGT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::LE:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSLE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::GE:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSGE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::EQ:
            case TokenType::ASSIGN:    return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntEQ, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::NE:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntNE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), JD_TAG_BOOL };
            case TokenType::BAND:      return { LLVMBuildAnd(builder, lhs.val, rhs.val, "band"), JD_TAG_I64 };
            case TokenType::BOR:       return { LLVMBuildOr(builder, lhs.val, rhs.val, "bor"), JD_TAG_I64 };
            case TokenType::XOR:
            case TokenType::BXOR:      return { LLVMBuildXor(builder, lhs.val, rhs.val, "bxor"), JD_TAG_I64 };
            case TokenType::SHL:       return { LLVMBuildShl(builder, lhs.val, rhs.val, "shl"), JD_TAG_I64 };
            // Arithmetic right shift (sign-preserving) — matches the
            // interpreter, which uses C++ `int64_t >> n` (impl-defined but
            // arithmetic on every mainstream compiler).
            case TokenType::SHR:       return { LLVMBuildAShr(builder, lhs.val, rhs.val, "shr"), JD_TAG_I64 };
            default: break;
        }
    }
    return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
}

LLVMCodegen::TypedValue LLVMCodegen::codegen_unary(const Expr& expr) {
    TypedValue operand = codegen_expr(*expr.right);
    if (expr.op == TokenType::MINUS) {
        if (operand.tag == JD_TAG_F64)
            return { LLVMBuildFNeg(builder, operand.val, "fneg"), JD_TAG_F64 };
        if (operand.tag == JD_TAG_ARR) {
            // Element-wise negate via arr * -1.0 (scalar_op code 2 = MUL).
            auto& fn = runtime_funcs["__arr_scalar_op"];
            LLVMValueRef m1 = LLVMConstReal(f64_type, -1.0);
            LLVMValueRef args[] = { operand.val, m1,
                                    LLVMConstInt(i32_type, 2, 0),
                                    LLVMConstInt(i32_type, 0, 0) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "neg_arr"),
                     JD_TAG_ARR };
        }
        return { LLVMBuildNeg(builder, operand.val, "neg"), JD_TAG_I64 };
    }
    if (expr.op == TokenType::NOT) {
        LLVMValueRef b = to_i1(operand);
        LLVMValueRef notb = LLVMBuildNot(builder, b, "not");
        return { LLVMBuildZExt(builder, notb, i64_type, "ext"), JD_TAG_BOOL };
    }
    if (expr.op == TokenType::BNOT) {
        if (operand.tag == JD_TAG_ARR) {
            // Element-wise: arr BXOR -1. Reuse the runtime scalar_op codes
            // (op 6 = XOR), pass scalar -1 as f64 like the binary path does.
            auto& fn = runtime_funcs["__arr_scalar_op"];
            LLVMValueRef neg1 = LLVMConstReal(f64_type, -1.0);
            LLVMValueRef args[] = { operand.val, neg1,
                                    LLVMConstInt(i32_type, 6, 0),
                                    LLVMConstInt(i32_type, 0, 0) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "bnot_arr"),
                     JD_TAG_ARR };
        }
        // Scalar: coerce to i64 then bit-flip.
        LLVMValueRef ival = (operand.tag == JD_TAG_F64)
            ? LLVMBuildFPToSI(builder, operand.val, i64_type, "ftoi")
            : operand.val;
        return { LLVMBuildNot(builder, ival, "bnot"), JD_TAG_I64 };
    }
    return operand;
}

// ── FUNCREF wrapper ─────────────────────────────────────────
//
// HOFs like SELECT/FILTER/REDUCE in jdb_runtime expect a JdbMapFn,
// i.e. `double(double)` (or `double(double,double)` for binary). User
// FUNCs may have differently-tagged params/returns (i64/bool), which
// LLVM cannot freely cast to the expected signature. Instead we
// generate a thin trampoline that forwards the call with proper
// SIToFP/FPToSI coercions. Cached per (name, arity) so each FUNC
// only gets one wrapper per .exe.
LLVMValueRef LLVMCodegen::build_funcref_wrapper(const std::string& fn_name, int arity) {
    std::string key = fn_name + "/" + std::to_string(arity);
    auto cached = funcref_wrappers.find(key);
    if (cached != funcref_wrappers.end()) return cached->second;

    auto fit = user_functions.find(fn_name);
    if (fit == user_functions.end()) return nullptr;
    auto& fi = fit->second;
    if ((int)fi.param_tags.size() < arity) return nullptr;

    std::vector<LLVMTypeRef> wrap_params(arity, f64_type);
    LLVMTypeRef wrap_ty = LLVMFunctionType(f64_type,
        wrap_params.empty() ? nullptr : wrap_params.data(), arity, 0);

    std::string wrap_name = "__funcref_" + fn_name + "_" + std::to_string(arity);
    for (auto& c : wrap_name) if (c == '.' || c == '$' || c == '@') c = '_';
    LLVMValueRef wrap_fn = LLVMAddFunction(module, wrap_name.c_str(), wrap_ty);

    LLVMValueRef saved_fn = current_fn;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(builder);

    current_fn = wrap_fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, wrap_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    std::vector<LLVMValueRef> inner_args;
    for (int i = 0; i < arity; i++) {
        LLVMValueRef p = LLVMGetParam(wrap_fn, i);
        int expected = fi.param_tags[i];
        // Param signature mirrors codegen_call: ptr-shaped tags get i8*,
        // i64 stays as i64, everything else passes as f64.
        if (expected == JD_TAG_I64 || expected == JD_TAG_BOOL) {
            p = LLVMBuildFPToSI(builder, p, i64_type, "ftoi");
        } else if (expected == JD_TAG_STR || expected == JD_TAG_ARR ||
                   expected == JD_TAG_NATIVE_MAP) {
            // Pun the f64 bits back to a pointer. HOF callers don't pass
            // pointer args today; this is a defensive coercion.
            LLVMValueRef as_i = pun_f64_to_i64(p);
            p = LLVMBuildIntToPtr(builder, as_i, i8_ptr_type, "itoptr");
        }
        inner_args.push_back(p);
    }

    LLVMTypeRef inner_ty = LLVMGlobalGetValueType(fi.fn);
    LLVMValueRef call = LLVMBuildCall2(builder, inner_ty, fi.fn,
        inner_args.empty() ? nullptr : inner_args.data(),
        (unsigned)inner_args.size(),
        fi.return_tag == -1 ? "" : "wcall");

    LLVMValueRef ret_val;
    if (fi.return_tag == -1) {
        ret_val = LLVMConstReal(f64_type, 0.0);
    } else if (fi.return_tag == JD_TAG_I64 || fi.return_tag == JD_TAG_BOOL) {
        ret_val = LLVMBuildSIToFP(builder, call, f64_type, "itof");
    } else if (fi.return_tag == JD_TAG_STR || fi.return_tag == JD_TAG_ARR ||
               fi.return_tag == JD_TAG_NATIVE_MAP) {
        // Pun ptr → i64 → f64 so the value survives transit.
        LLVMValueRef as_i = LLVMBuildPtrToInt(builder, call, i64_type, "ptoi");
        ret_val = pun_i64_to_f64(as_i);
    } else {
        ret_val = call;
    }
    LLVMBuildRet(builder, ret_val);

    current_fn = saved_fn;
    LLVMPositionBuilderAtEnd(builder, saved_bb);

    funcref_wrappers[key] = wrap_fn;
    return wrap_fn;
}

// ── CALL ────────────────────────────────────────────────────

LLVMCodegen::TypedValue LLVMCodegen::codegen_call(const Expr& expr) {
    std::string name = expr.func_name;

    // Phase 1: Channels are interp-only. The runtime data structure
    // (mutex + condvars + std::deque<Value>) lives behind native bridge
    // calls we haven't shaped yet, and it's not worth the API surface
    // until ASYNC FUNC is shippable in native too. Fail clear and early.
    // CHAN.* / FILE.* streaming primitives + AI.CHAT_TOKENS used to be
    // rejected here in Phase 1 — they now route through the generic VM-
    // bridge dispatch (jdrt_call_typed_*), so the runtime DLL handles
    // them via its in-process VM exactly like every other native. The
    // return-type classification sets below (object_returners for
    // CHAN.RECV, the trailing-$ heuristic for FILE.READLINE$, etc.) pick
    // the right __jdrt_call_typed_* variant.

    // UNIQUE on a string-tracked array dedupes by strcmp instead of by
    // raw double bits (which only catches pointer-identity duplicates).
    // Route to the dedicated runtime when the source var is known to be
    // a string array; otherwise fall through to the generic dispatch.
    if (name == "UNIQUE" && expr.args.size() == 1 && expr.args[0] &&
        expr.args[0]->kind == ExprKind::VARIABLE &&
        string_array_vars.count(expr.args[0]->str_val)) {
        TypedValue a = codegen_expr(*expr.args[0]);
        auto& fn = runtime_funcs["__unique_str"];
        LLVMValueRef args[] = { a.val };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "uniqs"), JD_TAG_ARR };
    }

    // APPEND(arr, arr) flattens; APPEND(arr, scalar) appends one element.
    if (name == "APPEND" && expr.args.size() == 2) {
        TypedValue a = codegen_expr(*expr.args[0]);
        TypedValue b = codegen_expr(*expr.args[1]);
        if (a.tag == JD_TAG_ARR && b.tag == JD_TAG_ARR) {
            auto& fn = runtime_funcs["__append_arr"];
            LLVMValueRef args[] = { a.val, b.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "appa"), JD_TAG_ARR };
        }
        if (a.tag == JD_TAG_ARR) {
            LLVMValueRef bf = b.tag == JD_TAG_I64
                ? LLVMBuildSIToFP(builder, b.val, f64_type, "itof")
                : (b.tag == JD_TAG_F64 ? b.val : coerce_to(b, f64_type));
            auto& fn = runtime_funcs["APPEND"];
            LLVMValueRef args[] = { a.val, bf };
            LLVMValueRef appended = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "app");
            // A string scalar is stored bit-punned but UNTAGGED by jdb_array_append.
            // Mark the result's elements as strings so the tag survives a pass
            // through the VM bridge — e.g. TUI.MENU doing arr->elements[i].as_string()
            // reads blank otherwise. Mirrors the ARRAY_LITERAL string path.
            if (b.tag == JD_TAG_STR) {
                auto* set_str = get_runtime_func("__arr_set_string_elems");
                if (set_str) {
                    LLVMValueRef ss[] = { appended };
                    LLVMBuildCall2(builder, set_str->fn_type, set_str->fn, ss, 1, "");
                }
            }
            return { appended, JD_TAG_ARR };
        }
    }

    // ZEROS([N]) / ZEROS([N, M]) used as an *expression* (LET assignment,
    // call argument, etc.) — codegen_dim has a special 2D path that
    // builds a real nested array, but in expression context the call
    // would otherwise fall through to runtime jdb_zeros which expects a
    // flat i64 size and reads the array-literal pointer as the size,
    // crashing as soon as the result is dereferenced. Handle the 1-D
    // and 2-D shapes here so `glyph = ZEROS([8, 7])` and `cache[i] =
    // ZEROS([2, 3])` build the proper structure in native, matching the
    // interpreter.
    if ((name == "ZEROS" || name == "ONES") && expr.args.size() == 1 &&
        expr.args[0] && expr.args[0]->kind == ExprKind::ARRAY_LITERAL) {
        auto& shape_args = expr.args[0]->args;
        auto& arr_new = runtime_funcs["__array_new"];
        bool is_ones = (name == "ONES");
        auto& arr_ones_fn = runtime_funcs["ONES"];

        if (shape_args.size() == 1) {
            TypedValue size_val = codegen_expr(*shape_args[0]);
            LLVMValueRef size_i64 = size_val.tag == JD_TAG_F64
                ? LLVMBuildFPToSI(builder, size_val.val, i64_type, "ftoi") : size_val.val;
            LLVMValueRef args[] = { size_i64 };
            LLVMValueRef arr;
            if (is_ones) {
                arr = LLVMBuildCall2(builder, arr_ones_fn.fn_type, arr_ones_fn.fn, args, 1, "arr1d");
            } else {
                arr = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, args, 1, "arr1d");
            }
            return { arr, JD_TAG_ARR };
        }
        if (shape_args.size() == 2) {
            auto& arr_append = runtime_funcs["APPEND"];
            TypedValue rows_val = codegen_expr(*shape_args[0]);
            TypedValue cols_val = codegen_expr(*shape_args[1]);
            LLVMValueRef rows = rows_val.tag == JD_TAG_F64
                ? LLVMBuildFPToSI(builder, rows_val.val, i64_type, "ftoi") : rows_val.val;
            LLVMValueRef cols = cols_val.tag == JD_TAG_F64
                ? LLVMBuildFPToSI(builder, cols_val.val, i64_type, "ftoi") : cols_val.val;

            LLVMValueRef zero_args[] = { LLVMConstInt(i64_type, 0, 0) };
            LLVMValueRef outer = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, zero_args, 1, "outer2d");

            LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "z2d.loop");
            LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "z2d.end");
            LLVMValueRef idx_alloca   = LLVMBuildAlloca(builder, i64_type,    "z2d_i");
            LLVMValueRef outer_alloca = LLVMBuildAlloca(builder, i8_ptr_type, "z2d_outer");
            LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), idx_alloca);
            LLVMBuildStore(builder, outer, outer_alloca);
            LLVMBuildBr(builder, loop_bb);

            LLVMPositionBuilderAtEnd(builder, loop_bb);
            LLVMValueRef cur_idx = LLVMBuildLoad2(builder, i64_type, idx_alloca, "i");
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, cur_idx, rows, "cmp");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "z2d.body");
            LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(builder, body_bb);
            LLVMValueRef inner_args[] = { cols };
            LLVMValueRef inner;
            if (is_ones) {
                inner = LLVMBuildCall2(builder, arr_ones_fn.fn_type, arr_ones_fn.fn, inner_args, 1, "inner");
            } else {
                inner = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, inner_args, 1, "inner");
            }
            LLVMValueRef inner_i64 = LLVMBuildPtrToInt(builder, inner, i64_type, "ptoi");
            LLVMValueRef inner_f64 = pun_i64_to_f64(inner_i64);
            LLVMValueRef cur_outer = LLVMBuildLoad2(builder, i8_ptr_type, outer_alloca, "out");
            LLVMValueRef append_args[] = { cur_outer, inner_f64 };
            LLVMValueRef new_outer = LLVMBuildCall2(builder, arr_append.fn_type, arr_append.fn, append_args, 2, "out");
            LLVMBuildStore(builder, new_outer, outer_alloca);
            LLVMValueRef next_idx = LLVMBuildAdd(builder, cur_idx, LLVMConstInt(i64_type, 1, 0), "next");
            LLVMBuildStore(builder, next_idx, idx_alloca);
            LLVMBuildBr(builder, loop_bb);

            LLVMPositionBuilderAtEnd(builder, end_bb);
            LLVMValueRef final_outer = LLVMBuildLoad2(builder, i8_ptr_type, outer_alloca, "arr2d");
            auto& set_nested = runtime_funcs["__arr_set_nested"];
            LLVMValueRef sn_args[] = { final_outer };
            LLVMBuildCall2(builder, set_nested.fn_type, set_nested.fn, sn_args, 1, "");
            return { final_outer, JD_TAG_ARR };
        }
    }

    // Handle __METHOD__ calls: obj.method() or obj.method(args)
    if (name == "__METHOD__" && expr.left && expr.left->kind == ExprKind::MEMBER_ACCESS) {
        auto& member = *expr.left;
        std::string method_name = member.str_val;
        // Uppercase for matching
        std::string upper_method = method_name;
        std::transform(upper_method.begin(), upper_method.end(), upper_method.begin(), ::toupper);

        // Evaluate the object expression
        TypedValue obj = codegen_expr(*member.left);
        LLVMValueRef obj_ptr = obj.val;
        // Decode ptr from f64/i64 if needed (e.g. array element)
        if (obj.tag == JD_TAG_F64) {
            LLVMValueRef as_i64 = pun_f64_to_i64(obj.val);
            obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
        } else if (obj.tag == JD_TAG_I64) {
            obj_ptr = LLVMBuildIntToPtr(builder, obj.val, i8_ptr_type, "itoptr");
        }

        // Try to resolve the UDT type from the variable
        std::string type_name;
        if (member.left->kind == ExprKind::VARIABLE) {
            auto tit = var_udt_type.find(member.left->str_val);
            if (tit != var_udt_type.end()) type_name = tit->second;
        }
        // Fallback: search all UDT types for a method with this name
        if (type_name.empty()) {
            for (auto& [tn, flds] : udt_types) {
                if (user_functions.count(tn + "." + upper_method)) {
                    type_name = tn;
                    break;
                }
            }
        }

        if (!type_name.empty()) {
            std::string full_name = type_name + "." + upper_method;
            auto fit = user_functions.find(full_name);
            if (fit != user_functions.end()) {
                auto& fi = fit->second;
                std::vector<LLVMValueRef> args;
                args.push_back(obj_ptr); // THIS (param 0)
                for (size_t ai = 0; ai < expr.args.size(); ai++) {
                    TypedValue av = codegen_expr(*expr.args[ai]);
                    int expected = (ai + 1 < fi.param_tags.size()) ? fi.param_tags[ai + 1] : 1;
                    LLVMTypeRef pt = (expected == 2 || expected == 3 || expected == 4) ? i8_ptr_type :
                                     (expected == 6) ? i64_type : f64_type;
                    args.push_back(coerce_to(av, pt));
                }
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
                if (fi.return_tag == -1) {
                    LLVMBuildCall2(builder, fn_type, fi.fn,
                                   args.data(), (unsigned)args.size(), "");
                    return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
                } else {
                    LLVMValueRef result = LLVMBuildCall2(builder, fn_type, fi.fn,
                                                          args.data(), (unsigned)args.size(), "mcall");
                    return { result, fi.return_tag };
                }
            }
        }
        // Fall through to VM bridge if method not found natively
    }

    // Handle dotted calls like PLAYER1.INIT → resolve to T_CHARACTER.INIT
    {
        size_t dot_pos = name.find('.');
        if (dot_pos != std::string::npos && name != "__METHOD__") {
            std::string var_part = name.substr(0, dot_pos);
            std::string method_part = name.substr(dot_pos + 1);
            auto tit = var_udt_type.find(var_part);
            if (tit != var_udt_type.end()) {
                std::string resolved = tit->second + "." + method_part;
                auto fit = user_functions.find(resolved);
                if (fit != user_functions.end()) {
                    // Load the object and pass as THIS
                    VarInfo* vi = lookup_var(var_part);
                    if (vi) {
                        LLVMValueRef obj_ptr = LLVMBuildLoad2(builder, i8_ptr_type,
                                                               vi->alloca_val, "this");
                        auto& fi = fit->second;
                        std::vector<LLVMValueRef> args;
                        args.push_back(obj_ptr); // THIS (param 0)
                        for (size_t ai = 0; ai < expr.args.size(); ai++) {
                            TypedValue av = codegen_expr(*expr.args[ai]);
                            int expected = (ai + 1 < fi.param_tags.size()) ? fi.param_tags[ai + 1] : 1;
                            LLVMTypeRef pt = (expected == 2 || expected == 3 || expected == 4) ? i8_ptr_type :
                                             (expected == 6) ? i64_type : f64_type;
                            args.push_back(coerce_to(av, pt));
                        }
                        LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
                        if (fi.return_tag == -1) {
                            LLVMBuildCall2(builder, fn_type, fi.fn,
                                           args.data(), (unsigned)args.size(), "");
                            return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
                        } else {
                            LLVMValueRef result = LLVMBuildCall2(builder, fn_type, fit->second.fn,
                                                                  args.data(), (unsigned)args.size(), "mcall");
                            return { result, fit->second.return_tag };
                        }
                    }
                }
            }
        }
    }

    // Indirect call: name refers to a variable holding a lambda/funcref.
    // Lambdas and `name@` funcrefs live in vars with tag=5 (ptr storage);
    // a FUNC parameter without type info is tag=1 (f64) and we pun its
    // bits back to a ptr. Uniform signature is (double, double, ...) → double.
    if (!user_functions.count(name) && !runtime_funcs.count(name)) {
        VarInfo* vi_fn = lookup_var(name);
        if (vi_fn && (vi_fn->tag == JD_TAG_FUNCREF || vi_fn->tag == JD_TAG_F64)) {
            LLVMValueRef fn_ptr;
            if (vi_fn->tag == JD_TAG_FUNCREF) {
                fn_ptr = LLVMBuildLoad2(builder, i8_ptr_type,
                                         vi_fn->alloca_val, name.c_str());
            } else {
                LLVMValueRef f64v = LLVMBuildLoad2(builder, f64_type,
                                                    vi_fn->alloca_val, name.c_str());
                LLVMValueRef i64v = pun_f64_to_i64(f64v);
                fn_ptr = LLVMBuildIntToPtr(builder, i64v, i8_ptr_type, "fn_as_ptr");
            }
            std::vector<LLVMValueRef> args;
            std::vector<LLVMTypeRef> arg_types;
            for (auto& a : expr.args) {
                TypedValue av = codegen_expr(*a);
                args.push_back(coerce_to(av, f64_type));
                arg_types.push_back(f64_type);
            }
            LLVMTypeRef fn_ty = LLVMFunctionType(f64_type,
                arg_types.empty() ? nullptr : arg_types.data(),
                (unsigned)arg_types.size(), 0);
            LLVMValueRef result = LLVMBuildCall2(builder, fn_ty, fn_ptr,
                args.empty() ? nullptr : args.data(),
                (unsigned)args.size(), "icall");
            return { result, JD_TAG_F64 };
        }
    }

    // 1. Try user-defined function
    auto uit = user_functions.find(name);
    if (uit != user_functions.end()) {
        auto& fi = uit->second;

        // ASYNC FUNC: spawn a thread via the runtime helper. We can't
        // emit a direct LLVMBuildCall — that runs the body inline in
        // the caller's thread. Instead, build/get a uniform funcref
        // wrapper (f64 args + f64 return) and pass its pointer to
        // __jdrt_async_spawn along with a packed args array; the
        // helper detaches a std::thread that invokes the wrapper and
        // registers the result in g_async_tasks for later AWAIT.
        if (fi.is_async) {
            int arity = (int)expr.args.size();
            LLVMValueRef wrapper = build_funcref_wrapper(name, arity);
            auto* spawn = get_runtime_func("__jdrt_async_spawn");
            if (wrapper && spawn) {
                // Coerce every arg to f64; pun ptr-typed args through
                // pun_i64_to_f64 so the wrapper can decode them via the
                // existing param-tag-aware dispatch.
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(builder);
                LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(current_fn);
                LLVMValueRef first = LLVMGetFirstInstruction(entry_bb);
                if (first) LLVMPositionBuilderBefore(builder, first);
                else       LLVMPositionBuilderAtEnd(builder, entry_bb);
                LLVMValueRef args_arr = arity > 0
                    ? LLVMBuildArrayAlloca(builder, f64_type,
                          LLVMConstInt(i32_type, arity, 0), "async_args")
                    : LLVMConstNull(i8_ptr_type);
                LLVMPositionBuilderAtEnd(builder, cur_bb);

                for (int i = 0; i < arity; i++) {
                    TypedValue av = codegen_expr(*expr.args[i]);
                    LLVMValueRef as_f64;
                    if (av.tag == JD_TAG_F64) {
                        as_f64 = av.val;
                    } else if (av.tag == JD_TAG_I64 || av.tag == JD_TAG_BOOL) {
                        as_f64 = LLVMBuildSIToFP(builder, av.val, f64_type, "i2f");
                    } else if (av.tag == JD_TAG_STR || av.tag == JD_TAG_ARR ||
                               av.tag == JD_TAG_NATIVE_MAP || av.tag == JD_TAG_FUNCREF) {
                        LLVMValueRef as_i = LLVMBuildPtrToInt(builder, av.val, i64_type, "p2i");
                        as_f64 = pun_i64_to_f64(as_i);
                    } else {
                        as_f64 = coerce_to(av, f64_type);
                    }
                    LLVMValueRef idx[] = { LLVMConstInt(i32_type, i, 0) };
                    LLVMValueRef slot = LLVMBuildGEP2(builder, f64_type, args_arr, idx, 1, "as");
                    LLVMBuildStore(builder, as_f64, slot);
                }

                LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef fn_ptr = LLVMBuildBitCast(builder, wrapper, i8_ptr_type, "f2p");
                LLVMValueRef args_ptr = arity > 0
                    ? LLVMBuildBitCast(builder, args_arr, i8_ptr_type, "args_p")
                    : LLVMConstNull(i8_ptr_type);
                LLVMValueRef call_args[] = {
                    rt, fn_ptr, args_ptr,
                    LLVMConstInt(i32_type, arity, 0),
                    LLVMConstInt(i32_type, fi.return_tag, 0)
                };
                LLVMValueRef task_id = LLVMBuildCall2(builder, spawn->fn_type, spawn->fn,
                                                      call_args, 5, "async_id");
                return { task_id, JD_TAG_I64 };
            }
            // No wrapper / no helper — fall through to the synchronous
            // call below as a degraded best-effort.
        }

        std::vector<LLVMValueRef> args;
        for (size_t i = 0; i < expr.args.size(); i++) {
            int expected_tag = (i < fi.param_tags.size()) ? fi.param_tags[i] : 1;
            TypedValue av;
            // Funcref-literal arg (`name@`) → build a wrapper trampoline
            // and pass the LLVM function pointer instead of the string
            // name. The callee's `fn(...)` indirect call needs an actual
            // fn-ptr; passing the string would route through the VM
            // bridge and die with "Undefined function: FN".
            //
            // Arity defaults to 1 (the common HOF shape: fn(elem)). If
            // the user function takes a 2-arg funcref, callers can
            // declare AS FUNC and the wrapper signature will match
            // through the param's declared type — for now arity 1
            // covers MAP-style HOFs which is what people write.
            if (expected_tag == JD_TAG_FUNCREF && expr.args[i] &&
                expr.args[i]->kind == ExprKind::LITERAL_STRING &&
                expr.args[i]->is_funcref_lit) {
                int arity = 1;
                LLVMValueRef wrap = build_funcref_wrapper(expr.args[i]->str_val, arity);
                if (wrap) {
                    args.push_back(wrap);
                    continue;
                }
            }
            // Scope the leaf-type hint to the param's expected type so an
            // outer assignment context (e.g. `out$ = ... + q$(params[i])`)
            // can't leak its STR hint into the inner index codegen.
            // F64/I64 params get NO hint — propagating would force the
            // map_get_f64 fast-path on map-stored strings (and break a
            // BINARY-EQ comparison passed in as the arg, since the EQ
            // result was being f64-pun'd through the string slot).
            // RUNTIME-typed param: also no hint — we want the arg
            // codegen to return a RUNTIME-tagged TypedValue with its
            // real per-cell tag preserved.
            int arg_leaf_hint = (expected_tag == JD_TAG_STR) ? JD_TAG_STR : -1;
            {
                ScopedLeafTag _lt(this, arg_leaf_hint);
                av = codegen_expr(*expr.args[i]);
            }
            if (expected_tag == JD_TAG_RUNTIME) {
                // Tag-aware ABI: pack (i64 val, i32 tag) for this param.
                // Strings / arrays / handles get pun'd to i64; numeric
                // values keep their f64 bits but are bit-cast to i64 so
                // both legs of the i64 alloca see uniform-shaped bits.
                LLVMValueRef val_i64;
                LLVMValueRef tag_i32;
                if (av.tag == JD_TAG_RUNTIME && av.runtime_tag) {
                    val_i64 = av.val;  // already i64
                    tag_i32 = av.runtime_tag;
                } else if (av.tag == JD_TAG_STR || av.tag == JD_TAG_ARR ||
                           av.tag == JD_TAG_NATIVE_MAP || av.tag == JD_TAG_FUNCREF) {
                    val_i64 = LLVMBuildPtrToInt(builder, av.val, i64_type, "av_pti");
                    tag_i32 = LLVMConstInt(i32_type, av.tag, 0);
                } else if (av.tag == JD_TAG_F64) {
                    val_i64 = pun_f64_to_i64(av.val);
                    tag_i32 = LLVMConstInt(i32_type, JD_TAG_F64, 0);
                } else if (av.tag == JD_TAG_BOOL) {
                    val_i64 = LLVMBuildSExt(builder, av.val, i64_type, "av_b2i");
                    tag_i32 = LLVMConstInt(i32_type, JD_TAG_BOOL, 0);
                } else {  // I64 / VM_HANDLE / unknown — pass raw bits
                    val_i64 = av.val;
                    tag_i32 = LLVMConstInt(i32_type, av.tag, 0);
                }
                args.push_back(val_i64);
                args.push_back(tag_i32);
                continue;
            }
            LLVMTypeRef pt = (expected_tag == 2 || expected_tag == 3 || expected_tag == 4 ||
                              expected_tag == 5) ? i8_ptr_type :
                             (expected_tag == 6) ? i64_type : f64_type;
            args.push_back(coerce_to(av, pt));
        }
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
        if (fi.return_tag == -1) {
            LLVMBuildCall2(builder, fn_type, fi.fn,
                           args.empty() ? nullptr : args.data(),
                           (unsigned)args.size(), "");
            return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
        } else {
            LLVMValueRef result = LLVMBuildCall2(builder, fn_type, fi.fn,
                                                  args.empty() ? nullptr : args.data(),
                                                  (unsigned)args.size(), "call");
            return { result, fi.return_tag };
        }
    }

    // 2. Handle FORMAT$ specially (variable number of args)
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // STR$(arg) — tag-aware. Default `jdb_str(double)` punnes strings
    // and prints booleans as 1/0; route to specific helpers when the
    // input tag is known.
    if (upper == "STR$" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_BOOL) {
            auto& fn = runtime_funcs["__str_bool"];
            LLVMValueRef args[] = { av.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "sb"), JD_TAG_STR };
        }
        if (av.tag == JD_TAG_STR) {
            auto& fn = runtime_funcs["__str_str"];
            LLVMValueRef args[] = { av.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "ss"), JD_TAG_STR };
        }
        if (av.tag == JD_TAG_ARR) {
            // Element-wise stringify, matches interpreter STR$(array).
            auto& fn = runtime_funcs["__str_arr"];
            LLVMValueRef args[] = { av.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "sarr"), JD_TAG_ARR };
        }
        // Numeric path: use existing jdb_str(double).
        auto& fn = runtime_funcs["STR$"];
        LLVMValueRef arg = coerce_to(av, f64_type);
        LLVMValueRef args[] = { arg };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "snum"), JD_TAG_STR };
    }

    if (upper == "FORMAT$" && expr.args.size() >= 2) {
        // FORMAT$(fmt_string, arg1, [arg2, [arg3, [arg4]]])
        TypedValue fmt_tv = codegen_expr(*expr.args[0]);
        int nargs = (int)expr.args.size() - 1;
        if (nargs > 4) nargs = 4;

        // Pre-evaluate args; route to __formatN_t when any arg's type can't
        // be reduced to a plain f64 at codegen time. Three triggers:
        //   - JD_TAG_STR        — direct string ptr, runtime needs to know
        //   - JD_TAG_VM_HANDLE  — pass raw key, runtime materialises per spec
        //   - JD_TAG_RUNTIME    — value type is decided at runtime by the
        //                         tagged map-getter; we materialise to STRING
        //                         at codegen time via coerce_to(i8_ptr_type),
        //                         which dispatches on the runtime tag.
        // RUNTIME-tagged numeric values with `{:.Nf}` go through the string
        // path (no direct f64 leg). Code that needs full precision should
        // DIM ... AS DOUBLE so the value lands on the JD_TAG_F64 fast path.
        std::vector<TypedValue> evald;
        evald.reserve(nargs);
        bool needs_tagged = false;
        for (int i = 0; i < nargs; i++) {
            TypedValue av = codegen_expr(*expr.args[i + 1]);
            if (av.tag == JD_TAG_STR ||
                av.tag == JD_TAG_VM_HANDLE ||
                av.tag == JD_TAG_RUNTIME)
                needs_tagged = true;
            evald.push_back(av);
        }

        if (needs_tagged) {
            std::string fn_name = "__format" + std::to_string(nargs) + "_t";
            auto fit3 = runtime_funcs.find(fn_name);
            if (fit3 != runtime_funcs.end()) {
                // Types buffer is now stack-allocated so RUNTIME-tagged args
                // can write either 's' or 'd' at runtime based on their
                // dispatching tag — a const string literal can't express
                // that. The buffer is nargs+1 bytes (+1 for NUL terminator).
                LLVMTypeRef i8_ty = LLVMInt8TypeInContext(ctx);
                LLVMTypeRef tbuf_ty = LLVMArrayType(i8_ty, nargs + 1);
                LLVMValueRef tbuf = LLVMBuildAlloca(builder, tbuf_ty, "fmt_tbuf");
                auto tbuf_slot = [&](int i) -> LLVMValueRef {
                    LLVMValueRef gep[] = {
                        LLVMConstInt(i32_type, 0, 0),
                        LLVMConstInt(i32_type, i, 0)
                    };
                    return LLVMBuildInBoundsGEP2(builder, tbuf_ty, tbuf, gep, 2, "tb_i");
                };
                auto store_tag = [&](int i, char c) {
                    LLVMBuildStore(builder,
                        LLVMConstInt(i8_ty, (uint64_t)(unsigned char)c, 0),
                        tbuf_slot(i));
                };

                std::vector<LLVMValueRef> arg_raws(nargs, nullptr);
                for (int i = 0; i < nargs; i++) {
                    auto& av = evald[i];
                    if (av.tag == JD_TAG_STR) {
                        store_tag(i, 's');
                        arg_raws[i] = LLVMBuildPtrToInt(builder, av.val, i64_type, "ptoi");
                    } else if (av.tag == JD_TAG_VM_HANDLE) {
                        store_tag(i, 'h');
                        arg_raws[i] = av.val;
                    } else if (av.tag == JD_TAG_RUNTIME && av.runtime_tag) {
                        // Runtime branch: tag==STR → 's' + ptr, else 'd' + f64-pun.
                        // Necessary because RUNTIME values from MAP indexing can be
                        // either at runtime, and {:.2f} on a numeric value must keep
                        // the precision spec working.
                        LLVMValueRef is_str = LLVMBuildICmp(builder, LLVMIntEQ,
                            av.runtime_tag, LLVMConstInt(i32_type, 2, 0), "rt_isstr");
                        LLVMBasicBlockRef bb_s   = LLVMAppendBasicBlock(current_fn, "fmt.rt_s");
                        LLVMBasicBlockRef bb_n   = LLVMAppendBasicBlock(current_fn, "fmt.rt_n");
                        LLVMBasicBlockRef bb_m   = LLVMAppendBasicBlock(current_fn, "fmt.rt_m");
                        LLVMBuildCondBr(builder, is_str, bb_s, bb_n);

                        LLVMPositionBuilderAtEnd(builder, bb_s);
                        store_tag(i, 's');
                        LLVMValueRef sptr = coerce_to(av, i8_ptr_type);
                        LLVMValueRef sraw = LLVMBuildPtrToInt(builder, sptr, i64_type, "rt_sraw");
                        LLVMBuildBr(builder, bb_m);
                        LLVMBasicBlockRef end_s = LLVMGetInsertBlock(builder);

                        LLVMPositionBuilderAtEnd(builder, bb_n);
                        store_tag(i, 'd');
                        LLVMValueRef nd   = coerce_to(av, f64_type);
                        LLVMValueRef nraw = pun_f64_to_i64(nd);
                        LLVMBuildBr(builder, bb_m);
                        LLVMBasicBlockRef end_n = LLVMGetInsertBlock(builder);

                        LLVMPositionBuilderAtEnd(builder, bb_m);
                        LLVMValueRef phi = LLVMBuildPhi(builder, i64_type, "rt_raw");
                        LLVMValueRef vals[] = { sraw, nraw };
                        LLVMBasicBlockRef bbs[] = { end_s, end_n };
                        LLVMAddIncoming(phi, vals, bbs, 2);
                        arg_raws[i] = phi;
                    } else if (av.tag == JD_TAG_I64 || av.tag == JD_TAG_BOOL) {
                        store_tag(i, 'd');
                        LLVMValueRef as_d = LLVMBuildSIToFP(builder, av.val, f64_type, "itof");
                        arg_raws[i] = pun_f64_to_i64(as_d);
                    } else if (av.tag == JD_TAG_F64) {
                        store_tag(i, 'd');
                        arg_raws[i] = pun_f64_to_i64(av.val);
                    } else {
                        store_tag(i, 'd');
                        arg_raws[i] = pun_f64_to_i64(coerce_to(av, f64_type));
                    }
                }
                // NUL-terminate the types buffer.
                LLVMBuildStore(builder, LLVMConstInt(i8_ty, 0, 0), tbuf_slot(nargs));

                std::vector<LLVMValueRef> args;
                args.push_back(fmt_tv.val);
                args.push_back(LLVMBuildBitCast(builder, tbuf, i8_ptr_type, "tbuf_p"));
                for (auto& r : arg_raws) args.push_back(r);

                LLVMValueRef result = LLVMBuildCall2(builder, fit3->second.fn_type, fit3->second.fn,
                                                      args.data(), (unsigned)args.size(), "fmt_t");
                return { result, JD_TAG_STR };
            }
        }

        std::string fn_name = "__format" + std::to_string(nargs);
        auto fit2 = runtime_funcs.find(fn_name);
        if (fit2 != runtime_funcs.end()) {
            std::vector<LLVMValueRef> args;
            args.push_back(fmt_tv.val);
            for (auto& av : evald) {
                // jdb_formatN is declared as (i8*, double, ...). Tags other
                // than F64/I64/BOOL (most importantly VM_HANDLE from map
                // indexing like p{"menge"}, plus RUNTIME-tagged values) used
                // to fall through as raw i64, tripping LLVM IR verification.
                // Route through coerce_to so handle-deref / pun goes through
                // the right runtime helper.
                LLVMValueRef val;
                if (av.tag == JD_TAG_F64) {
                    val = av.val;
                } else if (av.tag == JD_TAG_I64 || av.tag == JD_TAG_BOOL) {
                    val = LLVMBuildSIToFP(builder, av.val, f64_type, "itof");
                } else {
                    val = coerce_to(av, f64_type);
                }
                args.push_back(val);
            }
            LLVMValueRef result = LLVMBuildCall2(builder, fit2->second.fn_type, fit2->second.fn,
                                                  args.data(), (unsigned)args.size(), "fmt");
            return { result, JD_TAG_STR };
        }
    }

    // Handle SELECT/FILTER/REDUCE with lambda function pointers
    //
    // The runtime expects a JdbMapFn (`double(double)`). Lambdas already
    // emit that signature directly. For named FUNCs and module FUNCs
    // (`MOD.FUNC@` parses to a string literal), build a thin trampoline
    // that coerces tagged params/returns to f64.
    auto resolve_funcref = [&](const Expr& e, int arity) -> TypedValue {
        if (e.kind == ExprKind::LITERAL_STRING) {
            LLVMValueRef wrap = build_funcref_wrapper(e.str_val, arity);
            if (wrap) return { wrap, JD_TAG_FUNCREF };
        }
        return codegen_expr(e);
    };

    if ((upper == "SELECT" || upper == "FILTER") && expr.args.size() >= 2) {
        TypedValue fn_val = resolve_funcref(*expr.args[0], 1);
        TypedValue arr_val = codegen_expr(*expr.args[1]);
        if (fn_val.tag == JD_TAG_FUNCREF) {
            // Lambda function pointer + array
            LLVMValueRef arr_ptr = arr_val.val;
            if (arr_val.tag == JD_TAG_F64) {
                LLVMValueRef as_i64 = pun_f64_to_i64(arr_val.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            }
            std::string rt_name = (upper == "SELECT") ? "__select_fn" : "__filter_fn";
            auto& fn = runtime_funcs[rt_name];
            LLVMValueRef args[] = { fn_val.val, arr_ptr };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "hof");
            return { result, JD_TAG_ARR };  // returns array
        }
    }
    if (upper == "REDUCE" && expr.args.size() >= 2) {
        TypedValue fn_val = resolve_funcref(*expr.args[0], 2);
        TypedValue arr_val = codegen_expr(*expr.args[1]);
        double init = 0.0;
        if (fn_val.tag == JD_TAG_FUNCREF) {
            LLVMValueRef arr_ptr = arr_val.val;
            if (arr_val.tag == JD_TAG_F64) {
                LLVMValueRef as_i64 = pun_f64_to_i64(arr_val.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            }
            LLVMValueRef init_val = (expr.args.size() >= 3)
                ? codegen_expr(*expr.args[2]).val
                : LLVMConstReal(f64_type, 0.0);
            if (expr.args.size() >= 3) {
                TypedValue iv = codegen_expr(*expr.args[2]);
                init_val = iv.tag == JD_TAG_I64 ? LLVMBuildSIToFP(builder, iv.val, f64_type, "itof") : iv.val;
            }
            auto& fn = runtime_funcs["__reduce_fn"];
            LLVMValueRef args[] = { fn_val.val, arr_ptr, init_val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "red");
            return { result, JD_TAG_F64 };
        }
    }

    // Handle PUSH: PUSH arr, val → arr = APPEND(arr, val)
    if (upper == "PUSH" && expr.args.size() >= 2) {
        TypedValue arr_tv = codegen_expr(*expr.args[0]);
        TypedValue val_tv = codegen_expr(*expr.args[1]);
        // VM-handle / runtime-tagged pushes must preserve raw i64 bits —
        // coerce_to(tag=7, f64) would CONVERT a VM handle via __jdrt_val_to_f64
        // (yielding 0 for a map object), erasing the handle. Pun directly so
        // the element round-trips through f64 storage. EXCEPT: for a
        // RUNTIME-tagged value whose real type is I64/BOOL (e.g. a JSON
        // int field accessed via tag-aware obj_get_tagged), the i64 bits
        // are a real integer — pun would write a denormal f64 cell that
        // index_of's `=` comparison never matches, so combos lost their
        // selection on round-trip.
        LLVMValueRef fval;
        if (val_tv.tag == JD_TAG_VM_HANDLE) {
            fval = pun_i64_to_f64(val_tv.val);
        } else if (val_tv.tag == JD_TAG_RUNTIME && val_tv.runtime_tag) {
            LLVMValueRef is_int = LLVMBuildICmp(builder, LLVMIntEQ, val_tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_I64, 0), "push_isint");
            LLVMValueRef is_bool = LLVMBuildICmp(builder, LLVMIntEQ, val_tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_BOOL, 0), "push_isbool");
            LLVMValueRef is_intish = LLVMBuildOr(builder, is_int, is_bool, "push_isi");
            LLVMValueRef as_f_pun  = pun_i64_to_f64(val_tv.val);
            LLVMValueRef as_f_real = LLVMBuildSIToFP(builder, val_tv.val, f64_type, "push_i2f");
            fval = LLVMBuildSelect(builder, is_intish, as_f_real, as_f_pun, "push_f");
        } else {
            // coerce_to(_, f64_type) handles other tags: int→fp, ptr→ptoi+pun.
            fval = coerce_to(val_tv, f64_type);
        }
        auto& append_fn = runtime_funcs["APPEND"];
        LLVMValueRef arr_ptr = coerce_to(arr_tv, i8_ptr_type);
        LLVMValueRef args[] = { arr_ptr, fval };
        LLVMValueRef result = LLVMBuildCall2(builder, append_fn.fn_type, append_fn.fn, args, 2, "push");
        // PUSH map{"key"}, val: write the appended array back into the map
        // and propagate the string-elems flag so reads round-trip the type.
        if (expr.args[0]->kind == ExprKind::INDEX &&
            expr.args[0]->left &&
            expr.args[0]->right &&
            expr.args[0]->right->kind == ExprKind::LITERAL_STRING) {
            TypedValue map_tv = codegen_expr(*expr.args[0]->left);
            if (map_tv.tag == JD_TAG_NATIVE_MAP) {
                if (val_tv.tag == JD_TAG_STR) {
                    auto& mark = runtime_funcs["__arr_set_string_elems"];
                    LLVMValueRef margs[] = { result };
                    LLVMBuildCall2(builder, mark.fn_type, mark.fn, margs, 1, "");
                }
                LLVMValueRef key_str = LLVMBuildGlobalStringPtr(builder,
                    expr.args[0]->right->str_val.c_str(), ".pkey");
                auto& set_fn = runtime_funcs["__map_set_tagged"];
                LLVMValueRef rint = LLVMBuildPtrToInt(builder, result, i64_type, "rinst");
                LLVMValueRef rfval = pun_i64_to_f64(rint);
                LLVMValueRef sargs[] = { map_tv.val, key_str, rfval,
                    LLVMConstInt(i32_type, JD_TAG_ARR, 0) };
                LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, sargs, 4, "");
            }
        }
        if (expr.args[0]->kind == ExprKind::VARIABLE) {
            VarInfo* vi = lookup_var(expr.args[0]->str_val);
            if (vi) {
                LLVMBuildStore(builder, result, vi->alloca_val);
                vi->tag = JD_TAG_ARR;  // ensure var is tracked as array
            }
            // Propagate UDT element type if pushed value was a known UDT.
            if (expr.args[1]->kind == ExprKind::VARIABLE) {
                auto it = var_udt_type.find(expr.args[1]->str_val);
                if (it != var_udt_type.end())
                    var_udt_type[expr.args[0]->str_val + "[]"] = it->second;
            }
            // Pushing a string (direct tag=2, or element of a known string
            // array) marks the dest array as string-bearing so later
            // `arr[i]` reads return tag=2 instead of the punned-f64 default.
            bool pushing_str = (val_tv.tag == JD_TAG_STR);
            if (!pushing_str && expr.args[1]->kind == ExprKind::INDEX &&
                expr.args[1]->left &&
                expr.args[1]->left->kind == ExprKind::VARIABLE &&
                string_array_vars.count(expr.args[1]->left->str_val))
                pushing_str = true;
            if (!pushing_str && expr.args[1]->kind == ExprKind::VARIABLE &&
                string_array_vars.count(expr.args[1]->str_val))
                pushing_str = true;
            if (pushing_str)
                string_array_vars.insert(expr.args[0]->str_val);
            // Pushing a map (direct tag=4, element of known map array, or a
            // MAP_LITERAL) marks dest as map-bearing so `q = arr[i]` returns
            // tag=4 (ptr) instead of a scalar f64 that can't be mutated.
            bool pushing_map = (val_tv.tag == JD_TAG_NATIVE_MAP);
            if (!pushing_map && expr.args[1]->kind == ExprKind::MAP_LITERAL)
                pushing_map = true;
            if (!pushing_map && expr.args[1]->kind == ExprKind::VARIABLE &&
                map_array_vars.count(expr.args[1]->str_val))
                pushing_map = true;
            if (!pushing_map && expr.args[1]->kind == ExprKind::INDEX &&
                expr.args[1]->left &&
                expr.args[1]->left->kind == ExprKind::VARIABLE &&
                map_array_vars.count(expr.args[1]->left->str_val))
                pushing_map = true;
            if (pushing_map)
                map_array_vars.insert(expr.args[0]->str_val);
            // VM-handle pushes: tag=6 direct, tag=7 runtime-tagged (likely a
            // nested map from a JSON-parsed object), or INDEX into an existing
            // vm-array. Mark dest so reads come back as tag=6 handles that
            // MAP_ACCESS routes through the VM bridge.
            bool pushing_vm = (val_tv.tag == JD_TAG_VM_HANDLE ||
                              val_tv.tag == JD_TAG_RUNTIME);
            if (!pushing_vm && expr.args[1]->kind == ExprKind::VARIABLE &&
                vm_array_vars.count(expr.args[1]->str_val))
                pushing_vm = true;
            if (!pushing_vm && expr.args[1]->kind == ExprKind::INDEX &&
                expr.args[1]->left &&
                expr.args[1]->left->kind == ExprKind::VARIABLE &&
                vm_array_vars.count(expr.args[1]->left->str_val))
                pushing_vm = true;
            if (pushing_vm)
                vm_array_vars.insert(expr.args[0]->str_val);
        }
        return { result, JD_TAG_ARR };
    }

    // __EVENT_ON("name", "handler") → register handler at runtime
    if (name == "__EVENT_ON" && expr.args.size() >= 2 &&
        expr.args[0] && expr.args[1] &&
        expr.args[1]->kind == ExprKind::LITERAL_STRING) {
        TypedValue ev_name = codegen_expr(*expr.args[0]);
        auto fit = user_functions.find(expr.args[1]->str_val);
        if (fit != user_functions.end()) {
            LLVMValueRef ev_ptr = coerce_to(ev_name, i8_ptr_type);
            LLVMValueRef handler = LLVMBuildBitCast(builder, fit->second.fn,
                                                     i8_ptr_type, "h_as_ptr");
            // 1. Existing path: register with the local jdb_event_on
            //    table that RAISEEVENT in interp+native shares.
            auto& evfn = runtime_funcs["__event_on"];
            LLVMValueRef args1[] = { ev_ptr, handler };
            LLVMBuildCall2(builder, evfn.fn_type, evfn.fn, args1, 2, "");

            // 2. Native-mode SDL events: register the function pointer
            //    in the trampoline so jdrt_dispatch_event can find it.
            auto reg_it = runtime_funcs.find("__jdrt_reg_evh");
            if (reg_it != runtime_funcs.end()) {
                LLVMBuildCall2(builder, reg_it->second.fn_type, reg_it->second.fn,
                               args1, 2, "");
            }

            // 3. Tell the bridge VM that this event has a handler so
            //    its event_poll() actually drains the SDL queue and
            //    calls user_event_dispatch (set up in main init).
            //    bridge VM's __EVENT_ON registers in vm.event_handlers.
            //    We invoke it via jdrt_call_typed_void.
            LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
            auto vfn_it = runtime_funcs.find("__jdrt_call_typed_void");
            if (hg && vfn_it != runtime_funcs.end()) {
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef bname = LLVMBuildGlobalStringPtr(builder, "__EVENT_ON", ".bn");
                LLVMValueRef args_arr = LLVMBuildArrayAlloca(builder, i64_type,
                    LLVMConstInt(i32_type, 2, 0), "args2");
                LLVMValueRef tags_arr = LLVMBuildArrayAlloca(builder, i32_type,
                    LLVMConstInt(i32_type, 2, 0), "tags2");
                // Slot 0: event name (STR)
                LLVMValueRef idx0[] = { LLVMConstInt(i32_type, 0, 0) };
                LLVMValueRef ap0 = LLVMBuildGEP2(builder, i64_type, args_arr, idx0, 1, "ap0");
                LLVMBuildStore(builder, LLVMBuildPtrToInt(builder, ev_ptr, i64_type, "ev_i"), ap0);
                LLVMValueRef tp0 = LLVMBuildGEP2(builder, i32_type, tags_arr, idx0, 1, "tp0");
                LLVMBuildStore(builder, LLVMConstInt(i32_type, JD_TAG_STR, 0), tp0);
                // Slot 1: handler name (STR) — string literal
                LLVMValueRef hname_lit = LLVMBuildGlobalStringPtr(builder,
                    expr.args[1]->str_val.c_str(), ".hn");
                LLVMValueRef idx1[] = { LLVMConstInt(i32_type, 1, 0) };
                LLVMValueRef ap1 = LLVMBuildGEP2(builder, i64_type, args_arr, idx1, 1, "ap1");
                LLVMBuildStore(builder, LLVMBuildPtrToInt(builder, hname_lit, i64_type, "hn_i"), ap1);
                LLVMValueRef tp1 = LLVMBuildGEP2(builder, i32_type, tags_arr, idx1, 1, "tp1");
                LLVMBuildStore(builder, LLVMConstInt(i32_type, JD_TAG_STR, 0), tp1);
                LLVMValueRef vargs[] = { rt, bname,
                    LLVMBuildBitCast(builder, args_arr, i8_ptr_type, "ap"),
                    LLVMBuildBitCast(builder, tags_arr, i8_ptr_type, "tp"),
                    LLVMConstInt(i32_type, 2, 0) };
                LLVMBuildCall2(builder, vfn_it->second.fn_type, vfn_it->second.fn,
                               vargs, 5, "");
            }
            return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
        }
    }

    // __EVENT_RAISE("name", arg) → call jdb_event_raise_str (one string arg)
    if (name == "__EVENT_RAISE" && expr.args.size() >= 2) {
        TypedValue ev_name = codegen_expr(*expr.args[0]);
        TypedValue arg = codegen_expr(*expr.args[1]);
        auto& evfn = runtime_funcs["__event_raise_s"];
        LLVMValueRef args[] = {
            coerce_to(ev_name, i8_ptr_type),
            coerce_to(arg, i8_ptr_type),
        };
        LLVMBuildCall2(builder, evfn.fn_type, evfn.fn, args, 2, "");
        return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
    }

    // CVDATE — dispatch by argument type (string parses ISO, number is
    // epoch seconds, array is element-wise vectorized).
    if ((upper == "CVDATE" || upper == "CDATE") && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_ARR) {
            auto& fn = runtime_funcs["__cvdate_arr"];
            LLVMValueRef args[] = { av.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "cvda"), JD_TAG_ARR };
        }
        if (av.tag == JD_TAG_I64 || av.tag == JD_TAG_F64) {
            auto& fn = runtime_funcs["__cvdate_num"];
            LLVMValueRef args[] = { coerce_to(av, f64_type) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "cvdn"), JD_TAG_STR };
        }
        // String / fallback
        auto& fn = runtime_funcs["CVDATE"];
        LLVMValueRef args[] = { coerce_to(av, i8_ptr_type) };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "cvds"), JD_TAG_STR };
    }

    // ROUND(x, places) → native 2-arg form
    if (upper == "ROUND" && expr.args.size() == 2) {
        TypedValue x = codegen_expr(*expr.args[0]);
        TypedValue p = codegen_expr(*expr.args[1]);
        auto& fn = runtime_funcs["__round_p"];
        LLVMValueRef args[] = {
            coerce_to(x, f64_type),
            coerce_to(p, f64_type),
        };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "rnd"), JD_TAG_F64 };
    }

    // IOTA(count, start, step) → native 3-arg form.
    // IOTA(count, start) is the same with step=1 — without the explicit
    // dispatch, the bridge fallback returned an empty array.
    if (upper == "IOTA" && (expr.args.size() == 2 || expr.args.size() == 3)) {
        TypedValue n  = codegen_expr(*expr.args[0]);
        TypedValue st = codegen_expr(*expr.args[1]);
        LLVMValueRef sp_val = (expr.args.size() == 3)
            ? coerce_to(codegen_expr(*expr.args[2]), f64_type)
            : LLVMConstReal(f64_type, 1.0);
        auto& fn = runtime_funcs["__iota3"];
        LLVMValueRef args[] = {
            coerce_to(n,  f64_type),
            coerce_to(st, f64_type),
            sp_val,
        };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "iota3"), JD_TAG_ARR };
    }

    // POP(arr) — remove last element; return type follows the array's
    // string-flag (bit 1 of JdbArray::flags). We branch at runtime.
    if (upper == "POP" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_ARR) {
            auto& fn_str = runtime_funcs["__arr_pop_str"];
            auto& fn_num = runtime_funcs["__arr_pop"];
            LLVMValueRef arr_ptr = av.val;
            // JdbArray layout: double* data (8) | int64 length (8) | int32 flags (4)
            LLVMValueRef off = LLVMConstInt(i64_type, 16, 0);
            LLVMTypeRef i8_ty = LLVMInt8TypeInContext(ctx);
            LLVMValueRef flags_ptr = LLVMBuildGEP2(builder, i8_ty, arr_ptr, &off, 1, "flagsp");
            LLVMValueRef flags = LLVMBuildLoad2(builder, i32_type, flags_ptr, "flags");
            LLVMValueRef two = LLVMConstInt(i32_type, 2, 0);
            LLVMValueRef masked = LLVMBuildAnd(builder, flags, two, "mask");
            LLVMValueRef is_str = LLVMBuildICmp(builder, LLVMIntEQ, masked, two, "issf");

            LLVMBasicBlockRef str_bb  = LLVMAppendBasicBlock(current_fn, "pop_str");
            LLVMBasicBlockRef num_bb  = LLVMAppendBasicBlock(current_fn, "pop_num");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlock(current_fn, "pop_done");
            LLVMBuildCondBr(builder, is_str, str_bb, num_bb);

            LLVMPositionBuilderAtEnd(builder, str_bb);
            LLVMValueRef s_args[] = { arr_ptr };
            LLVMValueRef sres = LLVMBuildCall2(builder, fn_str.fn_type, fn_str.fn, s_args, 1, "pops");
            LLVMBuildBr(builder, done_bb);
            LLVMBasicBlockRef str_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, num_bb);
            LLVMValueRef n_args[] = { arr_ptr };
            LLVMValueRef nres = LLVMBuildCall2(builder, fn_num.fn_type, fn_num.fn, n_args, 1, "popn");
            // Convert f64 → i8* here so the value dominates done_bb from this path.
            LLVMValueRef nrep = LLVMBuildIntToPtr(builder,
                pun_f64_to_i64(nres), i8_ptr_type, "n2p");
            LLVMBuildBr(builder, done_bb);
            LLVMBasicBlockRef num_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, done_bb);
            // Unify via i8*: string is already a char*, number was punned above.
            // Consumers read tag=2; compare-as-number paths still coerce back.
            LLVMValueRef phi = LLVMBuildPhi(builder, i8_ptr_type, "popv");
            LLVMValueRef vals[] = { sres, nrep };
            LLVMBasicBlockRef bbs[] = { str_end, num_end };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return { phi, JD_TAG_STR };
        }
    }

    // Handle LENV — shape vector: [dim0, dim1, …] for nested arrays,
    // [n] for 1D arrays/strings. Always tag=3 (array) so callers can
    // index it.
    if (upper == "LENV" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_ARR) {
            auto* shape_fn = get_runtime_func("__arr_len_shape");
            if (shape_fn) {
                LLVMValueRef args[] = { av.val };
                return { LLVMBuildCall2(builder, shape_fn->fn_type,
                    shape_fn->fn, args, 1, "lenv"), JD_TAG_ARR };
            }
        }
        // Non-array: wrap the scalar length in a 1-element array.
        LLVMValueRef len_i64;
        if (av.tag == JD_TAG_STR) {
            auto& fn = runtime_funcs["LEN$"];
            LLVMValueRef a[] = { av.val };
            len_i64 = LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 1, "slen");
        } else {
            len_i64 = LLVMConstInt(i64_type, 0, 0);
        }
        auto* new_fn = get_runtime_func("__array_new");
        auto* set_fn = get_runtime_func("__array_set");
        if (new_fn && set_fn) {
            LLVMValueRef n_args[] = { LLVMConstInt(i64_type, 1, 0) };
            LLVMValueRef arr = LLVMBuildCall2(builder, new_fn->fn_type,
                new_fn->fn, n_args, 1, "lenv_one");
            LLVMValueRef as_d = LLVMBuildSIToFP(builder, len_i64, f64_type, "lenv_d");
            LLVMValueRef s_args[] = { arr, LLVMConstInt(i64_type, 0, 0), as_d };
            LLVMBuildCall2(builder, set_fn->fn_type, set_fn->fn, s_args, 3, "");
            return { arr, JD_TAG_ARR };
        }
        return { LLVMConstPointerNull(i8_ptr_type), JD_TAG_ARR };
    }

    // Handle LEN — dispatch based on argument type (string vs array)
    if (upper == "LEN" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_STR) {
            auto& fn = runtime_funcs["LEN$"];
            LLVMValueRef args[] = { av.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "slen"), JD_TAG_I64 };
        }
        if (av.tag == JD_TAG_ARR) {
            auto& fn = runtime_funcs["LEN"];
            LLVMValueRef args[] = { av.val };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "alen"), JD_TAG_I64 };
        }
        if (av.tag == JD_TAG_VM_HANDLE) {
            auto* fn = get_runtime_func("__jdrt_val_length");
            if (fn) {
                LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef args[] = { rt, av.val };
                return { LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 2, "hlen"), JD_TAG_I64 };
            }
        }
        if (av.tag == JD_TAG_RUNTIME && av.runtime_tag) {
            // Runtime-tagged: could be string, array, or VM handle.
            // Branch by runtime_tag: 2=STR → jdb_len_str (C string),
            // 3=ARR → jdb_array_len, else → jdrt_val_length (VM handle).
            LLVMValueRef is_str = LLVMBuildICmp(builder, LLVMIntEQ,
                av.runtime_tag, LLVMConstInt(i32_type, 2, 0), "is_str");
            LLVMValueRef is_arr = LLVMBuildICmp(builder, LLVMIntEQ,
                av.runtime_tag, LLVMConstInt(i32_type, 3, 0), "is_arr");
            LLVMBasicBlockRef bb_str = LLVMAppendBasicBlock(current_fn, "len7.str");
            LLVMBasicBlockRef bb_not_str = LLVMAppendBasicBlock(current_fn, "len7.nstr");
            LLVMBasicBlockRef bb_arr = LLVMAppendBasicBlock(current_fn, "len7.arr");
            LLVMBasicBlockRef bb_vm = LLVMAppendBasicBlock(current_fn, "len7.vm");
            LLVMBasicBlockRef bb_join = LLVMAppendBasicBlock(current_fn, "len7.join");
            LLVMBuildCondBr(builder, is_str, bb_str, bb_not_str);

            LLVMPositionBuilderAtEnd(builder, bb_str);
            LLVMValueRef sptr = LLVMBuildIntToPtr(builder, av.val, i8_ptr_type, "sptr");
            auto& fn_str = runtime_funcs["LEN$"];
            LLVMValueRef slen = LLVMBuildCall2(builder, fn_str.fn_type, fn_str.fn, &sptr, 1, "slen");
            LLVMBuildBr(builder, bb_join);
            LLVMBasicBlockRef bb_str_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_not_str);
            LLVMBuildCondBr(builder, is_arr, bb_arr, bb_vm);

            LLVMPositionBuilderAtEnd(builder, bb_arr);
            LLVMValueRef aptr = LLVMBuildIntToPtr(builder, av.val, i8_ptr_type, "aptr");
            auto& fn_arr = runtime_funcs["LEN"];
            LLVMValueRef alen = LLVMBuildCall2(builder, fn_arr.fn_type, fn_arr.fn, &aptr, 1, "alen");
            LLVMBuildBr(builder, bb_join);
            LLVMBasicBlockRef bb_arr_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_vm);
            auto* fn_vm = get_runtime_func("__jdrt_val_length");
            LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
            LLVMValueRef vargs[] = { rt, av.val };
            LLVMValueRef vlen = fn_vm
                ? LLVMBuildCall2(builder, fn_vm->fn_type, fn_vm->fn, vargs, 2, "vlen")
                : LLVMConstInt(i64_type, 0, 0);
            LLVMBuildBr(builder, bb_join);
            LLVMBasicBlockRef bb_vm_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_join);
            LLVMValueRef phi = LLVMBuildPhi(builder, i64_type, "len7");
            LLVMValueRef vals[] = { slen, alen, vlen };
            LLVMBasicBlockRef bbs[] = { bb_str_end, bb_arr_end, bb_vm_end };
            LLVMAddIncoming(phi, vals, bbs, 3);
            return { phi, JD_TAG_I64 };
        }
        // Fallback
    }

    // Handle TYPEOF — resolve type tag at compile time
    // ISBOOL/ISNUM/ISSTR/ISARR/ISMAP/ISNONE — compile-time type tests
    if ((upper == "ISBOOL" || upper == "ISNUM" || upper == "ISSTR" ||
         upper == "ISARR" || upper == "ISMAP" || upper == "ISNONE" ||
         upper == "ISNULL") && expr.args.size() == 1) {
        const Expr& arg = *expr.args[0];
        bool result = false;
        if (upper == "ISBOOL") {
            result = (arg.kind == ExprKind::LITERAL_BOOL);
        } else if (upper == "ISNUM") {
            // Bool literals are also numeric in jdBasic
            if (arg.kind == ExprKind::LITERAL_INT || arg.kind == ExprKind::LITERAL_FLOAT
                || arg.kind == ExprKind::LITERAL_BOOL) result = true;
            else if (arg.kind == ExprKind::VARIABLE) {
                VarInfo* v = lookup_var(arg.str_val);
                if (v && (v->tag == JD_TAG_I64 || v->tag == JD_TAG_F64)) result = true;
            }
        } else if (upper == "ISSTR") {
            if (arg.kind == ExprKind::LITERAL_STRING) result = true;
            else if (arg.kind == ExprKind::VARIABLE) {
                VarInfo* v = lookup_var(arg.str_val);
                if (v && v->tag == JD_TAG_STR) result = true;
            }
        } else if (upper == "ISARR") {
            if (arg.kind == ExprKind::ARRAY_LITERAL) result = true;
            else if (arg.kind == ExprKind::VARIABLE) {
                VarInfo* v = lookup_var(arg.str_val);
                if (v && v->tag == JD_TAG_ARR) result = true;
            }
        } else if (upper == "ISMAP") {
            if (arg.kind == ExprKind::MAP_LITERAL) result = true;
            else if (arg.kind == ExprKind::VARIABLE) {
                VarInfo* v = lookup_var(arg.str_val);
                if (v && v->tag == JD_TAG_NATIVE_MAP) result = true;
            }
        }
        // ISNONE / ISNULL: not natively supported, default to false
        return { LLVMConstInt(i64_type, result ? 1 : 0, 0), JD_TAG_I64 };
    }

    if (upper == "TYPEOF" && !expr.args.empty()) {
        // Special-case BOOL literals at compile time
        if (expr.args[0]->kind == ExprKind::LITERAL_BOOL) {
            return { LLVMBuildGlobalStringPtr(builder, "BOOLEAN", ".tof"), JD_TAG_STR };
        }
        // Variables declared AS BOOLEAN or bound to a bool literal
        if (expr.args[0]->kind == ExprKind::VARIABLE) {
            std::string up = expr.args[0]->str_val;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            if (bool_vars.count(up)) {
                return { LLVMBuildGlobalStringPtr(builder, "BOOLEAN", ".tof"), JD_TAG_STR };
            }
            if (date_vars.count(up)) {
                return { LLVMBuildGlobalStringPtr(builder, "DATE", ".tof"), JD_TAG_STR };
            }
        }
        // DATE values: variables typed via CVDATE/DATEADD return strings
        // tagged as DATE. Check if expr is a known date-producing call:
        if (expr.args[0]->kind == ExprKind::CALL || expr.args[0]->kind == ExprKind::VARIABLE) {
            std::string fn_or_var;
            if (expr.args[0]->kind == ExprKind::CALL) fn_or_var = expr.args[0]->func_name;
            std::transform(fn_or_var.begin(), fn_or_var.end(), fn_or_var.begin(), ::toupper);
            if (fn_or_var == "CVDATE" || fn_or_var == "CDATE" || fn_or_var == "DATEADD" || fn_or_var == "NOW")
                return { LLVMBuildGlobalStringPtr(builder, "DATE", ".tof"), JD_TAG_STR };
        }
        TypedValue av = codegen_expr(*expr.args[0]);
        // f64 values may be the NaN sentinel from EXITFUNC — dispatch at runtime.
        if (av.tag == JD_TAG_F64) {
            auto& fn = runtime_funcs["__typeof_f64"];
            LLVMValueRef args[] = { av.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "tof_f");
            return { result, JD_TAG_STR };
        }
        auto& fn = runtime_funcs["__typeof_tag"];
        // RUNTIME-tagged values carry their real JdTag in av.runtime_tag —
        // dispatch on that so TYPEOF on an untyped FUNC param sees the
        // execution-time type instead of the static "RUNTIME" placeholder.
        LLVMValueRef tag_val;
        if (av.tag == JD_TAG_RUNTIME && av.runtime_tag) {
            tag_val = LLVMBuildSExt(builder, av.runtime_tag, i64_type, "tof_rt");
        } else {
            tag_val = LLVMConstInt(i64_type, av.tag, 0);
        }
        LLVMValueRef args[] = { tag_val };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "typeof");
        return { result, JD_TAG_STR };
    }

    // Special case: DATEDIFF with array arg → native jdb_datediff_vec
    if (upper == "DATEDIFF" && expr.args.size() == 3) {
        TypedValue p = codegen_expr(*expr.args[0]);
        TypedValue d1 = codegen_expr(*expr.args[1]);
        TypedValue d2 = codegen_expr(*expr.args[2]);
        if (d2.tag == JD_TAG_ARR) {
            auto& fn = runtime_funcs["__datediff_vec"];
            LLVMValueRef args[] = {
                coerce_to(p, i8_ptr_type),
                coerce_to(d1, i8_ptr_type),
                d2.val
            };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "ddv");
            return { result, JD_TAG_ARR };
        }
    }

    // Auto-vectorization blocklist for native codegen.
    // Any function NOT listed here vectorizes element-wise when any arg is
    // an array (e.g. RIGHT$(["Atomi","Bert"], 2) → ["mi","rt"]).
    // This list is codegen-specific and does NOT mirror vm.cpp — the VM
    // bridge has its own smaller list in vm.cpp (VM::call_function).
    static const std::unordered_set<std::string> no_vectorize = {
        // Array producers
        "ZEROS", "ONES", "__MAKE_UDT_ARRAY__", "IOTA", "RESHAPE", "TENSOR",
        "RANGE", "LINSPACE",
        // Array/matrix operations that consume arrays as a whole
        "LEN", "PUSH", "POP", "APPEND", "DIFF", "TAKE", "DROP", "REVERSE", "FILLV", "COPYV",
        "UNIQUE", "SHUFFLE", "FIND_IN_ARRAY", "NORMALIZE", "DISTANCE",
        "GRADE", "TRANSPOSE", "MATMUL", "MVLET", "STACK", "SLICE", "SOLVE",
        "INVERT", "CONVOLVE", "PLACE", "OUTER", "ROTATE", "SHIFT", "XSORT",
        "SVD", "QR", "DET", "EIG", "FFT", "IFFT",
        "INTEGRATE", "FLATTEN", "ZIP", "DOT", "CROSS", "CUMSUM", "CUMPROD",
        "HISTOGRAM", "COUNT", "INDEXOF", "SORT",
        // Aggregations
        "SUM", "PRODUCT", "MIN", "MAX", "ANY", "ALL",
        "MEAN", "MEDIAN", "VARIANCE", "STDEV",
        // Higher-order
        "SCAN", "SELECT", "FILTER", "REDUCE",
        "TAKE_WHILE", "DROP_WHILE", "CHUNK", "ENUMERATE", "GROUPBY",
        // Meta/type
        "TYPEOF", "IIF", "ISNUM", "ISSTR", "ISARR", "ISMAP", "ISBOOL",
        "ISNONE", "ISNULL",
        // Scalar-returning date/time (note: DATEADD/DATEDIFF/FORMAT_DATE DO vectorize)
        "GETENV$", "SETENV", "SETLOCALE", "TICK", "NOW", "NOW_EPOCH",
        "DATE$", "TIME$", "CVDATE", "CDATE", "RANDOMSEED",
        "DATE.UTC", "DATE.PARTS",
        "MKTEMP$", "RMDIR", "MKDIR", "KILL",
        // Bitwise/math helpers (scalars-only)
        "ROTL", "ROTR", "GCD", "LCM",
        // Collections
        "MAP.EXISTS", "MAP.KEYS", "MAP.VALUES", "MAP.ITEMS", "MAP.SIZE",
        "MAP.DELETE", "MAP.CLEAR", "MAP.MERGE", "MAP.FROM",
        "JSON.PARSE$", "JSON.STRINGIFY$",
        // String/codec (produce from string)
        "SPLIT", "FORMAT$", "FRMV$", "INSERT$", "REPLACE$", "REVERSE$",
        "PACK$", "UNPACK", "JOIN",
        "CODEC.BASE64_ENCODE$", "CODEC.BASE64_DECODE$",
        "CODEC.SHA256$", "CODEC.UUID$",
        // Regex (produce arrays)
        "REGEX_MATCH", "REGEX_REPLACE$", "REGEX.MATCH", "REGEX.FINDALL", "REGEX.REPLACE",
        // File I/O
        "TXTREADER$", "TXTWRITER", "BINREADER$", "BINWRITER",
        "CSVREADER", "CSVWRITER",
        // System/console
        "CLS", "LOCATE", "COLOR", "CURSOR", "SLEEP",
        "GETX", "GETY", "INKEY$", "WAITKEY$", "OPTION",
        "CLIPBOARD.SET", "CLIPBOARD.GET$",
        "OS.GETOS", "OS.GETOS$", "OS.ARGS", "OS.EXEC",
        "OS.HOSTNAME$", "OS.IP$", "OS.LOAD",
        "DIR$", "DIR", "CD", "PWD", "MKDIR", "KILL",
        "FILE.EXISTS", "FILE.SIZE", "FILE.ISDIR", "FILE.STAT",
        "PATH.JOIN$", "PATH.BASENAME$", "PATH.EXT$",
        "PATH.DIRNAME$", "PATH.NORMALIZE$",
        // Execution
        "EXECUTE", "EVAL", "LOAD", "SAVE", "LIST", "HELP", "HELP$", "VARS",
        "RECUR", "CLEAR_RECUR", "LIST_RECUR",
        // Threads/async/react
        "AWAIT", "THREAD.ISDONE", "THREAD.GETRESULT",
        "REACT_BIND", "UNREACT",
        // FFI/internals
        "__EVENT_ON", "__EVENT_RAISE", "__FFI_DECLARE",
        // Graphics primitives that consume arrays as a whole (matrix arg,
        // colour-list arg). Auto-vectorising PLOTRAW with a 2D RGB cache
        // collapses to one PLOTRAW per element with a scalar in the matrix
        // slot — every call sees args[2].type==FLOAT64 and exits early.
        // Same shape applies to the other matrix-form drawing primitives.
        "PLOTRAW", "RECT", "CIRCLE", "LINE", "ELLIPSE", "ROUNDED_RECT",
        "CIRCLE_SECTOR", "PSET", "TEXT", "GFX.PLOT_POINTS", "DRAWCOLOR",
        "SCREEN", "SCREENFLIP", "SETFONT", "TOGGLE_FULLSCREEN",
        // OpenGL — array args (VBO data) are payload, not broadcast targets.
        "GL.WINDOW", "GL.CLOSE", "GL.CLEAR", "GL.FLIP", "GL.VIEWPORT",
        "GL.ENABLE", "GL.DISABLE",
        "GL.SHADER", "GL.USE", "GL.SHADER.DELETE",
        "GL.VBO", "GL.VBO.BIND", "GL.BUFFER.DELETE",
        "GL.VAO", "GL.VAO.BIND", "GL.VAO.DELETE",
        "GL.ATTRIB", "GL.DRAW.TRIS", "GL.DRAW.LINES", "GL.DRAW.TRIS.IDX",
        "GL.UNIFORM.F1", "GL.UNIFORM.F3", "GL.UNIFORM.F4", "GL.UNIFORM.I1",
        "GL.UNIFORM.MAT4",
        "GL.TEX.LOAD", "GL.TEX.BIND", "GL.TEX.DELETE", "GL.EBO",
        "MAT4.IDENTITY", "MAT4.PERSPECTIVE", "MAT4.LOOKAT",
        "MAT4.TRANSLATE", "MAT4.ROTATE", "MAT4.SCALE", "MAT4.MUL",
        // Assert is a user SUB but if used as native:
    };

    // Native vectorization table: funcname → (applier, scalar runtime fn, sig).
    // sig: "ff" = double(double), "ss" = str(str), "ifs" = int(str),
    //      "sfi" = str(str, int), "sfii" = str(str, int, int).
    struct VecSpec { const char* applier; const char* scalar; const char* sig; };
    static const std::unordered_map<std::string, VecSpec> native_vec = {
        // Numeric unary: all return double(double)
        {"SIN",  {"__arr_apply_ff", "jdb_sin",  "ff"}},
        {"COS",  {"__arr_apply_ff", "jdb_cos",  "ff"}},
        {"TAN",  {"__arr_apply_ff", "jdb_tan",  "ff"}},
        {"ASIN", {"__arr_apply_ff", "jdb_asin", "ff"}},
        {"ACOS", {"__arr_apply_ff", "jdb_acos", "ff"}},
        {"ATAN", {"__arr_apply_ff", "jdb_atan", "ff"}},
        {"SINH", {"__arr_apply_ff", "jdb_sinh", "ff"}},
        {"COSH", {"__arr_apply_ff", "jdb_cosh", "ff"}},
        {"TANH", {"__arr_apply_ff", "jdb_tanh", "ff"}},
        {"EXP",  {"__arr_apply_ff", "jdb_exp",  "ff"}},
        {"LOG",  {"__arr_apply_ff", "jdb_log",  "ff"}},
        {"LOG10",{"__arr_apply_ff", "jdb_log10","ff"}},
        {"SQR",  {"__arr_apply_ff", "jdb_sqr",  "ff"}},
        {"ABS",  {"__arr_apply_ff", "jdb_abs",  "ff"}},
        {"FLOOR",{"__arr_apply_ff", "jdb_floor","ff"}},
        {"CEIL", {"__arr_apply_ff", "jdb_ceil", "ff"}},
        {"ROUND",{"__arr_apply_ff", "jdb_round","ff"}},
        {"TRUNC",{"__arr_apply_ff", "jdb_trunc","ff"}},
        {"SIGN", {"__arr_apply_ff", "jdb_sign", "ff"}},
        {"FAC",  {"__arr_apply_ff", "jdb_fac",  "ff"}},
        // String unary: str(str)
        {"UCASE$", {"__arr_apply_ss", "jdb_upper", "ss"}},
        {"LCASE$", {"__arr_apply_ss", "jdb_lower", "ss"}},
        {"UPPER$", {"__arr_apply_ss", "jdb_upper", "ss"}},
        {"LOWER$", {"__arr_apply_ss", "jdb_lower", "ss"}},
        {"TRIM$",  {"__arr_apply_ss", "jdb_trim",  "ss"}},
        {"TRIM",   {"__arr_apply_ss", "jdb_trim",  "ss"}},
        {"LTRIM$", {"__arr_apply_ss", "jdb_ltrim", "ss"}},
        {"RTRIM$", {"__arr_apply_ss", "jdb_rtrim", "ss"}},
        // String + int: str(str, int)
        {"LEFT$",  {"__arr_apply_sfi", "jdb_left",  "sfi"}},
        {"RIGHT$", {"__arr_apply_sfi", "jdb_right", "sfi"}},
        // String + int + int: str(str, int, int)
        {"MID$",   {"__arr_apply_sfii", "jdb_mid",  "sfii"}},
        // String → int: int(str)
        {"LEN$",   {"__arr_apply_ifs", "jdb_len_str", "ifs"}},
        {"ASC",    {"__arr_apply_ifs", "jdb_asc",     "ifs"}},
    };

    // Direct array specializations — bypass the apply_ff function-pointer
    // callback for unary math that can inline + vectorise.
    {
        static const std::unordered_map<std::string, const char*> direct_array_unary = {
            {"SIN","__arr_sin"},   {"COS","__arr_cos"},   {"TAN","__arr_tan"},
            {"ASIN","__arr_asin"}, {"ACOS","__arr_acos"}, {"ATAN","__arr_atan"},
            {"SINH","__arr_sinh"}, {"COSH","__arr_cosh"}, {"TANH","__arr_tanh"},
            {"EXP","__arr_exp"},   {"LOG","__arr_log"},   {"LOG10","__arr_log10"},
            {"SQR","__arr_sqr"},   {"ABS","__arr_abs"},
            {"FLOOR","__arr_floor"},{"CEIL","__arr_ceil"},
            {"ROUND","__arr_round"},{"TRUNC","__arr_trunc"},
        };
        auto dit = direct_array_unary.find(upper);
        if (dit != direct_array_unary.end() && expr.args.size() == 1) {
            TypedValue av = codegen_expr(*expr.args[0]);
            if (av.tag == JD_TAG_ARR) {
                auto rit = runtime_funcs.find(dit->second);
                if (rit != runtime_funcs.end()) {
                    LLVMValueRef args[] = { av.val };
                    LLVMValueRef result = LLVMBuildCall2(builder, rit->second.fn_type,
                        rit->second.fn, args, 1, "vecd");
                    return { result, JD_TAG_ARR };
                }
            }
        }
    }

    // Try native vectorization first — avoids VM bridge overhead.
    {
        auto vit = native_vec.find(upper);
        if (vit != native_vec.end() && !expr.args.empty()) {
            TypedValue av = codegen_expr(*expr.args[0]);
            if (av.tag == JD_TAG_ARR) {
                const VecSpec& spec = vit->second;
                // Look up the scalar runtime function's LLVMValueRef (by name via runtime_funcs)
                // We need to find it by the jdb_* C name — scan all registered runtimes.
                LLVMValueRef scalar_fn = nullptr;
                for (auto& [k, rf] : runtime_funcs) {
                    // Compare via the C function name (LLVMValueRef name)
                    const char* ln = LLVMGetValueName(rf.fn);
                    if (ln && strcmp(ln, spec.scalar) == 0) {
                        scalar_fn = rf.fn;
                        break;
                    }
                }
                if (!scalar_fn) goto no_native_vec;  // fall through

                auto* applier = get_runtime_func(spec.applier);
                if (!applier) goto no_native_vec;

                if (strcmp(spec.sig, "ff") == 0 || strcmp(spec.sig, "ss") == 0 ||
                    strcmp(spec.sig, "ifs") == 0) {
                    LLVMValueRef args[] = { av.val, scalar_fn };
                    LLVMValueRef result = LLVMBuildCall2(builder, applier->fn_type,
                        applier->fn, args, 2, "vec");
                    return { result, JD_TAG_ARR };
                }
                if (strcmp(spec.sig, "sfi") == 0 && expr.args.size() == 2) {
                    TypedValue nv = codegen_expr(*expr.args[1]);
                    LLVMValueRef n = coerce_to(nv, i64_type);
                    LLVMValueRef args[] = { av.val, n, scalar_fn };
                    LLVMValueRef result = LLVMBuildCall2(builder, applier->fn_type,
                        applier->fn, args, 3, "vec");
                    return { result, JD_TAG_ARR };
                }
                if (strcmp(spec.sig, "sfii") == 0 && expr.args.size() == 3) {
                    TypedValue av2 = codegen_expr(*expr.args[1]);
                    TypedValue av3 = codegen_expr(*expr.args[2]);
                    LLVMValueRef a2 = coerce_to(av2, i64_type);
                    LLVMValueRef a3 = coerce_to(av3, i64_type);
                    LLVMValueRef args[] = { av.val, a2, a3, scalar_fn };
                    LLVMValueRef result = LLVMBuildCall2(builder, applier->fn_type,
                        applier->fn, args, 4, "vec");
                    return { result, JD_TAG_ARR };
                }
            }
        }
        no_native_vec:;
    }

    // Check if any argument is an array AND the function is not blocklisted.
    if (!no_vectorize.count(upper) && !expr.args.empty()) {
        // Evaluate args first
        std::vector<TypedValue> vals;
        vals.reserve(expr.args.size());
        bool has_array = false;
        for (auto& a : expr.args) {
            TypedValue v = codegen_expr(*a);
            vals.push_back(v);
            if (v.tag == JD_TAG_ARR) has_array = true;
        }
        if (has_array) {
            // Special case: DATEDIFF vectorization has a native fast path
            if (upper == "DATEDIFF" && vals.size() == 3 && vals[2].tag == JD_TAG_ARR && vals[1].tag != JD_TAG_ARR) {
                auto& fn = runtime_funcs["__datediff_vec"];
                LLVMValueRef a0 = coerce_to(vals[0], i8_ptr_type);
                LLVMValueRef a1 = coerce_to(vals[1], i8_ptr_type);
                LLVMValueRef args[] = { a0, a1, vals[2].val };
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "ddv");
                return { result, JD_TAG_ARR };
            }
            // Generic: dispatch via VM bridge for vectorized apply
            LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type,
                LLVMGetNamedGlobal(module, "__jdrt_handle"), "rt");
            int nargs = (int)vals.size();
            // Hoist args/tags allocas to entry block (see comment at the
            // main jdrt_call_typed_* call site below for why).
            LLVMBasicBlockRef cur_bb_v = LLVMGetInsertBlock(builder);
            LLVMBasicBlockRef entry_bb_v = LLVMGetEntryBasicBlock(current_fn);
            LLVMValueRef first_v = LLVMGetFirstInstruction(entry_bb_v);
            if (first_v) LLVMPositionBuilderBefore(builder, first_v);
            else         LLVMPositionBuilderAtEnd(builder, entry_bb_v);
            LLVMValueRef args_p = LLVMBuildArrayAlloca(builder, i64_type,
                LLVMConstInt(i32_type, nargs, 0), "args");
            LLVMValueRef tags_p = LLVMBuildArrayAlloca(builder, i32_type,
                LLVMConstInt(i32_type, nargs, 0), "tags");
            LLVMPositionBuilderAtEnd(builder, cur_bb_v);
            for (int i = 0; i < nargs; i++) {
                TypedValue av = vals[i];
                LLVMValueRef encoded; int32_t tg;
                if (av.tag == JD_TAG_STR || av.tag == JD_TAG_ARR) {
                    encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "ptoi"); tg = av.tag;
                } else if (av.tag == JD_TAG_F64) {
                    encoded = pun_f64_to_i64(av.val); tg = JD_TAG_F64;
                } else {
                    // I64: pass through unchanged with tag I64.
                    encoded = av.val; tg = JD_TAG_I64;
                }
                LLVMValueRef aidx[] = { LLVMConstInt(i32_type, i, 0) };
                LLVMBuildStore(builder, encoded,
                    LLVMBuildGEP2(builder, i64_type, args_p, aidx, 1, "a"));
                LLVMBuildStore(builder, LLVMConstInt(i32_type, tg, 0),
                    LLVMBuildGEP2(builder, i32_type, tags_p, aidx, 1, "t"));
            }
            LLVMValueRef name_str = LLVMBuildGlobalStringPtr(builder, upper.c_str(), ".fn");
            auto& vfn = runtime_funcs["__jdrt_call_typed_arr"];
            LLVMValueRef call_args[] = { handle, name_str, args_p, tags_p,
                LLVMConstInt(i32_type, nargs, 0) };
            LLVMValueRef result = LLVMBuildCall2(builder, vfn.fn_type, vfn.fn, call_args, 5, "vec");
            return { result, JD_TAG_ARR };
        }
    }

    // Handle YEAR/MONTH/DAY/HOUR/MINUTE/SECOND: dispatch to _str variant if arg is a string.
    static const std::unordered_map<std::string, std::string> date_accessors = {
        {"YEAR", "__year_str"}, {"MONTH", "__month_str"}, {"DAY", "__day_str"},
        {"HOUR", "__hour_str"}, {"MINUTE", "__minute_str"}, {"SECOND", "__second_str"}
    };
    auto dit = date_accessors.find(upper);
    if (dit != date_accessors.end() && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_STR) {  // string arg → use _str variant
            auto sit = runtime_funcs.find(dit->second);
            if (sit != runtime_funcs.end()) {
                LLVMValueRef args[] = { av.val };
                LLVMValueRef result = LLVMBuildCall2(builder, sit->second.fn_type,
                    sit->second.fn, args, 1, "dt");
                return { result, JD_TAG_I64 };
            }
        }
        // Fall through to f64 variant via generic runtime lookup below
    }

    // Handle IIF with strings: IIF(cond, str1, str2) → VM bridge
    if (upper == "IIF" && expr.args.size() == 3) {
        TypedValue cond = codegen_expr(*expr.args[0]);
        TypedValue val1 = codegen_expr(*expr.args[1]);
        TypedValue val2 = codegen_expr(*expr.args[2]);
        if (val1.tag == JD_TAG_STR || val2.tag == JD_TAG_STR) {
            // String IIF: use native select
            LLVMValueRef cond_i1 = to_i1(cond);
            // Ensure both are strings
            auto to_str_iif = [&](TypedValue tv) -> LLVMValueRef {
                if (tv.tag == JD_TAG_STR) return tv.val;
                if (tv.tag == JD_TAG_I64) {
                    auto& fn = runtime_funcs["__int_to_str"];
                    LLVMValueRef a[] = { tv.val };
                    return LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 1, "itostr");
                }
                if (tv.tag == JD_TAG_F64) {
                    auto& fn = runtime_funcs["__double_to_str"];
                    LLVMValueRef a[] = { tv.val };
                    return LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 1, "ftostr");
                }
                return to_string_ptr(tv);
            };
            LLVMValueRef result = LLVMBuildSelect(builder, cond_i1,
                to_str_iif(val1), to_str_iif(val2), "iif");
            return { result, JD_TAG_STR };
        }
    }

    // Handle MIN/MAX — scalar (2 args) or array (1 arg)
    if ((upper == "MIN" || upper == "MAX") && expr.args.size() == 1) {
        // Array version
        std::string fn_name = (upper == "MIN") ? "__arr_min" : "__arr_max";
        auto ait = runtime_funcs.find(fn_name);
        if (ait != runtime_funcs.end()) {
            TypedValue av = codegen_expr(*expr.args[0]);
            LLVMValueRef arr_ptr = av.val;
            if (av.tag == JD_TAG_F64) {
                LLVMValueRef as_i64 = pun_f64_to_i64(av.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            }
            LLVMValueRef args[] = { arr_ptr };
            LLVMValueRef result = LLVMBuildCall2(builder, ait->second.fn_type, ait->second.fn, args, 1, "call");
            return { result, ait->second.return_tag };
        }
    }

    // MAP.EXISTS(map, key$) — intercept to call __map_has directly.
    // The bridge has no NATIVE_MAP path, so going through jdrt_call_typed_f64
    // downgrades the map to I64, the VM's MAP.EXISTS does as_object() → empty,
    // and every lookup returns false. That breaks `IF MAP.EXISTS(m, k) THEN
    // m2{k}=m{k}` (classic JSON-to-map forwarding) — exactly the pattern
    // rpg_engine.jdb uses to build per-NPC data from Tiled properties.
    if (upper == "MAP.EXISTS" && expr.args.size() == 2) {
        TypedValue mtv = codegen_expr(*expr.args[0]);
        bool is_var = (expr.args[0]->kind == ExprKind::VARIABLE);
        if (mtv.tag == JD_TAG_NATIVE_MAP || (is_var && mtv.tag == JD_TAG_F64) || (is_var && mtv.tag == JD_TAG_I64)) {
            TypedValue ktv = codegen_expr(*expr.args[1]);
            LLVMValueRef kptr = coerce_to(ktv, i8_ptr_type);
            LLVMValueRef mptr;
            if (mtv.tag == JD_TAG_NATIVE_MAP) {
                mptr = coerce_to(mtv, i8_ptr_type);
            } else if (mtv.tag == JD_TAG_F64) {
                LLVMValueRef as_i64 = pun_f64_to_i64(mtv.val);
                mptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            } else {
                mptr = LLVMBuildIntToPtr(builder, mtv.val, i8_ptr_type, "itoptr");
            }
            auto& fn = runtime_funcs["__map_has"];
            LLVMValueRef a[] = { mptr, kptr };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 2, "mhas"), JD_TAG_I64 };
        }
        // VM handle (JSON.PARSE$, MAP.FROM, etc.): go direct to
        // __jdrt_obj_exists. Routing through jdrt_call_typed_f64 works in
        // principle but has been observed to return 0 for handles that
        // resolve correctly via the direct getter — likely because the
        // f64 bridge path runs MAP.EXISTS under the autovec guard, and
        // the handle's tag mapping into the typed-args helper loses info.
        if (mtv.tag == JD_TAG_VM_HANDLE) {
            TypedValue ktv = codegen_expr(*expr.args[1]);
            LLVMValueRef kptr = coerce_to(ktv, i8_ptr_type);
            LLVMValueRef handle_g = LLVMGetNamedGlobal(module, "__jdrt_handle");
            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, handle_g, "rt");
            auto& fn = runtime_funcs["__jdrt_obj_exists"];
            LLVMValueRef a[] = { rt, mtv.val, kptr };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 3, "oex"), JD_TAG_I64 };
        }
        // Runtime-tagged: branch on runtime_tag — if VM_HANDLE, use
        // obj_exists; else fall to __map_has on the punned pointer.
        if (mtv.tag == JD_TAG_RUNTIME && mtv.runtime_tag) {
            TypedValue ktv = codegen_expr(*expr.args[1]);
            LLVMValueRef kptr = coerce_to(ktv, i8_ptr_type);
            LLVMValueRef is_vm = LLVMBuildICmp(builder, LLVMIntEQ, mtv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_VM_HANDLE, 0), "isvm");
            LLVMBasicBlockRef vm_bb  = LLVMAppendBasicBlock(current_fn, "mex_vm");
            LLVMBasicBlockRef map_bb = LLVMAppendBasicBlock(current_fn, "mex_map");
            LLVMBasicBlockRef join   = LLVMAppendBasicBlock(current_fn, "mex_join");
            LLVMBuildCondBr(builder, is_vm, vm_bb, map_bb);

            LLVMPositionBuilderAtEnd(builder, vm_bb);
            LLVMValueRef handle_g = LLVMGetNamedGlobal(module, "__jdrt_handle");
            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, handle_g, "rt");
            auto& oex = runtime_funcs["__jdrt_obj_exists"];
            LLVMValueRef va[] = { rt, mtv.val, kptr };
            LLVMValueRef vres = LLVMBuildCall2(builder, oex.fn_type, oex.fn, va, 3, "oex");
            LLVMBuildBr(builder, join);
            LLVMBasicBlockRef vm_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, map_bb);
            LLVMValueRef mptr = LLVMBuildIntToPtr(builder, mtv.val, i8_ptr_type, "itoptr");
            auto& mh = runtime_funcs["__map_has"];
            LLVMValueRef ma[] = { mptr, kptr };
            LLVMValueRef mres = LLVMBuildCall2(builder, mh.fn_type, mh.fn, ma, 2, "mhas");
            LLVMBuildBr(builder, join);
            LLVMBasicBlockRef map_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, join);
            LLVMValueRef phi = LLVMBuildPhi(builder, i64_type, "exres");
            LLVMValueRef vals[] = { vres, mres };
            LLVMBasicBlockRef blks[] = { vm_end, map_end };
            LLVMAddIncoming(phi, vals, blks, 2);
            return { phi, JD_TAG_I64 };
        }
    }

    // Handle VAL with pointer-encoded doubles (from OS.ARGS array elements)
    if (upper == "VAL" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == JD_TAG_F64) {
            // f64 value — might be a pointer-encoded string from OS.ARGS
            auto& fn = runtime_funcs["__val_ptr"];
            LLVMValueRef args[] = { av.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "val");
            return { result, JD_TAG_F64 };
        }
    }

    // 3. Try runtime builtin (uppercase lookup)
    // (Vectorization for array args was handled in the block above.)
    // Route TXTREADER$/TXTWRITER to the codepage-aware variant when the user
    // supplied an encoding argument. Detection is by arg count: the
    // pass-through forms take 1 (TXTREADER$) and 2-3 (TXTWRITER) args; one
    // more positional arg => encoding string at the tail.
    if (upper == "TXTREADER$" && expr.args.size() == 2) upper = "__TXTREADER_ENC";
    else if (upper == "TXTWRITER" && expr.args.size() == 4) upper = "__TXTWRITER_ENC";
    // COPYV(dst, scalar) → reroute to FILLV (= scalar broadcast). Detection
    // by inferred type of arg[1]: if not ARRAY-typed, treat as fill.
    else if (upper == "COPYV" && expr.args.size() == 2 && expr.args[1]) {
        StaticType src_ty = infer_expr_type(*expr.args[1]);
        if (src_ty.kind != StaticType::Kind::ARRAY &&
            src_ty.kind != StaticType::Kind::UNKNOWN) {
            upper = "FILLV";
        }
    }
    auto rit = runtime_funcs.find(upper);
    if (rit != runtime_funcs.end() &&
        expr.args.size() <= LLVMCountParamTypes(rit->second.fn_type)) {
        // If the user passed MORE args than the direct binding accepts, fall
        // through to the VM bridge so optional tail args (e.g. an optional TZ
        // on CVDATE/FORMAT_DATE) are honoured instead of silently dropped.
        auto& rf = rit->second;
        std::vector<LLVMValueRef> args;
        unsigned param_count = LLVMCountParamTypes(rf.fn_type);
        std::vector<LLVMTypeRef> param_types(param_count);
        if (param_count > 0) LLVMGetParamTypes(rf.fn_type, param_types.data());

        for (size_t i = 0; i < expr.args.size() && i < param_count; i++) {
            // Propagate the parameter's expected LLVM type as an INDEX-leaf
            // hint so `SCREEN SW, SH, game{"title"}` asks the runtime for
            // f64 / f64 / str instead of all-strings.
            // Only set the hint for numeric params (f64/i64). Setting it
            // for i8_ptr is ambiguous — the LLVM type doesn't distinguish
            // a string-arg builtin (STR$, CONCAT) from an array-arg one
            // (JOIN, PUSH, SORT). For i8_ptr we let the arg codegen pick
            // the tagged path; coerce_to handles the resulting RUNTIME tag.
            LLVMTypeRef pt = param_types[i];
            int param_hint = -1;
            if (pt == f64_type || pt == i64_type) param_hint = JD_TAG_F64;
            // RAII-scoped: the per-arg hint never leaks past the codegen call,
            // so a builtin used as an outer INDEX's idx (e.g. `m{"k"}[ABS(x)]`)
            // can't clobber the outer's pending hint.
            TypedValue av;
            {
                ScopedLeafTag _lt(this, param_hint);
                av = codegen_expr(*expr.args[i]);
            }
            args.push_back(coerce_to(av, param_types[i]));
        }
        // Pad any unsupplied parameters with type-appropriate defaults so the
        // call is well-formed. -1 is the "rest of string / open length"
        // sentinel honoured by jdb_mid/jdb_left/jdb_right etc. FORMAT_DATE
        // uses NaN on the tz arg to signal "no tz specified → use localtime",
        // distinguishing it from an explicit tz=0 (UTC). TXTWRITER's append
        // flag and __TXTWRITER_ENC's append slot must default to 0 (no
        // append), not -1 (which is truthy and would silently switch to
        // append mode for 2-arg / 3-arg calls).
        bool i64_default_zero = (upper == "TXTWRITER" || upper == "__TXTWRITER_ENC");
        for (size_t i = expr.args.size(); i < param_count; i++) {
            LLVMTypeRef t = param_types[i];
            if (t == i64_type) {
                int64_t pad = i64_default_zero ? 0 : -1;
                args.push_back(LLVMConstInt(i64_type, pad, 1));
            }
            else if (t == f64_type) {
                if (upper == "FORMAT_DATE")
                    args.push_back(LLVMConstReal(f64_type, std::nan("")));
                else
                    args.push_back(LLVMConstReal(f64_type, 0.0));
            }
            else args.push_back(LLVMConstNull(t));
        }

        if (rf.return_tag == -1) {
            LLVMBuildCall2(builder, rf.fn_type, rf.fn,
                           args.empty() ? nullptr : args.data(),
                           (unsigned)args.size(), "");
            return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
        } else {
            LLVMValueRef result = LLVMBuildCall2(builder, rf.fn_type, rf.fn,
                                                  args.empty() ? nullptr : args.data(),
                                                  (unsigned)args.size(), "call");
            // APPEND(arr, <string scalar>) stores the string ptr bit-punned but
            // UNTAGGED via jdb_array_append. The codegen-side string_array_vars
            // flag lets arr[i] reads decode, but the element carries no runtime
            // JdTag — so passing the array through the VM bridge into a
            // register_native (TUI.MENU/TEXT doing arr->elements[i].as_string())
            // reads blank. Mark the result's elements as strings, mirroring the
            // ARRAY_LITERAL string path, so the tag travels with the array.
            if (upper == "APPEND" && expr.args.size() >= 2 && expr.args[1]) {
                const Expr* v = expr.args[1].get();
                bool val_is_string = false;
                if (v->kind == ExprKind::LITERAL_STRING) val_is_string = true;
                else if (v->kind == ExprKind::VARIABLE)
                    val_is_string = (!v->str_val.empty() && v->str_val.back() == '$') ||
                                    string_scalar_vars.count(v->str_val) != 0;
                else if (v->kind == ExprKind::CALL) {
                    std::string fu = v->func_name;
                    std::transform(fu.begin(), fu.end(), fu.begin(), ::toupper);
                    val_is_string = (!v->func_name.empty() && v->func_name.back() == '$') ||
                                    fu == "JOIN";
                }
                if (val_is_string) {
                    auto* set_str = get_runtime_func("__arr_set_string_elems");
                    if (set_str) {
                        LLVMValueRef ss[] = { result };
                        LLVMBuildCall2(builder, set_str->fn_type, set_str->fn, ss, 1, "");
                    }
                }
            }
            return { result, rf.return_tag };
        }
    }

    // 3. Fallback: call through VM bridge DLL with type tags
    {
        LLVMValueRef handle_global = LLVMGetNamedGlobal(module, "__jdrt_handle");
        if (handle_global) {
            LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type, handle_global, "rt");
            LLVMValueRef name_str = LLVMBuildGlobalStringPtr(builder, upper.c_str(), ".fn");

            int nargs = (int)expr.args.size();
            LLVMValueRef args_ptr, tags_ptr;

            if (nargs > 0) {
                // Allocate i64[] for args and i32[] for type tags.
                // Hoist these allocas to the function's entry block so
                // they don't stack up when this call site sits inside a
                // loop — a non-entry-block alloca allocates fresh stack
                // every loop iteration and never releases until the
                // function returns, which overflows the stack on long
                // render loops (observed in gfx_native_storm.jdb).
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(builder);
                LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(current_fn);
                LLVMValueRef first_instr = LLVMGetFirstInstruction(entry_bb);
                if (first_instr) LLVMPositionBuilderBefore(builder, first_instr);
                else             LLVMPositionBuilderAtEnd(builder, entry_bb);

                args_ptr = LLVMBuildArrayAlloca(builder, i64_type,
                    LLVMConstInt(i32_type, nargs, 0), "args");
                tags_ptr = LLVMBuildArrayAlloca(builder, i32_type,
                    LLVMConstInt(i32_type, nargs, 0), "tags");

                LLVMPositionBuilderAtEnd(builder, cur_bb);

                for (int i = 0; i < nargs; i++) {
                    // Bridge dispatch resolves arg types at runtime — an
                    // outer leaf-tag hint (e.g. `k_idx = GUI.COMBO(...)`
                    // with k_idx INTEGER setting leaf=I64) must NOT leak
                    // into the inner map-get for `vstate{"items"}`. Without
                    // this, the F64 fast-path stripped the ARR tag and the
                    // bridge handed the bridge a numeric Value, so GUI.COMBO
                    // saw an empty array.
                    TypedValue av;
                    {
                        ScopedLeafTag _lt(this, -1);
                        av = codegen_expr(*expr.args[i]);
                    }

                    // Encode every arg into an i64 slot and pick a wire
                    // tag. Only tags the bridge decodes are emitted here;
                    // tag 4 (native JdbMap*) and tag 5 (funcref) have no
                    // wire representation and fall back to raw i64 bits.
                    LLVMValueRef encoded;
                    int32_t tag;

                    if (av.tag == JD_TAG_STR) {
                        encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "stoi");
                        tag = JD_TAG_STR;
                    } else if (av.tag == JD_TAG_ARR) {
                        encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "atoi");
                        tag = JD_TAG_ARR;
                    } else if (av.tag == JD_TAG_NATIVE_MAP) {
                        // Native MAP → box into a VM_HANDLE before
                        // crossing the bridge so the receiver sees a
                        // proper Value::OBJECT (with all entries copied
                        // over) instead of the punned JdbMap pointer.
                        // Without this, e.g. CHAN.SEND would store the
                        // raw pointer as Value::make_i64(ptr), and any
                        // RECVer would see junk.
                        auto* boxer = get_runtime_func("__jdrt_map_to_handle");
                        if (boxer) {
                            LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                            LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                            LLVMValueRef bargs[] = { rt, av.val };
                            encoded = LLVMBuildCall2(builder, boxer->fn_type, boxer->fn,
                                                      bargs, 2, "mtoh");
                            tag = JD_TAG_VM_HANDLE;
                        } else {
                            encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "mptoi");
                            tag = JD_TAG_I64;
                        }
                    } else if (av.tag == JD_TAG_VM_HANDLE) {
                        encoded = av.val;
                        tag = JD_TAG_VM_HANDLE;
                    } else if (av.tag == JD_TAG_F64) {
                        encoded = pun_f64_to_i64(av.val);
                        tag = JD_TAG_F64;
                    } else if (av.tag == JD_TAG_RUNTIME && av.runtime_tag) {
                        encoded = av.val;
                        // Wire tag comes from the runtime_tag alloca below.
                    } else {
                        encoded = av.val;
                        tag = JD_TAG_I64;
                    }

                    LLVMValueRef aidx[] = { LLVMConstInt(i32_type, i, 0) };
                    LLVMValueRef aptr = LLVMBuildGEP2(builder, i64_type, args_ptr, aidx, 1, "arg");
                    LLVMBuildStore(builder, encoded, aptr);

                    LLVMValueRef tidx[] = { LLVMConstInt(i32_type, i, 0) };
                    LLVMValueRef tptr = LLVMBuildGEP2(builder, i32_type, tags_ptr, tidx, 1, "tag");
                    if (av.tag == JD_TAG_RUNTIME && av.runtime_tag) {
                        LLVMBuildStore(builder, av.runtime_tag, tptr);
                    } else {
                        LLVMBuildStore(builder, LLVMConstInt(i32_type, tag, 0), tptr);
                    }
                }
            } else {
                args_ptr = LLVMConstNull(i8_ptr_type);
                tags_ptr = LLVMConstNull(i8_ptr_type);
            }

            // Each bridged function routes to a __jdrt_call_typed_*
            // variant based on its return shape. Wrong classification
            // here silently corrupts values (e.g. a str returner fed to
            // _typed_f64 would get to_double()'d), so the name sets below
            // are the authoritative source.
            static const std::unordered_set<std::string> string_returners = {
                "GFX.POLLEVENT", "GFX.WAITEVENT", "INKEY$", "CLIPBOARD.GET$",
                "INPUT$", "INPUTKEY$", "SOUND.STATS",
                "DATE.UTC",
                // VM returns DATE Values which the bridge stringifies. These
                // have direct bindings too, but the bridge path is taken when
                // the user supplies the optional tz arg (n>direct arity).
                "CVDATE", "CDATE", "DATEADD", "FORMAT_DATE",
                // GUI.INPUT returns the (possibly edited) text-field content
                // as a string; without this it gets routed through
                // jdrt_call_typed_f64 and the result comes back as 0.0 —
                // every text field shows "0" and edits never persist. The
                // INT/DOUBLE variants stay numeric and don't need an entry.
                "GUI.INPUT",
                // TUI.INPUT mirrors GUI.INPUT — string return value, would
                // otherwise be dropped by the bridge's default f64 path.
                "TUI.INPUT"
            };
            bool is_string_fn = (!upper.empty() && upper.back() == '$') ||
                                string_returners.count(upper) ||
                                ffi_string_returners.count(upper);

            static const std::unordered_set<std::string> object_returners = {
                "JSON.PARSE$", "TILED.PROPERTIES", "TILED.OBJECTS",
                "MAP.FROM", "MAP.COPY", "GROUPBY",
                "FILE.STAT", "DATE.PARTS",
                "HTTP.REQUEST",
                "OS.EXEC",
                "SVD", "QR", "EIG",
                // Channel RECV returns whatever Value the producer sent —
                // could be i64, f64, string, array, map, or the EOF marker.
                // VM_HANDLE keeps the tag intact so CHAN.IS_EOF and the
                // FOR EACH polymorphic dispatch can recognise it later.
                "CHAN.RECV",
                // AWAIT / THREAD.GETRESULT yield the awaited task's actual
                // Value — could be any type, so route through VM_HANDLE so
                // strings + arrays + maps survive intact. Without this,
                // AWAIT was hitting the f64 fallback and stringifying via
                // to_double() = 0.0, killing string-returning ASYNC funcs.
                "AWAIT", "THREAD.GETRESULT",
                // MAT4.* returns a TENSOR Value; route through VM_HANDLE so
                // the 16-element flat doesn't get unboxed into a scalar.
                "MAT4.IDENTITY", "MAT4.PERSPECTIVE", "MAT4.LOOKAT",
                "MAT4.TRANSLATE", "MAT4.ROTATE", "MAT4.SCALE", "MAT4.MUL",
            };
            bool is_object_fn = object_returners.count(upper);

            static const std::unordered_set<std::string> array_returners = {
                "SPLIT", "KEYS", "VALUES", "SORTBY",
                "REGEX.FINDALL", "REGEX_MATCH", "REGEX_FINDALL",
                "OS.LIST", "OS.ARGS",
                "MAP.KEYS", "MAP.VALUES", "MAP.ITEMS",
                "LINES", "WORDS", "CHARS", "UNPACK",
                "TILED.SIZE", "TILED.TILE_SIZE", "TILED.LAYERS$",
                "GFX.HSV_RGB", "GFX.TEXTSIZE",
                "SPRITE.COLLISIONS",
                "CHUNK", "ENUMERATE", "TAKE_WHILE", "DROP_WHILE",
                "DIR$",
                // APL-style array primitives that lack a dedicated native
                // runtime function and fall through to the VM bridge.
                // Without a tag here, the bridge dispatch was treating
                // them as f64-returning and the result came back as an
                // empty / garbage array.
                "SHIFT", "OUTER", "ROTATE", "INVERT", "CONVOLVE", "PLACE",
                "MATMUL", "RESHAPE", "SLICE", "STACK", "MVLET",
                "ZIP", "TRANSPOSE", "SOLVE", "HISTOGRAM", "INTEGRATE",
                "FFT", "IFFT",
                "XSORT", "TAKE", "DROP",
                // APL-style scan / generators / shape ops. Without these
                // tags the bridge classifies the result as f64 and the
                // downstream array operators silently degrade to scalar
                // arithmetic — e.g. `signs = 1 - 2 * (CUMSUM(flips) MOD 2)`
                // collapses to a single number, so SOUND.PLAYBUFFER then
                // refuses its first argument as "not an array".
                "IOTA", "CUMSUM", "CUMPROD", "FLATTEN", "RANGE",
                "REVERSE", "UNIQUE", "SHUFFLE", "GRADE", "ARGMAX",
                "NORMALIZE", "DIFF", "APPEND",
                // Array of strings from common helpers
                "TILED.LAYERS", "FILE.LIST",
                // File I/O readers that return parsed-row arrays
                "CSVREADER"
            };
            bool is_array_fn = array_returners.count(upper) ||
                               ffi_array_returners.count(upper);
            bool is_void_fn  = ffi_void_returners.count(upper);

            LLVMValueRef call_args[] = { handle, name_str, args_ptr, tags_ptr,
                LLVMConstInt(i32_type, nargs, 0) };

            if (is_void_fn) {
                auto& fn = runtime_funcs["__jdrt_call_typed_void"];
                LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "");
                return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
            } else if (is_array_fn) {
                auto& fn = runtime_funcs["__jdrt_call_typed_arr"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmarr");
                return { result, JD_TAG_ARR };
            } else if (is_object_fn) {
                // Returned i64 looks identical to a native JdbMap* but is
                // a value_store handle — the VM_HANDLE tag routes field
                // access through jdrt_obj_* instead of jdb_map_*.
                auto& fn = runtime_funcs["__jdrt_call_typed_obj"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmobj");
                return { result, JD_TAG_VM_HANDLE };
            } else if (is_string_fn) {
                auto& fn = runtime_funcs["__jdrt_call_typed_str"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmcall");
                return { result, JD_TAG_STR };
            } else {
                auto& fn = runtime_funcs["__jdrt_call_typed_f64"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmcall");
                return { result, JD_TAG_F64 };
            }
        }
    }

    return { LLVMConstInt(i64_type, 0, 0), JD_TAG_I64 };
}

// ── Helpers ─────────────────────────────────────────────────

LLVMValueRef LLVMCodegen::to_i1(TypedValue tv) {
    if (tv.tag == JD_TAG_RUNTIME) {
        // Runtime-tagged: coerce to f64, then compare != 0
        LLVMValueRef f = coerce_to(tv, f64_type);
        return LLVMBuildFCmp(builder, LLVMRealONE, f,
                             LLVMConstReal(f64_type, 0.0), "dyn_tobool");
    }
    if (tv.tag == JD_TAG_F64)
        return LLVMBuildFCmp(builder, LLVMRealONE, tv.val,
                             LLVMConstReal(f64_type, 0.0), "tobool");
    if (tv.tag == JD_TAG_ARR) {
        // Array → ALL semantics: true iff every element is non-zero.
        auto it = runtime_funcs.find("ALL");
        if (it != runtime_funcs.end()) {
            LLVMValueRef args[] = { tv.val };
            LLVMValueRef all = LLVMBuildCall2(builder, it->second.fn_type,
                                               it->second.fn, args, 1, "all");
            return LLVMBuildICmp(builder, LLVMIntNE, all,
                                  LLVMConstInt(i64_type, 0, 0), "tobool");
        }
    }
    if (tv.tag == JD_TAG_STR) {
        // Strings: non-null and non-empty = true
        LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, tv.val, i64_type, "ptoi");
        return LLVMBuildICmp(builder, LLVMIntNE, as_i64,
                             LLVMConstInt(i64_type, 0, 0), "tobool");
    }
    return LLVMBuildICmp(builder, LLVMIntNE, tv.val,
                         LLVMConstInt(i64_type, 0, 0), "tobool");
}

LLVMCodegen::TypedValue LLVMCodegen::promote_to_f64(TypedValue tv) {
    if (tv.tag == JD_TAG_F64) return tv;
    // BOOL is bit-identical to I64 (0/1) — same conversion path.
    if (tv.tag == JD_TAG_I64 || tv.tag == JD_TAG_BOOL)
        return { LLVMBuildSIToFP(builder, tv.val, f64_type, "itof"), JD_TAG_F64 };
    return { LLVMConstReal(f64_type, 0.0), JD_TAG_F64 };
}

LLVMValueRef LLVMCodegen::pun_i64_to_f64(LLVMValueRef i64_val) {
    // Type-pun via entry-block alloca (always i64-typed, load as f64 through opaque ptr)
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(current_fn);
    // Insert alloca at start of entry block
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    if (first) LLVMPositionBuilderBefore(builder, first);
    else LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef alloca = LLVMBuildAlloca(builder, i64_type, "pun");
    LLVMPositionBuilderAtEnd(builder, cur);
    // Store i64, load as f64 (LLVM opaque ptr allows mismatched load type)
    LLVMBuildStore(builder, i64_val, alloca);
    return LLVMBuildLoad2(builder, f64_type, alloca, "pf");
}

LLVMValueRef LLVMCodegen::pun_f64_to_i64(LLVMValueRef f64_val) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(current_fn);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    if (first) LLVMPositionBuilderBefore(builder, first);
    else LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef alloca = LLVMBuildAlloca(builder, f64_type, "pun");
    LLVMPositionBuilderAtEnd(builder, cur);
    LLVMBuildStore(builder, f64_val, alloca);
    return LLVMBuildLoad2(builder, i64_type, alloca, "pi");
}

bool LLVMCodegen::is_udt_string_field(const std::string& var_name, const std::string& field_name) {
    auto tit = var_udt_type.find(var_name);
    if (tit == var_udt_type.end()) return false;
    auto uit = udt_types.find(tit->second);
    if (uit == udt_types.end()) return false;
    for (auto& f : uit->second) {
        if (f.name == field_name && f.is_string) return true;
    }
    return false;
}

bool LLVMCodegen::expr_involves_strings(const Expr& e) {
    // Used to infer whether a `RETURN <expr>` makes the surrounding
    // FUNC string-returning. We must look at what the EXPRESSION
    // evaluates to, NOT what's nested inside. Recursing into CALL
    // args was wrong: ASC("!") has a string literal argument but
    // returns int — yet recursing flagged the whole FUNC as string-
    // returning, so the codegen made `ret_type = ptr`. PRINT later
    // tried to %s-format an int-as-pointer and segfaulted.
    if (e.kind == ExprKind::LITERAL_STRING) return true;
    if (e.kind == ExprKind::VARIABLE && !e.str_val.empty() && e.str_val.back() == '$') return true;
    if (e.kind == ExprKind::CALL && !e.func_name.empty() && e.func_name.back() == '$') return true;
    // String concatenation: any arm being a string makes the result string.
    if (e.kind == ExprKind::BINARY && e.op == TokenType::PLUS) {
        if (e.left && expr_involves_strings(*e.left)) return true;
        if (e.right && expr_involves_strings(*e.right)) return true;
    }
    return false;
}

LLVMValueRef LLVMCodegen::coerce_to(TypedValue tv, LLVMTypeRef target) {
    // Runtime-tagged value (tag 7): branch on runtime tag to pick the
    // right conversion. Three cases matter:
    //   STR (2)        — pun i64 back to char* (or VAL→f64)
    //   VM_HANDLE (6)  — i64 is a value_store key; deref via jdrt_val_*
    //   anything else  — i64 holds f64 bits (or numeric)
    if (tv.tag == JD_TAG_RUNTIME && tv.runtime_tag) {
        LLVMValueRef is_str = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
            LLVMConstInt(i32_type, 2, 0), "dyn_isstr");
        LLVMValueRef is_arr = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
            LLVMConstInt(i32_type, 3, 0), "dyn_isarr");
        LLVMValueRef is_vmh = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
            LLVMConstInt(i32_type, 6, 0), "dyn_isvmh");
        LLVMBasicBlockRef bb_str = LLVMAppendBasicBlock(current_fn, "dyn.str");
        LLVMBasicBlockRef bb_chk_arr = LLVMAppendBasicBlock(current_fn, "dyn.chk_arr");
        LLVMBasicBlockRef bb_arr = LLVMAppendBasicBlock(current_fn, "dyn.arr");
        LLVMBasicBlockRef bb_chk_vmh = LLVMAppendBasicBlock(current_fn, "dyn.chk_vmh");
        LLVMBasicBlockRef bb_vmh = LLVMAppendBasicBlock(current_fn, "dyn.vmh");
        LLVMBasicBlockRef bb_other = LLVMAppendBasicBlock(current_fn, "dyn.other");
        LLVMBasicBlockRef bb_merge = LLVMAppendBasicBlock(current_fn, "dyn.merge");
        LLVMBuildCondBr(builder, is_str, bb_str, bb_chk_arr);
        LLVMPositionBuilderAtEnd(builder, bb_chk_arr);
        LLVMBuildCondBr(builder, is_arr, bb_arr, bb_chk_vmh);
        LLVMPositionBuilderAtEnd(builder, bb_chk_vmh);
        LLVMBuildCondBr(builder, is_vmh, bb_vmh, bb_other);

        if (target == f64_type) {
            LLVMPositionBuilderAtEnd(builder, bb_str);
            LLVMValueRef sptr = LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "sptr");
            auto* vfn = get_runtime_func("VAL");
            LLVMValueRef s2f = vfn
                ? LLVMBuildCall2(builder, vfn->fn_type, vfn->fn, &sptr, 1, "s2f")
                : LLVMConstReal(f64_type, 0.0);
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_str_end = LLVMGetInsertBlock(builder);

            // Array rtag in a numeric target is nonsensical; yield 0.
            LLVMPositionBuilderAtEnd(builder, bb_arr);
            LLVMValueRef arr_f = LLVMConstReal(f64_type, 0.0);
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_arr_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_vmh);
            auto* vtf = get_runtime_func("__jdrt_val_to_f64");
            LLVMValueRef vmh_f = LLVMConstReal(f64_type, 0.0);
            if (vtf) {
                LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef args[] = { rt, tv.val };
                vmh_f = LLVMBuildCall2(builder, vtf->fn_type, vtf->fn, args, 2, "vmh_f");
            }
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_vmh_end = LLVMGetInsertBlock(builder);

            // OTHER branch handles F64 (pun bits) AND I64/BOOL (SIToFP).
            // Without the I64-vs-F64 split, an int passed via tag-aware FUNC
            // ABI (val=42 i64, tag=I64) would have its bits pun'd as f64
            // and STR$(v) would format a denormal "0".
            LLVMPositionBuilderAtEnd(builder, bb_other);
            LLVMValueRef is_int = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_I64, 0), "dyn_isint");
            LLVMValueRef is_bool = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_BOOL, 0), "dyn_isbool");
            LLVMValueRef is_intish = LLVMBuildOr(builder, is_int, is_bool, "dyn_isintish");
            LLVMValueRef sitof = LLVMBuildSIToFP(builder, tv.val, f64_type, "dyn_i2f");
            LLVMValueRef pun_f = pun_i64_to_f64(tv.val);
            LLVMValueRef punned = LLVMBuildSelect(builder, is_intish, sitof, pun_f, "dyn_oth");
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_other_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_merge);
            LLVMValueRef phi = LLVMBuildPhi(builder, f64_type, "dyn_f64");
            LLVMValueRef vals[] = { s2f, arr_f, vmh_f, punned };
            LLVMBasicBlockRef bbs[] = { bb_str_end, bb_arr_end, bb_vmh_end, bb_other_end };
            LLVMAddIncoming(phi, vals, bbs, 4);
            return phi;
        }
        if (target == i8_ptr_type) {
            LLVMPositionBuilderAtEnd(builder, bb_str);
            LLVMValueRef sptr = LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "sptr");
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_str_end = LLVMGetInsertBlock(builder);

            // Array rtag: i64 holds a JdbArray* — return the raw ptr so
            // builtins that take an array (JOIN, PUSH, SORT, etc.) get the
            // real array. Consumers that want a stringified rendering
            // (PRINT / string-concat) call FRMV$ themselves; routing
            // everything through FRMV$ here makes JOIN crash because it
            // then receives a formatted string and reads its bytes as a
            // JdbArray length field.
            LLVMPositionBuilderAtEnd(builder, bb_arr);
            LLVMValueRef arr_s = LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "aptr");
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_arr_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_vmh);
            auto* vts = get_runtime_func("__jdrt_val_to_str");
            LLVMValueRef vmh_s = LLVMConstNull(i8_ptr_type);
            if (vts) {
                LLVMValueRef hg = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, hg, "rt");
                LLVMValueRef args[] = { rt, tv.val };
                vmh_s = LLVMBuildCall2(builder, vts->fn_type, vts->fn, args, 2, "vmh_s");
            }
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_vmh_end = LLVMGetInsertBlock(builder);

            // OTHER branch: F64 (pun then format) vs I64/BOOL (real int).
            // The tag-aware FUNC ABI passes a real i64 for I64/BOOL args,
            // so pun_i64_to_f64 would denormal-format them as "0".
            LLVMPositionBuilderAtEnd(builder, bb_other);
            LLVMValueRef is_int_s = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_I64, 0), "str_isint");
            LLVMValueRef is_bool_s = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_BOOL, 0), "str_isbool");
            LLVMValueRef is_intish_s = LLVMBuildOr(builder, is_int_s, is_bool_s, "str_isintish");
            LLVMValueRef f_pun  = pun_i64_to_f64(tv.val);
            LLVMValueRef f_real = LLVMBuildSIToFP(builder, tv.val, f64_type, "str_i2f");
            LLVMValueRef f      = LLVMBuildSelect(builder, is_intish_s, f_real, f_pun, "str_oth_f");
            auto& d2s = runtime_funcs["__double_to_str"];
            LLVMValueRef fstr = LLVMBuildCall2(builder, d2s.fn_type, d2s.fn, &f, 1, "f2s");
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_other_end = LLVMGetInsertBlock(builder);

            LLVMPositionBuilderAtEnd(builder, bb_merge);
            LLVMValueRef phi = LLVMBuildPhi(builder, i8_ptr_type, "dyn_str");
            LLVMValueRef vals[] = { sptr, arr_s, vmh_s, fstr };
            LLVMBasicBlockRef bbs[] = { bb_str_end, bb_arr_end, bb_vmh_end, bb_other_end };
            LLVMAddIncoming(phi, vals, bbs, 4);
            return phi;
        }
        if (target == i64_type) {
            // STR/ARR/VMH branches: i64 bits already (ptr-as-int / handle).
            LLVMPositionBuilderAtEnd(builder, bb_str);
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_str_end = LLVMGetInsertBlock(builder);
            LLVMPositionBuilderAtEnd(builder, bb_arr);
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_arr_end = LLVMGetInsertBlock(builder);
            LLVMPositionBuilderAtEnd(builder, bb_vmh);
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_vmh_end = LLVMGetInsertBlock(builder);
            // OTHER (F64/I64/BOOL). For I64/BOOL the i64 IS the int value
            // (passed through tag-aware FUNC ABI as a real i64); for F64
            // the i64 holds the f64-bit-pun and we need fpcast back to
            // int. Without the I64-vs-F64 split, an int 42 came through
            // as f64-pun denormal → FPToSI = 0.
            LLVMPositionBuilderAtEnd(builder, bb_other);
            LLVMValueRef is_int_i = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_I64, 0), "i64_isint");
            LLVMValueRef is_bool_i = LLVMBuildICmp(builder, LLVMIntEQ, tv.runtime_tag,
                LLVMConstInt(i32_type, JD_TAG_BOOL, 0), "i64_isbool");
            LLVMValueRef is_intish_i = LLVMBuildOr(builder, is_int_i, is_bool_i, "i64_isintish");
            LLVMValueRef as_f = pun_i64_to_f64(tv.val);
            LLVMValueRef from_f = LLVMBuildFPToSI(builder, as_f, i64_type, "f2i_dyn");
            LLVMValueRef as_i = LLVMBuildSelect(builder, is_intish_i, tv.val, from_f, "i64_oth");
            LLVMBuildBr(builder, bb_merge);
            LLVMBasicBlockRef bb_other_end = LLVMGetInsertBlock(builder);
            LLVMPositionBuilderAtEnd(builder, bb_merge);
            LLVMValueRef phi = LLVMBuildPhi(builder, i64_type, "dyn_i64");
            LLVMValueRef vals[] = { tv.val, tv.val, tv.val, as_i };
            LLVMBasicBlockRef bbs[] = { bb_str_end, bb_arr_end, bb_vmh_end, bb_other_end };
            LLVMAddIncoming(phi, vals, bbs, 4);
            return phi;
        }
    }
    if (target == f64_type) {
        // BOOL is bit-identical to I64 (0/1), so use the same conversion.
        if (tv.tag == JD_TAG_I64 || tv.tag == JD_TAG_BOOL)
            return LLVMBuildSIToFP(builder, tv.val, f64_type, "itof");
        if (tv.tag == JD_TAG_STR || tv.tag == JD_TAG_ARR || tv.tag == JD_TAG_NATIVE_MAP || tv.tag == JD_TAG_FUNCREF) {
            LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, tv.val, i64_type, "ptoi");
            return pun_i64_to_f64(as_i64);
        }
        // VM Value handle (tag 6) → call bridge to get the scalar the VM
        // sees. This materialises the real Value as double (0.0 if the
        // underlying Value isn't numeric).
        if (tv.tag == JD_TAG_VM_HANDLE) {
            auto* fn = get_runtime_func("__jdrt_val_to_f64");
            if (fn) {
                LLVMValueRef handle = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, handle, "rt");
                LLVMValueRef args[] = { rt, tv.val };
                return LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 2, "h2f");
            }
            return pun_i64_to_f64(tv.val);
        }
        return tv.val;
    }
    if (target == i64_type) {
        if (tv.tag == JD_TAG_F64) return LLVMBuildFPToSI(builder, tv.val, i64_type, "ftoi");
        if (tv.tag == JD_TAG_STR || tv.tag == JD_TAG_ARR || tv.tag == JD_TAG_NATIVE_MAP || tv.tag == JD_TAG_FUNCREF)
            return LLVMBuildPtrToInt(builder, tv.val, i64_type, "ptoi");
        if (tv.tag == JD_TAG_VM_HANDLE) {
            // VM handle is *already* an i64 value-store key — pass it through
            // unchanged. The previous "materialise as double, then FPToSI"
            // path lost the handle (the f64 form is the deref'd VALUE, not
            // the key) and was the silent cause of `RETURN handle` from
            // FUNCs returning JSON/MAP objects coming back as garbage in
            // the caller.
            return tv.val;
        }
        return tv.val;
    }
    if (target == i8_ptr_type) {
        if (tv.tag == JD_TAG_I64 || tv.tag == JD_TAG_BOOL) return LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "itoptr");
        if (tv.tag == JD_TAG_F64) {
            LLVMValueRef as_i64 = pun_f64_to_i64(tv.val);
            return LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "ftoptr");
        }
        if (tv.tag == JD_TAG_VM_HANDLE) {
            auto* fn = get_runtime_func("__jdrt_val_to_str");
            if (fn) {
                LLVMValueRef handle = LLVMGetNamedGlobal(module, "__jdrt_handle");
                LLVMValueRef rt = LLVMBuildLoad2(builder, i8_ptr_type, handle, "rt");
                LLVMValueRef args[] = { rt, tv.val };
                return LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 2, "h2s");
            }
            return LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "h2ptr");
        }
        return tv.val;
    }
    return tv.val;
}

LLVMCodegen::TypedValue LLVMCodegen::coerce_to_tag(TypedValue tv, int target_tag) {
    if (tv.tag == target_tag) return tv;
    if (target_tag == 1) return { coerce_to(tv, f64_type), JD_TAG_F64 };
    if (target_tag == 0) return { coerce_to(tv, i64_type), JD_TAG_I64 };
    if (target_tag == 2 || target_tag == 3) return { coerce_to(tv, i8_ptr_type), target_tag };
    return tv;
}

LLVMValueRef LLVMCodegen::to_string_ptr(TypedValue tv) {
    if (tv.tag == JD_TAG_STR) return tv.val;  // already a string ptr
    if (tv.tag == JD_TAG_I64 || tv.tag == JD_TAG_F64) {
        LLVMValueRef d = (tv.tag == JD_TAG_I64)
            ? LLVMBuildSIToFP(builder, tv.val, f64_type, "itof") : tv.val;
        auto* fn = get_runtime_func("__double_to_str");
        if (fn) {
            LLVMValueRef args[] = { d };
            return LLVMBuildCall2(builder, fn->fn_type, fn->fn, args, 1, "n2s");
        }
    }
    // Other ptr-typed values (array/map/lambda) — pass through as i8*.
    return coerce_to(tv, i8_ptr_type);
}

void LLVMCodegen::emit_trace(int line, const std::string& source_file) {
    if (!debug_log) return;
    auto* tr = get_runtime_func("__trace");
    if (!tr) return;
    // Extract basename for compact trace output.
    std::string label;
    if (!source_file.empty()) {
        size_t sep = source_file.find_last_of("/\\");
        label = (sep != std::string::npos) ? source_file.substr(sep + 1) : source_file;
        auto dot = label.rfind('.');
        if (dot != std::string::npos) label = label.substr(0, dot);
    }
    LLVMValueRef file_str = LLVMBuildGlobalStringPtr(builder, label.c_str(), ".trf");
    LLVMValueRef args[] = { file_str, LLVMConstInt(i64_type, line, 0) };
    LLVMBuildCall2(builder, tr->fn_type, tr->fn, args, 2, "");
}

void LLVMCodegen::emit_div_zero_check(TypedValue rhs) {
    LLVMValueRef is_zero;
    if (rhs.tag == JD_TAG_F64) {
        is_zero = LLVMBuildFCmp(builder, LLVMRealOEQ, rhs.val,
                                LLVMConstReal(f64_type, 0.0), "iszero");
    } else {
        LLVMValueRef rv = rhs.val;
        if (rhs.tag == JD_TAG_STR || rhs.tag == JD_TAG_ARR || rhs.tag == JD_TAG_NATIVE_MAP) {
            // Shouldn't happen for division but guard against ptr args
            return;
        }
        is_zero = LLVMBuildICmp(builder, LLVMIntEQ, rv,
                                LLVMConstInt(i64_type, 0, 0), "iszero");
    }
    LLVMBasicBlockRef zero_bb = LLVMAppendBasicBlock(current_fn, "divzero");
    LLVMBasicBlockRef ok_bb   = LLVMAppendBasicBlock(current_fn, "divok");
    LLVMBuildCondBr(builder, is_zero, zero_bb, ok_bb);

    LLVMPositionBuilderAtEnd(builder, zero_bb);
    LLVMValueRef msg = LLVMBuildGlobalStringPtr(builder, "Division by zero", ".dzmsg");
    auto& es = runtime_funcs["__err_set"];
    LLVMValueRef eargs[] = { msg, LLVMConstInt(i64_type, 1, 0) };
    LLVMBuildCall2(builder, es.fn_type, es.fn, eargs, 2, "");
    if (!try_stack.empty()) {
        LLVMBuildBr(builder, try_stack.back());
    } else {
        auto& uc = runtime_funcs["__throw_uncaught"];
        LLVMBuildCall2(builder, uc.fn_type, uc.fn, nullptr, 0, "");
        LLVMBuildUnreachable(builder);
    }
    LLVMPositionBuilderAtEnd(builder, ok_bb);
}

LLVMCodegen::RuntimeFunc* LLVMCodegen::get_runtime_func(const std::string& name) {
    auto it = runtime_funcs.find(name);
    if (it == runtime_funcs.end()) return nullptr;
    if (!it->second.fn) return nullptr;
    return &it->second;
}

// ── Object File Emission ────────────────────────────────────

// Forward-declare the New-PassManager C entry points. They are exported
// from LLVM-C.dll as of LLVM 17, but the bundled headers in libs/LLVM
// don't ship llvm-c/Transforms/PassBuilder.h — without this declaration
// we couldn't run the optimization pipeline and every DIM stayed as an
// alloca + load/store, leaving fib.exe ~1.5x slower than it had to be.
extern "C" {
    typedef struct LLVMOpaquePassBuilderOptions* LLVMPassBuilderOptionsRef;
    LLVMPassBuilderOptionsRef LLVMCreatePassBuilderOptions(void);
    void LLVMDisposePassBuilderOptions(LLVMPassBuilderOptionsRef Options);
    LLVMErrorRef LLVMRunPasses(LLVMModuleRef M, const char* Passes,
                                LLVMTargetMachineRef TM,
                                LLVMPassBuilderOptionsRef Options);
}

bool LLVMCodegen::emit_object_file(const std::string& obj_path) {
    // Initialize the host architecture's target. Linux/Windows x86_64 needs
    // X86Target; Apple Silicon / ARM64 Linux needs AArch64Target. Cheaper
    // than InitializeAllTargets, which would drag every backend (~10 MB of
    // static lib data) into the binary.
#if defined(__aarch64__) || defined(_M_ARM64)
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
#else
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
#endif

    char* triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char* err = nullptr;

    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        error_msg = "Failed to get target: " + std::string(err);
        LLVMDisposeMessage(err);
        LLVMDisposeMessage(triple);
        return false;
    }

    // Use host CPU + feature set so generated code can use SSE4 / AVX2 /
    // BMI etc. instead of the lowest-common-denominator "generic" target.
    char* host_cpu = LLVMGetHostCPUName();
    char* host_features = LLVMGetHostCPUFeatures();

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple,
        host_cpu ? host_cpu : "generic",
        host_features ? host_features : "",
        LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);

    if (host_cpu)      LLVMDisposeMessage(host_cpu);
    if (host_features) LLVMDisposeMessage(host_features);

    LLVMSetModuleDataLayout(module, LLVMCreateTargetDataLayout(machine));

    // Run the standard O2 optimization pipeline before emitting the object.
    // Promotes allocas to registers (mem2reg / sroa), runs instcombine /
    // gvn / simplifycfg / dead-store-elim / inliner. The per-call error-
    // pull preludes b57ec8e introduced (jdrt_last_error / jdb_err_code
    // checks after each runtime call) get dramatically thinned because
    // the optimizer can prove most of those branches are dead in straight-
    // line numeric code paths.
    LLVMPassBuilderOptionsRef pb_opts = LLVMCreatePassBuilderOptions();
    LLVMErrorRef pb_err = LLVMRunPasses(module, "default<O2>", machine, pb_opts);
    LLVMDisposePassBuilderOptions(pb_opts);
    if (pb_err) {
        char* msg = LLVMGetErrorMessage(pb_err);
        error_msg = "PassBuilder failed: " + std::string(msg ? msg : "(unknown)");
        LLVMDisposeErrorMessage(msg);
        LLVMDisposeTargetMachine(machine);
        LLVMDisposeMessage(triple);
        return false;
    }

    if (LLVMTargetMachineEmitToFile(machine, module, (char*)obj_path.c_str(),
                                     LLVMObjectFile, &err)) {
        error_msg = "Failed to emit object file: " + std::string(err);
        LLVMDisposeMessage(err);
        LLVMDisposeTargetMachine(machine);
        LLVMDisposeMessage(triple);
        return false;
    }

    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
    return true;
}

// ── Linking ─────────────────────────────────────────────────

bool LLVMCodegen::link_executable(const std::string& obj_path,
                                   const std::string& exe_path,
                                   const std::string& res_path) {
    // Find the precompiled runtime object. Win build emits .obj, Linux .o.
    // We probe both an in-tree dev layout (`build/`) and a flat layout
    // (next to whichever EXE invoked us — typical for redistributed bundles
    // where the user unpacks the zip and `cd`s into it before running -c).
    std::string runtime_obj;
    for (auto& candidate : {
            "build\\jdb_runtime.obj", "jdb_runtime.obj",
            "build/jdb_runtime.o",   "jdb_runtime.o" }) {
        if (std::filesystem::exists(candidate)) {
            runtime_obj = candidate;
            break;
        }
    }
    if (runtime_obj.empty()) {
        error_msg = "Cannot find jdb_runtime.{obj,o}. Build with NATIVEC flag first.";
        return false;
    }
#ifdef _WIN32
    // Same probe for the runtime's import library — `-c` used to hard-code
    // build\jdbrt.lib, which broke any flat-layout deployment.
    std::string jdbrt_lib;
    for (auto& candidate : {
            "build\\jdbrt.lib", "jdbrt.lib" }) {
        if (std::filesystem::exists(candidate)) {
            jdbrt_lib = candidate;
            break;
        }
    }
    if (jdbrt_lib.empty()) {
        error_msg = "Cannot find jdbrt.lib next to the runtime object. "
                    "Bundles must ship it alongside jdb_runtime.obj.";
        return false;
    }
#endif

#ifdef _WIN32
    // Discover MSVC + Windows SDK at runtime instead of hard-coding paths.
    // Works with VS 2017 / 2019 / 2022 (Community / Pro / Enterprise /
    // Build Tools). Discovery order:
    //   1) Env vars from a developer prompt (VCToolsInstallDir, WindowsSdkDir,
    //      WindowsSDKVersion).
    //   2) vswhere.exe at the standard Installer path. Multiple version-text
    //      filenames are tried (default.v143.txt, default.txt, etc.) and a
    //      direct directory scan of <vs>\VC\Tools\MSVC\ is the final fallback
    //      for non-2022 layouts.
    //   3) Windows SDK at the well-known `Program Files (x86)\Windows Kits\10`
    //      location, picking the newest version found under Lib\.
    // Every discovery step appends to a per-attempt log; if discovery fails
    // the log is included in error_msg so the deployment machine can see
    // exactly which paths were checked and what was/wasn't found.
    auto rstrip = [](std::string s) {
        while (!s.empty() && (s.back() == '\\' || s.back() == '/' ||
                              s.back() == '\r' || s.back() == '\n' ||
                              s.back() == ' ')) s.pop_back();
        return s;
    };

    std::string msvc;     // .../VC/Tools/MSVC/<version>
    std::string sdk;      // C:/Program Files (x86)/Windows Kits/10
    std::string sdkv;     // 10.0.26100.0
    std::string discovery_log;
    auto log = [&](const std::string& s) { discovery_log += "  " + s + "\n"; };

    // 1) Env-vars (developer prompt)
    if (const char* e = std::getenv("VCToolsInstallDir")) {
        msvc = rstrip(e);
        log("env VCToolsInstallDir=" + msvc);
    } else {
        log("env VCToolsInstallDir: not set");
    }
    if (const char* e = std::getenv("WindowsSdkDir"))     sdk  = rstrip(e);
    if (const char* e = std::getenv("WindowsSDKVersion")) sdkv = rstrip(e);

    // 2) vswhere.exe fallback for MSVC
    if (msvc.empty()) {
        const char* vswhere =
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (!std::filesystem::exists(vswhere)) {
            log(std::string("vswhere not found at ") + vswhere);
        } else {
            // Run vswhere with the given args and return its first stdout
            // line (an installationPath). Empty string = no match.
            auto run_vswhere = [&](const std::string& args) -> std::string {
                std::string cmd = std::string("\"\"") + vswhere + "\" " +
                                  args + " -property installationPath\"";
                FILE* p = _popen(cmd.c_str(), "r");
                std::string out;
                if (p) {
                    char buf[1024];
                    while (fgets(buf, sizeof(buf), p)) out += buf;
                    _pclose(p);
                    out = rstrip(out);
                }
                return out;
            };

            // Pass 1: filter on the historical C++ tools requires-key. Works
            // for VS2017/2019/2022.
            std::string vs_path = run_vswhere(
                "-latest -products * -requires Microsoft.VisualCpp.Tools.Core");

            // Pass 2: VS18 / VS2026 renamed the component, so the requires
            // filter returns empty even when MSVC is installed. Fall back to
            // an unfiltered query and let the downstream VC\Tools\MSVC scan
            // confirm the C++ tools are actually there.
            if (vs_path.empty()) {
                log("vswhere -requires Microsoft.VisualCpp.Tools.Core: empty");
                vs_path = run_vswhere("-latest -products *");
                if (!vs_path.empty()) {
                    log("vswhere -latest (no requires): " + vs_path);
                }
            }

            if (vs_path.empty()) {
                log("vswhere returned no installationPath");
            } else {
                log("vswhere installationPath=" + vs_path);

                // Try the well-known version text files in order. VS2022 uses
                // Microsoft.VCToolsVersion.default.txt; some installs only have
                // edition-suffixed variants (...v143.txt, ...v142.txt).
                std::string vc_version;
                const char* tried_files[] = {
                    "\\VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.default.txt",
                    "\\VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.v143.default.txt",
                    "\\VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.v142.default.txt",
                    "\\VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.v141.default.txt",
                };
                for (const char* suffix : tried_files) {
                    std::string f = vs_path + suffix;
                    std::ifstream ifs(f);
                    if (!ifs.is_open()) continue;
                    std::string v;
                    std::getline(ifs, v);
                    v = rstrip(v);
                    if (!v.empty()) {
                        vc_version = v;
                        log(std::string("read VC version ") + v +
                            " from " + suffix);
                        break;
                    }
                }

                // Fallback: scan VC\Tools\MSVC\ directly for version dirs and
                // pick the newest. Required for VS2019 layouts where the
                // .default.txt files live under different names per workload.
                std::string tools_root = vs_path + "\\VC\\Tools\\MSVC";
                if (vc_version.empty()) {
                    if (!std::filesystem::exists(tools_root)) {
                        log("VC tools dir missing: " + tools_root);
                    } else {
                        std::string newest;
                        try {
                            for (auto& e : std::filesystem::directory_iterator(tools_root)) {
                                if (!e.is_directory()) continue;
                                std::string n = e.path().filename().string();
                                // MSVC version dirs look like 14.xx.xxxxx
                                if (n.size() < 4 || n.compare(0, 3, "14.") != 0)
                                    continue;
                                if (n > newest) newest = n;
                            }
                        } catch (...) {}
                        if (!newest.empty()) {
                            vc_version = newest;
                            log("scanned VC\\Tools\\MSVC, newest=" + vc_version);
                        } else {
                            log("VC\\Tools\\MSVC has no 14.* version dirs");
                        }
                    }
                }

                if (!vc_version.empty()) {
                    msvc = vs_path + "\\VC\\Tools\\MSVC\\" + vc_version;
                }
            }
        }
    }

    // 3) Windows SDK fallback — pick newest version found under Lib.
    if (sdk.empty()) sdk = "C:\\Program Files (x86)\\Windows Kits\\10";
    if (sdkv.empty()) {
        std::string lib_root = sdk + "\\Lib";
        if (std::filesystem::exists(lib_root)) {
            std::string newest;
            try {
                for (auto& entry : std::filesystem::directory_iterator(lib_root)) {
                    if (!entry.is_directory()) continue;
                    std::string n = entry.path().filename().string();
                    // Expect "10.0.<build>.<rev>"
                    if (n.size() >= 5 && n.compare(0, 5, "10.0.") == 0 &&
                        n > newest) newest = n;
                }
            } catch (...) {
                // Permission errors etc. — leave sdkv empty so the helpful
                // error message below tells the user what to install.
            }
            sdkv = newest;
            if (!sdkv.empty()) log("Windows SDK newest=" + sdkv);
        } else {
            log("Windows SDK Lib dir missing: " + lib_root);
        }
    }

    if (msvc.empty()) {
        error_msg = "Cannot find MSVC toolchain. Install Visual Studio "
                    "2022 17.10 or newer (Community / Pro / Build Tools) "
                    "with the \"Desktop development with C++\" workload, "
                    "or run jdBasic.exe from an x64 Native Tools Command "
                    "Prompt. Discovery trace:\n" + discovery_log;
        return false;
    }
    if (sdkv.empty()) {
        error_msg = "Cannot find Windows SDK 10. Install the SDK via the VS "
                    "Installer (workload: Desktop development with C++). "
                    "Discovery trace:\n" + discovery_log;
        return false;
    }
    // Validate that link.exe actually exists at the discovered path.
    std::string link_exe = msvc + "\\bin\\Hostx64\\x64\\link.exe";
    if (!std::filesystem::exists(link_exe)) {
        error_msg = "MSVC link.exe not found at " + link_exe +
                    ". Discovered MSVC dir may be incomplete — reinstall VS "
                    "C++ tools. Discovery trace:\n" + discovery_log;
        return false;
    }

    std::string res_arg;
    if (!res_path.empty()) res_arg = "\"" + res_path + "\" ";

    std::string link_cmd =
        "cmd /c \"\"" + link_exe + "\" "
        "/NOLOGO /OUT:\"" + exe_path + "\" "
        "/SUBSYSTEM:CONSOLE "
        "\"" + obj_path + "\" "
        "\"" + runtime_obj + "\" "
        + res_arg +
        "/LIBPATH:\"" + msvc + "\\lib\\x64\" "
        "/LIBPATH:\"" + sdk + "\\Lib\\" + sdkv + "\\ucrt\\x64\" "
        "/LIBPATH:\"" + sdk + "\\Lib\\" + sdkv + "\\um\\x64\" "
        "libcmt.lib libucrt.lib kernel32.lib legacy_stdio_definitions.lib "
        "\"" + jdbrt_lib + "\"\"";

    int ret = std::system(link_cmd.c_str());
    if (ret != 0) {
        error_msg = "Linker failed (exit code " + std::to_string(ret) + ")";
        // LNK1120 = "N unresolved externals". The single most common cause
        // when -c works on the dev box but breaks on a deployment machine
        // is an MSVC version skew: jdb_runtime.obj was built against MSVC
        // v14.40+ (VS2022 17.10+) which emits vectorized-STL helper symbols
        // (__std_find_trivial_1, __std_find_last_of_trivial_pos_1, ...) that
        // older libcpmt.lib doesn't export. Surface the exact remedy.
        if (ret == 1120) {
            error_msg += "\n  → likely cause: MSVC version is too old. "
                         "jdb_runtime.obj requires MSVC v14.40+ "
                         "(Visual Studio 2022 17.10 or newer). If the "
                         "linker output above mentions __std_find_trivial_1 "
                         "or __std_find_last_of_trivial_pos_1, update VS "
                         "via the Visual Studio Installer.";
        }
        return false;
    }
    return true;
#else
    // POSIX: hand the pre-compiled runtime object plus the LLVM-generated obj
    // to g++. We don't link libjdbrt yet — that's only needed for programs
    // that call out to VM builtins (HTTP, GFX, etc.); pure arithmetic /
    // string / array programs are fully covered by jdb_runtime.o.
    (void)res_path;  // .res is Win-specific (VERSIONINFO resources)
    auto sh_quote = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
        out += "'";
        return out;
    };
    // -no-pie because LLVM emits non-PIC code by default and modern gcc
    // defaults to PIE — the resulting .obj has R_X86_64_32 relocations
    // that the linker rejects in PIE builds.
    // libjdbrt.so provides the VM C-API (jdrt_*). Embed three rpaths:
    //   $ORIGIN          — exe sits next to the .so (deployment case)
    //   $ORIGIN/build    — exe sits at the project root
    //   <abs-build-path> — absolute path of the build/ dir at compile
    //                      time, lets the generated exe run from any cwd
    std::string abs_build = std::filesystem::absolute("build").string();
    std::string link_cmd =
        "g++ -O2 -no-pie -o " + sh_quote(exe_path) + " "
        + sh_quote(obj_path) + " "
        + sh_quote(runtime_obj) + " "
        + "-Lbuild -ljdbrt "
        + "-Wl,-rpath,'$ORIGIN' "
        + "-Wl,-rpath,'$ORIGIN/build' "
        + "-Wl,-rpath," + sh_quote(abs_build) + " "
        + "-lm -lpthread -ldl";
    int ret = std::system(link_cmd.c_str());
    if (ret != 0) {
        error_msg = "Linker failed (exit code " + std::to_string(ret) + ")";
        return false;
    }
    return true;
#endif
}

// Parse <source>.props (key=value, # for comments). If present, write a temp
// .rc with VERSIONINFO + optional ICON, run rc.exe, return path to the .res.
// Empty return = no props file or generation skipped (link continues without).
std::string LLVMCodegen::generate_version_resource(const std::string& source_path,
                                                    const std::string& obj_path) {
    if (source_path.empty()) return "";
    std::string props_path = source_path + ".props";
    if (!std::filesystem::exists(props_path)) return "";

    std::ifstream in(props_path);
    if (!in) return "";

    std::map<std::string, std::string> props;
    std::string line;
    while (std::getline(in, line)) {
        // Strip CR, leading/trailing whitespace
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        if (line[s] == '#') continue;
        size_t eq = line.find('=', s);
        if (eq == std::string::npos) continue;
        std::string key = line.substr(s, eq - s);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        size_t vs = val.find_first_not_of(" \t");
        if (vs != std::string::npos) val = val.substr(vs);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
        // Allow optional surrounding quotes
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        props[key] = val;
    }

    auto get = [&](const char* k, const char* def) -> std::string {
        auto it = props.find(k);
        return (it != props.end() && !it->second.empty()) ? it->second : def;
    };

    // Parse "1.2.3.4" → "1, 2, 3, 4" for FILEVERSION/PRODUCTVERSION (need 4 ints).
    auto to_quad = [](std::string v) -> std::string {
        int parts[4] = {0,0,0,0};
        int i = 0;
        size_t p = 0;
        while (i < 4 && p <= v.size()) {
            size_t dot = v.find('.', p);
            std::string seg = v.substr(p, dot == std::string::npos ? std::string::npos : dot - p);
            try { parts[i++] = std::stoi(seg); } catch (...) { parts[i++] = 0; }
            if (dot == std::string::npos) break;
            p = dot + 1;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%d, %d, %d, %d", parts[0], parts[1], parts[2], parts[3]);
        return buf;
    };

    std::string file_ver_str    = get("FileVersion",    "1.0.0.0");
    std::string product_ver_str = get("ProductVersion", file_ver_str.c_str());
    std::string company         = get("CompanyName",      "");
    std::string description     = get("FileDescription",  "");
    std::string product_name    = get("ProductName",      "");
    std::string copyright       = get("LegalCopyright",   "");
    std::string original_name   = get("OriginalFilename", "");
    std::string internal_name   = get("InternalName",     original_name.c_str());
    std::string icon_path       = get("Icon",             "");

    // Escape backslashes for .rc string literals.
    auto rc_escape = [](std::string s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '\\' || c == '"') { out.push_back('\\'); out.push_back(c); }
            else out.push_back(c);
        }
        return out;
    };

    // Use obj_path's stem for temp files (same dir as the obj).
    std::string base = obj_path;
    auto dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    std::string rc_path  = base + ".jdb_props.rc";
    std::string res_path = base + ".jdb_props.res";

    {
        std::ofstream rc(rc_path);
        if (!rc) return "";
        if (!icon_path.empty() && std::filesystem::exists(icon_path)) {
            rc << "101 ICON \"" << rc_escape(icon_path) << "\"\n\n";
        }
        rc << "1 VERSIONINFO\n"
           << "FILEVERSION "    << to_quad(file_ver_str)    << "\n"
           << "PRODUCTVERSION " << to_quad(product_ver_str) << "\n"
           << "FILEFLAGSMASK 0x3fL\nFILEFLAGS 0x0L\nFILEOS 0x40004L\n"
           << "FILETYPE 0x1L\nFILESUBTYPE 0x0L\nBEGIN\n"
           << "  BLOCK \"StringFileInfo\"\n  BEGIN\n"
           << "    BLOCK \"040904b0\"\n    BEGIN\n";
        auto emit = [&](const char* k, const std::string& v) {
            if (!v.empty())
                rc << "      VALUE \"" << k << "\", \"" << rc_escape(v) << "\"\n";
        };
        emit("CompanyName",      company);
        emit("FileDescription",  description);
        emit("FileVersion",      file_ver_str);
        emit("InternalName",     internal_name);
        emit("LegalCopyright",   copyright);
        emit("OriginalFilename", original_name);
        emit("ProductName",      product_name);
        emit("ProductVersion",   product_ver_str);
        rc << "    END\n  END\n  BLOCK \"VarFileInfo\"\n  BEGIN\n"
           << "    VALUE \"Translation\", 0x409, 1200\n"
           << "  END\nEND\n";
    }

    std::string sdk  = "C:\\Program Files (x86)\\Windows Kits\\10";
    std::string sdkv = "10.0.26100.0";
    std::string rc_cmd =
        "cmd /c \"\"" + sdk + "\\bin\\" + sdkv + "\\x64\\rc.exe\" "
        "/nologo /fo \"" + res_path + "\" \"" + rc_path + "\" >nul 2>&1\"";

    int ret = std::system(rc_cmd.c_str());
    std::remove(rc_path.c_str());
    if (ret != 0 || !std::filesystem::exists(res_path)) {
        std::cerr << "Warning: rc.exe failed for " << props_path
                  << " (exit " << ret << ") — linking without version info." << std::endl;
        return "";
    }
    return res_path;
}

#endif // LLVM_CODEGEN
