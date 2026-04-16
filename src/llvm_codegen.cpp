#ifdef LLVM_CODEGEN
#include "llvm_codegen.h"
#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Analysis.h"
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
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
    LLVMTypeRef var_type = (tag == 1) ? f64_type :
                           (tag == 2) ? i8_ptr_type :
                           (tag == 3) ? i8_ptr_type : i64_type;

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
    reg("jdb_print_nl",     "__print_nl",      void_type, {}, -1);
    reg("jdb_print_space",  "__print_space",   void_type, {}, -1);

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
    reg("jdb_array_len",  "LEN",          i64_type, {i8_ptr_type}, 0);
    reg("jdb_iota",       "IOTA",         i8_ptr_type, {i64_type}, 3);
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
    reg("jdb_array_reverse","REVERSE",    i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_sort", "SORT",         i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_append","APPEND",      i8_ptr_type, {i8_ptr_type, f64_type}, 3);
    reg("jdb_array_count","COUNT",        i64_type, {i8_ptr_type, f64_type}, 0);
    reg("jdb_array_indexof","INDEXOF",    i64_type, {i8_ptr_type, f64_type}, 0);
    reg("jdb_array_unique","UNIQUE",      i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_cumsum","CUMSUM",      i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_cumprod","CUMPROD",    i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_array_take", "TAKE",         i8_ptr_type, {i8_ptr_type, i64_type}, 3);
    reg("jdb_array_drop", "DROP",         i8_ptr_type, {i8_ptr_type, i64_type}, 3);
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
    reg("jdb_array_cmp_arr",     "__arr_cmp_arr",     i8_ptr_type, {i8_ptr_type, i8_ptr_type, i32_type}, 3);
    reg("jdb_array_set_nested",  "__arr_set_nested",  void_type, {i8_ptr_type}, -1);
    reg("jdb_array_set_string_elems", "__arr_set_string_elems", void_type, {i8_ptr_type}, -1);
    // Native generic vectorization helpers (avoid VM bridge overhead)
    reg("jdb_array_apply_ff",   "__arr_apply_ff",   i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_ss",   "__arr_apply_ss",   i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_ifs",  "__arr_apply_ifs",  i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_sfi",  "__arr_apply_sfi",  i8_ptr_type, {i8_ptr_type, i64_type, i8_ptr_type}, 3);
    reg("jdb_array_apply_sfii", "__arr_apply_sfii", i8_ptr_type, {i8_ptr_type, i64_type, i64_type, i8_ptr_type}, 3);
    reg("jdb_array_len_shape",   "__arr_len_shape",   i8_ptr_type, {i8_ptr_type}, 3);
    reg("jdb_print_array_elem",  "__print_arr_elem",  void_type, {i8_ptr_type, i64_type}, -1);
    reg("jdb_array_str_concat",  "__arr_str_concat",  i8_ptr_type, {i8_ptr_type, i8_ptr_type, i32_type}, 3);
    reg("jdb_trace",             "__trace",           void_type, {i64_type}, -1);

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

    // String builtins
    reg("jdb_len_str",  "LEN$",     i64_type, {i8_ptr_type}, 0);
    reg("jdb_mid",      "MID$",     i8_ptr_type, {i8_ptr_type, i64_type, i64_type}, 2);
    reg("jdb_left",     "LEFT$",    i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_right",    "RIGHT$",   i8_ptr_type, {i8_ptr_type, i64_type}, 2);
    reg("jdb_upper",    "UPPER$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_lower",    "LOWER$",   i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_trim",     "TRIM$",    i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_chr",      "CHR$",     i8_ptr_type, {i64_type}, 2);
    reg("jdb_asc",      "ASC",      i64_type, {i8_ptr_type}, 0);
    reg("jdb_instr",    "INSTR",    i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    reg("jdb_replace",  "REPLACE$", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_str",      "STR$",     i8_ptr_type, {f64_type}, 2);
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
    reg("jdb_txtwriter",       "TXTWRITER",   void_type, {i8_ptr_type, i8_ptr_type}, -1);
    reg("jdb_txtwriter_append","TXTWRITER_APPEND", void_type, {i8_ptr_type, i8_ptr_type}, -1);
    reg("jdb_pwd",             "PWD",         i8_ptr_type, {}, 2);
    reg("jdb_cd",              "CD",          void_type, {i8_ptr_type}, -1);
    reg("jdb_mkdir_native",    "MKDIR",       void_type, {i8_ptr_type}, -1);
    reg("jdb_kill",            "KILL",        void_type, {i8_ptr_type}, -1);
    reg("jdb_file_exists",     "FILE.EXISTS", i64_type, {i8_ptr_type}, 0);

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
    reg("jdb_format_date", "FORMAT_DATE", i8_ptr_type, {f64_type, i8_ptr_type}, 2);

    // System
    reg("jdb_getenv",  "GETENV$",  i8_ptr_type, {i8_ptr_type}, 2);
    reg("jdb_iif",     "IIF",      f64_type, {i64_type, f64_type, f64_type}, 1);
    reg("jdb_isnum",   "ISNUM",    i64_type, {f64_type}, 0);

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

    // Date Add/Diff
    // Dates are ISO strings in the native runtime (not epochs like the VM).
    reg("jdb_dateadd",  "DATEADD",  i8_ptr_type, {i8_ptr_type, f64_type, i8_ptr_type}, 2);
    reg("jdb_datediff", "DATEDIFF", f64_type,    {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 1);
    reg("jdb_datediff_vec", "__datediff_vec", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 3);
    reg("jdb_cvdate",   "CVDATE",   i8_ptr_type, {i8_ptr_type}, 2);

    // Regex
    reg("jdb_regex_match",   "REGEX.MATCH",   i64_type, {i8_ptr_type, i8_ptr_type}, 0);
    // Note: REGEX_MATCH (legacy name) returns an array in the VM, so it must
    // go through the VM bridge — don't register it as the boolean native fn.
    reg("jdb_regex_replace", "REGEX.REPLACE",  i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_regex_replace", "REGEX_REPLACE$", i8_ptr_type, {i8_ptr_type, i8_ptr_type, i8_ptr_type}, 2);
    reg("jdb_regex_findall", "REGEX.FINDALL",  i8_ptr_type, {i8_ptr_type, i8_ptr_type}, 3);

    // TYPEOF (compile-time tag)
    reg("jdb_typeof_tag", "__typeof_tag", i8_ptr_type, {i64_type}, 2);

    // FRMV$ (format array)
    reg("jdb_frmv", "FRMV$", i8_ptr_type, {i8_ptr_type}, 2);

    // Misc
    reg("jdb_cdbl",     "CDBL",       f64_type, {f64_type}, 1);
    reg("jdb_tostr",    "TOSTR",      i8_ptr_type, {f64_type}, 2);
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
    }
}

// ── Pre-pass: declare all FUNC/SUB signatures ───────────────

void LLVMCodegen::declare_functions(const std::vector<StmtPtr>& program) {
    // Phase 1: collect function signatures with initial param types from name convention
    struct FuncDecl {
        const Stmt* stmt;
        std::vector<int> tags;  // per-param: 1=f64, 2=string
        int return_tag;
    };
    std::unordered_map<std::string, FuncDecl> decls;

    for (auto& stmt : program) {
        if (!stmt) continue;
        if (stmt->kind != StmtKind::FUNCTION && stmt->kind != StmtKind::SUB) continue;
        bool is_sub = (stmt->kind == StmtKind::SUB);
        bool returns_string = (!is_sub && !stmt->func_name.empty() &&
                               stmt->func_name.back() == '$');
        int ret_tag = is_sub ? -1 : (returns_string ? 2 : 1);
        std::vector<int> tags;
        for (auto& p : stmt->params) {
            bool sp = (!p.name.empty() && p.name.back() == '$');
            tags.push_back(sp ? 2 : 1);
        }
        decls[stmt->func_name] = { stmt.get(), tags, ret_tag };
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

    std::function<void(const Expr&)> scan_expr = [&](const Expr& e) {
        if (e.kind == ExprKind::CALL) {
            auto it = decls.find(e.func_name);
            if (it != decls.end()) {
                for (size_t i = 0; i < e.args.size() && i < it->second.tags.size(); i++) {
                    if (it->second.tags[i] != 2 && e.args[i] && is_str_expr(*e.args[i]))
                        it->second.tags[i] = 2;
                }
            }
        }
        if (e.left) scan_expr(*e.left);
        if (e.right) scan_expr(*e.right);
        for (auto& a : e.args) if (a) scan_expr(*a);
    };

    std::function<void(const Stmt&)> scan_stmt = [&](const Stmt& s) {
        if (s.expr) scan_expr(*s.expr);
        if (s.loop_cond) scan_expr(*s.loop_cond);
        if (s.end_expr) scan_expr(*s.end_expr);
        if (s.step_expr) scan_expr(*s.step_expr);
        for (auto& pe : s.print_exprs) if (pe) scan_expr(*pe);
        for (auto& ic : s.index_chain) if (ic) scan_expr(*ic);
        for (auto& b : s.body) if (b) scan_stmt(*b);
        for (auto& br : s.branches) {
            if (br.condition) scan_expr(*br.condition);
            for (auto& b : br.body) if (b) scan_stmt(*b);
        }
        for (auto& c : s.catch_body) if (c) scan_stmt(*c);
        for (auto& f : s.finally_body) if (f) scan_stmt(*f);
    };

    for (auto& stmt : program) {
        if (stmt) scan_stmt(*stmt);
    }

    // Phase 3: also infer return types from body if not indicated by name
    for (auto& [name, decl] : decls) {
        if (decl.return_tag == 1 && decl.stmt) {
            // Check if body involves string operations (like method return type inference)
            for (auto& s : decl.stmt->body) {
                if (s && s->expr && expr_involves_strings(*s->expr)) {
                    decl.return_tag = 2;
                    break;
                }
            }
        }
    }

    // Phase 4: create LLVM functions with inferred types
    for (auto& [name, decl] : decls) {
        std::vector<LLVMTypeRef> param_types;
        for (int t : decl.tags)
            param_types.push_back(t == 2 ? i8_ptr_type : f64_type);

        LLVMTypeRef ret_type;
        if (decl.return_tag == -1) ret_type = void_type;
        else if (decl.return_tag == 2) ret_type = i8_ptr_type;
        else ret_type = f64_type;

        LLVMTypeRef fn_type = LLVMFunctionType(ret_type,
            param_types.empty() ? nullptr : param_types.data(),
            (unsigned)param_types.size(), 0);
        LLVMValueRef fn = LLVMAddFunction(module, name.c_str(), fn_type);
        user_functions[name] = { fn, decl.return_tag, decl.tags };
    }
}

// ── Main Entry Points ───────────────────────────────────────

bool LLVMCodegen::compile(const std::vector<StmtPtr>& program,
                           const std::string& output_exe) {
    init_module();
    declare_runtime_functions();
    create_main_function();
    declare_functions(program);
    codegen_program(program);

    // Shutdown VM bridge before exit
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
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

    // Emit object file
    std::string obj_path = output_exe;
    auto dot = obj_path.rfind('.');
    if (dot != std::string::npos) obj_path = obj_path.substr(0, dot);
    obj_path += ".obj";

    if (!emit_object_file(obj_path)) return false;
    if (!link_executable(obj_path, output_exe)) return false;

    std::remove(obj_path.c_str());
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
            if (!lookup_var(stmt->var_name)) {
                // Determine type from initial expression
                // Recursive helper to infer expression result type
                std::function<int(const Expr*)> infer_tag = [&](const Expr* e) -> int {
                    if (!e) return 0;
                    if (e->kind == ExprKind::LITERAL_INT) return 0;
                    if (e->kind == ExprKind::LITERAL_FLOAT) return 1;
                    if (e->kind == ExprKind::LITERAL_STRING) return 2;
                    if (e->kind == ExprKind::ARRAY_LITERAL) return 3;
                    if (e->kind == ExprKind::CALL) {
                        if (e->func_name == "ZEROS" || e->func_name == "ONES" ||
                            e->func_name == "IOTA" || e->func_name == "RANGE" ||
                            e->func_name == "LINSPACE") return 3;
                        std::string upper = e->func_name;
                        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                        // If any arg is an array AND fn is not blocklisted → vectorized result (array)
                        // (Keep in sync with no_vectorize in codegen_call.)
                        static const std::unordered_set<std::string> no_vec_infer = {
                            "LEN","SUM","PRODUCT","MEAN","STDEV","MEDIAN","VARIANCE",
                            "MIN","MAX","ANY","ALL","COUNT","INDEXOF","REVERSE","SORT",
                            "TAKE","DROP","UNIQUE","APPEND","PUSH","POP","FLATTEN",
                            "TRANSPOSE","MATMUL","DOT","CROSS","CUMSUM","CUMPROD",
                            "SCAN","SELECT","FILTER","REDUCE","TYPEOF","IIF",
                            "ZEROS","ONES","IOTA","RANGE","LINSPACE","TENSOR","RESHAPE",
                            "SPLIT","JOIN","FORMAT$","FRMV$","PACK$","UNPACK",
                            "REGEX_MATCH","REGEX.MATCH","REGEX.FINDALL",
                            "NOW","CVDATE","DATE$","TIME$","TICK"
                        };
                        for (auto& a : e->args) {
                            if (a && infer_tag(a.get()) == 3 && !no_vec_infer.count(upper)) {
                                return 3;
                            }
                        }
                        // Known array-returning functions
                        static const std::unordered_set<std::string> arr_returners = {
                            "SPLIT", "KEYS", "VALUES", "SORTBY", "GROUPBY",
                            "REGEX.FINDALL", "REGEX_MATCH", "REGEX_FINDALL",
                            "OS.LIST", "OS.ARGS",
                            "MAP.KEYS", "MAP.VALUES", "LINES", "WORDS", "CHARS",
                            "UNPACK"
                        };
                        if (arr_returners.count(upper)) return 3;
                        if (!e->func_name.empty() && e->func_name.back() == '$') return 2;
                        auto rit = runtime_funcs.find(upper);
                        if (rit != runtime_funcs.end()) return rit->second.return_tag;
                        auto uit = user_functions.find(e->func_name);
                        if (uit != user_functions.end()) return uit->second.return_tag;
                        return -1;
                    }
                    if (e->kind == ExprKind::VARIABLE) {
                        VarInfo* v = lookup_var(e->str_val);
                        if (v) return v->tag;
                        if (!e->str_val.empty() && e->str_val.back() == '$') return 2;
                        return -1;
                    }
                    if (e->kind == ExprKind::BINARY) {
                        int lt = infer_tag(e->left.get());
                        int rt = infer_tag(e->right.get());
                        if (lt == 3 || rt == 3) return 3; // array op → array
                        if (lt == 2 || rt == 2) return 2; // string op → string
                        if (lt == 1 || rt == 1) return 1;
                        return 0;
                    }
                    if (e->kind == ExprKind::UNARY) return infer_tag(e->right.get());
                    return -1;
                };
                int tag = 0;
                if (stmt->kind == StmtKind::DIM && !stmt->label.empty() &&
                    type_names.count(stmt->label))
                    tag = 3;  // UDT object (ptr)
                else if (stmt->expr) {
                    int inferred = infer_tag(stmt->expr.get());
                    if (inferred >= 0) tag = inferred;
                }
                // Variables ending with $ are strings by convention
                if (tag == 0 && stmt->var_name.size() > 1 && stmt->var_name.back() == '$')
                    tag = 2;  // string
                create_var(stmt->var_name, tag);
            }
        }
    }

    // First pass: compile TYPE declarations (constructors + methods)
    for (auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::TYPE_DECL)
            codegen_type_decl(*stmt);
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

    // Emit runtime trace if enabled (--trace flag)
    if (stmt.line > 0)
        emit_trace(stmt.line);

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
        case StmtKind::TRY_CATCH:
            // TRY/CATCH: just execute the TRY body, skip catch for now
            for (auto& s : stmt.body) { if (s) codegen_stmt(*s); }
            break;
        case StmtKind::ENUM_DECL:
            codegen_enum(stmt);
            break;
        case StmtKind::TYPE_DECL:
            codegen_type_decl(stmt);
            break;
        case StmtKind::THROW_STMT:
            // TODO: proper exception throwing
            break;
        case StmtKind::SLEEP_STMT:
            if (stmt.expr) {
                TypedValue sv = codegen_expr(*stmt.expr);
                if (sv.tag == 1) sv.val = LLVMBuildFPToSI(builder, sv.val, i64_type, "ftoi");
                auto& fn = runtime_funcs["SLEEP"];
                LLVMValueRef args[] = { sv.val };
                LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "");
            }
            break;
        case StmtKind::END_STMT:
            LLVMBuildRet(builder, LLVMConstInt(i32_type, 0, 0));
            break;
        case StmtKind::EXIT_LOOP:
            if (!loop_stack.empty())
                LLVMBuildBr(builder, loop_stack.top().break_bb);
            break;
        case StmtKind::CONTINUE_LOOP:
            if (!loop_stack.empty())
                LLVMBuildBr(builder, loop_stack.top().continue_bb);
            break;
        default:
            break;
    }
    } catch (const std::exception& e) {
        std::cerr << "[NATIVE] Warning: codegen error at line " << stmt.line
                  << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[NATIVE] Warning: unknown codegen error at line " << stmt.line << std::endl;
    }
}

// ── FUNC / SUB ──────────────────────────────────────────────

void LLVMCodegen::codegen_function(const Stmt& stmt) {
    std::string fn_name = stmt.func_name;
    auto fit = user_functions.find(fn_name);
    if (fit == user_functions.end()) return;

    // Save current state
    LLVMValueRef saved_fn = current_fn;
    current_fn = fit->second.fn;

    // Push function scope
    scopes.push_back(Scope{});

    // Create entry block
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, current_fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    // Create allocas for parameters — use param_tags for types
    for (size_t i = 0; i < stmt.params.size() && i < fit->second.param_tags.size(); i++) {
        int ptag = fit->second.param_tags[i];
        VarInfo& vi = create_var(stmt.params[i].name, ptag);
        LLVMBuildStore(builder, LLVMGetParam(current_fn, (unsigned)i), vi.alloca_val);
    }

    // Compile body
    for (auto& s : stmt.body) {
        if (s) codegen_stmt(*s);
    }

    // If no terminator yet, add implicit return
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
        if (stmt.kind == StmtKind::SUB) {
            LLVMBuildRetVoid(builder);
        } else {
            auto fit2 = user_functions.find(stmt.func_name);
            if (fit2 != user_functions.end() && fit2->second.return_tag == 2)
                LLVMBuildRet(builder, LLVMConstNull(i8_ptr_type));  // return empty string
            else
                LLVMBuildRet(builder, LLVMConstReal(f64_type, 0.0));
        }
    }

    // Pop scope and restore
    scopes.pop_back();
    current_fn = saved_fn;

    // Position builder back at the end of the saved function's last block
    LLVMBasicBlockRef last_bb = LLVMGetLastBasicBlock(saved_fn);
    LLVMPositionBuilderAtEnd(builder, last_bb);
}

// ── RETURN ──────────────────────────────────────────────────

void LLVMCodegen::codegen_return(const Stmt& stmt) {
    if (stmt.expr) {
        TypedValue rv = codegen_expr(*stmt.expr);

        // Check what this function returns
        std::string fn_name = LLVMGetValueName(current_fn);
        auto fit = user_functions.find(fn_name);
        int expected_tag = (fit != user_functions.end()) ? fit->second.return_tag : 1;

        if (expected_tag == 2) {
            // String-returning function — return ptr
            LLVMBuildRet(builder, rv.val);
        } else {
            // Numeric function — return f64
            if (rv.tag == 0) rv.val = LLVMBuildSIToFP(builder, rv.val, f64_type, "itof");
            LLVMBuildRet(builder, rv.val);
        }
    } else {
        LLVMBuildRetVoid(builder);
    }
}

// ── LET / DIM / ASSIGN ─────────────────────────────────────

void LLVMCodegen::codegen_let_or_assign(const Stmt& stmt) {
    if (!stmt.expr) return;

    // Check for dotted UDT field assignment: Player1.Name = "Atomi"
    size_t dot_pos = stmt.var_name.find('.');
    if (dot_pos != std::string::npos) {
        std::string obj_name = stmt.var_name.substr(0, dot_pos);
        std::string field_name = stmt.var_name.substr(dot_pos + 1);
        auto tit = var_udt_type.find(obj_name);
        if (tit != var_udt_type.end()) {
            VarInfo* vi = lookup_var(obj_name);
            if (vi) {
                LLVMValueRef obj_ptr = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "obj");
                LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, field_name.c_str(), ".fld");
                TypedValue val = codegen_expr(*stmt.expr);
                bool is_str = val.tag == 2 || is_udt_string_field(obj_name, field_name);
                if (is_str) {
                    auto& set_fn = runtime_funcs["__udt_set_str"];
                    LLVMValueRef args[] = { obj_ptr, field_str, val.val };
                    LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
                } else {
                    LLVMValueRef fval = val.tag == 0
                        ? LLVMBuildSIToFP(builder, val.val, f64_type, "itof") : val.val;
                    auto& set_fn = runtime_funcs["__udt_set_f64"];
                    LLVMValueRef args[] = { obj_ptr, field_str, fval };
                    LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
                }
                return;
            }
        }
    }

    TypedValue rhs = codegen_expr(*stmt.expr);

    VarInfo* vi = lookup_var(stmt.var_name);
    if (vi) {
        if (vi->tag != rhs.tag) {
            if (vi->tag == 1 && rhs.tag == 0) {
                // Variable is f64, value is int → promote value
                rhs.val = LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof");
                rhs.tag = 1;
            } else if (vi->tag == 0 && rhs.tag == 1) {
                // Variable was int, value is float → truncate to int for storage
                rhs.val = LLVMBuildFPToSI(builder, rhs.val, i64_type, "ftoi");
                rhs.tag = 0;
            } else if ((rhs.tag == 3 || rhs.tag == 4) && vi->tag != rhs.tag) {
                // Array or VM-object handle — update variable tag
                if (rhs.tag == 4) {
                    // VM object handle is i64 — store directly
                    vi->tag = 4;
                    LLVMBuildStore(builder, rhs.val, vi->alloca_val);
                    return;
                }
                VarInfo& nv = create_var(stmt.var_name, 3);
                LLVMBuildStore(builder, rhs.val, nv.alloca_val);
                return;
            }
        }
        LLVMBuildStore(builder, rhs.val, vi->alloca_val);
    } else {
        // In functions, promote new numeric variables to f64 to avoid
        // type-upgrade issues when int vars later receive float values
        int var_tag = rhs.tag;
        if (var_tag == 0 && scopes.size() > 1) {
            rhs.val = LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof");
            var_tag = 1;
        }
        VarInfo& nv = create_var(stmt.var_name, var_tag);
        LLVMBuildStore(builder, rhs.val, nv.alloca_val);
    }
}

// ── DIM (array allocation) ──────────────────────────────────

void LLVMCodegen::codegen_dim(const Stmt& stmt) {
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
            VarInfo* vi = lookup_var(stmt.var_name);
            if (vi) {
                LLVMBuildStore(builder, obj, vi->alloca_val);
                vi->tag = 3;
            } else {
                VarInfo& nv = create_var(stmt.var_name, 3);
                LLVMBuildStore(builder, obj, nv.alloca_val);
            }
            var_udt_type[stmt.var_name] = stmt.label;
            return;
        }
    }

    if (!stmt.expr) {
        // DIM without value — create default var
        codegen_let_or_assign(stmt);
        return;
    }

    // DIM arr[N] AS TypeName → CALL("__MAKE_UDT_ARRAY__", [shape, "TypeName"])
    if (stmt.expr->kind == ExprKind::CALL && stmt.expr->func_name == "__MAKE_UDT_ARRAY__" &&
        stmt.expr->args.size() >= 2 && stmt.expr->args[0]->kind == ExprKind::ARRAY_LITERAL &&
        stmt.expr->args[1]->kind == ExprKind::LITERAL_STRING) {
        auto& shape_args = stmt.expr->args[0]->args;
        std::string type_name = stmt.expr->args[1]->str_val;
        auto ctor_it = user_functions.find(type_name);
        if (!shape_args.empty() && ctor_it != user_functions.end()) {
            auto& arr_new = runtime_funcs["__array_new"];
            auto& arr_append = runtime_funcs["APPEND"];
            LLVMTypeRef ctor_ft = LLVMGlobalGetValueType(ctor_it->second.fn);

            TypedValue size_val = codegen_expr(*shape_args[0]);
            LLVMValueRef n = size_val.tag == 1
                ? LLVMBuildFPToSI(builder, size_val.val, i64_type, "ftoi") : size_val.val;

            LLVMValueRef zero_args[] = { LLVMConstInt(i64_type, 0, 0) };
            LLVMValueRef outer = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, zero_args, 1, "outer");

            LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "udt_arr.loop");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "udt_arr.end");
            LLVMValueRef idx_alloca = LLVMBuildAlloca(builder, i64_type, "udt_i");
            LLVMValueRef outer_alloca = LLVMBuildAlloca(builder, i8_ptr_type, "udt_outer");
            LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), idx_alloca);
            LLVMBuildStore(builder, outer, outer_alloca);
            LLVMBuildBr(builder, loop_bb);

            LLVMPositionBuilderAtEnd(builder, loop_bb);
            LLVMValueRef cur_idx = LLVMBuildLoad2(builder, i64_type, idx_alloca, "i");
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, cur_idx, n, "cmp");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "udt_arr.body");
            LLVMBuildCondBr(builder, cmp, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(builder, body_bb);
            // Create new UDT instance
            LLVMValueRef inst = LLVMBuildCall2(builder, ctor_ft, ctor_it->second.fn, nullptr, 0, "inst");
            // Encode ptr as f64
            LLVMValueRef inst_i64 = LLVMBuildPtrToInt(builder, inst, i64_type, "ptoi");
            LLVMValueRef inst_f64 = pun_i64_to_f64(inst_i64);
            LLVMValueRef cur_outer = LLVMBuildLoad2(builder, i8_ptr_type, outer_alloca, "out");
            LLVMValueRef app_args[] = { cur_outer, inst_f64 };
            LLVMValueRef new_outer = LLVMBuildCall2(builder, arr_append.fn_type, arr_append.fn, app_args, 2, "out");
            LLVMBuildStore(builder, new_outer, outer_alloca);
            LLVMValueRef next_idx = LLVMBuildAdd(builder, cur_idx, LLVMConstInt(i64_type, 1, 0), "next");
            LLVMBuildStore(builder, next_idx, idx_alloca);
            LLVMBuildBr(builder, loop_bb);

            LLVMPositionBuilderAtEnd(builder, end_bb);
            LLVMValueRef final_outer = LLVMBuildLoad2(builder, i8_ptr_type, outer_alloca, "udt_arr");
            // Mark nested so array arithmetic handles correctly
            auto& set_nested = runtime_funcs["__arr_set_nested"];
            LLVMValueRef sn[] = { final_outer };
            LLVMBuildCall2(builder, set_nested.fn_type, set_nested.fn, sn, 1, "");

            VarInfo* vi = lookup_var(stmt.var_name);
            if (vi) { LLVMBuildStore(builder, final_outer, vi->alloca_val); vi->tag = 3; }
            else { VarInfo& nv = create_var(stmt.var_name, 3); LLVMBuildStore(builder, final_outer, nv.alloca_val); }
            // Track element type so arr[i].field access works
            var_udt_type[stmt.var_name + "[]"] = type_name;
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
                LLVMValueRef size_i64 = size_val.tag == 1
                    ? LLVMBuildFPToSI(builder, size_val.val, i64_type, "ftoi")
                    : size_val.val;
                LLVMValueRef args[] = { size_i64 };
                LLVMValueRef arr = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, args, 1, "arr");
                VarInfo* vi = lookup_var(stmt.var_name);
                if (vi) { LLVMBuildStore(builder, arr, vi->alloca_val); vi->tag = 3; }
                else { VarInfo& nv = create_var(stmt.var_name, 3); LLVMBuildStore(builder, arr, nv.alloca_val); }
            } else {
                // 2D+: build array of arrays
                // Outer array with shape_args[0] elements, each is an inner array of shape_args[1] elements
                TypedValue rows_val = codegen_expr(*shape_args[0]);
                TypedValue cols_val = codegen_expr(*shape_args[1]);
                LLVMValueRef rows = rows_val.tag == 1
                    ? LLVMBuildFPToSI(builder, rows_val.val, i64_type, "ftoi") : rows_val.val;
                LLVMValueRef cols = cols_val.tag == 1
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
                if (vi) { LLVMBuildStore(builder, final_outer, vi->alloca_val); vi->tag = 3; }
                else { VarInfo& nv = create_var(stmt.var_name, 3); LLVMBuildStore(builder, final_outer, nv.alloca_val); }
            }
            return;
        }
    }

    // Check if the expression produces an array (e.g. IOTA)
    TypedValue rhs = codegen_expr(*stmt.expr);
    if (rhs.tag == 3) {
        VarInfo* vi = lookup_var(stmt.var_name);
        if (vi) {
            LLVMBuildStore(builder, rhs.val, vi->alloca_val);
            vi->tag = 3;
        } else {
            VarInfo& nv = create_var(stmt.var_name, 3);
            LLVMBuildStore(builder, rhs.val, nv.alloca_val);
        }
        return;
    }

    // Fallback: treat like LET for scalar DIM
    VarInfo* vi = lookup_var(stmt.var_name);
    if (vi) {
        LLVMBuildStore(builder, rhs.val, vi->alloca_val);
    } else {
        VarInfo& nv = create_var(stmt.var_name, rhs.tag);
        LLVMBuildStore(builder, rhs.val, nv.alloca_val);
    }
}

// ── INDEX_ASSIGN: arr[i] = val ──────────────────────────────

void LLVMCodegen::codegen_index_assign(const Stmt& stmt) {
    // UDT field assignment: obj.field = val (print_exprs[0] = obj, label = field)
    if (!stmt.print_exprs.empty() && !stmt.label.empty()) {
        TypedValue obj = codegen_expr(*stmt.print_exprs[0]);
        TypedValue val = codegen_expr(*stmt.expr);
        LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, stmt.label.c_str(), ".fld");

        // Decode ptr from f64/i64 if needed (e.g. array element holding UDT)
        LLVMValueRef obj_ptr = obj.val;
        if (obj.tag == 1) {
            LLVMValueRef as_i64 = pun_f64_to_i64(obj.val);
            obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
        } else if (obj.tag == 0) {
            obj_ptr = LLVMBuildIntToPtr(builder, obj.val, i8_ptr_type, "itoptr");
        }

        // Determine field type
        bool is_str = (!stmt.label.empty() && stmt.label.back() == '$') || val.tag == 2;
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
            LLVMValueRef args[] = { obj_ptr, field_str, val.val };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        } else {
            LLVMValueRef fval = val.tag == 0 ? LLVMBuildSIToFP(builder, val.val, f64_type, "itof") : val.val;
            auto& set_fn = runtime_funcs["__udt_set_f64"];
            LLVMValueRef args[] = { obj_ptr, field_str, fval };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        }
        return;
    }

    VarInfo* vi = lookup_var(stmt.var_name);
    if (!vi || vi->tag != 3) return;  // not an array/UDT

    // Check if this is a UDT field assignment via index_chain with string key
    // (e.g. THIS.HitPoints = 150 parsed as INDEX_ASSIGN with string key)
    if (!stmt.index_chain.empty() && stmt.index_chain[0]->kind == ExprKind::LITERAL_STRING) {
        std::string field_name = stmt.index_chain[0]->str_val;
        LLVMValueRef obj_ptr = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "obj");
        LLVMValueRef field_str = LLVMBuildGlobalStringPtr(builder, field_name.c_str(), ".fld");
        TypedValue val_tv = codegen_expr(*stmt.expr);

        bool is_str = (!field_name.empty() && field_name.back() == '$') || val_tv.tag == 2;
        if (!is_str)
            is_str = is_udt_string_field(stmt.var_name, field_name);

        if (is_str) {
            auto& set_fn = runtime_funcs["__udt_set_str"];
            LLVMValueRef args[] = { obj_ptr, field_str, val_tv.val };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        } else {
            LLVMValueRef fval = val_tv.tag == 0
                ? LLVMBuildSIToFP(builder, val_tv.val, f64_type, "itof") : val_tv.val;
            auto& set_fn = runtime_funcs["__udt_set_f64"];
            LLVMValueRef args[] = { obj_ptr, field_str, fval };
            LLVMBuildCall2(builder, set_fn.fn_type, set_fn.fn, args, 3, "");
        }
        return;
    }

    // Load array pointer
    LLVMValueRef arr_ptr = LLVMBuildLoad2(builder, i8_ptr_type, vi->alloca_val, "arr");

    if (stmt.index_chain.empty()) return;

    // For multi-dimensional access (arr[i][j] = val), traverse the chain:
    // each index except the last does array_get + ptr decode
    auto& arr_get = runtime_funcs["__array_get"];
    for (size_t ic = 0; ic + 1 < stmt.index_chain.size(); ic++) {
        TypedValue idx_tv = codegen_expr(*stmt.index_chain[ic]);
        LLVMValueRef idx = idx_tv.tag == 1
            ? LLVMBuildFPToSI(builder, idx_tv.val, i64_type, "ftoi") : idx_tv.val;
        LLVMValueRef get_args[] = { arr_ptr, idx };
        LLVMValueRef elem = LLVMBuildCall2(builder, arr_get.fn_type, arr_get.fn, get_args, 2, "elem");
        // Decode f64 → ptr (inner array)
        LLVMValueRef as_i64 = pun_f64_to_i64(elem);
        arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "inner");
    }

    // Last index: array_set
    TypedValue idx_tv = codegen_expr(*stmt.index_chain.back());
    LLVMValueRef idx = idx_tv.tag == 1
        ? LLVMBuildFPToSI(builder, idx_tv.val, i64_type, "ftoi") : idx_tv.val;

    // Evaluate value
    TypedValue val_tv = codegen_expr(*stmt.expr);
    LLVMValueRef val;
    if (val_tv.tag == 0)
        val = LLVMBuildSIToFP(builder, val_tv.val, f64_type, "itof");
    else if (val_tv.tag == 2 || val_tv.tag == 3) {
        LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, val_tv.val, i64_type, "ptoi");
        val = pun_i64_to_f64(as_i64);
    } else
        val = val_tv.val;

    auto& arr_set = runtime_funcs["__array_set"];
    LLVMValueRef args[] = { arr_ptr, idx, val };
    LLVMBuildCall2(builder, arr_set.fn_type, arr_set.fn, args, 3, "");
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
            if (arr.tag == 3) {
                TypedValue idx = codegen_expr(*pe.right);
                LLVMValueRef idx_i64 = idx.tag == 1
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
        if (tv.tag == 0 || tv.tag == 4) {
            LLVMValueRef args[] = { tv.val };
            LLVMBuildCall2(builder, pr_int.fn_type, pr_int.fn, args, 1, "");
        } else if (tv.tag == 1) {
            LLVMValueRef args[] = { tv.val };
            LLVMBuildCall2(builder, pr_double.fn_type, pr_double.fn, args, 1, "");
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
        step_val = sv.val;
    } else {
        step_val = LLVMConstInt(i64_type, 1, 0);
    }

    VarInfo* vi = lookup_var(stmt.var_name);
    LLVMValueRef var_alloca;
    if (vi) {
        var_alloca = vi->alloca_val;
    } else {
        VarInfo& nv = create_var(stmt.var_name, 0);
        var_alloca = nv.alloca_val;
    }

    LLVMBuildStore(builder, start_val.val, var_alloca);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.body");
    LLVMBasicBlockRef inc_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.inc");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, current_fn, "for.end");

    loop_stack.push({ end_bb, inc_bb });

    LLVMBuildBr(builder, cond_bb);

    LLVMPositionBuilderAtEnd(builder, cond_bb);
    LLVMValueRef cur_val = LLVMBuildLoad2(builder, i64_type, var_alloca, "i");
    LLVMValueRef end_i64 = end_val.tag == 1
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

    if (stmt.cond_at_top && stmt.loop_cond) {
        // DO WHILE/UNTIL ... LOOP — condition checked before body
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
        for (auto& s : stmt.body) {
            if (s) codegen_stmt(*s);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
            LLVMBuildBr(builder, cond_bb);

        loop_stack.pop();
    } else {
        // DO ... LOOP [WHILE/UNTIL] — body runs at least once
        loop_stack.push({ end_bb, body_bb });

        LLVMBuildBr(builder, body_bb);

        LLVMPositionBuilderAtEnd(builder, body_bb);
        for (auto& s : stmt.body) {
            if (s) codegen_stmt(*s);
        }

        if (stmt.loop_cond) {
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                TypedValue cond = codegen_expr(*stmt.loop_cond);
                LLVMValueRef cond_i1 = to_i1(cond);
                if (stmt.is_while)
                    LLVMBuildCondBr(builder, cond_i1, body_bb, end_bb);
                else
                    LLVMBuildCondBr(builder, cond_i1, end_bb, body_bb);
            }
        } else {
            // Infinite loop: DO ... LOOP (exits only via EXITDO)
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, body_bb);
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
    // SWITCH expr / CASE val / ... / DEFAULT / ENDSWITCH
    // Reuse IF-like structure: compare switch_expr against each case condition
    TypedValue switch_val = codegen_expr(*stmt.expr);
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.end");

    for (size_t i = 0; i < stmt.branches.size(); i++) {
        auto& branch = stmt.branches[i];

        if (!branch.condition) {
            // DEFAULT
            for (auto& s : branch.body) { if (s) codegen_stmt(*s); }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, merge_bb);
        } else {
            // CASE: compare switch_val == case_val
            TypedValue case_val = codegen_expr(*branch.condition);

            LLVMValueRef cmp;
            if (switch_val.tag == 2 && case_val.tag == 2) {
                auto& fn = runtime_funcs["__str_eq"];
                LLVMValueRef args[] = { switch_val.val, case_val.val };
                cmp = LLVMBuildICmp(builder, LLVMIntNE,
                    LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "eq"),
                    LLVMConstInt(i64_type, 0, 0), "cmp");
            } else if (switch_val.tag == 1 || case_val.tag == 1) {
                TypedValue sv = promote_to_f64(switch_val);
                TypedValue cv = promote_to_f64(case_val);
                cmp = LLVMBuildFCmp(builder, LLVMRealOEQ, sv.val, cv.val, "cmp");
            } else {
                cmp = LLVMBuildICmp(builder, LLVMIntEQ, switch_val.val, case_val.val, "cmp");
            }

            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.case");
            LLVMBasicBlockRef next_bb = (i + 1 < stmt.branches.size())
                ? LLVMAppendBasicBlockInContext(ctx, current_fn, "sw.next")
                : merge_bb;

            LLVMBuildCondBr(builder, cmp, then_bb, next_bb);

            LLVMPositionBuilderAtEnd(builder, then_bb);
            for (auto& s : branch.body) { if (s) codegen_stmt(*s); }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
                LLVMBuildBr(builder, merge_bb);

            if (next_bb != merge_bb)
                LLVMPositionBuilderAtEnd(builder, next_bb);
        }
    }
    LLVMPositionBuilderAtEnd(builder, merge_bb);
}

// ── FOR EACH ────────────────────────────────────────────────

void LLVMCodegen::codegen_for_each(const Stmt& stmt) {
    // FOR EACH var IN collection ... NEXT
    TypedValue coll = codegen_expr(*stmt.expr);

    // Get collection pointer (may need conversion from f64 param encoding)
    LLVMValueRef arr_ptr = coll.val;
    if (coll.tag == 1) {
        LLVMValueRef as_i64 = pun_f64_to_i64(coll.val);
        arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
    }

    // Get length
    auto& len_fn = runtime_funcs["LEN"];
    LLVMValueRef len_args[] = { arr_ptr };
    LLVMValueRef len = LLVMBuildCall2(builder, len_fn.fn_type, len_fn.fn, len_args, 1, "len");

    // Index variable (hidden, unique name to avoid collisions)
    static int foreach_counter = 0;
    std::string idx_name = "__foreach_idx_" + std::to_string(foreach_counter++);
    VarInfo& idx_vi = create_var(idx_name, 0);
    LLVMBuildStore(builder, LLVMConstInt(i64_type, 0, 0), idx_vi.alloca_val);

    // Loop variable — always create a fresh f64 local in current scope
    // to avoid collisions with existing globals of the same name
    VarInfo& fe_var = create_var(stmt.var_name, 1);  // f64 (array elements)
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
    LLVMBuildStore(builder, elem, var_vi->alloca_val);

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
    // Register each enum member as a global constant
    for (auto& [name, value] : stmt.enum_members) {
        // Check if global already exists (avoid duplicate)
        LLVMValueRef existing = LLVMGetNamedGlobal(module, name.c_str());
        if (!existing) {
            existing = LLVMAddGlobal(module, i64_type, name.c_str());
            LLVMSetLinkage(existing, LLVMInternalLinkage);
        }
        LLVMSetInitializer(existing, LLVMConstInt(i64_type, (uint64_t)value, 1));
        scopes[0].vars[name] = { existing, 0 };
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
            // Parser already added THIS as first param with type OBJECT
            std::vector<LLVMTypeRef> ptypes;
            std::vector<int> ptags;
            for (auto& p : method->params) {
                if (p.name == "THIS" || p.type == VarType::OBJECT) {
                    ptypes.push_back(i8_ptr_type);
                    ptags.push_back(3);  // ptr for UDT object
                } else {
                    bool sp = (!p.name.empty() && p.name.back() == '$');
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
            return { LLVMConstInt(i64_type, (uint64_t)expr.int_val, 1), 0 };

        case ExprKind::LITERAL_FLOAT:
            return { LLVMConstReal(f64_type, expr.float_val), 1 };

        case ExprKind::LITERAL_STRING:
            return { LLVMBuildGlobalStringPtr(builder, expr.str_val.c_str(), ".str"), 2 };

        case ExprKind::LITERAL_BOOL:
            return { LLVMConstInt(i64_type, expr.bool_val ? 1 : 0, 0), 0 };

        case ExprKind::ARRAY_LITERAL: {
            // [] or [a, b, c] → create array
            auto& arr_new = runtime_funcs["__array_new"];
            LLVMValueRef size_arg[] = { LLVMConstInt(i64_type, 0, 0) };
            LLVMValueRef arr = LLVMBuildCall2(builder, arr_new.fn_type, arr_new.fn, size_arg, 1, "arr");
            bool has_ptr_elems = false;
            bool has_string_elems = false;
            if (!expr.args.empty()) {
                auto& arr_append = runtime_funcs["APPEND"];
                for (size_t i = 0; i < expr.args.size(); i++) {
                    TypedValue elem = codegen_expr(*expr.args[i]);
                    LLVMValueRef fval = elem.val;
                    if (elem.tag == 0) fval = LLVMBuildSIToFP(builder, fval, f64_type, "itof");
                    else if (elem.tag == 2 || elem.tag == 3) {
                        // ptr (string or array) → encode as f64
                        LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, fval, i64_type, "ptoi");
                        fval = pun_i64_to_f64(as_i64);
                        has_ptr_elems = true;
                        if (elem.tag == 2) has_string_elems = true;
                    }
                    LLVMValueRef append_args[] = { arr, fval };
                    arr = LLVMBuildCall2(builder, arr_append.fn_type, arr_append.fn, append_args, 2, "arr");
                }
                if (has_ptr_elems) {
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
                }
            }
            return { arr, 3 };
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
                            return { result, 2 };
                        } else {
                            auto& get_fn = runtime_funcs["__udt_get_f64"];
                            LLVMValueRef args[] = { obj_ptr, field_str };
                            LLVMValueRef result = LLVMBuildCall2(builder, get_fn.fn_type,
                                                    get_fn.fn, args, 2, "fget");
                            return { result, 1 };
                        }
                    }
                }
                return { LLVMConstInt(i64_type, 0, 0), 0 };
            }
            LLVMTypeRef load_type;
            int tag = vi->tag;
            if (tag == 1)       load_type = f64_type;
            else if (tag == 2)  load_type = i8_ptr_type;
            else if (tag == 3)  load_type = i8_ptr_type;
            else                load_type = i64_type;  // covers 0 and -2 (universal)
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
            if (obj.tag == 1) {
                LLVMValueRef as_i64 = pun_f64_to_i64(obj.val);
                obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            } else if (obj.tag == 0) {
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
                return { result, 2 };
            } else {
                auto& get_fn = runtime_funcs["__udt_get_f64"];
                LLVMValueRef args[] = { obj_ptr, field_str };
                LLVMValueRef result = LLVMBuildCall2(builder, get_fn.fn_type, get_fn.fn, args, 2, "fget");
                return { result, 1 };
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
                bool array_input = (left_val.tag == 3);

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
                        scopes.back().vars[lam.lambda_params[i]] = { nullptr, 3 };
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
                        VarInfo& vi = create_var(lam.lambda_params[i], 3);
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
                    if (left_val.tag == 0) arg = LLVMBuildSIToFP(builder, arg, f64_type, "itof");
                    LLVMValueRef args[] = { arg };
                    LLVMValueRef result = LLVMBuildCall2(builder, call_ft, lambda.val, args, 1, "pipe");
                    return { result, 1 };
                }
            } else if (expr.right->kind == ExprKind::VARIABLE) {
                // value |> FuncName  →  FuncName(value)
                std::string fn_name = expr.right->str_val;
                auto uit = user_functions.find(fn_name);
                if (uit != user_functions.end()) {
                    auto& fi = uit->second;
                    LLVMValueRef arg = left_val.val;
                    if (left_val.tag == 0) arg = LLVMBuildSIToFP(builder, arg, f64_type, "itof");
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
                if (left_val.tag == 0 && pipe_var->tag == 1)
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
                LLVMTypeRef load_type = (vi->tag == 1) ? f64_type :
                                        (vi->tag == 2) ? i8_ptr_type :
                                        (vi->tag == 3) ? i8_ptr_type : i64_type;
                return { LLVMBuildLoad2(builder, load_type, vi->alloca_val, "pipe_val"), vi->tag };
            }
            return { LLVMConstReal(f64_type, 0.0), 1 };
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
                VarInfo& vi = create_var(expr.lambda_params[i], 1);
                LLVMBuildStore(builder, LLVMGetParam(lambda_fn, i), vi.alloca_val);
            }

            TypedValue body = codegen_expr(*expr.right);

            LLVMValueRef ret_val = body.val;
            if (body.tag == 0) ret_val = LLVMBuildSIToFP(builder, ret_val, f64_type, "itof");
            LLVMBuildRet(builder, ret_val);

            // Restore state — position builder back at the EXACT block we were in
            scopes.pop_back();
            current_fn = saved_fn;
            LLVMPositionBuilderAtEnd(builder, saved_bb);

            return { lambda_fn, 5 };
        }

        case ExprKind::INDEX: {
            // Special case: LEN(arr)[i] → return shape array's i-th element
            // Interpreter's LEN returns shape for 2D+ arrays; we emulate here.
            if (expr.left && expr.left->kind == ExprKind::CALL) {
                std::string fn = expr.left->func_name;
                std::transform(fn.begin(), fn.end(), fn.begin(), ::toupper);
                if (fn == "LEN" && expr.left->args.size() == 1) {
                    TypedValue av = codegen_expr(*expr.left->args[0]);
                    if (av.tag == 3) {
                        auto* shape_fn = get_runtime_func("__arr_len_shape");
                        if (shape_fn) {
                            LLVMValueRef shape_args[] = { av.val };
                            LLVMValueRef shape = LLVMBuildCall2(builder, shape_fn->fn_type,
                                shape_fn->fn, shape_args, 1, "shape");
                            TypedValue idx_tv2 = codegen_expr(*expr.right);
                            LLVMValueRef idx2 = idx_tv2.tag == 1
                                ? LLVMBuildFPToSI(builder, idx_tv2.val, i64_type, "ftoi")
                                : idx_tv2.val;
                            auto& ag = runtime_funcs["__array_get"];
                            LLVMValueRef g_args[] = { shape, idx2 };
                            LLVMValueRef result = LLVMBuildCall2(builder, ag.fn_type, ag.fn, g_args, 2, "shape_i");
                            return { result, 1 };
                        }
                    }
                }
            }

            // arr[i] — array element access or obj{"key"} map access
            TypedValue arr_tv = codegen_expr(*expr.left);
            TypedValue idx_tv = codegen_expr(*expr.right);

            // String key → map/object access via VM bridge
            if (idx_tv.tag == 2) {
                LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type,
                    LLVMGetNamedGlobal(module, "__jdrt_handle"), "rt");
                LLVMValueRef args_arr = LLVMBuildArrayAlloca(builder, i64_type,
                    LLVMConstInt(i32_type, 2, 0), "args");
                LLVMValueRef tags_arr = LLVMBuildArrayAlloca(builder, i32_type,
                    LLVMConstInt(i32_type, 2, 0), "tags");
                // Arg 0: object
                LLVMValueRef enc0 = (arr_tv.tag == 3 || arr_tv.tag == 2)
                    ? LLVMBuildPtrToInt(builder, arr_tv.val, i64_type, "ptoi")
                    : (arr_tv.tag == 1 ? pun_f64_to_i64(arr_tv.val) : arr_tv.val);
                LLVMValueRef aidx0[] = { LLVMConstInt(i32_type, 0, 0) };
                LLVMBuildStore(builder, enc0, LLVMBuildGEP2(builder, i64_type, args_arr, aidx0, 1, "a"));
                LLVMBuildStore(builder, LLVMConstInt(i32_type, arr_tv.tag == 4 ? 4 : 3, 0),
                    LLVMBuildGEP2(builder, i32_type, tags_arr, aidx0, 1, "t"));
                // Arg 1: string key
                LLVMValueRef aidx1[] = { LLVMConstInt(i32_type, 1, 0) };
                LLVMBuildStore(builder, LLVMBuildPtrToInt(builder, idx_tv.val, i64_type, "stoi"),
                    LLVMBuildGEP2(builder, i64_type, args_arr, aidx1, 1, "a"));
                LLVMBuildStore(builder, LLVMConstInt(i32_type, 2, 0),
                    LLVMBuildGEP2(builder, i32_type, tags_arr, aidx1, 1, "t"));
                // Call MAP.GET or generic index via VM
                LLVMValueRef name_str = LLVMBuildGlobalStringPtr(builder, "MAP.GET", ".fn");
                auto& fn = runtime_funcs["__jdrt_call_typed_f64"];
                LLVMValueRef call_args[] = { handle, name_str, args_arr, tags_arr,
                    LLVMConstInt(i32_type, 2, 0) };
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "mapget");
                return { result, 1 };
            }

            // Get array pointer — may need to convert from encoded param
            LLVMValueRef arr_ptr = arr_tv.val;
            if (arr_tv.tag == 1) {
                LLVMValueRef as_i64 = pun_f64_to_i64(arr_tv.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            } else if (arr_tv.tag == 0) {
                arr_ptr = LLVMBuildIntToPtr(builder, arr_tv.val, i8_ptr_type, "itoptr");
            }

            // Convert index to i64
            LLVMValueRef idx = idx_tv.val;
            if (idx_tv.tag == 1)
                idx = LLVMBuildFPToSI(builder, idx, i64_type, "ftoi");

            auto& arr_get = runtime_funcs["__array_get"];
            LLVMValueRef args[] = { arr_ptr, idx };
            LLVMValueRef result = LLVMBuildCall2(builder, arr_get.fn_type, arr_get.fn, args, 2, "elem");
            return { result, 1 };  // array elements are f64
        }

        default:
            return { LLVMConstInt(i64_type, 0, 0), 0 };
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
        if (lhs_check.tag == 3) {
            TypedValue rhs_check = codegen_expr(*expr.right);
            int32_t cmp_op = (expr.op == TokenType::AND || expr.op == TokenType::ANDALSO) ? 2 : 3;
            if (rhs_check.tag == 3) {
                auto& fn = runtime_funcs["__arr_cmp_arr"];
                LLVMValueRef args[] = { lhs_check.val, rhs_check.val, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), 3 };
            } else {
                LLVMValueRef scalar = rhs_check.tag == 0
                    ? LLVMBuildSIToFP(builder, rhs_check.val, f64_type, "itof") : rhs_check.val;
                auto& fn = runtime_funcs["__arr_cmp_scalar"];
                LLVMValueRef args[] = { lhs_check.val, scalar, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), 3 };
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

        return { phi, 0 };
        } // end scalar short-circuit block
    }

    TypedValue lhs = codegen_expr(*expr.left);
    TypedValue rhs = codegen_expr(*expr.right);

    // String concatenation: str + str, str + int, int + str, str + float, etc.
    // But NOT if one side is an array (tag=3) — that goes to array arithmetic
    if (expr.op == TokenType::PLUS && (lhs.tag == 2 || rhs.tag == 2) &&
        lhs.tag != 3 && rhs.tag != 3) {
        // Convert non-string operand to string
        auto to_str = [&](TypedValue tv) -> LLVMValueRef {
            if (tv.tag == 2) return tv.val;
            if (tv.tag == 0) {
                auto& fn = runtime_funcs["__int_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "itostr");
            } else {
                auto& fn = runtime_funcs["__double_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "ftostr");
            }
        };
        auto& concat = runtime_funcs["__str_concat"];
        LLVMValueRef args[] = { to_str(lhs), to_str(rhs) };
        LLVMValueRef result = LLVMBuildCall2(builder, concat.fn_type, concat.fn, args, 2, "concat");
        return { result, 2 };
    }

    // String comparison with array element: arr[i] = "str" means arr[i]
    // is a ptr-encoded string. Decode the f64 back to ptr, then compare.
    if ((expr.op == TokenType::EQ || expr.op == TokenType::ASSIGN || expr.op == TokenType::NE) &&
        ((lhs.tag == 1 && rhs.tag == 2) || (lhs.tag == 2 && rhs.tag == 1))) {
        // Decode the f64 side as ptr (it's likely a ptr-encoded string from an array)
        auto decode_ptr = [&](TypedValue tv) -> LLVMValueRef {
            if (tv.tag == 2) return tv.val;
            LLVMValueRef as_i64 = pun_f64_to_i64(tv.val);
            return LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "ftoptr");
        };
        LLVMValueRef l = decode_ptr(lhs);
        LLVMValueRef r = decode_ptr(rhs);
        if (expr.op == TokenType::NE) {
            auto& fn = runtime_funcs["__str_ne"];
            LLVMValueRef args[] = { l, r };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "strne"), 0 };
        }
        auto& fn = runtime_funcs["__str_eq"];
        LLVMValueRef args[] = { l, r };
        return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "streq"), 0 };
    }

    // String comparison: str = str, str <> str
    if ((lhs.tag == 2 && rhs.tag == 2) &&
        (expr.op == TokenType::EQ || expr.op == TokenType::ASSIGN || expr.op == TokenType::NE)) {
        if (expr.op == TokenType::NE) {
            auto& fn = runtime_funcs["__str_ne"];
            LLVMValueRef args[] = { lhs.val, rhs.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "strne");
            return { result, 0 };
        } else {
            auto& fn = runtime_funcs["__str_eq"];
            LLVMValueRef args[] = { lhs.val, rhs.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "streq");
            return { result, 0 };
        }
    }

    // IN operator: "ell" IN "Hello" → INSTR(haystack, needle) > 0
    if (expr.op == TokenType::IN) {
        if (lhs.tag == 2 && rhs.tag == 2) {
            auto& fn = runtime_funcs["INSTR"];
            LLVMValueRef args[] = { rhs.val, lhs.val };
            LLVMValueRef pos = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "instr");
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSGT, pos,
                                              LLVMConstInt(i64_type, 0, 0), "in");
            return { LLVMBuildZExt(builder, cmp, i64_type, "ext"), 0 };
        }
        return { LLVMConstInt(i64_type, 0, 0), 0 };
    }

    // Power operator (^)
    if (expr.op == TokenType::CARET) {
        lhs = promote_to_f64(lhs);
        rhs = promote_to_f64(rhs);
        auto& fn = runtime_funcs["__pow"];
        LLVMValueRef args[] = { lhs.val, rhs.val };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "pow");
        return { result, 1 };
    }

    // Array + String or String + Array: native element-wise string concat
    if (expr.op == TokenType::PLUS &&
        ((lhs.tag == 3 && rhs.tag == 2) || (lhs.tag == 2 && rhs.tag == 3))) {
        bool scalar_left = (lhs.tag == 2);
        LLVMValueRef arr_ptr = scalar_left ? rhs.val : lhs.val;
        LLVMValueRef str_ptr = scalar_left ? lhs.val : rhs.val;
        auto& fn = runtime_funcs["__arr_str_concat"];
        LLVMValueRef args[] = { arr_ptr, str_ptr, LLVMConstInt(i32_type, scalar_left ? 1 : 0, 0) };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "asc");
        return { result, 3 };
    }

    // Array/ptr operands: use native array arithmetic functions
    if (lhs.tag == 3 || rhs.tag == 3) {
        // Determine op code: 0=add, 1=sub, 2=mul, 3=div
        int32_t arith_op = -1;
        switch (expr.op) {
            case TokenType::PLUS:  arith_op = 0; break;
            case TokenType::MINUS: arith_op = 1; break;
            case TokenType::STAR:  arith_op = 2; break;
            case TokenType::SLASH: arith_op = 3; break;
            default: break;
        }
        // Comparison ops: 0=eq, 1=ne, 2=and, 3=or
        int32_t cmp_op = -1;
        switch (expr.op) {
            case TokenType::EQ:
            case TokenType::ASSIGN: cmp_op = 0; break;
            case TokenType::NE:     cmp_op = 1; break;
            case TokenType::AND:    cmp_op = 2; break;
            case TokenType::OR:     cmp_op = 3; break;
            default: break;
        }

        if (lhs.tag == 3 && rhs.tag == 3) {
            // arr OP arr
            if (arith_op >= 0) {
                auto& fn = runtime_funcs["__arr_binop"];
                LLVMValueRef args[] = { lhs.val, rhs.val, LLVMConstInt(i32_type, arith_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "aop"), 3 };
            }
            if (cmp_op >= 0) {
                auto& fn = runtime_funcs["__arr_cmp_arr"];
                LLVMValueRef args[] = { lhs.val, rhs.val, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), 3 };
            }
        } else if (lhs.tag == 3) {
            // arr OP scalar
            LLVMValueRef scalar = rhs.tag == 0
                ? LLVMBuildSIToFP(builder, rhs.val, f64_type, "itof") : rhs.val;
            if (arith_op >= 0) {
                auto& fn = runtime_funcs["__arr_scalar_op"];
                LLVMValueRef args[] = { lhs.val, scalar, LLVMConstInt(i32_type, arith_op, 0),
                                         LLVMConstInt(i32_type, 0, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "asop"), 3 };
            }
            if (cmp_op >= 0) {
                auto& fn = runtime_funcs["__arr_cmp_scalar"];
                LLVMValueRef args[] = { lhs.val, scalar, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), 3 };
            }
        } else {
            // scalar OP arr
            LLVMValueRef scalar = lhs.tag == 0
                ? LLVMBuildSIToFP(builder, lhs.val, f64_type, "itof") : lhs.val;
            if (arith_op >= 0) {
                auto& fn = runtime_funcs["__arr_scalar_op"];
                LLVMValueRef args[] = { rhs.val, scalar, LLVMConstInt(i32_type, arith_op, 0),
                                         LLVMConstInt(i32_type, 1, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 4, "asop"), 3 };
            }
            if (cmp_op >= 0) {
                auto& fn = runtime_funcs["__arr_cmp_scalar"];
                LLVMValueRef args[] = { rhs.val, scalar, LLVMConstInt(i32_type, cmp_op, 0) };
                return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "acmp"), 3 };
            }
        }
        // Fallback for unsupported array ops
        return { LLVMConstInt(i64_type, 0, 0), 0 };
    }

    // String multiplication: "abc" * 3 or 3 * "abc"
    if (expr.op == TokenType::STAR && (lhs.tag == 2 || rhs.tag == 2)) {
        // Dispatch via VM bridge
        LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type,
            LLVMGetNamedGlobal(module, "__jdrt_handle"), "rt");
        LLVMValueRef args_arr = LLVMBuildArrayAlloca(builder, i64_type,
            LLVMConstInt(i32_type, 2, 0), "args");
        LLVMValueRef tags_arr = LLVMBuildArrayAlloca(builder, i32_type,
            LLVMConstInt(i32_type, 2, 0), "tags");

        auto encode_val = [&](TypedValue tv, int idx) {
            LLVMValueRef encoded;
            int32_t tag;
            if (tv.tag == 2) { encoded = LLVMBuildPtrToInt(builder, tv.val, i64_type, "stoi"); tag = 2; }
            else if (tv.tag == 1) { encoded = pun_f64_to_i64(tv.val); tag = 1; }
            else { LLVMValueRef f = LLVMBuildSIToFP(builder, tv.val, f64_type, "itof"); encoded = pun_f64_to_i64(f); tag = 0; }
            LLVMValueRef aidx[] = { LLVMConstInt(i32_type, idx, 0) };
            LLVMBuildStore(builder, encoded, LLVMBuildGEP2(builder, i64_type, args_arr, aidx, 1, "arg"));
            LLVMBuildStore(builder, LLVMConstInt(i32_type, tag, 0), LLVMBuildGEP2(builder, i32_type, tags_arr, aidx, 1, "tag"));
        };
        encode_val(lhs, 0);
        encode_val(rhs, 1);

        LLVMValueRef name_str = LLVMBuildGlobalStringPtr(builder, "REPEAT$", ".op");
        auto& fn = runtime_funcs["__jdrt_call_typed_str"];
        LLVMValueRef call_args[] = { handle, name_str, args_arr, tags_arr,
            LLVMConstInt(i32_type, 2, 0) };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmcall");
        return { result, 2 };
    }

    // String comparison: str = other, str <> other (when one side might not be str)
    if ((lhs.tag == 2 || rhs.tag == 2) &&
        (expr.op == TokenType::EQ || expr.op == TokenType::ASSIGN || expr.op == TokenType::NE)) {
        // Convert both to strings if needed
        auto to_str2 = [&](TypedValue tv) -> LLVMValueRef {
            if (tv.tag == 2) return tv.val;
            if (tv.tag == 0) {
                auto& fn = runtime_funcs["__int_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "itostr");
            } else {
                auto& fn = runtime_funcs["__double_to_str"];
                LLVMValueRef args[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "ftostr");
            }
        };
        if (expr.op == TokenType::NE) {
            auto& fn = runtime_funcs["__str_ne"];
            LLVMValueRef args[] = { to_str2(lhs), to_str2(rhs) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "strne"), 0 };
        } else {
            auto& fn = runtime_funcs["__str_eq"];
            LLVMValueRef args[] = { to_str2(lhs), to_str2(rhs) };
            return { LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "streq"), 0 };
        }
    }

    // String operations beyond concat/compare: dispatch via VM bridge
    if ((lhs.tag == 2 || rhs.tag == 2) &&
        (expr.op == TokenType::MINUS || expr.op == TokenType::STAR)) {
        // String subtraction or multiplication → VM bridge
        LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type,
            LLVMGetNamedGlobal(module, "__jdrt_handle"), "rt");
        LLVMValueRef args_arr = LLVMBuildArrayAlloca(builder, i64_type,
            LLVMConstInt(i32_type, 2, 0), "args");
        LLVMValueRef tags_arr = LLVMBuildArrayAlloca(builder, i32_type,
            LLVMConstInt(i32_type, 2, 0), "tags");
        auto enc2 = [&](TypedValue tv, int idx) {
            LLVMValueRef encoded; int32_t tag;
            if (tv.tag == 2) { encoded = LLVMBuildPtrToInt(builder, tv.val, i64_type, "stoi"); tag = 2; }
            else if (tv.tag == 1) { encoded = pun_f64_to_i64(tv.val); tag = 1; }
            else { LLVMValueRef f = LLVMBuildSIToFP(builder, tv.val, f64_type, "itof"); encoded = pun_f64_to_i64(f); tag = 0; }
            LLVMValueRef aidx[] = { LLVMConstInt(i32_type, idx, 0) };
            LLVMBuildStore(builder, encoded, LLVMBuildGEP2(builder, i64_type, args_arr, aidx, 1, "arg"));
            LLVMBuildStore(builder, LLVMConstInt(i32_type, tag, 0), LLVMBuildGEP2(builder, i32_type, tags_arr, aidx, 1, "tag"));
        };
        enc2(lhs, 0); enc2(rhs, 1);
        std::string op = (expr.op == TokenType::STAR) ? "REPEAT$" : "__STR_SUB";
        LLVMValueRef name_str = LLVMBuildGlobalStringPtr(builder, op.c_str(), ".op");
        auto& fn = runtime_funcs["__jdrt_call_typed_str"];
        LLVMValueRef call_args[] = { handle, name_str, args_arr, tags_arr,
            LLVMConstInt(i32_type, 2, 0) };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmcall");
        return { result, 2 };
    }

    bool use_float = (lhs.tag == 1 || rhs.tag == 1);
    if (use_float) {
        lhs = promote_to_f64(lhs);
        rhs = promote_to_f64(rhs);
    }

    if (use_float) {
        switch (expr.op) {
            case TokenType::PLUS:  return { LLVMBuildFAdd(builder, lhs.val, rhs.val, "fadd"), 1 };
            case TokenType::MINUS: return { LLVMBuildFSub(builder, lhs.val, rhs.val, "fsub"), 1 };
            case TokenType::STAR:  return { LLVMBuildFMul(builder, lhs.val, rhs.val, "fmul"), 1 };
            case TokenType::SLASH: return { LLVMBuildFDiv(builder, lhs.val, rhs.val, "fdiv"), 1 };
            case TokenType::MOD:   return { LLVMBuildFRem(builder, lhs.val, rhs.val, "fmod"), 1 };
            case TokenType::LT:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOLT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::GT:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOGT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::LE:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOLE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::GE:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOGE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::EQ:
            case TokenType::ASSIGN: return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealOEQ, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::NE:    return { LLVMBuildZExt(builder, LLVMBuildFCmp(builder, LLVMRealONE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            default: break;
        }
    } else {
        switch (expr.op) {
            case TokenType::PLUS:      return { LLVMBuildAdd(builder, lhs.val, rhs.val, "add"), 0 };
            case TokenType::MINUS:     return { LLVMBuildSub(builder, lhs.val, rhs.val, "sub"), 0 };
            case TokenType::STAR:      return { LLVMBuildMul(builder, lhs.val, rhs.val, "mul"), 0 };
            case TokenType::SLASH:     return { LLVMBuildSDiv(builder, lhs.val, rhs.val, "div"), 0 };
            case TokenType::BACKSLASH: return { LLVMBuildSDiv(builder, lhs.val, rhs.val, "idiv"), 0 };
            case TokenType::MOD:       return { LLVMBuildSRem(builder, lhs.val, rhs.val, "mod"), 0 };
            case TokenType::LT:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSLT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::GT:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSGT, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::LE:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSLE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::GE:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntSGE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::EQ:
            case TokenType::ASSIGN:    return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntEQ, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::NE:        return { LLVMBuildZExt(builder, LLVMBuildICmp(builder, LLVMIntNE, lhs.val, rhs.val, "cmp"), i64_type, "ext"), 0 };
            case TokenType::BAND:      return { LLVMBuildAnd(builder, lhs.val, rhs.val, "band"), 0 };
            case TokenType::BOR:       return { LLVMBuildOr(builder, lhs.val, rhs.val, "bor"), 0 };
            case TokenType::XOR:
            case TokenType::BXOR:      return { LLVMBuildXor(builder, lhs.val, rhs.val, "bxor"), 0 };
            default: break;
        }
    }
    return { LLVMConstInt(i64_type, 0, 0), 0 };
}

LLVMCodegen::TypedValue LLVMCodegen::codegen_unary(const Expr& expr) {
    TypedValue operand = codegen_expr(*expr.right);
    if (expr.op == TokenType::MINUS) {
        if (operand.tag == 1)
            return { LLVMBuildFNeg(builder, operand.val, "fneg"), 1 };
        else
            return { LLVMBuildNeg(builder, operand.val, "neg"), 0 };
    }
    if (expr.op == TokenType::NOT) {
        LLVMValueRef b = to_i1(operand);
        LLVMValueRef notb = LLVMBuildNot(builder, b, "not");
        return { LLVMBuildZExt(builder, notb, i64_type, "ext"), 0 };
    }
    return operand;
}

// ── CALL ────────────────────────────────────────────────────

LLVMCodegen::TypedValue LLVMCodegen::codegen_call(const Expr& expr) {
    std::string name = expr.func_name;

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
        if (obj.tag == 1) {
            LLVMValueRef as_i64 = pun_f64_to_i64(obj.val);
            obj_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
        } else if (obj.tag == 0) {
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
                    args.push_back(coerce_to(av, expected == 2 ? i8_ptr_type : f64_type));
                }
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
                if (fi.return_tag == -1) {
                    LLVMBuildCall2(builder, fn_type, fi.fn,
                                   args.data(), (unsigned)args.size(), "");
                    return { LLVMConstInt(i64_type, 0, 0), 0 };
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
                            args.push_back(coerce_to(av, expected == 2 ? i8_ptr_type : f64_type));
                        }
                        LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
                        if (fi.return_tag == -1) {
                            LLVMBuildCall2(builder, fn_type, fi.fn,
                                           args.data(), (unsigned)args.size(), "");
                            return { LLVMConstInt(i64_type, 0, 0), 0 };
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

    // 1. Try user-defined function
    auto uit = user_functions.find(name);
    if (uit != user_functions.end()) {
        auto& fi = uit->second;
        std::vector<LLVMValueRef> args;
        for (size_t i = 0; i < expr.args.size(); i++) {
            TypedValue av = codegen_expr(*expr.args[i]);
            int expected_tag = (i < fi.param_tags.size()) ? fi.param_tags[i] : 1;
            args.push_back(coerce_to(av, expected_tag == 2 ? i8_ptr_type : f64_type));
        }
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(fi.fn);
        if (fi.return_tag == -1) {
            LLVMBuildCall2(builder, fn_type, fi.fn,
                           args.empty() ? nullptr : args.data(),
                           (unsigned)args.size(), "");
            return { LLVMConstInt(i64_type, 0, 0), 0 };
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

    if (upper == "FORMAT$" && expr.args.size() >= 2) {
        // FORMAT$(fmt_string, arg1, [arg2, [arg3, [arg4]]])
        TypedValue fmt_tv = codegen_expr(*expr.args[0]);
        int nargs = (int)expr.args.size() - 1;
        if (nargs > 4) nargs = 4;

        std::string fn_name = "__format" + std::to_string(nargs);
        auto fit2 = runtime_funcs.find(fn_name);
        if (fit2 != runtime_funcs.end()) {
            std::vector<LLVMValueRef> args;
            args.push_back(fmt_tv.val);
            for (int i = 0; i < nargs; i++) {
                TypedValue av = codegen_expr(*expr.args[i + 1]);
                if (av.tag == 0) av.val = LLVMBuildSIToFP(builder, av.val, f64_type, "itof");
                args.push_back(av.val);
            }
            LLVMValueRef result = LLVMBuildCall2(builder, fit2->second.fn_type, fit2->second.fn,
                                                  args.data(), (unsigned)args.size(), "fmt");
            return { result, 2 };
        }
    }

    // Handle SELECT/FILTER/REDUCE with lambda function pointers
    if ((upper == "SELECT" || upper == "FILTER") && expr.args.size() >= 2) {
        TypedValue fn_val = codegen_expr(*expr.args[0]);
        TypedValue arr_val = codegen_expr(*expr.args[1]);
        if (fn_val.tag == 5) {
            // Lambda function pointer + array
            LLVMValueRef arr_ptr = arr_val.val;
            if (arr_val.tag == 1) {
                LLVMValueRef as_i64 = pun_f64_to_i64(arr_val.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            }
            std::string rt_name = (upper == "SELECT") ? "__select_fn" : "__filter_fn";
            auto& fn = runtime_funcs[rt_name];
            LLVMValueRef args[] = { fn_val.val, arr_ptr };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 2, "hof");
            return { result, 3 };  // returns array
        }
    }
    if (upper == "REDUCE" && expr.args.size() >= 2) {
        TypedValue fn_val = codegen_expr(*expr.args[0]);
        TypedValue arr_val = codegen_expr(*expr.args[1]);
        double init = 0.0;
        if (fn_val.tag == 5) {
            LLVMValueRef arr_ptr = arr_val.val;
            if (arr_val.tag == 1) {
                LLVMValueRef as_i64 = pun_f64_to_i64(arr_val.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            }
            LLVMValueRef init_val = (expr.args.size() >= 3)
                ? codegen_expr(*expr.args[2]).val
                : LLVMConstReal(f64_type, 0.0);
            if (expr.args.size() >= 3) {
                TypedValue iv = codegen_expr(*expr.args[2]);
                init_val = iv.tag == 0 ? LLVMBuildSIToFP(builder, iv.val, f64_type, "itof") : iv.val;
            }
            auto& fn = runtime_funcs["__reduce_fn"];
            LLVMValueRef args[] = { fn_val.val, arr_ptr, init_val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "red");
            return { result, 1 };
        }
    }

    // Handle PUSH: PUSH arr, val → arr = APPEND(arr, val)
    if (upper == "PUSH" && expr.args.size() >= 2) {
        TypedValue arr_tv = codegen_expr(*expr.args[0]);
        TypedValue val_tv = codegen_expr(*expr.args[1]);
        LLVMValueRef fval = val_tv.val;
        if (val_tv.tag == 0) fval = LLVMBuildSIToFP(builder, fval, f64_type, "itof");
        else if (val_tv.tag == 2 || val_tv.tag == 3) {
            LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, fval, i64_type, "ptoi");
            fval = pun_i64_to_f64(as_i64);
        }
        auto& append_fn = runtime_funcs["APPEND"];
        LLVMValueRef arr_ptr = coerce_to(arr_tv, i8_ptr_type);
        LLVMValueRef args[] = { arr_ptr, fval };
        LLVMValueRef result = LLVMBuildCall2(builder, append_fn.fn_type, append_fn.fn, args, 2, "push");
        if (expr.args[0]->kind == ExprKind::VARIABLE) {
            VarInfo* vi = lookup_var(expr.args[0]->str_val);
            if (vi) {
                LLVMBuildStore(builder, result, vi->alloca_val);
                vi->tag = 3;  // ensure var is tracked as array
            }
        }
        return { result, 3 };
    }

    // Handle LEN — dispatch based on argument type (string vs array)
    if (upper == "LEN" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == 2) {
            // String length
            auto& fn = runtime_funcs["LEN$"];
            LLVMValueRef args[] = { av.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "slen");
            return { result, 0 };
        }
        if (av.tag == 3) {
            // Array length: returns outer dimension count (scalar i64).
            // For 2D shape access LEN(arr)[i], see INDEX handling below.
            auto& fn = runtime_funcs["LEN"];
            LLVMValueRef args[] = { av.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "alen");
            return { result, 0 };
        }
        // Fallback
    }

    // Handle TYPEOF — resolve type tag at compile time
    if (upper == "TYPEOF" && !expr.args.empty()) {
        TypedValue av = codegen_expr(*expr.args[0]);
        auto& fn = runtime_funcs["__typeof_tag"];
        LLVMValueRef args[] = { LLVMConstInt(i64_type, av.tag, 0) };
        LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "typeof");
        return { result, 2 };
    }

    // Special case: DATEDIFF with array arg → native jdb_datediff_vec
    if (upper == "DATEDIFF" && expr.args.size() == 3) {
        TypedValue p = codegen_expr(*expr.args[0]);
        TypedValue d1 = codegen_expr(*expr.args[1]);
        TypedValue d2 = codegen_expr(*expr.args[2]);
        if (d2.tag == 3) {
            auto& fn = runtime_funcs["__datediff_vec"];
            LLVMValueRef args[] = {
                coerce_to(p, i8_ptr_type),
                coerce_to(d1, i8_ptr_type),
                d2.val
            };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "ddv");
            return { result, 3 };
        }
    }

    // Universal auto-vectorization (mirrors VM's no_vectorize blocklist pattern):
    // Any function not in the blocklist vectorizes element-wise when ANY arg
    // is an array. Example: RIGHT$(["Atomi","Bert"], 2) → ["mi","rt"].
    //
    // This blocklist must stay in sync with vm.cpp's no_vectorize map.
    // Kept alphabetically-ish by module for maintenance.
    static const std::unordered_set<std::string> no_vectorize = {
        // Array producers
        "ZEROS", "ONES", "__MAKE_UDT_ARRAY__", "IOTA", "RESHAPE", "TENSOR",
        "RANGE", "LINSPACE",
        // Array/matrix operations that consume arrays as a whole
        "LEN", "PUSH", "POP", "APPEND", "DIFF", "TAKE", "DROP", "REVERSE",
        "UNIQUE", "SHUFFLE", "FIND_IN_ARRAY", "NORMALIZE", "DISTANCE",
        "GRADE", "TRANSPOSE", "MATMUL", "MVLET", "STACK", "SLICE", "SOLVE",
        "INVERT", "CONVOLVE", "PLACE", "OUTER", "ROTATE", "SHIFT", "XSORT",
        "INTEGRATE", "FLATTEN", "ZIP", "DOT", "CROSS", "CUMSUM", "CUMPROD",
        "HISTOGRAM", "COUNT", "INDEXOF", "SORT",
        // Aggregations
        "SUM", "PRODUCT", "MIN", "MAX", "ANY", "ALL",
        "MEAN", "MEDIAN", "VARIANCE", "STDEV",
        // Higher-order
        "SCAN", "SELECT", "FILTER", "REDUCE",
        // Meta/type
        "TYPEOF", "IIF", "ISNUM", "ISSTR", "ISARR", "ISMAP", "ISBOOL",
        "ISNONE", "ISNULL",
        // Scalar-returning date/time (note: DATEADD/DATEDIFF/FORMAT_DATE DO vectorize)
        "GETENV$", "SETLOCALE", "TICK", "NOW", "NOW_EPOCH",
        "DATE$", "TIME$", "CVDATE", "RANDOMSEED",
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
        "PATH.JOIN$", "PATH.BASENAME$", "PATH.EXT$",
        // Execution
        "EXECUTE", "EVAL", "LOAD", "SAVE", "LIST", "HELP", "HELP$", "VARS",
        "RECUR", "CLEAR_RECUR", "LIST_RECUR",
        // Threads/async/react
        "AWAIT", "THREAD.ISDONE", "THREAD.GETRESULT",
        "REACT_BIND", "UNREACT",
        // FFI/internals
        "__EVENT_ON", "__EVENT_RAISE", "__FFI_DECLARE",
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

    // Try native vectorization first — avoids VM bridge overhead.
    {
        auto vit = native_vec.find(upper);
        if (vit != native_vec.end() && !expr.args.empty()) {
            TypedValue av = codegen_expr(*expr.args[0]);
            if (av.tag == 3) {
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
                    return { result, 3 };
                }
                if (strcmp(spec.sig, "sfi") == 0 && expr.args.size() == 2) {
                    TypedValue nv = codegen_expr(*expr.args[1]);
                    LLVMValueRef n = coerce_to(nv, i64_type);
                    LLVMValueRef args[] = { av.val, n, scalar_fn };
                    LLVMValueRef result = LLVMBuildCall2(builder, applier->fn_type,
                        applier->fn, args, 3, "vec");
                    return { result, 3 };
                }
                if (strcmp(spec.sig, "sfii") == 0 && expr.args.size() == 3) {
                    TypedValue av2 = codegen_expr(*expr.args[1]);
                    TypedValue av3 = codegen_expr(*expr.args[2]);
                    LLVMValueRef a2 = coerce_to(av2, i64_type);
                    LLVMValueRef a3 = coerce_to(av3, i64_type);
                    LLVMValueRef args[] = { av.val, a2, a3, scalar_fn };
                    LLVMValueRef result = LLVMBuildCall2(builder, applier->fn_type,
                        applier->fn, args, 4, "vec");
                    return { result, 3 };
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
            if (v.tag == 3) has_array = true;
        }
        if (has_array) {
            // Special case: DATEDIFF vectorization has a native fast path
            if (upper == "DATEDIFF" && vals.size() == 3 && vals[2].tag == 3 && vals[1].tag != 3) {
                auto& fn = runtime_funcs["__datediff_vec"];
                LLVMValueRef a0 = coerce_to(vals[0], i8_ptr_type);
                LLVMValueRef a1 = coerce_to(vals[1], i8_ptr_type);
                LLVMValueRef args[] = { a0, a1, vals[2].val };
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 3, "ddv");
                return { result, 3 };
            }
            // Generic: dispatch via VM bridge for vectorized apply
            LLVMValueRef handle = LLVMBuildLoad2(builder, i8_ptr_type,
                LLVMGetNamedGlobal(module, "__jdrt_handle"), "rt");
            int nargs = (int)vals.size();
            LLVMValueRef args_p = LLVMBuildArrayAlloca(builder, i64_type,
                LLVMConstInt(i32_type, nargs, 0), "args");
            LLVMValueRef tags_p = LLVMBuildArrayAlloca(builder, i32_type,
                LLVMConstInt(i32_type, nargs, 0), "tags");
            for (int i = 0; i < nargs; i++) {
                TypedValue av = vals[i];
                LLVMValueRef encoded; int32_t tg;
                if (av.tag == 2 || av.tag == 3) {
                    encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "ptoi"); tg = av.tag;
                } else if (av.tag == 1) {
                    encoded = pun_f64_to_i64(av.val); tg = 1;
                } else {
                    LLVMValueRef f = LLVMBuildSIToFP(builder, av.val, f64_type, "itof");
                    encoded = pun_f64_to_i64(f); tg = 0;
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
            return { result, 3 };
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
        if (av.tag == 2) {  // string arg → use _str variant
            auto sit = runtime_funcs.find(dit->second);
            if (sit != runtime_funcs.end()) {
                LLVMValueRef args[] = { av.val };
                LLVMValueRef result = LLVMBuildCall2(builder, sit->second.fn_type,
                    sit->second.fn, args, 1, "dt");
                return { result, 0 };
            }
        }
        // Fall through to f64 variant via generic runtime lookup below
    }

    // Handle IIF with strings: IIF(cond, str1, str2) → VM bridge
    if (upper == "IIF" && expr.args.size() == 3) {
        TypedValue cond = codegen_expr(*expr.args[0]);
        TypedValue val1 = codegen_expr(*expr.args[1]);
        TypedValue val2 = codegen_expr(*expr.args[2]);
        if (val1.tag == 2 || val2.tag == 2) {
            // String IIF: use native select
            LLVMValueRef cond_i1 = to_i1(cond);
            // Ensure both are strings
            auto to_str_iif = [&](TypedValue tv) -> LLVMValueRef {
                if (tv.tag == 2) return tv.val;
                if (tv.tag == 0) {
                    auto& fn = runtime_funcs["__int_to_str"];
                    LLVMValueRef a[] = { tv.val };
                    return LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 1, "itostr");
                }
                auto& fn = runtime_funcs["__double_to_str"];
                LLVMValueRef a[] = { tv.val };
                return LLVMBuildCall2(builder, fn.fn_type, fn.fn, a, 1, "ftostr");
            };
            LLVMValueRef result = LLVMBuildSelect(builder, cond_i1,
                to_str_iif(val1), to_str_iif(val2), "iif");
            return { result, 2 };
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
            if (av.tag == 1) {
                LLVMValueRef as_i64 = pun_f64_to_i64(av.val);
                arr_ptr = LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "itoptr");
            }
            LLVMValueRef args[] = { arr_ptr };
            LLVMValueRef result = LLVMBuildCall2(builder, ait->second.fn_type, ait->second.fn, args, 1, "call");
            return { result, ait->second.return_tag };
        }
    }

    // Handle VAL with pointer-encoded doubles (from OS.ARGS array elements)
    if (upper == "VAL" && expr.args.size() == 1) {
        TypedValue av = codegen_expr(*expr.args[0]);
        if (av.tag == 1) {
            // f64 value — might be a pointer-encoded string from OS.ARGS
            auto& fn = runtime_funcs["__val_ptr"];
            LLVMValueRef args[] = { av.val };
            LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, args, 1, "val");
            return { result, 1 };
        }
    }

    // 3. Try runtime builtin (uppercase lookup)
    // (Vectorization for array args was handled in the block above.)
    auto rit = runtime_funcs.find(upper);
    if (rit != runtime_funcs.end()) {
        auto& rf = rit->second;
        std::vector<LLVMValueRef> args;
        unsigned param_count = LLVMCountParamTypes(rf.fn_type);
        std::vector<LLVMTypeRef> param_types(param_count);
        if (param_count > 0) LLVMGetParamTypes(rf.fn_type, param_types.data());

        for (size_t i = 0; i < expr.args.size() && i < param_count; i++) {
            TypedValue av = codegen_expr(*expr.args[i]);
            args.push_back(coerce_to(av, param_types[i]));
        }

        if (rf.return_tag == -1) {
            LLVMBuildCall2(builder, rf.fn_type, rf.fn,
                           args.empty() ? nullptr : args.data(),
                           (unsigned)args.size(), "");
            return { LLVMConstInt(i64_type, 0, 0), 0 };
        } else {
            LLVMValueRef result = LLVMBuildCall2(builder, rf.fn_type, rf.fn,
                                                  args.empty() ? nullptr : args.data(),
                                                  (unsigned)args.size(), "call");
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
                // Allocate i64[] for args and i32[] for type tags
                args_ptr = LLVMBuildArrayAlloca(builder, i64_type,
                    LLVMConstInt(i32_type, nargs, 0), "args");
                tags_ptr = LLVMBuildArrayAlloca(builder, i32_type,
                    LLVMConstInt(i32_type, nargs, 0), "tags");

                for (int i = 0; i < nargs; i++) {
                    TypedValue av = codegen_expr(*expr.args[i]);

                    // Convert value to i64 encoding and determine tag
                    LLVMValueRef encoded;
                    int32_t tag;

                    if (av.tag == 2) {
                        // String: pointer → intptr → i64
                        encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "stoi");
                        tag = 2;
                    } else if (av.tag == 3) {
                        // Array pointer → i64
                        encoded = LLVMBuildPtrToInt(builder, av.val, i64_type, "atoi");
                        tag = 3;
                    } else if (av.tag == 4) {
                        // VM object handle — already i64
                        encoded = av.val;
                        tag = 4;
                    } else if (av.tag == 1) {
                        // f64 → bitcast to i64 so bridge can recover double
                        encoded = pun_f64_to_i64(av.val);
                        tag = 1;
                    } else {
                        // i64 direct — no conversion (preserves exact value for large ints)
                        encoded = av.val;
                        tag = 0;
                    }

                    // Store arg value
                    LLVMValueRef aidx[] = { LLVMConstInt(i32_type, i, 0) };
                    LLVMValueRef aptr = LLVMBuildGEP2(builder, i64_type, args_ptr, aidx, 1, "arg");
                    LLVMBuildStore(builder, encoded, aptr);

                    // Store type tag
                    LLVMValueRef tidx[] = { LLVMConstInt(i32_type, i, 0) };
                    LLVMValueRef tptr = LLVMBuildGEP2(builder, i32_type, tags_ptr, tidx, 1, "tag");
                    LLVMBuildStore(builder, LLVMConstInt(i32_type, tag, 0), tptr);
                }
            } else {
                args_ptr = LLVMConstNull(i8_ptr_type);
                tags_ptr = LLVMConstNull(i8_ptr_type);
            }

            bool is_string_fn = !upper.empty() && upper.back() == '$';
            // Functions that return objects (MAP, JSON.PARSE, etc.)
            bool is_object_fn = (upper.substr(0, 4) == "MAP." &&
                                 upper != "MAP.SIZE" && upper != "MAP.EXISTS") ||
                                upper == "JSON.PARSE$";
            // Functions that return arrays (decoded as ptr/tag=3)
            bool is_array_fn = (upper == "SPLIT" || upper == "KEYS" || upper == "VALUES" ||
                                upper == "SORTBY" || upper == "GROUPBY" || upper == "REGEX.FINDALL" ||
                                upper == "REGEX_MATCH" || upper == "REGEX_FINDALL" ||
                                upper == "OS.LIST" || upper == "OS.ARGS" || upper == "JSON.STRINGIFY$" ||
                                upper == "MAP.KEYS" || upper == "MAP.VALUES" ||
                                upper == "LINES" || upper == "WORDS" || upper == "CHARS" ||
                                upper == "UNPACK");

            LLVMValueRef call_args[] = { handle, name_str, args_ptr, tags_ptr,
                LLVMConstInt(i32_type, nargs, 0) };

            if (is_array_fn) {
                // Returns JdbArray* directly via dedicated bridge function
                auto& fn = runtime_funcs["__jdrt_call_typed_arr"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmarr");
                return { result, 3 };
            } else if (is_object_fn) {
                // Returns an opaque VM value handle (stored as i64)
                auto& fn = runtime_funcs["__jdrt_call_typed_obj"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmobj");
                return { result, 4 };  // tag=4 = VM object handle
            } else if (is_string_fn) {
                auto& fn = runtime_funcs["__jdrt_call_typed_str"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmcall");
                return { result, 2 };
            } else {
                auto& fn = runtime_funcs["__jdrt_call_typed_f64"];
                LLVMValueRef result = LLVMBuildCall2(builder, fn.fn_type, fn.fn, call_args, 5, "vmcall");
                return { result, 1 };
            }
        }
    }

    return { LLVMConstInt(i64_type, 0, 0), 0 };
}

// ── Helpers ─────────────────────────────────────────────────

LLVMValueRef LLVMCodegen::to_i1(TypedValue tv) {
    if (tv.tag == 1)
        return LLVMBuildFCmp(builder, LLVMRealONE, tv.val,
                             LLVMConstReal(f64_type, 0.0), "tobool");
    if (tv.tag == 2 || tv.tag == 3) {
        // ptr types: non-null = true
        LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, tv.val, i64_type, "ptoi");
        return LLVMBuildICmp(builder, LLVMIntNE, as_i64,
                             LLVMConstInt(i64_type, 0, 0), "tobool");
    }
    return LLVMBuildICmp(builder, LLVMIntNE, tv.val,
                         LLVMConstInt(i64_type, 0, 0), "tobool");
}

LLVMCodegen::TypedValue LLVMCodegen::promote_to_f64(TypedValue tv) {
    if (tv.tag == 1) return tv;
    if (tv.tag == 0)
        return { LLVMBuildSIToFP(builder, tv.val, f64_type, "itof"), 1 };
    return { LLVMConstReal(f64_type, 0.0), 1 };
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
    if (e.kind == ExprKind::LITERAL_STRING) return true;
    if (e.kind == ExprKind::CALL && !e.func_name.empty() && e.func_name.back() == '$') return true;
    if (e.left && expr_involves_strings(*e.left)) return true;
    if (e.right && expr_involves_strings(*e.right)) return true;
    for (auto& a : e.args) if (a && expr_involves_strings(*a)) return true;
    return false;
}

LLVMValueRef LLVMCodegen::coerce_to(TypedValue tv, LLVMTypeRef target) {
    if (target == f64_type) {
        if (tv.tag == 0) return LLVMBuildSIToFP(builder, tv.val, f64_type, "itof");
        if (tv.tag == 2 || tv.tag == 3 || tv.tag == 5) {
            LLVMValueRef as_i64 = LLVMBuildPtrToInt(builder, tv.val, i64_type, "ptoi");
            return pun_i64_to_f64(as_i64);
        }
        return tv.val;
    }
    if (target == i64_type) {
        if (tv.tag == 1) return LLVMBuildFPToSI(builder, tv.val, i64_type, "ftoi");
        if (tv.tag == 2 || tv.tag == 3 || tv.tag == 5)
            return LLVMBuildPtrToInt(builder, tv.val, i64_type, "ptoi");
        return tv.val;
    }
    if (target == i8_ptr_type) {
        if (tv.tag == 0) return LLVMBuildIntToPtr(builder, tv.val, i8_ptr_type, "itoptr");
        if (tv.tag == 1) {
            LLVMValueRef as_i64 = pun_f64_to_i64(tv.val);
            return LLVMBuildIntToPtr(builder, as_i64, i8_ptr_type, "ftoptr");
        }
        return tv.val;
    }
    return tv.val;
}

LLVMCodegen::TypedValue LLVMCodegen::coerce_to_tag(TypedValue tv, int target_tag) {
    if (tv.tag == target_tag) return tv;
    if (target_tag == 1) return { coerce_to(tv, f64_type), 1 };
    if (target_tag == 0) return { coerce_to(tv, i64_type), 0 };
    if (target_tag == 2 || target_tag == 3) return { coerce_to(tv, i8_ptr_type), target_tag };
    return tv;
}

void LLVMCodegen::emit_trace(int line) {
    if (!debug_log) return;
    auto* tr = get_runtime_func("__trace");
    if (!tr) return;
    LLVMValueRef args[] = { LLVMConstInt(i64_type, line, 0) };
    LLVMBuildCall2(builder, tr->fn_type, tr->fn, args, 1, "");
}

LLVMCodegen::RuntimeFunc* LLVMCodegen::get_runtime_func(const std::string& name) {
    auto it = runtime_funcs.find(name);
    if (it == runtime_funcs.end()) return nullptr;
    if (!it->second.fn) return nullptr;
    return &it->second;
}

// ── Object File Emission ────────────────────────────────────

bool LLVMCodegen::emit_object_file(const std::string& obj_path) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    char* triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char* err = nullptr;

    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        error_msg = "Failed to get target: " + std::string(err);
        LLVMDisposeMessage(err);
        LLVMDisposeMessage(triple);
        return false;
    }

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);

    LLVMSetModuleDataLayout(module, LLVMCreateTargetDataLayout(machine));

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
                                   const std::string& exe_path) {
    std::string runtime_obj;
    for (auto& candidate : {"build\\jdb_runtime.obj", "jdb_runtime.obj"}) {
        if (std::filesystem::exists(candidate)) {
            runtime_obj = candidate;
            break;
        }
    }
    if (runtime_obj.empty()) {
        error_msg = "Cannot find jdb_runtime.obj. Build with NATIVEC flag first.";
        return false;
    }

    std::string msvc = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207";
    std::string sdk = "C:\\Program Files (x86)\\Windows Kits\\10";
    std::string sdkv = "10.0.26100.0";

    std::string link_cmd =
        "cmd /c \"\"" + msvc + "\\bin\\Hostx64\\x64\\link.exe\" "
        "/NOLOGO /OUT:\"" + exe_path + "\" "
        "/SUBSYSTEM:CONSOLE "
        "\"" + obj_path + "\" "
        "\"" + runtime_obj + "\" "
        "/LIBPATH:\"" + msvc + "\\lib\\x64\" "
        "/LIBPATH:\"" + sdk + "\\Lib\\" + sdkv + "\\ucrt\\x64\" "
        "/LIBPATH:\"" + sdk + "\\Lib\\" + sdkv + "\\um\\x64\" "
        "libcmt.lib libucrt.lib kernel32.lib legacy_stdio_definitions.lib "
        "\"build\\jdbrt.lib\"\"";

    int ret = std::system(link_cmd.c_str());
    if (ret != 0) {
        error_msg = "Linker failed (exit code " + std::to_string(ret) + ")";
        return false;
    }
    return true;
}

#endif // LLVM_CODEGEN
