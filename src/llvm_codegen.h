#pragma once
#ifdef LLVM_CODEGEN

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include "ast.h"
#include "jdb_tags.h"
#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Analysis.h"

class LLVMCodegen {
public:
    LLVMCodegen();
    ~LLVMCodegen();

    bool compile(const std::vector<StmtPtr>& program,
                 const std::string& output_exe,
                 const std::string& source_path = "");
    bool emit_ir(const std::vector<StmtPtr>& program);

    std::string error_msg;
    bool debug_log = false;  // emit line-by-line runtime trace

    // STRICT/EXPLICIT compile mode. Defaults are OFF during the staged
    // rollout (Phases 3/4 add enforcement, Phase 6 migrates the stdlib
    // + tests, then the defaults flip to true). Opt in per-translation-
    // unit today via OPTION "EXPLICIT" / OPTION "STRICT" at the top of
    // a source file. Opt out later via OPTION "EXPLICITOFF"/"NOSTRICT".
    //   explicit_mode: undeclared variable use is a compile error.
    //   strict_mode:   every DIM needs a declared type, every assignment/call
    //                  is type-checked, diagnostics include file:line.
    bool explicit_mode = false;
    bool strict_mode = false;

    // Accumulated compile diagnostics under STRICT/EXPLICIT. Reported after
    // the codegen pass so the user sees every mismatch, not just the first.
    struct Diag { std::string file; int line; std::string msg; };
    std::vector<Diag> diagnostics;
    void report_error(const std::string& file, int line, const std::string& msg) {
        diagnostics.push_back({file, line, msg});
    }

    // Phase 2: compile-time type label. Collapses the parser's width-specific
    // VarType enum into coarse buckets — INTEGER covers all int widths,
    // NUMBER covers all float widths. Width-strict checking can be layered
    // on later if needed; for now the pivot only cares about ptr-vs-scalar
    // mismatches (the class of bug that slipped past the LLVM verifier).
    // STRICT uses this to validate assignments / calls / returns; EXPLICIT
    // uses it to detect uses of undeclared variables (kind == UNKNOWN).
    struct StaticType {
        enum class Kind {
            UNKNOWN,   // not declared — under EXPLICIT, a use is an error
            ANY,       // explicit opt-out (reserved for AS ANY, future)
            INTEGER, NUMBER, STRING, BOOLEAN, DATE,
            ARRAY, MAP, UDT, TENSOR, FUNCREF
        };
        Kind kind = Kind::UNKNOWN;
        std::string name;                    // UDT name (UDT / ARRAY[UDT])
        std::shared_ptr<StaticType> elem;    // ARRAY element type when known

        bool is_unknown() const { return kind == Kind::UNKNOWN; }
        std::string describe() const;
        static StaticType from_vartype(VarType vt, VarType et, const std::string& udt_name);
    };

    // name → declared type. Populated by populate_type_env() from the
    // program's top-level DIMs. Phase 3/4 will extend to FUNC params/locals.
    std::unordered_map<std::string, StaticType> type_env;
    void populate_type_env(const std::vector<StmtPtr>& program);

    // Per-file OPTION scope: each imported .jdb file sets its own flags.
    // A strict program can IMPORT a loose module without cascading the
    // strictness. The pre-scan builds these from top-level OPTION_STMTs
    // grouped by their source_file; diagnostic sites consult them via
    // is_explicit_here()/is_strict_here() instead of the global bools.
    std::unordered_set<std::string> explicit_files;
    std::unordered_set<std::string> strict_files;
    bool is_explicit_here(const std::string& file) const {
        if (explicit_files.count(file)) return true;
        return explicit_mode && file.empty();  // inline toggles / main
    }
    bool is_strict_here(const std::string& file) const {
        if (strict_files.count(file)) return true;
        return strict_mode && file.empty();
    }

    // Phase 4: infer the static type of an expression. Conservative —
    // returns UNKNOWN whenever the shape can't be determined without
    // running codegen. Callers treat UNKNOWN as "no check possible".
    StaticType infer_expr_type(const Expr& e) const;
    // Assignment / argument compatibility: can a value of type `src`
    // be used where `dst` is expected? UNKNOWN on either side accepts
    // (avoid false positives while coverage is still incomplete).
    static bool types_compatible(const StaticType& src, const StaticType& dst);

    // Source file of the statement currently being codegen'd. Diagnostics
    // raised from deep inside codegen_expr use this for the file: prefix
    // (Expr itself doesn't carry source_file — only Stmt does).
    std::string m_current_stmt_file;

    // Transient codegen state: true while evaluating an expression whose
    // result will be the LEFT of an INDEX chain (e.g. inner `a{"b"}` of
    // `a{"b"}{"c"}`). Consumers return a raw map ptr (tag=4) instead of a
    // formatted string so the outer INDEX can traverse further.
    bool m_want_ptr_result = false;

    // Transient: when set to 0/1/2, an INDEX leaf on a Map (native tag=4 or
    // VM handle tag=6) picks the matching get_* runtime function so the
    // value comes back with the right type instead of always-stringified.
    // -1 = no preference (default = stringify, matches legacy behavior).
    int m_want_leaf_tag = -1;

private:
    LLVMContextRef ctx;
    LLVMModuleRef  module;
    LLVMBuilderRef builder;

    // Current function being built
    LLVMValueRef current_fn;

    struct VarInfo {
        LLVMValueRef alloca_val;
        int tag;  // JdTag (see jdb_tags.h)
        LLVMValueRef runtime_tag_alloca = nullptr;  // i32 runtime type; used when tag == JD_TAG_RUNTIME
    };

    // Scope stack for local variables (functions push/pop scopes)
    struct Scope {
        std::unordered_map<std::string, VarInfo> vars;
    };
    std::vector<Scope> scopes;  // scopes[0] = global, scopes.back() = current

    VarInfo* lookup_var(const std::string& name);
    VarInfo& create_var(const std::string& name, int tag);

    // User-defined functions: name -> {LLVMValueRef, return_tag, param_tags}
    struct FuncInfo {
        LLVMValueRef fn;
        int return_tag;             // 0=i64, 1=f64, 2=str, -1=void(sub)
        std::vector<int> param_tags;
    };
    std::unordered_map<std::string, FuncInfo> user_functions;

    // Loop control: stack of {break_bb, continue_bb} for EXITDO/EXITFOR/CONTINUE
    struct LoopCtx {
        LLVMBasicBlockRef break_bb;
        LLVMBasicBlockRef continue_bb;
    };
    std::stack<LoopCtx> loop_stack;

    // TRY/CATCH: stack of catch-block labels. THROW and guarded div-by-zero
    // branch to try_stack.back() when non-empty, else call jdb_throw_uncaught.
    std::vector<LLVMBasicBlockRef> try_stack;

    // CONST tracking: names (upper-cased) bound to user-declared constants.
    // Subsequent assignments throw at runtime.
    std::unordered_set<std::string> const_vars;

    // Booleans are stored as i64 in codegen, but TYPEOF must still report
    // "BOOLEAN" for vars declared AS BOOLEAN or initialized with TRUE/FALSE.
    std::unordered_set<std::string> bool_vars;

    // Variables initialised from CVDATE/DATEADD/NOW are tagged DATE for TYPEOF.
    std::unordered_set<std::string> date_vars;

    // SUBs registered as event handlers via `ON "X" CALL Handler` — their
    // first parameter is forced to tag=3 (JdbArray*) so RAISEEVENT can
    // pass a packed args array.
    std::unordered_set<std::string> event_handler_subs;

    // Vars whose array elements are known to be strings — INDEX returns tag=2
    // (decoded char*) for these. Currently populated for the data param of
    // event handler SUBs.
    std::unordered_set<std::string> string_array_vars;

    // Vars whose array elements are known to hold map pointers — INDEX decodes
    // the punned-f64 and returns tag=4 so `q = arr[i]; q{"k"} = v` mutates
    // the shared map instead of silently bailing in INDEX_ASSIGN.
    std::unordered_set<std::string> map_array_vars;

    // Vars whose array elements are known to hold VM value handles (tag=6):
    // JSON-parsed nested maps, MAP.* results, etc. PUSH stores the raw i64
    // handle bits via pun_i64_to_f64; INDEX read puns back and returns tag=6
    // so MAP_ACCESS routes through __jdrt_obj_get_* instead of the native
    // JdbMap path (which would crash on a non-ptr handle).
    std::unordered_set<std::string> vm_array_vars;

    // Maps a top-level-DIM'd global name to the source file it came from.
    // Used by codegen_let_or_assign to decide whether an implicit assignment
    // inside a SUB should update that global (same file) or create a local
    // alloca (different file / imported module).
    std::unordered_map<std::string, std::string> global_source_file;

    // source_file of the FUNC/SUB currently being codegen'd. Empty while
    // codegen runs over main-level statements.
    std::string current_fn_source_file;

    // Unified exit block for the user FUNC/SUB currently being codegen'd.
    // Every RET in the body branches here instead of emitting ret directly,
    // so the recursion-guard leave can be placed in one spot and the error
    // unwind path has a target to jump to. nullptr when codegen is in main.
    LLVMBasicBlockRef current_exit_bb = nullptr;
    LLVMValueRef      current_retval_alloca = nullptr; // null for void / main

    // Emit either "store val→retval; br exit_bb" (inside a user FUNC/SUB,
    // so the guard leave runs on the common exit path) or a plain ret
    // (main). val is ignored for void functions; pass nullptr to return
    // the function's default zero value.
    void emit_fn_return(LLVMValueRef val);

    // After a call that may have set g_err_code, check and branch:
    //   - TRY active here → try_stack.back()
    //   - inside a user FUNC → propagate by exiting the function with
    //     the default retval (caller's per-stmt check picks it up)
    //   - inside main with no TRY → __throw_uncaught + unreachable
    // The builder is left positioned at the fall-through ok block.
    void emit_err_check();

    // LLVM types
    LLVMTypeRef i64_type;
    LLVMTypeRef f64_type;
    LLVMTypeRef i8_ptr_type;
    LLVMTypeRef tagged_val_type;  // {i64, i32} — JdTaggedVal
    LLVMTypeRef i32_type;
    LLVMTypeRef void_type;

    // Runtime function declarations (looked up by name)
    struct RuntimeFunc {
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        int return_tag;  // 0=i64, 1=f64, 2=str, -1=void
    };
    std::unordered_map<std::string, RuntimeFunc> runtime_funcs;

    // Typed value returned by expression codegen
    struct TypedValue {
        LLVMValueRef val;
        int tag;  // JdTag (see jdb_tags.h)
        LLVMValueRef runtime_tag = nullptr;  // i32 runtime type when tag == JD_TAG_RUNTIME
    };

    // Setup
    void init_module();
    void declare_runtime_functions();
    void create_main_function();

    // Pre-pass: declare all FUNC/SUB signatures before codegen
    void declare_functions(const std::vector<StmtPtr>& program);

    // Codegen
    void codegen_program(const std::vector<StmtPtr>& program);
    void codegen_stmt(const Stmt& stmt);
    void codegen_let_or_assign(const Stmt& stmt);
    void codegen_dim(const Stmt& stmt);
    void codegen_index_assign(const Stmt& stmt);
    void codegen_print(const Stmt& stmt);
    void codegen_for(const Stmt& stmt);
    void codegen_if(const Stmt& stmt);
    void codegen_do_loop(const Stmt& stmt);
    void codegen_function(const Stmt& stmt);
    void codegen_return(const Stmt& stmt);
    void codegen_switch(const Stmt& stmt);
    void codegen_for_each(const Stmt& stmt);
    void codegen_enum(const Stmt& stmt);
    void codegen_type_decl(const Stmt& stmt);

    // UDT type registry: type_name → list of {field_name, is_string}
    struct UDTField { std::string name; bool is_string; };
    std::unordered_map<std::string, std::vector<UDTField>> udt_types;

    // Variable-to-UDT-type mapping: var_name → UDT type name
    std::unordered_map<std::string, std::string> var_udt_type;

    TypedValue codegen_expr(const Expr& expr);
    TypedValue codegen_binary(const Expr& expr);
    TypedValue codegen_unary(const Expr& expr);
    TypedValue codegen_call(const Expr& expr);

    // Helpers
    LLVMValueRef to_i1(TypedValue tv);
    TypedValue promote_to_f64(TypedValue tv);
    bool is_udt_string_field(const std::string& var_name, const std::string& field_name);
    static bool expr_involves_strings(const Expr& e);
    void emit_trace(int line, const std::string& source_file = "");
    // Emit a divisor-zero check: if rhs == 0, record "Division by zero"
    // and branch to the top-of-stack catch block (or abort if none).
    // Caller positions the builder in the normal-path block afterwards.
    void emit_div_zero_check(TypedValue rhs);
    RuntimeFunc* get_runtime_func(const std::string& name);
    // Coerce a TypedValue to the expected LLVM type (prevents type mismatches)
    LLVMValueRef coerce_to(TypedValue tv, LLVMTypeRef target);
    TypedValue coerce_to_tag(TypedValue tv, int target_tag);
    // Convert any TypedValue to a real char* string ptr. For numeric tags
    // this calls __double_to_str (formatted text), not a bitcast — use this
    // when a runtime expects an actual string, not a punned ptr.
    LLVMValueRef to_string_ptr(TypedValue tv);
    // Type-pun between i64 and f64 (avoids LLVM bitcast restrictions on newer versions)
    LLVMValueRef pun_i64_to_f64(LLVMValueRef i64_val);
    LLVMValueRef pun_f64_to_i64(LLVMValueRef f64_val);

    // Emit object file and link
    bool emit_object_file(const std::string& obj_path);
    bool link_executable(const std::string& obj_path,
                         const std::string& exe_path,
                         const std::string& res_path = "");

    // If <source_path>.props exists, generate a Win32 .res with VERSIONINFO
    // (and optionally an icon). Returns the .res path, or empty string if
    // no props file was found / generation failed.
    std::string generate_version_resource(const std::string& source_path,
                                          const std::string& obj_path);
};

#endif // LLVM_CODEGEN
