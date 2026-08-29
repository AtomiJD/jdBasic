#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "token.h"

enum class VarType {
    NONE, BOOLEAN, BYTE, CHAR, INT16, INT32, INT64,
    FLOAT16, FLOAT32, FLOAT64, STRING, OBJECT, TENSOR, ARRAY,
    ANY  // DYNAMIC - tagged-mixed array (per-cell JdTag)
};

// VarType is the parser/compiler-side enum (extra CHAR slot at index 3),
// ValueType is the runtime enum used by the VM (no CHAR). The two enums
// have *different* numeric values for INT16+, so passing a VarType u8 to
// the CAST opcode (which interprets it as ValueType) silently corrupts
// the type - e.g. VarType::INT64 (6) became ValueType::FLOAT16, turning
// `DIM A AS INTEGER = $CAFEBABE` into `inf`. Always go through this map.
inline uint8_t vartype_to_valuetype_byte(VarType vt) {
    switch (vt) {
        case VarType::NONE:    return 0;  // ValueType::NONE
        case VarType::BOOLEAN: return 1;  // ValueType::BOOLEAN
        case VarType::BYTE:    return 2;  // ValueType::BYTE
        case VarType::CHAR:    return 2;  // map CHAR to BYTE - VM has no CHAR
        case VarType::INT16:   return 3;  // ValueType::INT16
        case VarType::INT32:   return 4;  // ValueType::INT32
        case VarType::INT64:   return 5;  // ValueType::INT64
        case VarType::FLOAT16: return 6;  // ValueType::FLOAT16
        case VarType::FLOAT32: return 7;  // ValueType::FLOAT32
        case VarType::FLOAT64: return 8;  // ValueType::FLOAT64
        case VarType::STRING:  return 9;  // ValueType::STRING
        case VarType::OBJECT:  return 10; // ValueType::OBJECT
        case VarType::TENSOR:  return 11; // ValueType::TENSOR
        case VarType::ARRAY:   return 12; // ValueType::ARRAY
        case VarType::ANY:     return 12; // map ANY to ARRAY for VM byte
    }
    return 0;
}

inline VarType token_to_vartype(TokenType t) {
    switch (t) {
        case TokenType::TY_BOOLEAN: return VarType::BOOLEAN;
        case TokenType::TY_BYTE:    return VarType::BYTE;
        case TokenType::TY_CHAR:    return VarType::CHAR;
        case TokenType::TY_INT16:   return VarType::INT16;
        case TokenType::TY_INT32:   return VarType::INT32;
        case TokenType::TY_INT64:   return VarType::INT64;
        case TokenType::TY_FLOAT16: return VarType::FLOAT16;
        case TokenType::TY_FLOAT32: return VarType::FLOAT32;
        case TokenType::TY_FLOAT64: return VarType::FLOAT64;
        case TokenType::TY_STRING:  return VarType::STRING;
        case TokenType::TY_OBJECT:  return VarType::OBJECT;
        case TokenType::TY_TENSOR:  return VarType::TENSOR;
        case TokenType::TY_ARRAY:   return VarType::ARRAY;
        case TokenType::TY_ANY:     return VarType::ANY;
        default: return VarType::NONE;
    }
}

// Forward declarations
struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ── Expressions ──────────────────────────────────────────────

enum class ExprKind {
    LITERAL_INT, LITERAL_FLOAT, LITERAL_STRING, LITERAL_BOOL,
    VARIABLE, BINARY, UNARY, CALL, INDEX, ARRAY_LITERAL, MAP_LITERAL, MEMBER_ACCESS,
    LAMBDA_EXPR, PIPE_EXPR, PLACEHOLDER_EXPR
};

struct Expr {
    ExprKind kind;
    int line = 0;

    // LITERAL_INT
    int64_t int_val = 0;
    // LITERAL_FLOAT
    double float_val = 0.0;
    // LITERAL_STRING, VARIABLE, MEMBER_ACCESS
    std::string str_val;
    // LITERAL_BOOL
    bool bool_val = false;

    // Set on string literals produced by the `name@` operator. The
    // compiler treats them like any other LITERAL_STRING; the module
    // rename pass uses the flag to namespace the referenced name.
    bool is_funcref_lit = false;
    // Set on an INDEX or MEMBER_ACCESS written with ?. / ?{ / ?[ : if the
    // thing being read into is absent, the access does not happen and the
    // result is absent too, so a chain stops instead of faulting.
    bool optional = false;

    // BINARY
    TokenType op = TokenType::PLUS;
    ExprPtr left;
    ExprPtr right;

    // UNARY
    // op + right

    // CALL
    std::string func_name;
    std::vector<ExprPtr> args;

    // INDEX: left[right]

    // ARRAY_LITERAL
    // args holds elements

    // MAP_LITERAL: map_keys[i] -> args[i]
    std::vector<std::string> map_keys;

    // LAMBDA_EXPR: lambda_params, lambda_captures, right=body
    std::vector<std::string> lambda_params;
    std::vector<std::string> lambda_captures;

    // MEMBER_ACCESS: left.str_val

    Expr() : kind(ExprKind::LITERAL_INT) {}
};

inline ExprPtr make_int_lit(int64_t v, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::LITERAL_INT;
    e->int_val = v;
    e->line = line;
    return e;
}

inline ExprPtr make_float_lit(double v, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::LITERAL_FLOAT;
    e->float_val = v;
    e->line = line;
    return e;
}

inline ExprPtr make_string_lit(const std::string& v, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::LITERAL_STRING;
    e->str_val = v;
    e->line = line;
    return e;
}

// Same shape as a string literal, but flagged as a funcref (`name@`)
// so the module-rename pass can namespace the referenced name.
inline ExprPtr make_funcref_lit(const std::string& v, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::LITERAL_STRING;
    e->str_val = v;
    e->is_funcref_lit = true;
    e->line = line;
    return e;
}

inline ExprPtr make_bool_lit(bool v, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::LITERAL_BOOL;
    e->bool_val = v;
    e->line = line;
    return e;
}

inline ExprPtr make_var(const std::string& name, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::VARIABLE;
    e->str_val = name;
    e->line = line;
    return e;
}

inline ExprPtr make_binary(TokenType op, ExprPtr left, ExprPtr right, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::BINARY;
    e->op = op;
    e->left = std::move(left);
    e->right = std::move(right);
    e->line = line;
    return e;
}

inline ExprPtr make_unary(TokenType op, ExprPtr operand, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::UNARY;
    e->op = op;
    e->right = std::move(operand);
    e->line = line;
    return e;
}

inline ExprPtr make_call(const std::string& name, std::vector<ExprPtr> args, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::CALL;
    e->func_name = name;
    e->args = std::move(args);
    e->line = line;
    return e;
}

inline ExprPtr make_index(ExprPtr arr, ExprPtr idx, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::INDEX;
    e->left = std::move(arr);
    e->right = std::move(idx);
    e->line = line;
    return e;
}

inline ExprPtr make_array_literal(std::vector<ExprPtr> elems, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::ARRAY_LITERAL;
    e->args = std::move(elems);
    e->line = line;
    return e;
}

// ── Statements ───────────────────────────────────────────────

enum class StmtKind {
    LET, DIM, PRINT, INPUT, GOTO, LABEL,
    IF, SUB, FUNCTION, DO_LOOP, FOR_LOOP, RETURN,
    EXPR_STMT, ASSIGN, INDEX_ASSIGN, DESTRUCTURE, ENUM_DECL,
    SWITCH_STMT, TRY_CATCH, THROW_STMT, TYPE_DECL,
    FOR_EACH, SLEEP_STMT, STOP_STMT, END_STMT, CLS_STMT,
    LOCATE_STMT, COLOR_STMT, CURSOR_STMT, OPTION_STMT,
    EXIT_LOOP, CONTINUE_LOOP, REACT_ASSIGN
};

struct Param {
    std::string name;
    VarType type = VarType::NONE;
    // A trailing parameter may name what it stands for when the caller
    // leaves it out. Literals only: a default that had to be evaluated
    // would need a scope to be evaluated in, and there is none at the
    // point a function is declared.
    ExprPtr default_value;
};

struct IfBranch {
    ExprPtr condition;           // nullptr for ELSE/SWITCH-DEFAULT
    // SWITCH multi-case labels: each pair is (low, high). high is nullptr for
    // a single-value label, non-null for "lo TO hi" ranges. Empty for IF
    // branches and SWITCH DEFAULT.
    std::vector<std::pair<ExprPtr, ExprPtr>> case_labels;
    std::vector<StmtPtr> body;
};

struct Stmt {
    StmtKind kind;
    int line = 0;
    std::string source_file;   // originating source file (set by parse_import)

    // LET / DIM / ASSIGN / CONST
    std::string var_name;
    VarType var_type = VarType::NONE;
    bool is_const = false;
    bool is_static = false;            // STATIC DIM inside FUNC/SUB body
    VarType elem_type = VarType::NONE; // for ARRAY OF <type>
    ExprPtr expr;

    // PRINT
    std::vector<ExprPtr> print_exprs;
    std::vector<int> print_seps;  // 0=none, 1=comma(space), 2=semicolon(no space)
    bool print_newline = true;

    // INPUT
    // var_name

    // GOTO / LABEL
    std::string label;

    // IF
    std::vector<IfBranch> branches;

    // SUB / FUNCTION
    std::string func_name;
    std::vector<Param> params;
    VarType return_type = VarType::NONE;
    std::vector<StmtPtr> body;

    // DO_LOOP
    ExprPtr loop_cond;
    bool cond_at_top = false;    // DO WHILE/UNTIL ... LOOP
    bool is_while = true;        // WHILE vs UNTIL
    bool is_async_func = false;  // ASYNC FUNC

    // FOR_LOOP: FOR var_name = expr TO end_expr [STEP step_expr]
    ExprPtr end_expr;
    ExprPtr step_expr;           // nullptr means STEP 1

    // RETURN
    // expr

    // EXPR_STMT
    // expr

    // INDEX_ASSIGN: var_name[i1][i2]...[iN] = expr
    std::vector<ExprPtr> index_chain;

    // DESTRUCTURE: [var1, var2, ...] = expr
    std::vector<std::string> destruct_vars;

    // ENUM_DECL: func_name = enum name, enum_members = {name, value} pairs
    std::vector<std::pair<std::string, int64_t>> enum_members;

    // TYPE_DECL: func_name=type name, type_members={name,type}, body=method stmts
    struct TypeMember { std::string name; VarType type; };
    std::vector<TypeMember> type_members;

    // DIM x AS T(arg1, arg2)            - scalar UDT constructor args
    // DIM arr[N] AS T(vec1, vec2)       - vectorised: vecK[i] -> arg K of slot i
    std::vector<ExprPtr> ctor_args;

    // TRY_CATCH: body=TRY block, catch_body, finally_body
    std::vector<StmtPtr> catch_body;
    std::vector<StmtPtr> finally_body;

    Stmt() : kind(StmtKind::LET) {}
};

inline StmtPtr make_let(const std::string& name, VarType type, ExprPtr val, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::LET;
    s->var_name = name;
    s->var_type = type;
    s->expr = std::move(val);
    s->line = line;
    return s;
}

inline StmtPtr make_assign(const std::string& name, ExprPtr val, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::ASSIGN;
    s->var_name = name;
    s->expr = std::move(val);
    s->line = line;
    return s;
}

inline StmtPtr make_index_assign(const std::string& name, std::vector<ExprPtr> chain, ExprPtr val, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::INDEX_ASSIGN;
    s->var_name = name;
    s->index_chain = std::move(chain);
    s->expr = std::move(val);
    s->line = line;
    return s;
}

inline StmtPtr make_print(std::vector<ExprPtr> exprs, bool newline, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::PRINT;
    s->print_exprs = std::move(exprs);
    s->print_newline = newline;
    s->line = line;
    return s;
}

inline StmtPtr make_input(const std::string& name, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::INPUT;
    s->var_name = name;
    s->line = line;
    return s;
}

inline StmtPtr make_goto(const std::string& label, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::GOTO;
    s->label = label;
    s->line = line;
    return s;
}

inline StmtPtr make_label(const std::string& label, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::LABEL;
    s->label = label;
    s->line = line;
    return s;
}

inline StmtPtr make_return(ExprPtr val, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::RETURN;
    s->expr = std::move(val);
    s->line = line;
    return s;
}

inline StmtPtr make_expr_stmt(ExprPtr val, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::EXPR_STMT;
    s->expr = std::move(val);
    s->line = line;
    return s;
}
