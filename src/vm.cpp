#include "vm_internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>
// Read one line from the page terminal. Asyncify suspends the call here until
// the user submits a line, so INPUT works in the browser without std::cin.
// Allocates with _malloc / writes via HEAPU8 (always available in EM_JS); the
// caller frees the returned buffer.
EM_ASYNC_JS(char*, jdb_read_line_js, (void), {
    let s = "";
    try { if (typeof Module !== 'undefined' && Module.jdbReadLine) s = await Module.jdbReadLine(); } catch (e) {}
    const bytes = new TextEncoder().encode(String(s));
    const ptr = _malloc(bytes.length + 1);
    HEAPU8.set(bytes, ptr);
    HEAPU8[ptr + bytes.length] = 0;
    return ptr;
});
// Non-blocking single-key poll for text-mode KEYDOWN / INKEY$ in the browser.
// The page feeds keystrokes from the terminal into a queue; returns the next
// key code, or -1 when the queue is empty. Text games have no SDL window, so
// SDL events never arrive - this is their key source.
EM_JS(int, jdb_poll_key_js, (void), {
    return (typeof Module !== 'undefined' && Module.jdbPollKey) ? Module.jdbPollKey() : -1;
});
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


// ── Async task infrastructure ────────────────────────────────
#include <mutex>
#include <atomic>
#include <map>
#include "async_task.h"

// Names in a chunk keep the case they were written in.
static bool name_ci_equal(const char* a, const std::string& b) {
    size_t n = b.size();
    for (size_t i = 0; i < n; i++) {
        if (!a[i] || std::toupper((unsigned char)a[i]) !=
                     std::toupper((unsigned char)b[i])) return false;
    }
    return a[n] == '\0';
}

// Defined beside register_native; every path that reaches a builtin
// runs it before the call.
static void check_native_arity(const std::string& name, const VM::NativeEntry& e, size_t argc);

// "2 args" when every parameter is required, "2 to 4 args" when the
// trailing ones carry defaults.
static std::string describe_arity(const FuncProto& p) {
    if (p.min_arity == p.arity) return std::to_string(p.arity) + " args";
    return std::to_string(p.min_arity) + " to " + std::to_string(p.arity) + " args";
}

std::map<int, std::shared_ptr<AsyncTask>> g_async_tasks;
std::mutex g_async_mutex;
std::atomic<int> g_async_next_id{1};

// Currently-executing VM, made available to free-standing callbacks
// (in particular the heap_release dispose hook in value.h, which has
// no VM context of its own). A stack-style save/restore in the VM
// ctor/dtor lets nested VMs (REPL, EXECUTE) work correctly.
thread_local VM* g_active_vm = nullptr;

static void vm_heap_dispose_hook(HeapObject* o) {
    if (!o || !g_active_vm) return;
    auto* obj = dynamic_cast<ObjectObj*>(o);
    if (!obj) return;
    Value* tv = obj->get("__TYPE__");
    if (!tv || tv->type != ValueType::STRING) return;
    std::string type_name = tv->as_string()->data;
    std::string dispose_name = type_name + ".DISPOSE";
    if (!g_active_vm->function_exists(dispose_name)) return;
    // Wrap the raw pointer as an OBJECT-typed Value. The hook parked the
    // refcount at 1 for us, so the wrapped Value's destructor will balance
    // out the fetch_sub afterwards in heap_release.
    Value wrapped;
    wrapped.type = ValueType::OBJECT;
    wrapped.obj = obj;
    heap_retain(obj); // for wrapped's eventual destructor
    try {
        g_active_vm->call_function(dispose_name, { wrapped });
    } catch (...) {
        // Swallow - DISPOSE failures must not corrupt the cleanup path.
    }
}

#ifdef PICOCALC
extern "C" int  picocalc_gfx_buffered(void);
extern "C" void picocalc_gfx_clear(int index);
#endif

VM::VM() {
#ifdef JDB_MCU
    // A board with half a megabyte of RAM starts small; the stack still
    // doubles on demand like everywhere else. Starting small is what
    // makes that doubling possible at all: from 4096 slots the next step
    // wanted more memory than the board has, so the stack could only
    // ever be its opening size.
    stack.resize(256);
    frames.reserve(64);
#else
    stack.resize(65536); // pre-allocate stack - avoids resize checks on hot paths
    frames.reserve(1024); // pre-allocate frame vector
#endif
    prev_active_vm_ = g_active_vm;
    g_active_vm = this;
    g_heap_dispose_hook = &vm_heap_dispose_hook;
    register_builtins();
}

VM::~VM() {
    g_active_vm = static_cast<VM*>(prev_active_vm_);
    if (!g_active_vm) g_heap_dispose_hook = nullptr;
}  // DebugInfo is complete here via dap.h include below

void VM::push(Value v) {
    if (sp >= stack.size()) stack.resize(stack.size() * 2);
    stack[sp++] = std::move(v);
}

Value VM::pop() {
    if (sp == 0) throw jdError(ErrCode::STACK_UNDERFLOW, "Stack underflow",
        (!frames.empty() && frame().ip > 0)
            ? frame().chunk->line_at(frame().ip - 1) : 0);
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

void VM::reject_builtin_collision(const FuncProto& f) const {
    if (f.is_exported) return; // module exports are namespaced on IMPORT
    if (!native_find(f.name) && !builtin_sig(f.name)) return;
    throw jdError(ErrCode::SYNTAX_ERROR,
        std::string(f.is_sub ? "SUB " : "FUNC ") + f.name +
        " collides with the builtin function " + f.name + " - choose another name");
}

void VM::load(Chunk& main_chunk, std::vector<FuncProto>& funcs) {
    is_halted = false; // fresh program
    // Move compiled functions into owned storage so func_protos stays valid even
    // across nested run_code() calls (EXECUTE/EVAL/REPL).
    for (auto& f : funcs) {
        reject_builtin_collision(f);
        // Compiled and about to become permanent: give back the room the
        // vectors kept while they were growing.
        f.chunk.shrink();
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

    // Allocate global slots for user variables - preserve any existing
    // native registrations (PI, E, …) at their assigned slots. The
    // previous code naively did `global_names[name] = i`, which made
    // `global_names["A"]` point at slot 0 (held by PI). User code that
    // referenced an undefined `A` then read PI's value (3.14159) and
    // `obj.x` for any name `obj` whose slot collided returned junk.
    for (uint16_t i = 0; i < main_chunk.name_count(); i++) {
        const std::string& name = main_chunk.name_at(i);
        if (global_names.count(name)) continue;
        global_names[name] = static_cast<uint16_t>(globals.size());
        globals.push_back(Value::make_none());
    }

    frames.push_back({&main_chunk, 0, 0});
}

std::pair<size_t, size_t> VM::merge_funcs(std::vector<FuncProto>& new_funcs) {
    size_t added = 0, updated = 0;
    for (auto& f : new_funcs) {
        reject_builtin_collision(f);
        // Compiled and about to become permanent: give back the room the
        // vectors kept while they were growing.
        f.chunk.shrink();
        auto it = func_map.find(f.name);
        if (it != func_map.end()) {
            owned_funcs[it->second] = std::move(f);
            updated++;
        } else {
            func_map[f.name] = owned_funcs.size();
            owned_funcs.push_back(std::move(f));
            added++;
        }
    }
    func_protos = &owned_funcs;
    if (added || updated) func_map_generation++;
    return {added, updated};
}

#if defined(JDB_MCU) && defined(JDB_LOAD_TRACE)
extern "C" void jdb_load_trace_n(const char*, unsigned, unsigned, unsigned);
#endif

void VM::run_code(Chunk& chunk, std::vector<FuncProto>& new_funcs) {
    // The chunk is finished; the room its vectors kept while growing is
    // given back before it runs.
    chunk.shrink();
    // Merge new functions into owned storage
    bool funcs_changed = false;
    for (auto& f : new_funcs) {
        reject_builtin_collision(f);
        // Compiled and about to become permanent: give back the room the
        // vectors kept while they were growing.
        f.chunk.shrink();
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
    // try_handlers is VM-global, but its catch_addr / saved_frame_count
    // refer to the OUTER chunk and frame depth. If we leave the outer
    // entries visible to the inner run(), an exception thrown from the
    // EXECUTE'd code will trip the outer handler and ip-jump into the
    // inner chunk at the outer's catch_addr - random bytes, "Unknown
    // opcode". Save and clear so the inner run sees a clean stack; the
    // exception will propagate up via run_code's catch block below and
    // the outer try_handlers are restored before re-throwing.
    auto saved_try_handlers = std::move(try_handlers);
    try_handlers.clear();
    frames.clear();
    // IMPORTANT: do NOT reset sp to 0 - the existing stack may hold locals of an
    // outer user function (e.g. EVAL called from inside Calc). Start the new
    // chunk's frame ABOVE the current sp so we don't clobber outer locals.
    size_t base = saved_sp;
    size_t needed = base + chunk.name_count() + 64;
#if defined(JDB_MCU) && defined(JDB_LOAD_TRACE)
    // What the value stack is being asked for, and what a slot costs.
    jdb_load_trace_n("stack", (unsigned)needed, (unsigned)stack.size(),
                     (unsigned)sizeof(Value));
#endif
    while (needed >= stack.size()) stack.resize(stack.size() * 2);
    sp = base + chunk.name_count();
    min_frame_depth = 0;
    is_stopped = false;  // fresh sub-run; prior STOP state preserved in stopped_*
    // A previous run may have ended via END_PROGRAM, leaving is_halted=true.
    // Without this reset, run() bails at its top-of-loop guard and the
    // sub-chunk silently never executes - wedges every MCP eval after END.
    is_halted = false;
    // Clear any pre-fired external stop signal: a sub-run inherits a clean
    // slate, so e.g. an MCP eval doesn't fire-and-forget-stop a tiny snippet
    // because someone sent jdb_stop while the VM was idle.
    stop_requested.store(false);
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
        try_handlers = std::move(saved_try_handlers);
        throw;
    }
    subrun_depth--;
    // Sub-run completed cleanly - restore the outer try_handlers.
    try_handlers = std::move(saved_try_handlers);

    if (is_stopped) {
        // Save stopped state for RESUME - keep chunk alive by copying
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
        // is_stopped stays true - caller (console) will check it
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
    // VMState.functions is a std::vector for portability; owned_funcs is a
    // std::deque (see vm.h for why). Copy-convert here.
    s.functions.assign(owned_funcs.begin(), owned_funcs.end());
    s.func_map = func_map;
    return s;
}

void VM::restore_state(const VMState& state) {
    globals = state.globals;
    global_names = state.global_names;
    // The restored table hands out its own slot numbers, so the by-slot
    // constant set is rebuilt from the names, which do not change.
    const_global_slots.clear();
    for (const auto& n : const_globals) {
        auto it = global_names.find(n);
        if (it != global_names.end()) const_global_slots.insert(it->second);
    }
    owned_funcs.assign(state.functions.begin(), state.functions.end());
    func_map = state.func_map;
    func_protos = &owned_funcs;
    frames.clear();
    sp = 0;
    func_map_generation++;
}

void VM::reset() {
    globals.clear();
    global_names.clear();
    // Slot numbers start over, so the by-slot constant set has to go with
    // them. The names survive and re-register their slots as they come back.
    const_global_slots.clear();
    owned_funcs.clear();
    func_map.clear();
    func_protos = &owned_funcs;
    frames.clear();
    sp = 0;
    is_stopped = false;
    event_handlers.clear();
    known_types.clear();
    func_map_generation++;
}

void VM::inject_stopped_locals() {
    injected_locals.clear();
    if (stopped_frames.empty()) return;
    // The deepest frame is where execution paused - usually the function with STOP
    const CallFrame& f = stopped_frames.back();
    if (!f.chunk) return;
    for (size_t i = 0; i < f.chunk->name_count(); i++) {
        const std::string& name = f.chunk->name_at(i);
        if (name.empty()) continue;
        size_t stack_idx = f.stack_base + i;
        if (stack_idx >= stopped_stack.size()) continue;
        const Value& slot_val = stopped_stack[stack_idx];
        auto git = global_names.find(name);
        // Sub-run main chunks store top-level vars via OP_STORE_GLOBAL; the
        // frame slot is allocated but unused (stays NONE). Don't clobber the
        // real global with a stale NONE - let the user inspect/mutate the
        // global directly. The script's OP_LOAD_GLOBAL on resume sees the
        // user's modification, since extract_stopped_locals has no entry to
        // revert.
        if (slot_val.type == ValueType::NONE && git != global_names.end()) {
            continue;
        }
        InjectedLocal il;
        il.name = name;
        il.slot_in_stopped_stack = stack_idx;
        if (git != global_names.end()) {
            il.was_existing = true;
            il.overridden_value = globals[git->second];
            il.global_slot = git->second;
            globals[git->second] = slot_val;
        } else {
            il.was_existing = false;
            il.global_slot = static_cast<uint16_t>(globals.size());
            globals.push_back(slot_val);
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

    // If run() returned because the script hit another STOP_OP, re-stash
    // the new stopped state so the NEXT resume() can find it. Mirrors
    // run_code() lines 298-310. Without this, a second jdb_resume after
    // a stop/eval/resume cycle moves empty stopped_frames into frames
    // and crashes on the first opcode fetch - silently swallowed by
    // mcp_stdio's catch(...) and surfacing as a hung SDL window with
    // the VM mysteriously back at idle (RUNNING=1 global, but worker
    // terminated mid-loop). stopped_chunk stays valid: it's the same
    // chunk copy from the first STOP, and frame pointers either still
    // reference it or point into owned_funcs (which persist).
    if (is_stopped) {
        stopped_frames = std::move(frames);
        stopped_stack.assign(stack.begin(), stack.begin() + sp);
        stopped_sp = sp;
        inject_stopped_locals();
    }

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
                size_t needed = proto.chunk.name_count();
                while (needed >= stack.size()) stack.resize(stack.size() * 2);
                if (sp < needed) sp = needed;
                run();
                Value result = pop();

                frames = std::move(saved_frames);
                sp = saved_sp_val;
                min_frame_depth = saved_min;

                // Set the reactive variable - check if dotted (UDT member)
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
    auto it = global_names.find(upper);
    if (it != global_names.end()) const_global_slots.insert(it->second);
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

// True when `name` takes an array argument whole instead of being applied
// per element. Read by OpCode::CALL and VM::call_function.
bool jdb_no_vectorize(const std::string& name) {
    return builtin_no_vectorize(name);
}

// Invoke an already-resolved native: a direct call, or element-wise
// auto-vectorisation when an arg is an array and no_vec is false. Shared by
// OpCode::CALL_NATIVE, the slow CALL path, and call_function.
Value VM::invoke_native(const NativeFunc& fn, const std::vector<Value>& args, bool no_vec) {
    bool has_arr = false;
    if (!no_vec) {
        for (auto& a : args) if (a.type == ValueType::ARRAY) { has_arr = true; break; }
    }
    if (!has_arr) return fn(args);
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
        if (!any) return fn(cur);
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

Value VM::call_function_idx(int32_t idx, const std::vector<Value>& args) {
    FuncProto& proto = (*func_protos)[idx];

    size_t new_base = sp;
    for (auto& a : args) push(a);
    size_t needed = new_base + proto.chunk.name_count();
    while (needed >= stack.size()) stack.resize(stack.size() * 2);
    for (size_t i = sp; i < needed; i++) stack[i] = Value::make_none();
    if (sp < needed) sp = needed;

    if (frames.size() >= JDB_MAX_FRAMES)
        throw jdError(ErrCode::STACK_OVERFLOW, "Call stack overflow (max " + std::to_string(JDB_MAX_FRAMES) + " frames)");
    size_t saved_min = min_frame_depth;
    size_t saved_frames = frames.size();
    // The caller's TRY entries belong to the caller's run loop: hidden here,
    // an error inside the callee leaves this run instead of jumping the
    // nested loop into the caller's CATCH block.
    auto saved_try_handlers = std::move(try_handlers);
    try_handlers.clear();
    min_frame_depth = frames.size();
    frames.push_back({&proto.chunk, 0, new_base});
    try {
        run();
    } catch (...) {
        if (frames.size() > saved_frames) frames.resize(saved_frames);
        try_handlers = std::move(saved_try_handlers);
        sp = new_base;
        min_frame_depth = saved_min;
        throw;
    }
    try_handlers = std::move(saved_try_handlers);
    min_frame_depth = saved_min;
    // If the called function (or anything it triggered) ran END, the
    // VM has no return value to pop - bail out cleanly. The is_halted
    // flag stays set so further nested unwinds also short-circuit.
    if (is_halted) return Value::make_none();
    return pop();
}

Value VM::call_function(const std::string& name, const std::vector<Value>& args) {
    // Native?
    if (const NativeEntry* ne = native_find(name)) {
        check_native_arity(name, *ne, args.size());
        bool no_vec = jdb_no_vectorize(name)
            || extra_no_vectorize.find(name) != extra_no_vectorize.end();
        return invoke_native(ne->fn, args, no_vec);
    }

    // User-defined?
    auto fit = func_map.find(name);
    if (fit == func_map.end()) {
        if (compiled_call_hook) {
            Value out;
            if (compiled_call_hook(name, args, out)) return out;
        }
        throw jdError(ErrCode::UNDEFINED_FUNCTION, "Undefined function: " + name);
    }

    return call_function_idx((int32_t)fit->second, args);
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
    const auto& name = frame().chunk->name_at(name_idx);
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

        // Cache the current frame once per iteration - frames.back() requires
        // multiple loads per access and is a measurable overhead in tight
        // recursive loops. CALL/RETURN fall out of the switch via break, so
        // the next iteration refetches automatically.
        CallFrame& cf = frames.back();

#ifdef JDB_MCU
        // Ctrl-C on the console ends the program the way END does, with
        // the line it was on. Every sixteenth opcode asks; the poll itself
        // looks at the console no more than ten times a second.
        static uint32_t break_ctr = 0;
        if ((++break_ctr & 15) == 0 && jdb_break_poll()) {
            emit("Break at line " + std::to_string(cf.chunk->line_at(cf.ip)) + "\n");
            is_halted = true;
            return;
        }
#endif

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
            // External STOP request (set from MCP reader thread, joystick
            // handler in the host, etc). Acts exactly like the in-script
            // STOP statement: stash state, return; run_code's epilogue
            // moves frames into stopped_* and inject_stopped_locals exposes
            // the pause-point's frame. cf.ip currently points to the NEXT
            // opcode (the one we are about to fetch), so the line at cf.ip
            // is the line that will execute on resume.
            if (stop_requested.exchange(false)) {
                int stop_line = cf.chunk->line_at(cf.ip);
                emit("STOP (external) at line " + std::to_string(stop_line)
                     + ". Type RESUME or call jdb_resume to continue.\n");
                is_stopped = true;
                return;
            }
        }

        size_t trace_ip = cf.ip;
        const Chunk* trace_chunk = cf.chunk;
        OpCode op = static_cast<OpCode>(cf.chunk->code[cf.ip++]);

        if (trace_enabled && !frames.empty()) {
            int tline = frame().chunk->line_at(trace_ip);
            if (tline > 0) std::cerr << "[TRACE] line " << tline << " op " << (int)op << std::endl;
        }

        // Debug adapter: check for breakpoints/stepping on line change.
        // Either the socket DAP (standalone / VS Code) or the embed host hook
        // (Godot) drives it.
        if (debug && (debug->dap || debug->host_hook) && !frames.empty()) {
            if (true) {
                int dline = frame().chunk->line_at(trace_ip);
                if (dline > 0) {
                    debug_check(dline);
                    // If IP was moved (goto) or the chunk was hot-swapped
                    // (recompile) during the pause, discard the already-fetched
                    // opcode and restart the fetch cycle against the new code.
                    if (frame().ip != trace_ip + 1 || frame().chunk != trace_chunk) continue;
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

        case OpCode::LOAD_STATIC: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            if (sp >= stack.size()) stack.resize(stack.size() * 2);
            stack[sp++] = cf.chunk->static_values[slot];
            break;
        }

        case OpCode::STORE_STATIC: {
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            cf.chunk->static_values[slot] = std::move(stack[--sp]);
            break;
        }

        case OpCode::FOREACH_NEXT: {
            // i16 exit_offset (relative to ip after operand).
            int16_t off = (int16_t)(cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8));
            cf.ip += 2;
            // Stack top is state, then iter (we pop in that order).
            Value state = std::move(stack[--sp]);
            Value iter  = std::move(stack[--sp]);

            auto take_exit = [&]() {
                cf.ip = (size_t)((ptrdiff_t)cf.ip + off);
            };

            if (iter.type == ValueType::BOOLEAN || iter.type == ValueType::BYTE ||
                iter.type == ValueType::INT16 || iter.type == ValueType::INT32 ||
                iter.type == ValueType::FLOAT16 || iter.type == ValueType::FLOAT32 ||
                iter.type == ValueType::FLOAT64 ||
                (iter.type == ValueType::INT64 && !chan_lookup(iter.to_int())))
                throw jdError(ErrCode::RUNTIME_ERROR, "FOR EACH needs an array, a string, a map or a channel");

            // The state starts as 0 (one loop variable) or 1 (two). The first
            // pass replaces it with [position, two variables, snapshot]: the
            // length of an array at entry, the character count of a string,
            // the keys and values of a map.
            if (state.type != ValueType::ARRAY) {
                Value snap = Value::make_array();
                auto& init = snap.as_array()->elements;
                init.push_back(Value::make_i64(0));
                init.push_back(Value::make_i64(state.to_int() == 1 ? 1 : 0));
                if (iter.type == ValueType::ARRAY) {
                    init.push_back(Value::make_i64((int64_t)iter.as_array()->elements.size()));
                } else if (iter.type == ValueType::STRING) {
                    init.push_back(Value::make_i64(0));
                } else if (iter.type == ValueType::OBJECT) {
                    Value keys = Value::make_array();
                    Value vals = Value::make_array();
                    for (auto& [k, v] : iter.as_object()->fields) {
                        keys.as_array()->elements.push_back(Value::make_string(k));
                        vals.as_array()->elements.push_back(v);
                    }
                    init.push_back(std::move(keys));
                    init.push_back(std::move(vals));
                }
                state = std::move(snap);
            }
            auto& se = state.as_array()->elements;
            int64_t pos = se[0].to_int();
            bool two = se[1].to_int() != 0;
            auto push_pass = [&](Value first, Value second) {
                if (sp + 3 > stack.size()) stack.resize(stack.size() * 2);
                stack[sp++] = state;
                if (two) stack[sp++] = std::move(first);
                stack[sp++] = std::move(second);
            };

            if (iter.type == ValueType::ARRAY) {
                auto* arr = iter.as_array();
                if (pos >= se[2].to_int() || pos >= (int64_t)arr->elements.size()) {
                    take_exit();
                    break;
                }
                se[0] = Value::make_i64(pos + 1);
                push_pass(Value::make_i64(pos), arr->elements[(size_t)pos]);
            } else if (iter.type == ValueType::STRING) {
                // UTF-8 characters; the position is the byte offset of the
                // next one, se[2] its character index.
                const auto& s = iter.as_string()->data;
                if (pos < 0 || pos >= (int64_t)s.size()) {
                    take_exit();
                    break;
                }
                unsigned char c = (unsigned char)s[(size_t)pos];
                size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
                if ((size_t)pos + len > s.size()) len = s.size() - (size_t)pos;
                int64_t char_index = se[2].to_int();
                se[0] = Value::make_i64(pos + (int64_t)len);
                se[2] = Value::make_i64(char_index + 1);
                push_pass(Value::make_i64(char_index), Value::make_string(s.substr((size_t)pos, len)));
            } else if (iter.type == ValueType::OBJECT) {
                auto& keys = se[2].as_array()->elements;
                auto& vals = se[3].as_array()->elements;
                if (pos >= (int64_t)keys.size()) {
                    take_exit();
                    break;
                }
                se[0] = Value::make_i64(pos + 1);
                if (two) push_pass(keys[(size_t)pos], vals[(size_t)pos]);
                else push_pass(Value::make_none(), keys[(size_t)pos]);
            } else if (iter.type == ValueType::INT64) {
                auto ch = chan_lookup(iter.to_int());
                std::unique_lock<std::mutex> lock(ch->mtx);
                ++ch->waiting_recv;
                ch->cv_recv.wait(lock, [&]() {
                    return !ch->buffer.empty() || ch->closed.load();
                });
                --ch->waiting_recv;
                if (ch->buffer.empty()) {
                    // Closed + drained. Exit.
                    take_exit();
                    break;
                }
                Value val = std::move(ch->buffer.front());
                ch->buffer.pop_front();
                ch->cv_send.notify_one();
                lock.unlock();
                se[0] = Value::make_i64(pos + 1);
                push_pass(Value::make_i64(pos), std::move(val));
            } else {
                // NONE and anything else without elements walk nothing.
                take_exit();
            }
            break;
        }

        case OpCode::MAYBE_INIT_STATIC: {
            // op(1) consumed; operands: u16 slot, i16 skip_offset
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            int16_t off = (int16_t)(cf.chunk->code[cf.ip + 2] | (cf.chunk->code[cf.ip + 3] << 8));
            cf.ip += 4;
            if (cf.chunk->static_inited[slot]) {
                cf.ip = (size_t)((ptrdiff_t)cf.ip + off);
            } else {
                // Set the guard BEFORE running init so a recursive call from
                // inside the initializer terminates against the default
                // slot value rather than re-entering the init block.
                cf.chunk->static_inited[slot] = 1;
                // Fall through to the init block.
            }
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
                const std::string& full = cf.chunk->name_at(name_idx);
                size_t dot = full.find('.');
                if (dot != std::string::npos) {
                    std::string obj_name = full.substr(0, dot);
                    std::string rest = full.substr(dot + 1);

                    // First, check the current function's locals - the
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
                        const Chunk* fc = frame().chunk;
                        for (size_t li = 0; li < fc->name_count(); li++) {
                            if (ci_eq(fc->name_at(li), obj_name)) {
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
                // Fallback: no global by this name - try a zero-arg native
                // call. This makes constant-like natives (PI, E, TICK, NOW, ...)
                // usable as bare identifiers: `2 * PI`.
                {
                    const std::string& full2 = cf.chunk->name_at(name_idx);
                    const NativeEntry* nit = native_find(full2);
                    // Only the ones that genuinely take nothing. This used to
                    // call and let the arity wrapper throw, which is no longer
                    // there to catch the mistake before it indexes an argument
                    // that was never passed.
                    if (nit && nit->min_args == 0) {
                        try {
                            Value r = nit->fn({});
                            if (sp >= stack.size()) stack.resize(stack.size() * 2);
                            stack[sp++] = std::move(r);
                            break;
                        } catch (...) {
                            // native needs arguments or failed - fall through
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
            // Every store runs this, so nothing here may build a std::string.
            // The name is a pointer into the chunk's packed name buffer:
            // binding it to a std::string would copy the characters on each
            // store, and heap-allocate for any name past the small-string
            // budget - measured at +13% on a global-assignment loop and +45%
            // with a long name.
            const char* full = cf.chunk->name_at(name_idx);
            // The constant check waits until the slot is known, further down -
            // by slot it is an integer hash, by name it is a string hash plus,
            // now that the name lives packed in the chunk, a copy to build one.
            // A dotted name is never a constant, so nothing is skipped by
            // testing after that branch.
            // Dotted name fallback: "OBJ.FIELD" → load OBJ, set FIELD.
            // The chunk already knows which names carry a dot, so the common
            // case costs a bit test rather than a walk to the terminator.
            const char* dotp = cf.chunk->name_is_dotted(name_idx)
                             ? strchr(full, '.') : nullptr;
            if (dotp) {
                std::string obj_name(full, dotp - full);
                std::string rest(dotp + 1);
                // The base object: a local of the running function first,
                // the way LOAD_GLOBAL and CALL resolve the same names, then
                // a global.
                Value cur;
                bool have_cur = false;
                if (frames.size() > 1) {
                    const Chunk* fc = frame().chunk;
                    for (size_t li = 0; li < fc->name_count(); li++) {
                        if (name_ci_equal(fc->name_at(li), obj_name)) {
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
                // Walk the parts before the last one; the last is the field.
                bool ok = have_cur;
                size_t pos = 0;
                while (ok) {
                    size_t next = rest.find('.', pos);
                    if (next == std::string::npos) break;
                    std::string part = rest.substr(pos, next - pos);
                    Value nr;
#ifdef COM
                    if (com_try_get_field(cur, part, nr)) { cur = nr; }
                    else if (cur.as_com()) { ok = false; }
                    else
#endif
                    if (cur.type == ValueType::OBJECT) {
                        Value* f = cur.as_object()->get(part);
                        if (f) cur = *f; else ok = false;
                    } else ok = false;
                    pos = next + 1;
                }
                if (ok && cur.type == ValueType::OBJECT) {
                    std::string field = rest.substr(pos);
                    Value new_val = pop();
#ifdef COM
                    if (com_try_set_field(cur, field, new_val)) {
                        if (!reactive_bindings.empty() && !reactive_updating)
                            reactive_pending.push_back(full);
                        break;
                    }
                    if (cur.as_com())
                        throw std::runtime_error("COM: Unknown member '" + field + "'");
#endif
                    Value* old_val = cur.as_object()->get(field);
                    bool changed = !old_val || !values_equal(*old_val, new_val);
                    cur.as_object()->set(field, std::move(new_val));
                    if (changed && !reactive_bindings.empty() && !reactive_updating)
                        reactive_pending.push_back(full);
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
            // Protected constant - but allow the CONST declaration itself,
            // which is a store followed by MARK_CONST for the same name. That
            // keeps CONST idempotent across repeated module loads.
            if (!const_global_slots.empty() && const_global_slots.count(slot) > 0) {
                bool is_const_init = false;
                if (cf.ip < cf.chunk->code.size() &&
                    (OpCode)cf.chunk->code[cf.ip] == OpCode::MARK_CONST) {
                    uint16_t mc_idx = cf.chunk->code[cf.ip + 1] | (cf.chunk->code[cf.ip + 2] << 8);
                    if (strcmp(cf.chunk->name_at(mc_idx), full) == 0) is_const_init = true;
                }
                if (!is_const_init) {
                    throw jdError(ErrCode::RUNTIME_ERROR,
                        std::string("Cannot assign to constant '") + full + "'");
                }
            }
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
                            if (y == 0) { int eln = cf.chunk->line_at(trace_ip); throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero", eln); }
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
                            if (y == 0.0) { int eln = cf.chunk->line_at(trace_ip); throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero", eln); }
                            r = x / y; break;
                        case OpCode::IDIV:
                            if (y == 0.0) { int eln = cf.chunk->line_at(trace_ip); throw jdError(ErrCode::DIVISION_BY_ZERO, "Division by zero", eln); }
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
                std::function<Value(const Value&)> neg_rec = [&](const Value& v) -> Value {
                    if (v.type == ValueType::ARRAY) {
                        Value r = Value::make_array();
                        auto* outv = r.as_array();
                        outv->elements.reserve(v.as_array()->elements.size());
                        for (auto& e : v.as_array()->elements)
                            outv->elements.push_back(neg_rec(e));
                        return r;
                    }
                    if (is_integer_type(v.type))
                        return Value::make_i64(-v.to_int());
                    return Value::make_f64(-v.to_double());
                };
                push(neg_rec(a));
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
            // Element-wise comparison for arrays - RECURSIVE so 2-D matrices
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
        // Element-wise broadcast over arrays (incl. nested matrices) so
        // APL-style pipelines like `Masks BAND 1` and `RowIdx SHR Cols`
        // work the same as `+`/`-`/`*`. Scalar operands route through
        // arithmetic() which now handles bitwise ops as integer-only.

        case OpCode::BIT_AND:
        case OpCode::BIT_OR:
        case OpCode::BIT_XOR:
        case OpCode::BIT_SHL:
        case OpCode::BIT_SHR: {
            Value b = pop(); Value a = pop();
            if (a.type == ValueType::ARRAY || b.type == ValueType::ARRAY) {
                push(array_arithmetic(a, b, op));
            } else {
                push(arithmetic(a, b, op));
            }
            break;
        }

        case OpCode::BIT_NOT: {
            Value a = pop();
            if (a.type == ValueType::ARRAY) {
                // Element-wise ~. Reuse array_arithmetic by XORing with -1
                // (all-bits-set sign-extended) - semantically identical
                // and avoids a parallel codepath.
                push(array_arithmetic(a, Value::make_i64(-1), OpCode::BIT_XOR));
            } else {
                push(Value::make_i64(~a.to_int()));
            }
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

        case OpCode::JUMP_IF_NOT_NONE: {
            int16_t offset = (int16_t)(cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8));
            cf.ip += 2;
            Value v = std::move(stack[--sp]);
            if (v.type != ValueType::NONE) cf.ip += offset;
            break;
        }

        case OpCode::JUMP_ABS: {
            uint16_t addr = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip = addr;
            break;
        }

        // ── Functions ────────────────────────────────────────

        case OpCode::CALL_NATIVE: {
            // Compile-time-resolved native: index straight into native_table,
            // no name copy, no hash, no no-vectorize set lookups (cached).
            uint16_t slot = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            uint8_t argc = cf.chunk->code[cf.ip++];

            const NativeEntry* fn = ((size_t)slot < native_table.size()) ? native_table[slot] : nullptr;
            if (fn == nullptr || !fn->fn) {
                int eline = cf.chunk->line_at(trace_ip);
                throw jdError(ErrCode::UNDEFINED_FUNCTION,
                    "native '" + jdb_native_name(slot) + "' not available in this VM", eline);
            }

            int8_t nv = ((size_t)slot < native_novec.size()) ? native_novec[slot] : -1;
            if (nv < 0) {
                const std::string& nm = jdb_native_name(slot);
                nv = (jdb_no_vectorize(nm) || extra_no_vectorize.find(nm) != extra_no_vectorize.end()) ? 1 : 0;
                if ((size_t)slot >= native_novec.size()) native_novec.resize(slot + 1, -1);
                native_novec[slot] = nv;
            }

            // Pooled, depth-indexed arg buffer: reused across calls so a hot
            // CALL_NATIVE loop performs no per-call heap allocation. Cleanup
            // (release elements, pop depth) runs exactly once.
            Value nat_result;
            {
                if (m_arg_depth >= m_arg_pool.size()) m_arg_pool.emplace_back();
                std::vector<Value>& args = m_arg_pool[m_arg_depth];
                args.resize(argc);
                ++m_arg_depth;
                for (int i = argc - 1; i >= 0; i--) args[i] = pop();
                try {
                    check_native_arity(jdb_native_name(slot), *fn, args.size());
                    nat_result = invoke_native(fn->fn, args, nv != 0);
                } catch (const jdError&) {
                    args.clear(); --m_arg_depth; throw;
                } catch (const std::exception& e) {
                    args.clear(); --m_arg_depth;
                    int eline = frame().chunk->line_at(trace_ip);
                    // A builtin that already names itself keeps one prefix.
                    std::string name = jdb_native_name(slot);
                    std::string msg = e.what();
                    std::string prefix = name + ": ";
                    throw jdError(ErrCode::RUNTIME_ERROR,
                        msg.compare(0, prefix.size(), prefix) == 0 ? msg : prefix + msg, eline);
                } catch (...) {
                    args.clear(); --m_arg_depth;
                    int eline = frame().chunk->line_at(trace_ip);
                    throw jdError(ErrCode::RUNTIME_ERROR, jdb_native_name(slot) + ": internal error", eline);
                }
                args.clear();
                --m_arg_depth;
            }
            push(std::move(nat_result));
            break;
        }

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
                        if (argc < proto.min_arity || argc > proto.arity) {
                            int eline = cf.chunk->line_at(trace_ip);
                            throw jdError(ErrCode::WRONG_ARG_COUNT,
                                "Function '" + proto.name + "' expects " +
                                describe_arity(proto) + ", got " +
                                std::to_string(argc), eline);
                        }
                        // The parameters the caller left out stand where it
                        // would have put them, so the frame below sees one
                        // uniform argument list.
                        while (argc < proto.arity) {
                            if (sp >= stack.size()) stack.resize(stack.size() * 2);
                            stack[sp++] = proto.defaults[argc];
                            argc++;
                        }
                        if (proto.is_async) goto call_slow_path;
                        if (frames.size() >= JDB_MAX_FRAMES)
                            throw jdError(ErrCode::STACK_OVERFLOW, "Call stack overflow (max " + std::to_string(JDB_MAX_FRAMES) + " frames)");
                        size_t new_base = sp - argc;
                        size_t needed = new_base + proto.chunk.name_count();
                        while (needed >= stack.size()) stack.resize(stack.size() * 2);
                        // The callee's locals start absent, whatever an
                        // earlier call left in these slots.
                        for (size_t i = sp; i < needed; i++) stack[i] = Value::make_none();
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
            const NativeEntry* native_it = native_find(func_name);
            if (native_it) {
                std::vector<Value> args(argc);
                for (int i = argc - 1; i >= 0; i--) {
                    args[i] = pop();
                }
                check_native_arity(func_name, *native_it, args.size());

                // Auto-vectorize: if any arg is an array and the function
                // is not array-aware, apply element-wise automatically.
                // The list of array-aware functions lives in jdb_no_vectorize()
                // at file scope so VM::call_function (the bridge entry) can
                // share the same set.

                bool has_array = false;
                size_t arr_len = 0;
                if (!jdb_no_vectorize(func_name)
                        && extra_no_vectorize.find(func_name) == extra_no_vectorize.end()) {
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
                        if (!any_arr) return native_it->fn(cur_args);
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
                        // A builtin that already names itself keeps one prefix.
                        std::string msg = e.what();
                        std::string prefix = func_name + ": ";
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            msg.compare(0, prefix.size(), prefix) == 0 ? msg : prefix + msg);
                    }
                } else {
                    try {
                        push(native_it->fn(args));
                    } catch (const jdError&) {
                        throw;
                    } catch (const std::out_of_range&) {
                        int eline = frame().chunk->line_at(trace_ip);
                        throw jdError(ErrCode::WRONG_ARG_COUNT,
                            func_name + ": wrong number of arguments (got " + std::to_string(argc) + ")", eline);
                    } catch (const std::bad_alloc&) {
                        int eline = frame().chunk->line_at(trace_ip);
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            func_name + ": out of memory", eline);
                    } catch (const std::exception& e) {
                        int eline = frame().chunk->line_at(trace_ip);
                        // A builtin that already names itself keeps one prefix.
                        std::string msg = e.what();
                        std::string prefix = func_name + ": ";
                        throw jdError(ErrCode::RUNTIME_ERROR,
                            msg.compare(0, prefix.size(), prefix) == 0 ? msg : prefix + msg, eline);
                    } catch (...) {
                        int eline = frame().chunk->line_at(trace_ip);
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
                        for (uint16_t vi = 0; vi < frame().chunk->name_count(); vi++) {
                            if (frame().chunk->name_at(vi) == func_name) {
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
                        // a local - even though the same call works at the
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
                            const Chunk* fc = frame().chunk;
                            for (size_t li = 0; li < fc->name_count(); li++) {
                                if (ci_eq(fc->name_at(li), obj_name)) {
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
                    int eline = frame().chunk->line_at(trace_ip);
                    throw jdError(ErrCode::UNDEFINED_FUNCTION, "Undefined function: " + func_name, eline);
                }
            call_done: break;
            }
            FuncProto& proto = (*func_protos)[it->second];
            if (argc < proto.min_arity || argc > proto.arity) {
                throw std::runtime_error("Function '" + func_name + "' expects " +
                    describe_arity(proto) + ", got " + std::to_string(argc));
            }
            while (argc < proto.arity) {
                if (sp >= stack.size()) stack.resize(stack.size() * 2);
                stack[sp++] = proto.defaults[argc];
                argc++;
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

                // Copy function registry and globals for the async VM.
                // owned_funcs / func_protos is a std::deque so pointers stay
                // stable; convert to vector for the worker via range copy.
                auto& funcs_src = func_protos ? *func_protos : owned_funcs;
                auto funcs_copy = std::make_shared<std::vector<FuncProto>>(
                    funcs_src.begin(), funcs_src.end());
                auto globals_copy = std::make_shared<std::vector<Value>>(globals);
                auto gnames_copy = std::make_shared<std::unordered_map<std::string, uint16_t>>(global_names);
                auto fmap_copy = std::make_shared<std::unordered_map<std::string, size_t>>(func_map);
                std::string fn = func_name;

                task->thread = std::thread([task, funcs_copy, fn, async_args]() {
                    install_seh_translator_for_this_thread();
                    try {
                        VM async_vm;
                        // Module natives (AI/LLM/HTTP/etc.) aren't auto-carried
                        // from the parent VM - re-register them on the worker
                        // so ASYNC FUNCs can call AI.SET / AI.CHAT / etc.
                        extern void register_ai_builtins(VM&);
                        extern void register_llm_builtins(VM&);
                        register_ai_builtins(async_vm);
                        register_llm_builtins(async_vm);
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
            size_t needed = new_base + proto.chunk.name_count();
            while (needed >= stack.size()) stack.resize(stack.size() * 2);
            for (size_t i = sp; i < needed; i++) stack[i] = Value::make_none();
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
                    const void* tobj = (const void*)type_val->as_string();
                    auto& mcache = frame().chunk->method_cache;

                    std::vector<Value> call_args;
                    call_args.reserve(args.size() + 1);
                    call_args.push_back(obj); // THIS
                    for (auto& a : args) call_args.push_back(std::move(a));

                    // Hit: same receiver type at this site as last time, and the
                    // function table has not been reloaded since. Skips building
                    // "Type.method" and both name lookups.
                    if (name_idx < mcache.size()) {
                        const auto& e = mcache[name_idx];
                        if (e.gen == func_map_generation && e.func_idx >= 0 &&
                            e.type_obj == tobj) {
                            push(call_function_idx(e.func_idx, call_args));
                            break;
                        }
                    }

                    std::string type_method = type_val->as_string()->data + "." + method;
                    auto fit = func_map.find(type_method);
                    if (fit != func_map.end()) {
                        if (name_idx >= mcache.size()) mcache.resize(name_idx + 1);
                        auto& e = mcache[name_idx];
                        e.gen = func_map_generation;
                        e.func_idx = (int32_t)fit->second;
                        e.type_obj = tobj;
                        push(call_function_idx(e.func_idx, call_args));
                        break;
                    }
                    // Not a user method - a native of that name may still exist.
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
            // Release every local in the popped frame so refcounted UDTs
            // (and strings/arrays) don't outlive the SUB. Without this,
            // SUB DISPOSE for nested locals never fires until their slot
            // gets overwritten by a later push.
            size_t base = cf.stack_base;
            for (size_t i = base; i < sp; ++i) stack[i] = Value();
            frames.pop_back();
            sp = base;
            push(Value::make_none());
            if (frames.size() <= min_frame_depth) return;
            break;
        }

        case OpCode::RETURN_VAL: {
            // Move the return value out, then release all other locals
            // (slots [base+1 .. sp-1]) before placing the return at base.
            // Same reason as RETURN_VOID - keep refcounted locals from
            // outliving the call.
            size_t base = cf.stack_base;
            Value retval = std::move(stack[sp - 1]);
            for (size_t i = base; i + 1 < sp; ++i) stack[i] = Value();
            stack[base] = std::move(retval);
            sp = base + 1;
            frames.pop_back();
            // cf is now dangling - break so next iteration refetches
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
            std::fflush(stdout);
#ifdef __EMSCRIPTEN__
            { char* line = jdb_read_line_js();
              if (line) { input = line; std::free(line); } }
#elif defined(JDB_MCU)
            // The board's stdio does not echo: read by character, show
            // what arrives, honour backspace, stop at return. A Ctrl-C
            // comes back as -1 and ends the program here as anywhere.
            for (;;) {
                int ch = jdb_stdin_getc(-1);
                if (ch < 0) {
                    is_waiting_input = false;
                    emit("\nBreak at line " + std::to_string(frames.back().chunk->line_at(frames.back().ip)) + "\n");
                    is_halted = true;
                    return;
                }
                if (ch == '\r' || ch == '\n') break;
                if (ch == 8 || ch == 127) {
                    if (!input.empty()) {
                        input.pop_back();
                        std::printf("\b \b");
                        std::fflush(stdout);
                    }
                    continue;
                }
                if (ch < 32 || ch > 126) continue;
                input.push_back((char)ch);
                std::printf("%c", ch);
                std::fflush(stdout);
            }
            std::printf("\r\n");
            std::fflush(stdout);
#else
            std::getline(std::cin, input);
#endif
            is_waiting_input = false;

            Value val;
            // The sigil decides: a $ variable keeps what was typed as text.
            // Anything else becomes a number when it reads as one.
            const Chunk* name_chunk = (frames.size() <= 1) ? cf.chunk : frame().chunk;
            const char* target = name_chunk->name_at(name_idx);
            size_t target_len = target ? std::strlen(target) : 0;
            if (target_len && target[target_len - 1] == '$') {
                val = Value::make_string(input);
            } else
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
            // Stack has key1,val1,...,keyN,valN - pop in reverse
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
                // Vectorised gather: idx is an array of indices → return gathered
                // array. The result keeps the shape of the index set, so a
                // matrix of indices gives a matrix. Coercing a nested index to
                // a number used to yield the same element for every row.
                if (idx.type == ValueType::ARRAY) {
                    std::function<Value(const Value&)> gather = [&](const Value& iv) -> Value {
                        if (iv.type == ValueType::ARRAY) {
                            Value out = Value::make_array();
                            auto& dst = out.as_array()->elements;
                            auto& src = iv.as_array()->elements;
                            dst.reserve(src.size());
                            for (auto& e : src) dst.push_back(gather(e));
                            return out;
                        }
                        int64_t i = iv.to_int();
                        if (i < 0 || i >= (int64_t)arr->elements.size()) {
                            int eln = cf.chunk->line_at(trace_ip);
                            throw jdError(ErrCode::INDEX_OUT_OF_RANGE, "Array index out of bounds: " + std::to_string(i), eln);
                        }
                        return arr->elements[i];
                    };
                    push(gather(idx));
                } else {
                    int64_t i = idx.to_int();
                    if (i < 0 || i >= (int64_t)arr->elements.size()) {
                        int eln = cf.chunk->line_at(trace_ip);
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
                    // Slot replacement vs. cyclic broadcast: the historical
                    // behaviour of `arr[i] = vec` was to flatten vec and cycle
                    // its leaves into target's existing storage. That broadcasts
                    // a row-vector into a multi-D slot (test_slice.jdb relies
                    // on this), but it also silently mutates any alias of
                    // arr[i] that the user captured with `LET row = arr[i]` -
                    // a real bug-source in the Mandelbrot APL bench.
                    //
                    // Compromise: when target and val are both flat 1D arrays
                    // of equal length, do a real slot-replacement (move). This
                    // preserves the alias of the OLD row that LET captured.
                    // Otherwise (multi-D target, or shape mismatch), keep the
                    // cyclic-broadcast semantics.
                    auto* tgt_arr = target.as_array();
                    auto* val_arr = val.as_array();
                    bool target_is_flat = !tgt_arr->elements.empty() &&
                        tgt_arr->elements[0].type != ValueType::ARRAY;
                    bool val_is_flat = !val_arr->elements.empty() &&
                        val_arr->elements[0].type != ValueType::ARRAY;
                    bool same_size = tgt_arr->elements.size() == val_arr->elements.size();
                    if (target_is_flat && val_is_flat && same_size) {
                        target = std::move(val);
                    } else {
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
            if (target & CAST_SCALAR_ONLY) {
                target &= static_cast<uint8_t>(~CAST_SCALAR_ONLY);
                if (!is_numeric(v.type)) { push(v); break; }
            }
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
            // Get current line from the chunk
            int err_line = 0;
            if (frame().ip > 0)
                err_line = frame().chunk->line_at(frame().ip - 1);
            throw std::runtime_error(msg.to_string());
        }

        case OpCode::STOP_OP: {
            int stop_line = 0;
            if (frame().ip > 0)
                stop_line = frame().chunk->line_at(frame().ip - 1);
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
            // User `END` statement - terminate the whole program,
            // unwinding any nested call_function() (e.g. an event handler
            // calling END from inside the main DO loop) and any sub-run
            // (REPL `run` command goes through run_code()).
            // The compiler always leaves the exit status on the stack.
            exit_code = (int)stack[--sp].to_double();
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
            // on the stack - same convention as INDEX_SET.
            break;
        }

        // ── Superinstructions ────────────────────────────────
        case OpCode::MARK_CONST: {
            uint16_t name_idx = cf.chunk->code[cf.ip] | (cf.chunk->code[cf.ip + 1] << 8);
            cf.ip += 2;
            std::string cname = cf.chunk->name_at(name_idx);
            const_global_slots.insert(ensure_global(name_idx));
            const_globals.insert(std::move(cname));
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
                // their shape - matches the unfused CMP_EQ handler.
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
            if (!frames.empty() && frame().ip > 0)
                err_line = frame().chunk->line_at(frame().ip - 1);

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
            std::string err_file;
            if (!frames.empty() && frame().ip > 0) {
                err_line = frame().chunk->line_at(frame().ip - 1);
                const std::string& f = frame().chunk->file_at(frame().ip - 1);
                if (!f.empty() && f != frames.front().chunk->source_file)
                    err_file = f.substr(f.find_last_of("/\\") + 1);
            }
            // If it's already a jdError, preserve code and add line if missing
            if (auto* je = dynamic_cast<const jdError*>(&e)) {
                if (je->line > 0) throw jdError(je->code, je->what(), je->line, je->file);
                throw jdError(je->code, je->what(), err_line, err_file);
            }
            throw jdError(ErrCode::RUNTIME_ERROR, e.what(), err_line, err_file);
        }
      }
    }
}

// ── Arithmetic helper ────────────────────────────────────────

Value VM::arithmetic(const Value& a, const Value& b, OpCode op) {
    // Bitwise / shift ops are integer-only: coerce both sides through
    // to_int() and do the op directly. Float operands match the existing
    // function-form (`SHL(1.5, 2)` → `int64_t(1) << int64_t(2)`).
    if (op == OpCode::BIT_AND || op == OpCode::BIT_OR || op == OpCode::BIT_XOR ||
        op == OpCode::BIT_SHL || op == OpCode::BIT_SHR) {
        int64_t ia = a.to_int(), ib = b.to_int(), r;
        switch (op) {
            case OpCode::BIT_AND: r = ia & ib; break;
            case OpCode::BIT_OR:  r = ia | ib; break;
            case OpCode::BIT_XOR: r = ia ^ ib; break;
            case OpCode::BIT_SHL: r = ia << ib; break;
            case OpCode::BIT_SHR: r = ia >> ib; break;
            default:              r = 0;
        }
        return Value::make_i64(r);
    }
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

    // Reference-type equality. A MAP/OBJECT or TENSOR must not fall through to
    // the to_double() path below - to_double() reads every reference type as
    // 0.0, which makes `map = NULL`, `map = 0`, and `obj1 = obj2` all compare
    // equal. Resolve =/<> by heap identity (same type + same object); leave
    // ordering (LT/GT/LE/GE) on the legacy numeric path. ARRAY is intentionally
    // excluded: element-wise array compares are intercepted before compare()
    // is ever reached, so arrays here are vanishingly rare and best left alone.
    if (a.type == ValueType::OBJECT || a.type == ValueType::TENSOR ||
        b.type == ValueType::OBJECT || b.type == ValueType::TENSOR) {
        if (op == OpCode::CMP_EQ || op == OpCode::CMP_NE) {
            bool eq = (a.type == b.type && a.obj == b.obj && a.obj != nullptr);
            return Value::make_bool(op == OpCode::CMP_EQ ? eq : !eq);
        }
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

// ── Process-global native slot registry ──────────────────────────
// Stable name->index map shared by every VM so CALL_NATIVE slot operands
// baked into a chunk stay valid under any VM. Guarded by a mutex because an
// async-worker VM may register on another thread while the main thread relinks.
static std::mutex& native_slot_mutex() { static std::mutex m; return m; }
#ifdef JDB_MCU
// The names live where the registration wrote them: a literal in flash
// for every builtin the interpreter and the boards register, a copy on
// the heap only for a host that builds a name at run time.
static std::vector<const char*>& native_slot_names() {
    static std::vector<const char*> v;
    if (v.empty()) v.reserve(512);
    return v;
}
#else
static std::vector<std::string>& native_slot_names() {
    static std::vector<std::string> v;
    if (v.empty()) v.reserve(512);
    return v;
}
#endif

#ifdef JDB_MCU
// A hash from name to slot costs a node and a second copy of the name
// for every builtin, and the only caller is the compiler resolving a
// word it has just read. The names are already kept once, in slot
// order; this is four bytes a builtin holding the same slots in
// alphabetical order, searched rather than hashed.
static std::vector<int32_t>& native_slot_sorted() {
    static std::vector<int32_t> v;
    if (v.empty()) v.reserve(512);
    return v;
}

// Both halves of the search, on the caller's lock.
static size_t slot_lower_bound(const char* name) {
    const auto& v = native_slot_sorted();
    const auto& names = native_slot_names();
    size_t lo = 0, hi = v.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(names[v[mid]], name) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int slot_find_locked(const char* name) {
    size_t at = slot_lower_bound(name);
    const auto& v = native_slot_sorted();
    if (at < v.size() && strcmp(native_slot_names()[v[at]], name) == 0) return v[at];
    return -1;
}
#else
static std::unordered_map<std::string, int>& native_slot_map() {
    static std::unordered_map<std::string, int> m; return m;
}
#endif

int jdb_native_slot(const std::string& name) {
    std::lock_guard<std::mutex> lk(native_slot_mutex());
#ifdef JDB_MCU
    return slot_find_locked(name.c_str());
#else
    auto& m = native_slot_map();
    auto it = m.find(name);
    return it == m.end() ? -1 : it->second;
#endif
}

std::string jdb_native_name(int slot) {
    std::lock_guard<std::mutex> lk(native_slot_mutex());
    auto& v = native_slot_names();
    if (slot < 0 || (size_t)slot >= v.size()) return std::string();
    return std::string(v[slot]);
}

// copy says whether the name has to be duplicated: a literal lives as
// long as the program, a name built by a host does not.
static int native_slot_intern(const char* name, bool copy) {
    std::lock_guard<std::mutex> lk(native_slot_mutex());
#ifdef JDB_MCU
    size_t at = slot_lower_bound(name);
    auto& v = native_slot_sorted();
    if (at < v.size() && strcmp(native_slot_names()[v[at]], name) == 0) return v[at];
    int slot = (int)native_slot_names().size();
    native_slot_names().push_back(copy ? strdup(name) : name);
    v.insert(v.begin() + (long)at, slot);
    return slot;
#else
    (void)copy;
    auto& m = native_slot_map();
    auto it = m.find(name);
    if (it != m.end()) return it->second;
    int slot = (int)m.size();
    m[name] = slot;
    native_slot_names().push_back(name);
    return slot;
#endif
}

// Every path that reaches a builtin runs this first. It used to be a
// lambda wrapped around each function, which is why there were two
// std::functions and a heap copy of the name for all of them.
static void check_native_arity(const std::string& name, const VM::NativeEntry& e, size_t argc) {
    int n = (int)argc;
    if (n < e.min_args)
        throw jdError(ErrCode::WRONG_ARG_COUNT,
            name + ": expected at least " + std::to_string(e.min_args) + " argument(s), got " + std::to_string(n));
    if (e.max_args >= 0 && n > e.max_args)
        throw jdError(ErrCode::WRONG_ARG_COUNT,
            name + ": expected at most " + std::to_string(e.max_args) + " argument(s), got " + std::to_string(n));
}

const VM::NativeEntry* VM::native_find(const std::string& name) const {
#ifdef JDB_MCU
    int slot = jdb_native_slot(name);
    if (slot < 0 || (size_t)slot >= native_table.size()) return nullptr;
    return native_table[slot];
#else
    auto it = natives.find(name);
    return it == natives.end() ? nullptr : &it->second;
#endif
}

std::vector<std::string> VM::native_names() const {
    std::vector<std::string> out;
#ifdef JDB_MCU
    for (size_t slot = 0; slot < native_table.size(); slot++)
        if (native_table[slot]) out.push_back(jdb_native_name((int)slot));
#else
    out.reserve(natives.size());
    for (auto& kv : natives) out.push_back(kv.first);
#endif
    return out;
}

size_t VM::native_registry_bytes() const {
    size_t n = native_table.capacity() * sizeof(NativeEntry*) + native_novec.capacity();
#ifdef JDB_MCU
    n += native_store.size() * sizeof(NativeEntry);
    n += native_slot_sorted().capacity() * sizeof(int32_t);
#else
    for (auto& kv : natives) n += sizeof(kv) + kv.first.capacity() + 1;
#endif
#ifdef JDB_MCU
    n += native_slot_names().capacity() * sizeof(const char*);
#else
    for (auto& name : native_slot_names())
        n += sizeof(std::string) + (name.capacity() > 15 ? name.capacity() + 1 : 0);
#endif
    return n;
}

void VM::install_native(int slot, const char* name, NativeFunc fn) {
    (void)name;
    if ((size_t)slot >= native_table.size()) {
        native_table.resize(slot + 1);
        native_novec.resize(slot + 1, -1);
    }
    NativeEntry* entry = native_table[slot];
    if (!entry) {
#ifdef JDB_MCU
        native_store.emplace_back();
        entry = &native_store.back();
#else
        entry = &natives[name];
#endif
    }
    entry->fn = std::move(fn);
    entry->min_args = 0;
    entry->max_args = -1;
    native_table[slot] = entry;
    native_novec[slot] = -1;  // recomputed lazily (extra_no_vectorize may fill later)
}

// Note: Functions registered with this simple overload rely on the
// CALL handler's try/catch for safety. The arity-checked overload
// (name, min, max, fn) provides better error messages.

void VM::register_native(const std::string& name, NativeFunc fn) {
    install_native(native_slot_intern(name.c_str(), true), name.c_str(), std::move(fn));
}

void VM::register_native(const char* name, NativeFunc fn) {
    install_native(native_slot_intern(name, false), name, std::move(fn));
}

void VM::register_native(const std::string& name, int min_args, int max_args, NativeFunc fn) {
    int slot = native_slot_intern(name.c_str(), true);
    install_native(slot, name.c_str(), std::move(fn));
    native_table[slot]->min_args = (int16_t)min_args;
    native_table[slot]->max_args = (int16_t)max_args;
}

void VM::register_native(const char* name, int min_args, int max_args, NativeFunc fn) {
    int slot = native_slot_intern(name, false);
    install_native(slot, name, std::move(fn));
    native_table[slot]->min_args = (int16_t)min_args;
    native_table[slot]->max_args = (int16_t)max_args;
}



// ── Debug adapter support ────────────────────────────────────────

// Evaluate a DAP hitCondition (">5", "<=10", "==3", "%4", or a bare "7")
// against the current hit count. A bare number breaks exactly on that hit.
static bool dap_hit_cond_met(const std::string& cond, int count) {
    size_t i = 0; std::string op;
    while (i < cond.size() && (cond[i] == '>' || cond[i] == '<' ||
                               cond[i] == '=' || cond[i] == '%' || cond[i] == '!'))
        op += cond[i++];
    while (i < cond.size() && std::isspace((unsigned char)cond[i])) i++;
    int n = 0;
    try { n = std::stoi(cond.substr(i)); } catch (...) { return true; }
    if (op == ">")  return count > n;
    if (op == ">=") return count >= n;
    if (op == "<")  return count < n;
    if (op == "<=") return count <= n;
    if (op == "%")  return n != 0 && (count % n) == 0;
    if (op == "==" || op == "=") return count == n;
    return count == n;  // bare number
}

void VM::debug_check(int line) {
    if (!debug || line <= 0) return;
    // Need either the socket DAP or a host hook to have somewhere to break to.
    if (!debug->dap && !debug->host_hook) return;
    // A watch-expression eval runs as a sub-chunk; don't let it re-break.
    if (debug->suppress) return;
    // Inside REPL/EVAL/EXECUTE sub-runs, never pause for the socket DAP: the
    // sub-chunk's line numbers would otherwise look like the main program and
    // re-trigger stopped events. The embed/Godot host hook is the opposite
    // case - there ALL script execution arrives as run_code sub-runs (every
    // _process / _input callback is a jdb_embed_eval), so the hook path must
    // debug them.
    if (!debug->host_hook && subrun_depth > 0) return;
    if (frames.empty()) return;
    const Chunk* cur_chunk = frames.back().chunk;
    const std::string& cur_file_ref = cur_chunk->file_at(frames.back().ip > 0 ? frames.back().ip - 1 : 0);
    if (line == debug->last_debug_line && &cur_file_ref == debug->last_debug_file) return;
    debug->last_debug_line = line;
    debug->last_debug_file = &cur_file_ref;

    std::string cur_file = cur_file_ref;
    std::string cur_file_norm = normalize_path(cur_file);

    bool should_pause = false;
    std::string pause_reason = "step";

    // 1. Breakpoint check (highest priority) - match normalized file + line
    {
        BreakpointInfo* bp = nullptr;
        auto it = debug->breakpoints.find(cur_file_norm);
        if (it != debug->breakpoints.end()) {
            auto j = it->second.find(line);
            if (j != it->second.end()) bp = &j->second;
        }
        if (!bp) {  // line-only fallback (breakpoints set without a file)
            auto it2 = debug->breakpoints.find("");
            if (it2 != debug->breakpoints.end()) {
                auto j = it2->second.find(line);
                if (j != it2->second.end()) bp = &j->second;
            }
        }
        if (bp) {
            bp->hit_count++;
            bool fire = true;
            if (fire && !bp->hit_condition.empty())
                fire = dap_hit_cond_met(bp->hit_condition, bp->hit_count);
            if (fire && !bp->condition.empty()) {
                Value cv;
                fire = debug_eval_value(bp->condition, cv) && cv.to_bool();
            }
            if (fire) {
                if (!bp->log_message.empty()) {
                    // Logpoint: interpolate {expr} parts, emit, and keep going.
                    std::string out;
                    const std::string& m = bp->log_message;
                    for (size_t k = 0; k < m.size(); ) {
                        if (m[k] == '{') {
                            size_t e = m.find('}', k);
                            if (e != std::string::npos) {
                                Value lv;
                                std::string ex = m.substr(k + 1, e - k - 1);
                                out += debug_eval_value(ex, lv) ? lv.to_string() : "?";
                                k = e + 1;
                                continue;
                            }
                        }
                        out += m[k++];
                    }
                    debug->dap->send_output_message(out + "\n");
                } else {
                    should_pause = true;
                    pause_reason = "breakpoint";
                }
            }
        }
        // Host-supplied breakpoint source (Godot editor breakpoints, polled
        // per line via is_breakpoint) - in addition to the map above.
        if (!should_pause && debug->line_break && debug->line_break(debug->line_ud, line)) {
            should_pause = true;
            pause_reason = "breakpoint";
        }
    }
    // 2. Stepping
    if (!should_pause) {
        switch (debug->state) {
            case DebugState::PAUSED:
                // The entry stop waits for the program's own file, past the
                // top-level code of its imports.
                should_pause = !(!debug->host_hook && debug->is_entry &&
                                 cur_file != cur_chunk->source_file);
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
                // At the same depth, the top-level code of an imported module
                // is stepped over like a call: pause only back in the file of
                // the last pause or in the chunk's own file, and not on the
                // line of the last pause, which a returning call lands on.
                if (frames.size() < debug->step_over_depth ||
                    (frames.size() == debug->step_over_depth &&
                     (cur_file == debug->pause_file || cur_file == cur_chunk->source_file) &&
                     !(line == debug->pause_line && cur_file == debug->pause_file))) {
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
        debug->pause_file = cur_file;
        debug->pause_line = line;
        if (debug->host_hook) {
            // Synchronous host break: the hook inspects the VM and sets the
            // next action (state) via the embed control ABI, then returns.
            // (No "entry" stop - that's a DAP-only concept.)
            debug->host_hook(debug->host_ud, line, pause_reason.c_str());
        } else {
            // A breakpoint on the entry line keeps its reason: a client
            // that skips the entry stop must still halt there.
            if (debug->is_entry) {
                if (pause_reason != "breakpoint") pause_reason = "entry";
                debug->is_entry = false;
            }
            // Fresh variable handles for this stop; the client re-requests
            // scopes/variables after every stopped event.
            debug_clear_var_handles();
            debug->dap->send_stopped_message(pause_reason, line,
                cur_file.empty() ? debug->program_path : cur_file);
            debug->pause();
        }
    }
}

int VM::debug_current_line() const {
    if (frames.empty()) return 0;
    auto& f = frames.back();
    size_t ip = f.ip > 0 ? f.ip - 1 : 0;
    return f.chunk->line_at(ip);
    return 0;
}

std::string VM::debug_current_file() const {
    if (frames.empty()) return "";
    auto& f = frames.back();
    return f.chunk->file_at(f.ip > 0 ? f.ip - 1 : 0);
}

size_t VM::debug_call_depth() const {
    return frames.size();
}

bool VM::debug_goto_line(int target_line) {
    if (frames.empty()) return false;
    auto& f = frames.back();
    // The first bytecode offset where the line transitions TO target_line -
    // an opcode boundary, never the middle of an operand. The line table
    // stores exactly those transitions.
    size_t at = f.chunk->first_ip_of_line(target_line, debug_current_file());
    if (at >= f.chunk->code.size()) return false;
    f.ip = at;
    // Reset stack to frame base and clear exception handlers
    sp = f.stack_base;
    try_handlers.clear();
    if (debug) debug->last_debug_line = -1;
    return true;
}

bool VM::debug_reload_main(Chunk& new_main, std::vector<FuncProto>& new_funcs, int target_line) {
    if (frames.empty()) return false;

    // A function whose body is currently executing must not be overwritten:
    // the live frame holds a pointer into its chunk and an ip into its
    // bytecode. Recompiling it under the running frame would jump into
    // re-laid-out code. Skip those; merge everything else.
    auto on_stack = [&](const Chunk* c) {
        for (auto& fr : frames) if (fr.chunk == c) return true;
        return false;
    };
    for (auto& f : new_funcs) {
        f.chunk.shrink();
        auto it = func_map.find(f.name);
        if (it != func_map.end()) {
            if (on_stack(&owned_funcs[it->second].chunk)) continue;
            owned_funcs[it->second] = std::move(f);
        } else {
            func_map[f.name] = owned_funcs.size();
            owned_funcs.push_back(std::move(f));
        }
    }
    func_protos = &owned_funcs;
    func_map_generation++;

    // Register top-level globals the recompiled chunk introduces (mirrors
    // load()), so a freshly added variable resolves to a real slot rather
    // than tripping LOAD_GLOBAL's NONE-name-as-native fallback.
    for (uint16_t i = 0; i < new_main.name_count(); i++) {
        std::string name = new_main.name_at(i);
        if (global_names.count(name)) continue;
        global_names[name] = static_cast<uint16_t>(globals.size());
        globals.push_back(Value::make_none());
    }

    // Reposition only when paused at top level (just the main frame). Inside a
    // function call the main chunk is not the active one, so we keep the
    // current position and let only the merged off-stack bodies take effect.
    if (frames.size() != 1) return true;

    CallFrame& f = frames[0];
    f.chunk = &new_main;

    size_t at = new_main.first_ip_of_line(target_line, new_main.source_file);
    if (at < new_main.code.size()) {
        f.ip = at;
        sp = f.stack_base;
        try_handlers.clear();
        if (debug) debug->last_debug_line = -1;
        return true;
    }
    // Target line no longer exists (deleted or blank now): restart the chunk.
    f.ip = 0;
    sp = f.stack_base;
    try_handlers.clear();
    if (debug) debug->last_debug_line = -1;
    return true;
}

std::vector<VM::DebugFrame> VM::debug_get_stack_frames() const {
    std::vector<DebugFrame> result;
    // Skip main frame (index 0), iterate user function frames
    for (size_t i = 1; i < frames.size(); i++) {
        int line = 0;
        size_t ip = frames[i].ip > 0 ? frames[i].ip - 1 : 0;
        line = frames[i].chunk->line_at(ip);
        // Find function name by matching chunk pointer
        std::string name = "<unknown>";
        if (func_protos) {
            for (auto& fp : *func_protos) {
                if (&fp.chunk == frames[i].chunk) { name = fp.name; break; }
            }
        }
        std::string file = frames[i].chunk->file_at(ip);
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
    return debug_get_locals_at(0);
}

std::vector<std::pair<std::string, std::string>> VM::debug_get_locals_at(int level) const {
    std::vector<std::pair<std::string, std::string>> result;
    if (level < 0) return result;
    // level 0 = innermost frame. Index 0 is the global frame (no locals).
    int idx = (int)frames.size() - 1 - level;
    if (idx <= 0 || idx >= (int)frames.size()) return result;

    auto& f = frames[idx];
    std::string func_name;
    if (func_protos) {
        for (auto& fp : *func_protos) {
            if (&fp.chunk == f.chunk) { func_name = fp.name; break; }
        }
    }

    // A frame's locals live between its base and the next frame's base
    // (or sp for the innermost) - bound by that so deeper frames don't
    // pick up the slots of the frames above them.
    size_t upper = (idx + 1 < (int)frames.size()) ? frames[idx + 1].stack_base : sp;
    for (uint16_t i = 0; i < f.chunk->name_count(); i++) {
        size_t abs = f.stack_base + i;
        if (abs < upper) {
            std::string name = func_name.empty()
                ? f.chunk->name_at(i)
                : func_name + "_" + f.chunk->name_at(i);
            result.push_back({name, stack[abs].to_string()});
        }
    }
    return result;
}

// ── Structured variable inspection (expandable arrays/maps/UDTs) ──

int VM::debug_register_var(const Value& v, const std::string& eval_name) {
    bool expandable =
        (v.type == ValueType::ARRAY  && !v.as_array()->elements.empty()) ||
        (v.type == ValueType::OBJECT && !v.as_object()->fields.empty());
    if (!expandable) return 0;
    int ref = debug_next_var_handle_++;
    debug_var_handles_[ref] = { v, eval_name };
    return ref;
}

void VM::debug_clear_var_handles() {
    debug_var_handles_.clear();
    debug_next_var_handle_ = 1;
}

std::vector<VM::DebugVar> VM::debug_vars_global() {
    std::vector<DebugVar> out;
    for (auto& [name, slot] : global_names) {
        if (slot >= globals.size()) continue;
        if (name.find("__") != std::string::npos) continue;
        const Value& v = globals[slot];
        out.push_back({ name, v.to_string(), name, debug_register_var(v, name) });
    }
    std::sort(out.begin(), out.end(),
        [](const DebugVar& a, const DebugVar& b) { return a.name < b.name; });
    return out;
}

std::vector<VM::DebugVar> VM::debug_vars_local(int frame_index) {
    std::vector<DebugVar> out;
    if (frame_index <= 0 || frame_index >= (int)frames.size()) return out;
    auto& f = frames[frame_index];
    // Locals live between this frame's base and the next frame's base (or sp
    // for the innermost), so deeper frames don't bleed their slots in.
    size_t upper = (frame_index + 1 < (int)frames.size())
                   ? frames[frame_index + 1].stack_base : sp;
    for (uint16_t i = 0; i < f.chunk->name_count(); i++) {
        size_t abs = f.stack_base + i;
        if (abs >= upper) continue;
        const std::string& name = f.chunk->name_at(i);
        const Value& v = stack[abs];
        out.push_back({ name, v.to_string(), name, debug_register_var(v, name) });
    }
    return out;
}

std::vector<VM::DebugVar> VM::debug_var_children(int ref) {
    std::vector<DebugVar> out;
    auto it = debug_var_handles_.find(ref);
    if (it == debug_var_handles_.end()) return out;
    const Value& v = it->second.value;
    const std::string base = it->second.eval_name;  // copy: map may rehash below
    if (v.type == ValueType::ARRAY) {
        auto* a = v.as_array();
        for (size_t i = 0; i < a->elements.size(); i++) {
            const Value& e = a->elements[i];
            std::string en = base + "[" + std::to_string(i) + "]";
            out.push_back({ "[" + std::to_string(i) + "]", e.to_string(), en,
                            debug_register_var(e, en) });
        }
    } else if (v.type == ValueType::OBJECT) {
        auto* o = v.as_object();
        for (auto& [k, val] : o->fields) {
            std::string en = base + "." + k;
            out.push_back({ k, val.to_string(), en, debug_register_var(val, en) });
        }
    }
    return out;
}

std::pair<std::string, int> VM::debug_eval_watch(const std::string& expr) {
    Value v;
    if (!debug_eval_value(expr, v)) return { "", 0 };
    return { v.to_string(), debug_register_var(v, expr) };
}

bool VM::debug_eval_value(const std::string& expr, Value& result_out) {
    // Find a root variable by name (innermost frame locals first, then
    // globals). jdBasic identifiers are case-insensitive (stored upper).
    auto find_root = [&](const std::string& name_in, Value& out) -> bool {
        std::string name = name_in;
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        if (frames.size() > 1) {
            auto& f = frames.back();
            for (uint16_t i = 0; i < f.chunk->name_count(); i++) {
                size_t abs = f.stack_base + i;
                if (abs < sp && f.chunk->name_at(i) == name) { out = stack[abs]; return true; }
            }
        }
        auto it = global_names.find(name);
        if (it != global_names.end() && it->second < globals.size()) {
            out = globals[it->second]; return true;
        }
        return false;
    };
    auto nav_field = [](Value& cur, const std::string& key) -> bool {
        if (cur.type != ValueType::OBJECT) return false;
        auto* o = cur.as_object();
        for (auto& [k, val] : o->fields) if (k == key) { cur = val; return true; }
        for (auto& [k, val] : o->fields) {  // case-insensitive fallback
            if (k.size() == key.size() &&
                std::equal(k.begin(), k.end(), key.begin(), [](char a, char b) {
                    return std::toupper((unsigned char)a) == std::toupper((unsigned char)b); }))
                { cur = val; return true; }
        }
        return false;
    };

    // Try to parse + navigate a simple lvalue path: root ( .field | [idx] | ["key"] )*
    auto resolve_path = [&](const std::string& s, Value& out) -> bool {
        size_t i = 0, n = s.size();
        while (i < n && std::isspace((unsigned char)s[i])) i++;
        if (i >= n || !(std::isalpha((unsigned char)s[i]) || s[i] == '_')) return false;
        size_t st = i;
        while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$')) i++;
        Value cur;
        if (!find_root(s.substr(st, i - st), cur)) return false;
        while (i < n) {
            while (i < n && std::isspace((unsigned char)s[i])) i++;
            if (i >= n) break;
            if (s[i] == '.') {
                i++; size_t fs = i;
                while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$')) i++;
                if (i == fs || !nav_field(cur, s.substr(fs, i - fs))) return false;
            } else if (s[i] == '[') {
                i++; size_t bs = i; int depth = 1;
                while (i < n && depth > 0) {
                    if (s[i] == '[') depth++;
                    else if (s[i] == ']') { depth--; if (depth == 0) break; }
                    i++;
                }
                if (depth != 0) return false;
                std::string inside = s.substr(bs, i - bs); i++;  // skip ']'
                size_t a = 0, b = inside.size();
                while (a < b && std::isspace((unsigned char)inside[a])) a++;
                while (b > a && std::isspace((unsigned char)inside[b - 1])) b--;
                inside = inside.substr(a, b - a);
                if (inside.size() >= 2 && inside.front() == '"' && inside.back() == '"') {
                    if (!nav_field(cur, inside.substr(1, inside.size() - 2))) return false;
                } else {
                    if (cur.type != ValueType::ARRAY) return false;
                    char* end = nullptr;
                    long idx = std::strtol(inside.c_str(), &end, 10);
                    if (end == inside.c_str() || *end != '\0') return false;
                    auto* arr = cur.as_array();
                    if (idx < 0 || (size_t)idx >= arr->elements.size()) return false;
                    cur = arr->elements[idx];
                }
            } else {
                return false;  // operator / something complex: not a simple path
            }
        }
        out = cur; return true;
    };

    Value v;
    bool found = resolve_path(expr, v);
    if (!found && on_eval) {
        // Complex expression: fall back to the global-scope evaluator, but
        // first expose the innermost frame's locals as temporary globals so
        // expressions over locals (n * 10, a + b) resolve. Restore exactly
        // afterwards so no local leaks into the global namespace.
        std::vector<std::pair<uint16_t, Value>> saved;   // pre-existing globals to restore
        std::vector<std::pair<uint16_t, std::string>> created;  // slots/names we added
        if (frames.size() > 1) {
            auto& f = frames.back();
            for (uint16_t i = 0; i < f.chunk->name_count(); i++) {
                size_t abs = f.stack_base + i;
                if (abs >= sp) continue;
                const std::string& nm = f.chunk->name_at(i);
                auto it = global_names.find(nm);
                uint16_t slot;
                if (it != global_names.end()) {
                    slot = it->second;
                    saved.push_back({ slot, globals[slot] });
                } else {
                    slot = (uint16_t)globals.size();
                    globals.push_back(Value::make_none());
                    global_names[nm] = slot;
                    created.push_back({ slot, nm });
                }
                globals[slot] = stack[abs];
            }
        }
        try { v = on_eval(*this, expr); found = true; }
        catch (...) { found = false; }
        for (auto& [slot, old] : saved) globals[slot] = old;
        for (auto& [slot, nm] : created) { globals[slot] = Value::make_none(); global_names.erase(nm); }
    }
    if (!found) return false;
    result_out = v;
    return true;
}





void VM::register_builtins() {
#ifdef PICO
    // The board brings its own family: pins, the LED, whatever the
    // platform layer in embedded/pico/ decides to expose.
    extern void register_pico_builtins(VM&);
    register_pico_builtins(*this);
#endif
#ifdef ESP32
    // The S3 brings its own family: pins, heap figures, whatever the
    // platform layer in embedded/esp32/ decides to expose.
    extern void register_esp32_builtins(VM&);
    register_esp32_builtins(*this);
#endif
    register_math_builtins();
    register_string_builtins();
    register_data_builtins();
    register_array_builtins();
    register_datetime_builtins();
    register_codec_builtins();
    register_console_builtins();
    register_file_builtins();
    register_system_builtins();
    register_async_builtins();
#ifdef JDB_MCU
    // Every builtin there will be is in now; the growth room goes back.
    native_table.shrink_to_fit();
    native_novec.shrink_to_fit();
    {
        std::lock_guard<std::mutex> lk(native_slot_mutex());
        native_slot_names().shrink_to_fit();
        native_slot_sorted().shrink_to_fit();
    }
#endif
}

// ── Event system implementation ─────────────────────────────────

void VM::event_on(const std::string& event_name, const std::string& handler) {
    event_handlers[event_name] = handler;
}

void VM::event_raise(const std::string& event_name, const std::vector<Value>& data) {
    auto it = event_handlers.find(event_name);
    if (it == event_handlers.end()) return;

    // Native-mode bridge: hand the event off to the .exe's dispatcher
    // trampoline, which knows how to build the JdbArray/JdbMap and
    // invoke the LLVM-compiled handler directly.
    if (user_event_dispatch) {
        try {
            user_event_dispatch(event_name, data);
        } catch (const std::exception& e) {
            print_error(ErrCode::RUNTIME_ERROR,
                "Event handler for '" + event_name + "': " + e.what());
        }
        return;
    }

    // Interpreter path: dispatch through the VM's own function table.
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

#ifdef PICO
    // The board's sources: periodic timer, keyboard, pin edges. The
    // platform layer drains what its ISRs collected.
    extern void pico_event_poll(VM& vm);
    pico_event_poll(*this);
    return;
#endif
#ifdef ESP32
    // Same three sources on the S3, drained by the platform layer in embedded/esp32/.
    extern void esp32_event_poll(VM& vm);
    esp32_event_poll(*this);
    return;
#endif

#ifdef GFX
    extern bool gfx_is_active();
    extern bool gfx_has_pending_events();
    extern std::vector<SDL_Event> gfx_drain_pending_events();
#ifdef OPENGL
    // A GL.WINDOW alone (no SCREEN) doesn't create a SDL_Renderer, so
    // gfx_is_active() would return false and the queue would never drain.
    extern bool gl_is_active();
    bool any_window = gfx_is_active() || gl_is_active();
#else
    bool any_window = gfx_is_active();
#endif
    if (any_window) {
        // Drain the shared event queue that SCREENFLIP and gfx_pump_events
        // already populated. We must NOT call SDL_PollEvent here - that
        // would race with SCREENFLIP's loop and whichever runs first eats
        // the events, starving the other. Instead, all SDL_PollEvent sites
        // push into the shared queue and we consume from it.
        if (!gfx_has_pending_events()) return;
        auto events = gfx_drain_pending_events();
        extern SDL_Renderer* g_renderer;
        for (auto& ev : events) {
            // Mouse positions in the logical coordinates GFX.MOUSEX/Y report.
            if (g_renderer && (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                               ev.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                               ev.type == SDL_EVENT_MOUSE_MOTION))
                SDL_ConvertEventToRenderCoordinates(g_renderer, &ev);
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
                        // keycode: SDL_Keycode - ASCII-compatible for printable keys
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
#if defined(__EMSCRIPTEN__)
    // Text-mode games: keys come from the page's key queue, not a SDL window.
    {
        auto kit = event_handlers.find("KEYDOWN");
        if (kit != event_handlers.end()) {
            int code;
            while ((code = jdb_poll_key_js()) >= 0) {
                Value info = Value::make_object();
                info.as_object()->set("scancode", Value::make_i64(code));
                info.as_object()->set("keycode", Value::make_i64(code));
                info.as_object()->set("key", Value::make_string(std::string(1, (char)(code & 0xFF))));
                event_raise("KEYDOWN", {info});
            }
        }
    }
#elif defined(_WIN32)
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
#else
    // POSIX console KEYDOWN: only enter raw mode when the script actually
    // wants keystrokes. Otherwise INPUT/READLINE would lose line discipline.
    auto kit = event_handlers.find("KEYDOWN");
    if (kit != event_handlers.end()) {
        static bool g_posix_raw_set = false;
        static struct termios g_posix_saved_tio;
        if (!g_posix_raw_set) {
            if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_posix_saved_tio) == 0) {
                struct termios raw = g_posix_saved_tio;
                // ICANON off so we see each byte; ECHO off so keys don't
                // leak onto a CLS-drawn screen. Keep ISIG so Ctrl+C still
                // breaks out of the script.
                raw.c_lflag &= ~(ICANON | ECHO);
                raw.c_iflag &= ~(IXON | ICRNL);
                raw.c_cc[VMIN]  = 0;   // read() returns immediately
                raw.c_cc[VTIME] = 0;
                if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                    g_posix_raw_set = true;
                    std::atexit([]() {
                        if (g_posix_raw_set) {
                            tcsetattr(STDIN_FILENO, TCSANOW, &g_posix_saved_tio);
                        }
                    });
                }
            }
        }
        unsigned char buf[16];
        ssize_t n;
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                Value info = Value::make_object();
                info.as_object()->set("scancode", Value::make_i64((int)buf[i]));
                info.as_object()->set("key", Value::make_string(std::string(1, (char)buf[i])));
                event_raise("KEYDOWN", {info});
            }
        }
    }
#endif
}
