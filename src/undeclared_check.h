#pragma once
// Names a program reads or assigns without declaring them anywhere. Shared by
// --lint, which lists them, and by the compiler, which rejects them in a file
// that says OPTION "EXPLICIT". Scopes are flat: a name counts as declared when
// any DIM, LET, parameter, loop variable, lambda parameter, TYPE, ENUM, FUNC or
// SUB in the program introduces it.

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include "ast.h"

struct UndeclaredRef {
    std::string name;
    int line;
    std::string file;
};

// The files whose OPTION statements leave EXPLICIT switched on.
inline std::set<std::string> explicit_option_files(const std::vector<StmtPtr>& ast) {
    std::set<std::string> files;
    for (auto& s : ast) {
        if (!s || s->kind != StmtKind::OPTION_STMT || !s->expr ||
            s->expr->kind != ExprKind::LITERAL_STRING) continue;
        std::string opt = s->expr->str_val;
        std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
        if (opt == "EXPLICIT") files.insert(s->source_file());
        else if (opt == "EXPLICITOFF" || opt == "NOEXPLICIT") files.erase(s->source_file());
    }
    return files;
}

inline std::vector<UndeclaredRef> find_undeclared(
        const std::vector<StmtPtr>& ast,
        const std::function<bool(const std::string&)>& is_builtin,
        std::vector<std::string>* defined_funcs = nullptr) {
    std::set<std::string> declared;
    for (const char* k : {"MATH.PI", "MATH.E", "TRUE", "FALSE", "NULL", "NONE", "VBNEWLINE",
                          "NOTHING", "ERR", "ERL", "ERRMSG$", "ERRLINE", "STACK$", "THIS"})
        declared.insert(k);

    std::function<void(const Expr*)> collect_expr = [&](const Expr* e) {
        if (!e) return;
        if (e->kind == ExprKind::LAMBDA_EXPR) {
            for (auto& p : e->lambda_params) declared.insert(p);
            for (auto& c : e->lambda_captures) declared.insert(c);
        }
        collect_expr(e->left.get());
        collect_expr(e->right.get());
        for (auto& a : e->args) collect_expr(a.get());
    };
    std::function<void(const std::vector<StmtPtr>&)> collect = [&](const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) {
            if (!s) continue;
            switch (s->kind) {
                case StmtKind::LET:
                case StmtKind::DIM:
                case StmtKind::REACT_ASSIGN:
                    declared.insert(s->var_name);
                    break;
                case StmtKind::SUB:
                case StmtKind::FUNCTION:
                    declared.insert(s->func_name);
                    if (defined_funcs) defined_funcs->push_back(s->func_name);
                    for (auto& p : s->params()) declared.insert(p.name);
                    break;
                case StmtKind::FOR_LOOP:
                    declared.insert(s->var_name);
                    break;
                case StmtKind::FOR_EACH:
                    declared.insert(s->var_name);
                    if (!s->label.empty()) declared.insert(s->label);
                    break;
                case StmtKind::DESTRUCTURE:
                    for (auto& v : s->destruct_vars()) declared.insert(v);
                    break;
                case StmtKind::TYPE_DECL:
                    declared.insert(s->func_name);
                    break;
                case StmtKind::ENUM_DECL:
                    declared.insert(s->func_name);
                    for (auto& m : s->enum_members()) declared.insert(m.first);
                    break;
                default: break;
            }
            collect_expr(s->expr.get());
            for (auto& pe : s->print_exprs) collect_expr(pe.get());
            for (auto& br : s->branches) collect_expr(br.condition.get());
            collect(s->body);
            collect(s->catch_body());
            collect(s->finally_body());
            for (auto& br : s->branches) collect(br.body);
        }
    };
    collect(ast);

    auto known = [&](const std::string& n) {
        auto dot = n.find('.');
        std::string head = dot == std::string::npos ? n : n.substr(0, dot);
        return declared.count(head) || declared.count(n) || is_builtin(n) || is_builtin(head);
    };

    std::vector<UndeclaredRef> out;
    std::string file;
    std::function<void(const Expr*)> walk_expr = [&](const Expr* e) {
        if (!e) return;
        if (e->kind == ExprKind::VARIABLE && !e->str_val.empty() && !known(e->str_val))
            out.push_back({e->str_val, e->line, file});
        walk_expr(e->left.get());
        walk_expr(e->right.get());
        for (auto& a : e->args) walk_expr(a.get());
    };
    std::function<void(const std::vector<StmtPtr>&)> walk = [&](const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) {
            if (!s) continue;
            file = s->source_file();
            if ((s->kind == StmtKind::ASSIGN || s->kind == StmtKind::INDEX_ASSIGN) &&
                !s->var_name.empty() && !known(s->var_name))
                out.push_back({s->var_name, s->line, file});
            walk_expr(s->expr.get());
            for (auto& pe : s->print_exprs) walk_expr(pe.get());
            for (auto& ix : s->index_chain) walk_expr(ix.get());
            walk_expr(s->loop_cond.get());
            walk_expr(s->end_expr.get());
            walk_expr(s->step_expr.get());
            for (auto& br : s->branches) walk_expr(br.condition.get());
            walk(s->body);
            walk(s->catch_body());
            walk(s->finally_body());
            for (auto& br : s->branches) walk(br.body);
        }
    };
    walk(ast);
    return out;
}
