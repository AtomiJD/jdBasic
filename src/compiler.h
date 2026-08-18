#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "ast.h"
#include "bytecode.h"

struct CompilerScope {
    Chunk chunk;
    std::unordered_map<std::string, uint16_t> locals;
    // Names registered as FUNC/SUB parameters. Used to reject DIM that would
    // silently alias a parameter (BASIC is case-insensitive, so `DIM v` inside
    // FUNC F(V) overwrites the param - caught the user twice in 4d benches).
    std::unordered_set<std::string> params;
    // STATIC DIM <name> registers the name here with its persistent slot
    // index (separate namespace from `locals`). Subsequent reads/writes of
    // the name inside this function emit LOAD_STATIC / STORE_STATIC.
    std::unordered_map<std::string, uint16_t> statics;
    bool is_function = false;
};

class Compiler {
public:
    Compiler();

    // Compile a program: returns the main chunk and function prototypes
    // main_source_file is set on the main chunk for debugger source mapping
    void compile(const std::vector<StmtPtr>& program, const std::string& main_source_file = "");

    Chunk& main_chunk() { return scopes[0].chunk; }
    std::vector<FuncProto>& functions() { return funcs; }

    // Type names this compilation registered. The REPL merges them into the
    // VM so a TYPE declared by one chunk is still known to the next.
    const std::unordered_set<std::string>& types() const { return user_types; }

    // Seed from a previous compilation, so `DIM x AS T` resolves against a
    // TYPE that an earlier chunk declared.
    void seed_types(const std::unordered_set<std::string>& t) {
        user_types.insert(t.begin(), t.end());
    }

private:
    std::vector<CompilerScope> scopes;
    std::vector<FuncProto> funcs;

    // Label tracking for GOTO
    std::unordered_map<std::string, size_t> label_positions;
    std::vector<std::pair<std::string, size_t>> unresolved_gotos; // label, patch_addr

    // Global variable names (for name-based lookup)
    std::unordered_map<std::string, uint16_t> globals;

    // Known user-defined type names (for DIM AS TypeName)
    std::unordered_set<std::string> user_types;

    // Type method registries (qualified "TypeName.INIT" / "TypeName.DISPOSE").
    // Used to decide whether DIM emits an INIT call and whether the runtime
    // dispose hook should fire for a given object.
    std::unordered_set<std::string> type_inits;
    // Subset of type_inits where INIT takes no user parameters - i.e. SUB INIT().
    // DIM auto-calls INIT only when this entry is present (or when the user
    // passes constructor args explicitly), preserving back-compat for code
    // that DIMs and then calls obj.INIT(args) manually.
    std::unordered_set<std::string> type_init_zero_arg;
    std::unordered_set<std::string> type_disposes;

    // Known global variable names (collected before function compilation)
    std::unordered_set<std::string> known_globals;

    // Loop context for EXITFOR/CONTINUFOR etc.
    struct LoopCtx {
        size_t continue_addr = 0;         // where CONTINUE jumps (for DO loops)
        std::vector<size_t> break_patches; // addresses to patch for EXIT
        std::vector<size_t> continue_patches; // addresses to patch for CONTINUE
        bool is_for;                      // FOR vs DO
    };
    std::vector<LoopCtx> loop_stack;

    CompilerScope& current_scope();
    Chunk& current_chunk();
    uint16_t resolve_var(const std::string& name);
    uint16_t resolve_local(const std::string& name);  // force local (for LET/DIM in functions)
    uint16_t resolve_global(const std::string& name);
    bool should_use_global(const std::string& name) const;

    // Statements
    void compile_stmt(const Stmt& stmt);
    void compile_let(const Stmt& stmt);
    void compile_dim(const Stmt& stmt);
    void compile_assign(const Stmt& stmt);
    void compile_index_assign(const Stmt& stmt);
    void compile_print(const Stmt& stmt);
    void compile_input(const Stmt& stmt);
    void compile_goto(const Stmt& stmt);
    void compile_label(const Stmt& stmt);
    void compile_if(const Stmt& stmt);
    void compile_do_loop(const Stmt& stmt);
    void compile_for(const Stmt& stmt);
    void compile_return(const Stmt& stmt);
    void compile_sub(const Stmt& stmt);
    void compile_function(const Stmt& stmt);
    void compile_expr_stmt(const Stmt& stmt);
    void compile_destructure(const Stmt& stmt);

    // Expressions
    void compile_expr(const Expr& expr);
    void compile_binary(const Expr& expr);
    void compile_unary(const Expr& expr);
    void compile_call(const Expr& expr);

    // Global variable collection (pre-scan)
    void collect_globals(const std::vector<StmtPtr>& program);
    void collect_globals_stmt(const Stmt& stmt);
    void collect_globals_expr(const Expr& expr);

    // Helpers
    void emit_constant(Value val, int line);
    size_t emit_jump(OpCode op, int line);
    void patch_jump(size_t addr);
    void resolve_labels();

    // Variable load/store dispatch - checks `statics` first so STATIC slots
    // route through LOAD_STATIC/STORE_STATIC. Falls through to the existing
    // global-vs-local logic otherwise. Mirrors the pre-existing inline
    // pattern at the caller, just centralises the static check.
    bool is_static_name(const std::string& name) const;
    void emit_var_load(const std::string& name, int line);
    void emit_var_store(const std::string& name, int line, bool prefer_local);
};
