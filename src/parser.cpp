#include "parser.h"
#include <algorithm>

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens) {}

const Token& Parser::current() const { return tokens[pos]; }
const Token& Parser::peek_at(size_t offset) const { return tokens[pos + offset]; }

Token Parser::advance() {
    Token t = tokens[pos];
    if (pos < tokens.size() - 1) pos++;
    return t;
}

bool Parser::check(TokenType type) const { return current().type == type; }

bool Parser::match(TokenType type) {
    if (check(type)) { advance(); return true; }
    return false;
}

Token Parser::expect(TokenType type, const std::string& msg) {
    if (check(type)) return advance();
    throw std::runtime_error("Parse error at line " + std::to_string(current().line) +
        ": expected " + msg + ", got '" + current().value + "'");
}

void Parser::skip_newlines() {
    while (check(TokenType::NEWLINE)) advance();
}

void Parser::expect_newline() {
    if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) &&
        !check(TokenType::COLON) && !check(TokenType::ELSE)) {
        throw std::runtime_error("Parse error at line " + std::to_string(current().line) +
            ": expected end of statement, got '" + current().value + "'");
    }
    if (check(TokenType::NEWLINE) || check(TokenType::COLON)) advance();
    // Note: ELSE is NOT consumed here - the IF parser handles it
}

bool Parser::is_type_token(TokenType t) const {
    return t >= TokenType::TY_BOOLEAN && t <= TokenType::TY_ANY;
}

VarType Parser::parse_type() {
    // MAP as alias for OBJECT
    if (current().type == TokenType::IDENTIFIER && current().value == "MAP") {
        advance();
        return VarType::OBJECT;
    }
    // DATE - runtime stores dates as strings (CVDATE returns char*).
    // Without this branch `FUNC X() AS DATE` would fall into the UDT
    // catch-all below and silently get treated as OBJECT/VM_HANDLE.
    if (current().type == TokenType::IDENTIFIER && current().value == "DATE") {
        advance();
        return VarType::STRING;
    }
    // User-defined type names (any unrecognized IDENTIFIER in type context).
    // Recorded rather than trusted: validate_type_refs() rejects the ones that
    // never get a TYPE declaration, so a typo cannot silently become an empty
    // OBJECT.
    if (current().type == TokenType::IDENTIFIER && !is_type_token(current().type)) {
        record_type_ref(current().value, current().line);
        advance();
        return VarType::OBJECT; // UDTs are objects
    }
    if (!is_type_token(current().type)) {
        throw std::runtime_error("Parse error at line " + std::to_string(current().line) +
            ": expected type name, got '" + current().value + "'");
    }
    VarType vt = token_to_vartype(advance().type);
    // Handle ARRAY OF <type>
    if (vt == VarType::ARRAY && check(TokenType::OF)) {
        advance(); // skip OF
        // The element type will be stored separately
    }
    return vt;
}

// ── Parse program ────────────────────────────────────────────

void Parser::record_type_ref(const std::string& name, int line) {
    if (!name.empty()) udt_type_refs.emplace_back(name, line);
}

namespace {
std::string upper_copy(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    return u;
}

// A TYPE inside an imported module reaches this list under its rewritten name:
// `MODULE.Type` when exported, `__MODULE__Type` when not. Record the bare name
// as well, since that is what a reference inside the module itself still says.
void collect_type_decls(const std::vector<StmtPtr>& stmts,
                        std::unordered_set<std::string>& out) {
    for (const auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::TYPE_DECL && !s->func_name.empty()) {
            std::string n = upper_copy(s->func_name);
            out.insert(n);
            size_t dot = n.rfind('.');
            if (dot != std::string::npos) out.insert(n.substr(dot + 1));
            size_t us = n.rfind("__");
            if (us != std::string::npos && us + 2 < n.size())
                out.insert(n.substr(us + 2));
        }
        collect_type_decls(s->body, out);
        collect_type_decls(s->catch_body, out);
        collect_type_decls(s->finally_body, out);
        for (const auto& br : s->branches) collect_type_decls(br.body, out);
    }
}
}  // namespace

// An identifier in type position is only legal if some TYPE declares it. The
// check runs over the finished program rather than at the point of use, so a
// type may be declared below its first use or come from an imported module.
// A dotted MODULE.TypeName is validated on its last segment, which is the name
// the module declares.
void Parser::validate_type_refs(const std::vector<StmtPtr>& stmts) {
    if (udt_type_refs.empty()) return;
    std::unordered_set<std::string> declared;
    for (const auto& t : predeclared_types) declared.insert(upper_copy(t));
    collect_type_decls(stmts, declared);
    for (const auto& ref : udt_type_refs) {
        std::string name = ref.first;
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) name = name.substr(dot + 1);
        if (declared.count(upper_copy(name))) continue;
        throw std::runtime_error("Parse error at line " + std::to_string(ref.second) +
            ": unknown type '" + ref.first + "'. Declare it with TYPE " + ref.first +
            " ... ENDTYPE, or use a built-in type.");
    }
}

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> stmts;
    skip_newlines();
    while (!check(TokenType::EOF_TOKEN)) {
        if (check(TokenType::IMPORT_KW)) {
            auto imported = parse_import();
            for (auto& s : imported) stmts.push_back(std::move(s));
        } else {
            auto s = parse_statement();
            // Tag with current source file if not already set (imports set their own)
            if (s && s->source_file.empty() && !current_source_file.empty()) {
                s->source_file = current_source_file;
                for (auto& child : s->body) {
                    if (child && child->source_file.empty())
                        child->source_file = current_source_file;
                }
            }
            stmts.push_back(std::move(s));
        }
        skip_newlines();
    }
    validate_type_refs(stmts);
    return stmts;
}

// ── Statements ───────────────────────────────────────────────

StmtPtr Parser::parse_statement() {
    // Drain any synthesised statements queued from a previous call (e.g.
    // multi-variable DIM `DIM a, b, c` which expands into 3 separate stmts).
    if (!pending_stmts.empty()) {
        auto s = std::move(pending_stmts.front());
        pending_stmts.erase(pending_stmts.begin());
        return s;
    }
    switch (current().type) {
        case TokenType::LET:     return parse_let();
        case TokenType::DIM:     return parse_dim();
        case TokenType::STATIC_KW: {
            // STATIC DIM <decl>[, <decl>...] inside FUNC/SUB body.
            // Each declared slot persists across calls; init runs once,
            // guarded at the line. Top-level STATIC is rejected.
            int ln = current().line;
            advance(); // STATIC
            if (!check(TokenType::DIM)) {
                throw std::runtime_error("Line " + std::to_string(ln) +
                    ": expected DIM after STATIC");
            }
            advance(); // DIM
            StmtPtr first = parse_dim_clause(ln);
            first->is_static = true;
            while (match(TokenType::COMMA)) {
                auto more = parse_dim_clause(ln);
                more->is_static = true;
                pending_stmts.push_back(std::move(more));
            }
            expect_newline();
            return first;
        }
        case TokenType::CONST_KW: return parse_const();
        case TokenType::PRINT:   return parse_print();
        case TokenType::INPUT:   return parse_input();
        case TokenType::GOTO:    return parse_goto();
        case TokenType::IF:      return parse_if();
        case TokenType::SUB:     return parse_sub();
        case TokenType::FUNCTION:return parse_function();
        case TokenType::ASYNC: {
            advance(); // ASYNC
            if (check(TokenType::FUNCTION)) {
                auto s = parse_function();
                // Mark as async - compiler will set is_async on the FuncProto
                s->is_async_func = true;
                return s;
            }
            throw std::runtime_error("Parse error: expected FUNC after ASYNC");
        }
        case TokenType::DO:      return parse_do_loop();
        case TokenType::FOR:     return parse_for();
        case TokenType::THIS_KW: {
            // THIS.field = expr (inside TYPE methods)
            int ln = current().line;
            advance(); // THIS
            expect(TokenType::DOT, "'.'");
            std::string field = advance().value; // accept any token as field
            if (check(TokenType::ASSIGN)) {
                advance(); // =
                ExprPtr val = parse_expr();
                expect_newline();
                // Compile as INDEX_ASSIGN on THIS with string key
                std::vector<ExprPtr> chain;
                chain.push_back(make_string_lit(field, ln));
                return make_index_assign("THIS", std::move(chain), std::move(val), ln);
            }
            if (check(TokenType::LPAREN)) {
                // THIS.method(args) - shouldn't be common but handle it
                advance();
                std::vector<ExprPtr> args;
                if (!check(TokenType::RPAREN)) {
                    args.push_back(parse_expr());
                    while (match(TokenType::COMMA)) args.push_back(parse_expr());
                }
                expect(TokenType::RPAREN, "')'");
                auto expr = make_call("THIS." + field, std::move(args), ln);
                expect_newline();
                return make_expr_stmt(std::move(expr), ln);
            }
            // THIS.field as expression
            auto expr = make_var("THIS", ln);
            auto member = std::make_unique<Expr>();
            member->kind = ExprKind::MEMBER_ACCESS;
            member->str_val = field;
            member->left = std::move(expr);
            member->line = ln;
            expect_newline();
            return make_expr_stmt(std::move(member), ln);
        }
        case TokenType::TYPE_KW: {
            int ln = current().line;
            advance(); // TYPE
            std::string type_name = expect(TokenType::IDENTIFIER, "type name").value;
            expect_newline();
            skip_newlines();
            auto s = std::make_unique<Stmt>();
            s->kind = StmtKind::TYPE_DECL;
            s->func_name = type_name;
            s->line = ln;
            while (!check(TokenType::ENDTYPE) && !check(TokenType::EOF_TOKEN)) {
                if (check(TokenType::SUB) || check(TokenType::FUNCTION)) {
                    // Method: prepend THIS as implicit first parameter
                    auto method = parse_statement(); // parses SUB or FUNCTION
                    // Rename: Method → TypeName.Method
                    method->func_name = type_name + "." + method->func_name;
                    // Insert THIS as first parameter
                    Param this_param; this_param.name = "THIS"; this_param.type = VarType::OBJECT;
                    method->params.insert(method->params.begin(), this_param);
                    s->body.push_back(std::move(method));
                } else if (check(TokenType::IDENTIFIER)) {
                    // Member declaration: Name AS Type
                    Stmt::TypeMember mem;
                    mem.name = advance().value;
                    if (match(TokenType::AS)) {
                        mem.type = parse_type();
                    } else {
                        mem.type = VarType::NONE;
                    }
                    s->type_members.push_back(std::move(mem));
                    expect_newline();
                } else {
                    advance(); // skip unexpected
                }
                skip_newlines();
            }
            expect(TokenType::ENDTYPE, "'ENDTYPE'");
            expect_newline();
            return s;
        }
        case TokenType::SWITCH: {
            int ln = current().line;
            advance(); // SWITCH
            auto s = std::make_unique<Stmt>();
            s->kind = StmtKind::SWITCH_STMT;
            s->expr = parse_expr(); // the switch expression
            s->line = ln;
            expect_newline();
            skip_newlines();

            // Collect CASE / DEFAULT branches until ENDSWITCH
            while (!check(TokenType::ENDSWITCH) && !check(TokenType::EOF_TOKEN)) {
                if (check(TokenType::CASE)) {
                    advance(); // CASE
                    IfBranch branch;
                    // Multi-case: comma-separated values, each optionally a TO range.
                    //   CASE 1, 3, 5
                    //   CASE 10 TO 19
                    //   CASE 1, 5 TO 9, 12
                    do {
                        ExprPtr low = parse_expr();
                        ExprPtr high;
                        if (check(TokenType::TO)) {
                            advance(); // TO
                            high = parse_expr();
                        }
                        branch.case_labels.emplace_back(std::move(low), std::move(high));
                    } while (match(TokenType::COMMA));
                    expect_newline();
                    skip_newlines();
                    while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
                           !check(TokenType::ENDSWITCH) && !check(TokenType::EOF_TOKEN)) {
                        branch.body.push_back(parse_statement());
                        skip_newlines();
                    }
                    s->branches.push_back(std::move(branch));
                } else if (check(TokenType::DEFAULT)) {
                    advance(); // DEFAULT
                    expect_newline();
                    skip_newlines();
                    IfBranch branch; // condition is nullptr = default
                    while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
                           !check(TokenType::ENDSWITCH) && !check(TokenType::EOF_TOKEN)) {
                        branch.body.push_back(parse_statement());
                        skip_newlines();
                    }
                    s->branches.push_back(std::move(branch));
                } else {
                    throw std::runtime_error("Parse error at line " + std::to_string(current().line) +
                        ": expected CASE, DEFAULT, or ENDSWITCH");
                }
            }
            expect(TokenType::ENDSWITCH, "'ENDSWITCH'");
            expect_newline();
            return s;
        }
        case TokenType::RETURN:  return parse_return();
        case TokenType::CALL: {
            // CALL SubName(args) - parse as expression statement
            int ln = current().line;
            advance(); // CALL
            ExprPtr expr = parse_expr();
            expect_newline();
            return make_expr_stmt(std::move(expr), ln);
        }
        case TokenType::THROW_KW: {
            int ln = current().line;
            advance(); // THROW
            ExprPtr msg;
            if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) && !check(TokenType::COLON)) {
                msg = parse_expr();
            }
            expect_newline();
            auto s = std::make_unique<Stmt>();
            s->kind = StmtKind::THROW_STMT;
            s->expr = std::move(msg);
            s->line = ln;
            return s;
        }
        case TokenType::TRY: {
            int ln = current().line;
            advance(); // TRY
            expect_newline();
            skip_newlines();
            auto s = std::make_unique<Stmt>();
            s->kind = StmtKind::TRY_CATCH;
            s->line = ln;

            // TRY body
            while (!check(TokenType::CATCH) && !check(TokenType::FINALLY) &&
                   !check(TokenType::ENDTRY) && !check(TokenType::EOF_TOKEN)) {
                s->body.push_back(parse_statement());
                skip_newlines();
            }

            // CATCH block (optional)
            if (check(TokenType::CATCH)) {
                advance(); // CATCH
                expect_newline();
                skip_newlines();
                while (!check(TokenType::FINALLY) && !check(TokenType::ENDTRY) && !check(TokenType::EOF_TOKEN)) {
                    s->catch_body.push_back(parse_statement());
                    skip_newlines();
                }
            }

            // FINALLY block (optional)
            if (check(TokenType::FINALLY)) {
                advance(); // FINALLY
                expect_newline();
                skip_newlines();
                while (!check(TokenType::ENDTRY) && !check(TokenType::EOF_TOKEN)) {
                    s->finally_body.push_back(parse_statement());
                    skip_newlines();
                }
            }

            expect(TokenType::ENDTRY, "'ENDTRY'");
            expect_newline();
            return s;
        }
        case TokenType::ENUM: {
            int ln = current().line;
            advance(); // ENUM
            std::string enum_name = expect(TokenType::IDENTIFIER, "enum name").value;
            expect_newline();
            skip_newlines();
            auto s = std::make_unique<Stmt>();
            s->kind = StmtKind::ENUM_DECL;
            s->func_name = enum_name;
            s->line = ln;
            int64_t next_val = 0;
            while (!check(TokenType::ENDENUM) && !check(TokenType::EOF_TOKEN)) {
                // Accept any word token as member name - including BASIC
                // keywords like DEFAULT, IF, THEN, etc. ENUM members live in
                // their own namespace (Enum.Member), so there's no clash.
                std::string member;
                {
                    auto& tok = current();
                    if (tok.type == TokenType::IDENTIFIER ||
                        (!tok.value.empty() && (std::isalpha((unsigned char)tok.value[0]) ||
                                                tok.value[0] == '_'))) {
                        member = advance().value;
                    } else {
                        member = expect(TokenType::IDENTIFIER, "enum member").value;
                    }
                }
                if (match(TokenType::ASSIGN)) {
                    // Explicit value
                    if (check(TokenType::INTEGER_LIT)) {
                        next_val = std::stoll(advance().value);
                    } else if (check(TokenType::MINUS)) {
                        advance();
                        next_val = -std::stoll(expect(TokenType::INTEGER_LIT, "integer").value);
                    }
                }
                s->enum_members.push_back({member, next_val});
                next_val++;
                expect_newline();
                skip_newlines();
            }
            expect(TokenType::ENDENUM, "'ENDENUM'");
            expect_newline();
            return s;
        }
        case TokenType::CLS_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::CLS_STMT; s->line = ln;
            // Optional color args: CLS r, g, b
            if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) && !check(TokenType::COLON)) {
                s->print_exprs.push_back(parse_expr());
                while (match(TokenType::COMMA)) s->print_exprs.push_back(parse_expr());
            }
            expect_newline(); return s;
        }
        case TokenType::SLEEP_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::SLEEP_STMT; s->line = ln;
            s->expr = parse_expr(); expect_newline(); return s;
        }
        case TokenType::STOP_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::STOP_STMT; s->line = ln;
            expect_newline(); return s;
        }
        case TokenType::END: {
            // END alone = exit program; END IF/SUB/FUNCTION handled elsewhere
            if (peek_at(1).type == TokenType::NEWLINE || peek_at(1).type == TokenType::EOF_TOKEN ||
                peek_at(1).type == TokenType::COLON) {
                int ln = current().line; advance();
                auto s = std::make_unique<Stmt>(); s->kind = StmtKind::END_STMT; s->line = ln;
                expect_newline(); return s;
            }
            throw std::runtime_error("Parse error at line " + std::to_string(current().line) +
                ": unexpected 'END'");
        }
        case TokenType::LOCATE_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::LOCATE_STMT; s->line = ln;
            s->print_exprs.push_back(parse_expr());
            expect(TokenType::COMMA, "','");
            s->print_exprs.push_back(parse_expr());
            expect_newline(); return s;
        }
        case TokenType::COLOR_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::COLOR_STMT; s->line = ln;
            s->print_exprs.push_back(parse_expr());
            expect(TokenType::COMMA, "','");
            s->print_exprs.push_back(parse_expr());
            expect_newline(); return s;
        }
        case TokenType::CURSOR_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::CURSOR_STMT; s->line = ln;
            s->expr = parse_expr(); expect_newline(); return s;
        }
        case TokenType::OPTION_KW: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::OPTION_STMT; s->line = ln;
            // Accept both OPTION "EXPLICIT" (string) and the classic bare
            // OPTION EXPLICIT / OPTION STRICT. Normalize the bare keyword to a
            // string literal so the codegen + lint option pre-pass (which match
            // string literals) recognize it, and so the bare keyword is not
            // walked as an undeclared identifier reference.
            if (check(TokenType::STRING_LIT)) {
                s->expr = parse_expr();
            } else if (!check(TokenType::NEWLINE) && !check(TokenType::COLON) &&
                       !check(TokenType::EOF_TOKEN)) {
                s->expr = make_string_lit(advance().value, ln);
            }
            expect_newline(); return s;
        }
        case TokenType::EXITFOR: case TokenType::EXITDO: case TokenType::EXITFUNC: {
            int ln = current().line;
            bool is_func = (current().type == TokenType::EXITFUNC);
            advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::EXIT_LOOP;
            s->line = ln; s->is_while = is_func; // reuse: is_while=true means EXITFUNC
            expect_newline(); return s;
        }
        case TokenType::CONTINUEFOR: case TokenType::CONTINUEDO: {
            int ln = current().line; advance();
            auto s = std::make_unique<Stmt>(); s->kind = StmtKind::CONTINUE_LOOP; s->line = ln;
            expect_newline(); return s;
        }
        case TokenType::HELP_KW: {
            // HELP [topic] - topic can be any token (keyword, identifier, string)
            int ln = current().line;
            advance(); // HELP
            ExprPtr arg;
            if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) && !check(TokenType::COLON)) {
                if (check(TokenType::STRING_LIT)) {
                    // HELP "topic"
                    arg = make_string_lit(advance().value, ln);
                } else {
                    // HELP UNTIL / HELP PRINT / HELP FOR etc. - take raw token value as string
                    arg = make_string_lit(advance().value, ln);
                }
            }
            expect_newline();
            // Compile as: HELP() or HELP("topic")
            std::vector<ExprPtr> args;
            if (arg) args.push_back(std::move(arg));
            auto call = make_call("HELP", std::move(args), ln);
            return make_expr_stmt(std::move(call), ln);
        }
        // DECLARE FUNC name LIB "dll" ALIAS "export" (params) AS type
        case TokenType::DECLARE_KW: {
            int ln = current().line;
            advance(); // DECLARE
            bool is_func = check(TokenType::FUNCTION);
            if (!is_func && !check(TokenType::SUB))
                throw std::runtime_error("Parse error at line " + std::to_string(ln) + ": expected FUNC or SUB after DECLARE");
            advance(); // FUNC/SUB

            std::string name = expect(TokenType::IDENTIFIER, "function name").value;

            // LIB "dllname"
            if (current().value != "LIB")
                throw std::runtime_error("Parse error at line " + std::to_string(ln) + ": expected LIB");
            advance();
            std::string dll = expect(TokenType::STRING_LIT, "DLL name").value;

            // ALIAS "exportname"
            std::string alias = name;
            if (current().value == "ALIAS") {
                advance();
                alias = expect(TokenType::STRING_LIT, "alias name").value;
            }

            // Parameter list
            expect(TokenType::LPAREN, "'('");
            std::vector<ExprPtr> param_types_arr;
            std::vector<ExprPtr> param_names_arr;
            if (!check(TokenType::RPAREN)) {
                do {
                    std::string pname = expect(TokenType::IDENTIFIER, "param name").value;
                    expect(TokenType::AS, "'AS'");
                    std::string ptype;
                    if (current().value == "RETURN") {
                        ptype = "RETURN"; advance();
                    } else if (current().value == "INTEGER" || current().type == TokenType::TY_INT64 || current().type == TokenType::TY_INT32) {
                        ptype = "INTEGER"; advance();
                    } else if (current().value == "STRING" || current().type == TokenType::TY_STRING) {
                        ptype = "STRING"; advance();
                    } else {
                        ptype = advance().value;
                    }
                    param_names_arr.push_back(make_string_lit(pname, ln));
                    param_types_arr.push_back(make_string_lit(ptype, ln));
                } while (match(TokenType::COMMA));
            }
            expect(TokenType::RPAREN, "')'");

            // AS return_type
            std::string ret_type = is_func ? "INTEGER" : "VOID";
            if (match(TokenType::AS)) {
                if (current().value == "ARRAY" || current().type == TokenType::TY_ARRAY) {
                    ret_type = "ARRAY"; advance();
                } else if (current().value == "INTEGER" || current().type == TokenType::TY_INT64 || current().type == TokenType::TY_INT32) {
                    ret_type = "INTEGER"; advance();
                } else if (current().value == "STRING" || current().type == TokenType::TY_STRING) {
                    ret_type = "STRING"; advance();
                } else {
                    ret_type = advance().value;
                }
            }
            expect_newline();

            // Compile as: __FFI_DECLARE(name, dll, alias, [param_types], [param_names], ret_type)
            std::vector<ExprPtr> args;
            args.push_back(make_string_lit(name, ln));
            args.push_back(make_string_lit(dll, ln));
            args.push_back(make_string_lit(alias, ln));
            args.push_back(make_array_literal(std::move(param_types_arr), ln));
            args.push_back(make_array_literal(std::move(param_names_arr), ln));
            args.push_back(make_string_lit(ret_type, ln));
            auto call = make_call("__FFI_DECLARE", std::move(args), ln);
            return make_expr_stmt(std::move(call), ln);
        }
        // Module statements (inside module files)
        case TokenType::MODULE_KW: {
            // MODULE MODNAME - declaration, skip
            int ln = current().line; advance();
            if (check(TokenType::IDENTIFIER)) advance(); // module name
            expect_newline();
            // Return a no-op expr statement
            return make_expr_stmt(make_int_lit(0, ln), ln);
        }
        case TokenType::EXPORT_KW: {
            // EXPORT FUNC/SUB/TYPE/DIM - parse the statement, mark as exported
            advance(); // EXPORT
            if (check(TokenType::MODULE_KW)) {
                // EXPORT MODULE MODNAME - just a declaration, skip
                int ln = current().line;
                advance(); // MODULE
                if (check(TokenType::IDENTIFIER)) advance();
                expect_newline();
                return make_expr_stmt(make_int_lit(0, ln), ln);
            }
            auto stmt = parse_statement();
            if (stmt->kind == StmtKind::FUNCTION || stmt->kind == StmtKind::SUB ||
                stmt->kind == StmtKind::TYPE_DECL ||
                stmt->kind == StmtKind::DIM || stmt->kind == StmtKind::LET) {
                stmt->label = "__EXPORT__";
            }
            return stmt;
        }
        // Event statements
        case TokenType::ON_KW: {
            // ON "event" CALL HandlerName
            int ln = current().line;
            advance(); // ON
            ExprPtr event_name = parse_expr();
            expect(TokenType::CALL, "'CALL'");
            std::string handler = expect(TokenType::IDENTIFIER, "handler name").value;
            expect_newline();
            std::vector<ExprPtr> args;
            args.push_back(std::move(event_name));
            args.push_back(make_string_lit(handler, ln));
            auto call = make_call("__EVENT_ON", std::move(args), ln);
            return make_expr_stmt(std::move(call), ln);
        }
        case TokenType::RAISEEVENT_KW: {
            // RAISEEVENT "event", data1, data2, ...
            int ln = current().line;
            advance(); // RAISEEVENT
            std::vector<ExprPtr> args;
            args.push_back(parse_expr()); // event name
            while (match(TokenType::COMMA)) args.push_back(parse_expr());
            expect_newline();
            auto call = make_call("__EVENT_RAISE", std::move(args), ln);
            return make_expr_stmt(std::move(call), ln);
        }
        // Graphics statements - parse args, compile as native calls
        case TokenType::SCREEN_KW: case TokenType::SCREENFLIP_KW:
        case TokenType::DRAWCOLOR_KW: case TokenType::SETFONT_KW:
        case TokenType::PSET_KW: case TokenType::LINE_KW:
        case TokenType::RECT_KW: case TokenType::CIRCLE_KW:
        case TokenType::ELLIPSE_KW: case TokenType::ROUNDED_RECT_KW:
        case TokenType::CIRCLE_SECTOR_KW: case TokenType::TEXT_KW:
        case TokenType::PLOTRAW_KW: case TokenType::TOGGLE_FULLSCREEN_KW: {
            int ln = current().line;
            std::string cmd = current().value;
            advance();
            std::vector<ExprPtr> args;
            if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) && !check(TokenType::COLON)) {
                args.push_back(parse_expr());
                while (match(TokenType::COMMA)) args.push_back(parse_expr());
            }
            expect_newline();
            auto call = make_call(cmd, std::move(args), ln);
            return make_expr_stmt(std::move(call), ln);
        }
        case TokenType::IDENTIFIER: return parse_ident_stmt();
        case TokenType::LBRACKET: {
            // Destructuring: [t0, t1, ...] = expr
            // Targets may be plain variables ([A, B] = [B, A]) OR indexed
            // lvalues ([arr[i], arr[j]] = [...], [m{k1}, m{k2}] = [...]),
            // which makes in-place array/map swaps a one-liner.
            int ln = current().line;
            advance(); // [
            std::vector<ExprPtr> targets;
            targets.push_back(parse_expr());
            while (match(TokenType::COMMA)) targets.push_back(parse_expr());
            expect(TokenType::RBRACKET, "']'");
            expect(TokenType::ASSIGN, "'='");
            ExprPtr val = parse_expr();
            expect_newline();

            bool all_simple = true;
            for (auto& t : targets) {
                if (t->kind == ExprKind::VARIABLE) continue;
                if (t->kind == ExprKind::INDEX) { all_simple = false; continue; }
                throw std::runtime_error("Parse error at line " + std::to_string(ln) +
                    ": destructuring target must be a variable or an indexed element");
            }

            auto s = std::make_unique<Stmt>();
            s->kind = StmtKind::DESTRUCTURE;
            s->line = ln;

            if (all_simple) {
                // Fast path (unchanged): plain variable targets.
                for (auto& t : targets) s->destruct_vars.push_back(t->str_val);
                s->expr = std::move(val);
                return s;
            }

            // Indexed / mixed targets: desugar into temps + per-target stores
            // so the RHS is evaluated ONCE, up front (swap-safe), and each
            // store reuses the existing ASSIGN / INDEX_ASSIGN paths.
            static int destr_seq = 0;
            std::vector<ExprPtr> rvals;
            if (val->kind == ExprKind::ARRAY_LITERAL &&
                val->args.size() == targets.size()) {
                // [t..] = [e0, e1, ...] - capture each element in its OWN typed
                // temp. Keeps a string element a string (a temp array would make
                // the element reads runtime-tagged, and `strarr[i] = <rt-str>`
                // mis-coerces via jdb_val on the native path).
                for (size_t i = 0; i < val->args.size(); i++) {
                    std::string t = "__destr_" + std::to_string(destr_seq++);
                    s->body.push_back(make_let(t, VarType::NONE,
                                               std::move(val->args[i]), ln));
                    rvals.push_back(make_var(t, ln));
                }
            } else {
                // [t..] = <array expression> - one temp array, indexed per target.
                std::string tmp = "__destr_" + std::to_string(destr_seq++);
                s->body.push_back(make_let(tmp, VarType::NONE, std::move(val), ln));
                for (size_t i = 0; i < targets.size(); i++)
                    rvals.push_back(make_index(make_var(tmp, ln),
                                               make_int_lit((int64_t)i, ln), ln));
            }
            for (size_t i = 0; i < targets.size(); i++) {
                if (targets[i]->kind == ExprKind::VARIABLE) {
                    s->body.push_back(make_let(targets[i]->str_val, VarType::NONE,
                                               std::move(rvals[i]), ln));
                    continue;
                }
                // INDEX target → flatten container[i0][i1]... to base var + chain
                // (covers a[i], m{k} since both parse to ExprKind::INDEX, plus
                // nested a[i][j]).
                std::vector<ExprPtr> chain;
                ExprPtr node = std::move(targets[i]);
                while (node->kind == ExprKind::INDEX) {
                    chain.push_back(std::move(node->right));
                    node = std::move(node->left);
                }
                if (node->kind != ExprKind::VARIABLE)
                    throw std::runtime_error("Parse error at line " + std::to_string(ln) +
                        ": destructuring index target must have a variable base");
                std::reverse(chain.begin(), chain.end());
                s->body.push_back(make_index_assign(node->str_val, std::move(chain),
                                                     std::move(rvals[i]), ln));
            }
            return s;
        }
        default:
            throw std::runtime_error("Parse error at line " + std::to_string(current().line) +
                ": unexpected token '" + current().value + "'");
    }
}

StmtPtr Parser::parse_let() {
    int ln = current().line;
    advance(); // LET
    std::string name = expect(TokenType::IDENTIFIER, "variable name").value;
    VarType vt = VarType::NONE;

    if (match(TokenType::AS)) {
        vt = parse_type();
    }

    expect(TokenType::ASSIGN, "'='");
    ExprPtr val = parse_expr();
    expect_newline();
    return make_let(name, vt, std::move(val), ln);
}

// Parse a single DIM clause (one variable). The DIM keyword has already
// been consumed by parse_dim(); this just handles the
//   <name>[ '[' shape ']' ] [ AS Type ] [ '=' expr ]
// part. Used by parse_dim() in a comma loop so that
//   DIM a, b, c                      → 3 statements
//   DIM x AS INTEGER, y AS STRING    → 2 statements with their own types
// all work without affecting each other.
StmtPtr Parser::parse_dim_clause(int ln) {
    std::string name = expect(TokenType::IDENTIFIER, "variable name").value;
    VarType vt = VarType::NONE;
    VarType et = VarType::NONE;
    std::string udt_name;

    // Constructor args. Declared at the outer scope so the trailing
    // `s->ctor_args = std::move(ctor_args)` sees them. Populated only
    // inside the `if (match(AS))` branch when the type is a UDT.
    std::vector<ExprPtr> ctor_args;

    // Classic-BASIC array form: DIM A[20], DIM M[5,3], DIM A[20] AS INTEGER
    ExprPtr val;
    if (check(TokenType::LBRACKET)) {
        advance(); // [
        std::vector<ExprPtr> dims;
        if (!check(TokenType::RBRACKET)) {
            dims.push_back(parse_expr());
            while (match(TokenType::COMMA)) dims.push_back(parse_expr());
        }
        expect(TokenType::RBRACKET, "']'");

        auto shape = std::make_unique<Expr>();
        shape->kind = ExprKind::ARRAY_LITERAL;
        shape->args = std::move(dims);
        shape->line = ln;

        auto call = std::make_unique<Expr>();
        call->kind = ExprKind::CALL;
        call->func_name = "ZEROS";
        call->args.push_back(std::move(shape));
        call->line = ln;

        val = std::move(call);
        vt = VarType::ARRAY;
    }

    if (match(TokenType::AS)) {
        if (current().type == TokenType::IDENTIFIER && current().value == "REACT") {
            advance();
        }
        if (current().type == TokenType::IDENTIFIER && current().value != "MAP") {
            udt_name = current().value;
            // Allow dotted type names: MODULE.TypeName
            while (pos + 1 < tokens.size() && peek_at(1).type == TokenType::DOT) {
                // Check: ident DOT ident
                size_t saved = pos;
                advance(); // consume first ident
                if (check(TokenType::DOT)) {
                    advance(); // consume dot
                    if (current().type == TokenType::IDENTIFIER) {
                        udt_name += "." + current().value;
                    } else {
                        pos = saved; break;
                    }
                } else {
                    pos = saved; break;
                }
            }
        }
        VarType type_after_as = parse_type();

        // Constructor args: `DIM x AS T(a, b)` (scalar) or
        // `DIM arr[N] AS T(vec1, vec2)` (per-element vectors).
        // Only valid when the type is a UDT.
        if (!udt_name.empty() && check(TokenType::LPAREN)) {
            advance(); // (
            if (!check(TokenType::RPAREN)) {
                ctor_args.push_back(parse_expr());
                while (match(TokenType::COMMA)) ctor_args.push_back(parse_expr());
            }
            expect(TokenType::RPAREN, "')' after constructor arguments");
        }
        // STRICT-mode friendly syntax: `DIM arr AS T[]` declares a typed
        // empty array. Element type is T (with udt_name if T is a UDT).
        // Equivalent to `DIM arr[0] AS T` but without the size expression.
        bool has_array_suffix = false;
        if (check(TokenType::LBRACKET) && peek_at(1).type == TokenType::RBRACKET) {
            advance(); // [
            advance(); // ]
            has_array_suffix = true;
        }
        if (has_array_suffix) {
            et = type_after_as;
            vt = VarType::ARRAY;
            if (!val) {
                if (et == VarType::OBJECT && !udt_name.empty()) {
                    // UDT-typed empty array: route through __MAKE_UDT_ARRAY__
                    // with shape [0] so native codegen sees the element type
                    // and sets up a correctly-tagged empty JdbArray.
                    auto shape = std::make_unique<Expr>();
                    shape->kind = ExprKind::ARRAY_LITERAL;
                    shape->line = ln;
                    shape->args.push_back(make_int_lit(0, ln));

                    auto call = std::make_unique<Expr>();
                    call->kind = ExprKind::CALL;
                    call->func_name = "__MAKE_UDT_ARRAY__";
                    call->args.push_back(std::move(shape));
                    call->args.push_back(make_string_lit(udt_name, ln));
                    call->line = ln;
                    val = std::move(call);
                } else {
                    // Non-UDT typed empty array: plain `[]` literal is fine -
                    // JdbArray* of length 0, element type tracked statically
                    // in codegen's type env (added in Phase 2).
                    auto lit = std::make_unique<Expr>();
                    lit->kind = ExprKind::ARRAY_LITERAL;
                    lit->line = ln;
                    val = std::move(lit);
                }
            }
        } else if (vt == VarType::ARRAY && val) {
            et = type_after_as;
            if (et == VarType::OBJECT && !udt_name.empty()) {
                if (val->kind == ExprKind::CALL && val->func_name == "ZEROS") {
                    val->func_name = "__MAKE_UDT_ARRAY__";
                    auto type_lit = make_string_lit(udt_name, ln);
                    val->args.push_back(std::move(type_lit));
                    // Append per-element ctor vectors as additional args.
                    // Runtime walks arr leaves and calls INIT(slot, vec1[i], vec2[i], ...).
                    for (auto& a : ctor_args) val->args.push_back(std::move(a));
                    ctor_args.clear();
                }
            }
        } else {
            vt = type_after_as;
            if (vt == VarType::ARRAY && is_type_token(current().type)) {
                et = token_to_vartype(advance().type);
            }
        }
    }

    if (match(TokenType::ASSIGN)) {
        val = parse_expr();
    }

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::DIM;
    s->var_name = name;
    s->var_type = vt;
    s->elem_type = et;
    s->label = udt_name;
    s->expr = std::move(val);
    s->ctor_args = std::move(ctor_args);
    s->line = ln;
    return s;
}

StmtPtr Parser::parse_dim() {
    int ln = current().line;
    advance(); // DIM

    // Parse first declaration. Each clause has its own type / initializer
    // so `DIM a AS INTEGER, b AS STRING` declares an int and a string.
    StmtPtr first = parse_dim_clause(ln);

    // Comma-separated additional declarations are queued for the next
    // parse_statement() calls.
    while (match(TokenType::COMMA)) {
        pending_stmts.push_back(parse_dim_clause(ln));
    }
    expect_newline();
    return first;
}

StmtPtr Parser::parse_const() {
    int ln = current().line;
    advance(); // CONST
    // CONST name = value
    std::string name = expect(TokenType::IDENTIFIER, "constant name").value;
    expect(TokenType::ASSIGN, "'=' after constant name");
    auto val = parse_expr();
    expect_newline();
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::LET;
    s->line = ln;
    s->var_name = name;
    s->expr = std::move(val);
    s->is_const = true;
    return s;
}

StmtPtr Parser::parse_print() {
    int ln = current().line;
    advance(); // PRINT

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::PRINT;
    s->print_newline = true;
    s->line = ln;

    if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) && !check(TokenType::COLON)) {
        s->print_exprs.push_back(parse_expr());
        s->print_seps.push_back(0); // no separator before first item

        while (check(TokenType::SEMICOLON) || check(TokenType::COMMA)) {
            int sep = (current().type == TokenType::COMMA) ? 1 : 2;
            advance(); // consume separator

            // Trailing separator at end of statement?
            if (check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN) || check(TokenType::COLON)) {
                // Trailing comma or semicolon = suppress newline
                s->print_newline = false;
                break;
            }
            s->print_exprs.push_back(parse_expr());
            s->print_seps.push_back(sep);
        }
    }
    expect_newline();
    return s;
}

StmtPtr Parser::parse_input() {
    int ln = current().line;
    advance(); // INPUT

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::INPUT;
    s->line = ln;
    s->print_newline = false; // becomes true when "," separator is used

    // Forms supported:
    //   INPUT var$                - no prompt
    //   INPUT prompt_expr ; var$  - prompt without "? " suffix
    //   INPUT prompt_expr , var$  - prompt with "? " suffix
    // The prompt may be any expression (string literal, variable, call, ...).
    // We parse one expression first; if a separator follows, the expression
    // was the prompt and the next identifier is the variable. Otherwise the
    // expression itself must have been a bare identifier, and that's the var.
    ExprPtr first = parse_expr();

    if (check(TokenType::SEMICOLON) || check(TokenType::COMMA)) {
        s->print_newline = check(TokenType::COMMA);
        advance(); // consume separator
        s->expr = std::move(first); // prompt expression
        s->var_name = expect(TokenType::IDENTIFIER, "variable name").value;
    } else {
        // No prompt: the expression must be a bare variable.
        if (!first || first->kind != ExprKind::VARIABLE) {
            throw std::runtime_error("Parse error at line " + std::to_string(ln) +
                ": INPUT expects a variable name (or prompt; var / prompt, var)");
        }
        s->var_name = first->str_val;
    }
    expect_newline();
    return s;
}

StmtPtr Parser::parse_goto() {
    int ln = current().line;
    advance(); // GOTO
    std::string label = expect(TokenType::IDENTIFIER, "label name").value;
    expect_newline();
    return make_goto(label, ln);
}

StmtPtr Parser::parse_if() {
    int ln = current().line;
    advance(); // IF

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::IF;
    s->line = ln;

    // First branch: IF condition THEN
    IfBranch first;
    first.condition = parse_expr();
    expect(TokenType::THEN, "'THEN'");

    // One-liner IF: IF cond THEN stmt [:stmt...] [ELSE stmt [:stmt...]]
    if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN)) {
        bool saved_inline = in_inline_if_;
        in_inline_if_ = true;
        // parse_statement() calls expect_newline() which consumes ':'.
        // So after each statement, check if we're still on the same line
        // and if the next token continues the one-liner body.
        while (true) {
            if (check(TokenType::ELSE) || check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN)) break;
            first.body.push_back(parse_statement());
            // parse_statement consumed ':' via expect_newline - check if still on same line
            if (current().line != ln) break;
            if (check(TokenType::ELSE)) break;
            if (check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN)) break;
        }
        s->branches.push_back(std::move(first));

        // Optional ELSE on same line
        if (check(TokenType::ELSE) && current().line == ln) {
            advance(); // ELSE
            IfBranch else_branch;
            while (true) {
                if (check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN)) break;
                else_branch.body.push_back(parse_statement());
                if (current().line != ln) break;
                if (check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN)) break;
            }
            s->branches.push_back(std::move(else_branch));
        }
        in_inline_if_ = saved_inline;
        // Don't expect END IF for one-liners
        return s;
    }

    expect_newline();
    skip_newlines();

    // Helper: check for end-of-if-block tokens
    auto is_end_if = [this]() {
        return check(TokenType::ENDIF_KW) || (check(TokenType::END) && peek_at(1).type == TokenType::IF);
    };

    // Collect body until ELSEIF, ELSE, or END IF / ENDIF
    while (!check(TokenType::ELSEIF) && !check(TokenType::ELSE) && !is_end_if() && !check(TokenType::EOF_TOKEN)) {
        first.body.push_back(parse_statement());
        skip_newlines();
    }
    s->branches.push_back(std::move(first));

    // ELSEIF branches
    while (check(TokenType::ELSEIF)) {
        advance(); // ELSEIF
        IfBranch branch;
        branch.condition = parse_expr();
        expect(TokenType::THEN, "'THEN'");
        expect_newline();
        skip_newlines();
        while (!check(TokenType::ELSEIF) && !check(TokenType::ELSE) && !is_end_if() && !check(TokenType::EOF_TOKEN)) {
            branch.body.push_back(parse_statement());
            skip_newlines();
        }
        s->branches.push_back(std::move(branch));
    }

    // ELSE branch
    if (match(TokenType::ELSE)) {
        expect_newline();
        skip_newlines();
        IfBranch else_branch;
        while (!is_end_if() && !check(TokenType::EOF_TOKEN)) {
            else_branch.body.push_back(parse_statement());
            skip_newlines();
        }
        s->branches.push_back(std::move(else_branch));
    }

    if (match(TokenType::ENDIF_KW)) { /* single keyword */ }
    else { expect(TokenType::END, "'END'"); expect(TokenType::IF, "'IF'"); }
    expect_newline();
    return s;
}

std::vector<Param> Parser::parse_params() {
    std::vector<Param> params;
    expect(TokenType::LPAREN, "'('");
    if (!check(TokenType::RPAREN)) {
        do {
            Param p;
            p.name = expect(TokenType::IDENTIFIER, "parameter name").value;
            if (match(TokenType::AS)) {
                p.type = parse_type();
            }
            params.push_back(std::move(p));
        } while (match(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, "')'");
    return params;
}

StmtPtr Parser::parse_sub() {
    int ln = current().line;
    advance(); // SUB
    std::string name = expect(TokenType::IDENTIFIER, "sub name").value;

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::SUB;
    s->func_name = name;
    s->line = ln;
    s->params = parse_params();
    expect_newline();
    skip_newlines();

    while (!(check(TokenType::END) && peek_at(1).type == TokenType::SUB) &&
           !check(TokenType::ENDSUB) && !check(TokenType::EOF_TOKEN)) {
        s->body.push_back(parse_statement());
        skip_newlines();
    }

    if (match(TokenType::ENDSUB)) { /* single keyword */ }
    else { expect(TokenType::END, "'END'"); expect(TokenType::SUB, "'SUB'"); }
    expect_newline();
    return s;
}

StmtPtr Parser::parse_function() {
    int ln = current().line;
    advance(); // FUNCTION
    std::string name = expect(TokenType::IDENTIFIER, "function name").value;

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::FUNCTION;
    s->func_name = name;
    s->line = ln;
    s->params = parse_params();

    if (match(TokenType::AS)) {
        s->return_type = parse_type();
    }
    expect_newline();
    skip_newlines();

    while (!(check(TokenType::END) && peek_at(1).type == TokenType::FUNCTION) &&
           !check(TokenType::ENDFUNC) && !check(TokenType::EOF_TOKEN)) {
        s->body.push_back(parse_statement());
        skip_newlines();
    }

    if (match(TokenType::ENDFUNC)) { /* single keyword */ }
    else { expect(TokenType::END, "'END'"); expect(TokenType::FUNCTION, "'FUNCTION'"); }
    expect_newline();
    return s;
}

StmtPtr Parser::parse_do_loop() {
    int ln = current().line;
    advance(); // DO

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::DO_LOOP;
    s->line = ln;
    s->is_while = true;
    s->cond_at_top = false;

    // DO WHILE expr / DO UNTIL expr
    if (check(TokenType::WHILE) || check(TokenType::UNTIL)) {
        s->cond_at_top = true;
        s->is_while = check(TokenType::WHILE);
        advance();
        s->loop_cond = parse_expr();
    }
    expect_newline();
    skip_newlines();

    // Body
    while (!check(TokenType::LOOP) && !check(TokenType::EOF_TOKEN)) {
        s->body.push_back(parse_statement());
        skip_newlines();
    }

    expect(TokenType::LOOP, "'LOOP'");

    // LOOP WHILE expr / LOOP UNTIL expr
    if (!s->cond_at_top && (check(TokenType::WHILE) || check(TokenType::UNTIL))) {
        s->is_while = check(TokenType::WHILE);
        advance();
        s->loop_cond = parse_expr();
    }
    expect_newline();
    return s;
}

StmtPtr Parser::parse_for() {
    int ln = current().line;
    advance(); // FOR

    // FOR EACH var IN collection
    if (check(TokenType::EACH)) {
        advance(); // EACH
        std::string var = expect(TokenType::IDENTIFIER, "loop variable").value;
        expect(TokenType::IN, "'IN'");
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::FOR_EACH;
        s->var_name = var;
        s->expr = parse_expr(); // collection
        s->line = ln;
        if (check(TokenType::NEWLINE) || check(TokenType::COLON)) advance();
        skip_newlines();
        while (!check(TokenType::NEXT) && !check(TokenType::EOF_TOKEN)) {
            s->body.push_back(parse_statement());
            skip_newlines();
        }
        expect(TokenType::NEXT, "'NEXT'");
        if (check(TokenType::IDENTIFIER)) advance();
        expect_newline();
        return s;
    }

    std::string var = expect(TokenType::IDENTIFIER, "loop variable").value;
    expect(TokenType::ASSIGN, "'='");
    ExprPtr start_expr = parse_expr();
    expect(TokenType::TO, "'TO'");
    if (check(TokenType::NEWLINE) || check(TokenType::COLON) || check(TokenType::EOF_TOKEN)) {
        throw std::runtime_error("Parse error at line " + std::to_string(ln) +
            ": FOR is missing its end value after TO");
    }
    ExprPtr end_val = parse_expr();
    ExprPtr step_val;
    if (match(TokenType::STEP)) {
        step_val = parse_expr();
    }

    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::FOR_LOOP;
    s->var_name = var;
    s->expr = std::move(start_expr);
    s->end_expr = std::move(end_val);
    s->step_expr = std::move(step_val);
    s->line = ln;

    // Consume newline or colon after FOR header
    if (check(TokenType::NEWLINE) || check(TokenType::COLON)) advance();

    // Collect body until NEXT
    skip_newlines();
    while (!check(TokenType::NEXT) && !check(TokenType::EOF_TOKEN)) {
        s->body.push_back(parse_statement());
        skip_newlines();
    }

    expect(TokenType::NEXT, "'NEXT'");
    // Optional variable name after NEXT
    if (check(TokenType::IDENTIFIER)) advance();
    expect_newline();
    return s;
}

StmtPtr Parser::parse_return() {
    int ln = current().line;
    advance(); // RETURN
    ExprPtr val;
    if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN)) {
        val = parse_expr();
    }
    expect_newline();
    return make_return(std::move(val), ln);
}

StmtPtr Parser::parse_ident_stmt() {
    int ln = current().line;
    std::string name = current().value;

    // ── Built-in commands callable as statements (no parens needed) ──
    // CD, PWD, MKDIR, KILL, DIR, TRON, TROFF, DUMP - must also be usable as
    // function calls (e.g. PRINT PWD()), so only match here when NOT followed
    // by '(', '=', '.', ':', '->'.
    {
        std::string upper = name;
        for (auto& c : upper) c = std::toupper((unsigned char)c);
        bool is_builtin_cmd = (upper == "CD" || upper == "PWD" || upper == "MKDIR" ||
                               upper == "KILL" || upper == "DIR" || upper == "TRON" ||
                               upper == "TROFF" || upper == "DUMP");
        if (is_builtin_cmd) {
            TokenType nt = peek_at(1).type;
            bool is_call_or_lvalue = (nt == TokenType::LPAREN || nt == TokenType::ASSIGN ||
                                      nt == TokenType::DOT || nt == TokenType::COLON ||
                                      nt == TokenType::ARROW);
            if (!is_call_or_lvalue) {
                advance(); // consume command name
                std::vector<ExprPtr> args;
                if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) && !check(TokenType::COLON)) {
                    args.push_back(parse_expr());
                    while (match(TokenType::COMMA)) args.push_back(parse_expr());
                }
                expect_newline();
                auto call = make_call(upper, std::move(args), ln);
                // CD and PWD return the path - print it for the user
                if (upper == "CD" || upper == "PWD") {
                    auto s = std::make_unique<Stmt>();
                    s->kind = StmtKind::PRINT;
                    s->print_newline = true;
                    s->line = ln;
                    s->print_exprs.push_back(std::move(call));
                    s->print_seps.push_back(0);
                    return s;
                }
                return make_expr_stmt(std::move(call), ln);
            }
        }
    }

    // Label: identifier followed by ':'. Not inside a one-liner IF body, where
    // `THEN Sub : ...` is a no-args call followed by the ':' separator.
    if (peek_at(1).type == TokenType::COLON && !in_inline_if_) {
        advance(); // identifier
        advance(); // :
        if (check(TokenType::NEWLINE)) advance();
        return make_label(name, ln);
    }

    // Reactive assignment: var -> expr
    if (peek_at(1).type == TokenType::ARROW) {
        advance(); // identifier
        advance(); // ->
        // Capture formula as string from tokens
        size_t formula_start = pos;
        ExprPtr val = parse_expr();
        // Reconstruct formula string
        std::string formula;
        for (size_t i = formula_start; i < pos; i++) {
            if (i > formula_start) formula += " ";
            formula += tokens[i].value;
        }
        expect_newline();
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::REACT_ASSIGN;
        s->var_name = name;
        s->label = formula; // formula string for DUMP/re-eval
        s->expr = std::move(val);
        return s;
    }

    // Assignment: identifier = expr
    if (peek_at(1).type == TokenType::ASSIGN) {
        advance(); // identifier
        advance(); // =
        ExprPtr val = parse_expr();
        expect_newline();
        return make_assign(name, std::move(val), ln);
    }

    // Dotted chain: obj.x.y.method(args).prop = value
    // Build the full LHS expression, then check for = at the end
    if (peek_at(1).type == TokenType::DOT) {
        advance(); // consume identifier
        ExprPtr lhs = make_var(name, ln);

        // First, try to collect a pure dotted name for simple function calls
        // Peek ahead: if it's all dots+idents ending in (, it's a simple dotted call
        {
            std::string dotted = name;
            size_t saved = pos;
            bool pure_dots = true;
            while (check(TokenType::DOT)) {
                advance();
                // Accept any word token after dot (identifiers AND keywords like TEXT, END, etc.)
                TokenType nt = current().type;
                if (nt != TokenType::IDENTIFIER && nt != TokenType::EOF_TOKEN &&
                    nt != TokenType::NEWLINE && !current().value.empty() &&
                    std::isalpha(current().value[0])) {
                    // It's a keyword used as a member name - accept it
                    dotted += "." + advance().value;
                } else if (nt == TokenType::IDENTIFIER) {
                    dotted += "." + advance().value;
                } else {
                    pure_dots = false; break;
                }
            }
            if (pure_dots && check(TokenType::LPAREN)) {
                // Only commit to method-call form when the '(' sits
                // IMMEDIATELY after the identifier (no whitespace). A
                // space between the name and the paren means the '('
                // is grouping the first argument of an imperative call
                // - e.g. 'TURTLE.SETPOS (x+1), (y+1)' should parse as
                // SETPOS with two args, not as 'SETPOS((x+1))' followed
                // by stray ', (y+1)'.
                const Token& prev_name = tokens[pos - 1];
                const Token& lpar_tok  = tokens[pos];
                // Lexer stores Token.col as the column AFTER the last
                // consumed character (i.e. where the next char will
                // land). Adjacency therefore means lpar.col equals
                // prev.col + lpar's own width - no whitespace between.
                bool paren_is_adjacent =
                    (lpar_tok.line == prev_name.line &&
                     lpar_tok.col  == prev_name.col + (int)lpar_tok.value.size());
                if (paren_is_adjacent) {
                    // Simple dotted function call: HTTP.SETHEADER(...)
                    advance(); // (
                    std::vector<ExprPtr> args;
                    if (!check(TokenType::RPAREN)) {
                        args.push_back(parse_expr());
                        while (match(TokenType::COMMA)) args.push_back(parse_expr());
                    }
                    expect(TokenType::RPAREN, "')'");
                    // If chain continues (.field, [idx], {key}), fall through to chain parser
                    if (check(TokenType::DOT) || check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
                        lhs = make_call(dotted, std::move(args), ln);
                        pure_dots = false; // prevent rewind below
                    } else {
                        auto call = make_call(dotted, std::move(args), ln);
                        expect_newline();
                        return make_expr_stmt(std::move(call), ln);
                    }
                }
                // else: fall through to the imperative-call branch below,
                // which already accepts LPAREN as the start of the first
                // bare argument.
            }
            if (pure_dots && check(TokenType::ASSIGN)) {
                // Simple dotted assignment: obj.field = val
                advance(); // =
                auto val = parse_expr();
                expect_newline();
                return make_assign(dotted, std::move(val), ln);
            }
            if (pure_dots && check(TokenType::ARROW)) {
                // Dotted reactive assignment: obj.field -> expr
                advance(); // ->
                size_t formula_start = pos;
                ExprPtr val = parse_expr();
                std::string formula;
                for (size_t fi = formula_start; fi < pos; fi++) {
                    if (fi > formula_start) formula += " ";
                    formula += tokens[fi].value;
                }
                expect_newline();
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::REACT_ASSIGN;
                s->var_name = dotted;
                s->label = formula;
                s->expr = std::move(val);
                return s;
            }
            // Module-qualified indexed assignment: MOD.ARR[i1, i2, ...] = val
            // Disambiguates against the no-parens dotted call `MOD.FN [arr]`
            // by peeking past the matching `]` for an `=`. Emits the same
            // shape as the local `name[i, j] = val` path so module-rename
            // and codegen_index_assign treat it identically.
            if (pure_dots && check(TokenType::LBRACKET)) {
                size_t la = pos;
                int depth = 0;
                while (la < tokens.size()) {
                    TokenType lt = tokens[la].type;
                    if (lt == TokenType::LBRACKET) depth++;
                    else if (lt == TokenType::RBRACKET) {
                        depth--;
                        if (depth == 0) { la++; break; }
                    } else if (lt == TokenType::NEWLINE ||
                               lt == TokenType::EOF_TOKEN) break;
                    la++;
                }
                if (la < tokens.size() &&
                    tokens[la].type == TokenType::ASSIGN) {
                    advance(); // [
                    std::vector<ExprPtr> simple_chain;
                    simple_chain.push_back(parse_expr());
                    while (match(TokenType::COMMA))
                        simple_chain.push_back(parse_expr());
                    expect(TokenType::RBRACKET, "']'");
                    advance(); // =
                    ExprPtr val = parse_expr();
                    expect_newline();
                    return make_index_assign(dotted, std::move(simple_chain),
                                             std::move(val), ln);
                }
            }
            // Check if this is a module function call or a method call on a variable.
            // Module calls have known prefixes (multiple dots or known module names).
            // Single-dot names like "q.INIT" could be method calls on local vars.
            // Heuristic: if the dotted name has only ONE dot and the first part is
            // lowercase or matches a known local/param, treat as method chain.
            bool likely_method_call = false;
            if (pure_dots) {
                size_t dot_count = 0;
                for (char ch : dotted) if (ch == '.') dot_count++;
                if (dot_count == 1) {
                    // Single dot: check if first part could be a variable
                    // Variables: lowercase start, or known in current scope
                    std::string first_part = dotted.substr(0, dotted.find('.'));
                    // If it's a short name (< 4 chars) and doesn't look like a module,
                    // it's likely a variable
                    bool looks_like_module = (first_part.length() >= 3);
                    // Check if it matches known module-style prefixes
                    if (looks_like_module) {
                        // Known modules/namespaces are typically UPPERCASE
                        bool all_upper = true;
                        for (char ch : first_part) {
                            if (ch != '_' && !std::isupper((unsigned char)ch) && !std::isdigit((unsigned char)ch))
                                all_upper = false;
                        }
                        if (!all_upper) likely_method_call = true;
                    } else {
                        likely_method_call = true;
                    }
                }
            }

            // No-parens dotted call (only for module-style calls, not method calls)
            if (pure_dots && !likely_method_call) {
                TokenType nt = current().type;
                if (nt == TokenType::STRING_LIT || nt == TokenType::INTEGER_LIT ||
                    nt == TokenType::FLOAT_LIT || nt == TokenType::TRUE_KW ||
                    nt == TokenType::FALSE_KW || nt == TokenType::LBRACKET ||
                    nt == TokenType::LBRACE || nt == TokenType::IDENTIFIER ||
                    nt == TokenType::MINUS || nt == TokenType::LPAREN ||
                    nt == TokenType::NOT || nt == TokenType::THIS_KW) {
                    std::vector<ExprPtr> args;
                    args.push_back(parse_expr());
                    while (match(TokenType::COMMA)) args.push_back(parse_expr());
                    auto call = make_call(dotted, std::move(args), ln);
                    expect_newline();
                    return make_expr_stmt(std::move(call), ln);
                }
            }
            if (pure_dots && !likely_method_call &&
                (check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN) || check(TokenType::COLON))) {
                // Dotted name at end of line → no-args function call (e.g. SOUND.INIT, SOUND.RESET)
                std::vector<ExprPtr> no_args;
                auto call = make_call(dotted, std::move(no_args), ln);
                expect_newline();
                return make_expr_stmt(std::move(call), ln);
            }
            // Fall through to chain parser (handles method calls on objects)
            pos = saved;
        }

        // Build chain with parse_postfix-like logic (for complex chains with method calls)
        while (true) {
            if (check(TokenType::DOT)) {
                advance(); // .
                std::string field = advance().value; // accept any token as field
                if (check(TokenType::LPAREN)) {
                    // .method(args)
                    auto member = std::make_unique<Expr>();
                    member->kind = ExprKind::MEMBER_ACCESS;
                    member->str_val = field;
                    member->left = std::move(lhs);
                    member->line = ln;
                    advance(); // (
                    std::vector<ExprPtr> args;
                    if (!check(TokenType::RPAREN)) {
                        args.push_back(parse_expr());
                        while (match(TokenType::COMMA)) args.push_back(parse_expr());
                    }
                    expect(TokenType::RPAREN, "')'");
                    auto call = std::make_unique<Expr>();
                    call->kind = ExprKind::CALL;
                    call->func_name = "__METHOD__";
                    call->left = std::move(member);
                    call->args = std::move(args);
                    call->line = ln;
                    lhs = std::move(call);
                } else {
                    // Check if this is a no-parens method call: obj.method arg1, arg2
                    // Only if followed by an expression token (not newline/colon/assign/dot)
                    TokenType nt = current().type;
                    bool has_args = (nt == TokenType::STRING_LIT || nt == TokenType::INTEGER_LIT ||
                        nt == TokenType::FLOAT_LIT || nt == TokenType::TRUE_KW ||
                        nt == TokenType::FALSE_KW || nt == TokenType::LBRACKET ||
                        nt == TokenType::LBRACE || nt == TokenType::IDENTIFIER ||
                        nt == TokenType::MINUS || nt == TokenType::LPAREN ||
                        nt == TokenType::NOT || nt == TokenType::THIS_KW);
                    // But NOT if followed by another dot (chain continues) or assign
                    if (has_args && nt != TokenType::DOT && !check(TokenType::ASSIGN)) {
                        // No-parens method call: obj.method arg1, arg2
                        auto member = std::make_unique<Expr>();
                        member->kind = ExprKind::MEMBER_ACCESS;
                        member->str_val = field;
                        member->left = std::move(lhs);
                        member->line = ln;
                        std::vector<ExprPtr> args;
                        args.push_back(parse_expr());
                        while (match(TokenType::COMMA)) args.push_back(parse_expr());
                        auto call = std::make_unique<Expr>();
                        call->kind = ExprKind::CALL;
                        call->func_name = "__METHOD__";
                        call->left = std::move(member);
                        call->args = std::move(args);
                        call->line = ln;
                        expect_newline();
                        return make_expr_stmt(std::move(call), ln);
                    }
                    auto member = std::make_unique<Expr>();
                    member->kind = ExprKind::MEMBER_ACCESS;
                    member->str_val = field;
                    member->left = std::move(lhs);
                    member->line = ln;
                    lhs = std::move(member);
                }
            } else if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
                bool brace = check(TokenType::LBRACE);
                advance();
                auto idx = parse_expr();
                // Multi-dim read: C[a, b, c] expands to C[a][b][c]
                if (!brace && check(TokenType::COMMA)) {
                    while (match(TokenType::COMMA)) {
                        lhs = make_index(std::move(lhs), std::move(idx), ln);
                        idx = parse_expr();
                    }
                }
                if (brace) expect(TokenType::RBRACE, "'}'");
                else expect(TokenType::RBRACKET, "']'");
                lhs = make_index(std::move(lhs), std::move(idx), ln);
            } else break;
        }

        // Check for assignment: lhs = expr
        if (check(TokenType::ASSIGN)) {
            advance(); // =
            ExprPtr val = parse_expr();
            expect_newline();

            // If LHS ends with MEMBER_ACCESS, compile as: eval obj, push val, SET_FIELD
            if (lhs->kind == ExprKind::MEMBER_ACCESS) {
                // Create a special statement: expr.field = val
                // Reuse INDEX_ASSIGN: compile lhs->left, then INDEX_SET with string key
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::EXPR_STMT; // We'll handle this specially
                // Actually, create an expression that does the set
                // Simplest: emit as inline bytecode via a new stmt kind
                // Let's use a trick: make it an EXPR_STMT with a special "set" expression
                // For now, use MEMBER_ACCESS + assignment
                s->kind = StmtKind::INDEX_ASSIGN;
                s->line = ln;
                s->expr = std::move(val);
                // We need to store the object expression and field name
                // Store object expr in print_exprs[0] and field in label
                s->label = lhs->str_val; // field name
                s->print_exprs.push_back(std::move(lhs->left)); // object expression
                return s;
            }
            // If LHS ends with INDEX, compile as: eval container, idx, val, INDEX_SET
            if (lhs->kind == ExprKind::INDEX) {
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::INDEX_ASSIGN;
                s->line = ln;
                s->expr = std::move(val);
                s->label = "__INDEX__";
                s->print_exprs.push_back(std::move(lhs->left)); // container
                s->print_exprs.push_back(std::move(lhs->right)); // index
                return s;
            }
            // Fallback: shouldn't happen
            throw std::runtime_error("Cannot assign to expression at line " + std::to_string(ln));
        }

        expect_newline();
        return make_expr_stmt(std::move(lhs), ln);
    }

    // Index/field assignment: identifier[i1]{i2}.field[i3]... = expr
    // Supports any combination of [ ], { }, and .field after the leading
    // identifier so things like `Tracks[i].Name = "x"` parse correctly.
    if (peek_at(1).type == TokenType::LBRACKET ||
        peek_at(1).type == TokenType::LBRACE) {
        advance(); // identifier
        // First index/brace step (we know there's at least one [ or {).
        // Track whether the chain so far is a *pure* index chain (only
        // [..]/{...} on the leading var) - that lets us still emit the
        // optimised make_index_assign for the simple case.
        std::vector<ExprPtr> simple_chain;
        ExprPtr chain_expr; // populated as soon as we see a `.` or another mixed step
        bool pure_index = true;

        auto into_expr = [&]() {
            if (chain_expr) return;
            chain_expr = make_var(name, ln);
            for (auto& idx : simple_chain)
                chain_expr = make_index(std::move(chain_expr), std::move(idx), ln);
            simple_chain.clear();
        };

        while (true) {
            if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
                bool is_brace = check(TokenType::LBRACE);
                advance(); // [ or {
                ExprPtr first_idx = parse_expr();
                // Multi-dim shortcut: arr[a, b, c] → arr[a][b][c]
                std::vector<ExprPtr> these_indices;
                these_indices.push_back(std::move(first_idx));
                if (!is_brace) {
                    while (match(TokenType::COMMA))
                        these_indices.push_back(parse_expr());
                }
                if (is_brace) expect(TokenType::RBRACE, "'}'");
                else expect(TokenType::RBRACKET, "']'");

                if (pure_index) {
                    for (auto& i : these_indices)
                        simple_chain.push_back(std::move(i));
                } else {
                    for (auto& i : these_indices)
                        chain_expr = make_index(std::move(chain_expr), std::move(i), ln);
                }
            } else if (check(TokenType::DOT)) {
                pure_index = false;
                into_expr(); // collapse simple_chain into chain_expr
                advance(); // .
                std::string field = advance().value; // accept any token as field
                auto member = std::make_unique<Expr>();
                member->kind = ExprKind::MEMBER_ACCESS;
                member->str_val = field;
                member->left = std::move(chain_expr);
                member->line = ln;
                // Method call: arr[i].method(args) - wrap the MEMBER_ACCESS in
                // a __METHOD__ CALL. Mirrors parse_postfix's handling but at
                // statement scope so `cs[1].BUMP(5)` parses as a statement.
                if (check(TokenType::LPAREN)) {
                    advance(); // (
                    std::vector<ExprPtr> args;
                    if (!check(TokenType::RPAREN)) {
                        args.push_back(parse_expr());
                        while (match(TokenType::COMMA)) args.push_back(parse_expr());
                    }
                    expect(TokenType::RPAREN, "')'");
                    auto call = std::make_unique<Expr>();
                    call->kind = ExprKind::CALL;
                    call->func_name = "__METHOD__";
                    call->left = std::move(member);
                    call->args = std::move(args);
                    call->line = ln;
                    chain_expr = std::move(call);
                } else {
                    chain_expr = std::move(member);
                }
            } else {
                break;
            }
        }

        if (check(TokenType::ASSIGN)) {
            advance(); // =
            ExprPtr val = parse_expr();
            expect_newline();
            if (pure_index) {
                return make_index_assign(name, std::move(simple_chain),
                                         std::move(val), ln);
            }
            // Mixed chain - last node decides how to assign
            if (chain_expr->kind == ExprKind::MEMBER_ACCESS) {
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::INDEX_ASSIGN;
                s->line = ln;
                s->expr = std::move(val);
                s->label = chain_expr->str_val; // field name
                s->print_exprs.push_back(std::move(chain_expr->left));
                return s;
            }
            if (chain_expr->kind == ExprKind::INDEX) {
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::INDEX_ASSIGN;
                s->line = ln;
                s->expr = std::move(val);
                s->label = "__INDEX__";
                s->print_exprs.push_back(std::move(chain_expr->left));
                s->print_exprs.push_back(std::move(chain_expr->right));
                return s;
            }
            throw std::runtime_error("Cannot assign to expression at line " + std::to_string(ln));
        }

        // Expression statement, not an assignment
        if (pure_index) into_expr();
        expect_newline();
        return make_expr_stmt(std::move(chain_expr), ln);
    }

    // Function call with parens: NAME(args)
    if (peek_at(1).type == TokenType::LPAREN) {
        // Fall through to parse_expr which handles Name(args)
        ExprPtr expr = parse_expr();
        expect_newline();
        return make_expr_stmt(std::move(expr), ln);
    }

    // No-parens procedure call: NAME arg1, arg2, ...
    // Classic BASIC style: TXTWRITER "file", "content"
    {
        TokenType next = peek_at(1).type;
        if (next == TokenType::STRING_LIT || next == TokenType::INTEGER_LIT ||
            next == TokenType::FLOAT_LIT || next == TokenType::TRUE_KW ||
            next == TokenType::FALSE_KW || next == TokenType::LBRACKET ||
            next == TokenType::LBRACE || next == TokenType::IDENTIFIER ||
            next == TokenType::MINUS || next == TokenType::NOT) {
            advance(); // consume the identifier
            std::vector<ExprPtr> args;
            args.push_back(parse_expr());
            while (match(TokenType::COMMA)) args.push_back(parse_expr());
            auto call = make_call(name, std::move(args), ln);
            expect_newline();
            return make_expr_stmt(std::move(call), ln);
        }
    }

    // Bare identifier: a no-args SUB/FUNC call (LIST, VARS, HELP, CLS, ...).
    // Terminated by end-of-statement: NEWLINE/EOF, or - inside a one-liner IF -
    // ELSE or the ':' separator (so `IF c THEN Sub : x` / `THEN Sub ELSE x` call
    // Sub instead of evaluating it as a discarded expression).
    if (peek_at(1).type == TokenType::NEWLINE || peek_at(1).type == TokenType::EOF_TOKEN ||
        peek_at(1).type == TokenType::ELSE ||
        (in_inline_if_ && peek_at(1).type == TokenType::COLON)) {
        advance(); // consume identifier
        std::vector<ExprPtr> no_args;
        auto call = make_call(name, std::move(no_args), ln);
        expect_newline();
        return make_expr_stmt(std::move(call), ln);
    }

    // Expression statement (fallback)
    ExprPtr expr = parse_expr();
    expect_newline();
    return make_expr_stmt(std::move(expr), ln);
}

// ── Expressions ──────────────────────────────────────────────

ExprPtr Parser::parse_expr() {
    auto left = parse_or();
    // Pipe operator: lowest precedence
    while (check(TokenType::PIPE)) {
        int ln = current().line;
        advance(); // |>
        auto right = parse_or();
        auto pipe = std::make_unique<Expr>();
        pipe->kind = ExprKind::PIPE_EXPR;
        pipe->left = std::move(left);
        pipe->right = std::move(right);
        pipe->line = ln;
        left = std::move(pipe);
    }
    return left;
}

ExprPtr Parser::parse_or() {
    auto left = parse_and();
    while (check(TokenType::OR) || check(TokenType::ORELSE)) {
        int ln = current().line;
        TokenType op = advance().type;
        auto right = parse_and();
        left = make_binary(op, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_and() {
    auto left = parse_not();
    while (check(TokenType::AND) || check(TokenType::ANDALSO)) {
        int ln = current().line;
        TokenType op = advance().type;
        auto right = parse_not();
        left = make_binary(op, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_not() {
    if (check(TokenType::NOT)) {
        int ln = current().line;
        advance();
        auto operand = parse_not();
        return make_unary(TokenType::NOT, std::move(operand), ln);
    }
    return parse_bor();
}

ExprPtr Parser::parse_bor() {
    auto left = parse_xor();
    while (check(TokenType::BOR)) {
        int ln = current().line;
        advance();
        auto right = parse_xor();
        left = make_binary(TokenType::BOR, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_xor() {
    auto left = parse_band();
    while (check(TokenType::XOR) || check(TokenType::BXOR)) {
        int ln = current().line;
        advance();
        auto right = parse_band();
        left = make_binary(TokenType::XOR, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_band() {
    auto left = parse_comparison();
    while (check(TokenType::BAND)) {
        int ln = current().line;
        advance();
        auto right = parse_comparison();
        left = make_binary(TokenType::BAND, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_comparison() {
    auto left = parse_shift();
    while (check(TokenType::GT) || check(TokenType::LT) || check(TokenType::GE) ||
           check(TokenType::LE) || check(TokenType::NE) || check(TokenType::ASSIGN) ||
           check(TokenType::IN) ||
           // Classic-BASIC infix NOT: `IF x NOT y` means `IF x <> y`.
           // Must peek ahead to distinguish from `NOT expr` (unary prefix)
           // - only treat as binary when the NEXT token is a primary
           // (literal, identifier, LPAREN, ...), not THEN / NEWLINE / EOF.
           (check(TokenType::NOT) &&
            peek_at(1).type != TokenType::THEN &&
            peek_at(1).type != TokenType::NEWLINE &&
            peek_at(1).type != TokenType::EOF_TOKEN &&
            peek_at(1).type != TokenType::AND &&
            peek_at(1).type != TokenType::OR)) {
        int ln = current().line;
        TokenType op = current().type;
        // NOT → treat as <>
        if (op == TokenType::NOT) op = TokenType::NE;
        advance();
        auto right = parse_shift();
        left = make_binary(op, std::move(left), std::move(right), ln);
    }
    return left;
}

// SHL / SHR - C-style precedence: looser than additive, tighter than
// comparison. So `1 + 2 SHL 3` is `(1+2) SHL 3` = 24 and
// `5 BAND 3 SHL 1` is `5 BAND (3 SHL 1)` = 4. The function form
// `SHL(x, n)` / `SHR(x, n)` still works (registered as natives in vm.cpp)
// since the lexer matches keywords ahead of identifiers, but the call-site
// parser only routes via natives when the token is not consumed here.
ExprPtr Parser::parse_shift() {
    auto left = parse_addition();
    while (check(TokenType::SHL) || check(TokenType::SHR)) {
        int ln = current().line;
        TokenType op = advance().type;
        auto right = parse_addition();
        left = make_binary(op, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_addition() {
    auto left = parse_multiplication();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        int ln = current().line;
        TokenType op = advance().type;
        auto right = parse_multiplication();
        left = make_binary(op, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_multiplication() {
    auto left = parse_power();
    while (check(TokenType::STAR) || check(TokenType::SLASH) ||
           check(TokenType::BACKSLASH) || check(TokenType::MOD) ||
           (check(TokenType::IDENTIFIER) && peek_at(1).type == TokenType::AT)) {
        int ln = current().line;
        // Function-as-operator: expr FUNCNAME@ expr → FUNCNAME(left, right)
        if (check(TokenType::IDENTIFIER) && peek_at(1).type == TokenType::AT) {
            std::string fn = advance().value; // IDENTIFIER
            advance(); // @
            auto right = parse_power();
            std::vector<ExprPtr> args;
            args.push_back(std::move(left));
            args.push_back(std::move(right));
            left = make_call(fn, std::move(args), ln);
        } else {
            TokenType op = advance().type;
            auto right = parse_power();
            left = make_binary(op, std::move(left), std::move(right), ln);
        }
    }
    return left;
}

ExprPtr Parser::parse_power() {
    auto left = parse_unary();
    if (check(TokenType::CARET)) {
        int ln = current().line;
        advance();
        auto right = parse_power(); // right-associative
        left = make_binary(TokenType::CARET, std::move(left), std::move(right), ln);
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenType::MINUS)) {
        int ln = current().line;
        advance();
        auto operand = parse_unary();
        return make_unary(TokenType::MINUS, std::move(operand), ln);
    }
    if (check(TokenType::BNOT)) {
        int ln = current().line;
        advance();
        auto operand = parse_unary();
        return make_unary(TokenType::BNOT, std::move(operand), ln);
    }
    return parse_primary();
}

ExprPtr Parser::parse_primary() {
    int ln = current().line;

    // Boolean literals
    if (match(TokenType::TRUE_KW))  return make_bool_lit(true, ln);
    if (match(TokenType::FALSE_KW)) return make_bool_lit(false, ln);

    // Integer literal
    if (check(TokenType::INTEGER_LIT)) {
        int64_t val = std::stoll(advance().value);
        return make_int_lit(val, ln);
    }

    // Float literal
    if (check(TokenType::FLOAT_LIT)) {
        double val = std::stod(advance().value);
        return make_float_lit(val, ln);
    }

    // String literal
    if (check(TokenType::STRING_LIT)) {
        return make_string_lit(advance().value, ln);
    }

    // Parenthesized expression
    if (match(TokenType::LPAREN)) {
        auto expr = parse_expr();
        expect(TokenType::RPAREN, "')'");
        return parse_postfix(std::move(expr));
    }

    // Array literal [a, b, c]
    if (match(TokenType::LBRACKET)) {
        std::vector<ExprPtr> elems;
        if (!check(TokenType::RBRACKET)) {
            elems.push_back(parse_expr());
            while (match(TokenType::COMMA)) {
                elems.push_back(parse_expr());
            }
        }
        expect(TokenType::RBRACKET, "']'");
        auto arr = make_array_literal(std::move(elems), ln);
        // Allow postfix ops directly on the literal: ["a","b"][0], [1,2].method()
        return parse_postfix(std::move(arr));
    }

    // AWAIT expr → call AWAIT(expr)
    if (check(TokenType::AWAIT_KW)) {
        advance();
        auto arg = parse_unary();
        std::vector<ExprPtr> args;
        args.push_back(std::move(arg));
        return make_call("AWAIT", std::move(args), ln);
    }

    // Placeholder ? (for pipe operator)
    if (check(TokenType::PLACEHOLDER)) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::PLACEHOLDER_EXPR;
        e->line = ln;
        return e;
    }

    // Lambda: lambda [USE(captures)] params -> body
    if (check(TokenType::LAMBDA)) {
        advance(); // LAMBDA
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::LAMBDA_EXPR;
        e->line = ln;

        // Optional USE(captures)
        if (check(TokenType::USE)) {
            advance(); // USE
            expect(TokenType::LPAREN, "'('");
            e->lambda_captures.push_back(expect(TokenType::IDENTIFIER, "capture var").value);
            while (match(TokenType::COMMA))
                e->lambda_captures.push_back(expect(TokenType::IDENTIFIER, "capture var").value);
            expect(TokenType::RPAREN, "')'");
        }

        // Parameters
        e->lambda_params.push_back(expect(TokenType::IDENTIFIER, "parameter").value);
        while (match(TokenType::COMMA))
            e->lambda_params.push_back(expect(TokenType::IDENTIFIER, "parameter").value);

        expect(TokenType::ARROW, "'->'");
        e->right = parse_expr(); // body expression
        return e;
    }

    // THIS keyword → variable with postfix member access
    if (check(TokenType::THIS_KW)) {
        advance();
        auto expr = make_var("THIS", ln);
        return parse_postfix(std::move(expr));
    }

    // SHL / SHR as function call - keeps the legacy `SHL(x, n)` syntax
    // working alongside the new infix `x SHL n`. The lexer always emits
    // a keyword token; here we route to the registered native if the next
    // token is `(`.
    if ((check(TokenType::SHL) || check(TokenType::SHR)) &&
        peek_at(1).type == TokenType::LPAREN) {
        std::string name = (current().type == TokenType::SHL) ? "SHL" : "SHR";
        advance(); // SHL/SHR
        advance(); // (
        std::vector<ExprPtr> args;
        if (!check(TokenType::RPAREN)) {
            args.push_back(parse_expr());
            while (match(TokenType::COMMA)) args.push_back(parse_expr());
        }
        expect(TokenType::RPAREN, "')'");
        return make_call(name, std::move(args), ln);
    }

    // Map literal: {"key": value, ...}
    if (check(TokenType::LBRACE)) {
        advance(); // {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::MAP_LITERAL;
        e->line = ln;
        if (!check(TokenType::RBRACE)) {
            // Parse key: value pairs
            do {
                std::string key = expect(TokenType::STRING_LIT, "map key string").value;
                expect(TokenType::COLON, "':'");
                e->map_keys.push_back(key);
                e->args.push_back(parse_expr());
            } while (match(TokenType::COMMA));
        }
        expect(TokenType::RBRACE, "'}'");
        return e;
    }

    // Identifier (variable or function call)
    if (check(TokenType::IDENTIFIER)) {
        std::string name = advance().value;

        // Dotted name: Enum.Member, MAP.FUNC, GUI.TEXT, GUI.INPUT etc.
        // Accept keywords after dot (they're valid member/function names)
        while (check(TokenType::DOT)) {
            auto& next = peek_at(1);
            if (next.type == TokenType::IDENTIFIER ||
                (next.type != TokenType::EOF_TOKEN && next.type != TokenType::NEWLINE &&
                 !next.value.empty() && std::isalpha(next.value[0]))) {
                advance(); // .
                name += "." + advance().value;
            } else break;
        }
        if (name.find('.') != std::string::npos) {
            // Fall through to check for function call or variable
        }

        // Function reference: name@
        if (check(TokenType::AT)) {
            advance(); // @
            return make_funcref_lit(name, ln);
        }

        // Function call: name(args) or Dotted.Name(args)
        if (check(TokenType::LPAREN)) {
            advance(); // (
            std::vector<ExprPtr> args;
            if (!check(TokenType::RPAREN)) {
                args.push_back(parse_expr());
                while (match(TokenType::COMMA)) {
                    args.push_back(parse_expr());
                }
            }
            expect(TokenType::RPAREN, "')'");
            auto expr = make_call(name, std::move(args), ln);
            return parse_postfix(std::move(expr));
        }

        auto expr = make_var(name, ln);
        return parse_postfix(std::move(expr));
    }

    throw std::runtime_error("Parse error at line " + std::to_string(ln) +
        ": unexpected token '" + current().value + "'");
}

// ── Module import ───────────────────────────────────────────────

#include "lexer.h"

std::vector<StmtPtr> Parser::parse_import() {
    int ln = current().line;
    advance(); // IMPORT
    std::string module_name = expect(TokenType::IDENTIFIER, "module name").value;
    expect_newline();

    // Circular import check
    if (imported_modules.count(module_name))
        return {}; // already imported, skip
    imported_modules.insert(module_name);

    // Read the module file
    if (!file_reader) {
        throw std::runtime_error("Parse error at line " + std::to_string(ln) +
            ": IMPORT not available (no file reader)");
    }
    auto [source, module_file_path] = file_reader(module_name);
    if (source.empty()) {
        throw std::runtime_error("Parse error at line " + std::to_string(ln) +
            ": cannot load module '" + module_name + "'");
    }

    // Lex + parse the module file
    Lexer mod_lexer(source);
    auto mod_tokens = mod_lexer.tokenize();
    Parser mod_parser(mod_tokens);
    mod_parser.file_reader = file_reader;
    mod_parser.imported_modules = imported_modules; // share import set
    mod_parser.current_source_file = module_file_path; // propagate file path
    auto mod_stmts = mod_parser.parse();
    // Propagate any new imports back
    imported_modules = mod_parser.imported_modules;

    // Separate: statements from sub-imports (already renamed) vs own module stmts
    // Sub-imported stmts were inserted at the beginning by recursive parse_import()
    // Own module code starts after all sub-imports are processed.
    // Strategy: Track which stmts are "own" by recording count before sub-parse.
    // Since mod_parser.parse() returns all stmts (sub-imports first, then own),
    // we mark the split point: all FUNC/SUB/LET/DIM whose names don't contain '.'
    // or '__' are own module code.

    // Collect module-defined function names and which are exported
    // Only consider functions whose names are NOT already dotted (from sub-imports)
    std::unordered_set<std::string> exported_funcs;
    std::unordered_set<std::string> exported_vars;
    std::unordered_set<std::string> all_funcs;
    std::unordered_set<std::string> module_vars;

    for (auto& s : mod_stmts) {
        if (s->kind == StmtKind::FUNCTION || s->kind == StmtKind::SUB) {
            // Skip already-renamed functions from sub-imports
            if (s->func_name.find('.') != std::string::npos ||
                s->func_name.substr(0, 2) == "__") continue;
            all_funcs.insert(s->func_name);
            if (s->label == "__EXPORT__") exported_funcs.insert(s->func_name);
        }
        // TYPE_DECL: constructor + methods are functions
        if (s->kind == StmtKind::TYPE_DECL) {
            if (s->func_name.find('.') != std::string::npos) continue;
            // The TYPE creates a constructor function with the same name
            all_funcs.insert(s->func_name);
            if (s->label == "__EXPORT__") exported_funcs.insert(s->func_name);
            // Also rename all methods - they're already named "TypeName.Method"
            for (auto& member : s->body) {
                if (member->kind == StmtKind::FUNCTION || member->kind == StmtKind::SUB) {
                    // member->func_name is already "T_ENTITY.INIT" etc.
                    all_funcs.insert(member->func_name);
                    if (s->label == "__EXPORT__") exported_funcs.insert(member->func_name);
                }
            }
        }
        if (s->kind == StmtKind::LET || s->kind == StmtKind::DIM) {
            if (s->var_name.find('.') == std::string::npos) {
                module_vars.insert(s->var_name);
                if (s->label == "__EXPORT__") exported_vars.insert(s->var_name);
            }
        }
        if (s->kind == StmtKind::ASSIGN) {
            if (s->var_name.find('.') == std::string::npos)
                module_vars.insert(s->var_name);
        }
    }

    // Build rename maps
    std::unordered_map<std::string, std::string> func_map;
    for (auto& fn : all_funcs) {
        if (exported_funcs.count(fn))
            func_map[fn] = module_name + "." + fn;
        else
            func_map[fn] = "__" + module_name + "__" + fn;
    }

    std::unordered_map<std::string, std::string> var_map;
    for (auto& v : module_vars) {
        if (exported_vars.count(v))
            var_map[v] = module_name + "." + v;
        else
            var_map[v] = "__" + module_name + "__" + v;
    }

    // Apply renames only to own module statements (skip already-renamed sub-imports)
    for (auto& s : mod_stmts) {
        // Skip statements from sub-imports (already have dotted or mangled names)
        bool is_subimport = false;
        if (s->kind == StmtKind::FUNCTION || s->kind == StmtKind::SUB ||
            s->kind == StmtKind::TYPE_DECL) {
            is_subimport = (s->func_name.find('.') != std::string::npos ||
                            s->func_name.substr(0, 2) == "__");
        }
        if (s->kind == StmtKind::LET || s->kind == StmtKind::DIM || s->kind == StmtKind::ASSIGN) {
            if (s->var_name.find('.') != std::string::npos ||
                s->var_name.substr(0, 2) == "__") {
                is_subimport = true;
            }
        }
        if (!is_subimport) {
            module_rename_stmt(*s, func_map, var_map);
            // Tag with module source file recursively (module_rename_stmt
            // already visits every descendant - no separate loop needed).
            module_set_source_file(*s, module_file_path);
        }
    }

    return mod_stmts;
}

// ── AST rewriting helpers ───────────────────────────────────────

void Parser::module_rename_expr(Expr& expr,
    const std::unordered_map<std::string, std::string>& func_map,
    const std::unordered_map<std::string, std::string>& var_map) {

    switch (expr.kind) {
        case ExprKind::VARIABLE: {
            auto it = var_map.find(expr.str_val);
            if (it != var_map.end()) expr.str_val = it->second;
            break;
        }
        case ExprKind::LITERAL_STRING: {
            // Funcref literal (`name@`) - rewrite the embedded function
            // name to the module-qualified slot so cross-module dispatch
            // and stored-funcref dispatch both find it via the VM's
            // global lookup. Plain string literals fall through.
            if (expr.is_funcref_lit) {
                auto it = func_map.find(expr.str_val);
                if (it != func_map.end()) expr.str_val = it->second;
            }
            break;
        }
        case ExprKind::CALL: {
            // Rename function calls to module-defined functions
            auto it = func_map.find(expr.func_name);
            if (it != func_map.end()) {
                expr.func_name = it->second;
            } else {
                // Funcref stored in a module-level variable: rewrite the
                // call site to the qualified slot ("MOD.cb" / "__MOD__cb")
                // so the VM's CALL-fallback can find the global. Without
                // this, `cb(x)` inside the module emits CALL "CB" and the
                // VM's `global_names.find("CB")` misses the namespaced
                // entry - every dispatch dies with "Undefined function".
                auto vit = var_map.find(expr.func_name);
                if (vit != var_map.end()) expr.func_name = vit->second;
            }
            // Recurse into args
            for (auto& a : expr.args) module_rename_expr(*a, func_map, var_map);
            // Recurse into left (for method calls)
            if (expr.left) module_rename_expr(*expr.left, func_map, var_map);
            break;
        }
        case ExprKind::BINARY:
            if (expr.left) module_rename_expr(*expr.left, func_map, var_map);
            if (expr.right) module_rename_expr(*expr.right, func_map, var_map);
            break;
        case ExprKind::UNARY:
            if (expr.right) module_rename_expr(*expr.right, func_map, var_map);
            break;
        case ExprKind::INDEX:
            if (expr.left) module_rename_expr(*expr.left, func_map, var_map);
            if (expr.right) module_rename_expr(*expr.right, func_map, var_map);
            break;
        case ExprKind::ARRAY_LITERAL:
        case ExprKind::MAP_LITERAL:
            for (auto& a : expr.args) module_rename_expr(*a, func_map, var_map);
            break;
        case ExprKind::MEMBER_ACCESS:
            if (expr.left) module_rename_expr(*expr.left, func_map, var_map);
            break;
        case ExprKind::PIPE_EXPR:
        case ExprKind::LAMBDA_EXPR:
            if (expr.left) module_rename_expr(*expr.left, func_map, var_map);
            if (expr.right) module_rename_expr(*expr.right, func_map, var_map);
            break;
        default: break;
    }
}

// Recursively tag every statement and its descendants with a source file.
void Parser::module_set_source_file(Stmt& stmt, const std::string& path) {
    if (stmt.source_file.empty()) stmt.source_file = path;
    for (auto& s : stmt.body)         if (s) module_set_source_file(*s, path);
    for (auto& s : stmt.catch_body)   if (s) module_set_source_file(*s, path);
    for (auto& s : stmt.finally_body) if (s) module_set_source_file(*s, path);
    for (auto& br : stmt.branches)
        for (auto& s : br.body) if (s) module_set_source_file(*s, path);
}

void Parser::module_rename_stmt(Stmt& stmt,
    const std::unordered_map<std::string, std::string>& func_map,
    const std::unordered_map<std::string, std::string>& var_map) {

    // Rename function/sub declarations
    if (stmt.kind == StmtKind::FUNCTION || stmt.kind == StmtKind::SUB) {
        auto it = func_map.find(stmt.func_name);
        if (it != func_map.end()) stmt.func_name = it->second;
        // Shadow guard: a SUB/FUNC parameter that has the SAME name as a
        // module-level variable would be silently rewritten to the global's
        // namespaced name by the rename pass below, and the function body's
        // uses of the parameter would resolve to the global instead. The
        // surface symptom is "the param value is ignored and the body sees
        // whatever the global holds" - debugged 2026-05-20 on the DOOM
        // renderer's `DrawWallStrip(tex_name$ AS STRING)` colliding with
        // the module's `DIM tex_name$[...]`. Catch it at parse time.
        for (auto& p : stmt.params) {
            if (var_map.count(p.name)) {
                throw std::runtime_error("Parse error at line " +
                    std::to_string(stmt.line) +
                    ": parameter '" + p.name + "' in '" + stmt.func_name +
                    "' shadows a module-level variable of the same name; "
                    "rename the parameter to avoid silent name capture.");
            }
        }
    }

    // Rename TYPE declarations: type name + method names
    if (stmt.kind == StmtKind::TYPE_DECL) {
        auto it = func_map.find(stmt.func_name);
        if (it != func_map.end()) {
            stmt.func_name = it->second;
        }
        // Methods: their func_name is already "OldType.Method", rename via func_map
        for (auto& member : stmt.body) {
            if (member->kind == StmtKind::FUNCTION || member->kind == StmtKind::SUB) {
                auto mit = func_map.find(member->func_name);
                if (mit != func_map.end()) member->func_name = mit->second;
            }
        }
    }

    // Rename variable declarations + index assigns
    if (stmt.kind == StmtKind::LET || stmt.kind == StmtKind::DIM ||
        stmt.kind == StmtKind::ASSIGN || stmt.kind == StmtKind::INDEX_ASSIGN) {
        auto it = var_map.find(stmt.var_name);
        if (it != var_map.end()) stmt.var_name = it->second;
    }

    // DIM x AS TypeName - when TypeName is a module-internal type name
    // referenced from within the same module's body, the rename pass
    // would otherwise leave the label unmangled and compile_dim wouldn't
    // recognise it (no entry in user_types under the bare name).
    if (stmt.kind == StmtKind::DIM && !stmt.label.empty()) {
        auto it = func_map.find(stmt.label);
        if (it != func_map.end()) stmt.label = it->second;
    }

    // Rename expressions
    if (stmt.expr) module_rename_expr(*stmt.expr, func_map, var_map);
    if (stmt.end_expr) module_rename_expr(*stmt.end_expr, func_map, var_map);
    if (stmt.step_expr) module_rename_expr(*stmt.step_expr, func_map, var_map);
    if (stmt.loop_cond) module_rename_expr(*stmt.loop_cond, func_map, var_map);
    for (auto& e : stmt.print_exprs) module_rename_expr(*e, func_map, var_map);
    for (auto& e : stmt.index_chain) module_rename_expr(*e, func_map, var_map);
    for (auto& e : stmt.ctor_args) module_rename_expr(*e, func_map, var_map);

    // Rename in branches (IF/SWITCH).
    // SWITCH multi-case lives in `case_labels` (vector of (low, high)
    // expression pairs), separate from the `condition` slot used by IF
    // and single-value CASE. Without renaming those, `CASE AM_ABS` in a
    // module's SWITCH would keep the unqualified name while the DIM
    // gets namespaced to `__MOD__AM_ABS`, and the comparison silently
    // misses on every iteration.
    for (auto& br : stmt.branches) {
        if (br.condition) module_rename_expr(*br.condition, func_map, var_map);
        for (auto& lbl : br.case_labels) {
            if (lbl.first)  module_rename_expr(*lbl.first,  func_map, var_map);
            if (lbl.second) module_rename_expr(*lbl.second, func_map, var_map);
        }
        for (auto& s : br.body) module_rename_stmt(*s, func_map, var_map);
    }

    // Rename in body (FUNC/SUB/DO/FOR/TRY)
    for (auto& s : stmt.body) module_rename_stmt(*s, func_map, var_map);
    for (auto& s : stmt.catch_body) module_rename_stmt(*s, func_map, var_map);
    for (auto& s : stmt.finally_body) module_rename_stmt(*s, func_map, var_map);
}

// ── Postfix parsing ─────────────────────────────────────────────

ExprPtr Parser::parse_postfix(ExprPtr expr) {
    // Postfix chain: [idx], {key}, .field, .method(args)
    while (true) {
        if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
            int ln = current().line;
            bool is_brace = check(TokenType::LBRACE);
            advance();
            auto idx = parse_expr();
            // Multi-dim read: C[a, b, c] → C[a][b][c]
            if (!is_brace && check(TokenType::COMMA)) {
                while (match(TokenType::COMMA)) {
                    expr = make_index(std::move(expr), std::move(idx), ln);
                    idx = parse_expr();
                }
            }
            if (is_brace) expect(TokenType::RBRACE, "'}'");
            else expect(TokenType::RBRACKET, "']'");
            expr = make_index(std::move(expr), std::move(idx), ln);
        }
        else if (check(TokenType::DOT)) {
            int ln = current().line;
            advance(); // .
            std::string field = advance().value; // accept any token as field
            // .method(args) → member access then call
            if (check(TokenType::LPAREN)) {
                // First: get the member (creates the dispatch target)
                auto member = std::make_unique<Expr>();
                member->kind = ExprKind::MEMBER_ACCESS;
                member->str_val = field;
                member->left = std::move(expr);
                member->line = ln;
                // Then: call it with args
                advance(); // (
                std::vector<ExprPtr> args;
                if (!check(TokenType::RPAREN)) {
                    args.push_back(parse_expr());
                    while (match(TokenType::COMMA)) args.push_back(parse_expr());
                }
                expect(TokenType::RPAREN, "')'");
                // Create a CALL expression on the member result
                auto call = std::make_unique<Expr>();
                call->kind = ExprKind::CALL;
                call->func_name = "__METHOD__";
                call->left = std::move(member); // object.method
                call->args = std::move(args);
                call->line = ln;
                expr = std::move(call);
            } else {
                auto member = std::make_unique<Expr>();
                member->kind = ExprKind::MEMBER_ACCESS;
                member->str_val = field;
                member->left = std::move(expr);
                member->line = ln;
                expr = std::move(member);
            }
        }
        else break;
    }
    return expr;
}
