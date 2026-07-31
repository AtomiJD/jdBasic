#pragma once
#include <vector>
#include <deque>
#include <string>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <functional>
#include <memory>
#include "bytecode.h"
#include "value.h"

struct DebugInfo; // forward

struct CallFrame {
    const Chunk* chunk;
    size_t ip;
    size_t stack_base;   // base of this frame on the value stack
};

struct TryHandler {
    size_t catch_addr;       // absolute IP to jump to on error
    size_t saved_sp;         // stack pointer to restore
    size_t saved_frame_count;// frame count to restore
};

// Snapshot of VM state for workspace save/restore
struct VMState {
    std::vector<Value> globals;
    std::unordered_map<std::string, uint16_t> global_names;
    std::vector<FuncProto> functions;
    std::unordered_map<std::string, size_t> func_map;
};

// Process-global native-slot registry: a stable name->index assignment shared
// by all VMs, so the compiler can bake CALL_NATIVE slot indices that stay valid
// in any VM (each VM fills its own native_table at those slots). Returns -1 if
// `name` is not a registered native.
int jdb_native_slot(const std::string& name);
const std::string& jdb_native_name(int slot);

class VM {
public:
    VM();
    ~VM();

    // One-shot: load and run (for file execution)
    void load(Chunk& main_chunk, std::vector<FuncProto>& funcs);
    void run();

    // Incremental: run new code keeping existing state (for console)
    void run_code(Chunk& chunk, std::vector<FuncProto>& new_funcs);

    // Merge new function definitions into the live VM without running
    // anything. Same merge rules as run_code's prelude - same name
    // overwrites, new name appends, func_map_generation bumps so any
    // cached lookups invalidate. Returns {added, updated} for the caller
    // to surface in a recompile summary. Safe to call while the VM is
    // STOPped (the existing stopped_frames keep working - they dispatch
    // through func_map on the next CALL opcode and pick up the new bodies).
    std::pair<size_t, size_t> merge_funcs(std::vector<FuncProto>& new_funcs);

    // Builtins always win at call dispatch, so a user FUNC/SUB carrying a
    // builtin's name could never be reached - reject it at load/merge time.
    void reject_builtin_collision(const FuncProto& f) const;

    // State management for workspaces
    VMState save_state() const;
    void restore_state(const VMState& state);
    void reset();
    bool resume();  // continue after STOP
    bool is_paused() const { return is_stopped; }  // true between STOP and RESUME

    // Access globals (for serialization)
    const std::vector<Value>& get_globals() const { return globals; }
    const std::unordered_map<std::string, uint16_t>& get_global_names() const { return global_names; }
    const std::deque<FuncProto>& get_funcs() const { return owned_funcs; }
    void set_global(const std::string& name, Value val);

    // Register a constant (cannot be overwritten by user code)
    void register_const(const std::string& name, Value val);
    bool is_const(const std::string& name) const;
    // Names of registered constants (PI, E, TRUE, ...) for symbol dumps.
    const std::unordered_set<std::string>& const_names() const { return const_globals; }

    // Register a native/built-in function
    using NativeFunc = std::function<Value(const std::vector<Value>&)>;
    void register_native(const std::string& name, NativeFunc fn);
    // Register with arity check: min_args, max_args (-1 = unlimited)
    void register_native(const std::string& name, int min_args, int max_args, NativeFunc fn);

    // Per-VM no-vectorize extension. Names added here join the static
    // jdb_no_vectorize() set: a call to one of them will pass through
    // unmodified even if some of its args are arrays. Embed hosts
    // register their natives this way - "GDX.SET(node, prop, [r,g,b,a])"
    // should call the bridge once, not four times per colour channel.
    std::unordered_set<std::string> extra_no_vectorize;

    // Callback for EXECUTE/EVAL - set by the host to provide compilation
    using CompileAndRunFunc = std::function<void(VM&, const std::string&)>;
    using CompileAndEvalFunc = std::function<Value(VM&, const std::string&)>;
    using ParseCheckFunc = std::function<std::string(VM&, const std::string&)>;
    CompileAndRunFunc on_execute;
    CompileAndEvalFunc on_eval;
    // Lex + Parse only; returns "" on success, an error message otherwise.
    // Lets tools (e.g. the MCP server's jdb_check) validate code without
    // contaminating the persistent VM.
    ParseCheckFunc on_check;

    // Output callback - if set, all VM output goes here instead of std::cout.
    // The host (Console) sets this to route output to the workspace buffer.
    std::function<void(const std::string&)> on_output;

    // Helper: write to on_output if set, else std::cout
    void emit(const std::string& s) {
        if (on_output) on_output(s);
        else std::cout << s;
    }

    // OUTPUT.CAPTURE_BEGIN/END$/PEEK$ stack - saves the previous on_output
    // handler when a capture starts so that nested captures and Console-host
    // routing both restore correctly.
    std::vector<std::function<void(const std::string&)>> output_capture_prev;
    std::vector<std::shared_ptr<std::string>> output_capture_buffers;

    // Periodic tick during execution (for RECUR tasks)
    std::function<void()> on_tick;
    int tick_counter = 0;

    // Native-mode event dispatch hook. The bridge installs this so
    // that ON-handlers, whose bodies live as LLVM-IR in the .exe,
    // get reached through a runtime-side trampoline. When set,
    // event_raise calls this instead of vm.call_function.
    std::function<void(const std::string&, const std::vector<Value>&)>
        user_event_dispatch;

    // Call a function by name (native or user-defined) from within native code
    Value call_function(const std::string& name, const std::vector<Value>& args);
    // Call a funcref value (string or lambda array)
    Value call_funcref(const Value& ref, const std::vector<Value>& args);
    // Invoke an already-resolved native: direct call, or element-wise
    // auto-vectorisation when an arg is an array and no_vec is false.
    Value invoke_native(const NativeFunc& fn, const std::vector<Value>& args, bool no_vec);

    // Apply a binary operator by string name
    Value apply_binary_op(const std::string& op, const Value& a, const Value& b);

    // Debug adapter support
    std::unique_ptr<DebugInfo> debug;
    void debug_check(int line);                    // called in run loop on line change
    int debug_current_line() const;                // current source line
    std::string debug_current_file() const;        // current source file path
    size_t debug_call_depth() const;               // current call stack depth
    bool debug_goto_line(int target_line);         // move IP to source line
    // Hot-reload: swap the running main chunk for a freshly compiled one,
    // merge the new function bodies (skipping any currently on the call
    // stack), and reposition the main frame's IP to target_line. Called
    // from the DAP thread while the VM is parked at a pause. Returns false
    // only when there is no frame to reposition.
    bool debug_reload_main(Chunk& new_main, std::vector<FuncProto>& new_funcs, int target_line);
    // Returns {line, func_name, source_file} per stack frame
    struct DebugFrame { int line; std::string name; std::string file; };
    std::vector<DebugFrame> debug_get_stack_frames() const;
    std::vector<std::pair<std::string, std::string>> debug_get_globals() const;  // name, value_str
    std::vector<std::pair<std::string, std::string>> debug_get_locals() const;   // innermost frame
    std::vector<std::pair<std::string, std::string>> debug_get_locals_at(int level) const; // 0 = innermost

    // Structured variable inspection for the DAP (expandable arrays/maps/UDTs).
    // ref 0 = leaf; ref > 0 is a handle the client passes back to
    // debug_var_children to drill in. Handles hold a copy of the Value (a
    // cheap refcount bump for heap objects) so navigation needs no re-resolve
    // and sees frame locals directly. Cleared on every pause.
    struct DebugVar {
        std::string name;       // display name ("ARR", "[2]", "field")
        std::string value;      // to_string() of the value
        std::string eval_name;  // full evaluatable path (for watch / copy)
        int ref = 0;            // 0 = leaf, > 0 = expandable handle
    };
    std::vector<DebugVar> debug_vars_global();
    std::vector<DebugVar> debug_vars_local(int frame_index);  // vm frame index (1..n)
    std::vector<DebugVar> debug_var_children(int ref);
    void debug_clear_var_handles();

    // Resolve a hover/watch expression: navigates real Values (so it sees the
    // innermost frame's locals) for plain names, array indexing and field/key
    // access; falls back to the global-scope evaluator otherwise. Returns
    // {to_string(result), expandable-handle} (handle 0 = leaf/none/error).
    std::pair<std::string, int> debug_eval_watch(const std::string& expr);

    // Evaluate an expression to a Value in the current debug context (locals
    // of the innermost frame are visible). Returns false if it can't be
    // resolved/evaluated. Used by hover/watch and conditional breakpoints.
    bool debug_eval_value(const std::string& expr, Value& out);

private:
    // Handle table for debug_var_* (one entry per expandable value handed to
    // the client this pause). Stores a Value copy + the evaluatable path.
    struct VarHandleEntry { Value value; std::string eval_name; };
    std::map<int, VarHandleEntry> debug_var_handles_;
    int debug_next_var_handle_ = 1;
    int debug_register_var(const Value& v, const std::string& eval_name);

    // Execution state
    size_t min_frame_depth = 0;  // for nested call_function
    std::vector<CallFrame> frames;
    std::vector<Value> stack;
    size_t sp = 0;

    // Exception handling
    std::vector<TryHandler> try_handlers;

public:
    // Tracing
    bool trace_enabled = false;

    // Reactive system
    struct ReactiveBinding {
        std::string formula;                    // source formula for DUMP
        std::string func_name;                  // compiled function name
        std::vector<std::string> dependencies;  // variables this reads
    };
    std::unordered_map<std::string, ReactiveBinding> reactive_bindings;
    bool reactive_updating = false;
    std::vector<std::string> reactive_pending; // vars that changed, pending propagation
    void propagate_reactive(const std::string& changed_var);
    void bind_reactive(const std::string& var, const std::string& formula,
                       const std::string& func_name, const std::vector<std::string>& deps);

    // Event system
    std::unordered_map<std::string, std::string> event_handlers; // event_name -> func_name
    void event_on(const std::string& event_name, const std::string& handler);
    void event_raise(const std::string& event_name, const std::vector<Value>& data);
    void event_poll(); // called from tick - polls SDL/keyboard events

    bool is_native(const std::string& name) const { return natives.count(name) > 0; }
    bool function_exists(const std::string& name) const {
        return natives.count(name) > 0 || func_map.count(name) > 0;
    }
    std::vector<std::string> native_names() const {
        std::vector<std::string> out;
        out.reserve(natives.size());
        for (auto& kv : natives) out.push_back(kv.first);
        return out;
    }

private:
    // Save the previous "active VM" pointer so nested VMs (REPL/EXECUTE)
    // can restore it on destruction. Stored as void* to avoid leaking the
    // implementation detail into the public API.
    void* prev_active_vm_ = nullptr;

    // Sub-run depth: >0 means we're inside a nested run_code() (REPL/EXECUTE/EVAL)
    // so HALT must NOT signal program-ended to the DAP client.
    int subrun_depth = 0;

    // Set by HALT (END statement) so nested call_function() invocations
    // - e.g. a SUB that runs `END` while it was triggered from an event
    // handler - can short-circuit instead of trying to pop a return value
    // from the empty stack ("Stack underflow").
public:
    bool is_halted = false;
    std::atomic<bool> is_waiting_input{false};  // true while blocking on INPUT/std::cin
    // External STOP request - set from another thread (e.g. the MCP reader
    // thread when the client sends a jdb_stop tool call). The VM's dispatch
    // tick polls this every ~200 opcodes; on true, it stashes state exactly
    // like the in-script STOP statement and returns from run().
    std::atomic<bool> stop_requested{false};
private:

    // STOP/RESUME state
    bool is_stopped = false;
    Chunk stopped_chunk;
    std::vector<CallFrame> stopped_frames;
    std::vector<Value> stopped_stack;
    size_t stopped_sp = 0;

    // Locals from the stopped function frame, exposed as temporary globals
    // so the user can `PRINT a` from the console while STOP'd.
    struct InjectedLocal {
        std::string name;
        size_t slot_in_stopped_stack;
        uint16_t global_slot;
        bool was_existing;
        Value overridden_value;
    };
    std::vector<InjectedLocal> injected_locals;
    void inject_stopped_locals();
    void extract_stopped_locals();

    // Globals
    std::vector<Value> globals;
    std::unordered_map<std::string, uint16_t> global_names;
    std::unordered_set<std::string> const_globals;  // protected constant names (uppercase)

    // Functions (owned).
    // std::deque (not vector) so push_back never invalidates pointers/refs
    // to existing FuncProtos. Frames hold pointers to chunks living inside
    // these FuncProtos; a vector reallocation during a re-entrant EXECUTE
    // (HTTP-handler thread evaluates user code that adds funcs) would dangle
    // those pointers and the next opcode read crashes with garbage data.
    std::deque<FuncProto> owned_funcs;
    std::unordered_map<std::string, size_t> func_map;
    std::unordered_map<std::string, NativeFunc> natives;
    // Per-VM native dispatch tables indexed by the *global* native slot
    // (jdb_native_slot). CALL_NATIVE indexes these directly, skipping the
    // name hash. native_novec is computed lazily on first call (-1 = unknown).
    std::vector<NativeFunc> native_table;
    std::vector<int8_t>     native_novec;
    // Reusable per-depth argument buffers for native calls, so a hot loop of
    // CALL_NATIVE doesn't heap-allocate a std::vector every call. A deque keeps
    // element references stable when a re-entrant native (one that calls back
    // into the VM) grows the pool at a deeper level.
    std::deque<std::vector<Value>> m_arg_pool;
    size_t                         m_arg_depth = 0;

    // Bumped whenever owned_funcs / func_map changes. Per-chunk inline CALL
    // caches store this value so they can invalidate themselves on reload.
    uint32_t func_map_generation = 1;

    // Backwards compat pointer (used by run() internals)
    std::deque<FuncProto>* func_protos = nullptr;

    // Stack operations
    void push(Value v);
    Value pop();
    Value& peek(size_t offset = 0);

    // Current frame helpers
    CallFrame& frame();
    uint8_t read_byte();
    uint16_t read_u16();
    int16_t read_i16();

    // Arithmetic helpers
    Value arithmetic(const Value& a, const Value& b, OpCode op);
    Value array_arithmetic(const Value& a, const Value& b, OpCode op);
    Value scalar_binop(const Value& a, const Value& b, OpCode op);
    Value compare(const Value& a, const Value& b, OpCode op);
    Value cast_value(const Value& v, ValueType target);

    // Global variable management
    uint16_t ensure_global(uint16_t name_idx);

    void register_builtins();
};
