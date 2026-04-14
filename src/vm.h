#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

class VM {
public:
    VM();
    ~VM();

    // One-shot: load and run (for file execution)
    void load(Chunk& main_chunk, std::vector<FuncProto>& funcs);
    void run();

    // Incremental: run new code keeping existing state (for console)
    void run_code(Chunk& chunk, std::vector<FuncProto>& new_funcs);

    // State management for workspaces
    VMState save_state() const;
    void restore_state(const VMState& state);
    void reset();
    bool resume();  // continue after STOP

    // Access globals (for serialization)
    const std::vector<Value>& get_globals() const { return globals; }
    const std::unordered_map<std::string, uint16_t>& get_global_names() const { return global_names; }
    void set_global(const std::string& name, Value val);

    // Register a constant (cannot be overwritten by user code)
    void register_const(const std::string& name, Value val);
    bool is_const(const std::string& name) const;

    // Register a native/built-in function
    using NativeFunc = std::function<Value(const std::vector<Value>&)>;
    void register_native(const std::string& name, NativeFunc fn);
    // Register with arity check: min_args, max_args (-1 = unlimited)
    void register_native(const std::string& name, int min_args, int max_args, NativeFunc fn);

    // Callback for EXECUTE/EVAL — set by the host to provide compilation
    using CompileAndRunFunc = std::function<void(VM&, const std::string&)>;
    using CompileAndEvalFunc = std::function<Value(VM&, const std::string&)>;
    CompileAndRunFunc on_execute;
    CompileAndEvalFunc on_eval;

    // Periodic tick during execution (for RECUR tasks)
    std::function<void()> on_tick;
    int tick_counter = 0;

    // Call a function by name (native or user-defined) from within native code
    Value call_function(const std::string& name, const std::vector<Value>& args);
    // Call a funcref value (string or lambda array)
    Value call_funcref(const Value& ref, const std::vector<Value>& args);

    // Apply a binary operator by string name
    Value apply_binary_op(const std::string& op, const Value& a, const Value& b);

    // Debug adapter support
    std::unique_ptr<DebugInfo> debug;
    void debug_check(int line);                    // called in run loop on line change
    int debug_current_line() const;                // current source line
    size_t debug_call_depth() const;               // current call stack depth
    bool debug_goto_line(int target_line);         // move IP to source line
    std::vector<std::pair<int, std::string>> debug_get_stack_frames() const; // line, func_name
    std::vector<std::pair<std::string, std::string>> debug_get_globals() const;  // name, value_str
    std::vector<std::pair<std::string, std::string>> debug_get_locals() const;   // name, value_str

private:
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
    void event_poll(); // called from tick — polls SDL/keyboard events

private:
    // Sub-run depth: >0 means we're inside a nested run_code() (REPL/EXECUTE/EVAL)
    // so HALT must NOT signal program-ended to the DAP client.
    int subrun_depth = 0;

    // Set by HALT (END statement) so nested call_function() invocations
    // — e.g. a SUB that runs `END` while it was triggered from an event
    // handler — can short-circuit instead of trying to pop a return value
    // from the empty stack ("Stack underflow").
public:
    bool is_halted = false;
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

    // Functions (owned)
    std::vector<FuncProto> owned_funcs;
    std::unordered_map<std::string, size_t> func_map;
    std::unordered_map<std::string, NativeFunc> natives;

    // Bumped whenever owned_funcs / func_map changes. Per-chunk inline CALL
    // caches store this value so they can invalidate themselves on reload.
    uint32_t func_map_generation = 1;

    // Backwards compat pointer (used by run() internals)
    std::vector<FuncProto>* func_protos = nullptr;

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
