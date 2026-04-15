#pragma once
#ifdef LLVM_CODEGEN

#include <string>
#include <vector>
#include <unordered_map>
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

    TypedValue codegen_expr(const Expr& expr);
    TypedValue codegen_binary(const Expr& expr);
    TypedValue codegen_unary(const Expr& expr);
    TypedValue codegen_call(const Expr& expr);

    // Helpers
    LLVMValueRef to_i1(TypedValue tv);
    TypedValue promote_to_f64(TypedValue tv);
    // Type-pun between i64 and f64 (avoids LLVM bitcast restrictions on newer versions)
    LLVMValueRef pun_i64_to_f64(LLVMValueRef i64_val);
    LLVMValueRef pun_f64_to_i64(LLVMValueRef f64_val);

    // Emit object file and link
    bool emit_object_file(const std::string& obj_path);
    bool link_executable(const std::string& obj_path,
                         const std::string& exe_path);
};

#endif // LLVM_CODEGEN
