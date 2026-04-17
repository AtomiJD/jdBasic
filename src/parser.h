#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include "token.h"
#include "ast.h"

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    std::vector<StmtPtr> parse();

    // Module support: callback to read a file
    // Returns {source_code, resolved_file_path}
    using FileReader = std::function<std::pair<std::string, std::string>(const std::string& module_name)>;
    FileReader file_reader;

    // Source file path for the current parse context (set by caller for main file)
    std::string current_source_file;

    // Tracks already-imported modules to prevent circular imports
    std::unordered_set<std::string> imported_modules;

private:
    std::vector<Token> tokens;
    size_t pos = 0;

    // Queue of synthesised statements that should be returned by the next
    // parse_statement() calls before any new tokens are consumed. Used to
    // expand `DIM a, b, c` into multiple separate DIM statements.
    std::vector<StmtPtr> pending_stmts;

    // Helpers
    const Token& current() const;
    const Token& peek_at(size_t offset) const;
    Token advance();
    bool check(TokenType type) const;
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
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_not();
    ExprPtr parse_bor();
    ExprPtr parse_xor();
    ExprPtr parse_band();
    ExprPtr parse_comparison();
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
    static void module_set_source_file(Stmt& stmt, const std::string& path);
    static void module_rename_stmt(Stmt& stmt,
        const std::unordered_map<std::string, std::string>& func_map,
        const std::unordered_map<std::string, std::string>& var_map);
};
