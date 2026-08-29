#include "compiler.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <functional>
#include <algorithm>

// Process-global native-slot registry (defined in vm.cpp).
int jdb_native_slot(const std::string& name);

// Rewrite CALL -> CALL_NATIVE in place wherever the callee is a registered
// native. Same operand layout (u16 + u8 argc), so it's a byte patch that keeps
// every jump offset valid. The VM then dispatches the native by direct table
// index, skipping the per-call name copy, hash, and no-vectorize set lookups.
static void relink_call_natives(Chunk& chunk) {
    auto& code = chunk.code;
    size_t n = code.size();
    size_t ip = 0;
    while (ip < n) {
        OpCode op = (OpCode)code[ip];
        int w = opcode_width(op);
        if (w < 1) w = 1;
        if (op == OpCode::CALL && ip + 3 < n) {
            uint16_t name_idx = code[ip + 1] | (code[ip + 2] << 8);
            if (name_idx < chunk.constants.size()
                    && chunk.constants[name_idx].type == ValueType::STRING) {
                int slot = jdb_native_slot(chunk.constants[name_idx].as_string()->data);
                if (slot >= 0 && slot <= 0xFFFF) {
                    code[ip] = (uint8_t)OpCode::CALL_NATIVE;
                    code[ip + 1] = (uint8_t)(slot & 0xFF);
                    code[ip + 2] = (uint8_t)((slot >> 8) & 0xFF);
                }
            }
        }
        ip += w;
    }
}

Compiler::Compiler() {
    scopes.push_back(CompilerScope{}); // main scope
}

CompilerScope& Compiler::current_scope() { return scopes.back(); }
Chunk& Compiler::current_chunk() { return scopes.back().chunk; }

// Force-register a name as a local variable (for LET/DIM inside functions)
uint16_t Compiler::resolve_local(const std::string& name) {
    auto& locals = current_scope().locals;
    auto it = locals.find(name);
    if (it != locals.end()) return it->second;
    uint16_t slot = current_chunk().add_var_name(name);
    locals[name] = slot; // always register as local
    return slot;
}

uint16_t Compiler::resolve_var(const std::string& name) {
    auto& locals = current_scope().locals;
    auto it = locals.find(name);
    if (it != locals.end()) return it->second;
    uint16_t slot = current_chunk().add_var_name(name);
    // In function scope: don't register known globals as locals
    // (they use LOAD_GLOBAL/STORE_GLOBAL with name-based lookup)
    bool is_global_in_func = (scopes.size() > 1) &&
        (known_globals.count(name) > 0 || name.find('.') != std::string::npos);
    if (!is_global_in_func) {
        locals[name] = slot;
    }
    return slot;
}

bool Compiler::should_use_global(const std::string& name) const {
    // Always global if in main scope
    if (scopes.size() <= 1) return true;
    // Dotted names are always global (module vars, enum members)
    if (name.find('.') != std::string::npos) return true;
    // In function scope: global if NOT a local param/var AND IS a known global
    if (scopes.back().locals.count(name) == 0 && known_globals.count(name) > 0)
        return true;
    return false;
}

uint16_t Compiler::resolve_global(const std::string& name) {
    auto it = globals.find(name);
    if (it != globals.end()) return it->second;
    uint16_t idx = static_cast<uint16_t>(globals.size());
    globals[name] = idx;
    return idx;
}

void Compiler::emit_constant(Value val, int line) {
    uint16_t idx = current_chunk().add_constant(std::move(val));
    current_chunk().emit(OpCode::LOAD_CONST, line);
    current_chunk().emit_u16(idx, line);
}

size_t Compiler::emit_jump(OpCode op, int line) {
    current_chunk().emit(op, line);
    size_t addr = current_chunk().code.size();
    current_chunk().emit_u16(0, line); // placeholder
    return addr;
}

void Compiler::patch_jump(size_t addr) {
    size_t target = current_chunk().code.size();
    int16_t offset = static_cast<int16_t>(target - addr - 2);
    current_chunk().patch_i16(addr, offset);
}

bool Compiler::is_static_name(const std::string& name) const {
    // Walk scopes inside-out; STATIC slots are function-scoped so we only
    // match the innermost active function frame, but inner blocks share it.
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        if (it->statics.count(name)) return true;
        // Don't cross function boundaries: each FUNC has its own statics
        if (it->is_function) break;
    }
    return false;
}

// Helper: locate the static slot index for a given name (assumes it exists).
static uint16_t lookup_static_slot(const std::vector<CompilerScope>& scopes,
                                   const std::string& name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto sit = it->statics.find(name);
        if (sit != it->statics.end()) return sit->second;
        if (it->is_function) break;
    }
    return 0; // unreachable if is_static_name() was true
}

void Compiler::emit_var_load(const std::string& name, int line) {
    if (is_static_name(name)) {
        current_chunk().emit(OpCode::LOAD_STATIC, line);
        current_chunk().emit_u16(lookup_static_slot(scopes, name), line);
        return;
    }
    bool use_global = should_use_global(name);
    uint16_t slot = resolve_var(name);
    current_chunk().emit(use_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, line);
    current_chunk().emit_u16(slot, line);
}

void Compiler::emit_var_store(const std::string& name, int line, bool prefer_local) {
    if (is_static_name(name)) {
        current_chunk().emit(OpCode::STORE_STATIC, line);
        current_chunk().emit_u16(lookup_static_slot(scopes, name), line);
        return;
    }
    if (prefer_local) {
        uint16_t slot = resolve_local(name);
        current_chunk().emit(OpCode::STORE_VAR, line);
        current_chunk().emit_u16(slot, line);
    } else {
        bool use_global = should_use_global(name);
        uint16_t slot = resolve_var(name);
        current_chunk().emit(use_global ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, line);
        current_chunk().emit_u16(slot, line);
    }
}

void Compiler::resolve_labels() {
    // Resolve all pending GOTOs against collected label positions.
    // IMPORTANT: labels and GOTOs are per‑chunk (each SUB/FUNC has its own
    // chunk). This function must be called at the end of EACH chunk
    // compilation (SUB, FUNC, TYPE methods, main code) so that it patches
    // the correct chunk. If called only at the very end, SUB labels would
    // be patched into the main chunk → bytecode corruption.
    for (auto& [label, patch_addr] : unresolved_gotos) {
        auto it = label_positions.find(label);
        if (it == label_positions.end()) {
            throw std::runtime_error("Undefined label: " + label);
        }
        uint16_t target = static_cast<uint16_t>(it->second);
        current_chunk().patch_u16(patch_addr, target);
    }
    // Clear for the next chunk
    unresolved_gotos.clear();
    label_positions.clear();
}

// ── Compile program ──────────────────────────────────────────

// ── Pre-scan: collect global variable names ─────────────────────

void Compiler::collect_globals(const std::vector<StmtPtr>& program) {
    // VM-managed globals: set by the runtime in TRY/CATCH and other built-ins.
    // These never appear in user assignments, so without seeding them here,
    // the compiler would treat e.g. ERRMSG$ as an uninitialised local inside
    // a SUB and `PRINT ERRMSG$` would print NONE instead of the error text.
    //
    // Built-in math/string constants (PI/E/INF/NAN/VBNEWLINE/VBCRLF/VBTAB)
    // are registered the same way - register_const(name) in vm.cpp puts
    // them in globals. Without listing them here, a `SUB { ... PI ... }` in
    // a module emits LOAD_VAR (slot never stored) and reads NONE. Top-level
    // works because main-scope LOAD always uses LOAD_GLOBAL.
    static const char* k_vm_globals[] = {
        "ERR", "ERL", "ERRMSG$", "STACK$",
        "PI", "E",
        "VBNEWLINE", "VBCRLF", "VBTAB"
    };
    for (auto* name : k_vm_globals) known_globals.insert(name);

    for (auto& stmt : program) {
        if (stmt->kind != StmtKind::SUB && stmt->kind != StmtKind::FUNCTION) {
            collect_globals_stmt(*stmt);
        }
    }
}

void Compiler::collect_globals_stmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::LET:
        case StmtKind::DIM:
        case StmtKind::ASSIGN:
            known_globals.insert(stmt.var_name);
            break;
        case StmtKind::FOR_LOOP:
        case StmtKind::FOR_EACH:
            known_globals.insert(stmt.var_name);
            for (auto& s : stmt.body) collect_globals_stmt(*s);
            break;
        case StmtKind::INPUT:
            known_globals.insert(stmt.var_name);
            break;
        case StmtKind::DESTRUCTURE:
            for (auto& v : stmt.destruct_vars) known_globals.insert(v);
            break;
        case StmtKind::INDEX_ASSIGN:
            if (!stmt.var_name.empty()) known_globals.insert(stmt.var_name);
            break;
        case StmtKind::IF:
            for (auto& br : stmt.branches)
                for (auto& s : br.body) collect_globals_stmt(*s);
            break;
        case StmtKind::DO_LOOP:
            for (auto& s : stmt.body) collect_globals_stmt(*s);
            break;
        case StmtKind::SWITCH_STMT:
            for (auto& br : stmt.branches)
                for (auto& s : br.body) collect_globals_stmt(*s);
            break;
        case StmtKind::TRY_CATCH:
            for (auto& s : stmt.body) collect_globals_stmt(*s);
            for (auto& s : stmt.catch_body) collect_globals_stmt(*s);
            for (auto& s : stmt.finally_body) collect_globals_stmt(*s);
            break;
        case StmtKind::REACT_ASSIGN:
            known_globals.insert(stmt.var_name);
            break;
        case StmtKind::ENUM_DECL:
            for (auto& [member, val] : stmt.enum_members)
                known_globals.insert(stmt.func_name + "." + member);
            break;
        default: break;
    }
}

void Compiler::collect_globals_expr(const Expr& expr) {
    // Not needed for now - globals are identified by assignment, not by reference
    (void)expr;
}

// ── Compile program ──────────────────────────────────────────

void Compiler::compile(const std::vector<StmtPtr>& program, const std::string& main_source_file) {
    // Set source file on main chunk for debugger
    current_chunk().source_file = main_source_file;

    // Pass 0: collect all global variable names from main code
    collect_globals(program);

    // Pass 0b: pre-register all UDT names so that `DIM x AS UserType`
    // inside a SUB (compiled in Pass 1, before TYPE_DECL is reached in
    // Pass 2) recognises the type and emits the constructor call.
    // Without this, the local fell back to a default empty object and
    // `obj.method(...)` calls failed because __TYPE__ was missing.
    for (auto& stmt : program) {
        if (stmt->kind == StmtKind::TYPE_DECL) {
            user_types.insert(stmt->func_name);
            // Same reason for INIT / DISPOSE: a DIM in a SUB compiled in
            // Pass 1 must already know whether the type has these methods.
            // Track INIT()-zero-arg separately so we can preserve back-compat
            // (auto-call only when INIT takes no user parameters).
            for (auto& m : stmt->body) {
                if (!m) continue;
                if (m->func_name == stmt->func_name + ".INIT") {
                    type_inits.insert(m->func_name);
                    // Parser prepended THIS as param 0; user-param count is
                    // params.size() - 1. Zero-arg INIT means params.size()==1.
                    if (m->params.size() <= 1)
                        type_init_zero_arg.insert(m->func_name);
                } else if (m->func_name == stmt->func_name + ".DISPOSE") {
                    type_disposes.insert(m->func_name);
                }
            }
        }
    }

    // Pass 1: extract SUB/FUNCTION declarations
    for (auto& stmt : program) {
        if (stmt->kind == StmtKind::SUB || stmt->kind == StmtKind::FUNCTION) {
            compile_stmt(*stmt);
        }
    }

    // Pass 2: compile main code
    for (auto& stmt : program) {
        if (stmt->kind != StmtKind::SUB && stmt->kind != StmtKind::FUNCTION) {
            compile_stmt(*stmt);
        }
    }

    current_chunk().emit(OpCode::HALT, 0);
    resolve_labels();

    // Peephole pass: fuse common sequences like LOAD_VAR+LOAD_CONST+SUB.
    // Runs after label resolution so jump offsets are final.
    peephole_optimize(current_chunk());
    relink_call_natives(current_chunk());
    for (auto& f : funcs) {
        peephole_optimize(f.chunk);
        relink_call_natives(f.chunk);
    }
}

// ── Statements ───────────────────────────────────────────────

void Compiler::compile_stmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::LET:          compile_let(stmt); break;
        case StmtKind::DIM:          compile_dim(stmt); break;
        case StmtKind::ASSIGN:       compile_assign(stmt); break;
        case StmtKind::INDEX_ASSIGN: compile_index_assign(stmt); break;
        case StmtKind::PRINT:        compile_print(stmt); break;
        case StmtKind::INPUT:        compile_input(stmt); break;
        case StmtKind::GOTO:         compile_goto(stmt); break;
        case StmtKind::LABEL:        compile_label(stmt); break;
        case StmtKind::IF:           compile_if(stmt); break;
        case StmtKind::DO_LOOP:      compile_do_loop(stmt); break;
        case StmtKind::FOR_LOOP:     compile_for(stmt); break;
        case StmtKind::RETURN:       compile_return(stmt); break;
        case StmtKind::SUB:          compile_sub(stmt); break;
        case StmtKind::FUNCTION:     compile_function(stmt); break;
        case StmtKind::EXPR_STMT:    compile_expr_stmt(stmt); break;
        case StmtKind::DESTRUCTURE:  compile_destructure(stmt); break;
        case StmtKind::REACT_ASSIGN: {
            // 1. Compile formula as a named function __REACT_VARNAME
            static int react_counter = 0;
            std::string fname = "__REACT_" + stmt.var_name + "_" + std::to_string(react_counter++);
            FuncProto proto;
            proto.name = fname;
            proto.arity = 0;
            proto.is_sub = false;
            scopes.push_back(CompilerScope{});
            current_scope().is_function = true;
            compile_expr(*stmt.expr);
            current_chunk().emit(OpCode::RETURN_VAL, stmt.line);
            proto.chunk = std::move(current_chunk());
            scopes.pop_back();
            funcs.push_back(std::move(proto));

            // 2. Extract dependencies: walk expression for VARIABLE nodes
            std::vector<std::string> deps;
            std::function<void(const Expr&)> collect_deps = [&](const Expr& e) {
                if (e.kind == ExprKind::VARIABLE) {
                    if (std::find(deps.begin(), deps.end(), e.str_val) == deps.end())
                        deps.push_back(e.str_val);
                }
                if (e.left) collect_deps(*e.left);
                if (e.right) collect_deps(*e.right);
                for (auto& a : e.args) collect_deps(*a);
            };
            collect_deps(*stmt.expr);

            // 3. Emit: initial evaluation + REACT_BIND call
            // First evaluate the expression for the initial value
            compile_expr(*stmt.expr);
            uint16_t slot = resolve_var(stmt.var_name);
            bool is_global = (scopes.size() <= 1);
            current_chunk().emit(is_global ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, stmt.line);
            current_chunk().emit_u16(slot, stmt.line);

            // Call REACT_BIND(var_name, formula, func_name, [deps])
            emit_constant(Value::make_string(stmt.var_name), stmt.line);
            emit_constant(Value::make_string(stmt.label), stmt.line);  // formula
            emit_constant(Value::make_string(fname), stmt.line);       // func name
            // Build deps array
            for (auto& d : deps) emit_constant(Value::make_string(d), stmt.line);
            current_chunk().emit(OpCode::MAKE_ARRAY, stmt.line);
            current_chunk().emit_u16(static_cast<uint16_t>(deps.size()), stmt.line);
            // Call REACT_BIND with 4 args
            uint16_t fi = current_chunk().add_constant(Value::make_string("REACT_BIND"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(4, stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::TYPE_DECL: {
            // Register type name
            user_types.insert(stmt.func_name);

            // Discover INIT / DISPOSE before compiling methods so that the
            // ctor-call lookup in compile_dim sees them even when the type's
            // first DIM appears textually before the type body finishes.
            // Method names were rewritten by the parser to TypeName.METHOD.
            for (auto& method : stmt.body) {
                if (!method) continue;
                if (method->func_name == stmt.func_name + ".INIT") {
                    type_inits.insert(method->func_name);
                    if (method->params.size() <= 1)
                        type_init_zero_arg.insert(method->func_name);
                } else if (method->func_name == stmt.func_name + ".DISPOSE") {
                    // DISPOSE must take only THIS - no user parameters.
                    // Parser already prepended THIS, so params.size() must be 1.
                    if (method->params.size() != 1) {
                        throw std::runtime_error("Line " + std::to_string(method->line) +
                            ": SUB DISPOSE must not take parameters");
                    }
                    type_disposes.insert(method->func_name);
                }
            }

            // Compile methods (they're stored in body as SUB/FUNCTION stmts)
            for (auto& method : stmt.body) compile_stmt(*method);

            // Create constructor function: TYPENAME.__NEW__
            FuncProto ctor;
            ctor.name = stmt.func_name + ".__NEW__";
            ctor.arity = 0;
            ctor.is_sub = false;
            Chunk& cc = ctor.chunk;

            // Create new object
            cc.emit(OpCode::MAKE_MAP, 0); cc.emit_u16(0, 0); // empty object

            // Set __TYPE__ field
            uint16_t type_str = cc.add_constant(Value::make_string(stmt.func_name));
            cc.emit(OpCode::DUP, 0);
            uint16_t type_key = cc.add_constant(Value::make_string("__TYPE__"));
            cc.emit(OpCode::LOAD_CONST, 0); cc.emit_u16(type_key, 0);
            cc.emit(OpCode::LOAD_CONST, 0); cc.emit_u16(type_str, 0);
            cc.emit(OpCode::INDEX_SET, 0);

            // Set default values for each member
            for (auto& mem : stmt.type_members) {
                cc.emit(OpCode::DUP, 0); // keep object on stack
                uint16_t key_idx = cc.add_constant(Value::make_string(mem.name));
                cc.emit(OpCode::LOAD_CONST, 0); cc.emit_u16(key_idx, 0);
                // Default value based on type
                Value def;
                switch (mem.type) {
                    case VarType::STRING:  def = Value::make_string(""); break;
                    case VarType::BOOLEAN: def = Value::make_bool(false); break;
                    case VarType::OBJECT:  def = Value::make_object(); break;
                    case VarType::ARRAY:   def = Value::make_array(); break;
                    case VarType::ANY:     def = Value::make_array(); break;
                    default: def = Value::make_i64(0); break;
                }
                uint16_t val_idx = cc.add_constant(std::move(def));
                cc.emit(OpCode::LOAD_CONST, 0); cc.emit_u16(val_idx, 0);
                cc.emit(OpCode::INDEX_SET, 0);
            }

            // Return the object
            cc.emit(OpCode::RETURN_VAL, 0);
            funcs.push_back(std::move(ctor));
            break;
        }
        case StmtKind::CLS_STMT: {
            for (auto& e : stmt.print_exprs) compile_expr(*e);
            uint16_t fi = current_chunk().add_constant(Value::make_string("CLS"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(static_cast<uint8_t>(stmt.print_exprs.size()), stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::SLEEP_STMT: {
            compile_expr(*stmt.expr);
            uint16_t fi = current_chunk().add_constant(Value::make_string("SLEEP"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(1, stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::LOCATE_STMT: {
            for (auto& e : stmt.print_exprs) compile_expr(*e);
            uint16_t fi = current_chunk().add_constant(Value::make_string("LOCATE"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(2, stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::COLOR_STMT: {
            for (auto& e : stmt.print_exprs) compile_expr(*e);
            uint16_t fi = current_chunk().add_constant(Value::make_string("COLOR"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(2, stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::CURSOR_STMT: {
            compile_expr(*stmt.expr);
            uint16_t fi = current_chunk().add_constant(Value::make_string("CURSOR"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(1, stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::OPTION_STMT: {
            compile_expr(*stmt.expr);
            uint16_t fi = current_chunk().add_constant(Value::make_string("OPTION"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(fi, stmt.line);
            current_chunk().emit_u8(1, stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::STOP_STMT:
            current_chunk().emit(OpCode::STOP_OP, stmt.line);
            break;
        case StmtKind::END_STMT:
            current_chunk().emit(OpCode::END_PROGRAM, stmt.line);
            break;
        case StmtKind::EXIT_LOOP: {
            if (stmt.is_while) {
                // EXITFUNC → RETURN NONE
                current_chunk().emit(OpCode::RETURN_VOID, stmt.line);
            } else if (!loop_stack.empty()) {
                size_t jmp = emit_jump(OpCode::JUMP, stmt.line);
                loop_stack.back().break_patches.push_back(jmp);
            }
            break;
        }
        case StmtKind::CONTINUE_LOOP: {
            if (!loop_stack.empty()) {
                if (loop_stack.back().is_for) {
                    // FOR loops: continue_addr not yet known, use patch
                    size_t jmp = emit_jump(OpCode::JUMP, stmt.line);
                    loop_stack.back().continue_patches.push_back(jmp);
                } else {
                    // DO loops: continue_addr = loop_start (already known)
                    current_chunk().emit(OpCode::JUMP, stmt.line);
                    size_t addr = current_chunk().code.size();
                    int16_t offset = static_cast<int16_t>(loop_stack.back().continue_addr - addr - 2);
                    current_chunk().emit_i16(offset, stmt.line);
                }
            }
            break;
        }
        case StmtKind::FOR_EACH: {
            // Evaluate the iterable once, stash in a hidden temp slot.
            // The FOREACH_NEXT opcode advances per-iteration: pops state +
            // iter from the stack, on continue pushes (new_state, value)
            // - and dispatches based on iter type so ARRAY (index walk),
            // STRING (char walk), and channel handle (CHAN.RECV-until-EOF)
            // all share one loop body.
            compile_expr(*stmt.expr);
            std::string iter_tmp  = "__FOREACH_ITER_"  + std::to_string(stmt.line);
            std::string state_tmp = "__FOREACH_STATE_" + std::to_string(stmt.line);
            uint16_t iter_slot  = resolve_var(iter_tmp);
            uint16_t state_slot = resolve_var(state_tmp);
            bool is_global = should_use_global(iter_tmp);
            auto emit_store_temp = [&](uint16_t slot) {
                current_chunk().emit(is_global ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, stmt.line);
                current_chunk().emit_u16(slot, stmt.line);
            };
            auto emit_load_temp = [&](uint16_t slot) {
                current_chunk().emit(is_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, stmt.line);
                current_chunk().emit_u16(slot, stmt.line);
            };

            emit_store_temp(iter_slot);
            emit_constant(Value::make_i64(0), stmt.line);
            emit_store_temp(state_slot);

            { LoopCtx lc; lc.is_for = true; loop_stack.push_back(std::move(lc)); }
            size_t loop_start = current_chunk().code.size();

            // Push iter, state; opcode pops both. On exit it jumps to
            // exit_patch; on continue it pushes (new_state, value).
            emit_load_temp(iter_slot);
            emit_load_temp(state_slot);
            current_chunk().emit(OpCode::FOREACH_NEXT, stmt.line);
            size_t exit_patch = current_chunk().code.size();
            current_chunk().emit_i16(0, stmt.line); // placeholder

            // Stack now: [..., new_state, value]. Pop value into the
            // user's loop variable, pop new_state back to the temp slot.
            emit_var_store(stmt.var_name, stmt.line, /*prefer_local=*/false);
            emit_store_temp(state_slot);

            // Body
            for (auto& s : stmt.body) compile_stmt(*s);

            // Continue jumps land here; loop back to loop_start.
            for (size_t cp : loop_stack.back().continue_patches) patch_jump(cp);
            current_chunk().emit(OpCode::JUMP, stmt.line);
            size_t back_addr = current_chunk().code.size();
            current_chunk().emit_i16(static_cast<int16_t>(loop_start - back_addr - 2), stmt.line);

            // Patch FOREACH_NEXT exit offset to here.
            int16_t fwd_off = static_cast<int16_t>(
                current_chunk().code.size() - (exit_patch + 2));
            current_chunk().patch_i16(exit_patch, fwd_off);

            // Break patches land at exit too.
            for (size_t bp : loop_stack.back().break_patches) patch_jump(bp);
            loop_stack.pop_back();
            break;
        }
        case StmtKind::THROW_STMT: {
            if (stmt.expr) {
                compile_expr(*stmt.expr);
            } else {
                emit_constant(Value::make_string("User error"), stmt.line);
            }
            current_chunk().emit(OpCode::THROW_OP, stmt.line);
            break;
        }
        case StmtKind::TRY_CATCH: {
            // SETUP_TRY catch_offset
            current_chunk().emit(OpCode::SETUP_TRY, stmt.line);
            size_t catch_addr = current_chunk().code.size();
            current_chunk().emit_u16(0, stmt.line); // placeholder

            // TRY body
            for (auto& s : stmt.body) compile_stmt(*s);

            // POP_TRY (no error occurred)
            current_chunk().emit(OpCode::POP_TRY, stmt.line);

            // Jump over CATCH to FINALLY
            size_t jump_to_finally = emit_jump(OpCode::JUMP, stmt.line);

            // Patch CATCH address
            size_t catch_target = current_chunk().code.size();
            current_chunk().patch_u16(catch_addr,
                static_cast<uint16_t>(catch_target - catch_addr - 2));

            // CATCH body
            for (auto& s : stmt.catch_body) compile_stmt(*s);

            // FINALLY label (reached from both paths)
            patch_jump(jump_to_finally);

            // FINALLY body
            for (auto& s : stmt.finally_body) compile_stmt(*s);
            break;
        }
        case StmtKind::SWITCH_STMT: {
            // Evaluate switch expression once, keep on stack
            compile_expr(*stmt.expr);

            std::vector<size_t> end_jumps;

            for (auto& branch : stmt.branches) {
                bool is_default = branch.case_labels.empty() && !branch.condition;
                if (is_default) {
                    // DEFAULT: just execute body
                    for (auto& s : branch.body) compile_stmt(*s);
                    continue;
                }

                // Multi-case: any label match → run body. Each label is
                // either a single value (low only) or a "low TO high" range.
                std::vector<size_t> match_jumps;

                // Legacy single-value case (parser now always populates
                // case_labels, but keep this for any AST consumer that
                // still sets condition).
                if (branch.case_labels.empty() && branch.condition) {
                    current_chunk().emit(OpCode::DUP, stmt.line);
                    compile_expr(*branch.condition);
                    current_chunk().emit(OpCode::CMP_EQ, stmt.line);
                    match_jumps.push_back(emit_jump(OpCode::JUMP_IF_TRUE, stmt.line));
                } else {
                    for (auto& [low, high] : branch.case_labels) {
                        if (high) {
                            // Range: switch_val >= low AND switch_val <= high.
                            // Two-step test reuses CMP_GE/CMP_LE without
                            // needing a temporary; a failed lower bound
                            // skips past the upper-bound test.
                            current_chunk().emit(OpCode::DUP, stmt.line);
                            compile_expr(*low);
                            current_chunk().emit(OpCode::CMP_GE, stmt.line);
                            size_t skip_low_fail = emit_jump(OpCode::JUMP_IF_FALSE, stmt.line);

                            current_chunk().emit(OpCode::DUP, stmt.line);
                            compile_expr(*high);
                            current_chunk().emit(OpCode::CMP_LE, stmt.line);
                            match_jumps.push_back(emit_jump(OpCode::JUMP_IF_TRUE, stmt.line));

                            patch_jump(skip_low_fail);
                        } else {
                            current_chunk().emit(OpCode::DUP, stmt.line);
                            compile_expr(*low);
                            current_chunk().emit(OpCode::CMP_EQ, stmt.line);
                            match_jumps.push_back(emit_jump(OpCode::JUMP_IF_TRUE, stmt.line));
                        }
                    }
                }

                // No label matched → skip the body.
                size_t skip_body = emit_jump(OpCode::JUMP, stmt.line);

                // Body entry: patch every match-jump here.
                for (size_t j : match_jumps) patch_jump(j);
                for (auto& s : branch.body) compile_stmt(*s);
                end_jumps.push_back(emit_jump(OpCode::JUMP, stmt.line));

                patch_jump(skip_body);
            }

            // Patch all end jumps to here
            for (size_t addr : end_jumps) patch_jump(addr);

            // Pop the switch value
            current_chunk().emit(OpCode::POP, stmt.line);
            break;
        }
        case StmtKind::ENUM_DECL: {
            // ENUM creates global constants: ENUMNAME.MEMBER = value
            for (auto& [member, val] : stmt.enum_members) {
                std::string full_name = stmt.func_name + "." + member;
                emit_constant(Value::make_i64(val), stmt.line);
                uint16_t slot = resolve_var(full_name);
                current_chunk().emit(OpCode::STORE_GLOBAL, stmt.line);
                current_chunk().emit_u16(slot, stmt.line);
            }
            break;
        }
    }
}

void Compiler::compile_let(const Stmt& stmt) {
    compile_expr(*stmt.expr);
    if (stmt.var_type != VarType::NONE) {
        current_chunk().emit(OpCode::CAST, stmt.line);
        current_chunk().emit_u8(vartype_to_valuetype_byte(stmt.var_type), stmt.line);
    }
    // LET always declares a LOCAL variable in function scope
    if (scopes.size() > 1 && !stmt.is_const) {
        uint16_t slot = resolve_local(stmt.var_name);
        current_chunk().emit(OpCode::STORE_VAR, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
    } else {
        uint16_t slot = resolve_var(stmt.var_name);
        current_chunk().emit(OpCode::STORE_GLOBAL, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
        if (stmt.is_const) {
            current_chunk().emit(OpCode::MARK_CONST, stmt.line);
            current_chunk().emit_u16(slot, stmt.line);
        }
    }
}

void Compiler::compile_dim(const Stmt& stmt) {
    // ── STATIC DIM <name> [AS T] [= init] ─────────────────────────
    // Function-scoped persistent slot. Init runs once on the first
    // execution of THIS line; subsequent executions skip past it via
    // MAYBE_INIT_STATIC's guard. Reads/writes elsewhere in the function
    // route through LOAD_STATIC / STORE_STATIC (see emit_var_load /
    // emit_var_store).
    if (stmt.is_static) {
        if (scopes.size() <= 1) {
            throw std::runtime_error("Line " + std::to_string(stmt.line) +
                ": STATIC DIM is only allowed inside a FUNC or SUB");
        }
        if (current_scope().params.count(stmt.var_name)) {
            throw std::runtime_error("Line " + std::to_string(stmt.line) +
                ": STATIC DIM '" + stmt.var_name + "' shadows function parameter");
        }
        if (current_scope().statics.count(stmt.var_name)) {
            throw std::runtime_error("Line " + std::to_string(stmt.line) +
                ": STATIC DIM '" + stmt.var_name + "' redeclared");
        }
        // Allocate the persistent slot. The default value is NONE; the
        // first-call init below stores the user-provided initializer.
        uint16_t slot = static_cast<uint16_t>(current_chunk().static_values.size());
        current_chunk().static_values.push_back(Value::make_none());
        current_chunk().static_inited.push_back(0);
        current_scope().statics[stmt.var_name] = slot;

        // MAYBE_INIT_STATIC <slot> <i16 skip_offset>: if the guard is set
        // jump past the init block; else set the guard FIRST (recursion
        // safe) and fall through.
        current_chunk().emit(OpCode::MAYBE_INIT_STATIC, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
        size_t skip_patch = current_chunk().code.size();
        current_chunk().emit_i16(0, stmt.line);

        // Init expression (or default-zero / empty value if absent).
        if (stmt.expr) {
            compile_expr(*stmt.expr);
        } else if (!stmt.label.empty() && user_types.count(stmt.label)) {
            uint16_t ctor_idx = current_chunk().add_constant(
                Value::make_string(stmt.label + ".__NEW__"));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(ctor_idx, stmt.line);
            current_chunk().emit_u8(0, stmt.line);
            // (ctor args / INIT auto-call kept in non-static path; static
            //  UDTs with constructor args are deferred to a follow-up.)
        } else {
            // Emit MAKE_ARRAY / MAKE_MAP at runtime so each DIM call gets
            // a fresh backing buffer instead of pooling one shared instance.
            switch (stmt.var_type) {
                case VarType::ARRAY:
                case VarType::ANY:
                    current_chunk().emit(OpCode::MAKE_ARRAY, stmt.line);
                    current_chunk().emit_u16(0, stmt.line);
                    break;
                case VarType::OBJECT:
                    current_chunk().emit(OpCode::MAKE_MAP, stmt.line);
                    current_chunk().emit_u16(0, stmt.line);
                    break;
                case VarType::STRING:
                    emit_constant(Value::make_string(""), stmt.line);
                    break;
                default:
                    emit_constant(Value::make_i64(0), stmt.line);
                    break;
            }
        }
        if (stmt.var_type != VarType::NONE && stmt.var_type != VarType::ARRAY &&
            stmt.var_type != VarType::ANY && stmt.var_type != VarType::OBJECT) {
            current_chunk().emit(OpCode::CAST, stmt.line);
            current_chunk().emit_u8(vartype_to_valuetype_byte(stmt.var_type), stmt.line);
        }
        current_chunk().emit(OpCode::STORE_STATIC, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);

        // Patch the skip offset: jump from (skip_patch + 2) to here.
        size_t target = current_chunk().code.size();
        int16_t off = static_cast<int16_t>(target - (skip_patch + 2));
        current_chunk().patch_i16(skip_patch, off);
        return;
    }

    if (stmt.expr) {
        compile_expr(*stmt.expr);
    } else if (!stmt.label.empty() && user_types.count(stmt.label)) {
        // DIM x AS UserType [(args)] → call __NEW__ then optional INIT
        uint16_t ctor_idx = current_chunk().add_constant(
            Value::make_string(stmt.label + ".__NEW__"));
        current_chunk().emit(OpCode::CALL, stmt.line);
        current_chunk().emit_u16(ctor_idx, stmt.line);
        current_chunk().emit_u8(0, stmt.line); // 0 args
        // Optional user-defined INIT(self, ...). Two trigger rules so we
        // stay compatible with pre-existing code that DIMs first and calls
        // obj.INIT(args) manually (type_id.jdb / RPG_ENGINE pattern):
        //   * If the user passed ctor_args, INIT MUST exist - call it.
        //   * If no ctor_args were passed, only auto-call INIT when it
        //     declares no user parameters (i.e. SUB INIT()). Otherwise
        //     leave the call to the user.
        std::string init_name = stmt.label + ".INIT";
        bool init_known = type_inits.count(init_name) > 0;
        bool emit_init = false;
        if (!stmt.ctor_args.empty()) {
            if (!init_known) {
                throw std::runtime_error("Line " + std::to_string(stmt.line) +
                    ": type '" + stmt.label +
                    "' has no SUB INIT, cannot pass constructor arguments");
            }
            emit_init = true;
        } else if (init_known && type_init_zero_arg.count(init_name)) {
            emit_init = true;
        }
        if (emit_init) {
            current_chunk().emit(OpCode::DUP, stmt.line);
            for (auto& a : stmt.ctor_args) compile_expr(*a);
            uint16_t init_idx = current_chunk().add_constant(Value::make_string(init_name));
            current_chunk().emit(OpCode::CALL, stmt.line);
            current_chunk().emit_u16(init_idx, stmt.line);
            current_chunk().emit_u8(static_cast<uint8_t>(stmt.ctor_args.size() + 1), stmt.line);
            current_chunk().emit(OpCode::POP, stmt.line);
        }
    } else {
        // Same as the STATIC branch above: emit a fresh container per call.
        switch (stmt.var_type) {
            case VarType::ARRAY:
            case VarType::ANY:
                current_chunk().emit(OpCode::MAKE_ARRAY, stmt.line);
                current_chunk().emit_u16(0, stmt.line);
                break;
            case VarType::OBJECT:
                current_chunk().emit(OpCode::MAKE_MAP, stmt.line);
                current_chunk().emit_u16(0, stmt.line);
                break;
            case VarType::STRING:
                emit_constant(Value::make_string(""), stmt.line);
                break;
            default:
                emit_constant(Value::make_i64(0), stmt.line);
                break;
        }
    }
    if (stmt.var_type != VarType::NONE && stmt.var_type != VarType::ARRAY &&
        stmt.var_type != VarType::ANY && stmt.var_type != VarType::OBJECT) {
        current_chunk().emit(OpCode::CAST, stmt.line);
        current_chunk().emit_u8(vartype_to_valuetype_byte(stmt.var_type), stmt.line);
    }
    // DIM always declares a LOCAL variable in function scope
    if (scopes.size() > 1) {
        // Reject DIM-shadows-parameter: BASIC is case-insensitive, so
        // `DIM v` inside `FUNC F(V)` aliases V and silently overwrites it
        // when assigned. Caught us twice in the 4d benches.
        if (current_scope().params.count(stmt.var_name)) {
            throw std::runtime_error("Line " + std::to_string(stmt.line) +
                ": DIM '" + stmt.var_name + "' shadows function parameter '" +
                stmt.var_name + "' (BASIC identifiers are case-insensitive). " +
                "Rename the local; assigning to it would silently overwrite the parameter.");
        }
        uint16_t slot = resolve_local(stmt.var_name);
        current_chunk().emit(OpCode::STORE_VAR, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
    } else {
        uint16_t slot = resolve_var(stmt.var_name);
        current_chunk().emit(OpCode::STORE_GLOBAL, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
    }
}

void Compiler::compile_assign(const Stmt& stmt) {
    compile_expr(*stmt.expr);
    emit_var_store(stmt.var_name, stmt.line, /*prefer_local=*/false);
}

void Compiler::compile_index_assign(const Stmt& stmt) {
    // Expression-based LHS: expr.field = val or expr[idx] = val
    if (!stmt.print_exprs.empty()) {
        if (stmt.label == "__INDEX__" && stmt.print_exprs.size() >= 2) {
            // expr[idx] = val
            compile_expr(*stmt.print_exprs[0]); // container expression
            compile_expr(*stmt.print_exprs[1]); // index expression
            compile_expr(*stmt.expr);
            current_chunk().emit(OpCode::INDEX_SET, stmt.line);
        } else {
            // expr.field = val (label = field name)
            compile_expr(*stmt.print_exprs[0]); // object expression
            emit_constant(Value::make_string(stmt.label), stmt.line); // field as string key
            compile_expr(*stmt.expr);
            current_chunk().emit(OpCode::INDEX_SET, stmt.line);
        }
        return;
    }

    // Variable-based LHS: var[i1][i2]...[iN] = val
    bool is_global = should_use_global(stmt.var_name);
    if (is_static_name(stmt.var_name)) {
        current_chunk().emit(OpCode::LOAD_STATIC, stmt.line);
        current_chunk().emit_u16(lookup_static_slot(scopes, stmt.var_name), stmt.line);
    } else {
        uint16_t slot = resolve_var(stmt.var_name);
        current_chunk().emit(is_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
    }

    // For multi-dimensional assignment (size >= 2), emit a MULTI_INDEX_SET.
    // This lets the runtime do fancy (vectorized) indexing when any index is
    // an array - the plain nested INDEX_GET/INDEX_SET path only handles
    // scalar walks through intermediate rows.
    if (stmt.index_chain.size() >= 2) {
        for (auto& idx : stmt.index_chain) compile_expr(*idx);
        compile_expr(*stmt.expr);
        current_chunk().emit(OpCode::MULTI_INDEX_SET, stmt.line);
        current_chunk().emit_u8((uint8_t)stmt.index_chain.size(), stmt.line);
        return;
    }

    for (size_t i = 0; i + 1 < stmt.index_chain.size(); i++) {
        compile_expr(*stmt.index_chain[i]);
        current_chunk().emit(OpCode::INDEX_GET, stmt.line);
    }

    compile_expr(*stmt.index_chain.back());
    compile_expr(*stmt.expr);
    current_chunk().emit(OpCode::INDEX_SET, stmt.line);
}

void Compiler::compile_print(const Stmt& stmt) {
    for (size_t i = 0; i < stmt.print_exprs.size(); i++) {
        // Emit space before this item if preceded by comma
        if (i < stmt.print_seps.size() && stmt.print_seps[i] == 1) {
            current_chunk().emit(OpCode::PRINT_SPACE, stmt.line);
        }
        compile_expr(*stmt.print_exprs[i]);
        current_chunk().emit(OpCode::PRINT, stmt.line);
    }
    if (stmt.print_newline) {
        current_chunk().emit(OpCode::PRINT_NL, stmt.line);
    }
}

void Compiler::compile_input(const Stmt& stmt) {
    // Prompt: either an expression (set by the parser) or default "? ".
    if (stmt.expr) {
        compile_expr(*stmt.expr);
        current_chunk().emit(OpCode::PRINT, stmt.line);
        if (stmt.print_newline) {
            // Comma separator: append "? " after the prompt expression.
            emit_constant(Value::make_string("? "), stmt.line);
            current_chunk().emit(OpCode::PRINT, stmt.line);
        }
    } else {
        // No prompt at all: classic BASIC default.
        emit_constant(Value::make_string("? "), stmt.line);
        current_chunk().emit(OpCode::PRINT, stmt.line);
    }
    uint16_t slot = resolve_var(stmt.var_name);
    current_chunk().emit(OpCode::INPUT_VAR, stmt.line);
    current_chunk().emit_u16(slot, stmt.line);
}

void Compiler::compile_goto(const Stmt& stmt) {
    current_chunk().emit(OpCode::JUMP_ABS, stmt.line);
    size_t addr = current_chunk().code.size();
    current_chunk().emit_u16(0, stmt.line); // placeholder
    unresolved_gotos.push_back({stmt.label, addr});
}

void Compiler::compile_label(const Stmt& stmt) {
    label_positions[stmt.label] = current_chunk().code.size();
}

void Compiler::compile_if(const Stmt& stmt) {
    std::vector<size_t> end_jumps;

    for (size_t i = 0; i < stmt.branches.size(); i++) {
        auto& branch = stmt.branches[i];

        if (branch.condition) {
            compile_expr(*branch.condition);
            size_t false_jump = emit_jump(OpCode::JUMP_IF_FALSE, stmt.line);

            for (auto& s : branch.body) compile_stmt(*s);
            end_jumps.push_back(emit_jump(OpCode::JUMP, stmt.line));

            patch_jump(false_jump);
        } else {
            // ELSE branch (no condition)
            for (auto& s : branch.body) compile_stmt(*s);
        }
    }

    for (size_t addr : end_jumps) patch_jump(addr);
}

void Compiler::compile_do_loop(const Stmt& stmt) {
    size_t loop_start = current_chunk().code.size();
    { LoopCtx lc; lc.is_for = false; loop_stack.push_back(std::move(lc)); }
    loop_stack.back().continue_addr = loop_start;

    if (stmt.cond_at_top && stmt.loop_cond) {
        // DO WHILE/UNTIL ... LOOP
        compile_expr(*stmt.loop_cond);
        size_t exit_jump;
        if (stmt.is_while) {
            exit_jump = emit_jump(OpCode::JUMP_IF_FALSE, stmt.line);
        } else {
            exit_jump = emit_jump(OpCode::JUMP_IF_TRUE, stmt.line);
        }

        for (auto& s : stmt.body) compile_stmt(*s);

        // Jump back to start
        current_chunk().emit(OpCode::JUMP, stmt.line);
        size_t back_addr = current_chunk().code.size();
        int16_t back_offset = static_cast<int16_t>(loop_start - back_addr - 2);
        current_chunk().emit_i16(back_offset, stmt.line);

        patch_jump(exit_jump);
    } else {
        // DO ... LOOP WHILE/UNTIL
        for (auto& s : stmt.body) compile_stmt(*s);

        if (stmt.loop_cond) {
            compile_expr(*stmt.loop_cond);
            if (stmt.is_while) {
                // LOOP WHILE: jump back if true
                current_chunk().emit(OpCode::JUMP_IF_TRUE, stmt.line);
            } else {
                // LOOP UNTIL: jump back if false
                current_chunk().emit(OpCode::JUMP_IF_FALSE, stmt.line);
            }
            size_t back_addr = current_chunk().code.size();
            int16_t back_offset = static_cast<int16_t>(loop_start - back_addr - 2);
            current_chunk().emit_i16(back_offset, stmt.line);
        } else {
            // Infinite loop: DO ... LOOP
            current_chunk().emit(OpCode::JUMP, stmt.line);
            size_t back_addr = current_chunk().code.size();
            int16_t back_offset = static_cast<int16_t>(loop_start - back_addr - 2);
            current_chunk().emit_i16(back_offset, stmt.line);
        }
    }
    for (size_t bp : loop_stack.back().break_patches) patch_jump(bp);
    loop_stack.pop_back();
}

void Compiler::compile_for(const Stmt& stmt) {
    // FOR var = start TO end [STEP step]
    //   body
    // NEXT

    // 1. Initialize: var = start
    compile_expr(*stmt.expr);
    // Inside a function, the loop variable is always local - force-register
    // it so the inner FOR doesn't clobber an outer global with the same name.
    if (scopes.size() > 1 && scopes.back().locals.count(stmt.var_name) == 0) {
        resolve_local(stmt.var_name);
    }
    bool is_global = should_use_global(stmt.var_name);
    uint16_t slot = resolve_var(stmt.var_name);
    current_chunk().emit(is_global ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, stmt.line);
    current_chunk().emit_u16(slot, stmt.line);

    // 2. Loop start: check condition
    size_t loop_start = current_chunk().code.size();

    // Load var
    current_chunk().emit(is_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, stmt.line);
    current_chunk().emit_u16(slot, stmt.line);
    // Load end
    compile_expr(*stmt.end_expr);

    // Compare: if step is negative use >=, otherwise <=
    if (stmt.step_expr) {
        // Dynamic: we don't know sign at compile time, use <=  for now
        // For negative step the user should use STEP -1 and we check >=
        // Simple heuristic: check if step is a negative literal
        bool negative_step = false;
        if (stmt.step_expr->kind == ExprKind::UNARY && stmt.step_expr->op == TokenType::MINUS) {
            negative_step = true;
        } else if (stmt.step_expr->kind == ExprKind::LITERAL_INT && stmt.step_expr->int_val < 0) {
            negative_step = true;
        } else if (stmt.step_expr->kind == ExprKind::LITERAL_FLOAT && stmt.step_expr->float_val < 0) {
            negative_step = true;
        }
        current_chunk().emit(negative_step ? OpCode::CMP_GE : OpCode::CMP_LE, stmt.line);
    } else {
        current_chunk().emit(OpCode::CMP_LE, stmt.line);
    }

    size_t exit_jump = emit_jump(OpCode::JUMP_IF_FALSE, stmt.line);

    // 3. Body (with loop context for EXITFOR/CONTINUEFOR)
    { LoopCtx lc; lc.is_for = true; loop_stack.push_back(std::move(lc)); }
    for (auto& s : stmt.body) compile_stmt(*s);

    // 4. Continue point + Increment: var = var + step
    // Patch all CONTINUEFOR jumps to here
    for (size_t cp : loop_stack.back().continue_patches) patch_jump(cp);
    current_chunk().emit(is_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, stmt.line);
    current_chunk().emit_u16(slot, stmt.line);
    if (stmt.step_expr) {
        compile_expr(*stmt.step_expr);
    } else {
        emit_constant(Value::make_i64(1), stmt.line);
    }
    current_chunk().emit(OpCode::ADD, stmt.line);
    current_chunk().emit(is_global ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, stmt.line);
    current_chunk().emit_u16(slot, stmt.line);

    // 5. Jump back to condition check
    current_chunk().emit(OpCode::JUMP, stmt.line);
    size_t back_addr = current_chunk().code.size();
    int16_t back_offset = static_cast<int16_t>(loop_start - back_addr - 2);
    current_chunk().emit_i16(back_offset, stmt.line);

    // 6. Patch exit + break patches
    patch_jump(exit_jump);
    for (size_t bp : loop_stack.back().break_patches) patch_jump(bp);
    loop_stack.pop_back();
}

void Compiler::compile_return(const Stmt& stmt) {
    if (stmt.expr) {
        compile_expr(*stmt.expr);
        current_chunk().emit(OpCode::RETURN_VAL, stmt.line);
    } else {
        current_chunk().emit(OpCode::RETURN_VOID, stmt.line);
    }
}

// A default has to be a literal. Anything else would need a scope to be
// evaluated in, and a function is declared before any scope exists.
static void fill_param_defaults(FuncProto& proto, const std::vector<Param>& params) {
    proto.min_arity = 0;
    proto.defaults.assign(params.size(), Value::make_none());
    bool optional_from_here = false;
    for (size_t i = 0; i < params.size(); i++) {
        const Expr* d = params[i].default_value.get();
        if (!d) {
            if (!optional_from_here) proto.min_arity = (int)i + 1;
            continue;
        }
        optional_from_here = true;
        switch (d->kind) {
            case ExprKind::LITERAL_INT:    proto.defaults[i] = Value::make_i64(d->int_val); break;
            case ExprKind::LITERAL_FLOAT:  proto.defaults[i] = Value::make_f64(d->float_val); break;
            case ExprKind::LITERAL_STRING: proto.defaults[i] = Value::make_string(d->str_val); break;
            case ExprKind::LITERAL_BOOL:   proto.defaults[i] = Value::make_bool(d->bool_val); break;
            default:
                throw std::runtime_error("Default for parameter '" + params[i].name +
                    "' must be a literal number, string, TRUE, FALSE or NONE");
        }
    }
}

void Compiler::compile_sub(const Stmt& stmt) {
    // Builtins always win at call dispatch, so a SUB with a builtin's name
    // could never be reached - reject the definition instead. Module exports
    // are exempt: IMPORT prefixes them with the module name.
    bool exported = (stmt.label == "__EXPORT__");
    if (!exported && jdb_native_slot(stmt.func_name) >= 0)
        throw std::runtime_error("Line " + std::to_string(stmt.line) +
            ": SUB " + stmt.func_name + " collides with the builtin function " +
            stmt.func_name + " - choose another name");
    FuncProto proto;
    proto.name = stmt.func_name;
    proto.arity = static_cast<int>(stmt.params.size());
    fill_param_defaults(proto, stmt.params);
    proto.is_sub = true;
    proto.is_exported = exported;
    for (auto& p : stmt.params) proto.param_names.push_back(p.name);

    // Push a new scope for the function body
    scopes.push_back(CompilerScope{});
    current_scope().is_function = true;
    // Propagate source file for debugger (module file, or inherit from main chunk)
    current_chunk().source_file = !stmt.source_file.empty()
        ? stmt.source_file : scopes[0].chunk.source_file;

    // Register parameters as local variables (always local, even if name matches a global)
    for (auto& p : stmt.params) {
        resolve_local(p.name);
        current_scope().params.insert(p.name);
    }

    for (auto& s : stmt.body) compile_stmt(*s);
    current_chunk().emit(OpCode::RETURN_VOID, stmt.line);

    // Resolve GOTO labels for THIS chunk before moving it out
    resolve_labels();

    proto.chunk = std::move(current_chunk());
    scopes.pop_back();

    funcs.push_back(std::move(proto));
}

void Compiler::compile_function(const Stmt& stmt) {
    // Builtins always win at call dispatch, so a FUNC with a builtin's name
    // could never be reached - reject the definition instead. Module exports
    // are exempt: IMPORT prefixes them with the module name.
    bool exported = (stmt.label == "__EXPORT__");
    if (!exported && jdb_native_slot(stmt.func_name) >= 0)
        throw std::runtime_error("Line " + std::to_string(stmt.line) +
            ": FUNC " + stmt.func_name + " collides with the builtin function " +
            stmt.func_name + " - choose another name");
    FuncProto proto;
    proto.name = stmt.func_name;
    proto.arity = static_cast<int>(stmt.params.size());
    fill_param_defaults(proto, stmt.params);
    proto.is_sub = false;
    proto.is_exported = exported;
    proto.is_async = stmt.is_async_func;
    for (auto& p : stmt.params) proto.param_names.push_back(p.name);

    scopes.push_back(CompilerScope{});
    current_scope().is_function = true;
    // Propagate source file for debugger (module file, or inherit from main chunk)
    current_chunk().source_file = !stmt.source_file.empty()
        ? stmt.source_file : scopes[0].chunk.source_file;

    // Register parameters as local variables (always local, even if name matches a global)
    for (auto& p : stmt.params) {
        resolve_local(p.name);
        current_scope().params.insert(p.name);
    }

    for (auto& s : stmt.body) compile_stmt(*s);

    // Implicit return NONE if no explicit return
    emit_constant(Value::make_none(), stmt.line);
    current_chunk().emit(OpCode::RETURN_VAL, stmt.line);

    // Resolve GOTO labels for THIS chunk before moving it out
    resolve_labels();

    proto.chunk = std::move(current_chunk());
    scopes.pop_back();

    funcs.push_back(std::move(proto));
}

void Compiler::compile_expr_stmt(const Stmt& stmt) {
    compile_expr(*stmt.expr);
    current_chunk().emit(OpCode::POP, stmt.line);
}

void Compiler::compile_destructure(const Stmt& stmt) {
    // Indexed / mixed targets are pre-desugared by the parser into a temp
    // LET + per-target ASSIGN/INDEX_ASSIGN statements (swap-safe). Just run
    // them in order.
    if (!stmt.body.empty()) {
        for (auto& sub : stmt.body) if (sub) compile_stmt(*sub);
        return;
    }
    // Evaluate RHS (should produce an array)
    compile_expr(*stmt.expr);

    bool is_global = (scopes.size() <= 1);

    // For each target variable: DUP array, push index, INDEX_GET, STORE
    for (size_t i = 0; i < stmt.destruct_vars.size(); i++) {
        current_chunk().emit(OpCode::DUP, stmt.line);
        emit_constant(Value::make_i64(static_cast<int64_t>(i)), stmt.line);
        current_chunk().emit(OpCode::INDEX_GET, stmt.line);
        uint16_t slot = resolve_var(stmt.destruct_vars[i]);
        current_chunk().emit(is_global ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, stmt.line);
        current_chunk().emit_u16(slot, stmt.line);
    }

    // Pop the original array
    current_chunk().emit(OpCode::POP, stmt.line);
}

// ── Expressions ──────────────────────────────────────────────

void Compiler::compile_expr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::LITERAL_INT:
            emit_constant(Value::make_i64(expr.int_val), expr.line);
            break;

        case ExprKind::LITERAL_FLOAT:
            emit_constant(Value::make_f64(expr.float_val), expr.line);
            break;

        case ExprKind::LITERAL_STRING:
            emit_constant(Value::make_string(expr.str_val), expr.line);
            break;

        case ExprKind::LITERAL_BOOL:
            emit_constant(Value::make_bool(expr.bool_val), expr.line);
            break;

        case ExprKind::VARIABLE: {
            emit_var_load(expr.str_val, expr.line);
            break;
        }

        case ExprKind::BINARY:
            compile_binary(expr);
            break;

        case ExprKind::UNARY:
            compile_unary(expr);
            break;

        case ExprKind::CALL:
            compile_call(expr);
            break;

        case ExprKind::INDEX:
            compile_expr(*expr.left);
            if (expr.optional) {
                // Written with ?[ or ?{ : if there is nothing to read into,
                // the read does not happen and the absence is the answer,
                // which is what lets a chain stop instead of faulting.
                current_chunk().emit(OpCode::DUP, expr.line);
                size_t go = emit_jump(OpCode::JUMP_IF_NOT_NONE, expr.line);
                size_t done = emit_jump(OpCode::JUMP, expr.line);
                patch_jump(go);
                compile_expr(*expr.right);
                current_chunk().emit(OpCode::INDEX_GET, expr.line);
                patch_jump(done);
                break;
            }
            compile_expr(*expr.right);
            current_chunk().emit(OpCode::INDEX_GET, expr.line);
            break;

        case ExprKind::ARRAY_LITERAL:
            for (auto& elem : expr.args) {
                compile_expr(*elem);
            }
            current_chunk().emit(OpCode::MAKE_ARRAY, expr.line);
            current_chunk().emit_u16(static_cast<uint16_t>(expr.args.size()), expr.line);
            break;

        case ExprKind::MAP_LITERAL:
            // Push key, value pairs then MAKE_MAP
            for (size_t i = 0; i < expr.map_keys.size(); i++) {
                emit_constant(Value::make_string(expr.map_keys[i]), expr.line);
                compile_expr(*expr.args[i]);
            }
            current_chunk().emit(OpCode::MAKE_MAP, expr.line);
            current_chunk().emit_u16(static_cast<uint16_t>(expr.map_keys.size()), expr.line);
            break;

        case ExprKind::MEMBER_ACCESS:
            compile_expr(*expr.left);
            {
                uint16_t name_idx = current_chunk().add_constant(
                    Value::make_string(expr.str_val));
                if (expr.optional) {
                    current_chunk().emit(OpCode::DUP, expr.line);
                    size_t go = emit_jump(OpCode::JUMP_IF_NOT_NONE, expr.line);
                    size_t done = emit_jump(OpCode::JUMP, expr.line);
                    patch_jump(go);
                    current_chunk().emit(OpCode::GET_FIELD, expr.line);
                    current_chunk().emit_u16(name_idx, expr.line);
                    patch_jump(done);
                    break;
                }
                current_chunk().emit(OpCode::GET_FIELD, expr.line);
                current_chunk().emit_u16(name_idx, expr.line);
            }
            break;

        case ExprKind::PLACEHOLDER_EXPR:
            // Load pipe temp variable
            {
                uint16_t slot = resolve_var("__PIPE_TMP__");
                bool is_global = (scopes.size() <= 1);
                current_chunk().emit(is_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, expr.line);
                current_chunk().emit_u16(slot, expr.line);
            }
            break;

        case ExprKind::PIPE_EXPR:
            if (expr.right->kind == ExprKind::LAMBDA_EXPR ||
                expr.right->kind == ExprKind::VARIABLE ||
                expr.right->kind == ExprKind::LITERAL_STRING) {
                // value |> func → __PIPE_APPLY(func, value)
                // VARIABLE on the RHS: if the name resolves to a user FUNC
                // or VM native, lift it to a funcref-string constant so
                // call_funcref can dispatch by name. Otherwise the VARIABLE
                // path emits LOAD_VAR for an unset slot and __PIPE_APPLY
                // gets NONE → "Invalid function reference". Falls through
                // to ordinary VARIABLE compile when the name doesn't match.
                if (expr.right->kind == ExprKind::VARIABLE) {
                    const std::string& vn = expr.right->str_val;
                    bool is_func = false;
                    for (auto& fp : funcs) if (fp.name == vn) { is_func = true; break; }
                    if (is_func) emit_constant(Value::make_string(vn), expr.line);
                    else compile_expr(*expr.right);
                } else {
                    compile_expr(*expr.right); // funcref
                }
                compile_expr(*expr.left);  // value as argument
                {
                    uint16_t fi = current_chunk().add_constant(Value::make_string("__PIPE_APPLY"));
                    current_chunk().emit(OpCode::CALL, expr.line);
                    current_chunk().emit_u16(fi, expr.line);
                    current_chunk().emit_u8(2, expr.line);
                }
            } else {
                // value |> expr(?) - placeholder-based pipe
                compile_expr(*expr.left);
                {
                    uint16_t slot = resolve_var("__PIPE_TMP__");
                    bool use_g = should_use_global("__PIPE_TMP__");
                    current_chunk().emit(use_g ? OpCode::STORE_GLOBAL : OpCode::STORE_VAR, expr.line);
                    current_chunk().emit_u16(slot, expr.line);
                }
                compile_expr(*expr.right);
            }
            break;

        case ExprKind::LAMBDA_EXPR: {
            // Generate unique function name
            static int lambda_counter = 0;
            std::string lambda_name = "__LAMBDA_" + std::to_string(lambda_counter++);

            // Build function prototype
            FuncProto proto;
            proto.name = lambda_name;
            proto.is_sub = false;

            // Parameters: captures first, then regular params
            for (auto& c : expr.lambda_captures) proto.param_names.push_back(c);
            for (auto& p : expr.lambda_params) proto.param_names.push_back(p);
            proto.arity = (int)proto.param_names.size();
            // Lambdas take no defaults: their parameter list is a bare name
            // list with no room to write one.
            proto.min_arity = proto.arity;

            // Compile body into the function's chunk
            scopes.push_back(CompilerScope{});
            current_scope().is_function = true;
            for (auto& name : proto.param_names) resolve_local(name);

            compile_expr(*expr.right); // body expression
            current_chunk().emit(OpCode::RETURN_VAL, expr.line);

            proto.chunk = std::move(current_chunk());
            scopes.pop_back();
            funcs.push_back(std::move(proto));

            // Emit the lambda value
            if (expr.lambda_captures.empty()) {
                // Simple funcref: just the name string
                emit_constant(Value::make_string(lambda_name), expr.line);
            } else {
                // Lambda with captures: [name, capture1, capture2, ...]
                emit_constant(Value::make_string(lambda_name), expr.line);
                for (auto& cap : expr.lambda_captures) {
                    uint16_t cap_slot = resolve_var(cap);
                    bool is_global = (scopes.size() <= 1);
                    current_chunk().emit(is_global ? OpCode::LOAD_GLOBAL : OpCode::LOAD_VAR, expr.line);
                    current_chunk().emit_u16(cap_slot, expr.line);
                }
                current_chunk().emit(OpCode::MAKE_ARRAY, expr.line);
                current_chunk().emit_u16(static_cast<uint16_t>(1 + expr.lambda_captures.size()), expr.line);
            }
            break;
        }
    }
}

void Compiler::compile_binary(const Expr& expr) {
    // Short-circuit: ANDALSO / ORELSE
    if (expr.op == TokenType::ANDALSO) {
        compile_expr(*expr.left);
        current_chunk().emit(OpCode::DUP, expr.line);
        size_t skip = emit_jump(OpCode::JUMP_IF_FALSE, expr.line);
        current_chunk().emit(OpCode::POP, expr.line);
        compile_expr(*expr.right);
        patch_jump(skip);
        return;
    }
    // ?? asks whether the left side is absent, not whether it is false.
    // Zero and the empty string are values and keep their place.
    if (expr.op == TokenType::COALESCE) {
        compile_expr(*expr.left);
        current_chunk().emit(OpCode::DUP, expr.line);
        size_t skip = emit_jump(OpCode::JUMP_IF_NOT_NONE, expr.line);
        current_chunk().emit(OpCode::POP, expr.line);
        compile_expr(*expr.right);
        patch_jump(skip);
        return;
    }
    if (expr.op == TokenType::ORELSE) {
        compile_expr(*expr.left);
        current_chunk().emit(OpCode::DUP, expr.line);
        size_t skip = emit_jump(OpCode::JUMP_IF_TRUE, expr.line);
        current_chunk().emit(OpCode::POP, expr.line);
        compile_expr(*expr.right);
        patch_jump(skip);
        return;
    }

    compile_expr(*expr.left);
    compile_expr(*expr.right);

    switch (expr.op) {
        case TokenType::PLUS:   current_chunk().emit(OpCode::ADD, expr.line); break;
        case TokenType::MINUS:  current_chunk().emit(OpCode::SUB, expr.line); break;
        case TokenType::STAR:   current_chunk().emit(OpCode::MUL, expr.line); break;
        case TokenType::SLASH:  current_chunk().emit(OpCode::DIV, expr.line); break;
        case TokenType::BACKSLASH: current_chunk().emit(OpCode::IDIV, expr.line); break;
        case TokenType::MOD:    current_chunk().emit(OpCode::MOD_OP, expr.line); break;
        case TokenType::CARET:  current_chunk().emit(OpCode::POW, expr.line); break;
        case TokenType::GT:     current_chunk().emit(OpCode::CMP_GT, expr.line); break;
        case TokenType::LT:     current_chunk().emit(OpCode::CMP_LT, expr.line); break;
        case TokenType::GE:     current_chunk().emit(OpCode::CMP_GE, expr.line); break;
        case TokenType::LE:     current_chunk().emit(OpCode::CMP_LE, expr.line); break;
        case TokenType::NE:     current_chunk().emit(OpCode::CMP_NE, expr.line); break;
        case TokenType::ASSIGN: current_chunk().emit(OpCode::CMP_EQ, expr.line); break; // = in expr context
        case TokenType::AND:    current_chunk().emit(OpCode::LOG_AND, expr.line); break;
        case TokenType::OR:     current_chunk().emit(OpCode::LOG_OR, expr.line); break;
        case TokenType::BAND:   current_chunk().emit(OpCode::BIT_AND, expr.line); break;
        case TokenType::BOR:    current_chunk().emit(OpCode::BIT_OR, expr.line); break;
        case TokenType::XOR:    current_chunk().emit(OpCode::BIT_XOR, expr.line); break;
        case TokenType::BXOR:   current_chunk().emit(OpCode::BIT_XOR, expr.line); break;
        case TokenType::SHL:    current_chunk().emit(OpCode::BIT_SHL, expr.line); break;
        case TokenType::SHR:    current_chunk().emit(OpCode::BIT_SHR, expr.line); break;
        case TokenType::IN:     current_chunk().emit(OpCode::OP_IN, expr.line); break;
        default:
            throw std::runtime_error("Unknown binary operator");
    }
}

void Compiler::compile_unary(const Expr& expr) {
    compile_expr(*expr.right);
    switch (expr.op) {
        case TokenType::MINUS: current_chunk().emit(OpCode::NEG, expr.line); break;
        case TokenType::NOT:   current_chunk().emit(OpCode::LOG_NOT, expr.line); break;
        case TokenType::BNOT:  current_chunk().emit(OpCode::BIT_NOT, expr.line); break;
        default:
            throw std::runtime_error("Unknown unary operator");
    }
}

void Compiler::compile_call(const Expr& expr) {
    // Method call on expression result: obj.method(args)
    if (expr.func_name == "__METHOD__" && expr.left) {
        // expr.left is MEMBER_ACCESS: left=object_expr, str_val=method_name
        // Compile: push object, push args, CALL_METHOD
        compile_expr(*expr.left->left); // the object expression
        for (auto& arg : expr.args) compile_expr(*arg);
        uint16_t name_idx = current_chunk().add_constant(
            Value::make_string(expr.left->str_val));
        current_chunk().emit(OpCode::CALL_METHOD, expr.line);
        current_chunk().emit_u16(name_idx, expr.line);
        current_chunk().emit_u8(static_cast<uint8_t>(expr.args.size()), expr.line);
        return;
    }

    // Regular function call
    for (auto& arg : expr.args) {
        compile_expr(*arg);
    }

    uint16_t func_idx = current_chunk().add_constant(Value::make_string(expr.func_name));
    current_chunk().emit(OpCode::CALL, expr.line);
    current_chunk().emit_u16(func_idx, expr.line);
    current_chunk().emit_u8(static_cast<uint8_t>(expr.args.size()), expr.line);
}
