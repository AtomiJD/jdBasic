#include "vm.h"
#include "dap.h"
#include "errors.h"
#ifdef COM
#include "com.h"
#endif
#ifdef GFX
#include "graphics.h"
#include <SDL3/SDL.h>
#endif
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <regex>
#include <unordered_set>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#include <windows.h>
#include <conio.h>
#include <eh.h>  // _set_se_translator
#endif

// ── Windows SEH → C++ exception translator ───────────────────
// Without this, an access violation, divide-by-zero, stack overflow, etc.
// inside a native function tears down the whole interpreter. With this
// (combined with /EHa) the structured exception is rethrown as a C++
// std::runtime_error which the existing CALL handler catch chain turns
// into a clean jdError that propagates through TRY/CATCH or back to the
// REPL prompt. The user sees an error message instead of a hard crash.
#if defined(_WIN32) && defined(_MSC_VER)
static const char* seh_code_name(unsigned code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
        case EXCEPTION_INT_OVERFLOW:          return "integer overflow";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "float divide by zero";
        case EXCEPTION_FLT_OVERFLOW:          return "float overflow";
        case EXCEPTION_FLT_UNDERFLOW:         return "float underflow";
        case EXCEPTION_FLT_INVALID_OPERATION: return "float invalid op";
        case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
        case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "datatype misalignment";
        case EXCEPTION_IN_PAGE_ERROR:         return "in-page I/O error";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "non-continuable";
        default:                              return "structured exception";
    }
}
static void seh_translator(unsigned code, EXCEPTION_POINTERS*) {
    throw std::runtime_error(std::string("crash: ") + seh_code_name(code));
}
struct SehInstaller {
    SehInstaller() { _set_se_translator(seh_translator); }
};
// Per-thread install. Main thread gets the translator at static-init time;
// async tasks install it themselves before running native code.
static thread_local SehInstaller g_seh_installer;
void install_seh_translator_for_this_thread() {
    _set_se_translator(seh_translator);
}
#else
void install_seh_translator_for_this_thread() {}
#endif

static auto g_program_start = std::chrono::steady_clock::now();

// ── Async task infrastructure ────────────────────────────────
#include <mutex>
#include <atomic>
#include <map>
#include "async_task.h"

std::map<int, std::shared_ptr<AsyncTask>> g_async_tasks;
std::mutex g_async_mutex;
std::atomic<int> g_async_next_id{1};

VM::VM() {
    stack.resize(65536); // pre-allocate stack — avoids resize checks on hot paths
    frames.reserve(1024); // pre-allocate frame vector
    register_builtins();
}

VM::~VM() = default;  // DebugInfo is complete here via dap.h include below

void VM::push(Value v) {
    if (sp >= stack.size()) stack.resize(stack.size() * 2);
    stack[sp++] = std::move(v);
}

Value VM::pop() {
    if (sp == 0) throw jdError(ErrCode::STACK_UNDERFLOW, "Stack underflow",
        (!frames.empty() && frame().ip > 0 && frame().ip <= frame().chunk->line_info.size())
            ? frame().chunk->line_info[frame().ip - 1] : 0);
    return std::move(stack[--sp]);
}

Value& VM::peek(size_t offset) {
    return stack[sp - 1 - offset];
}

CallFrame& VM::frame() { return frames.back(); }

uint8_t VM::read_byte() {
    return frame().chunk->code[frame().ip++];
}

uint16_t VM::read_u16() {
    uint8_t lo = read_byte();
    uint8_t hi = read_byte();
    return lo | (hi << 8);
}

int16_t VM::read_i16() {
    return static_cast<int16_t>(read_u16());
}

void VM::load(Chunk& main_chunk, std::vector<FuncProto>& funcs) {
    is_halted = false; // fresh program
    // Move compiled functions into owned storage so func_protos stays valid even
    // across nested run_code() calls (EXECUTE/EVAL/REPL).
    for (auto& f : funcs) {
        auto it = func_map.find(f.name);
        if (it != func_map.end()) {
            owned_funcs[it->second] = std::move(f);
        } else {
            func_map[f.name] = owned_funcs.size();
            owned_funcs.push_back(std::move(f));
        }
    }
    func_protos = &owned_funcs;
    func_map_generation++;

    // Initialize globals storage
    globals.resize(main_chunk.var_names.size());
    for (uint16_t i = 0; i < main_chunk.var_names.size(); i++) {
        global_names[main_chunk.var_names[i]] = i;
    }

    frames.push_back({&main_chunk, 0, 0});
}

void VM::run_code(Chunk& chunk, std::vector<FuncProto>& new_funcs) {
    // Merge new functions into owned storage
    bool funcs_changed = false;
    for (auto& f : new_funcs) {
        auto it = func_map.find(f.name);
        if (it != func_map.end()) {
            owned_funcs[it->second] = std::move(f);
        } else {
            func_map[f.name] = owned_funcs.size();
            owned_funcs.push_back(std::move(f));
        }
        funcs_changed = true;
    }
    func_protos = &owned_funcs;
    if (funcs_changed) func_map_generation++;

    // Save and reset execution state (allows nested calls from EXECUTE/EVAL)
    auto saved_frames = std::move(frames);
    auto saved_sp = sp;
    auto saved_min = min_frame_depth;
    bool saved_is_stopped = is_stopped;
    frames.clear();
    // IMPORTANT: do NOT reset sp to 0 — the existing stack may hold locals of an
    // outer user function (e.g. EVAL called from inside Calc). Start the new
    // chunk's frame ABOVE the current sp so we don't clobber outer locals.
    size_t base = saved_sp;
    size_t needed = base + chunk.var_names.size() + 64;
    while (needed >= stack.size()) stack.resize(stack.size() * 2);
    sp = base + chunk.var_names.size();
    min_frame_depth = 0;
    is_stopped = false;  // fresh sub-run; prior STOP state preserved in stopped_*
    subrun_depth++;

    // Run the new chunk
    frames.push_back({&chunk, 0, base});
    try {
        run();
    } catch (...) {
        subrun_depth--;
        // Restore state before propagating so the VM is left consistent
        frames = std::move(saved_frames);
        sp = saved_sp;
        min_frame_depth = saved_min;
        is_stopped = saved_is_stopped;
        throw;
    }
    subrun_depth--;

    if (is_stopped) {
        // Save stopped state for RESUME — keep chunk alive by copying
        stopped_chunk = chunk;
        // Update frame pointers: frames pointing to &chunk must point to &stopped_chunk
        stopped_frames = std::move(frames);
        for (auto& f : stopped_frames) {
            if (f.chunk == &chunk) f.chunk = &stopped_chunk;
        }
        stopped_stack.assign(stack.begin(), stack.begin() + sp);
        stopped_sp = sp;
        // Expose locals of the stopped function frame as console globals
        inject_stopped_locals();
        // is_stopped stays true — caller (console) will check it
    } else {
        // Sub-run completed normally; restore prior STOP state if any
        is_stopped = saved_is_stopped;
    }

    // Restore previous execution state
    frames = std::move(saved_frames);
    sp = saved_sp;
    min_frame_depth = saved_min;
}

VMState VM::save_state() const {
    VMState s;
    s.globals = globals;
    s.global_names = global_names;
    s.functions = owned_funcs;
    s.func_map = func_map;
    return s;
}

void VM::restore_state(const VMState& state) {
    globals = state.globals;
    global_names = state.global_names;
    owned_funcs = state.functions;
    func_map = state.func_map;
    func_protos = &owned_funcs;
    frames.clear();
    sp = 0;
    func_map_generation++;
}

void VM::reset() {
    globals.clear();
    global_names.clear();
    owned_funcs.clear();
    func_map.clear();
    func_protos = &owned_funcs;
    frames.clear();
    sp = 0;
    is_stopped = false;
    event_handlers.clear();
    func_map_generation++;
}

void VM::inject_stopped_locals() {
    injected_locals.clear();
    if (stopped_frames.empty()) return;
    // The deepest frame is where execution paused — usually the function with STOP
    const CallFrame& f = stopped_frames.back();
    if (!f.chunk) return;
    for (size_t i = 0; i < f.chunk->var_names.size(); i++) {
        const std::string& name = f.chunk->var_names[i];
        if (name.empty()) continue;
        size_t stack_idx = f.stack_base + i;
        if (stack_idx >= stopped_stack.size()) continue;
        InjectedLocal il;
        il.name = name;
        il.slot_in_stopped_stack = stack_idx;
        auto git = global_names.find(name);
        if (git != global_names.end()) {
            il.was_existing = true;
            il.overridden_value = globals[git->second];
            il.global_slot = git->second;
            globals[git->second] = stopped_stack[stack_idx];
        } else {
            il.was_existing = false;
            il.global_slot = static_cast<uint16_t>(globals.size());
            globals.push_back(stopped_stack[stack_idx]);
            global_names[name] = il.global_slot;
        }
        injected_locals.push_back(std::move(il));
    }
}

void VM::extract_stopped_locals() {
    for (auto& il : injected_locals) {
        // Write back possibly-modified value to the stopped frame's stack slot
        if (il.slot_in_stopped_stack < stopped_stack.size() &&
            il.global_slot < globals.size()) {
            stopped_stack[il.slot_in_stopped_stack] = globals[il.global_slot];
        }
        if (il.was_existing) {
            if (il.global_slot < globals.size())
                globals[il.global_slot] = il.overridden_value;
        } else {
            global_names.erase(il.name);
            if (il.global_slot < globals.size())
                globals[il.global_slot] = Value();  // NONE; slot becomes orphaned
        }
    }
    injected_locals.clear();
}

bool VM::resume() {
    if (!is_stopped) return false;

    // Pull any console-modified locals back into the stopped frame, then clean up
    extract_stopped_locals();

    // Restore stopped execution state
    frames = std::move(stopped_frames);
    // Restore stack
    if (stopped_sp > stack.size()) stack.resize(stopped_sp * 2);
    for (size_t i = 0; i < stopped_sp; i++) stack[i] = stopped_stack[i];
    sp = stopped_sp;
    min_frame_depth = 0;
    is_stopped = false;
    func_protos = &owned_funcs;

    // Continue execution
    run();
    return true;
}

void VM::bind_reactive(const std::string& var, const std::string& formula,
                        const std::string& func_name, const std::vector<std::string>& deps) {
    ReactiveBinding binding;
    binding.formula = formula;
    binding.func_name = func_name;
    binding.dependencies = deps;
    reactive_bindings[var] = std::move(binding);
}

void VM::propagate_reactive(const std::string& changed_var) {
    if (reactive_updating) return;
    reactive_updating = true;

    // Find all reactive vars that depend on changed_var (BFS)
    std::vector<std::string> to_update;
    std::vector<std::string> queue = {changed_var};
    std::unordered_set<std::string> visited;

    while (!queue.empty()) {
        std::string current = queue.front();
        queue.erase(queue.begin());
        if (visited.count(current)) continue;
        visited.insert(current);

        for (auto& [name, binding] : reactive_bindings) {
            for (auto& dep : binding.dependencies) {
                if (dep == current && !visited.count(name)) {
                    to_update.push_back(name);
                    queue.push_back(name);
                }
            }
        }
    }

    // Re-evaluate using the compiled reactive functions
    for (auto& var : to_update) {
        auto it = reactive_bindings.find(var);
        if (it == reactive_bindings.end()) continue;
        try {
            // The reactive function was compiled during REACT_ASSIGN
            auto fit = func_map.find(it->second.func_name);
            if (fit == func_map.end() && func_protos) {
                // Search in func_protos (file mode)
                for (size_t i = 0; i < func_protos->size(); i++) {
                    if ((*func_protos)[i].name == it->second.func_name) {
                        func_map[it->second.func_name] = i;
                        fit = func_map.find(it->second.func_name);
                        break;
                    }
                }
            }
            if (fit != func_map.end()) {
                // Save/restore execution state for safe re-entrant call
                auto saved_frames = std::move(frames);
                auto saved_sp_val = sp;
                auto saved_min = min_frame_depth;

                frames.clear();
                sp = 0;
                min_frame_depth = 0;

                FuncProto& proto = func_protos ? (*func_protos)[fit->second] : owned_funcs[fit->second];
                frames.push_back({&proto.chunk, 0, 0});
                size_t needed = proto.chunk.var_names.size();
                while (needed >= stack.size()) stack.resize(stack.size() * 2);
                if (sp < needed) sp = needed;
                run();
                Value result = pop();

                frames = std::move(saved_frames);
                sp = saved_sp_val;
                min_frame_depth = saved_min;

                // Set the reactive variable — check if dotted (UDT member)
                size_t rdot = var.find('.');
                if (rdot != std::string::npos) {
                    std::string obj_name = var.substr(0, rdot);
                    std::string field = var.substr(rdot + 1);
                    auto oit = global_names.find(obj_name);
                    if (oit != global_names.end() && oit->second < globals.size() &&
                        globals[oit->second].type == ValueType::OBJECT) {
                        Value* old_val = globals[oit->second].as_object()->get(field);
                        if (!old_val || !values_equal(*old_val, result))
                            globals[oit->second].as_object()->set(field, std::move(result));
                    }
                } else {
                    auto git = global_names.find(var);
                    if (git != global_names.end() && git->second < globals.size())
                        globals[git->second] = std::move(result);
                }
            }
        } catch (...) { /* ignore errors during reactive update */ }
    }

    reactive_updating = false;
}

void VM::set_global(const std::string& name, Value val) {
    auto it = global_names.find(name);
    if (it != global_names.end()) {
        globals[it->second] = std::move(val);
    } else {
        uint16_t slot = static_cast<uint16_t>(globals.size());
        globals.push_back(std::move(val));
        global_names[name] = slot;
    }
}

void VM::register_const(const std::string& name, Value val) {
    // Convert to uppercase for case-insensitive matching
    std::string upper = name;
    for (auto& c : upper) c = std::toupper(c);
    set_global(upper, std::move(val));
    const_globals.insert(upper);
}

bool VM::is_const(const std::string& name) const {
    std::string upper = name;
    for (auto& c : upper) c = std::toupper(c);
    return const_globals.count(upper) > 0;
}

// Call a funcref (string name or array [name, captures...])
Value VM::call_funcref(const Value& ref, const std::vector<Value>& args) {
    if (ref.type == ValueType::STRING) {
        return call_function(ref.as_string()->data, args);
    }
    if (ref.type == ValueType::ARRAY) {
        auto* arr = ref.as_array();
        if (!arr->elements.empty() && arr->elements[0].type == ValueType::STRING) {
            std::string real_name = arr->elements[0].as_string()->data;
            std::vector<Value> full_args;
            for (size_t i = 1; i < arr->elements.size(); i++)
                full_args.push_back(arr->elements[i]);
            for (auto& a : args) full_args.push_back(a);
            return call_function(real_name, full_args);
        }
    }
    throw std::runtime_error("Invalid function reference");
}

Value VM::call_function(const std::string& name, const std::vector<Value>& args) {
    // Native?
    auto nit = natives.find(name);
    if (nit != natives.end()) {
        // Auto-vectorize: mirror OpCode::CALL behaviour so external callers
        // (e.g. the native-compiler VM bridge) also get vectorized results.
        // This is a subset of OpCode::CALL's blocklist, focused on what
        // the bridge actually hits — kept deliberately smaller.
        static const std::unordered_set<std::string> no_vec = {
            "ZEROS","ONES","__MAKE_UDT_ARRAY__","IOTA","RESHAPE","LEN","LENV","PUSH","POP",
            "TENSOR","TYPEOF","APPEND","DIFF",
            "SUM","PRODUCT","MIN","MAX","ANY","ALL",
            "SCAN","SELECT","FILTER","REDUCE",
            "TAKE","DROP","REVERSE","UNIQUE","SHUFFLE",
            "FIND_IN_ARRAY","NORMALIZE","DISTANCE","GRADE",
            "TRANSPOSE","MATMUL","MVLET","STACK","SLICE",
            "SOLVE","INVERT","CONVOLVE","PLACE",
            "OUTER","ROTATE","SHIFT","XSORT","INTEGRATE",
            "GETENV$","SETLOCALE","TICK","NOW",
            "DATE$","TIME$","CVDATE",
            "MAP.EXISTS","MAP.KEYS","MAP.VALUES","MAP.ITEMS","MAP.SIZE",
            "MAP.DELETE","MAP.CLEAR","MAP.MERGE","MAP.FROM",
            "JSON.PARSE$","JSON.STRINGIFY$",
            "SPLIT","FORMAT$","FRMV$","INSERT$","REPLACE$","REVERSE$",
            "PACK$","UNPACK","JOIN",
            "REGEX_MATCH","REGEX_REPLACE$","REGEX.MATCH","REGEX.FINDALL","REGEX.REPLACE",
            "MEAN","MEDIAN","VARIANCE","STDEV","DOT","CROSS","CUMSUM","CUMPROD",
            "HISTOGRAM","LINSPACE","FLATTEN","ZIP","RANGE","COUNT","INDEXOF",
            "ISNUM","ISSTR","ISARR","ISMAP","ISBOOL","ISNONE","ISNULL","RANDOMSEED",
            "IIF","EXECUTE","EVAL","LOAD","SAVE","LIST","HELP","HELP$","VARS",
            // SPRITE.ANIM id, name$, frames[], fps, [loop] — frames[] is the payload,
            // broadcasting would leave anim.frames empty and crash SPRITE.PLAY.
            "SPRITE.ANIM",
            // GFX.PLOT_POINTS points[], GFX.PLOT_POINTS_TEX points[] — array is the data.
            "GFX.PLOT_POINTS","GFX.PLOT_POINTS_TEX"
        };
        bool has_arr = false;
        if (!no_vec.count(name)) {
            for (auto& a : args) if (a.type == ValueType::ARRAY) { has_arr = true; break; }
        }
        if (has_arr) {
            std::function<Value(const std::vector<Value>&)> vec =
                [&](const std::vector<Value>& cur) -> Value {
                size_t alen = 0;
                bool any = false;
                for (auto& a : cur) {
                    if (a.type == ValueType::ARRAY) {
                        any = true;
                        size_t n = a.as_array()->elements.size();
                        if (n > alen) alen = n;
                    }
                }
                if (!any) return nit->second(cur);
                Value r = Value::make_array();
                r.as_array()->elements.reserve(alen);
                for (size_t i = 0; i < alen; i++) {
                    std::vector<Value> ea(cur.size());
                    for (size_t j = 0; j < cur.size(); j++) {
                        if (cur[j].type == ValueType::ARRAY) {
                            auto* ar = cur[j].as_array();
                            ea[j] = ar->elements[i % ar->elements.size()];
                        } else {
                            ea[j] = cur[j];
                        }
                    }
                    r.as_array()->elements.push_back(vec(ea));
                }
                return r;
            };
            return vec(args);
        }
        return nit->second(args);
    }

    // User-defined?
    auto fit = func_map.find(name);
    if (fit == func_map.end())
        throw jdError(ErrCode::UNDEFINED_FUNCTION, "Undefined function: " + name);

    FuncProto& proto = (*func_protos)[fit->second];
    size_t new_base = sp;
    for (auto& a : args) push(a);
    size_t needed = new_base + proto.chunk.var_names.size();
    while (needed >= stack.size()) stack.resize(stack.size() * 2);
    if (sp < needed) sp = needed;

    if (frames.size() >= 512)
        throw jdError(ErrCode::STACK_OVERFLOW, "Call stack overflow (max 512 frames)");
    size_t saved_min = min_frame_depth;
    min_frame_depth = frames.size();
    frames.push_back({&proto.chunk, 0, new_base});
    run();
    min_frame_depth = saved_min;
    // If the called function (or anything it triggered) ran END, the
    // VM has no return value to pop — bail out cleanly. The is_halted
    // flag stays set so further nested unwinds also short-circuit.
    if (is_halted) return Value::make_none();
    return pop();
}

Value VM::apply_binary_op(const std::string& op, const Value& a, const Value& b) {
    if (op == "+" || op == "ADD") return arithmetic(a, b, OpCode::ADD);
    if (op == "-" || op == "SUB") return arithmetic(a, b, OpCode::SUB);
    if (op == "*" || op == "MUL") return arithmetic(a, b, OpCode::MUL);
    if (op == "/" || op == "DIV") return arithmetic(a, b, OpCode::DIV);
    if (op == "\\" || op == "IDIV") return arithmetic(a, b, OpCode::IDIV);
    if (op == "MOD") return arithmetic(a, b, OpCode::MOD_OP);
    if (op == "^" || op == "POW") return arithmetic(a, b, OpCode::POW);
    if (op == ">" || op == "GT") return compare(a, b, OpCode::CMP_GT);
    if (op == "<" || op == "LT") return compare(a, b, OpCode::CMP_LT);
    if (op == "=" || op == "EQ") return compare(a, b, OpCode::CMP_EQ);
    if (op == ">=" || op == "GE") return compare(a, b, OpCode::CMP_GE);
    if (op == "<=" || op == "LE") return compare(a, b, OpCode::CMP_LE);
    if (op == "<>" || op == "NE") return compare(a, b, OpCode::CMP_NE);
    if (op == "MIN") return a.to_double() <= b.to_double() ? a : b;
    if (op == "MAX") return a.to_double() >= b.to_double() ? a : b;
    if (op == "AND") return Value::make_bool(a.to_bool() && b.to_bool());
    if (op == "OR") return Value::make_bool(a.to_bool() || b.to_bool());
    // Treat as function name
    return call_function(op, {a, b});
}

uint16_t VM::ensure_global(uint16_t name_idx) {
    const auto& name = frame().chunk->var_names[name_idx];
    auto it = global_names.find(name);
    if (it != global_names.end()) return it->second;
    uint16_t slot = static_cast<uint16_t>(globals.size());
    globals.push_back(Value::make_none());
    global_names[name] = slot;
    return slot;
}

// ── Main execution loop ──────────────────────────────────────

void VM::run() {
    while (true) {
      try {
        // If a nested call (e.g. an event handler) ran END, abort the
        // outer loop too. Without this, main keeps executing past the
        // RAISEEVENT call site even though the program said END.
        if (is_halted) return;

        // Cache the current frame once per iteration — frames.back() requires
        // multiple loads per access and is a measurable overhead in tight
        // recursive loops. CALL/RETURN fall out of the switch via break, so
        // the next iteration refetches automatically.
        CallFrame& cf = frames.back();

        // Process pending reactive updates
        if (!reactive_pending.empty() && !reactive_updating && !reactive_bindings.empty()) {
            std::vector<std::string> pending;
            pending.swap(reactive_pending);
            for (auto& var : pending) propagate_reactive(var);
        }

        // Periodic tick for RECUR tasks + event polling.
        // Low threshold (200) so event handlers fire frequently enough for
        // interactive graphics (a tight DO/SCREENFLIP loop is ~20 opcodes
        // per iteration → fires every ~10 frames at 60 FPS).
        if (++tick_counter >= 200) {
            tick_counter = 0;
            if (on_tick) on_tick();
            if (!event_handlers.empty()) event_poll();
        }

        size_t trace_ip = cf.ip;
        OpCode op = static_cast<OpCode>(cf.chunk->code[cf.ip++]);

        if (trace_enabled && !frames.empty()) {
            int tline = (trace_ip < frame().chunk->line_info.size()) ? frame().chunk->line_info[trace_ip] : 0;
            if (tline > 0) std::cerr << "[TRACE] line " << tline << " op " << (int)op << std::endl;
        }

        // Debug adapter: check for breakpoints/stepping on line change
        if (debug && debug->dap && !frames.empty()) {
            if (trace_ip < frame().chunk->line_info.size()) {
                int dline = frame().chunk->line_info[trace_ip];
                if (dline > 0) {
                    debug_check(dline);
                    // If IP was changed by goto during pause, restart the fetch cycle
                    if (frame().ip != trace_ip + 1) continue;
                }
            }
        }

        switch (op) {

        case OpCode::LOAD_CONST: {
            uint16_t idx = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            if (sp >= stack.size()) stack.resize(stack.size() * 2);
            stack[sp++] = cf.chunk->constants[idx];
            break;
        }

        case OpCode::LOAD_VAR: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            if (sp >= stack.size()) stack.resize(stack.size() * 2);
            stack[sp++] = stack[cf.stack_base + slot];
            break;
        }

        case OpCode::STORE_VAR: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            size_t abs_slot = cf.stack_base + slot;
            if (abs_slot >= sp) {
                while (abs_slot >= stack.size()) stack.resize(stack.size() * 2);
                if (abs_slot >= sp) sp = abs_slot + 1;
            }
            stack[abs_slot] = std::move(stack[--sp]);
            break;
        }

        case OpCode::LOAD_GLOBAL: {
            uint16_t name_idx = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;

            // ── Inline cache fast path ──────────────────────────
            uint16_t slot;
            {
                auto& gcache = cf.chunk->global_cache;
                if (name_idx < gcache.size()) {
                    uint64_t entry = gcache[name_idx];
                    if ((uint32_t)(entry >> 32) == func_map_generation) {
                        slot = (uint16_t)(entry & 0xFFFFu);
                        Value& val = globals[slot];
                        if (val.type != ValueType::NONE) {
                            if (sp >= stack.size()) stack.resize(stack.size() * 2);
                            stack[sp++] = val;
                            break;
                        }
                    }
                }
                // Slow path: resolve + cache
                slot = ensure_global(name_idx);
                if (name_idx >= gcache.size()) gcache.resize(name_idx + 1, 0);
                gcache[name_idx] = ((uint64_t)func_map_generation << 32) | (uint64_t)slot;
            }

            Value& val = globals[slot];
            if (val.type == ValueType::NONE) {
                // Dotted name fallback: "OBJ.X.Y.FIELD" → navigate chain
                const std::string& full = cf.chunk->var_names[name_idx];
                size_t dot = full.find('.');
                if (dot != std::string::npos) {
                    std::string obj_name = full.substr(0, dot);
                    std::string rest = full.substr(dot + 1);

                    // First, check the current function's locals — the
                    // parser greedily folds `rs.Fields.Count` into a single
                    // global name "RS.FIELDS.COUNT", so when `rs` is a local
                    // (e.g. inside a SUB/FUNC) the global lookup misses and
                    // the result was NONE.
                    auto ci_eq = [](const std::string& a, const std::string& b) {
                        if (a.size() != b.size()) return false;
                        for (size_t i = 0; i < a.size(); i++)
                            if (std::toupper((unsigned char)a[i]) !=
                                std::toupper((unsigned char)b[i])) return false;
                        return true;
                    };
                    Value cur;
                    bool have_cur = false;
                    if (frames.size() > 1) {
                        auto& var_names = frame().chunk->var_names;
                        for (size_t li = 0; li < var_names.size(); li++) {
                            if (ci_eq(var_names[li], obj_name)) {
                                size_t abs = frame().stack_base + li;
                                if (abs < stack.size() &&
                                    stack[abs].type == ValueType::OBJECT) {
                                    cur = stack[abs];
                                    have_cur = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!have_cur) {
                        auto oit = global_names.find(obj_name);
                        if (oit != global_names.end() && oit->second < globals.size() &&
                            globals[oit->second].type == ValueType::OBJECT) {
                            cur = globals[oit->second];
                            have_cur = true;
                        }
                    }
                    if (have_cur) {
                        size_t pos = 0;
                        bool ok = true;
                        while (pos < rest.size()) {
                            size_t next = rest.find('.', pos);
                            std::string part = (next == std::string::npos) ? rest.substr(pos) : rest.substr(pos, next - pos);
#ifdef COM
                            Value com_r;
                            if (com_try_get_field(cur, part, com_r)) { cur = com_r; }
                            else
#endif
                            if (cur.type == ValueType::OBJECT) {
                                Value* f = cur.as_object()->get(part);
                                if (f) cur = *f; else { ok = false; break; }
                            } else { ok = false; break; }
                            pos = (next == std::string::npos) ? rest.size() : next + 1;
                        }
                        if (ok) { push(cur); break; }
                    }
                }
                // Fallback: no global by this name — try a zero-arg native
                // call. This makes constant-like natives (PI, E, TICK, NOW, ...)
                // usable as bare identifiers: `2 * PI`.
                {
                    const std::string& full2 = cf.chunk->var_names[name_idx];
                    auto nit = natives.find(full2);
                    if (nit != natives.end()) {
                        try {
                            Value r = nit->second({});
                            if (sp >= stack.size()) stack.resize(stack.size() * 2);
                            stack[sp++] = std::move(r);
                            break;
                        } catch (...) {
                            // native needs arguments or failed — fall through
                        }
                    }
                }
            }
            if (sp >= stack.size()) stack.resize(stack.size() * 2);
            stack[sp++] = val;
            break;
        }

        case OpCode::STORE_GLOBAL: {
            uint16_t name_idx = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            const std::string& full = cf.chunk->var_names[name_idx];
            // Check if this is a protected constant — but allow if this is
            // a CONST re-declaration (next opcode is MARK_CONST for same name).
            // This makes CONST idempotent across repeated module loads.
            if (const_globals.count(full) > 0) {
                bool is_const_init = false;
                if (cf.ip < cf.chunk->code.size() &&
                    (OpCode)cf.chunk->code[cf.ip] == OpCode::MARK_CONST) {
                    uint16_t mc_idx = cf.chunk->code[cf.ip + 1] | (cf.chunk->code[cf.ip + 2] << 8);
                    if (cf.chunk->var_names[mc_idx] == full) is_const_init = true;
                }
                if (!is_const_init) {
                    throw jdError(ErrCode::RUNTIME_ERROR,
                        "Cannot assign to constant '" + full + "'");
                }
            }
            // Dotted name fallback: "OBJ.FIELD" → load OBJ, set FIELD
            size_t dot = full.find('.');
            if (dot != std::string::npos) {
                std::string obj_name = full.substr(0, dot);
                std::string field = full.substr(dot + 1);
                auto oit = global_names.find(obj_name);
                if (oit != global_names.end() && oit->second < globals.size() &&
                    globals[oit->second].type == ValueType::OBJECT) {
{
                    Value new_val = pop();
                    Value* old_val = globals[oit->second].as_object()->get(field);
                    bool changed = !old_val || !values_equal(*old_val, new_val);
#ifdef COM
                    if (com_try_set_field(globals[oit->second], field, new_val)) {
                        if (changed && !reactive_bindings.empty() && !reactive_updating)
                            reactive_pending.push_back(full);
                        break;
                    }
#endif
                    globals[oit->second].as_object()->set(field, std::move(new_val));
                    if (changed && !reactive_bindings.empty() && !reactive_updating)
                        reactive_pending.push_back(full);
}
                    break;
                }
            }
            // ── Inline cache lookup/populate ────────────────────
            uint16_t slot;
            {
                auto& gcache = cf.chunk->global_cache;
                if (name_idx < gcache.size()) {
                    uint64_t entry = gcache[name_idx];
                    if ((uint32_t)(entry >> 32) == func_map_generation) {
                        slot = (uint16_t)(entry & 0xFFFFu);
                        goto store_global_slot_ready;
                    }
                }
                slot = ensure_global(name_idx);
                if (name_idx >= gcache.size()) gcache.resize(name_idx + 1, 0);
                gcache[name_idx] = ((uint64_t)func_map_generation << 32) | (uint64_t)slot;
            }
        store_global_slot_ready:
            if (slot >= globals.size()) globals.resize(slot + 1);
            {
                Value new_val = std::move(stack[--sp]);
                bool has_reactive = !reactive_bindings.empty() && !reactive_updating;
                if (has_reactive) {
                    bool changed = !values_equal(globals[slot], new_val);
                    globals[slot] = std::move(new_val);
                    if (changed) reactive_pending.push_back(full);
                } else {
                    globals[slot] = std::move(new_val);
                }
            }
            break;
        }

        // ── Arithmetic ───────────────────────────────────────

        case OpCode::ADD: case OpCode::SUB: case OpCode::MUL:
        case OpCode::DIV: case OpCode::IDIV: case OpCode::MOD_OP: case OpCode::POW: {
            // ── Fast path: both INT64 (recursive numeric code) ─────
            if (sp >= 2) {
                Value& fa = stack[sp - 2];
                Value& fb = stack[sp - 1];
                if (fa.type == ValueType::INT64 && fb.type == ValueType::INT64 &&
                    op != OpCode::DIV && op != OpCode::POW) {
                    int64_t x = fa.i64, y = fb.i64, r;
                    bool overflow = false;
                    switch (op) {
                        case OpCode::ADD: {
                            // Cheap signed-add overflow check
                            uint64_t ur = (uint64_t)x + (uint64_t)y;
                            r = (int64_t)ur;
                            overflow = ((x ^ r) & (y ^ r)) < 0;
                            break;
                        }
                        case OpCode::SUB: {
                            uint64_t ur = (uint64_t)x - (uint64_t)y;
                            r = (int64_t)ur;
                            overflow = ((x ^ y) & (x ^ r)) < 0;
                            break;
                        }
                        case OpCode::MUL: {
                            r = (int64_t)((uint64_t)x * (uint64_t)y);
                            // Verify by division: classic overflow detector
                            if (x != 0 && r / x != y) overflow = true;
                            break;
                        }
                        case OpCode::IDIV:
                            if (y == 0) { int eln = (trace_ip < cf.chunk->line_info.size()) ? cf.chunk->line_info[trace_ip] : 0; throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero", eln); }
                            r = x / y; break; // C++ int division truncates toward zero
                        case OpCode::MOD_OP: r = (y != 0) ? x % y : 0; break;
                        default: r = 0;
                    }
                    if (overflow) {
                        // Promote to double rather than wrap around silently.
                        // Lets factorials, products and sums of large integers
                        // produce a meaningful (if approximate) result instead
                        // of garbage. The double has ~15 significant digits;
                        // beyond that the user wanted BigInt anyway.
                        double dx = (double)x, dy = (double)y, dr;
                        switch (op) {
                            case OpCode::ADD: dr = dx + dy; break;
                            case OpCode::SUB: dr = dx - dy; break;
                            case OpCode::MUL: dr = dx * dy; break;
                            default:          dr = 0; break;
                        }
                        fa.type = ValueType::FLOAT64;
                        fa.f64 = dr;
                        sp--;
                        break;
                    }
                    fa.i64 = r; // reuse slot; type already INT64; obj is null
                    sp--;
                    break;
                }
                // Fast path: both FLOAT64
                if (fa.type == ValueType::FLOAT64 && fb.type == ValueType::FLOAT64 &&
                    op != OpCode::POW) {
                    double x = fa.f64, y = fb.f64, r;
                    bool want_int = false;
                    switch (op) {
                        case OpCode::ADD:    r = x + y; break;
                        case OpCode::SUB:    r = x - y; break;
                        case OpCode::MUL:    r = x * y; break;
                        case OpCode::DIV:
                            if (y == 0.0) { int eln = (trace_ip < cf.chunk->line_info.size()) ? cf.chunk->line_info[trace_ip] : 0; throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero", eln); }
                            r = x / y; break;
                        case OpCode::IDIV:
                            if (y == 0.0) { int eln = (trace_ip < cf.chunk->line_info.size()) ? cf.chunk->line_info[trace_ip] : 0; throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero", eln); }
                            // Truncates toward zero, like C++ integer division
                            r = (double)(int64_t)(x / y); want_int = true; break;
                        case OpCode::MOD_OP: r = (y != 0.0) ? std::fmod(x, y) : 0.0; break;
                        default: r = 0.0;
                    }
                    if (want_int) {
                        fa.type = ValueType::INT64;
                        fa.i64 = (int64_t)r;
                    } else {
                        fa.f64 = r;
                    }
                    sp--;
                    break;
                }
            }

            Value b = pop();
            Value a = pop();
            // Array element-wise: array OP scalar, scalar OP array, array OP array
            if (a.type == ValueType::ARRAY || b.type == ValueType::ARRAY) {
                push(array_arithmetic(a, b, op));
            }
            // String operators
            else if (a.type == ValueType::STRING || b.type == ValueType::STRING) {
                if (op == OpCode::ADD) {
                    // Concatenation: "Hello " + "World"
                    push(Value::make_string(a.to_string() + b.to_string()));
                } else if (op == OpCode::SUB && a.type == ValueType::STRING && b.type == ValueType::STRING) {
                    // Replacement: "abababac" - "ab" → "ac"
                    std::string s = a.as_string()->data;
                    std::string rem = b.as_string()->data;
                    if (!rem.empty()) {
                        size_t pos;
                        while ((pos = s.find(rem)) != std::string::npos)
                            s.erase(pos, rem.size());
                    }
                    push(Value::make_string(s));
                } else if (op == OpCode::MUL && a.type == ValueType::STRING) {
                    // Repetition: "-" * 10
                    std::string s;
                    int n = (int)b.to_int();
                    std::string src = a.as_string()->data;
                    for (int i = 0; i < n; i++) s += src;
                    push(Value::make_string(s));
                } else if (op == OpCode::MUL && b.type == ValueType::STRING) {
                    // Repetition: 10 * "-"
                    std::string s;
                    int n = (int)a.to_int();
                    std::string src = b.as_string()->data;
                    for (int i = 0; i < n; i++) s += src;
                    push(Value::make_string(s));
                } else if (op == OpCode::DIV) {
                    // Slicing: 5 / "Welcome" → "Welco" (left), "Welcome" / 4 → "come" (right)
                    if (a.type == ValueType::STRING && is_numeric(b.type)) {
                        std::string s = a.as_string()->data;
                        int n = (int)b.to_int();
                        push(Value::make_string(s.size() > (size_t)n ? s.substr(s.size() - n) : s));
                    } else if (is_numeric(a.type) && b.type == ValueType::STRING) {
                        std::string s = b.as_string()->data;
                        int n = (int)a.to_int();
                        push(Value::make_string(s.substr(0, n)));
                    } else {
                        push(Value::make_string(a.to_string()));
                    }
                } else {
                    push(Value::make_string(a.to_string() + b.to_string()));
                }
            } else {
                push(arithmetic(a, b, op));
            }
            break;
        }

        case OpCode::NEG: {
            Value a = pop();
            if (a.type == ValueType::ARRAY) {
                auto* arr = a.as_array();
                Value result = Value::make_array();
                auto* out = result.as_array();
                out->elements.reserve(arr->elements.size());
                for (auto& elem : arr->elements) {
                    if (is_integer_type(elem.type))
                        out->elements.push_back(Value::make_i64(-elem.to_int()));
                    else
                        out->elements.push_back(Value::make_f64(-elem.to_double()));
                }
                push(std::move(result));
            } else if (a.type == ValueType::STRING) {
                // Unary split: -"ABC" → ["A", "B", "C"]
                auto& s = a.as_string()->data;
                Value result = Value::make_array();
                auto* out = result.as_array();
                for (size_t i = 0; i < s.size(); ) {
                    unsigned char c = s[i];
                    int len = 1;
                    if (c >= 0xC0) { len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2; }
                    out->elements.push_back(Value::make_string(s.substr(i, len)));
                    i += len;
                }
                push(std::move(result));
            } else if (is_integer_type(a.type)) {
                push(Value::make_i64(-a.to_int()));
            } else {
                push(Value::make_f64(-a.to_double()));
            }
            break;
        }

        // ── Comparison ───────────────────────────────────────

        case OpCode::CMP_EQ: case OpCode::CMP_NE:
        case OpCode::CMP_LT: case OpCode::CMP_GT:
        case OpCode::CMP_LE: case OpCode::CMP_GE: {
            // Fast path: both INT64
            if (sp >= 2) {
                Value& fa = stack[sp - 2];
                Value& fb = stack[sp - 1];
                if (fa.type == ValueType::INT64 && fb.type == ValueType::INT64) {
                    int64_t x = fa.i64, y = fb.i64;
                    bool r;
                    switch (op) {
                        case OpCode::CMP_EQ: r = (x == y); break;
                        case OpCode::CMP_NE: r = (x != y); break;
                        case OpCode::CMP_LT: r = (x <  y); break;
                        case OpCode::CMP_GT: r = (x >  y); break;
                        case OpCode::CMP_LE: r = (x <= y); break;
                        case OpCode::CMP_GE: r = (x >= y); break;
                        default: r = false;
                    }
                    // Replace fa with boolean result; fb slot is dropped
                    fa.type = ValueType::BOOLEAN;
                    fa.boolean = r;
                    sp--;
                    break;
                }
                if (fa.type == ValueType::FLOAT64 && fb.type == ValueType::FLOAT64) {
                    double x = fa.f64, y = fb.f64;
                    bool r;
                    switch (op) {
                        case OpCode::CMP_EQ: r = (x == y); break;
                        case OpCode::CMP_NE: r = (x != y); break;
                        case OpCode::CMP_LT: r = (x <  y); break;
                        case OpCode::CMP_GT: r = (x >  y); break;
                        case OpCode::CMP_LE: r = (x <= y); break;
                        case OpCode::CMP_GE: r = (x >= y); break;
                        default: r = false;
                    }
                    fa.type = ValueType::BOOLEAN;
                    fa.boolean = r;
                    sp--;
                    break;
                }
            }
            Value b = pop();
            Value a = pop();
            // Element-wise comparison for arrays — RECURSIVE so 2-D matrices
            // (e.g. `(board = 0)` for a [10,10] board) keep their shape.
            std::function<Value(const Value&, const Value&)> rec =
                [&](const Value& la, const Value& lb) -> Value {
                if (la.type == ValueType::ARRAY || lb.type == ValueType::ARRAY) {
                    Value result = Value::make_array();
                    auto* out = result.as_array();
                    if (la.type == ValueType::ARRAY && lb.type == ValueType::ARRAY) {
                        auto& aa = la.as_array()->elements;
                        auto& bb = lb.as_array()->elements;
                        size_t len = std::min(aa.size(), bb.size());
                        out->elements.reserve(len);
                        for (size_t i = 0; i < len; i++)
                            out->elements.push_back(rec(aa[i], bb[i]));
                    } else if (la.type == ValueType::ARRAY) {
                        for (auto& e : la.as_array()->elements)
                            out->elements.push_back(rec(e, lb));
                    } else {
                        for (auto& e : lb.as_array()->elements)
                            out->elements.push_back(rec(la, e));
                    }
                    return result;
                }
                return compare(la, lb, op);
            };
            push(rec(a, b));
            break;
        }

        // ── Logical ──────────────────────────────────────────

        case OpCode::LOG_AND:
        case OpCode::LOG_OR: {
            Value b = pop();
            Value a = pop();
            // Element-wise on arrays so APL-style boolean masks compose, and
            // recursive so 2-D matrices (`board`-shaped arrays of rows) work
            // for `(board = 0) AND (neighbors = 3)`-style expressions.
            std::function<Value(const Value&, const Value&)> rec =
                [&](const Value& la, const Value& lb) -> Value {
                if (la.type == ValueType::ARRAY || lb.type == ValueType::ARRAY) {
                    Value result = Value::make_array();
                    auto* out = result.as_array();
                    if (la.type == ValueType::ARRAY && lb.type == ValueType::ARRAY) {
                        auto& aa = la.as_array()->elements;
                        auto& bb = lb.as_array()->elements;
                        size_t len = std::min(aa.size(), bb.size());
                        out->elements.reserve(len);
                        for (size_t i = 0; i < len; i++)
                            out->elements.push_back(rec(aa[i], bb[i]));
                    } else if (la.type == ValueType::ARRAY) {
                        for (auto& e : la.as_array()->elements)
                            out->elements.push_back(rec(e, lb));
                    } else {
                        for (auto& e : lb.as_array()->elements)
                            out->elements.push_back(rec(la, e));
                    }
                    return result;
                }
                bool av = la.to_bool(), bv = lb.to_bool();
                bool r = (op == OpCode::LOG_AND) ? (av && bv) : (av || bv);
                return Value::make_bool(r);
            };
            push(rec(a, b));
            break;
        }

        case OpCode::LOG_NOT: {
            Value a = pop();
            std::function<Value(const Value&)> rec = [&](const Value& v) -> Value {
                if (v.type == ValueType::ARRAY) {
                    Value result = Value::make_array();
                    auto* out = result.as_array();
                    for (auto& e : v.as_array()->elements)
                        out->elements.push_back(rec(e));
                    return result;
                }
                return Value::make_bool(!v.to_bool());
            };
            push(rec(a));
            break;
        }

        // ── Bitwise ─────────────────────────────────────────

        case OpCode::BIT_AND: {
            Value b = pop(); Value a = pop();
            push(Value::make_i64(a.to_int() & b.to_int()));
            break;
        }

        case OpCode::BIT_OR: {
            Value b = pop(); Value a = pop();
            push(Value::make_i64(a.to_int() | b.to_int()));
            break;
        }

        case OpCode::BIT_XOR: {
            Value b = pop(); Value a = pop();
            push(Value::make_i64(a.to_int() ^ b.to_int()));
            break;
        }

        case OpCode::BIT_NOT: {
            Value a = pop();
            push(Value::make_i64(~a.to_int()));
            break;
        }

        case OpCode::BIT_SHL: {
            Value b = pop(); Value a = pop();
            push(Value::make_i64(a.to_int() << b.to_int()));
            break;
        }

        case OpCode::BIT_SHR: {
            Value b = pop(); Value a = pop();
            push(Value::make_i64(a.to_int() >> b.to_int()));
            break;
        }

        case OpCode::OP_IN: {
            Value container = pop();
            Value needle = pop();
            bool found = false;
            if (container.type == ValueType::ARRAY) {
                std::string ns = needle.to_string();
                for (auto& e : container.as_array()->elements) {
                    if (e.to_string() == ns) { found = true; break; }
                }
            } else if (container.type == ValueType::OBJECT) {
                // Check if key exists in map
                std::string key = needle.to_string();
                found = (container.as_object()->get(key) != nullptr);
            } else if (container.type == ValueType::STRING) {
                // Check if substring exists
                found = (container.as_string()->data.find(needle.to_string()) != std::string::npos);
            }
            push(Value::make_bool(found));
            break;
        }

        // ── Control flow ─────────────────────────────────────

        case OpCode::JUMP: {
            int16_t offset = (int16_t)(cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8));
            cf.ip += 2 + offset;
            break;
        }

        case OpCode::JUMP_IF_FALSE: {
            int16_t offset = (int16_t)(cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8));
            cf.ip += 2;
            Value cond = std::move(stack[--sp]);
            if (!cond.to_bool()) cf.ip += offset;
            break;
        }

        case OpCode::JUMP_IF_TRUE: {
            int16_t offset = (int16_t)(cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8));
            cf.ip += 2;
            Value cond = std::move(stack[--sp]);
            if (cond.to_bool()) cf.ip += offset;
            break;
        }

        case OpCode::JUMP_ABS: {
            uint16_t addr = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip = addr;
            break;
        }

        // ── Functions ────────────────────────────────────────

        case OpCode::CALL: {
            uint16_t name_idx = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            uint8_t argc = cf.chunk->code[cf.ip++];

            // ── Inline cache fast path for user functions ──────────
            {
                auto& cache = cf.chunk->call_cache;
                if (name_idx < cache.size()) {
                    uint64_t entry = cache[name_idx];
                    uint32_t gen = (uint32_t)(entry >> 32);
                    int32_t idx = (int32_t)(entry & 0xFFFFFFFFu);
                    if (gen == func_map_generation && idx >= 0) {
                        FuncProto& proto = (*func_protos)[idx];
                        if (argc != proto.arity) {
                            int eline = (trace_ip < cf.chunk->line_info.size())
                                ? cf.chunk->line_info[trace_ip] : 0;
                            throw jdError(ErrCode::WRONG_ARG_COUNT,
                                "Function '" + proto.name + "' expects " +
                                std::to_string(proto.arity) + " args, got " +
                                std::to_string(argc), eline);
                        }
                        if (proto.is_async) goto call_slow_path;
                        if (frames.size() >= 512)
                            throw jdError(ErrCode::STACK_OVERFLOW, "Call stack overflow (max 512 frames)");
                        size_t new_base = sp - argc;
                        size_t needed = new_base + proto.chunk.var_names.size();
                        while (needed >= stack.size()) stack.resize(stack.size() * 2);
                        if (sp < needed) sp = needed;
                        // NOTE: push_back may reallocate and invalidate cf.
                        // We `break` immediately afterwards so the next
                        // iteration refetches frames.back().
                        frames.push_back({&proto.chunk, 0, new_base});
                        break;
                    }
                }
            }
        call_slow_path:
            std::string func_name = cf.chunk->constants[name_idx].as_string()->data;

            // Check native functions first
            auto native_it = natives.find(func_name);
            if (native_it != natives.end()) {
                std::vector<Value> args(argc);
                for (int i = argc - 1; i >= 0; i--) {
                    args[i] = pop();
                }

                // Auto-vectorize: if any arg is an array and the function
                // is not array-aware, apply element-wise automatically
                static const std::unordered_map<std::string, bool> no_vectorize = {
                    {"ZEROS",1}, {"ONES",1}, {"__MAKE_UDT_ARRAY__",1},
                    {"IOTA",1}, {"RESHAPE",1}, {"LEN",1}, {"LENV",1}, {"PUSH",1}, {"POP",1},
                    {"TENSOR",1}, {"TYPEOF",1}, {"APPEND",1}, {"DIFF",1},
                    {"SUM",1}, {"PRODUCT",1}, {"MIN",1}, {"MAX",1}, {"ANY",1}, {"ALL",1},
                    {"SCAN",1}, {"SELECT",1}, {"FILTER",1}, {"REDUCE",1},
                    {"TAKE",1}, {"DROP",1}, {"REVERSE",1}, {"UNIQUE",1}, {"SHUFFLE",1},
                    {"FIND_IN_ARRAY",1}, {"NORMALIZE",1}, {"DISTANCE",1}, {"GRADE",1},
                    {"TRANSPOSE",1}, {"MATMUL",1}, {"MVLET",1}, {"STACK",1}, {"SLICE",1},
                    {"SOLVE",1}, {"INVERT",1}, {"CONVOLVE",1}, {"PLACE",1},
                    {"OUTER",1}, {"ROTATE",1}, {"SHIFT",1}, {"XSORT",1}, {"INTEGRATE",1},
                    {"GETENV$",1}, {"SETLOCALE",1}, {"TICK",1}, {"NOW",1},
                    {"DATE$",1}, {"TIME$",1}, {"CVDATE",1},
                    // DATEADD/DATEDIFF/FORMAT_DATE intentionally vectorize
                    // so `DATEDIFF("D", scalar, [d1,d2,d3])` returns [d-d1,
                    // d-d2, d-d3] element-wise.
                    {"MAP.EXISTS",1}, {"MAP.KEYS",1}, {"MAP.VALUES",1}, {"MAP.ITEMS",1},
                    {"MAP.SIZE",1}, {"MAP.DELETE",1}, {"MAP.CLEAR",1}, {"MAP.MERGE",1},
                    {"MAP.FROM",1}, {"JSON.PARSE$",1}, {"JSON.STRINGIFY$",1},
                    {"CLS",1}, {"LOCATE",1}, {"COLOR",1}, {"CURSOR",1}, {"SLEEP",1},
                    {"GETX",1}, {"GETY",1}, {"INKEY$",1}, {"WAITKEY$",1}, {"OPTION",1},
                    {"CLIPBOARD.SET",1}, {"CLIPBOARD.GET$",1},
                    {"SPLIT",1}, {"FORMAT$",1}, {"FRMV$",1}, {"INSERT$",1},
                    {"REPLACE$",1}, {"REVERSE$",1}, {"PACK$",1}, {"UNPACK",1},
                    {"TXTREADER$",1}, {"TXTWRITER",1}, {"BINREADER$",1}, {"BINWRITER",1},
                    {"CSVREADER",1}, {"CSVWRITER",1}, {"IIF",1},
                    {"JOIN",1}, {"REGEX_MATCH",1}, {"REGEX_REPLACE$",1},
                    {"REGEX.MATCH",1}, {"REGEX.FINDALL",1}, {"REGEX.REPLACE",1},
                    {"MEAN",1}, {"MEDIAN",1}, {"VARIANCE",1}, {"STDEV",1},
                    {"DOT",1}, {"CROSS",1}, {"CUMSUM",1}, {"CUMPROD",1},
                    {"HISTOGRAM",1}, {"LINSPACE",1}, {"FLATTEN",1}, {"ZIP",1},
                    {"ZEROS",1}, {"ONES",1}, {"RANGE",1}, {"COUNT",1}, {"INDEXOF",1},
                    {"ISNUM",1}, {"ISSTR",1}, {"ISARR",1}, {"ISMAP",1}, {"ISBOOL",1},
                    {"ISNONE",1}, {"ISNULL",1}, {"RANDOMSEED",1},
                    {"CODEC.BASE64_ENCODE$",1}, {"CODEC.BASE64_DECODE$",1},
                    {"CODEC.SHA256$",1}, {"CODEC.UUID$",1},
                    {"OS.GETOS",1}, {"OS.GETOS$",1}, {"OS.ARGS",1}, {"OS.EXEC",1},
                    {"OS.HOSTNAME$",1}, {"OS.IP$",1}, {"OS.LOAD",1}, {"OS.FEATURE",1},
                    {"DIR$",1}, {"DIR",1}, {"CD",1}, {"PWD",1}, {"MKDIR",1}, {"KILL",1},
                    {"PATH.JOIN$",1}, {"PATH.BASENAME$",1}, {"PATH.EXT$",1},
                    {"RECUR",1}, {"CLEAR_RECUR",1}, {"LIST_RECUR",1},
                    {"AWAIT",1}, {"THREAD.ISDONE",1}, {"THREAD.GETRESULT",1},
                    {"REACT_BIND",1}, {"UNREACT",1},
                    {"EXECUTE",1}, {"EVAL",1},
                    {"LOAD",1}, {"SAVE",1}, {"LIST",1}, {"HELP",1}, {"HELP$",1}, {"VARS",1},
                    {"SCREEN",1}, {"SCREENFLIP",1}, {"DRAWCOLOR",1}, {"SETFONT",1},
                    {"PSET",1}, {"LINE",1}, {"RECT",1}, {"CIRCLE",1}, {"ELLIPSE",1},
                    {"ROUNDED_RECT",1}, {"CIRCLE_SECTOR",1}, {"TEXT",1},
                    {"PLOTRAW",1}, {"TOGGLE_FULLSCREEN",1},
                    {"AUDIO.INIT",1}, {"AUDIO.LOADWAV",1}, {"AUDIO.LOADMUS",1},
                    {"AUDIO.PLAY",1}, {"AUDIO.PLAYMUS",1}, {"AUDIO.STOP",1},
                    {"AUDIO.STOPMUS",1}, {"AUDIO.VOLUME",1}, {"AUDIO.VOLUMEMUS",1},
                    {"AUDIO.PAUSE",1}, {"AUDIO.RESUME",1}, {"AUDIO.PAUSEMUS",1},
                    {"AUDIO.RESUMEMUS",1}, {"AUDIO.FREE",1}, {"AUDIO.FREEMUS",1},
                    {"AUDIO.CLOSE",1},
                    {"GFX.POLLEVENT",1}, {"GFX.KEYSTATE",1}, {"GFX.MOUSEX",1},
                    {"GFX.MOUSEY",1}, {"GFX.MOUSEBUTTON",1}, {"GFX.DELAY",1},
                    {"GFX.TICKS",1}, {"GFX.CLOSE",1}, {"GFX.LOADIMAGE",1},
                    {"GFX.DRAWIMAGE",1}, {"GFX.FREEIMAGE",1}, {"GFX.TEXTSIZE",1},
                    {"GFX.PLOT_POINTS",1}, {"GFX.PLOT_POINTS_TEX",1}, {"GFX.HSV_RGB",1},
                    {"MOUSEX",1}, {"MOUSEY",1}, {"MOUSEB",1},
                    {"TURTLE.FORWARD",1}, {"TURTLE.BACKWARD",1},
                    {"TURTLE.LEFT",1}, {"TURTLE.RIGHT",1},
                    {"TURTLE.PENUP",1}, {"TURTLE.PENDOWN",1},
                    {"TURTLE.SETPOS",1}, {"TURTLE.SETHEADING",1},
                    {"TURTLE.HOME",1}, {"TURTLE.DRAW",1}, {"TURTLE.CLEAR",1},
                    {"TURTLE.SET_COLOR",1},
                    {"JOY.COUNT",1}, {"JOY.NAME$",1}, {"JOY.BUTTON",1},
                    {"JOY.AXIS",1}, {"JOY.HAT",1},
                    {"SPRITE.LOAD",1}, {"SPRITE.POS",1}, {"SPRITE.MOVE",1},
                    {"SPRITE.DRAW",1}, {"SPRITE.DRAW_ALL",1}, {"SPRITE.DELETE",1},
                    {"SPRITE.GET_X",1}, {"SPRITE.GET_Y",1},
                    {"SPRITE.SCALE",1}, {"SPRITE.FLIP",1}, {"SPRITE.ALPHA",1},
                    {"SPRITE.VISIBLE",1}, {"SPRITE.ROTATE",1}, {"SPRITE.SET_ORIGIN",1},
                    {"SPRITE.COLLISION",1}, {"SPRITE.WIDTH",1}, {"SPRITE.HEIGHT",1},
                    {"SPRITE.ANIM",1}, {"SPRITE.PLAY",1}, {"SPRITE.STOP",1},
                    {"SPRITE.FRAME",1}, {"SPRITE.UPDATE",1}, {"SPRITE.PLAYING",1},
                    {"SPRITE.VELOCITY",1}, {"SPRITE.GET_VX",1}, {"SPRITE.GET_VY",1},
                    {"SPRITE.GROUP",1}, {"SPRITE.COLLISIONS",1}, {"SPRITE.COLLISION_FIRST",1},
                    {"TILEMAP.CREATE",1}, {"TILEMAP.DRAW",1}, {"TILEMAP.SET",1},
                    {"TILEMAP.GET",1}, {"TILEMAP.SIZE",1}, {"TILEMAP.COLLIDES",1},
                    {"TILEMAP.TILE_AT",1},
                    {"CAM.FOLLOW",1}, {"CAM.SET",1}, {"CAM.BOUNDS",1}, {"CAM.SHAKE",1},
                    {"CAM.X",1}, {"CAM.Y",1},
                    {"SPRITE.ZORDER",1}, {"SPRITE.GRAVITY",1}, {"SPRITE.ON_GROUND",1}, {"SPRITE.LAND",1},
                    {"PARTICLE.EMIT",1}, {"PARTICLE.DRAW",1}, {"PARTICLE.CLEAR",1}, {"PARTICLE.COUNT",1},
                    {"GFX.CAPTURE",1}, {"GFX.DRAW_CAPTURE",1},
                    {"GFX.FADE",1}, {"GFX.SAVE_SCREENSHOT",1}, {"GFX.SAVE_IMAGE",1},
                    {"GFX.DRAWIMAGE_REGION",1}, {"GFX.DRAWIMAGE_EX",1},
                    {"GFX.COLOR_TO_ALPHA",1},
                    {"__EVENT_ON",1}, {"__EVENT_RAISE",1},
                    {"__FFI_DECLARE",1},
                    {"AI.LOAD",1}, {"AI.RUN",1}, {"AI.FREE",1}, {"AI.INFO",1},
                    {"AI.LIST",1}, {"AI.TENSOR",1}, {"AI.SOFTMAX",1},
                    {"AI.ARGMAX",1}, {"AI.TOPK",1},
                    {"AI.LOAD_LLM",1}, {"AI.CHAT",1}, {"AI.CHAT_STREAM",1},
                    {"AI.CHAT_RAW",1}, {"AI.SET",1}, {"AI.TOKENIZE",1},
                    {"AI.DETOKENIZE",1}, {"AI.LLM_INFO",1}, {"AI.FREE_LLM",1},
                    {"AI.TOOL_ADD",1}, {"AI.TOOL_REMOVE",1}, {"AI.TOOL_CHAT",1},
                    {"AI.TOOL_LIST",1},
                    {"AI.CLEAR_HISTORY",1}, {"AI.GET_HISTORY",1},
                    {"AI.TOKEN_COUNT",1}, {"AI.COSINE_SIM",1}, {"AI.NORMALIZE",1},
                    {"AI.RAG_CREATE",1}, {"AI.RAG_ADD",1}, {"AI.RAG_ADD_FILE",1},
                    {"AI.RAG_ADD_DIR",1}, {"AI.RAG_SAVE",1}, {"AI.RAG_LOAD",1},
                    {"AI.RAG_SEARCH",1}, {"AI.RAG_QUERY",1}, {"AI.RAG_QUERY_FULL",1},
                    {"AI.RAG_QUERY_STREAM",1}, {"AI.RAG_BUILD_INDEX",1},
                    {"AI.RAG_INFO",1}, {"AI.RAG_CLEAR",1}, {"AI.RAG_FREE",1},
                    {"AI.EMBED",1}, {"AI.EMBED_LLM",1}, {"AI.SIMILARITY",1},
                    {"AI.LOAD_EMBEDDINGS",1},
                    {"AI.SET_GRAMMAR",1}, {"AI.CLEAR_GRAMMAR",1},
                    {"AI.SET_JSON_MODE",1}, {"AI.CHAT_JSON",1},
                    {"AI.CLASSIFIER_CREATE",1}, {"AI.CLASSIFIER_ADD",1},
                    {"AI.CLASSIFIER_ADD_BATCH",1}, {"AI.CLASSIFIER_PREDICT",1},
                    {"AI.CLASSIFIER_BUILD_INDEX",1},
                    {"AI.CLASSIFIER_SAVE",1}, {"AI.CLASSIFIER_LOAD",1},
                    {"AI.CLASSIFIER_INFO",1}, {"AI.CLASSIFIER_FREE",1},
                    {"SOUND.INIT",1}, {"SOUND.VOICE",1}, {"SOUND.SAMPLE",1},
                    {"SOUND.PLAY",1}, {"SOUND.RELEASE",1}, {"SOUND.STOP",1},
                    {"SOUND.SEQ",1}, {"SOUND.BPM",1},
                    {"SOUND.NOTE",1},
                    {"SOUND.GAIN",1}, {"SOUND.PAN",1}, {"SOUND.FILTER",1},
                    {"SOUND.EQ",1}, {"SOUND.LFO",1}, {"SOUND.FM",1},
                    {"SOUND.UNISON",1}, {"SOUND.BITCRUSH",1}, {"SOUND.RINGMOD",1},
                    {"SOUND.REVERBSEND",1}, {"SOUND.DELAYSEND",1}, {"SOUND.SIDECHAIN",1},
                    {"SOUND.SCALE",1}, {"SOUND.DELAY",1}, {"SOUND.REVERB",1},
                    {"SOUND.COMPRESSOR",1}, {"SOUND.DISTORTION",1},
                    {"SOUND.RESET",1}, {"SOUND.SHUTDOWN",1},
                    {"SOUND.GET_WAVE",1}, {"SOUND.GET_BUS_WAVE",1},
                    {"SFX.LOAD",1}, {"SFX.PLAY",1}, {"MUSIC.PLAY",1}, {"MUSIC.STOP",1},
                    {"GUI.BEGIN",1}, {"GUI.END",1}, {"GUI.BEGIN_CHILD",1}, {"GUI.END_CHILD",1},
                    {"GUI.COLLAPSING_HEADER",1}, {"GUI.TREE_NODE",1}, {"GUI.TREE_POP",1},
                    {"GUI.SAME_LINE",1}, {"GUI.SEPARATOR",1}, {"GUI.SEPARATOR_TEXT",1}, {"GUI.DUMMY",1},
                    {"GUI.TEXT",1}, {"GUI.BUTTON",1}, {"GUI.CHECKBOX",1}, {"GUI.RADIO",1},
                    {"GUI.SLIDER",1}, {"GUI.PROGRESS",1}, {"GUI.COLOR",1},
                    {"GUI.HELPMARKER",1}, {"GUI.TOOLTIP",1},
                    {"GUI.INPUT",1}, {"GUI.INPUT_INT",1}, {"GUI.INPUT_DOUBLE",1},
                    {"GUI.COMBO",1}, {"GUI.LISTBOX",1}, {"GUI.SELECTABLE",1},
                    {"GUI.BEGIN_MAIN_MENU_BAR",1}, {"GUI.END_MAIN_MENU_BAR",1},
                    {"GUI.BEGIN_MENU_BAR",1}, {"GUI.END_MENU_BAR",1},
                    {"GUI.BEGIN_MENU",1}, {"GUI.END_MENU",1}, {"GUI.MENU_ITEM",1},
                    {"GUI.OPEN_POPUP",1}, {"GUI.BEGIN_POPUP",1}, {"GUI.BEGIN_POPUP_MODAL",1},
                    {"GUI.END_POPUP",1}, {"GUI.CLOSE_CURRENT_POPUP",1},
                    {"GUI.BEGIN_TAB_BAR",1}, {"GUI.END_TAB_BAR",1},
                    {"GUI.BEGIN_TAB_ITEM",1}, {"GUI.END_TAB_ITEM",1},
                    {"GUI.PLOT_LINES",1}, {"GUI.PLOT_HISTOGRAM",1},
                    {"GUI.BEGIN_TABLE",1}, {"GUI.END_TABLE",1},
                    {"GUI.TABLE_SETUP_COLUMN",1}, {"GUI.TABLE_HEADERS_ROW",1},
                    {"GUI.TABLE_NEXT_ROW",1}, {"GUI.TABLE_SET_COLUMN_INDEX",1}, {"GUI.TABLE_NEXT_COLUMN",1},
                    {"GUI.THEME",1}, {"GUI.FLAG",1}, {"GUI.COL",1},
                    {"GUI.PUSH_STYLE_COLOR",1}, {"GUI.POP_STYLE_COLOR",1},
                    {"GUI.PUSH_ID",1}, {"GUI.POP_ID",1}, {"GUI.SHOW_FONT_ATLAS",1},
                    {"GUI.ITEM_RECT",1}, {"GUI.SET_CURSOR_SCREEN_POS",1},
                    {"GUI.SET_NEXT_ITEM_WIDTH",1}, {"GUI.SET_KEYBOARD_FOCUS",1},
                    {"GUI.ITEM_DEACTIVATED_AFTER_EDIT",1}
                };

                bool has_array = false;
                size_t arr_len = 0;
                if (no_vectorize.find(func_name) == no_vectorize.end()) {
                    for (auto& a : args) {
                        if (a.type == ValueType::ARRAY) {
                            has_array = true;
                            size_t n = a.as_array()->elements.size();
                            if (n > arr_len) arr_len = n;
                        }
                    }
                }

                if (has_array) {
                    // Apply function element-wise, recursively for N-D arrays
                    auto vectorize_call = [&](auto& self, const std::vector<Value>& cur_args) -> Value {
                        // Check if any arg is still an array
                        bool any_arr = false;
                        size_t alen = 0;
                        for (auto& a : cur_args) {
                            if (a.type == ValueType::ARRAY) {
                                any_arr = true;
                                size_t n = a.as_array()->elements.size();
                                if (n > alen) alen = n;
                            }
                        }
                        if (!any_arr) return native_it->second(cur_args);
                        Value result = Value::make_array();
                        auto* out = result.as_array();
                        out->elements.reserve(alen);
                        for (size_t ei = 0; ei < alen; ei++) {
                            std::vector<Value> elem_args(cur_args.size());
                            for (size_t j = 0; j < cur_args.size(); j++) {
                                if (cur_args[j].type == ValueType::ARRAY) {
                                    auto* arr = cur_args[j].as_array();
                                    elem_args[j] = arr->elements[ei % arr->elements.size()];
                                } else {
                                    elem_args[j] = cur_args[j];
                                }
                            }
                            out->elements.push_back(self(self, elem_args));
                        }
                        return result;
                    };
                    try {
                        push(vectorize_call(vectorize_call, args));
                    } catch (const jdError&) { throw; }
                    catch (const std::exception& e) {
                        throw jdError(ErrCode::RUNTIME_ERROR, func_name + ": " + e.what());
                    }
                } else {
                    try {
                        push(native_it->second(args));
                    } catch (const jdError&) {
                        throw;
                    } catch (const std::out_of_range&) {
                        int eline = (trace_ip < frame().chunk->line_info.size()) ? frame().chunk->line_info[trace_ip] : 0;
                        throw jdError(ErrCode::WRONG_ARG_COUNT,
                            func_name + ": wrong number of arguments (got " + std::to_string(argc) + ")", eline);
                    } catch (const std::bad_alloc&) {
                        int eline = (trace_ip < frame().chunk->line_info.size()) ? frame().chunk->line_info[trace_ip] : 0;
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            func_name + ": out of memory", eline);
                    } catch (const std::exception& e) {
                        int eline = (trace_ip < frame().chunk->line_info.size()) ? frame().chunk->line_info[trace_ip] : 0;
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            func_name + ": " + e.what(), eline);
                    } catch (...) {
                        int eline = (trace_ip < frame().chunk->line_info.size()) ? frame().chunk->line_info[trace_ip] : 0;
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            func_name + ": internal error", eline);
                    }
                }
                break;
            }

            // User-defined function (or indirect call via variable holding funcref)
            auto it = func_map.find(func_name);
            if (it == func_map.end()) {
                // Check if a variable holds a funcref (string) or lambda (array)
                {
                    Value* ref_val = nullptr;
                    // Check global
                    auto git = global_names.find(func_name);
                    if (git != global_names.end() && git->second < globals.size())
                        ref_val = &globals[git->second];
                    // Check local
                    if (!ref_val && frames.size() > 1) {
                        size_t base = frame().stack_base;
                        for (uint16_t vi = 0; vi < frame().chunk->var_names.size(); vi++) {
                            if (frame().chunk->var_names[vi] == func_name) {
                                ref_val = &stack[base + vi]; break;
                            }
                        }
                    }
                    if (ref_val) {
                        std::vector<Value> call_args(argc);
                        for (int i = argc - 1; i >= 0; i--) call_args[i] = pop();

                        if (ref_val->type == ValueType::STRING) {
                            // Simple funcref
                            push(call_function(ref_val->as_string()->data, call_args));
                            goto call_done;
                        }
                        if (ref_val->type == ValueType::ARRAY) {
                            // Lambda with captures: [func_name, cap1, cap2, ...]
                            auto* arr = ref_val->as_array();
                            if (!arr->elements.empty() && arr->elements[0].type == ValueType::STRING) {
                                std::string real_name = arr->elements[0].as_string()->data;
                                // Prepend captures to args
                                std::vector<Value> full_args;
                                for (size_t ci = 1; ci < arr->elements.size(); ci++)
                                    full_args.push_back(arr->elements[ci]);
                                for (auto& a : call_args) full_args.push_back(std::move(a));
                                push(call_function(real_name, full_args));
                                goto call_done;
                            }
                        }
                    }
                }
                // Method call fallback: "OBJ.X.Y.METHOD" → navigate chain, call last
                {
                    size_t dot = func_name.find('.');
                    if (dot != std::string::npos) {
                        std::string obj_name = func_name.substr(0, dot);
                        std::string rest = func_name.substr(dot + 1);

                        // First, look for OBJ as a LOCAL variable in the
                        // current function frame. The parser greedily turns
                        // `conn.Open(arg)` into a CALL to "conn.Open", which
                        // would otherwise fail inside a SUB where `conn` is
                        // a local — even though the same call works at the
                        // top level via the global lookup below.
                        auto ci_eq = [](const std::string& a, const std::string& b) {
                            if (a.size() != b.size()) return false;
                            for (size_t i = 0; i < a.size(); i++)
                                if (std::toupper((unsigned char)a[i]) !=
                                    std::toupper((unsigned char)b[i])) return false;
                            return true;
                        };
                        bool found = false;
                        Value obj;
                        if (frames.size() > 1) {
                            auto& var_names = frame().chunk->var_names;
                            for (size_t li = 0; li < var_names.size(); li++) {
                                if (ci_eq(var_names[li], obj_name)) {
                                    size_t abs = frame().stack_base + li;
                                    if (abs < stack.size() &&
                                        stack[abs].type == ValueType::OBJECT) {
                                        obj = stack[abs];
                                        found = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!found) {
                            auto oit = global_names.find(obj_name);
                            if (oit != global_names.end() && oit->second < globals.size() &&
                                globals[oit->second].type == ValueType::OBJECT) {
                                obj = globals[oit->second];
                                found = true;
                            }
                        }

                        if (found) {

                            // Navigate intermediate dots: "X.Y.METHOD" → get X, get Y, call METHOD
                            size_t rdot = rest.rfind('.');
                            if (rdot != std::string::npos) {
                                // Navigate to the penultimate object
                                std::string nav = rest.substr(0, rdot);
                                rest = rest.substr(rdot + 1);
                                size_t pos = 0;
                                while (pos < nav.size()) {
                                    size_t next = nav.find('.', pos);
                                    std::string part = (next == std::string::npos) ? nav.substr(pos) : nav.substr(pos, next - pos);
#ifdef COM
                                    Value nr;
                                    if (com_try_get_field(obj, part, nr)) { obj = nr; }
                                    else
#endif
                                    {
                                        Value* f = obj.as_object()->get(part);
                                        if (f) obj = *f; else break;
                                    }
                                    pos = (next == std::string::npos) ? nav.size() : next + 1;
                                }
                            }
                            std::string method = rest;
#ifdef COM
                            // Try COM method call
                            {
                                std::vector<Value> com_args(argc);
                                for (int i = argc - 1; i >= 0; i--) com_args[i] = stack[sp - argc + i];
                                Value com_result;
                                if (com_try_call_method(obj, method, com_args, com_result)) {
                                    for (int i = 0; i < argc; i++) pop();
                                    push(std::move(com_result));
                                    goto call_done;
                                }
                            }
#endif
                            Value* type_val = obj.as_object()->get("__TYPE__");
                            if (type_val && type_val->type == ValueType::STRING) {
                                std::string type_method = type_val->as_string()->data + "." + method;
                                std::vector<Value> call_args(argc + 1);
                                call_args[0] = obj; // THIS
                                for (int i = argc - 1; i >= 0; i--) call_args[i + 1] = pop();
                                push(call_function(type_method, call_args));
                                goto call_done;
                            }
                        }
                    }
                }
                {
                    int eline = (trace_ip < frame().chunk->line_info.size()) ? frame().chunk->line_info[trace_ip] : 0;
                    throw jdError(ErrCode::UNDEFINED_FUNCTION, "Undefined function: " + func_name, eline);
                }
            call_done: break;
            }
            FuncProto& proto = (*func_protos)[it->second];
            if (argc != proto.arity) {
                throw std::runtime_error("Function '" + func_name + "' expects " +
                    std::to_string(proto.arity) + " args, got " + std::to_string(argc));
            }

            // Populate the inline cache so subsequent calls at this site hit
            // the fast path. Only cache non-async functions.
            if (!proto.is_async) {
                auto& cache = frame().chunk->call_cache;
                if (name_idx >= cache.size()) cache.resize(name_idx + 1, 0);
                cache[name_idx] =
                    ((uint64_t)func_map_generation << 32) |
                    (uint64_t)(uint32_t)(int32_t)it->second;
            }

            // ASYNC function → run in background thread
            if (proto.is_async) {
                std::vector<Value> async_args(argc);
                for (int i = argc - 1; i >= 0; i--) async_args[i] = pop();

                int task_id = g_async_next_id++;
                auto task = std::make_shared<AsyncTask>();

                // Copy function registry and globals for the async VM
                auto funcs_copy = std::make_shared<std::vector<FuncProto>>(
                    func_protos ? *func_protos : owned_funcs);
                auto globals_copy = std::make_shared<std::vector<Value>>(globals);
                auto gnames_copy = std::make_shared<std::unordered_map<std::string, uint16_t>>(global_names);
                auto fmap_copy = std::make_shared<std::unordered_map<std::string, size_t>>(func_map);
                std::string fn = func_name;

                task->thread = std::thread([task, funcs_copy, fn, async_args]() {
                    install_seh_translator_for_this_thread();
                    try {
                        VM async_vm;
                        // Register all functions via restore_state
                        VMState st;
                        st.functions = *funcs_copy;
                        for (size_t i = 0; i < st.functions.size(); i++) {
                            st.func_map[st.functions[i].name] = i;
                        }
                        async_vm.restore_state(st);
                        task->result = async_vm.call_function(fn, async_args);
                    } catch (const std::exception& e) {
                        task->result = Value::make_string("ERROR: " + std::string(e.what()));
                    }
                    task->done = true;
                });
                task->thread.detach();

                { std::lock_guard<std::mutex> lock(g_async_mutex);
                  g_async_tasks[task_id] = task; }

                push(Value::make_i64(task_id));
                break;
            }

            // Set up new frame (synchronous call)
            if (frames.size() >= 512)
                throw jdError(ErrCode::STACK_OVERFLOW, "Call stack overflow (max 512 frames)");
            size_t new_base = sp - argc;
            frames.push_back({&proto.chunk, 0, new_base});

            // Ensure enough stack space for locals
            size_t needed = new_base + proto.chunk.var_names.size();
            while (needed >= stack.size()) stack.resize(stack.size() * 2);
            if (sp < needed) sp = needed;
            break;
        }

        case OpCode::CALL_METHOD: {
            // Stack: [object, arg1, ..., argN]
            uint16_t name_idx = read_u16();
            uint8_t argc = read_byte();
            std::string method = frame().chunk->constants[name_idx].as_string()->data;

            // Pop args
            std::vector<Value> args(argc);
            for (int i = argc - 1; i >= 0; i--) args[i] = pop();
            // Pop object
            Value obj = pop();

            if (obj.type == ValueType::OBJECT) {
#ifdef COM
                // Try COM method call
                Value com_result;
                if (com_try_call_method(obj, method, args, com_result)) {
                    push(std::move(com_result));
                    break;
                }
#endif
                // Try UDT method call
                Value* type_val = obj.as_object()->get("__TYPE__");
                if (type_val && type_val->type == ValueType::STRING) {
                    std::string type_method = type_val->as_string()->data + "." + method;
                    std::vector<Value> call_args;
                    call_args.push_back(obj); // THIS
                    for (auto& a : args) call_args.push_back(std::move(a));
                    push(call_function(type_method, call_args));
                    break;
                }
                // Try as regular field that is a funcref
                Value* f = obj.as_object()->get(method);
                if (f) {
                    push(call_funcref(*f, args));
                    break;
                }
            }
            // Fallback: if object has no method, try as zero-arg property get
            if (argc == 0 && obj.type == ValueType::OBJECT) {
#ifdef COM
                Value com_result;
                if (com_try_get_field(obj, method, com_result)) {
                    push(std::move(com_result));
                    break;
                }
#endif
            }
            throw std::runtime_error("Cannot call method '" + method + "' on value");
        }

        case OpCode::RETURN_VOID: {
            size_t base = cf.stack_base;
            frames.pop_back();
            sp = base;
            push(Value::make_none());
            if (frames.size() <= min_frame_depth) return;
            break;
        }

        case OpCode::RETURN_VAL: {
            // Return value is currently at stack[sp-1]. After return we need
            // it to sit at stack[frame_base], with sp = frame_base + 1 and the
            // frame popped. Avoid the extra pop+push dance.
            size_t base = cf.stack_base;
            stack[base] = std::move(stack[sp - 1]);
            sp = base + 1;
            frames.pop_back();
            // cf is now dangling — break so next iteration refetches
            if (frames.size() <= min_frame_depth) return;
            break;
        }

        // ── I/O ──────────────────────────────────────────────

        case OpCode::PRINT: {
            Value v = pop();
            emit(v.to_string());
            break;
        }

        case OpCode::PRINT_NL: {
            emit("\n");
            break;
        }

        case OpCode::PRINT_SPACE: {
            emit(" ");
            break;
        }

        case OpCode::INPUT_VAR: {
            uint16_t name_idx = read_u16();
            std::string input;
            is_waiting_input = true;
            std::getline(std::cin, input);
            is_waiting_input = false;

            Value val;
            // Try to parse as number
            try {
                size_t pos;
                int64_t iv = std::stoll(input, &pos);
                if (pos == input.size()) {
                    val = Value::make_i64(iv);
                } else {
                    double dv = std::stod(input, &pos);
                    if (pos == input.size()) {
                        val = Value::make_f64(dv);
                    } else {
                        val = Value::make_string(input);
                    }
                }
            } catch (...) {
                val = Value::make_string(input);
            }

            // Store to global or local
            if (frames.size() <= 1) {
                uint16_t slot = ensure_global(name_idx);
                if (slot >= globals.size()) globals.resize(slot + 1);
                globals[slot] = val;
            } else {
                size_t abs_slot = frame().stack_base + name_idx;
                if (abs_slot >= stack.size()) stack.resize(abs_slot + 1);
                stack[abs_slot] = val;
            }
            break;
        }

        // ── Stack ────────────────────────────────────────────

        case OpCode::POP:
            pop();
            break;

        case OpCode::DUP:
            push(peek());
            break;

        // ── Arrays ───────────────────────────────────────────

        case OpCode::MAKE_ARRAY: {
            uint16_t count = read_u16();
            Value arr = Value::make_array();
            auto* obj = arr.as_array();
            obj->elements.resize(count);
            for (int i = count - 1; i >= 0; i--) {
                obj->elements[i] = pop();
            }
            push(std::move(arr));
            break;
        }

        case OpCode::MAKE_MAP: {
            uint16_t count = read_u16();
            Value map = Value::make_object();
            auto* o = map.as_object();
            // Stack has key1,val1,...,keyN,valN — pop in reverse
            std::vector<std::pair<std::string, Value>> pairs(count);
            for (int i = count - 1; i >= 0; i--) {
                Value val = pop();
                Value key = pop();
                pairs[i] = {key.as_string()->data, std::move(val)};
            }
            for (auto& [k, v] : pairs) o->set(k, std::move(v));
            push(std::move(map));
            break;
        }

        case OpCode::INDEX_GET: {
            Value idx = pop();
            Value container = pop();
            if (container.type == ValueType::ARRAY) {
                auto* arr = container.as_array();
                // Vectorised gather: idx is an array of indices → return gathered array
                if (idx.type == ValueType::ARRAY) {
                    auto& idxs = idx.as_array()->elements;
                    auto result = std::make_shared<ArrayObj>();
                    result->elements.reserve(idxs.size());
                    for (auto& ix : idxs) {
                        int64_t i = ix.to_int();
                        if (i < 0 || i >= (int64_t)arr->elements.size()) {
                            int eln = (trace_ip < cf.chunk->line_info.size()) ? cf.chunk->line_info[trace_ip] : 0;
                            throw jdError(ErrCode::INDEX_OUT_OF_RANGE, "Array index out of bounds: " + std::to_string(i), eln);
                        }
                        result->elements.push_back(arr->elements[i]);
                    }
                    Value rv = Value::make_array();
                    rv.as_array()->elements = std::move(result->elements);
                    push(std::move(rv));
                } else {
                    int64_t i = idx.to_int();
                    if (i < 0 || i >= (int64_t)arr->elements.size()) {
                        int eln = (trace_ip < cf.chunk->line_info.size()) ? cf.chunk->line_info[trace_ip] : 0;
                        throw jdError(ErrCode::INDEX_OUT_OF_RANGE, "Array index out of bounds: " + std::to_string(i), eln);
                    }
                    push(arr->elements[i]);
                }
            } else if (container.type == ValueType::STRING) {
                int64_t i = idx.to_int();
                auto* s = container.as_string();
                if (i < 0 || i >= (int64_t)s->data.size()) {
                    throw std::runtime_error("String index out of bounds");
                }
                push(Value::make_string(std::string(1, s->data[i])));
            } else if (container.type == ValueType::TENSOR) {
                int64_t i = idx.to_int();
                auto* t = container.as_tensor();
                if (i < 0 || i >= (int64_t)t->data.size()) {
                    throw std::runtime_error("Tensor index out of bounds");
                }
                push(Value::make_f64(t->data[i]));
            } else if (container.type == ValueType::OBJECT) {
                std::string key = idx.to_string();
#ifdef COM
                Value com_result;
                if (com_try_get_field(container, key, com_result)) {
                    push(std::move(com_result));
                } else
#endif
                {
                    Value* f = container.as_object()->get(key);
                    push(f ? *f : Value::make_none());
                }
            } else {
                throw std::runtime_error("Cannot index into " + container.to_string());
            }
            break;
        }

        case OpCode::INDEX_SET: {
            Value val = pop();
            Value idx = pop();
            Value container = pop();
            if (container.type == ValueType::ARRAY) {
                // ── Vectorised scatter: idx is an ARRAY of indices ──
                // arr[i_arr] = val_arr  →  for each k: arr[i_arr[k]] = val_arr[k]
                // arr[i_arr] = scalar   →  for each k: arr[i_arr[k]] = scalar
                if (idx.type == ValueType::ARRAY) {
                    auto* arr = container.as_array();
                    auto& idxs = idx.as_array()->elements;
                    if (val.type == ValueType::ARRAY) {
                        auto& vs = val.as_array()->elements;
                        for (size_t k = 0; k < idxs.size(); k++) {
                            int64_t pos = idxs[k].to_int();
                            if (pos < 0 || pos >= (int64_t)arr->elements.size())
                                throw jdError(ErrCode::INDEX_OUT_OF_RANGE,
                                    "scatter: index " + std::to_string(pos) + " out of bounds");
                            arr->elements[(size_t)pos] = vs[k % vs.size()];
                        }
                    } else {
                        for (size_t k = 0; k < idxs.size(); k++) {
                            int64_t pos = idxs[k].to_int();
                            if (pos < 0 || pos >= (int64_t)arr->elements.size())
                                throw jdError(ErrCode::INDEX_OUT_OF_RANGE,
                                    "scatter: index " + std::to_string(pos) + " out of bounds");
                            arr->elements[(size_t)pos] = val;
                        }
                    }
                    break;
                }
                int64_t i = idx.to_int();
                auto* arr = container.as_array();
                if (i < 0 || i >= (int64_t)arr->elements.size()) {
                    throw jdError(ErrCode::INDEX_OUT_OF_RANGE, "Array index out of bounds: " + std::to_string(i));
                }
                Value& target = arr->elements[i];

                if (target.type == ValueType::ARRAY && val.type != ValueType::ARRAY) {
                    // Scalar broadcast: fill all leaf elements recursively
                    std::function<void(Value&, const Value&)> broadcast =
                        [&](Value& node, const Value& scalar) {
                        if (node.type == ValueType::ARRAY) {
                            for (auto& elem : node.as_array()->elements)
                                broadcast(elem, scalar);
                        } else {
                            node = scalar;
                        }
                    };
                    broadcast(target, val);
                } else if (target.type == ValueType::ARRAY && val.type == ValueType::ARRAY) {
                    // Cyclic vectorized assignment: flatten source, cycle into leaf slots
                    std::vector<Value> src_flat;
                    std::function<void(const Value&)> flatten_src =
                        [&](const Value& v) {
                        if (v.type == ValueType::ARRAY)
                            for (auto& e : v.as_array()->elements) flatten_src(e);
                        else
                            src_flat.push_back(v);
                    };
                    flatten_src(val);

                    if (!src_flat.empty()) {
                        size_t si = 0;
                        std::function<void(Value&)> fill = [&](Value& node) {
                            if (node.type == ValueType::ARRAY) {
                                for (auto& elem : node.as_array()->elements)
                                    fill(elem);
                            } else {
                                node = src_flat[si % src_flat.size()];
                                si++;
                            }
                        };
                        fill(target);
                    }
                } else {
                    // Simple assignment
                    target = std::move(val);
                }
            } else if (container.type == ValueType::TENSOR) {
                int64_t i = idx.to_int();
                auto* t = container.as_tensor();
                if (i < 0 || i >= (int64_t)t->data.size()) {
                    throw std::runtime_error("Tensor index out of bounds");
                }
                t->data[i] = val.to_double();
            } else if (container.type == ValueType::OBJECT) {
#ifdef COM
                if (com_try_set_field(container, idx.to_string(), val)) { /* COM set */ }
                else
#endif
                container.as_object()->set(idx.to_string(), std::move(val));
            } else {
                throw std::runtime_error("Cannot index-assign into this type");
            }
            break;
        }

        // ── Objects ──────────────────────────────────────────

        case OpCode::GET_FIELD: {
            uint16_t name_idx = read_u16();
            Value obj_val = pop();
            std::string field = frame().chunk->constants[name_idx].as_string()->data;
            if (obj_val.type == ValueType::OBJECT) {
#ifdef COM
                Value com_r;
                if (com_try_get_field(obj_val, field, com_r)) {
                    push(std::move(com_r));
                } else
#endif
                {
                    Value* f = obj_val.as_object()->get(field);
                    push(f ? *f : Value::make_none());
                }
            } else {
                throw std::runtime_error("Cannot access field on non-object");
            }
            break;
        }

        case OpCode::SET_FIELD: {
            uint16_t name_idx = read_u16();
            Value val = pop();
            Value obj_val = pop();
            std::string field = frame().chunk->constants[name_idx].as_string()->data;
            if (obj_val.type == ValueType::OBJECT) {
#ifdef COM
                if (com_try_set_field(obj_val, field, val)) { break; }
#endif
                obj_val.as_object()->set(field, std::move(val));
            } else {
                throw std::runtime_error("Cannot set field on non-object");
            }
            break;
        }

        // ── Type cast ────────────────────────────────────────

        case OpCode::CAST: {
            uint8_t target = read_byte();
            Value v = pop();
            push(cast_value(v, static_cast<ValueType>(target)));
            break;
        }

        case OpCode::STR_CONCAT: {
            Value b = pop();
            Value a = pop();
            push(Value::make_string(a.to_string() + b.to_string()));
            break;
        }

        // ── Exception handling opcodes ────────────────────────

        case OpCode::SETUP_TRY: {
            uint16_t offset = read_u16();
            size_t catch_addr = frame().ip + offset;
            try_handlers.push_back({catch_addr, sp, frames.size()});
            break;
        }

        case OpCode::POP_TRY: {
            if (!try_handlers.empty()) try_handlers.pop_back();
            break;
        }

        case OpCode::THROW_OP: {
            Value msg = pop();
            // Get current line from line_info
            int err_line = 0;
            if (frame().ip > 0 && frame().ip - 1 < frame().chunk->line_info.size())
                err_line = frame().chunk->line_info[frame().ip - 1];
            throw std::runtime_error(msg.to_string());
        }

        case OpCode::STOP_OP: {
            int stop_line = 0;
            if (frame().ip > 0 && frame().ip - 1 < frame().chunk->line_info.size())
                stop_line = frame().chunk->line_info[frame().ip - 1];
            emit("STOP at line " + std::to_string(stop_line) + ". Type RESUME to continue.\n");
            is_stopped = true;
            return;
        }

        case OpCode::HALT:
            is_stopped = false;
            // Natural end-of-chunk marker. Sub-runs (REPL/EXECUTE/EVAL)
            // complete normally and the outer program continues. Only the
            // outermost run shuts down the GFX/audio subsystems and signals
            // the DAP client.
            if (subrun_depth == 0) {
                if (debug && debug->dap) debug->dap->send_program_ended_message();
#ifdef GFX
                {
                    extern void gfx_shutdown();
                    extern void sound_shutdown();
                    sound_shutdown();
                    gfx_shutdown();
                }
#endif
            }
            return;

        case OpCode::END_PROGRAM:
            // User `END` statement — terminate the whole program,
            // unwinding any nested call_function() (e.g. an event handler
            // calling END from inside the main DO loop) and any sub-run
            // (REPL `run` command goes through run_code()).
            is_stopped = false;
            is_halted = true;
            if (debug && debug->dap) debug->dap->send_program_ended_message();
#ifdef GFX
            {
                extern void gfx_shutdown();
                extern void sound_shutdown();
                sound_shutdown();
                gfx_shutdown();
            }
#endif
            return;

        // ── Multi-index assignment (NumPy-style fancy indexing) ──
        //   container[i1][i2]...[iN] = val
        // Stack: [container, i1, i2, ..., iN, val]
        // If any index is an ARRAY, iterates in parallel: all array
        // indices must be the same length. Scalar indices are broadcast.
        // val may be a scalar (broadcast) or an ARRAY of the same length.
        case OpCode::MULTI_INDEX_SET: {
            uint8_t count = cf.chunk->code[cf.ip++];
            Value val = std::move(stack[--sp]);
            std::vector<Value> idxs(count);
            for (int i = count - 1; i >= 0; i--) idxs[i] = std::move(stack[--sp]);
            Value container = std::move(stack[--sp]);

            // Detect vectorized path: any index is an array?
            int vec_len = -1;
            for (auto& ix : idxs) {
                if (ix.type == ValueType::ARRAY) {
                    int n = (int)ix.as_array()->elements.size();
                    if (vec_len < 0) vec_len = n;
                    else if (n != vec_len) {
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            "Multi-index assignment: index arrays must have the same length");
                    }
                }
            }

            // Navigate to the leaf using mixed array[int]/object{string} indices
            auto assign_mixed = [&](const std::vector<Value>& path, const Value& v) {
                Value* cur = &container;
                for (int k = 0; k + 1 < (int)path.size(); k++) {
                    const auto& idx = path[k];
                    if (cur->type == ValueType::ARRAY) {
                        auto& els = cur->as_array()->elements;
                        int64_t p = idx.to_int();
                        if (p < 0 || p >= (int64_t)els.size())
                            throw jdError(ErrCode::INDEX_OUT_OF_RANGE,
                                "Multi-index: out of bounds " + std::to_string(p));
                        cur = &els[(size_t)p];
                    } else if (cur->type == ValueType::OBJECT) {
                        std::string key = idx.to_string();
                        Value* f = cur->as_object()->get(key);
                        if (!f) {
                            cur->as_object()->set(key, Value::make_none());
                            f = cur->as_object()->get(key);
                        }
                        cur = f;
                    } else {
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            "Multi-index assignment: intermediate is not array or object");
                    }
                }
                // Set value on the leaf
                const auto& last_idx = path.back();
                if (cur->type == ValueType::ARRAY) {
                    auto& leaf = cur->as_array()->elements;
                    int64_t li = last_idx.to_int();
                    if (li < 0 || li >= (int64_t)leaf.size())
                        throw jdError(ErrCode::INDEX_OUT_OF_RANGE,
                            "Multi-index: out of bounds " + std::to_string(li));
                    leaf[(size_t)li] = v;
                } else if (cur->type == ValueType::OBJECT) {
                    cur->as_object()->set(last_idx.to_string(), v);
                } else {
                    throw jdError(ErrCode::RUNTIME_ERROR,
                        "Multi-index assignment: leaf is not array or object");
                }
            };

            if (vec_len < 0) {
                // All indices are scalar
                assign_mixed(idxs, val);
            } else {
                // Vectorized: iterate parallel
                bool val_is_array = (val.type == ValueType::ARRAY);
                if (val_is_array && (int)val.as_array()->elements.size() != vec_len) {
                    throw jdError(ErrCode::RUNTIME_ERROR,
                        "Multi-index assignment: value array length mismatch");
                }
                std::vector<Value> path(count);
                for (int i = 0; i < vec_len; i++) {
                    for (int k = 0; k < count; k++) {
                        if (idxs[k].type == ValueType::ARRAY) {
                            path[k] = idxs[k].as_array()->elements[i];
                        } else {
                            path[k] = idxs[k];
                        }
                    }
                    const Value& v = val_is_array
                        ? val.as_array()->elements[i]
                        : val;
                    assign_mixed(path, v);
                }
            }

            // Container is modified in place (arrays have reference semantics
            // via intrusive refcount). The statement does not leave a value
            // on the stack — same convention as INDEX_SET.
            break;
        }

        // ── Superinstructions ────────────────────────────────
        case OpCode::MARK_CONST: {
            uint16_t name_idx = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            const std::string& cname = cf.chunk->var_names[name_idx];
            const_globals.insert(cname);
            break;
        }
        case OpCode::NOP:
            break;

        // LOAD_VAR slot, LOAD_CONST idx, ADD  (INT64 fast path, fallback to general)
        case OpCode::LOAD_VAR_ADD_CONST:
        case OpCode::LOAD_VAR_SUB_CONST:
        case OpCode::LOAD_VAR_MUL_CONST: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            uint16_t cidx = cf.chunk->code[cf.ip + 2] | (cf.chunk->code[cf.ip + 3] << 8);
            cf.ip += 4 + 2; // 2 slot + 2 const_idx + 2 NOP padding
            const Value& a = stack[cf.stack_base + slot];
            const Value& c = cf.chunk->constants[cidx];
            if (sp >= stack.size()) stack.resize(stack.size() * 2);
            if (a.type == ValueType::INT64 && c.type == ValueType::INT64) {
                int64_t x = a.i64, y = c.i64, r;
                bool overflow = false;
                switch (op) {
                    case OpCode::LOAD_VAR_ADD_CONST: {
                        uint64_t ur = (uint64_t)x + (uint64_t)y;
                        r = (int64_t)ur;
                        overflow = ((x ^ r) & (y ^ r)) < 0;
                        break;
                    }
                    case OpCode::LOAD_VAR_SUB_CONST: {
                        uint64_t ur = (uint64_t)x - (uint64_t)y;
                        r = (int64_t)ur;
                        overflow = ((x ^ y) & (x ^ r)) < 0;
                        break;
                    }
                    case OpCode::LOAD_VAR_MUL_CONST: {
                        r = (int64_t)((uint64_t)x * (uint64_t)y);
                        if (x != 0 && r / x != y) overflow = true;
                        break;
                    }
                    default: r = 0;
                }
                Value& out = stack[sp++];
                if (overflow) {
                    out.type = ValueType::FLOAT64;
                    double dx = (double)x, dy = (double)y, dr;
                    switch (op) {
                        case OpCode::LOAD_VAR_ADD_CONST: dr = dx + dy; break;
                        case OpCode::LOAD_VAR_SUB_CONST: dr = dx - dy; break;
                        case OpCode::LOAD_VAR_MUL_CONST: dr = dx * dy; break;
                        default: dr = 0; break;
                    }
                    out.f64 = dr;
                } else {
                    out.type = ValueType::INT64;
                    out.i64 = r;
                }
            } else {
                // Slow path: must mirror the full ADD/SUB/MUL handler so that
                // strings, arrays, and mixed types behave identically whether
                // the peephole fused the sequence or not.
                OpCode arith_op = (op == OpCode::LOAD_VAR_ADD_CONST) ? OpCode::ADD :
                                  (op == OpCode::LOAD_VAR_SUB_CONST) ? OpCode::SUB : OpCode::MUL;
                Value result;
                if (a.type == ValueType::ARRAY || c.type == ValueType::ARRAY) {
                    result = array_arithmetic(a, c, arith_op);
                } else if (a.type == ValueType::STRING || c.type == ValueType::STRING) {
                    result = scalar_binop(a, c, arith_op);
                } else {
                    result = arithmetic(a, c, arith_op);
                }
                stack[sp++] = std::move(result);
            }
            break;
        }

        // LOAD_VAR slot, LOAD_CONST idx, CMP_*
        case OpCode::LOAD_VAR_CMP_LT_CONST:
        case OpCode::LOAD_VAR_CMP_GT_CONST:
        case OpCode::LOAD_VAR_CMP_LE_CONST:
        case OpCode::LOAD_VAR_CMP_GE_CONST:
        case OpCode::LOAD_VAR_CMP_EQ_CONST:
        case OpCode::LOAD_VAR_CMP_NE_CONST: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            uint16_t cidx = cf.chunk->code[cf.ip + 2] | (cf.chunk->code[cf.ip + 3] << 8);
            cf.ip += 4 + 2;
            const Value& a = stack[cf.stack_base + slot];
            const Value& c = cf.chunk->constants[cidx];
            if (sp >= stack.size()) stack.resize(stack.size() * 2);
            if (a.type == ValueType::INT64 && c.type == ValueType::INT64) {
                int64_t x = a.i64, y = c.i64;
                bool r;
                switch (op) {
                    case OpCode::LOAD_VAR_CMP_LT_CONST: r = (x <  y); break;
                    case OpCode::LOAD_VAR_CMP_GT_CONST: r = (x >  y); break;
                    case OpCode::LOAD_VAR_CMP_LE_CONST: r = (x <= y); break;
                    case OpCode::LOAD_VAR_CMP_GE_CONST: r = (x >= y); break;
                    case OpCode::LOAD_VAR_CMP_EQ_CONST: r = (x == y); break;
                    case OpCode::LOAD_VAR_CMP_NE_CONST: r = (x != y); break;
                    default: r = false;
                }
                Value& out = stack[sp++];
                out.type = ValueType::BOOLEAN;
                out.boolean = r;
            } else {
                OpCode cmp_op;
                switch (op) {
                    case OpCode::LOAD_VAR_CMP_LT_CONST: cmp_op = OpCode::CMP_LT; break;
                    case OpCode::LOAD_VAR_CMP_GT_CONST: cmp_op = OpCode::CMP_GT; break;
                    case OpCode::LOAD_VAR_CMP_LE_CONST: cmp_op = OpCode::CMP_LE; break;
                    case OpCode::LOAD_VAR_CMP_GE_CONST: cmp_op = OpCode::CMP_GE; break;
                    case OpCode::LOAD_VAR_CMP_EQ_CONST: cmp_op = OpCode::CMP_EQ; break;
                    default: cmp_op = OpCode::CMP_NE; break;
                }
                // Element-wise comparison, RECURSIVE so 2-D matrices keep
                // their shape — matches the unfused CMP_EQ handler.
                std::function<Value(const Value&, const Value&)> rec =
                    [&](const Value& la, const Value& lb) -> Value {
                    if (la.type == ValueType::ARRAY || lb.type == ValueType::ARRAY) {
                        Value result = Value::make_array();
                        auto* out = result.as_array();
                        if (la.type == ValueType::ARRAY && lb.type == ValueType::ARRAY) {
                            auto& aa = la.as_array()->elements;
                            auto& bb = lb.as_array()->elements;
                            size_t len = std::min(aa.size(), bb.size());
                            out->elements.reserve(len);
                            for (size_t i = 0; i < len; i++)
                                out->elements.push_back(rec(aa[i], bb[i]));
                        } else if (la.type == ValueType::ARRAY) {
                            for (auto& e : la.as_array()->elements)
                                out->elements.push_back(rec(e, lb));
                        } else {
                            for (auto& e : lb.as_array()->elements)
                                out->elements.push_back(rec(la, e));
                        }
                        return result;
                    }
                    return compare(la, lb, cmp_op);
                };
                stack[sp++] = rec(a, c);
            }
            break;
        }

        // LOAD_VAR slot, RETURN_VAL
        case OpCode::LOAD_VAR_RETURN: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2 + 1; // slot + 1 NOP padding
            size_t base = cf.stack_base;
            stack[base] = stack[base + slot]; // copy local to frame-base slot
            sp = base + 1;
            frames.pop_back();
            if (frames.size() <= min_frame_depth) return;
            break;
        }

        default:
            throw std::runtime_error("Unknown opcode: " + std::to_string(static_cast<int>(op)));
        }

      } catch (const std::exception& e) {
        // Check if there's a TRY handler (catches runtime_error, out_of_range, logic_error, etc.)
        if (!try_handlers.empty()) {
            auto handler = try_handlers.back();
            try_handlers.pop_back();

            // Get error line
            int err_line = 0;
            if (!frames.empty() && frame().ip > 0 && frame().ip <= frame().chunk->line_info.size())
                err_line = frame().chunk->line_info[frame().ip - 1];

            // Restore VM state
            while (frames.size() > handler.saved_frame_count) frames.pop_back();
            sp = handler.saved_sp;

            // Set error variables
            set_global("ERR", Value::make_i64(1));
            set_global("ERL", Value::make_i64(err_line));
            set_global("ERRMSG$", Value::make_string(e.what()));
            set_global("STACK$", Value::make_string("line " + std::to_string(err_line)));

            // Jump to catch block
            frame().ip = handler.catch_addr;
        } else {
            // Re-throw with line number so the caller can display it
            int err_line = 0;
            if (!frames.empty() && frame().ip > 0 && frame().ip <= frame().chunk->line_info.size())
                err_line = frame().chunk->line_info[frame().ip - 1];
            // If it's already a jdError, preserve code and add line if missing
            if (auto* je = dynamic_cast<const jdError*>(&e)) {
                throw jdError(je->code, je->what(), je->line > 0 ? je->line : err_line);
            }
            throw jdError(ErrCode::RUNTIME_ERROR, e.what(), err_line);
        }
      }
    }
}

// ── Arithmetic helper ────────────────────────────────────────

Value VM::arithmetic(const Value& a, const Value& b, OpCode op) {
    // If both are integers, use integer arithmetic (also covers IDIV).
    // Detect overflow on +, -, * and promote to double rather than wrap.
    if (is_integer_type(a.type) && is_integer_type(b.type) && op != OpCode::DIV && op != OpCode::POW) {
        int64_t ia = a.to_int();
        int64_t ib = b.to_int();
        int64_t result;
        bool overflow = false;
        switch (op) {
            case OpCode::ADD: {
                uint64_t ur = (uint64_t)ia + (uint64_t)ib;
                result = (int64_t)ur;
                overflow = ((ia ^ result) & (ib ^ result)) < 0;
                break;
            }
            case OpCode::SUB: {
                uint64_t ur = (uint64_t)ia - (uint64_t)ib;
                result = (int64_t)ur;
                overflow = ((ia ^ ib) & (ia ^ result)) < 0;
                break;
            }
            case OpCode::MUL: {
                result = (int64_t)((uint64_t)ia * (uint64_t)ib);
                if (ia != 0 && result / ia != ib) overflow = true;
                break;
            }
            case OpCode::IDIV:
                if (ib == 0) throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero at arithmetic helper");
                result = ia / ib; break;
            case OpCode::MOD_OP: result = (ib != 0) ? ia % ib : 0; break;
            default: result = 0;
        }
        if (overflow) {
            double da = (double)ia, db = (double)ib, dr;
            switch (op) {
                case OpCode::ADD: dr = da + db; break;
                case OpCode::SUB: dr = da - db; break;
                case OpCode::MUL: dr = da * db; break;
                default:          dr = 0; break;
            }
            return Value::make_f64(dr);
        }
        return Value::make_i64(result);
    }

    // Fall back to float arithmetic
    double da = a.to_double();
    double db = b.to_double();
    double result;
    switch (op) {
        case OpCode::ADD:    result = da + db; break;
        case OpCode::SUB:    result = da - db; break;
        case OpCode::MUL:    result = da * db; break;
        case OpCode::DIV:
            if (db == 0.0) throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero at arithmetic helper");
            result = da / db; break;
        case OpCode::IDIV:
            if (db == 0.0) throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero at arithmetic helper");
            // Integer division on floats: truncate toward zero, return INT64
            return Value::make_i64((int64_t)(da / db));
        case OpCode::MOD_OP: result = (db != 0.0) ? std::fmod(da, db) : 0.0; break;
        case OpCode::POW:    result = std::pow(da, db); break;
        default: result = 0.0;
    }
    return Value::make_f64(result);
}

// ── Array arithmetic helper ──────────────────────────────────

// Element-wise scalar binary op used by array_arithmetic. Handles numeric +
// string cases so arrays of strings broadcast correctly.
Value VM::scalar_binop(const Value& a, const Value& b, OpCode op) {
    // String + anything with ADD → concat
    if (op == OpCode::ADD && (a.type == ValueType::STRING || b.type == ValueType::STRING)) {
        return Value::make_string(a.to_string() + b.to_string());
    }
    // String * int or int * String with MUL → repetition
    if (op == OpCode::MUL) {
        if (a.type == ValueType::STRING && is_numeric(b.type)) {
            std::string s, src = a.as_string()->data;
            int64_t n = b.to_int();
            for (int64_t i = 0; i < n; i++) s += src;
            return Value::make_string(s);
        }
        if (b.type == ValueType::STRING && is_numeric(a.type)) {
            std::string s, src = b.as_string()->data;
            int64_t n = a.to_int();
            for (int64_t i = 0; i < n; i++) s += src;
            return Value::make_string(s);
        }
    }
    return arithmetic(a, b, op);
}

Value VM::array_arithmetic(const Value& a, const Value& b, OpCode op) {
    // Recursive so 2-D matrices keep their shape: when an inner element is
    // itself an array, recurse instead of dropping into scalar_binop which
    // would coerce the whole row to a number.
    if (a.type == ValueType::ARRAY && b.type == ValueType::ARRAY) {
        auto& la = a.as_array()->elements;
        auto& lb = b.as_array()->elements;
        // NumPy-style broadcasting for mismatched sizes
        if (la.size() != lb.size()) {
            bool la_2d = !la.empty() && la[0].type == ValueType::ARRAY;
            bool lb_2d = !lb.empty() && lb[0].type == ValueType::ARRAY;
            // (M,N) op (N,) or (1,N) op (N,) → broadcast flat array to each row
            if (la_2d && !lb_2d) {
                Value result = Value::make_array();
                auto* out = result.as_array();
                out->elements.reserve(la.size());
                for (size_t i = 0; i < la.size(); i++)
                    out->elements.push_back(array_arithmetic(la[i], b, op));
                return result;
            }
            // (N,) op (M,N) → broadcast flat array to each row
            if (lb_2d && !la_2d) {
                Value result = Value::make_array();
                auto* out = result.as_array();
                out->elements.reserve(lb.size());
                for (size_t i = 0; i < lb.size(); i++)
                    out->elements.push_back(array_arithmetic(a, lb[i], op));
                return result;
            }
            // Both 2D: broadcast size-1 side
            if (la.size() == 1) {
                Value result = Value::make_array();
                auto* out = result.as_array();
                out->elements.reserve(lb.size());
                for (size_t i = 0; i < lb.size(); i++)
                    out->elements.push_back(array_arithmetic(la[0], lb[i], op));
                return result;
            }
            if (lb.size() == 1) {
                Value result = Value::make_array();
                auto* out = result.as_array();
                out->elements.reserve(la.size());
                for (size_t i = 0; i < la.size(); i++)
                    out->elements.push_back(array_arithmetic(la[i], lb[0], op));
                return result;
            }
        }
        Value result = Value::make_array();
        auto* out = result.as_array();
        size_t len = std::min(la.size(), lb.size());
        out->elements.reserve(len);
        for (size_t i = 0; i < len; i++) {
            if (la[i].type == ValueType::ARRAY || lb[i].type == ValueType::ARRAY)
                out->elements.push_back(array_arithmetic(la[i], lb[i], op));
            else
                out->elements.push_back(scalar_binop(la[i], lb[i], op));
        }
        return result;
    }
    if (a.type == ValueType::ARRAY) {
        Value result = Value::make_array();
        auto* out = result.as_array();
        auto& arr = a.as_array()->elements;
        out->elements.reserve(arr.size());
        for (auto& elem : arr) {
            if (elem.type == ValueType::ARRAY)
                out->elements.push_back(array_arithmetic(elem, b, op));
            else
                out->elements.push_back(scalar_binop(elem, b, op));
        }
        return result;
    }
    Value result = Value::make_array();
    auto* out = result.as_array();
    auto& arr = b.as_array()->elements;
    out->elements.reserve(arr.size());
    for (auto& elem : arr) {
        if (elem.type == ValueType::ARRAY)
            out->elements.push_back(array_arithmetic(a, elem, op));
        else
            out->elements.push_back(scalar_binop(a, elem, op));
    }
    return result;
}

// ── Comparison helper ────────────────────────────────────────

Value VM::compare(const Value& a, const Value& b, OpCode op) {
    // String comparison
    if (a.type == ValueType::STRING && b.type == ValueType::STRING) {
        const std::string& sa = a.as_string()->data;
        const std::string& sb = b.as_string()->data;
        bool result;
        switch (op) {
            case OpCode::CMP_EQ: result = (sa == sb); break;
            case OpCode::CMP_NE: result = (sa != sb); break;
            case OpCode::CMP_LT: result = (sa < sb); break;
            case OpCode::CMP_GT: result = (sa > sb); break;
            case OpCode::CMP_LE: result = (sa <= sb); break;
            case OpCode::CMP_GE: result = (sa >= sb); break;
            default: result = false;
        }
        return Value::make_bool(result);
    }

    // Numeric comparison
    double da = a.to_double();
    double db = b.to_double();
    bool result;
    switch (op) {
        case OpCode::CMP_EQ: result = (da == db); break;
        case OpCode::CMP_NE: result = (da != db); break;
        case OpCode::CMP_LT: result = (da < db); break;
        case OpCode::CMP_GT: result = (da > db); break;
        case OpCode::CMP_LE: result = (da <= db); break;
        case OpCode::CMP_GE: result = (da >= db); break;
        default: result = false;
    }
    return Value::make_bool(result);
}

// ── Type cast ────────────────────────────────────────────────

Value VM::cast_value(const Value& v, ValueType target) {
    switch (target) {
        case ValueType::BOOLEAN: return Value::make_bool(v.to_bool());
        case ValueType::BYTE:    return Value::make_byte(static_cast<uint8_t>(v.to_int()));
        case ValueType::INT16:   return Value::make_i16(static_cast<int16_t>(v.to_int()));
        case ValueType::INT32:   return Value::make_i32(static_cast<int32_t>(v.to_int()));
        case ValueType::INT64:   return Value::make_i64(v.to_int());
        case ValueType::FLOAT16: return Value::make_f16(static_cast<float>(v.to_double()));
        case ValueType::FLOAT32: return Value::make_f32(static_cast<float>(v.to_double()));
        case ValueType::FLOAT64: return Value::make_f64(v.to_double());
        case ValueType::STRING:  return Value::make_string(v.to_string());
        default: return v;
    }
}

// ── Built-in functions ───────────────────────────────────────

void VM::register_native(const std::string& name, NativeFunc fn) {
    natives[name] = std::move(fn);
}

// Note: Functions registered with this simple overload rely on the
// CALL handler's try/catch for safety. The arity-checked overload
// (name, min, max, fn) provides better error messages.

void VM::register_native(const std::string& name, int min_args, int max_args, NativeFunc fn) {
    std::string fname = name;
    natives[name] = [fname, min_args, max_args, fn](const std::vector<Value>& args) -> Value {
        int n = (int)args.size();
        if (n < min_args)
            throw jdError(ErrCode::WRONG_ARG_COUNT,
                fname + ": expected at least " + std::to_string(min_args) + " argument(s), got " + std::to_string(n));
        if (max_args >= 0 && n > max_args)
            throw jdError(ErrCode::WRONG_ARG_COUNT,
                fname + ": expected at most " + std::to_string(max_args) + " argument(s), got " + std::to_string(n));
        return fn(args);
    };
}

// ── Helpers for matrix operations ─────────────────────────────

static std::vector<Value> flatten_values(const Value& v) {
    std::vector<Value> flat;
    std::function<void(const Value&)> go = [&](const Value& val) {
        if (val.type == ValueType::ARRAY)
            for (auto& e : val.as_array()->elements) go(e);
        else flat.push_back(val);
    };
    go(v);
    return flat;
}

static void get_2d(const Value& m, int& rows, int& cols) {
    auto* a = m.as_array();
    rows = (int)a->elements.size();
    cols = (rows > 0 && a->elements[0].type == ValueType::ARRAY)
        ? (int)a->elements[0].as_array()->elements.size() : 0;
}

static double elem2d(const Value& m, int r, int c) {
    return m.as_array()->elements[r].as_array()->elements[c].to_double();
}

static Value make_matrix(int rows, int cols, double fill = 0.0) {
    Value m = Value::make_array();
    auto* ma = m.as_array();
    ma->elements.reserve(rows);
    for (int i = 0; i < rows; i++) {
        Value row = Value::make_array();
        auto* ra = row.as_array();
        ra->elements.reserve(cols);
        for (int j = 0; j < cols; j++) ra->elements.push_back(Value::make_f64(fill));
        ma->elements.push_back(std::move(row));
    }
    return m;
}

// ── Debug adapter support ────────────────────────────────────────

void VM::debug_check(int line) {
    if (!debug || !debug->dap || line <= 0) return;
    // Inside REPL/EVAL/EXECUTE sub-runs, never pause: the sub-chunk's line numbers
    // would otherwise look like the main program and re-trigger stopped events.
    if (subrun_depth > 0) return;
    if (line == debug->last_debug_line) return;
    debug->last_debug_line = line;

    // Determine current source file from the active chunk
    std::string cur_file = debug_current_file();
    std::string cur_file_norm = normalize_path(cur_file);

    bool should_pause = false;
    std::string pause_reason = "step";

    // 1. Breakpoint check (highest priority) — match normalized file + line
    {
        auto it = debug->breakpoints.find(cur_file_norm);
        if (it != debug->breakpoints.end() && it->second.count(line)) {
            should_pause = true;
            pause_reason = "breakpoint";
        }
        // Also check by line-only for backward compat (breakpoints set without file)
        if (!should_pause) {
            auto it2 = debug->breakpoints.find("");
            if (it2 != debug->breakpoints.end() && it2->second.count(line)) {
                should_pause = true;
                pause_reason = "breakpoint";
            }
        }
    }
    // 2. Stepping
    if (!should_pause) {
        switch (debug->state) {
            case DebugState::PAUSED:
                should_pause = true;
                break;
            case DebugState::STEP_IN:
                should_pause = true;
                pause_reason = "step";
                break;
            case DebugState::STEP_OUT:
                if (frames.size() < debug->step_out_depth) {
                    should_pause = true;
                    pause_reason = "step";
                }
                break;
            case DebugState::STEP_OVER:
                if (frames.size() <= debug->step_over_depth) {
                    should_pause = true;
                    pause_reason = "step";
                }
                break;
            case DebugState::RUNNING:
                break;
        }
    }

    if (should_pause) {
        debug->state = DebugState::PAUSED;
        if (debug->is_entry) {
            pause_reason = "entry";
            debug->is_entry = false;
        }
        debug->dap->send_stopped_message(pause_reason, line,
            cur_file.empty() ? debug->program_path : cur_file);
        debug->pause();
    }
}

int VM::debug_current_line() const {
    if (frames.empty()) return 0;
    auto& f = frames.back();
    size_t ip = f.ip > 0 ? f.ip - 1 : 0;
    if (ip < f.chunk->line_info.size()) return f.chunk->line_info[ip];
    return 0;
}

std::string VM::debug_current_file() const {
    if (frames.empty()) return "";
    return frames.back().chunk->source_file;
}

size_t VM::debug_call_depth() const {
    return frames.size();
}

bool VM::debug_goto_line(int target_line) {
    if (frames.empty()) return false;
    auto& f = frames.back();
    const auto& li = f.chunk->line_info;

    // Find the first bytecode offset where the line transitions TO target_line.
    // This ensures we land on an opcode boundary, not in the middle of operands.
    int prev_line = 0;
    for (size_t i = 0; i < li.size(); i++) {
        if (li[i] == target_line && li[i] != prev_line) {
            f.ip = i;
            // Reset stack to frame base and clear exception handlers
            sp = f.stack_base;
            try_handlers.clear();
            if (debug) debug->last_debug_line = -1;
            return true;
        }
        prev_line = li[i];
    }
    return false;
}

std::vector<VM::DebugFrame> VM::debug_get_stack_frames() const {
    std::vector<DebugFrame> result;
    // Skip main frame (index 0), iterate user function frames
    for (size_t i = 1; i < frames.size(); i++) {
        int line = 0;
        size_t ip = frames[i].ip > 0 ? frames[i].ip - 1 : 0;
        if (ip < frames[i].chunk->line_info.size())
            line = frames[i].chunk->line_info[ip];
        // Find function name by matching chunk pointer
        std::string name = "<unknown>";
        if (func_protos) {
            for (auto& fp : *func_protos) {
                if (&fp.chunk == frames[i].chunk) { name = fp.name; break; }
            }
        }
        std::string file = frames[i].chunk->source_file;
        result.push_back({line, name, file});
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> VM::debug_get_globals() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& [name, slot] : global_names) {
        if (slot < globals.size() && name.find("__") == std::string::npos) {
            result.push_back({name, globals[slot].to_string()});
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> VM::debug_get_locals() const {
    std::vector<std::pair<std::string, std::string>> result;
    if (frames.size() <= 1) return result; // no function frame

    auto& f = frames.back();
    // Find function name
    std::string func_name;
    if (func_protos) {
        for (auto& fp : *func_protos) {
            if (&fp.chunk == f.chunk) { func_name = fp.name; break; }
        }
    }

    for (uint16_t i = 0; i < f.chunk->var_names.size(); i++) {
        size_t abs = f.stack_base + i;
        if (abs < sp) {
            std::string name = func_name.empty()
                ? f.chunk->var_names[i]
                : func_name + "_" + f.chunk->var_names[i];
            result.push_back({name, stack[abs].to_string()});
        }
    }
    return result;
}

void VM::register_builtins() {
    // ── Math ─────────────────────────────────────────────────
    register_native("ABS", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::abs(args[0].to_double()));
    });
    register_native("SQR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::sqrt(args[0].to_double()));
    });
    register_native("INT", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(static_cast<int64_t>(args[0].to_double()));
    });
    register_native("SIN", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::sin(args[0].to_double()));
    });
    register_native("COS", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::cos(args[0].to_double()));
    });
    register_native("TAN", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::tan(args[0].to_double()));
    });
    register_native("LOG", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::log(args[0].to_double()));
    });
    register_native("EXP", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::exp(args[0].to_double()));
    });
    register_native("RND", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_f64(static_cast<double>(rand()) / RAND_MAX);
    });
    register_native("LOG10", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::log10(args[0].to_double()));
    });
    register_native("FAC", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        if (n < 0) return Value::make_i64(0);
        // Stay in INT64 as long as possible (exact up to 20!), then continue
        // in FLOAT64 (still fits 170! ≈ 7.26e306). Beyond that, returns inf.
        int64_t ir = 1;
        int64_t i = 2;
        for (; i <= n; i++) {
            int64_t nr = (int64_t)((uint64_t)ir * (uint64_t)i);
            if (ir != 0 && nr / ir != i) break; // overflow → switch to double
            ir = nr;
        }
        if (i > n) return Value::make_i64(ir);
        double dr = (double)ir;
        for (; i <= n; i++) dr *= (double)i;
        return Value::make_f64(dr);
    });
    register_native("FLOOR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::floor(args[0].to_double()));
    });
    register_native("CEIL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::ceil(args[0].to_double()));
    });
    register_native("ROUND", 1, 2, [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        int dec = (args.size() >= 2) ? (int)args[1].to_int() : 0;
        double m = std::pow(10.0, dec);
        return Value::make_f64(std::round(v * m) / m);
    });
    register_native("CLAMP", 3, 3, [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        double lo = args[1].to_double();
        double hi = args[2].to_double();
        return Value::make_f64(v < lo ? lo : (v > hi ? hi : v));
    });
    register_native("TRUNC", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::trunc(args[0].to_double()));
    });
    register_native("CDBL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(args[0].to_double());
    });
    register_native("IIF", 3, 3, [](const std::vector<Value>& args) -> Value {
        // Vectorized: IIF(cond_array, true_val, false_val)
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            auto* conds = args[0].as_array();
            bool t_arr = (args[1].type == ValueType::ARRAY);
            bool f_arr = (args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < conds->elements.size(); i++) {
                if (conds->elements[i].to_bool()) {
                    r.as_array()->elements.push_back(t_arr ? args[1].as_array()->elements[i % args[1].as_array()->elements.size()] : args[1]);
                } else {
                    r.as_array()->elements.push_back(f_arr ? args[2].as_array()->elements[i % args[2].as_array()->elements.size()] : args[2]);
                }
            }
            return r;
        }
        return args[0].to_bool() ? args[1] : args[2];
    });

    // String functions
    register_native("LEN", 1, 1, [](const std::vector<Value>& args) -> Value {
        // Always scalar: element count for arrays, byte count for strings.
        // Use LENV for the shape vector of a nested array.
        if (args[0].type == ValueType::STRING)
            return Value::make_i64(args[0].as_string()->data.size());
        if (args[0].type == ValueType::ARRAY)
            return Value::make_i64(args[0].as_array()->elements.size());
        return Value::make_i64(0);
    });
    register_native("LENV", 1, 1, [](const std::vector<Value>& args) -> Value {
        // Shape vector: [dim0, dim1, …]. Non-nested arrays return [n];
        // strings return [byte_count]; scalars return [0].
        Value shape = Value::make_array();
        auto& elems = shape.as_array()->elements;
        if (args[0].type == ValueType::STRING) {
            elems.push_back(Value::make_i64(args[0].as_string()->data.size()));
            return shape;
        }
        if (args[0].type != ValueType::ARRAY) {
            elems.push_back(Value::make_i64(0));
            return shape;
        }
        const ArrayObj* cur = args[0].as_array();
        while (cur) {
            elems.push_back(Value::make_i64(cur->elements.size()));
            if (!cur->elements.empty() && cur->elements[0].type == ValueType::ARRAY)
                cur = cur->elements[0].as_array();
            else
                break;
        }
        return shape;
    });
    register_native("LEFT", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        return Value::make_string(s.substr(0, n));
    });
    register_native("RIGHT", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t n = args[1].to_int();
        if (n >= (int64_t)s.size()) return Value::make_string(s);
        return Value::make_string(s.substr(s.size() - n));
    });
    register_native("MID", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t start = args[1].to_int();
        int64_t len = args.size() > 2 ? args[2].to_int() : (int64_t)s.size();
        return Value::make_string(s.substr(start, len));
    });
    register_native("STR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("VAL", 1, 1, [](const std::vector<Value>& args) -> Value {
        try { return Value::make_f64(std::stod(args[0].as_string()->data)); }
        catch (...) { return Value::make_f64(0); }
    });
    register_native("CHR", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_string(std::string(1, static_cast<char>(args[0].to_int())));
    });
    register_native("ASC", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto& s = args[0].as_string()->data;
        return Value::make_i64(s.empty() ? 0 : static_cast<uint8_t>(s[0]));
    });
    register_native("UCASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return Value::make_string(s);
    });
    register_native("LCASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return Value::make_string(s);
    });

    // Array functions
    register_native("PUSH", 2, -1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        arr->elements.push_back(args[1]);
        return Value::make_i64(arr->elements.size());
    });
    register_native("POP", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        if (arr->elements.empty()) return Value::make_none();
        Value v = arr->elements.back();
        arr->elements.pop_back();
        return v;
    });

    // Type checking
    auto typeof_one = [](const Value& v) -> Value {
        if (v.type == ValueType::FLOAT64 && v.subtype == ValueSubtype::DATE)
            return Value::make_string("DATE");
        switch (v.type) {
            case ValueType::NONE:    return Value::make_string("NONE");
            case ValueType::BOOLEAN: return Value::make_string("BOOLEAN");
            case ValueType::BYTE:    return Value::make_string("BYTE");
            case ValueType::INT16:   return Value::make_string("INT16");
            case ValueType::INT32:   return Value::make_string("INT32");
            case ValueType::INT64:   return Value::make_string("INT64");
            case ValueType::FLOAT16: return Value::make_string("FLOAT16");
            case ValueType::FLOAT32: return Value::make_string("FLOAT32");
            case ValueType::FLOAT64: return Value::make_string("FLOAT64");
            case ValueType::STRING:  return Value::make_string("STRING");
            case ValueType::OBJECT:  return Value::make_string("OBJECT");
            case ValueType::TENSOR:  return Value::make_string("TENSOR");
            case ValueType::ARRAY:   return Value::make_string("ARRAY");
        }
        return Value::make_string("UNKNOWN");
    };
    register_native("TYPEOF", 1, -1, [typeof_one](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) return typeof_one(args[0]);
        // Multiple args: return array of type strings
        Value result = Value::make_array();
        for (auto& a : args) result.as_array()->elements.push_back(typeof_one(a));
        return result;
    });

    // Tensor creation
    register_native("TENSOR", 1, 2, [](const std::vector<Value>& args) -> Value {
        // TENSOR(rows, cols) - create zero tensor
        if (args.size() >= 2) {
            size_t rows = args[0].to_int();
            size_t cols = args[1].to_int();
            return Value::make_tensor({rows, cols}, std::vector<double>(rows * cols, 0.0));
        } else if (args.size() == 1) {
            size_t size = args[0].to_int();
            return Value::make_tensor({size}, std::vector<double>(size, 0.0));
        }
        return Value::make_none();
    });

    // IOTA(N, [B=1], [S=1]) -> vector of N numbers starting at B with step S
    register_native("IOTA", 1, 3, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_array();
        int64_t n = args[0].to_int();
        double base = (args.size() >= 2) ? args[1].to_double() : 1.0;
        double step = (args.size() >= 3) ? args[2].to_double() : 1.0;

        Value result = Value::make_array();
        auto* arr = result.as_array();
        arr->elements.reserve(n);

        // Use integer path when base and step are whole numbers
        bool use_int = (base == (int64_t)base) && (step == (int64_t)step);
        if (use_int) {
            int64_t ib = (int64_t)base, is = (int64_t)step;
            for (int64_t i = 0; i < n; i++) {
                arr->elements.push_back(Value::make_i64(ib + i * is));
            }
        } else {
            for (int64_t i = 0; i < n; i++) {
                arr->elements.push_back(Value::make_f64(base + i * step));
            }
        }
        return result;
    });

    // RESHAPE(source_array, shape_vector) -> nested array
    // e.g. RESHAPE(IOTA(8), [2,2,2]) -> [[[1,2],[3,4]],[[5,6],[7,8]]]
    register_native("RESHAPE", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_none();

        // Flatten source into a flat list of values
        std::vector<Value> flat;
        std::function<void(const Value&)> flatten = [&](const Value& v) {
            if (v.type == ValueType::ARRAY) {
                for (auto& e : v.as_array()->elements) flatten(e);
            } else {
                flat.push_back(v);
            }
        };
        flatten(args[0]);

        // Read shape
        std::vector<int64_t> shape;
        if (args[1].type == ValueType::ARRAY) {
            for (auto& e : args[1].as_array()->elements)
                shape.push_back(e.to_int());
        } else {
            shape.push_back(args[1].to_int());
        }

        if (shape.empty()) return Value::make_array();

        // Recursive builder
        size_t idx = 0;
        std::function<Value(size_t)> build = [&](size_t dim) -> Value {
            if (dim == shape.size() - 1) {
                // Innermost dimension: create leaf array
                Value arr = Value::make_array();
                auto* a = arr.as_array();
                a->elements.reserve(shape[dim]);
                for (int64_t i = 0; i < shape[dim]; i++) {
                    a->elements.push_back(flat[idx % flat.size()]);
                    idx++;
                }
                return arr;
            }
            // Outer dimension: array of sub-arrays
            Value arr = Value::make_array();
            auto* a = arr.as_array();
            a->elements.reserve(shape[dim]);
            for (int64_t i = 0; i < shape[dim]; i++) {
                a->elements.push_back(build(dim + 1));
            }
            return arr;
        };

        return build(0);
    });

    // ── APPEND ───────────────────────────────────────────────
    register_native("APPEND", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::vector<Value> flat;
        for (auto& a : args) {
            if (a.type == ValueType::ARRAY)
                for (auto& e : a.as_array()->elements) flat.push_back(e);
            else flat.push_back(a);
        }
        Value r = Value::make_array();
        r.as_array()->elements = std::move(flat);
        return r;
    });

    // ── DIFF ─────────────────────────────────────────────────
    register_native("DIFF", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return args[0];
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        for (auto& e : a->elements) {
            bool found = false;
            for (auto& f : b->elements) {
                if (e.to_string() == f.to_string()) { found = true; break; }
            }
            if (!found) out->elements.push_back(e);
        }
        return r;
    });

    // ── TAKE / DROP ──────────────────────────────────────────
    register_native("TAKE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        int sz = (int)arr->elements.size();
        if (n >= 0) {
            for (int i = 0; i < n && i < sz; i++) out->elements.push_back(arr->elements[i]);
        } else {
            int start = std::max(0, sz + (int)n);
            for (int i = start; i < sz; i++) out->elements.push_back(arr->elements[i]);
        }
        return r;
    });

    register_native("DROP", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t n = args[0].to_int();
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        int sz = (int)arr->elements.size();
        if (n >= 0) {
            for (int i = (int)n; i < sz; i++) out->elements.push_back(arr->elements[i]);
        } else {
            int end = sz + (int)n;
            for (int i = 0; i < end && i < sz; i++) out->elements.push_back(arr->elements[i]);
        }
        return r;
    });

    // ── REVERSE ──────────────────────────────────────────────
    register_native("REVERSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        auto* arr = args[0].as_array();
        auto* out = r.as_array();
        out->elements.assign(arr->elements.rbegin(), arr->elements.rend());
        return r;
    });

    // ── UNIQUE ───────────────────────────────────────────────
    register_native("UNIQUE", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        for (auto& e : arr->elements) {
            bool dup = false;
            for (auto& o : out->elements) {
                if (e.to_string() == o.to_string()) { dup = true; break; }
            }
            if (!dup) out->elements.push_back(e);
        }
        return r;
    });

    // ── SHUFFLE ──────────────────────────────────────────────
    register_native("SHUFFLE", 1, 1, [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        r.as_array()->elements = args[0].as_array()->elements;
        auto& elems = r.as_array()->elements;
        for (int i = (int)elems.size() - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            std::swap(elems[i], elems[j]);
        }
        return r;
    });

    // ── FIND_IN_ARRAY ────────────────────────────────────────
    register_native("FIND_IN_ARRAY", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        std::string target = args[1].to_string();
        for (size_t i = 0; i < arr->elements.size(); i++) {
            if (arr->elements[i].to_string() == target) return Value::make_i64(i);
        }
        return Value::make_i64(-1);
    });

    // ── NORMALIZE ────────────────────────────────────────────
    register_native("NORMALIZE", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.empty()) return Value::make_array();
        double mn = flat[0].to_double(), mx = mn;
        for (auto& v : flat) { double d = v.to_double(); if (d < mn) mn = d; if (d > mx) mx = d; }
        double range = mx - mn;
        Value r = Value::make_array();
        auto* out = r.as_array();
        for (auto& v : flat)
            out->elements.push_back(Value::make_f64(range == 0 ? 0.0 : (v.to_double() - mn) / range));
        return r;
    });

    // ── DISTANCE ─────────────────────────────────────────────
    register_native("DISTANCE", [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        double sum = 0;
        size_t n = std::min(a->elements.size(), b->elements.size());
        for (size_t i = 0; i < n; i++) {
            double d = a->elements[i].to_double() - b->elements[i].to_double();
            sum += d * d;
        }
        return Value::make_f64(std::sqrt(sum));
    });

    // ── LERP ─────────────────────────────────────────────────
    register_native("LERP", [](const std::vector<Value>& args) -> Value {
        double a = args[0].to_double(), b = args[1].to_double(), t = args[2].to_double();
        return Value::make_f64(a + (b - a) * t);
    });

    // ── GRADE ────────────────────────────────────────────────
    register_native("GRADE", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        std::vector<int64_t> idx(arr->elements.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int64_t a, int64_t b) {
            return arr->elements[a].to_double() < arr->elements[b].to_double();
        });
        Value r = Value::make_array();
        for (auto i : idx) r.as_array()->elements.push_back(Value::make_i64(i));
        return r;
    });

    // ── XSORT ────────────────────────────────────────────────
    register_native("XSORT", [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        r.as_array()->elements = args[0].as_array()->elements;
        bool desc = (args.size() >= 3 && args[2].to_bool());
        auto& elems = r.as_array()->elements;
        // Check if 2D (sort rows by dimension)
        if (args.size() >= 2 && args[0].as_array()->elements.size() > 0 &&
            args[0].as_array()->elements[0].type == ValueType::ARRAY) {
            int dim = (int)args[1].to_int();
            std::sort(elems.begin(), elems.end(), [dim, desc](const Value& a, const Value& b) {
                double va = a.as_array()->elements[dim].to_double();
                double vb = b.as_array()->elements[dim].to_double();
                return desc ? va > vb : va < vb;
            });
        } else {
            std::sort(elems.begin(), elems.end(), [desc](const Value& a, const Value& b) {
                return desc ? a.to_double() > b.to_double() : a.to_double() < b.to_double();
            });
        }
        return r;
    });

    // ── ROTATE ───────────────────────────────────────────────
    register_native("ROTATE", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        int64_t shift = 0;
        if (args[1].type == ValueType::ARRAY) {
            auto& sv = args[1].as_array()->elements;
            if (!sv.empty()) shift = sv[0].to_int();
        } else {
            shift = args[1].to_int();
        }
        int n = (int)arr->elements.size();
        if (n == 0) return args[0];
        shift = ((shift % n) + n) % n;
        Value r = Value::make_array();
        auto* out = r.as_array();
        out->elements.reserve(n);
        for (int i = 0; i < n; i++) out->elements.push_back(arr->elements[(i + shift) % n]);
        return r;
    });

    // ── SHIFT ────────────────────────────────────────────────
    register_native("SHIFT", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        // shift may be a scalar or a 1-element shift-vector ([-1] etc.)
        int64_t shift = 0;
        if (args[1].type == ValueType::ARRAY) {
            auto& sv = args[1].as_array()->elements;
            if (!sv.empty()) shift = sv[0].to_int();
        } else {
            shift = args[1].to_int();
        }
        Value fill = (args.size() >= 3) ? args[2] : Value::make_i64(0);
        int n = (int)arr->elements.size();
        Value r = Value::make_array();
        auto* out = r.as_array();
        out->elements.resize(n, fill);
        for (int i = 0; i < n; i++) {
            int src = i - (int)shift;
            if (src >= 0 && src < n) out->elements[i] = arr->elements[src];
        }
        return r;
    });

    // ── Reductions: SUM, PRODUCT, MIN, MAX, ANY, ALL ─────────
    register_native("SUM", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (args.size() >= 2 && args[0].as_array()->elements[0].type == ValueType::ARRAY) {
            // 2D with dimension
            int dim = (int)args[1].to_int();
            int rows, cols; get_2d(args[0], rows, cols);
            Value r = Value::make_array();
            if (dim == 0) { // reduce rows → one row of col sums
                for (int c = 0; c < cols; c++) {
                    double s = 0; for (int ro = 0; ro < rows; ro++) s += elem2d(args[0], ro, c);
                    r.as_array()->elements.push_back(Value::make_f64(s));
                }
            } else { // reduce cols → one col of row sums
                for (int ro = 0; ro < rows; ro++) {
                    double s = 0; for (int c = 0; c < cols; c++) s += elem2d(args[0], ro, c);
                    r.as_array()->elements.push_back(Value::make_f64(s));
                }
            }
            return r;
        }
        auto flat = flatten_values(args[0]);
        double s = 0; for (auto& v : flat) s += v.to_double();
        return Value::make_f64(s);
    });

    register_native("PRODUCT", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        double s = 1; for (auto& v : flat) s *= v.to_double();
        return Value::make_f64(s);
    });

    register_native("MIN", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.empty()) return Value::make_f64(0);
        double m = flat[0].to_double();
        for (size_t i = 1; i < flat.size(); i++) { double d = flat[i].to_double(); if (d < m) m = d; }
        return Value::make_f64(m);
    });

    register_native("MAX", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.empty()) return Value::make_f64(0);
        double m = flat[0].to_double();
        for (size_t i = 1; i < flat.size(); i++) { double d = flat[i].to_double(); if (d > m) m = d; }
        return Value::make_f64(m);
    });

    register_native("ANY", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        for (auto& v : flat) if (v.to_bool()) return Value::make_bool(true);
        return Value::make_bool(false);
    });

    register_native("ALL", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        for (auto& v : flat) if (!v.to_bool()) return Value::make_bool(false);
        return Value::make_bool(true);
    });

    // ── SCAN ─────────────────────────────────────────────────
    register_native("SCAN", [this](const std::vector<Value>& args) -> Value {
        std::string op = args[0].as_string()->data;
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        auto* out = r.as_array();
        if (arr->elements.empty()) return r;
        Value acc = arr->elements[0];
        out->elements.push_back(acc);
        for (size_t i = 1; i < arr->elements.size(); i++) {
            acc = apply_binary_op(op, acc, arr->elements[i]);
            out->elements.push_back(acc);
        }
        return r;
    });

    // ── TRANSPOSE ────────────────────────────────────────────
    register_native("TRANSPOSE", [](const std::vector<Value>& args) -> Value {
        int rows, cols; get_2d(args[0], rows, cols);
        if (cols == 0) return args[0];
        Value r = make_matrix(cols, rows);
        for (int i = 0; i < rows; i++)
            for (int j = 0; j < cols; j++)
                r.as_array()->elements[j].as_array()->elements[i] = args[0].as_array()->elements[i].as_array()->elements[j];
        return r;
    });

    // ── MATMUL ───────────────────────────────────────────────
    register_native("MATMUL", [](const std::vector<Value>& args) -> Value {
        int ar, ac, br, bc;
        get_2d(args[0], ar, ac); get_2d(args[1], br, bc);
        if (ac != br) throw std::runtime_error("MATMUL: incompatible dimensions");
        Value r = make_matrix(ar, bc);
        for (int i = 0; i < ar; i++)
            for (int j = 0; j < bc; j++) {
                double s = 0;
                for (int k = 0; k < ac; k++) s += elem2d(args[0], i, k) * elem2d(args[1], k, j);
                r.as_array()->elements[i].as_array()->elements[j] = Value::make_f64(s);
            }
        return r;
    });

    // ── MVLET ────────────────────────────────────────────────
    register_native("MVLET", [](const std::vector<Value>& args) -> Value {
        // MVLET(matrix, dim, index, vector)
        int rows, cols; get_2d(args[0], rows, cols);
        int dim = (int)args[1].to_int();
        int idx = (int)args[2].to_int();
        auto* vec = args[3].as_array();
        // Deep copy
        Value r = Value::make_array();
        for (auto& row : args[0].as_array()->elements) {
            Value nr = Value::make_array();
            nr.as_array()->elements = row.as_array()->elements;
            r.as_array()->elements.push_back(std::move(nr));
        }
        if (dim == 0) { // replace row
            for (int c = 0; c < cols && c < (int)vec->elements.size(); c++)
                r.as_array()->elements[idx].as_array()->elements[c] = vec->elements[c];
        } else { // replace column
            for (int ro = 0; ro < rows && ro < (int)vec->elements.size(); ro++)
                r.as_array()->elements[ro].as_array()->elements[idx] = vec->elements[ro];
        }
        return r;
    });

    // ── STACK ────────────────────────────────────────────────
    register_native("STACK", [](const std::vector<Value>& args) -> Value {
        int dim = (int)args[0].to_int();
        Value r = Value::make_array();
        if (dim == 0) { // stack as rows
            for (size_t i = 1; i < args.size(); i++) {
                Value row = Value::make_array();
                if (args[i].type == ValueType::ARRAY)
                    row.as_array()->elements = args[i].as_array()->elements;
                else row.as_array()->elements.push_back(args[i]);
                r.as_array()->elements.push_back(std::move(row));
            }
        } else { // stack as columns (transpose of row stack)
            size_t num_vecs = args.size() - 1;
            size_t len = (num_vecs > 0 && args[1].type == ValueType::ARRAY) ? args[1].as_array()->elements.size() : 0;
            for (size_t i = 0; i < len; i++) {
                Value row = Value::make_array();
                for (size_t v = 1; v <= num_vecs; v++)
                    row.as_array()->elements.push_back(args[v].as_array()->elements[i]);
                r.as_array()->elements.push_back(std::move(row));
            }
        }
        return r;
    });

    // ── SLICE ────────────────────────────────────────────────
    register_native("SLICE", [](const std::vector<Value>& args) -> Value {
        int dim = (int)args[1].to_int();
        int idx = (int)args[2].to_int();
        auto* src = args[0].as_array();
        if (!src) throw std::runtime_error("SLICE: expected array");
        // 4-arg form: SLICE(arr, axis, start, count) — extract sub-matrix
        if (args.size() >= 4) {
            int count = (int)args[3].to_int();
            Value r = Value::make_array();
            if (dim == 0) { // extract rows [start..start+count)
                int end = std::min(idx + count, (int)src->elements.size());
                for (int i = idx; i < end; i++)
                    r.as_array()->elements.push_back(src->elements[i]);
            } else { // extract columns [start..start+count) from each row
                for (auto& row : src->elements) {
                    auto* ra = row.as_array();
                    Value nr = Value::make_array();
                    int end = std::min(idx + count, (int)ra->elements.size());
                    for (int j = idx; j < end; j++)
                        nr.as_array()->elements.push_back(ra->elements[j]);
                    r.as_array()->elements.push_back(std::move(nr));
                }
            }
            return r;
        }
        // 3-arg form: SLICE(arr, axis, index) — extract single row/column
        int rows, cols; get_2d(args[0], rows, cols);
        Value r = Value::make_array();
        if (dim == 0) { // extract row
            r.as_array()->elements = src->elements[idx].as_array()->elements;
        } else { // extract column
            for (int ro = 0; ro < rows; ro++)
                r.as_array()->elements.push_back(src->elements[ro].as_array()->elements[idx]);
        }
        return r;
    });

    // ── SOLVE (Ax=b) ─────────────────────────────────────────
    register_native("SOLVE", [](const std::vector<Value>& args) -> Value {
        int n, nc; get_2d(args[0], n, nc);
        auto* bv = args[1].as_array();
        // Build augmented matrix
        std::vector<std::vector<double>> A(n, std::vector<double>(n + 1));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) A[i][j] = elem2d(args[0], i, j);
            A[i][n] = bv->elements[i].to_double();
        }
        // Gaussian elimination with partial pivoting
        for (int col = 0; col < n; col++) {
            int best = col;
            for (int r = col + 1; r < n; r++) if (std::abs(A[r][col]) > std::abs(A[best][col])) best = r;
            std::swap(A[col], A[best]);
            if (std::abs(A[col][col]) < 1e-15) throw std::runtime_error("SOLVE: singular matrix");
            for (int r = col + 1; r < n; r++) {
                double f = A[r][col] / A[col][col];
                for (int j = col; j <= n; j++) A[r][j] -= f * A[col][j];
            }
        }
        // Back substitution
        std::vector<double> x(n);
        for (int i = n - 1; i >= 0; i--) {
            x[i] = A[i][n];
            for (int j = i + 1; j < n; j++) x[i] -= A[i][j] * x[j];
            x[i] /= A[i][i];
        }
        Value r = Value::make_array();
        for (double v : x) r.as_array()->elements.push_back(Value::make_f64(v));
        return r;
    });

    // ── INVERT ───────────────────────────────────────────────
    register_native("INVERT", [](const std::vector<Value>& args) -> Value {
        int n, nc; get_2d(args[0], n, nc);
        // Augmented [A|I]
        std::vector<std::vector<double>> A(n, std::vector<double>(2 * n, 0.0));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) A[i][j] = elem2d(args[0], i, j);
            A[i][n + i] = 1.0;
        }
        // Gauss-Jordan
        for (int col = 0; col < n; col++) {
            int best = col;
            for (int r = col + 1; r < n; r++) if (std::abs(A[r][col]) > std::abs(A[best][col])) best = r;
            std::swap(A[col], A[best]);
            double piv = A[col][col];
            if (std::abs(piv) < 1e-15) throw std::runtime_error("INVERT: singular matrix");
            for (int j = 0; j < 2 * n; j++) A[col][j] /= piv;
            for (int r = 0; r < n; r++) {
                if (r == col) continue;
                double f = A[r][col];
                for (int j = 0; j < 2 * n; j++) A[r][j] -= f * A[col][j];
            }
        }
        Value r = make_matrix(n, n);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                r.as_array()->elements[i].as_array()->elements[j] = Value::make_f64(A[i][n + j]);
        return r;
    });

    // ── CONVOLVE ─────────────────────────────────────────────
    register_native("CONVOLVE", [](const std::vector<Value>& args) -> Value {
        int ar, ac, kr, kc;
        get_2d(args[0], ar, ac); get_2d(args[1], kr, kc);
        bool wrap = (args.size() >= 3) ? args[2].to_bool() : false;
        int hr = kr / 2, hc = kc / 2;
        Value r = make_matrix(ar, ac);
        for (int i = 0; i < ar; i++)
            for (int j = 0; j < ac; j++) {
                double s = 0;
                for (int ki = 0; ki < kr; ki++)
                    for (int kj = 0; kj < kc; kj++) {
                        int si = i + ki - hr, sj = j + kj - hc;
                        if (wrap) { si = ((si % ar) + ar) % ar; sj = ((sj % ac) + ac) % ac; }
                        else if (si < 0 || si >= ar || sj < 0 || sj >= ac) continue;
                        s += elem2d(args[0], si, sj) * elem2d(args[1], ki, kj);
                    }
                r.as_array()->elements[i].as_array()->elements[j] = Value::make_f64(s);
            }
        return r;
    });

    // ── PLACE ────────────────────────────────────────────────
    register_native("PLACE", [](const std::vector<Value>& args) -> Value {
        // Deep copy destination
        int dr, dc; get_2d(args[0], dr, dc);
        Value r = make_matrix(dr, dc);
        for (int i = 0; i < dr; i++)
            for (int j = 0; j < dc; j++)
                r.as_array()->elements[i].as_array()->elements[j] =
                    args[0].as_array()->elements[i].as_array()->elements[j];
        // Place source at coordinates
        int sr, sc; get_2d(args[1], sr, sc);
        auto* coords = args[2].as_array();
        int oy = (int)coords->elements[0].to_int();
        int ox = (int)coords->elements[1].to_int();
        for (int i = 0; i < sr; i++)
            for (int j = 0; j < sc; j++) {
                int ti = oy + i, tj = ox + j;
                if (ti >= 0 && ti < dr && tj >= 0 && tj < dc)
                    r.as_array()->elements[ti].as_array()->elements[tj] = args[1].as_array()->elements[i].as_array()->elements[j];
            }
        return r;
    });

    // ── SELECT (higher-order) ────────────────────────────────
    register_native("SELECT", [this](const std::vector<Value>& args) -> Value {
        // SELECT(func, array, [row_wise])   — function first, APL/functional style
        Value fn = args[0];
        auto* arr = args[1].as_array();
        bool row_wise = (args.size() >= 3 && args[2].to_bool());
        Value r = Value::make_array();
        if (row_wise) {
            for (auto& row : arr->elements)
                r.as_array()->elements.push_back(call_funcref(fn, {row}));
        } else {
            for (auto& e : arr->elements)
                r.as_array()->elements.push_back(call_funcref(fn, {e}));
        }
        return r;
    });

    // ── FILTER (higher-order) ────────────────────────────────
    register_native("FILTER", [this](const std::vector<Value>& args) -> Value {
        // FILTER(func, array)   — function first
        Value fn = args[0];
        auto* arr = args[1].as_array();
        Value r = Value::make_array();
        for (auto& e : arr->elements) {
            Value res = call_funcref(fn, {e});
            if (res.to_bool()) r.as_array()->elements.push_back(e);
        }
        return r;
    });

    // ── REDUCE (higher-order) ────────────────────────────────
    register_native("REDUCE", [this](const std::vector<Value>& args) -> Value {
        // REDUCE(func, array, [init])   — function first
        Value fn = args[0];
        auto* arr = args[1].as_array();
        if (arr->elements.empty()) return (args.size() >= 3) ? args[2] : Value::make_none();
        Value acc = (args.size() >= 3) ? args[2] : arr->elements[0];
        size_t start = (args.size() >= 3) ? 0 : 1;
        for (size_t i = start; i < arr->elements.size(); i++)
            acc = call_funcref(fn, {acc, arr->elements[i]});
        return acc;
    });

    // ── OUTER ────────────────────────────────────────────────
    register_native("OUTER", [this](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array();
        auto* b = args[1].as_array();
        std::string op = args[2].as_string()->data;
        Value r = Value::make_array();
        for (auto& ai : a->elements) {
            Value row = Value::make_array();
            for (auto& bj : b->elements)
                row.as_array()->elements.push_back(apply_binary_op(op, ai, bj));
            r.as_array()->elements.push_back(std::move(row));
        }
        return r;
    });

    // ── INTEGRATE (Gauss-Legendre) ───────────────────────────
    register_native("INTEGRATE", [this](const std::vector<Value>& args) -> Value {
        Value fn = args[0];
        auto* lim = args[1].as_array();
        int n = (args.size() >= 3) ? (int)args[2].to_int() : 5;
        double a = lim->elements[0].to_double();
        double b = lim->elements[1].to_double();
        // Gauss-Legendre points and weights (up to 5)
        static const double pts5[] = {-0.9061798459, -0.5384693101, 0.0, 0.5384693101, 0.9061798459};
        static const double wts5[] = {0.2369268851, 0.4786286705, 0.5688888889, 0.4786286705, 0.2369268851};
        static const double pts3[] = {-0.7745966692, 0.0, 0.7745966692};
        static const double wts3[] = {0.5555555556, 0.8888888889, 0.5555555556};
        const double* pts = (n >= 5) ? pts5 : pts3;
        const double* wts = (n >= 5) ? wts5 : wts3;
        int np = (n >= 5) ? 5 : 3;
        double mid = (a + b) / 2.0, half = (b - a) / 2.0;
        double sum = 0;
        for (int i = 0; i < np; i++) {
            double x = mid + half * pts[i];
            Value fx = call_funcref(fn, {Value::make_f64(x)});
            sum += wts[i] * fx.to_double();
        }
        return Value::make_f64(sum * half);
    });

    // ── System / Environment ─────────────────────────────────

    register_native("GETENV$", [](const std::vector<Value>& args) -> Value {
        const char* val = std::getenv(args[0].as_string()->data.c_str());
        return Value::make_string(val ? val : "");
    });

    register_native("SETLOCALE", [](const std::vector<Value>& args) -> Value {
        std::string loc_name = args[0].as_string()->data;
        std::setlocale(LC_ALL, loc_name.c_str());
        try {
            g_jd_locale = std::locale(loc_name.c_str());
            std::cout.imbue(g_jd_locale);
        } catch (const std::runtime_error&) {
            // If the C++ locale fails, keep the previous one
        }
        return Value::make_none();
    });

    register_native("TICK", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - g_program_start).count();
        return Value::make_f64(ms);
    });

    // ── Date / Time ──────────────────────────────────────────
    // DateTime is stored as f64 = seconds since Unix epoch

    register_native("DATE$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::time(nullptr);
        auto* tm = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        return Value::make_string(buf);
    });

    register_native("TIME$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::time(nullptr);
        auto* tm = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
        return Value::make_string(buf);
    });

    register_native("NOW", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto t = std::chrono::system_clock::now();
        double epoch = std::chrono::duration<double>(t.time_since_epoch()).count();
        return Value::make_f64(epoch);
    });

    register_native("CVDATE", [](const std::vector<Value>& args) -> Value {
        // Helper: convert one Value to a DATE (epoch seconds wrapped in DATE tag).
        std::function<Value(const Value&)> one = [&](const Value& v) -> Value {
            // Numeric input → treat as Unix epoch seconds.
            if (v.type == ValueType::INT64 || v.type == ValueType::INT32 ||
                v.type == ValueType::INT16 || v.type == ValueType::BYTE ||
                v.type == ValueType::FLOAT64 || v.type == ValueType::FLOAT32 ||
                v.type == ValueType::FLOAT16 || v.type == ValueType::BOOLEAN) {
                return Value::make_date(v.to_double());
            }
            // Array input → vectorize element-wise.
            if (v.type == ValueType::ARRAY) {
                Value arr = Value::make_array();
                auto* out = arr.as_array();
                for (auto& e : v.as_array()->elements)
                    out->elements.push_back(one(e));
                return arr;
            }
            // String input → parse ISO "YYYY-MM-DD[ HH:MM:SS]".
            std::string s = v.as_string()->data;
            std::tm tm = {};
            std::istringstream ss(s);
            ss >> std::get_time(&tm, "%Y-%m-%d");
            if (ss.fail()) return Value::make_date(0);
            char c;
            if (ss >> c) {
                std::istringstream ts(s.substr(ss.tellg()));
                ts >> std::get_time(&tm, "%H:%M:%S");
            }
            tm.tm_isdst = -1;
            return Value::make_date(static_cast<double>(std::mktime(&tm)));
        };
        return one(args[0]);
    });

    register_native("DATEADD", [](const std::vector<Value>& args) -> Value {
        // DATEADD(part$, num, date_epoch) — preserves the DATE subtype tag.
        std::string part = args[0].as_string()->data;
        double num = args[1].to_double();
        double epoch = args[2].to_double();
        if (part == "D")      epoch += num * 86400;
        else if (part == "H") epoch += num * 3600;
        else if (part == "N") epoch += num * 60;    // N = minutes
        else if (part == "S") epoch += num;
        return Value::make_date(epoch);
    });

    register_native("DATEDIFF", [](const std::vector<Value>& args) -> Value {
        // DATEDIFF(part$, date1, date2) -> number
        std::string part = args[0].as_string()->data;
        double d1 = args[1].to_double();
        double d2 = args[2].to_double();
        double diff = d2 - d1;
        if (part == "D")      return Value::make_f64(diff / 86400);
        else if (part == "H") return Value::make_f64(diff / 3600);
        else if (part == "N") return Value::make_f64(diff / 60);
        else if (part == "S") return Value::make_f64(diff);
        return Value::make_f64(diff);
    });

    // Format a DateTime epoch as string
    register_native("FORMAT_DATE", [](const std::vector<Value>& args) -> Value {
        double epoch = args[0].to_double();
        std::string fmt = (args.size() >= 2) ? args[1].as_string()->data : "%Y-%m-%d %H:%M:%S";
        std::time_t t = static_cast<std::time_t>(epoch);
        auto* tm = std::localtime(&t);
        char buf[128];
        std::strftime(buf, sizeof(buf), fmt.c_str(), tm);
        return Value::make_string(buf);
    });

    // ── MAP functions ────────────────────────────────────────

    register_native("MAP.EXISTS", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        std::string key = args[1].as_string()->data;
        return Value::make_bool(o->get(key) != nullptr);
    });

    register_native("MAP.KEYS", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        Value r = Value::make_array();
        for (auto& [k, v] : o->fields) r.as_array()->elements.push_back(Value::make_string(k));
        return r;
    });

    register_native("MAP.VALUES", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        Value r = Value::make_array();
        for (auto& [k, v] : o->fields) r.as_array()->elements.push_back(v);
        return r;
    });

    register_native("MAP.ITEMS", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        Value r = Value::make_array();
        for (auto& [k, v] : o->fields) {
            Value pair = Value::make_array();
            pair.as_array()->elements.push_back(Value::make_string(k));
            pair.as_array()->elements.push_back(v);
            r.as_array()->elements.push_back(std::move(pair));
        }
        return r;
    });

    register_native("MAP.SIZE", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(args[0].as_object()->fields.size());
    });

    register_native("MAP.DELETE", [](const std::vector<Value>& args) -> Value {
        auto* o = args[0].as_object();
        std::string key = args[1].as_string()->data;
        auto& f = o->fields;
        f.erase(std::remove_if(f.begin(), f.end(), [&](auto& p) { return p.first == key; }), f.end());
        return Value::make_none();
    });

    register_native("MAP.CLEAR", [](const std::vector<Value>& args) -> Value {
        args[0].as_object()->fields.clear();
        return Value::make_none();
    });

    register_native("MAP.MERGE", [](const std::vector<Value>& args) -> Value {
        auto* dst = args[0].as_object();
        auto* src = args[1].as_object();
        for (auto& [k, v] : src->fields) dst->set(k, v);
        return Value::make_none();
    });

    register_native("MAP.FROM", [](const std::vector<Value>& args) -> Value {
        // Simple JSON object parser: {"key":"val", "key2": 123}
        std::string s = args[0].as_string()->data;
        Value m = Value::make_object();
        auto* o = m.as_object();
        size_t i = s.find('{');
        if (i == std::string::npos) return m;
        i++;
        auto skip_ws = [&]() { while (i < s.size() && std::isspace(s[i])) i++; };
        auto parse_str = [&]() -> std::string {
            skip_ws();
            if (i >= s.size() || s[i] != '"') return "";
            i++; // "
            std::string r;
            while (i < s.size() && s[i] != '"') { if (s[i] == '\\' && i+1 < s.size()) { i++; } r += s[i++]; }
            if (i < s.size()) i++; // closing "
            return r;
        };
        while (i < s.size()) {
            skip_ws();
            if (s[i] == '}') break;
            if (s[i] == ',') { i++; continue; }
            std::string key = parse_str();
            skip_ws();
            if (i < s.size() && s[i] == ':') i++;
            skip_ws();
            // Parse value
            if (i < s.size() && s[i] == '"') {
                o->set(key, Value::make_string(parse_str()));
            } else if (i < s.size() && (s[i] == 't' || s[i] == 'f')) {
                bool val = (s[i] == 't');
                while (i < s.size() && std::isalpha(s[i])) i++;
                o->set(key, Value::make_bool(val));
            } else if (i < s.size() && s[i] == 'n') {
                while (i < s.size() && std::isalpha(s[i])) i++;
                o->set(key, Value::make_none());
            } else {
                // Number
                size_t start = i;
                bool is_float = false;
                if (i < s.size() && s[i] == '-') i++;
                while (i < s.size() && (std::isdigit(s[i]) || s[i] == '.')) {
                    if (s[i] == '.') is_float = true;
                    i++;
                }
                std::string ns = s.substr(start, i - start);
                if (is_float) o->set(key, Value::make_f64(std::stod(ns)));
                else o->set(key, Value::make_i64(std::stoll(ns)));
            }
        }
        return m;
    });

    // ── JSON functions ───────────────────────────────────────

    register_native("JSON.PARSE$", [](const std::vector<Value>& args) -> Value {
        // Reuse MAP.FROM for objects, also handle arrays
        std::string s = args[0].as_string()->data;
        size_t p = 0;
        auto skip_ws = [&]() { while (p < s.size() && std::isspace(s[p])) p++; };

        std::function<Value()> parse_value = [&]() -> Value {
            skip_ws();
            if (p >= s.size()) return Value::make_none();
            char c = s[p];
            if (c == '"') {
                p++; std::string r;
                while (p < s.size() && s[p] != '"') {
                    if (s[p] == '\\' && p+1 < s.size()) {
                        p++;
                        switch(s[p]) {
                            case 'n': r += '\n'; break;
                            case 't': r += '\t'; break;
                            case 'r': r += '\r'; break;
                            case '/': r += '/'; break;
                            case '\\': r += '\\'; break;
                            case '"': r += '"'; break;
                            case 'u': {
                                // \uXXXX → UTF-8
                                if (p + 4 < s.size()) {
                                    std::string hex = s.substr(p+1, 4);
                                    unsigned int cp = 0;
                                    for (char h : hex) {
                                        cp <<= 4;
                                        if (h >= '0' && h <= '9') cp |= (h - '0');
                                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                                    }
                                    p += 4;
                                    if (cp < 0x80) { r += (char)cp; }
                                    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
                                    else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
                                } else { r += s[p]; }
                                break;
                            }
                            default: r += s[p];
                        }
                    }
                    else r += s[p];
                    p++;
                }
                if (p < s.size()) p++;
                return Value::make_string(r);
            }
            if (c == '{') {
                p++;
                Value m = Value::make_object();
                auto* o = m.as_object();
                skip_ws();
                if (p < s.size() && s[p] == '}') { p++; return m; }
                while (p < s.size()) {
                    skip_ws();
                    if (s[p] != '"') break;
                    p++; std::string key;
                    while (p < s.size() && s[p] != '"') { key += s[p++]; }
                    if (p < s.size()) p++;
                    skip_ws(); if (p < s.size() && s[p] == ':') p++;
                    o->set(key, parse_value());
                    skip_ws(); if (p < s.size() && s[p] == ',') p++; else break;
                }
                skip_ws(); if (p < s.size() && s[p] == '}') p++;
                return m;
            }
            if (c == '[') {
                p++;
                Value a = Value::make_array();
                skip_ws();
                if (p < s.size() && s[p] == ']') { p++; return a; }
                while (p < s.size()) {
                    a.as_array()->elements.push_back(parse_value());
                    skip_ws(); if (p < s.size() && s[p] == ',') p++; else break;
                }
                skip_ws(); if (p < s.size() && s[p] == ']') p++;
                return a;
            }
            if (c == 't') { p += 4; return Value::make_bool(true); }
            if (c == 'f') { p += 5; return Value::make_bool(false); }
            if (c == 'n') { p += 4; return Value::make_none(); }
            // Number
            size_t start = p; bool is_f = false;
            if (p < s.size() && s[p] == '-') p++;
            while (p < s.size() && (std::isdigit(s[p]) || s[p] == '.' || s[p] == 'e' || s[p] == 'E' || s[p] == '+' || s[p] == '-')) {
                if (s[p] == '.' || s[p] == 'e' || s[p] == 'E') is_f = true;
                p++;
            }
            std::string ns = s.substr(start, p - start);
            if (is_f) return Value::make_f64(std::stod(ns));
            return Value::make_i64(std::stoll(ns));
        };

        return parse_value();
    });

    register_native("JSON.STRINGIFY$", [](const std::vector<Value>& args) -> Value {
        std::function<std::string(const Value&)> to_json = [&](const Value& v) -> std::string {
            switch (v.type) {
                case ValueType::NONE: return "null";
                case ValueType::BOOLEAN: return v.boolean ? "true" : "false";
                case ValueType::STRING: return "\"" + v.as_string()->data + "\"";
                case ValueType::ARRAY: {
                    std::string r = "[";
                    auto* a = v.as_array();
                    for (size_t i = 0; i < a->elements.size(); i++) {
                        if (i > 0) r += ",";
                        r += to_json(a->elements[i]);
                    }
                    return r + "]";
                }
                case ValueType::OBJECT: {
                    std::string r = "{";
                    auto* o = v.as_object();
                    for (size_t i = 0; i < o->fields.size(); i++) {
                        if (i > 0) r += ",";
                        r += "\"" + o->fields[i].first + "\":" + to_json(o->fields[i].second);
                    }
                    return r + "}";
                }
                default: return v.to_string();
            }
        };
        return Value::make_string(to_json(args[0]));
    });

    // ── Console I/O ──────────────────────────────────────────

    register_native("CLS", 0, 3, [this](const std::vector<Value>& args) -> Value {
        uint8_t r = 0, g = 0, b = 0;
        bool has_color = (args.size() >= 3);
        if (has_color) {
            r = (uint8_t)args[0].to_int();
            g = (uint8_t)args[1].to_int();
            b = (uint8_t)args[2].to_int();
        }
#ifdef GFX
        if (gfx_is_active()) {
            gfx_clear(r, g, b);
            return Value::make_none();
        }
#endif
        if (has_color)
            emit("\033[48;2;" + std::to_string((int)r) + ";" + std::to_string((int)g) + ";" + std::to_string((int)b) + "m");
        emit("\033[2J\033[H");
        return Value::make_none();
    });

    register_native("LOCATE", [this](const std::vector<Value>& args) -> Value {
        int row = (int)args[0].to_int();
        int col = (int)args[1].to_int();
        emit("\033[" + std::to_string(row) + ";" + std::to_string(col) + "H");
        return Value::make_none();
    });

    register_native("COLOR", [](const std::vector<Value>& args) -> Value {
#if defined(_WIN32)
        int fg = (int)args[0].to_int();
        int bg = (args.size() >= 2) ? (int)args[1].to_int() : 0;
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hOut, (WORD)(fg | (bg << 4)));
#endif
        return Value::make_none();
    });

    register_native("CURSOR", [this](const std::vector<Value>& args) -> Value {
        if (args[0].to_bool())
            emit("\033[?25h");
        else
            emit("\033[?25l");
        return Value::make_none();
    });

    register_native("SLEEP", [this](const std::vector<Value>& args) -> Value {
        int ms = (int)args[0].to_int();
        // Slice the wait so events get polled while sleeping. Without this,
        // a tight `DO sleep 15 LOOP` could starve key/quit events for many
        // seconds — the periodic on_tick fires only every 2000 VM ticks.
        const int slice_ms = 5;
        int remaining = ms;
        while (remaining > 0) {
            int chunk = remaining > slice_ms ? slice_ms : remaining;
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
            remaining -= chunk;
            if (on_tick) on_tick();
            if (!event_handlers.empty()) event_poll();
            if (is_halted) break; // event handler may have run END
        }
        return Value::make_none();
    });

    register_native("GETX", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO ci;
        if (GetConsoleScreenBufferInfo(h, &ci))
            return Value::make_i64(ci.dwCursorPosition.X + 1);
#endif
        return Value::make_i64(1);
    });

    register_native("GETY", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO ci;
        if (GetConsoleScreenBufferInfo(h, &ci))
            return Value::make_i64(ci.dwCursorPosition.Y + 1);
#endif
        return Value::make_i64(1);
    });

    register_native("INKEY$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#ifdef GFX
        extern bool gfx_is_active();
        extern bool gfx_has_key();
        extern std::string gfx_get_key();
        extern void gfx_pump_events();
        if (gfx_is_active()) {
            gfx_pump_events();
            return Value::make_string(gfx_has_key() ? gfx_get_key() : "");
        }
#endif
#if defined(_WIN32)
        static auto last_check = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check).count() >= 100) {
            last_check = now;
            if (_kbhit()) {
                int ch = _getch();
                return Value::make_string(std::string(1, (char)ch));
            }
        }
#endif
        return Value::make_string("");
    });

    register_native("WAITKEY$", 0, -1, [this](const std::vector<Value>& args) -> Value {
        (void)args;
#ifdef GFX
        extern bool gfx_is_active();
        extern bool gfx_has_key();
        extern std::string gfx_get_key();
        extern void gfx_pump_events();
        if (gfx_is_active()) {
            // SDL window has focus, console won't see any keys. Pump SDL
            // events directly so the window stays responsive (otherwise
            // Windows shows the spinning-cursor "not responding" state) and
            // KEYDOWN events get buffered into g_key_available.
            while (true) {
                gfx_pump_events();
                if (gfx_has_key()) return Value::make_string(gfx_get_key());
                if (on_tick) on_tick();
                if (!event_handlers.empty()) event_poll();
                if (is_halted) return Value::make_string("");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
#endif
#if defined(_WIN32)
        int ch = _getch();
        return Value::make_string(std::string(1, (char)ch));
#else
        return Value::make_string("");
#endif
    });

    register_native("CLIPBOARD.SET", [](const std::vector<Value>& args) -> Value {
#if defined(_WIN32)
        std::string text = args[0].to_string();
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (hg) { memcpy(GlobalLock(hg), text.c_str(), text.size() + 1); GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg); }
            CloseClipboard();
        }
#endif
        return Value::make_none();
    });

    register_native("CLIPBOARD.GET$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        if (OpenClipboard(NULL)) {
            HANDLE hg = GetClipboardData(CF_TEXT);
            if (hg) {
                char* s = static_cast<char*>(GlobalLock(hg));
                if (s) { std::string r(s); GlobalUnlock(hg); CloseClipboard(); return Value::make_string(r); }
            }
            CloseClipboard();
        }
#endif
        return Value::make_string("");
    });

    register_native("OPTION", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_none();
        std::string opt = args[0].as_string()->data;
        std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
        if (opt == "NOCOLOR" || opt == "PLAINTEXT") g_color_errors = false;
        else if (opt == "COLOR") g_color_errors = true;
        return Value::make_none();
    });

    register_native("LCASE$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return Value::make_string(s);
    });

    register_native("SHL", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(args[0].to_int() << args[1].to_int());
    });
    register_native("SHR", [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(args[0].to_int() >> args[1].to_int());
    });

    // ── File I/O ─────────────────────────────────────────────

    register_native("TXTREADER$", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::ifstream f(fname);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        std::ostringstream ss;
        ss << f.rdbuf();
        return Value::make_string(ss.str());
    });

    register_native("TXTWRITER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string content = args[1].as_string()->data;
        std::ofstream f(fname);
        if (!f) throw std::runtime_error("Cannot write file: " + fname);
        f << content;
        return Value::make_none();
    });

    register_native("BINREADER$", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::ifstream f(fname, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        size_t sz = f.tellg();
        f.seekg(0);
        std::string data(sz, '\0');
        f.read(&data[0], sz);
        return Value::make_string(data);
    });

    register_native("BINWRITER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::ofstream f(fname, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot write file: " + fname);
        f.write(data.c_str(), data.size());
        return Value::make_none();
    });

    register_native("CSVREADER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        std::string delim = (args.size() >= 2) ? args[1].as_string()->data : ",";
        bool has_header = (args.size() >= 3) ? args[2].to_bool() : false;
        std::ifstream f(fname);
        if (!f) throw std::runtime_error("Cannot open file: " + fname);
        Value result = Value::make_array();
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            // Skip header row
            if (first && has_header) { first = false; continue; }
            first = false;
            // Remove trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Split by delimiter
            Value row = Value::make_array();
            size_t pos = 0;
            while (true) {
                size_t found = line.find(delim, pos);
                std::string cell = (found == std::string::npos)
                    ? line.substr(pos) : line.substr(pos, found - pos);
                // Strip quotes
                if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"')
                    cell = cell.substr(1, cell.size() - 2);
                // Try to parse as number
                try {
                    size_t np;
                    int64_t iv = std::stoll(cell, &np);
                    if (np == cell.size()) { row.as_array()->elements.push_back(Value::make_i64(iv)); }
                    else {
                        double dv = std::stod(cell, &np);
                        if (np == cell.size()) row.as_array()->elements.push_back(Value::make_f64(dv));
                        else row.as_array()->elements.push_back(Value::make_string(cell));
                    }
                } catch (...) {
                    row.as_array()->elements.push_back(Value::make_string(cell));
                }
                if (found == std::string::npos) break;
                pos = found + delim.size();
            }
            result.as_array()->elements.push_back(std::move(row));
        }
        return result;
    });

    register_native("CSVWRITER", [](const std::vector<Value>& args) -> Value {
        std::string fname = args[0].as_string()->data;
        auto* data = args[1].as_array();
        std::string delim = (args.size() >= 3) ? args[2].as_string()->data : ",";
        std::ofstream f(fname);
        if (!f) throw std::runtime_error("Cannot write file: " + fname);
        // Optional header array
        if (args.size() >= 4 && args[3].type == ValueType::ARRAY) {
            auto* hdr = args[3].as_array();
            for (size_t i = 0; i < hdr->elements.size(); i++) {
                if (i > 0) f << delim;
                f << hdr->elements[i].to_string();
            }
            f << "\n";
        }
        // Data rows
        for (auto& row : data->elements) {
            if (row.type == ValueType::ARRAY) {
                for (size_t i = 0; i < row.as_array()->elements.size(); i++) {
                    if (i > 0) f << delim;
                    auto& cell = row.as_array()->elements[i];
                    if (cell.type == ValueType::STRING) {
                        std::string s = cell.as_string()->data;
                        if (s.find(delim) != std::string::npos || s.find('"') != std::string::npos)
                            f << "\"" << s << "\"";
                        else f << s;
                    } else {
                        f << cell.to_string();
                    }
                }
            } else {
                f << row.to_string();
            }
            f << "\n";
        }
        return Value::make_none();
    });

    // ── String functions ($ aliases + new ones) ──────────────

    // $ aliases for existing functions
    // String slicing helpers — implicitly stringify non-string inputs so
    // callers can pass e.g. a DATE-tagged FLOAT64 or a number directly,
    // matching classic BASIC's loose typing.
    register_native("LEFT$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].to_string();
        int64_t n = args[1].to_int();
        if (n < 0) n = 0;
        if (n > (int64_t)s.size()) n = (int64_t)s.size();
        return Value::make_string(s.substr(0, n));
    });
    register_native("RIGHT$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].to_string();
        int64_t n = args[1].to_int();
        if (n < 0) n = 0;
        if (n >= (int64_t)s.size()) return Value::make_string(s);
        return Value::make_string(s.substr(s.size() - n));
    });
    register_native("MID$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].to_string();
        int64_t start = args[1].to_int();
        int64_t len = args.size() > 2 ? args[2].to_int() : (int64_t)s.size();
        if (start < 0) start = 0;
        if (start > (int64_t)s.size()) return Value::make_string("");
        return Value::make_string(s.substr(start, len));
    });
    register_native("UCASE$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return Value::make_string(s);
    });
    register_native("STR$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });
    register_native("CHR$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(std::string(1, (char)args[0].to_int()));
    });
    register_native("INSTR$", [](const std::vector<Value>& args) -> Value {
        // Same as INSTR
        int64_t start = 0;
        std::string haystack, needle;
        if (args.size() >= 3) {
            start = args[0].to_int();
            haystack = args[1].as_string()->data;
            needle = args[2].as_string()->data;
        } else {
            haystack = args[0].as_string()->data;
            needle = args[1].as_string()->data;
        }
        size_t pos = haystack.find(needle, start);
        return Value::make_i64(pos == std::string::npos ? -1 : (int64_t)pos);
    });

    // TRIM$
    register_native("TRIM$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return Value::make_string(a == std::string::npos ? "" : s.substr(a, b - a + 1));
    });
    register_native("TRIM", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return Value::make_string(a == std::string::npos ? "" : s.substr(a, b - a + 1));
    });

    // INSTR
    register_native("INSTR", [](const std::vector<Value>& args) -> Value {
        int64_t start = 0;
        std::string haystack, needle;
        if (args.size() >= 3) {
            start = args[0].to_int();
            haystack = args[1].as_string()->data;
            needle = args[2].as_string()->data;
        } else {
            haystack = args[0].as_string()->data;
            needle = args[1].as_string()->data;
        }
        size_t pos = haystack.find(needle, start);
        return Value::make_i64(pos == std::string::npos ? -1 : (int64_t)pos);
    });

    // INSERT$: insert text into string or element into array at position
    register_native("INSERT$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING) {
            std::string s = args[0].as_string()->data;
            std::string ins = args[1].as_string()->data;
            int64_t pos = args[2].to_int();
            if (pos < 0) pos = 0;
            if (pos > (int64_t)s.size()) pos = s.size();
            s.insert(pos, ins);
            return Value::make_string(s);
        }
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            r.as_array()->elements = args[0].as_array()->elements;
            int64_t pos = args[2].to_int();
            if (pos < 0) pos = 0;
            if (pos > (int64_t)r.as_array()->elements.size()) pos = r.as_array()->elements.size();
            r.as_array()->elements.insert(r.as_array()->elements.begin() + pos, args[1]);
            return r;
        }
        return args[0];
    });

    // SPLIT: split string by delimiter
    register_native("SPLIT", [](const std::vector<Value>& args) -> Value {
        std::string src = args[0].as_string()->data;
        std::string delim = args[1].as_string()->data;
        Value r = Value::make_array();
        if (delim.empty()) {
            r.as_array()->elements.push_back(Value::make_string(src));
            return r;
        }
        size_t pos = 0;
        while (true) {
            size_t found = src.find(delim, pos);
            if (found == std::string::npos) {
                r.as_array()->elements.push_back(Value::make_string(src.substr(pos)));
                break;
            }
            r.as_array()->elements.push_back(Value::make_string(src.substr(pos, found - pos)));
            pos = found + delim.size();
        }
        return r;
    });

    // REPLACE$: replace all occurrences
    register_native("REPLACE$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING) {
            std::string s = args[0].as_string()->data;
            std::string find = args[1].as_string()->data;
            std::string repl = args[2].as_string()->data;
            if (!find.empty()) {
                size_t pos = 0;
                while ((pos = s.find(find, pos)) != std::string::npos) {
                    s.replace(pos, find.size(), repl);
                    pos += repl.size();
                }
            }
            return Value::make_string(s);
        }
        // Array: element-wise
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            for (auto& e : args[0].as_array()->elements) {
                if (e.type == ValueType::STRING) {
                    std::string s = e.as_string()->data;
                    std::string find = args[1].as_string()->data;
                    std::string repl = args[2].as_string()->data;
                    if (!find.empty()) {
                        size_t pos = 0;
                        while ((pos = s.find(find, pos)) != std::string::npos) {
                            s.replace(pos, find.size(), repl);
                            pos += repl.size();
                        }
                    }
                    r.as_array()->elements.push_back(Value::make_string(s));
                } else {
                    r.as_array()->elements.push_back(e);
                }
            }
            return r;
        }
        return args[0];
    });

    // REVERSE$: reverse a string (REVERSE for arrays already exists)
    register_native("REVERSE$", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::STRING) {
            std::string s = args[0].as_string()->data;
            // UTF-8 aware reverse
            std::vector<std::string> chars;
            for (size_t i = 0; i < s.size(); ) {
                unsigned char c = s[i];
                int len = 1;
                if (c >= 0xF0) len = 4;
                else if (c >= 0xE0) len = 3;
                else if (c >= 0xC0) len = 2;
                chars.push_back(s.substr(i, len));
                i += len;
            }
            std::string r;
            for (int i = (int)chars.size() - 1; i >= 0; i--) r += chars[i];
            return Value::make_string(r);
        }
        // Array: delegate to REVERSE
        if (args[0].type == ValueType::ARRAY) {
            Value r = Value::make_array();
            auto& src = args[0].as_array()->elements;
            r.as_array()->elements.assign(src.rbegin(), src.rend());
            return r;
        }
        return args[0];
    });

    // BYTEAT: raw byte value at index
    register_native("BYTEAT", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        int64_t idx = args[1].to_int();
        if (idx < 0 || idx >= (int64_t)s.size()) return Value::make_i64(0);
        return Value::make_i64((uint8_t)s[idx]);
    });

    // FORMAT$: C++20-style string formatting
    register_native("FORMAT$", [](const std::vector<Value>& args) -> Value {
        std::string fmt = args[0].as_string()->data;
        std::string result;
        size_t arg_idx = 1; // next auto-arg
        size_t i = 0;
        while (i < fmt.size()) {
            if (fmt[i] == '{' && i + 1 < fmt.size()) {
                if (fmt[i + 1] == '{') { result += '{'; i += 2; continue; } // escaped {{
                size_t end = fmt.find('}', i);
                if (end == std::string::npos) { result += fmt[i++]; continue; }
                std::string spec = fmt.substr(i + 1, end - i - 1);
                i = end + 1;

                // Parse: [index][:format]
                size_t colon = spec.find(':');
                std::string idx_str = (colon != std::string::npos) ? spec.substr(0, colon) : spec;
                std::string fmt_spec = (colon != std::string::npos) ? spec.substr(colon + 1) : "";

                size_t ai;
                if (!idx_str.empty() && std::isdigit(idx_str[0]))
                    ai = std::stoi(idx_str) + 1; // +1 because args[0] is format string
                else
                    ai = arg_idx++;

                if (ai >= args.size()) { result += "?"; continue; }
                const Value& v = args[ai];

                if (fmt_spec.empty()) {
                    result += v.to_string();
                } else {
                    // Parse fill, align, width, precision, type
                    char fill = ' ', align = '>';
                    size_t fp = 0;
                    if (fmt_spec.size() >= 2 && (fmt_spec[1] == '<' || fmt_spec[1] == '>' || fmt_spec[1] == '^')) {
                        fill = fmt_spec[0]; align = fmt_spec[1]; fp = 2;
                    } else if (!fmt_spec.empty() && (fmt_spec[0] == '<' || fmt_spec[0] == '>' || fmt_spec[0] == '^')) {
                        align = fmt_spec[0]; fp = 1;
                    }
                    // Parse width
                    int width = 0;
                    while (fp < fmt_spec.size() && std::isdigit(fmt_spec[fp]))
                        width = width * 10 + (fmt_spec[fp++] - '0');
                    // Parse .precision
                    int prec = -1;
                    if (fp < fmt_spec.size() && fmt_spec[fp] == '.') {
                        fp++; prec = 0;
                        while (fp < fmt_spec.size() && std::isdigit(fmt_spec[fp]))
                            prec = prec * 10 + (fmt_spec[fp++] - '0');
                    }
                    // Parse # flag and type
                    bool hash_flag = false;
                    char type = 0;
                    if (fp < fmt_spec.size() && fmt_spec[fp] == '#') { hash_flag = true; fp++; }
                    if (fp < fmt_spec.size()) type = fmt_spec[fp];

                    // Format the value
                    std::string sv;
                    if (type == 'd') {
                        sv = std::to_string(v.to_int());
                    } else if (type == 'f') {
                        std::ostringstream os;
                        if (prec >= 0) os << std::fixed << std::setprecision(prec);
                        os << v.to_double();
                        sv = os.str();
                    } else if (type == 'x') {
                        char buf[32]; snprintf(buf, sizeof(buf), "%llx", (long long)v.to_int()); sv = buf;
                        if (hash_flag) sv = "0x" + sv;
                    } else if (type == 'X') {
                        char buf[32]; snprintf(buf, sizeof(buf), "%llX", (long long)v.to_int()); sv = buf;
                        if (hash_flag) sv = "0x" + sv;
                    } else {
                        sv = v.to_string();
                        if (prec >= 0 && is_numeric(v.type)) {
                            std::ostringstream os;
                            os << std::fixed << std::setprecision(prec) << v.to_double();
                            sv = os.str();
                        }
                    }

                    // Apply width and alignment
                    if (width > 0 && (int)sv.size() < width) {
                        int pad = width - (int)sv.size();
                        if (align == '<') sv += std::string(pad, fill);
                        else if (align == '>') sv = std::string(pad, fill) + sv;
                        else { int l = pad / 2; sv = std::string(l, fill) + sv + std::string(pad - l, fill); }
                    }
                    result += sv;
                }
            } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
                result += '}'; i += 2;
            } else {
                result += fmt[i++];
            }
        }
        return Value::make_string(result);
    });

    // FRMV$: format array as aligned table
    register_native("FRMV$", [this](const std::vector<Value>& args) -> Value {
        bool is_2d = (args[0].type == ValueType::ARRAY && !args[0].as_array()->elements.empty() &&
                      args[0].as_array()->elements[0].type == ValueType::ARRAY);
        bool has_fmt = (args.size() >= 2 && args[1].type == ValueType::STRING);
        std::string result;

        if (has_fmt) {
            // Apply format string per row: FORMAT$(fmt, col1, col2, ...)
            std::string fmt = args[1].as_string()->data;
            if (is_2d) {
                for (auto& row : args[0].as_array()->elements) {
                    if (row.type != ValueType::ARRAY) continue;
                    std::vector<Value> fmt_args;
                    fmt_args.push_back(Value::make_string(fmt));
                    for (auto& cell : row.as_array()->elements) fmt_args.push_back(cell);
                    Value line = call_function("FORMAT$", fmt_args);
                    result += line.as_string()->data + "\n";
                }
            } else {
                // 1D: format each element
                for (auto& e : args[0].as_array()->elements) {
                    std::vector<Value> fmt_args;
                    fmt_args.push_back(Value::make_string(fmt));
                    fmt_args.push_back(e);
                    Value line = call_function("FORMAT$", fmt_args);
                    result += line.as_string()->data + "\n";
                }
            }
        } else if (is_2d) {
            auto* rows = args[0].as_array();
            int cols = 0;
            for (auto& row : rows->elements)
                if (row.type == ValueType::ARRAY)
                    cols = std::max(cols, (int)row.as_array()->elements.size());
            std::vector<int> widths(cols, 0);
            for (auto& row : rows->elements) {
                if (row.type != ValueType::ARRAY) continue;
                for (int c = 0; c < (int)row.as_array()->elements.size(); c++) {
                    int w = (int)row.as_array()->elements[c].to_string().size();
                    if (w > widths[c]) widths[c] = w;
                }
            }
            for (auto& row : rows->elements) {
                if (row.type != ValueType::ARRAY) continue;
                for (int c = 0; c < (int)row.as_array()->elements.size(); c++) {
                    std::string s = row.as_array()->elements[c].to_string();
                    int pad = widths[c] - (int)s.size();
                    if (pad > 0) result += std::string(pad, ' ');
                    result += s;
                    if (c + 1 < (int)row.as_array()->elements.size()) result += " ";
                }
                result += "\n";
            }
        } else {
            auto flat = flatten_values(args[0]);
            for (auto& v : flat) result += v.to_string() + "\n";
        }
        return Value::make_string(result);
    });

    // PACK$: pack values to binary string. Format chars are CASE-INSENSITIVE
    // (so `>Isbsd` and `>isbsd` are equivalent — matches the docs which use
    // `I = Integer (4 bytes)`). Unknown chars throw a clean error so a typo
    // can't silently drop a value and produce a too-short string.
    //   < / >  little / big endian
    //   b      byte    (1)
    //   s      short   (2)
    //   i      int     (4)
    //   l      long    (8)
    //   f      float   (4)
    //   d      double  (8)
    register_native("PACK$", [](const std::vector<Value>& args) -> Value {
        std::string fmt = args[0].as_string()->data;
        std::string result;
        bool big_endian = false;
        size_t ai = 1;
        for (char c : fmt) {
            if (c == '<') { big_endian = false; continue; }
            if (c == '>') { big_endian = true; continue; }
            if (c == ' ' || c == '\t') continue;  // allow whitespace
            char lc = (char)std::tolower((unsigned char)c);
            int width = 0;
            switch (lc) {
                case 'b': width = 1; break;
                case 's': width = 2; break;
                case 'i': width = 4; break;
                case 'l': width = 8; break;
                case 'f': width = 4; break;
                case 'd': width = 8; break;
                default:
                    throw std::runtime_error(std::string("PACK$: unknown format char '") + c + "'");
            }
            if (ai >= args.size())
                throw std::runtime_error("PACK$: not enough values for format");
            auto write = [&](const void* data, int n) {
                const uint8_t* p = (const uint8_t*)data;
                if (big_endian) for (int i = n - 1; i >= 0; i--) result += (char)p[i];
                else for (int i = 0; i < n; i++) result += (char)p[i];
            };
            (void)width;
            if (lc == 'b') { uint8_t v = (uint8_t)args[ai++].to_int(); write(&v, 1); }
            else if (lc == 's') { int16_t v = (int16_t)args[ai++].to_int(); write(&v, 2); }
            else if (lc == 'i') { int32_t v = (int32_t)args[ai++].to_int(); write(&v, 4); }
            else if (lc == 'l') { int64_t v = args[ai++].to_int(); write(&v, 8); }
            else if (lc == 'f') { float v = (float)args[ai++].to_double(); write(&v, 4); }
            else if (lc == 'd') { double v = args[ai++].to_double(); write(&v, 8); }
        }
        return Value::make_string(result);
    });

    // UNPACK: unpack binary string to array. Same case-insensitive format
    // chars as PACK$.
    register_native("UNPACK", [](const std::vector<Value>& args) -> Value {
        std::string fmt = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        Value r = Value::make_array();
        bool big_endian = false;
        size_t pos = 0;
        auto read_bytes = [&](void* dst, int n) {
            uint8_t* p = (uint8_t*)dst;
            if (big_endian) for (int i = n - 1; i >= 0 && pos < data.size(); i--) p[i] = (uint8_t)data[pos++];
            else for (int i = 0; i < n && pos < data.size(); i++) p[i] = (uint8_t)data[pos++];
        };
        for (char c : fmt) {
            if (c == '<') { big_endian = false; continue; }
            if (c == '>') { big_endian = true; continue; }
            if (c == ' ' || c == '\t') continue;
            char lc = (char)std::tolower((unsigned char)c);
            // Treat unpacked integers as UNSIGNED (zero-extend) so binary
            // file headers like 0xCAFEBABE round-trip cleanly. BASIC users
            // rarely deal with signed binary fields.
            if      (lc == 'b') { uint8_t  v = 0; read_bytes(&v, 1); r.as_array()->elements.push_back(Value::make_i64((int64_t)v)); }
            else if (lc == 's') { uint16_t v = 0; read_bytes(&v, 2); r.as_array()->elements.push_back(Value::make_i64((int64_t)v)); }
            else if (lc == 'i') { uint32_t v = 0; read_bytes(&v, 4); r.as_array()->elements.push_back(Value::make_i64((int64_t)v)); }
            else if (lc == 'l') { int64_t  v = 0; read_bytes(&v, 8); r.as_array()->elements.push_back(Value::make_i64(v)); }
            else if (lc == 'f') { float v = 0; read_bytes(&v, 4); r.as_array()->elements.push_back(Value::make_f64(v)); }
            else if (lc == 'd') { double v = 0; read_bytes(&v, 8); r.as_array()->elements.push_back(Value::make_f64(v)); }
            else throw std::runtime_error(std::string("UNPACK: unknown format char '") + c + "'");
        }
        return r;
    });

    // ── 1. Additional Math ─────────────────────────────────────

    register_native("ASIN", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::asin(args[0].to_double()));
    });
    register_native("ACOS", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::acos(args[0].to_double()));
    });
    register_native("ATAN", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::atan(args[0].to_double()));
    });
    register_native("ATAN2", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::atan2(args[0].to_double(), args[1].to_double()));
    });
    register_native("SINH", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::sinh(args[0].to_double()));
    });
    register_native("COSH", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::cosh(args[0].to_double()));
    });
    register_native("TANH", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(std::tanh(args[0].to_double()));
    });
    register_native("SGN", [](const std::vector<Value>& args) -> Value {
        double v = args[0].to_double();
        return Value::make_i64(v > 0 ? 1 : (v < 0 ? -1 : 0));
    });
    // Math constants — registered as protected constants, not functions
    // Can be used as PI or PI() — both work
    register_const("PI", Value::make_f64(3.14159265358979323846));
    register_const("E", Value::make_f64(2.71828182845904523536));
    // Keep function versions too for PI() syntax
    register_native("PI", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_f64(3.14159265358979323846);
    });
    register_native("E", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_f64(2.71828182845904523536);
    });
    register_native("VBNEWLINE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string("\r\n");
    });
    register_native("VBCRLF", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string("\r\n");
    });
    register_native("VBTAB", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_string("\t");
    });
    register_native("RANDOM", [](const std::vector<Value>& args) -> Value {
        double lo = args[0].to_double(), hi = args[1].to_double();
        return Value::make_f64(lo + (double)rand() / RAND_MAX * (hi - lo));
    });
    register_native("RANDOMSEED", [](const std::vector<Value>& args) -> Value {
        srand((unsigned)args[0].to_int()); return Value::make_none();
    });

    // ── 2. Additional String ─────────────────────────────────

    register_native("JOIN", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        std::string delim = (args.size() >= 2) ? args[1].as_string()->data : "";
        std::string r;
        for (size_t i = 0; i < arr->elements.size(); i++) {
            if (i > 0) r += delim;
            r += arr->elements[i].to_string();
        }
        return Value::make_string(r);
    });
    register_native("STARTSWITH", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, p = args[1].as_string()->data;
        return Value::make_bool(s.size() >= p.size() && s.substr(0, p.size()) == p);
    });
    register_native("ENDSWITH", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, p = args[1].as_string()->data;
        return Value::make_bool(s.size() >= p.size() && s.substr(s.size() - p.size()) == p);
    });
    register_native("LTRIM$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t p = s.find_first_not_of(" \t\r\n");
        return Value::make_string(p == std::string::npos ? "" : s.substr(p));
    });
    register_native("RTRIM$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data;
        size_t p = s.find_last_not_of(" \t\r\n");
        return Value::make_string(p == std::string::npos ? "" : s.substr(0, p + 1));
    });
    register_native("SPACE$", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(std::string((size_t)args[0].to_int(), ' '));
    });
    register_native("HEX$", [](const std::vector<Value>& args) -> Value {
        char buf[32]; snprintf(buf, sizeof(buf), "%llX", (long long)args[0].to_int());
        return Value::make_string(buf);
    });
    register_native("BIN$", [](const std::vector<Value>& args) -> Value {
        int64_t v = args[0].to_int(); std::string r;
        if (v == 0) return Value::make_string("0");
        uint64_t u = (uint64_t)v; bool started = false;
        for (int i = 63; i >= 0; i--) {
            if (u & (1ULL << i)) { started = true; r += '1'; }
            else if (started) r += '0';
        }
        return Value::make_string(r);
    });
    register_native("OCT$", [](const std::vector<Value>& args) -> Value {
        char buf[32]; snprintf(buf, sizeof(buf), "%llo", (long long)args[0].to_int());
        return Value::make_string(buf);
    });
    register_native("REGEX_MATCH", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, pat = args[1].as_string()->data;
        Value r = Value::make_array();
        try {
            std::regex re(pat);
            std::sregex_iterator it(s.begin(), s.end(), re), end;
            for (; it != end; ++it)
                r.as_array()->elements.push_back(Value::make_string((*it)[0].str()));
        } catch (...) { }
        return r;
    });
    register_native("REGEX_REPLACE$", [](const std::vector<Value>& args) -> Value {
        std::string s = args[0].as_string()->data, pat = args[1].as_string()->data, repl = args[2].as_string()->data;
        try { return Value::make_string(std::regex_replace(s, std::regex(pat), repl)); }
        catch (...) { return Value::make_string(s); }
    });

    // REGEX.MATCH(pattern, text) → TRUE/FALSE or array of captures
    register_native("REGEX.MATCH", [](const std::vector<Value>& args) -> Value {
        std::string pat = args[0].as_string()->data;
        std::string text = args[1].as_string()->data;
        try {
            std::regex re(pat);
            std::smatch m;
            if (!std::regex_match(text, m, re)) return Value::make_bool(false);
            // No capture groups → just TRUE
            if (m.size() <= 1) return Value::make_bool(true);
            // Has capture groups → return array of captures
            Value r = Value::make_array();
            for (size_t i = 1; i < m.size(); i++)
                r.as_array()->elements.push_back(Value::make_string(m[i].str()));
            return r;
        } catch (...) { return Value::make_bool(false); }
    });

    // REGEX.FINDALL(pattern, text) → 1D array of matches or 2D if groups
    register_native("REGEX.FINDALL", [](const std::vector<Value>& args) -> Value {
        std::string pat = args[0].as_string()->data;
        std::string text = args[1].as_string()->data;
        Value r = Value::make_array();
        try {
            std::regex re(pat);
            std::sregex_iterator it(text.begin(), text.end(), re), end;
            bool has_groups = false;
            for (; it != end; ++it) {
                auto& m = *it;
                if (m.size() > 1) {
                    // Has capture groups → row per match
                    has_groups = true;
                    Value row = Value::make_array();
                    for (size_t i = 1; i < m.size(); i++)
                        row.as_array()->elements.push_back(Value::make_string(m[i].str()));
                    r.as_array()->elements.push_back(std::move(row));
                } else {
                    r.as_array()->elements.push_back(Value::make_string(m[0].str()));
                }
            }
        } catch (...) { }
        return r;
    });

    // REGEX.REPLACE(pattern, text, replacement) → string
    register_native("REGEX.REPLACE", [](const std::vector<Value>& args) -> Value {
        std::string pat = args[0].as_string()->data;
        std::string text = args[1].as_string()->data;
        std::string repl = args[2].as_string()->data;
        try { return Value::make_string(std::regex_replace(text, std::regex(pat), repl)); }
        catch (...) { return Value::make_string(text); }
    });

    // ── 3. Statistics ────────────────────────────────────────

    register_native("MEAN", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.empty()) return Value::make_f64(0);
        double s = 0; for (auto& v : flat) s += v.to_double();
        return Value::make_f64(s / flat.size());
    });
    register_native("MEDIAN", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.empty()) return Value::make_f64(0);
        std::vector<double> vals; for (auto& v : flat) vals.push_back(v.to_double());
        std::sort(vals.begin(), vals.end());
        size_t n = vals.size();
        return Value::make_f64(n % 2 ? vals[n/2] : (vals[n/2-1] + vals[n/2]) / 2.0);
    });
    register_native("VARIANCE", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.size() < 2) return Value::make_f64(0);
        double s = 0; for (auto& v : flat) s += v.to_double();
        double mean = s / flat.size(); double ss = 0;
        for (auto& v : flat) { double d = v.to_double() - mean; ss += d * d; }
        return Value::make_f64(ss / flat.size());
    });
    register_native("STDEV", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        if (flat.size() < 2) return Value::make_f64(0);
        double s = 0; for (auto& v : flat) s += v.to_double();
        double mean = s / flat.size(); double ss = 0;
        for (auto& v : flat) { double d = v.to_double() - mean; ss += d * d; }
        return Value::make_f64(std::sqrt(ss / flat.size()));
    });
    register_native("DOT", [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array(); auto* b = args[1].as_array();
        double s = 0; size_t n = std::min(a->elements.size(), b->elements.size());
        for (size_t i = 0; i < n; i++) s += a->elements[i].to_double() * b->elements[i].to_double();
        return Value::make_f64(s);
    });
    register_native("CROSS", [](const std::vector<Value>& args) -> Value {
        auto* a = args[0].as_array(); auto* b = args[1].as_array();
        if (a->elements.size() < 3 || b->elements.size() < 3) throw std::runtime_error("CROSS requires 3D vectors");
        double ax=a->elements[0].to_double(), ay=a->elements[1].to_double(), az=a->elements[2].to_double();
        double bx=b->elements[0].to_double(), by=b->elements[1].to_double(), bz=b->elements[2].to_double();
        Value r = Value::make_array();
        r.as_array()->elements.push_back(Value::make_f64(ay*bz - az*by));
        r.as_array()->elements.push_back(Value::make_f64(az*bx - ax*bz));
        r.as_array()->elements.push_back(Value::make_f64(ax*by - ay*bx));
        return r;
    });
    register_native("CUMSUM", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        Value r = Value::make_array(); double s = 0;
        for (auto& v : arr->elements) { s += v.to_double(); r.as_array()->elements.push_back(Value::make_f64(s)); }
        return r;
    });
    register_native("CUMPROD", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        Value r = Value::make_array(); double s = 1;
        for (auto& v : arr->elements) { s *= v.to_double(); r.as_array()->elements.push_back(Value::make_f64(s)); }
        return r;
    });
    register_native("HISTOGRAM", [](const std::vector<Value>& args) -> Value {
        auto flat = flatten_values(args[0]);
        int bins = (args.size() >= 2) ? (int)args[1].to_int() : 10;
        double mn = flat[0].to_double(), mx = mn;
        for (auto& v : flat) { double d = v.to_double(); if (d<mn) mn=d; if (d>mx) mx=d; }
        double range = mx - mn; if (range == 0) range = 1;
        Value r = Value::make_array();
        r.as_array()->elements.resize(bins, Value::make_i64(0));
        for (auto& v : flat) {
            int b = (int)((v.to_double() - mn) / range * bins);
            if (b >= bins) b = bins - 1; if (b < 0) b = 0;
            r.as_array()->elements[b] = Value::make_i64(r.as_array()->elements[b].to_int() + 1);
        }
        return r;
    });
    register_native("LINSPACE", [](const std::vector<Value>& args) -> Value {
        double start = args[0].to_double(), end = args[1].to_double();
        int n = (int)args[2].to_int();
        Value r = Value::make_array();
        if (n <= 1) { r.as_array()->elements.push_back(Value::make_f64(start)); return r; }
        for (int i = 0; i < n; i++)
            r.as_array()->elements.push_back(Value::make_f64(start + (end - start) * i / (n - 1)));
        return r;
    });

    // ── 4. Array Utilities ───────────────────────────────────

    register_native("FLATTEN", [](const std::vector<Value>& args) -> Value {
        Value r = Value::make_array();
        r.as_array()->elements = flatten_values(args[0]);
        return r;
    });
    register_native("ZIP", [](const std::vector<Value>& args) -> Value {
        size_t n = args[0].as_array()->elements.size();
        for (size_t a = 1; a < args.size(); a++)
            n = std::min(n, args[a].as_array()->elements.size());
        Value r = Value::make_array();
        for (size_t i = 0; i < n; i++) {
            Value row = Value::make_array();
            for (size_t a = 0; a < args.size(); a++)
                row.as_array()->elements.push_back(args[a].as_array()->elements[i]);
            r.as_array()->elements.push_back(std::move(row));
        }
        return r;
    });
    // ZEROS / ONES: accept either a scalar (1-D vector of given length) or
    // a shape_vector ([rows, cols, ...]) for N-dimensional arrays.
    auto make_filled = [](const Value& shape_arg, const Value& fill) -> Value {
        std::vector<int64_t> shape;
        if (shape_arg.type == ValueType::ARRAY) {
            for (auto& e : shape_arg.as_array()->elements) shape.push_back(e.to_int());
        } else {
            shape.push_back(shape_arg.to_int());
        }
        if (shape.empty()) return Value::make_array();
        std::function<Value(size_t)> build = [&](size_t dim) -> Value {
            Value r = Value::make_array();
            int64_t n = shape[dim];
            r.as_array()->elements.reserve(n);
            if (dim + 1 == shape.size()) {
                r.as_array()->elements.resize(n, fill);
            } else {
                for (int64_t i = 0; i < n; i++) r.as_array()->elements.push_back(build(dim + 1));
            }
            return r;
        };
        return build(0);
    };
    register_native("ZEROS", [make_filled](const std::vector<Value>& args) -> Value {
        return make_filled(args[0], Value::make_i64(0));
    });
    // __MAKE_UDT_ARRAY__(shape, "TypeName") — fill an array of the given
    // shape with freshly-constructed UDT instances. Used by the parser to
    // desugar `DIM A[N] AS UserType`. We can't use ZEROS here because each
    // slot needs its own constructor result (with __TYPE__ + default fields)
    // rather than a numeric zero.
    register_native("__MAKE_UDT_ARRAY__", 2, 2, [this, make_filled](const std::vector<Value>& args) -> Value {
        std::string type_name = args[1].to_string();
        std::string ctor_name = type_name + ".__NEW__";
        // First build a zeros-array of the right shape, then walk it and
        // replace every leaf scalar with a fresh constructor result. This
        // works for any number of dimensions for free.
        Value arr = make_filled(args[0], Value::make_i64(0));
        std::function<void(Value&)> fill = [&](Value& v) {
            if (v.type == ValueType::ARRAY) {
                for (auto& e : v.as_array()->elements) fill(e);
            } else {
                v = call_function(ctor_name, {});
            }
        };
        fill(arr);
        return arr;
    });
    register_native("ONES", [make_filled](const std::vector<Value>& args) -> Value {
        return make_filled(args[0], Value::make_i64(1));
    });
    register_native("RANGE", [](const std::vector<Value>& args) -> Value {
        int64_t start = args[0].to_int();
        int64_t end = args[1].to_int();
        int64_t step = (args.size() >= 3) ? args[2].to_int() : (start <= end ? 1 : -1);
        Value r = Value::make_array();
        if (step > 0) for (int64_t i = start; i < end; i += step) r.as_array()->elements.push_back(Value::make_i64(i));
        else if (step < 0) for (int64_t i = start; i > end; i += step) r.as_array()->elements.push_back(Value::make_i64(i));
        return r;
    });
    register_native("COUNT", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array(); std::string target = args[1].to_string();
        int64_t c = 0; for (auto& e : arr->elements) if (e.to_string() == target) c++;
        return Value::make_i64(c);
    });
    register_native("INDEXOF", [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array(); std::string target = args[1].to_string();
        for (size_t i = 0; i < arr->elements.size(); i++)
            if (arr->elements[i].to_string() == target) return Value::make_i64(i);
        return Value::make_i64(-1);
    });

    // ── 5. Type Checking ─────────────────────────────────────

    register_native("ISNUM", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(is_numeric(args[0].type));
    });
    register_native("ISSTR", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::STRING);
    });
    register_native("ISARR", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::ARRAY);
    });
    register_native("ISMAP", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::OBJECT);
    });
    register_native("ISBOOL", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::BOOLEAN);
    });
    register_native("ISNONE", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::NONE);
    });
    register_native("ISNULL", [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::NONE);
    });
    register_native("TONUM", [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(args[0].to_double());
    });
    register_native("TOSTR", [](const std::vector<Value>& args) -> Value {
        return Value::make_string(args[0].to_string());
    });

    // ── 8. DateTime Extraction ───────────────────────────────

    register_native("YEAR", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_year + 1900);
    });
    register_native("MONTH", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_mon + 1);
    });
    register_native("DAY", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_mday);
    });
    register_native("HOUR", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_hour);
    });
    register_native("MINUTE", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_min);
    });
    register_native("SECOND", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_sec);
    });
    register_native("WEEKDAY", [](const std::vector<Value>& args) -> Value {
        std::time_t t = (std::time_t)args[0].to_double();
        return Value::make_i64(std::localtime(&t)->tm_wday);
    });

    // ── CODEC: Encoding & Hashing ───────────────────────────

    register_native("CODEC.BASE64_ENCODE$", [](const std::vector<Value>& args) -> Value {
        static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string& in = args[0].as_string()->data;
        std::string out;
        int val = 0, bits = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c; bits += 8;
            while (bits >= 0) { out += t[(val >> bits) & 0x3F]; bits -= 6; }
        }
        if (bits > -6) out += t[((val << 8) >> (bits + 8)) & 0x3F];
        while (out.size() % 4) out += '=';
        return Value::make_string(out);
    });

    register_native("CODEC.BASE64_DECODE$", [](const std::vector<Value>& args) -> Value {
        static const int d[] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51
        };
        const std::string& in = args[0].as_string()->data;
        std::string out;
        int val = 0, bits = -8;
        for (unsigned char c : in) {
            if (c == '=' || c >= 128) break;
            int v = d[c]; if (v < 0) continue;
            val = (val << 6) + v; bits += 6;
            if (bits >= 0) { out += (char)((val >> bits) & 0xFF); bits -= 8; }
        }
        return Value::make_string(out);
    });

    register_native("CODEC.SHA256$", [](const std::vector<Value>& args) -> Value {
        // SHA-256 implementation (FIPS 180-4)
        const std::string& msg = args[0].as_string()->data;
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        auto ror = [](uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); };

        // Pre-processing: padding
        std::vector<uint8_t> data(msg.begin(), msg.end());
        uint64_t bitlen = data.size() * 8;
        data.push_back(0x80);
        while (data.size() % 64 != 56) data.push_back(0);
        for (int i = 7; i >= 0; i--) data.push_back((uint8_t)(bitlen >> (i * 8)));

        uint32_t h0=0x6a09e667, h1=0xbb67ae85, h2=0x3c6ef372, h3=0xa54ff53a;
        uint32_t h4=0x510e527f, h5=0x9b05688c, h6=0x1f83d9ab, h7=0x5be0cd19;

        for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
            uint32_t w[64];
            for (int i = 0; i < 16; i++)
                w[i] = (data[chunk+i*4]<<24) | (data[chunk+i*4+1]<<16) | (data[chunk+i*4+2]<<8) | data[chunk+i*4+3];
            for (int i = 16; i < 64; i++) {
                uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15]>>3);
                uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19) ^ (w[i-2]>>10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }
            uint32_t a=h0,b=h1,c=h2,d=h3,e=h4,f=h5,g=h6,h=h7;
            for (int i = 0; i < 64; i++) {
                uint32_t S1 = ror(e,6)^ror(e,11)^ror(e,25);
                uint32_t ch = (e&f)^(~e&g);
                uint32_t t1 = h+S1+ch+K[i]+w[i];
                uint32_t S0 = ror(a,2)^ror(a,13)^ror(a,22);
                uint32_t maj = (a&b)^(a&c)^(b&c);
                uint32_t t2 = S0+maj;
                h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
            }
            h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=h;
        }
        char hex[65];
        snprintf(hex, sizeof(hex), "%08x%08x%08x%08x%08x%08x%08x%08x", h0,h1,h2,h3,h4,h5,h6,h7);
        return Value::make_string(hex);
    });

    register_native("CODEC.UUID$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        auto rb = []() -> uint8_t { return (uint8_t)(rand() & 0xFF); };
        uint8_t bytes[16];
        for (int i = 0; i < 16; i++) bytes[i] = rb();
        bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
        bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant 1
        char buf[37];
        snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            bytes[0],bytes[1],bytes[2],bytes[3], bytes[4],bytes[5],
            bytes[6],bytes[7], bytes[8],bytes[9],
            bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
        return Value::make_string(buf);
    });

    // ── OS Functions ──────────────────────────────────────────

    auto getos_fn = [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        return Value::make_string("WINDOWS");
#elif defined(__APPLE__)
        return Value::make_string("MACOS");
#else
        return Value::make_string("LINUX");
#endif
    };
    register_native("OS.GETOS", getos_fn);
    register_native("OS.GETOS$", getos_fn);

    register_native("OS.ARGS", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        // Populated by main() via set_global
        return Value::make_array(); // fallback; main sets the real value
    });

    register_native("OS.EXEC", [](const std::vector<Value>& args) -> Value {
        std::string cmd = args[0].as_string()->data;
        // Append args if provided
        if (args.size() >= 2 && args[1].type == ValueType::ARRAY) {
            for (auto& a : args[1].as_array()->elements)
                cmd += " " + a.to_string();
        }
        // Execute and capture output
        std::string output;
        int exit_code = -1;
#if defined(_WIN32)
        cmd = "cmd /c " + cmd + " 2>&1";
#else
        cmd = cmd + " 2>&1";
#endif
        FILE* pipe = _popen(cmd.c_str(), "r");
        if (pipe) {
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe)) output += buf;
            exit_code = _pclose(pipe);
        }
        Value result = Value::make_object();
        result.as_object()->set("OUTPUT", Value::make_string(output));
        result.as_object()->set("EXIT_CODE", Value::make_i64(exit_code));
        return result;
    });

    register_native("OS.HOSTNAME$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        char buf[256] = {};
#if defined(_WIN32)
        DWORD sz = sizeof(buf);
        GetComputerNameA(buf, &sz);
#else
        gethostname(buf, sizeof(buf));
#endif
        return Value::make_string(buf);
    });

    register_native("OS.IP$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::string ip = "127.0.0.1";
#if defined(_WIN32)
        ULONG buf_size = 15000;
        PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(buf_size);
        if (pAddresses) {
            DWORD ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &buf_size);
            if (ret == ERROR_BUFFER_OVERFLOW) {
                free(pAddresses);
                pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(buf_size);
                if (pAddresses) ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &buf_size);
            }
            if (ret == NO_ERROR) {
                for (auto pAddr = pAddresses; pAddr; pAddr = pAddr->Next) {
                    // Skip loopback and down interfaces
                    if (pAddr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                    if (pAddr->OperStatus != IfOperStatusUp) continue;
                    for (auto pUni = pAddr->FirstUnicastAddress; pUni; pUni = pUni->Next) {
                        auto sa = (struct sockaddr_in*)pUni->Address.lpSockaddr;
                        char buf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                        std::string candidate(buf);
                        if (candidate != "127.0.0.1" && candidate.substr(0,4) != "169.") {
                            ip = candidate;
                            goto found;
                        }
                    }
                }
                found:;
            }
            free(pAddresses);
        }
#endif
        return Value::make_string(ip);
    });

    register_native("OS.FEATURE", 1, 1, [](const std::vector<Value>& args) -> Value {
        // Reports whether the running binary advertises a given build feature.
        // Used by tests/programs that need to skip work the current backend
        // doesn't support — e.g. `IF NOT OS.FEATURE("NATIVEC") THEN ...`.
        if (args.empty() || args[0].type != ValueType::STRING)
            return Value::make_bool(false);
        std::string name = args[0].as_string()->data;
        for (auto& c : name) c = (char)std::toupper((unsigned char)c);

        // Interpreter never reports NATIVEC. INTERPRETER is its inverse.
        if (name == "NATIVEC")     return Value::make_bool(false);
        if (name == "INTERPRETER") return Value::make_bool(true);

        // Optional build flags — true iff this jdBasic.exe was built with them.
        bool on = false;
#ifdef COM
        if (name == "COM") on = true;
#endif
#ifdef HTTP
        if (name == "HTTP") on = true;
#endif
#ifdef USE_SERIAL
        if (name == "SERIAL") on = true;
#endif
#ifdef GFX
        if (name == "GFX") on = true;
#endif
#ifdef IMGUI
        if (name == "IMGUI") on = true;
#endif
#ifdef LLM
        if (name == "LLM") on = true;
#endif
#ifdef ONNX
        if (name == "ONNX") on = true;
#endif
#ifdef LLVM_CODEGEN
        if (name == "LLVMC") on = true;  // compiler available (--compile)
#endif
        return Value::make_bool(on);
    });

    register_native("OS.LOAD", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        double load = 0.0;
#if defined(_WIN32)
        // Use a quick WMI/perf counter approach via wmic
        FILE* pipe = _popen("wmic cpu get LoadPercentage /value 2>nul", "r");
        if (pipe) {
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe)) {
                std::string line(buf);
                if (line.find("LoadPercentage=") != std::string::npos) {
                    try { load = std::stod(line.substr(line.find('=') + 1)); } catch (...) {}
                }
            }
            _pclose(pipe);
        }
#endif
        return Value::make_f64(load);
    });

    // ── Reactive system ───────────────────────────────────────

    register_native("REACT_BIND", [this](const std::vector<Value>& args) -> Value {
        std::string var = args[0].as_string()->data;
        std::string formula = args[1].as_string()->data;
        std::string func = args[2].as_string()->data;
        std::vector<std::string> deps;
        if (args[3].type == ValueType::ARRAY)
            for (auto& d : args[3].as_array()->elements)
                deps.push_back(d.as_string()->data);
        bind_reactive(var, formula, func, deps);
        return Value::make_none();
    });

    register_native("UNREACT", [this](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        if (name == "ALL" || name == "*") {
            reactive_bindings.clear();
        } else {
            reactive_bindings.erase(name);
        }
        return Value::make_none();
    });

    // ── Async / Thread ────────────────────────────────────────

    register_native("AWAIT", [](const std::vector<Value>& args) -> Value {
        int task_id = (int)args[0].to_int();
        std::shared_ptr<AsyncTask> task;
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          auto it = g_async_tasks.find(task_id);
          if (it == g_async_tasks.end()) throw std::runtime_error("Invalid async task ID: " + std::to_string(task_id));
          task = it->second; }
        // Busy-wait with small sleep to stay responsive
        while (!task->done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Value result = task->result;
        // Cleanup
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          g_async_tasks.erase(task_id); }
        return result;
    });

    register_native("THREAD.ISDONE", [](const std::vector<Value>& args) -> Value {
        int task_id = (int)args[0].to_int();
        std::lock_guard<std::mutex> lock(g_async_mutex);
        auto it = g_async_tasks.find(task_id);
        if (it == g_async_tasks.end()) return Value::make_bool(true);
        return Value::make_bool(it->second->done.load());
    });

    register_native("THREAD.GETRESULT", [](const std::vector<Value>& args) -> Value {
        int task_id = (int)args[0].to_int();
        std::shared_ptr<AsyncTask> task;
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          auto it = g_async_tasks.find(task_id);
          if (it == g_async_tasks.end()) throw std::runtime_error("Invalid task ID");
          task = it->second; }
        while (!task->done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Value result = task->result;
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          g_async_tasks.erase(task_id); }
        return result;
    });

    // ── Filesystem ────────────────────────────────────────────

    register_native("DIR$", [](const std::vector<Value>& args) -> Value {
        std::string pattern = (args.size() >= 1) ? args[0].as_string()->data : "*";
        bool extended = (args.size() >= 2) ? args[1].to_bool() : false;
        Value result = Value::make_array();
#if defined(_WIN32)
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::string name(fd.cFileName);
                if (name == "." || name == "..") continue;
                if (!extended) {
                    result.as_array()->elements.push_back(Value::make_string(name));
                } else {
                    Value row = Value::make_array();
                    row.as_array()->elements.push_back(Value::make_string(name));
                    int64_t size = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    row.as_array()->elements.push_back(Value::make_i64(size));
                    std::string type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "DIR" :
                                       (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? "LINK" : "FILE";
                    row.as_array()->elements.push_back(Value::make_string(type));
                    SYSTEMTIME st; FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
                    char dt[32]; snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
                        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    row.as_array()->elements.push_back(Value::make_string(dt));
                    std::string attr;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) attr += "R";
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) attr += "W";
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) attr += "H";
                    row.as_array()->elements.push_back(Value::make_string(attr));
                    result.as_array()->elements.push_back(std::move(row));
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
#endif
        return result;
    });

    register_native("DIR", [this](const std::vector<Value>& args) -> Value {
        std::string pattern = (args.size() >= 1) ? args[0].as_string()->data : "*";
        Value files = call_function("DIR$", {Value::make_string(pattern), Value::make_bool(true)});
        auto* arr = files.as_array();
        for (auto& row : arr->elements) {
            if (row.type != ValueType::ARRAY) continue;
            auto* r = row.as_array();
            std::string type = (r->elements.size() > 2) ? r->elements[2].to_string() : "";
            std::string name = r->elements[0].to_string();
            std::string size_s = (r->elements.size() > 1) ? r->elements[1].to_string() : "";
            std::string date_s = (r->elements.size() > 3) ? r->elements[3].to_string() : "";
            if (type == "DIR") {
                emit(date_s + "    <DIR>          " + name + "\n");
            } else {
                char buf[16]; snprintf(buf, sizeof(buf), "%12s", size_s.c_str());
                emit(date_s + " " + std::string(buf) + " " + name + "\n");
            }
        }
        emit("  " + std::to_string(arr->elements.size()) + " item(s)\n");
        return Value::make_none();
    });

    register_native("CD", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            // CD without args: just return current dir (no print in script mode)
            char buf[MAX_PATH];
#if defined(_WIN32)
            GetCurrentDirectoryA(MAX_PATH, buf);
#else
            getcwd(buf, sizeof(buf));
#endif
            return Value::make_string(buf);
        }
        std::string path = args[0].to_string();
        if (path.empty()) {
            throw jdError(ErrCode::RUNTIME_ERROR, "CD: empty path");
        }
#if defined(_WIN32)
        if (!SetCurrentDirectoryA(path.c_str()))
            throw jdError(ErrCode::FILE_NOT_FOUND, "Cannot change to directory: " + path);
        char buf[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buf);
        return Value::make_string(buf);
#else
        if (chdir(path.c_str()) != 0)
            throw jdError(ErrCode::FILE_NOT_FOUND, "Cannot change to directory: " + path);
        char buf[4096];
        getcwd(buf, sizeof(buf));
        return Value::make_string(buf);
#endif
    });

    register_native("PWD", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        char buf[MAX_PATH];
#if defined(_WIN32)
        GetCurrentDirectoryA(MAX_PATH, buf);
#else
        getcwd(buf, sizeof(buf));
#endif
        return Value::make_string(buf);
    });

    register_native("MKDIR", [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
#if defined(_WIN32)
        if (!CreateDirectoryA(path.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create directory: " + path);
#endif
        return Value::make_none();
    });

    register_native("KILL", [](const std::vector<Value>& args) -> Value {
        if (args[0].type == ValueType::ARRAY) {
            for (auto& f : args[0].as_array()->elements) {
                if (std::remove(f.to_string().c_str()) != 0)
                    throw std::runtime_error("Cannot delete: " + f.to_string());
            }
        } else {
            if (std::remove(args[0].as_string()->data.c_str()) != 0)
                throw std::runtime_error("Cannot delete: " + args[0].as_string()->data);
        }
        return Value::make_none();
    });

    register_native("TRON", 0, 0, [this](const std::vector<Value>& args) -> Value {
        (void)args;
        trace_enabled = true;
        emit("Trace ON\n");
        return Value::make_none();
    });

    register_native("TROFF", 0, 0, [this](const std::vector<Value>& args) -> Value {
        (void)args;
        trace_enabled = false;
        emit("Trace OFF\n");
        return Value::make_none();
    });

    register_native("DUMP", 0, 1, [this](const std::vector<Value>& args) -> Value {
        std::string what = args.empty() ? std::string("GLOBAL") : args[0].to_string();
        std::transform(what.begin(), what.end(), what.begin(), ::toupper);

        if (what == "REACT") {
            if (reactive_bindings.empty()) {
                emit("No reactive bindings.\n");
            } else {
                emit("--- Reactive Graph (" + std::to_string(reactive_bindings.size()) + " bindings) ---\n");
                for (auto& [name, b] : reactive_bindings) {
                    std::string line = "  " + name + " -> " + b.formula + "  [deps: ";
                    for (size_t i = 0; i < b.dependencies.size(); i++) {
                        if (i > 0) line += ", ";
                        line += b.dependencies[i];
                    }
                    line += "]\n";
                    emit(line);
                }
            }
        } else if (what == "STACK") {
            emit("--- Call Stack (" + std::to_string(frames.size()) + " frames) ---\n");
            for (size_t i = 0; i < frames.size(); i++) {
                int line = 0;
                if (frames[i].chunk && frames[i].ip > 0 &&
                    frames[i].ip - 1 < frames[i].chunk->line_info.size())
                    line = frames[i].chunk->line_info[frames[i].ip - 1];
                emit("  #" + std::to_string(i) + " line " + std::to_string(line) + "\n");
            }
        } else if (what == "FUNCS" || what == "FUNCTIONS") {
            emit("--- Functions (" + std::to_string(owned_funcs.size()) + ") ---\n");
            for (auto& f : owned_funcs)
                emit("  " + f.name + " (" + std::to_string(f.arity) + " params)\n");
        } else { // GLOBAL or VARS (default)
            emit("--- Global Variables (" + std::to_string(global_names.size()) + ") ---\n");
            for (auto& [name, slot] : global_names) {
                if (slot < globals.size() && name.substr(0, 2) != "__")
                    emit("  " + name + " = " + globals[slot].to_string() + "\n");
            }
        }
        return Value::make_none();
    });

    // ── Path Functions ───────────────────────────────────────

    register_native("PATH.JOIN$", [](const std::vector<Value>& args) -> Value {
#if defined(_WIN32)
        char sep = '\\';
#else
        char sep = '/';
#endif
        std::string result;
        for (size_t i = 0; i < args.size(); i++) {
            std::string part = args[i].to_string();
            if (i > 0 && !result.empty() && result.back() != sep && result.back() != '/' && result.back() != '\\')
                result += sep;
            result += part;
        }
        return Value::make_string(result);
    });

    register_native("PATH.BASENAME$", [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
        size_t pos = p.find_last_of("/\\");
        return Value::make_string(pos == std::string::npos ? p : p.substr(pos + 1));
    });

    register_native("PATH.EXT$", [](const std::vector<Value>& args) -> Value {
        std::string p = args[0].as_string()->data;
        size_t slash = p.find_last_of("/\\");
        size_t dot = p.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return Value::make_string("");
        return Value::make_string(p.substr(dot));
    });

    // ── Dynamic code ─────────────────────────────────────────

    register_native("EXECUTE", [this](const std::vector<Value>& args) -> Value {
        std::string code = args[0].as_string()->data;
        if (on_execute) on_execute(*this, code);
        return Value::make_none();
    });

    register_native("EVAL", [this](const std::vector<Value>& args) -> Value {
        std::string expr_str = args[0].as_string()->data;
        if (on_eval) return on_eval(*this, expr_str);
        return Value::make_none();
    });

    // ── Pipe apply (internal: calls funcref with value) ─────
    register_native("__PIPE_APPLY", 2, 2, [this](const std::vector<Value>& args) -> Value {
        return call_funcref(args[0], {args[1]});
    });

    // ── Event system ─────────────────────────────────────────

    register_native("__EVENT_ON", [this](const std::vector<Value>& args) -> Value {
        std::string event_name = args[0].as_string()->data;
        std::string handler = args[1].as_string()->data;
        event_on(event_name, handler);
        return Value::make_none();
    });

    register_native("__EVENT_RAISE", [this](const std::vector<Value>& args) -> Value {
        std::string event_name = args[0].as_string()->data;
        std::vector<Value> data;
        for (size_t i = 1; i < args.size(); i++) data.push_back(args[i]);
        event_raise(event_name, data);
        return Value::make_none();
    });
}

// ── Event system implementation ─────────────────────────────────

void VM::event_on(const std::string& event_name, const std::string& handler) {
    event_handlers[event_name] = handler;
}

void VM::event_raise(const std::string& event_name, const std::vector<Value>& data) {
    auto it = event_handlers.find(event_name);
    if (it == event_handlers.end()) return;

    // Build data array argument
    Value data_arr = Value::make_array();
    for (auto& d : data) data_arr.as_array()->elements.push_back(d);

    try {
        call_function(it->second, {data_arr});
    } catch (const std::exception& e) {
        print_error(ErrCode::RUNTIME_ERROR,
            "Event handler '" + it->second + "' for '" + event_name + "': " + e.what());
    }
}

void VM::event_poll() {
    if (event_handlers.empty()) return;

#ifdef GFX
    extern bool gfx_is_active();
    extern bool gfx_has_pending_events();
    extern std::vector<SDL_Event> gfx_drain_pending_events();
    if (gfx_is_active()) {
        // Drain the shared event queue that SCREENFLIP and gfx_pump_events
        // already populated. We must NOT call SDL_PollEvent here — that
        // would race with SCREENFLIP's loop and whichever runs first eats
        // the events, starving the other. Instead, all SDL_PollEvent sites
        // push into the shared queue and we consume from it.
        if (!gfx_has_pending_events()) return;
        auto events = gfx_drain_pending_events();
        for (auto& ev : events) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: {
                    event_raise("QUIT", {});
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    auto it = event_handlers.find("KEYDOWN");
                    if (it != event_handlers.end()) {
                        Value info = Value::make_object();
                        info.as_object()->set("scancode", Value::make_i64(ev.key.scancode));
                        // keycode: SDL_Keycode — ASCII-compatible for printable keys
                        // (ESC=27, Enter=13, A=97, ...). Old jdBasic had this field.
                        info.as_object()->set("keycode", Value::make_i64((int64_t)ev.key.key));
                        const char* name = SDL_GetKeyName(ev.key.key);
                        info.as_object()->set("key", Value::make_string(name ? name : ""));
                        info.as_object()->set("repeat", Value::make_bool(ev.key.repeat));
                        event_raise("KEYDOWN", {info});
                    }
                    break;
                }
                case SDL_EVENT_KEY_UP: {
                    auto it = event_handlers.find("KEYUP");
                    if (it != event_handlers.end()) {
                        Value info = Value::make_object();
                        info.as_object()->set("scancode", Value::make_i64(ev.key.scancode));
                        info.as_object()->set("keycode", Value::make_i64((int64_t)ev.key.key));
                        const char* name = SDL_GetKeyName(ev.key.key);
                        info.as_object()->set("key", Value::make_string(name ? name : ""));
                        event_raise("KEYUP", {info});
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    auto it = event_handlers.find("MOUSEDOWN");
                    if (it != event_handlers.end()) {
                        Value info = Value::make_object();
                        info.as_object()->set("button", Value::make_i64(ev.button.button));
                        info.as_object()->set("x", Value::make_i64((int64_t)ev.button.x));
                        info.as_object()->set("y", Value::make_i64((int64_t)ev.button.y));
                        event_raise("MOUSEDOWN", {info});
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    auto it = event_handlers.find("MOUSEUP");
                    if (it != event_handlers.end()) {
                        Value info = Value::make_object();
                        info.as_object()->set("button", Value::make_i64(ev.button.button));
                        info.as_object()->set("x", Value::make_i64((int64_t)ev.button.x));
                        info.as_object()->set("y", Value::make_i64((int64_t)ev.button.y));
                        event_raise("MOUSEUP", {info});
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION: {
                    auto it = event_handlers.find("MOUSEMOVE");
                    if (it != event_handlers.end()) {
                        Value info = Value::make_object();
                        info.as_object()->set("x", Value::make_i64((int64_t)ev.motion.x));
                        info.as_object()->set("y", Value::make_i64((int64_t)ev.motion.y));
                        event_raise("MOUSEMOVE", {info});
                    }
                    break;
                }
                default: break;
            }
        }
        return;
    }
#endif

    // Console mode: poll keyboard with 100ms throttle
#if defined(_WIN32)
    static auto last_check = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check).count() >= 100) {
        last_check = now;
        if (_kbhit()) {
            int ch = _getch();
            auto kit = event_handlers.find("KEYDOWN");
            if (kit != event_handlers.end()) {
                Value info = Value::make_object();
                info.as_object()->set("scancode", Value::make_i64(ch));
                info.as_object()->set("key", Value::make_string(std::string(1, (char)ch)));
                event_raise("KEYDOWN", {info});
            }
        }
    }
#endif
}
