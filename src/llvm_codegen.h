#pragma once
#ifdef LLVM_CODEGEN

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include "ast.h"
#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Analysis.h"

class LLVMCodegen {
public:
    LLVMCodegen();
    ~LLVMCodegen();

    bool compile(const std::vector<StmtPtr>& program,
                 const std::string& output_exe);
    bool emit_ir(const std::vector<StmtPtr>& program);

    std::string error_msg;
    bool debug_log = false;  // emit line-by-line runtime trace

    // Transient codegen state: true while evaluating an expression whose
    // result will be the LEFT of an INDEX chain (e.g. inner `a{"b"}` of
    // `a{"b"}{"c"}`). Consumers return a raw map ptr (tag=4) instead of a
    // formatted string so the outer INDEX can traverse further.
    bool m_want_ptr_result = false;

private:
    LLVMContextRef ctx;
    LLVMModuleRef  module;
    LLVMBuilderRef builder;

    // Current function being built
    LLVMValueRef current_fn;

    // Variable storage per scope: name -> {alloca, tag}
    struct VarInfo {
        LLVMValueRef alloca_val;
        int tag;  // 0=i64, 1=f64, 2=string(i8*), 3=array(JdbArray*)
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

    // LLVM types
    LLVMTypeRef i64_type;
    LLVMTypeRef f64_type;
    LLVMTypeRef i8_ptr_type;
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
        int tag;  // 0=i64, 1=f64, 2=string(i8*)
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
    void emit_trace(int line);
    // Emit a divisor-zero check: if rhs == 0, record "Division by zero"
    // and branch to the top-of-stack catch block (or abort if none).
    // Caller positions the builder in the normal-path block afterwards.
    void emit_div_zero_check(TypedValue rhs);
    RuntimeFunc* get_runtime_func(const std::string& name);
    // Coerce a TypedValue to the expected LLVM type (prevents type mismatches)
    LLVMValueRef coerce_to(TypedValue tv, LLVMTypeRef target);
    TypedValue coerce_to_tag(TypedValue tv, int target_tag);
    // Type-pun between i64 and f64 (avoids LLVM bitcast restrictions on newer versions)
    LLVMValueRef pun_i64_to_f64(LLVMValueRef i64_val);
    LLVMValueRef pun_f64_to_i64(LLVMValueRef f64_val);

    // Emit object file and link
    bool emit_object_file(const std::string& obj_path);
    bool link_executable(const std::string& obj_path,
                         const std::string& exe_path);
};

#endif // LLVM_CODEGEN
