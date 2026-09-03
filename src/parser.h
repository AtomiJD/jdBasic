#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include "token.h"
#include "ast.h"

class Lexer;

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    // Reads tokens from the lexer as it goes, keeping only the current
    // line and a few tokens behind it.
    explicit Parser(Lexer& lexer);
    std::vector<StmtPtr> parse();

    // Module support: callback to read a file
    // Returns {source_code, resolved_file_path}
    using FileReader = std::function<std::pair<std::string, std::string>(const std::string& module_name)>;
    FileReader file_reader;

    // Source file path for the current parse context (set by caller for main file)
    std::string current_source_file;

    // Tracks already-imported modules to prevent circular imports
    std::unordered_set<std::string> imported_modules;

    // Types the caller already knows about - the VM's registry when a REPL line
    // or an EXECUTE chunk is parsed. Compared upper-cased, like every other
    // identifier.
    std::unordered_set<std::string> predeclared_types;

private:
    // The token window, a ring: token number `base` sits at ring[head],
    // `count` tokens follow it. With a lexer attached the ring fills on
    // demand, drops what lies more than a few tokens behind `pos` - never
    // below `hold`, which a statement sets at a position it may come back
    // to - and doubles only when one line needs more than it holds.
    // Without a lexer it holds the whole stream.
    std::vector<Token> ring;
    size_t head = 0;
    size_t count = 0;
    size_t pos = 0;
    size_t base = 0;
    size_t hold = (size_t)-1;
    Lexer* lexer = nullptr;
    // While set, every token taken by advance() is appended to it.
    std::string* recording = nullptr;

    const Token& at(size_t i);
    void mark(size_t at_pos) { if (at_pos < hold) hold = at_pos; }
    void trim();

    // True while parsing a one-liner IF body (IF c THEN stmt [:stmt] [ELSE ...]).
    // There a bare identifier before ':' or ELSE is a no-args SUB/FUNC call, not
    // a label - labels are meaningless inside an inline IF.
    bool in_inline_if_ = false;

    // Queue of synthesised statements that should be returned by the next
    // parse_statement() calls before any new tokens are consumed. Used to
    // expand `DIM a, b, c` into multiple separate DIM statements.
    std::vector<StmtPtr> pending_stmts;

    // A statement the parser still owes the block it is filling. `DIM a, b, c`
    // reads as one statement and compiles to three, and all three belong where
    // the DIM was written: a body loop that stops on its end token while two
    // are still queued hoists them out of the block, so they run when the
    // block does not.
    bool owes_statement() const { return !pending_stmts.empty(); }

    // Every type name that parsed as an unknown identifier in type position,
    // with the line it appeared on. Checked against the TYPE declarations once
    // the whole program (imports included) has been parsed, so a type may be
    // declared after its use and may live in an imported module.
    std::vector<std::pair<std::string, int>> udt_type_refs;
    void record_type_ref(const std::string& name, int line);
    void validate_type_refs(const std::vector<StmtPtr>& stmts);

    // Helpers
    const Token& current();
    const Token& peek_at(size_t offset);
    Token advance();
    bool check(TokenType type);
    bool match(TokenType type);
    Token expect(TokenType type, const std::string& msg);
    void skip_newlines();
    void expect_newline();
    bool is_type_token(TokenType t) const;
    VarType parse_type();

    // Statements
    StmtPtr parse_statement();
    StmtPtr parse_let();
    StmtPtr parse_dim();
    StmtPtr parse_const();
    StmtPtr parse_dim_clause(int ln);
    StmtPtr parse_print();
    StmtPtr parse_input();
    StmtPtr parse_goto();
    StmtPtr parse_if();
    StmtPtr parse_sub();
    StmtPtr parse_function();
    StmtPtr parse_do_loop();
    StmtPtr parse_for();
    StmtPtr parse_return();
    StmtPtr parse_ident_stmt();  // assignment, call, label

    // Expressions (precedence climbing)
    ExprPtr parse_expr();
    ExprPtr parse_interp_string(const std::string& raw, int line);
    ExprPtr parse_coalesce();
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_not();
    ExprPtr parse_bor();
    ExprPtr parse_xor();
    ExprPtr parse_band();
    ExprPtr parse_comparison();
    ExprPtr parse_shift();
    ExprPtr parse_addition();
    ExprPtr parse_multiplication();
    ExprPtr parse_power();
    ExprPtr parse_unary();
    ExprPtr parse_primary();
    ExprPtr parse_postfix(ExprPtr expr);

    std::vector<Param> parse_params();

    // Module support
    std::vector<StmtPtr> parse_import();

    // AST rewriting for modules
    static void module_rename_expr(Expr& expr,
        const std::unordered_map<std::string, std::string>& func_map,
        const std::unordered_map<std::string, std::string>& var_map);
    static void module_set_source_file(Stmt& stmt,
        const std::shared_ptr<const std::string>& path);
    // current_source_file as one shared string, made when it changes.
    std::shared_ptr<const std::string> file_ref_;
    const std::shared_ptr<const std::string>& file_ref() {
        if (!file_ref_ || *file_ref_ != current_source_file)
            file_ref_ = std::make_shared<const std::string>(current_source_file);
        return file_ref_;
    }
    static void module_rename_stmt(Stmt& stmt,
        const std::unordered_map<std::string, std::string>& func_map,
        const std::unordered_map<std::string, std::string>& var_map);
};
