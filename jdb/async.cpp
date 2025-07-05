/*
------------------------------------------------------------------
File: Types.hpp (Corrected)
------------------------------------------------------------------
- No changes from the previous version. The circular dependency
  is fixed, and BasicValue correctly holds a TaskRef.
*/
#pragma once
#ifdef JDCOM
// COM support
#include <windows.h>
#include <objbase.h>
#include <oaidl.h>
#include <comdef.h>
#endif

#include <variant>
#include <string>
#include <chrono>
#include <vector>
#include <numeric>
#include <stdexcept>
#include <future>
#include <map>
#include "json.hpp"

// Forward-declare the structs so BasicValue can know they exist.
struct Array;
struct Map;
struct JsonObject;
struct Tensor;

// An enum to represent the declared type of a variable.
enum class DataType {
    DEFAULT, BOOL, INTEGER, DOUBLE, STRING, DATETIME, MAP, JSON
};

// A struct to hold our date/time value, based on std::chrono.
struct DateTime {
    std::chrono::system_clock::time_point time_point;
    DateTime() : time_point(std::chrono::system_clock::now()) {}
    DateTime(const std::chrono::system_clock::time_point& tp) : time_point(tp) {}
    bool operator==(const DateTime&) const = default;
};

#ifdef JDCOM
// Structype to hold COM IDispatch pointers
struct ComObject {
    IDispatchPtr  ptr;
    ComObject() : ptr(nullptr) {}
    ComObject(IDispatch* pDisp) : ptr(pDisp) {}
    ComObject(const ComObject& other) : ptr(other.ptr) {}
    ComObject(ComObject&& other) noexcept : ptr(std::move(other.ptr)) {}
    ComObject& operator=(const ComObject& other) { if (this != &other) { ptr = other.ptr; } return *this; }
    ComObject& operator=(ComObject&& other) noexcept { if (this != &other) { ptr = std::move(other.ptr); } return *this; }
    bool operator==(const ComObject& other) const { return ptr == other.ptr; }
    bool operator!=(const ComObject& other) const { return ptr != other.ptr; }
};
#endif

// This struct represents a "pointer" or "reference" to a function.
struct FunctionRef {
    std::string name;
    bool operator==(const FunctionRef&) const = default;
};

struct JsonObject {
    nlohmann::json data;
};

// A lightweight handle to a task, not the full task object.
struct TaskRef {
    int id = -1;
    bool operator==(const TaskRef&) const = default;
};

// --- Use a std::shared_ptr to break the circular dependency ---
#ifdef JDCOM
using BasicValue = std::variant<bool, double, std::string, FunctionRef, int, DateTime, std::shared_ptr<Array>, std::shared_ptr<Map>, std::shared_ptr<JsonObject>, ComObject, std::shared_ptr<Tensor>, TaskRef>;
#else
using BasicValue = std::variant<bool, double, std::string, FunctionRef, int, DateTime, std::shared_ptr<Array>, std::shared_ptr<Map>, std::shared_ptr<JsonObject>, std::shared_ptr<Tensor>, TaskRef>;
#endif

// --- A structure to represent N-dimensional arrays ---
struct Array {
    std::vector<BasicValue> data;
    std::vector<size_t> shape;
    Array() = default;
    size_t size() const { if (shape.empty()) return 0; return std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>()); }
    size_t get_flat_index(const std::vector<size_t>& indices) const {
        if (indices.size() != shape.size()) { throw std::runtime_error("Mismatched number of dimensions for indexing."); }
        size_t flat_index = 0;
        size_t multiplier = 1;
        for (int i = shape.size() - 1; i >= 0; --i) {
            if (indices[i] >= shape[i]) { throw std::out_of_range("Array index out of bounds."); }
            flat_index += indices[i] * multiplier;
            multiplier *= shape[i];
        }
        return flat_index;
    }
    bool operator==(const Array&) const = default;
};

// --- A structure to represent a Map (associative array) ---
struct Map {
    std::map<std::string, BasicValue> data;
};

using GradFunc = std::function<std::vector<std::shared_ptr<Tensor>>(std::shared_ptr<Tensor>)>;

struct FloatArray {
    std::vector<double> data;
    std::vector<size_t> shape;
    size_t size() const { if (shape.empty()) return 0; return std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>()); }
};

struct Tensor {
    std::shared_ptr<FloatArray> data;
    std::shared_ptr<Tensor> grad;
    std::vector<std::shared_ptr<Tensor>> parents;
    GradFunc backward_fn = nullptr;
    bool has_been_processed = false;
};

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================
inline double to_double(const BasicValue& val) {
    if (std::holds_alternative<double>(val)) { return std::get<double>(val); }
    if (std::holds_alternative<int>(val)) { return static_cast<double>(std::get<int>(val)); }
    if (std::holds_alternative<bool>(val)) { return std::get<bool>(val) ? 1.0 : 0.0; }
    if (std::holds_alternative<std::shared_ptr<Array>>(val)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(val);
        if (arr_ptr && arr_ptr->data.size() == 1) { return to_double(arr_ptr->data[0]); }
    }
    return 0.0;
}

inline bool to_bool(const BasicValue& val) {
    if (std::holds_alternative<bool>(val)) { return std::get<bool>(val); }
    if (std::holds_alternative<double>(val)) { return std::get<double>(val) != 0.0; }
    if (std::holds_alternative<std::shared_ptr<Array>>(val)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(val);
        if (arr_ptr && arr_ptr->data.size() == 1) { return to_bool(arr_ptr->data[0]); }
    }
    return false;
}

/*
------------------------------------------------------------------
File: Tokens.hpp (Corrected)
------------------------------------------------------------------
- No changes needed here.
*/
#pragma once
#include <cstdint>

namespace Tokens {
    enum class ID : uint8_t {
        T_EOF = 0, FUNC = 0x01, ENDFUNC = 0x02, LOCAL = 0x03, FOR = 0x04,
        BREAK = 0x05, IF = 0x06, THEN = 0x07, ELSE = 0x08, ENDIF = 0x09,
        AND = 0x0A, OR = 0x0B, NOT = 0x0C, MOD = 0x0D, JD_TRUE = 0x0E,
        JD_FALSE = 0x0F, DO = 0x10, WHILE = 0x11, LOOP = 0x12, UNTIL = 0x13,
        DIM = 0x14, AS = 0x15, LABEL = 0x16, GOTO = 0x17, PRINT = 0x18,
        C_CR = 0x19, C_COMMA = 0x1A, C_SEMICOLON = 0x1B, C_PLUS = 0x1C,
        C_MINUS = 0x1D, C_OR = 0x1E, C_ASTR = 0x1F, C_SLASH = 0x20,
        C_MOD = 0x21, C_LEFTPAREN = 0x22, C_LEFTBRACKET = 0x23,
        C_RIGHTBRACKET = 0x24, C_HASH = 0x25, C_RIGHTPAREN = 0x26,
        C_LT = 0x27, C_GT = 0x28, C_EQ = 0x29, C_DOLLAR = 0x2A,
        RETURN = 0x2B, NUMBER = 0x2C, STRING = 0x2D, FAST = 0x2E,
        BYTE = 0x2F, INT = 0x30, LONG = 0x31, FLOAT = 0x32, DOUBLE = 0x33,
        ARRAY_ACCESS = 0x34, STRVAR = 0x35, C_APOSTROPHE = 0x36,
        C_NE = 0x37, C_LE = 0x38, C_GE = 0x39, C_COLON = 0x3A,
        CONSTANT = 0x3B, MAP = 0x3C, BOOL = 0x3D, DATE = 0x3E,
        STRTYPE = 0x3F, MODULE = 0x40, GOFUNC = 0x41, F_INC = 0x42,
        TO = 0x43, VARIANT = 0x44, NEXT = 0x45, STEP = 0x46,
        CALLFUNC = 0x47, EXPORT = 0x48, IMPORT = 0x49, JSON = 0x4A,
        TYPE = 0x4B, ENDTYPE = 0x4C, INPUT = 0x4D, STOP = 0x50,
        RESUME = 0x51, C_LEFTBRACE = 0x52, C_RIGHTBRACE = 0x53,
        MAP_ACCESS = 0x54, ONERRORCALL = 0x55, C_DOT = 0x56,
        C_CARET = 0x57, EXIT_FOR = 0x58, EXIT_DO = 0x59, TENSOR = 0x5A,
        XOR = 0x5B,
        AWAIT = 0x5C,
        ASYNC = 0x5D,
        OP_START_TASK = 0x5E,
        OP_AWAIT_TASK = 0x5F,
        GET = 0xBA, WAIT = 0xD1, BLOAD = 0xD2, BSAVE = 0xD4,
        FUNCREF = 0xE3, REM = 0xE4, SUB = 0xE5, ENDSUB = 0xE6,
        CALLSUB = 0xE7, LIST = 0x70, RUN = 0x71, EDIT = 0x72,
        DIR = 0x73, LOAD = 0x74, TRON = 0x76, TROFF = 0x77,
        DUMP = 0x78, SAVE = 0x79, COMPILE = 0x7A, WHITE = 0x7D,
        TOKEN = 0x7E, NOCMD = 0x7F
    };
}

/*
------------------------------------------------------------------
File: NeReLaBasic.hpp (Corrected)
------------------------------------------------------------------
- Renamed execute() to execute_main_program().
- Added new declarations for execute_synchronous_block() and
  execute_synchronous_function().
- execute_function_for_value() is now a simple wrapper.
*/
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <stack>
#include "Types.hpp"
#include "Tokens.hpp"
#include "NetworkManager.hpp"
#include <functional>
#include <future>
#ifdef SDL3
#include "Graphics.hpp"
#include "SoundSystem.hpp"
#endif

class NetworkManager;
class DAPHandler;
class Compiler;

// Enum for the status of an asynchronous task
enum class TaskStatus {
    RUNNING,
    PAUSED_ON_AWAIT,
    COMPLETED,
    ERRORED
};

class NeReLaBasic {
public:
    // --- Nested Types for Execution Machinery ---
    struct FunctionInfo;

    struct ForLoopInfo {
        std::string variable_name;
        double end_value = 0;
        double step_value = 0;
        uint16_t loop_start_pcode = 0;
        std::vector<uint16_t> exit_patch_locations;
    };

    struct StackFrame {
        std::string function_name;
        uint16_t linenr;
        std::unordered_map<std::string, BasicValue> local_variables;
        uint16_t return_pcode = 0;
        const std::vector<uint8_t>* return_p_code_ptr;
        std::unordered_map<std::string, FunctionInfo>* previous_function_table_ptr;
        size_t for_stack_size_on_entry;
    };

    using NativeFunction = std::function<BasicValue(NeReLaBasic&, const std::vector<BasicValue>&)>;

    struct FunctionInfo {
        std::string name;
        int arity = 0;
        bool is_procedure = false;
        bool is_exported = false;
        bool is_async = false;
        std::string module_name;
        uint16_t start_pcode = 0;
        std::vector<std::string> parameter_names;
        NativeFunction native_impl = nullptr;
    };

    using FunctionTable = std::unordered_map<std::string, FunctionInfo>;

    struct Task {
        int id;
        TaskStatus status = TaskStatus::RUNNING;
        BasicValue result = false;
        std::shared_ptr<Task> awaiting_task = nullptr;
        const std::vector<uint8_t>* p_code_ptr = nullptr;
        uint16_t p_code_counter = 0;
        std::vector<StackFrame> call_stack;
        std::vector<ForLoopInfo> for_stack;
        bool yielded_execution = false;
    };

    // --- Member Variables ---
    // ... (existing variables)
    std::map<int, std::shared_ptr<Task>> task_queue;
    int next_task_id = 0;
    Task* current_task = nullptr;

    // --- Member Functions ---
    NeReLaBasic();
    ~NeReLaBasic();
    void start();
    void execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    void execute_synchronous_block(const std::vector<uint8_t>& code_to_run);
    BasicValue execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    BasicValue execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    // ... (rest of the class definition)
};

/*
------------------------------------------------------------------
File: Commands.cpp (Corrected)
------------------------------------------------------------------
- Added the missing case for TaskRef in the to_string function.
- do_run now calls execute_main_program.
- do_stop now calls execute_synchronous_block.
*/
// ... (includes)
std::string to_string(const BasicValue& val) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, TaskRef>) {
            return "<Task ID: " + std::to_string(arg.id) + ">";
        }
        // ... (rest of to_string implementation)
        return ""; // Fallback
    }, val);
}

void Commands::do_run(NeReLaBasic& vm) {
    // ... (compilation logic is the same)
    vm.execute_main_program(vm.program_p_code, false);
    // ... (error printing)
}

void Commands::do_stop(NeReLaBasic& vm) {
    // ... (REPL setup)
    while (paused) {
        // ...
        // Execute the single command from the direct_p_code buffer.
        vm.execute_synchronous_block(vm.direct_p_code);
        // ... (rest of the loop)
    }
}
// ... (rest of Commands.cpp)

/*
------------------------------------------------------------------
File: NeReLaBasic.cpp (Corrected)
------------------------------------------------------------------
- Renamed old execute() to execute_main_program().
- Added new execute_synchronous_block() for REPL.
- Added new execute_synchronous_function() for function calls.
- Updated parse_primary to use the new synchronous function executor.
*/
// ... (includes)

// Wrapper for synchronous function calls from the expression parser
BasicValue NeReLaBasic::execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    if (func_info.native_impl != nullptr) {
        return func_info.native_impl(*this, args);
    }
    return execute_synchronous_function(func_info, args);
}

// New synchronous executor for REPL and direct commands
void NeReLaBasic::execute_synchronous_block(const std::vector<uint8_t>& code_to_run) {
    if (code_to_run.empty()) return;

    auto prev_active_p_code = active_p_code;
    auto prev_pcode = pcode;
    active_p_code = &code_to_run;
    pcode = 0;

    while (pcode < active_p_code->size()) {
        if (pcode == 0) pcode += 2; // Skip line number
        Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (token == Tokens::ID::NOCMD || token == Tokens::ID::C_CR) break;
        statement();
        if (Error::get() != 0) break;
        if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_COLON) {
            pcode++;
        }
    }

    active_p_code = prev_active_p_code;
    pcode = prev_pcode;
}

// New synchronous executor for user-defined functions
BasicValue NeReLaBasic::execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    size_t initial_stack_depth = call_stack.size();

    StackFrame frame;
    // ... (populate frame with args, return address, etc.)
    call_stack.push_back(frame);

    // Context switch
    auto prev_active_code = this->active_p_code;
    auto prev_pcode = this->pcode;
    // ... (set active_p_code and pcode to function's start)

    while (call_stack.size() > initial_stack_depth && !is_stopped) {
        if (Error::get() != 0) break;
        statement();
    }

    // Context restore
    this->active_p_code = prev_active_code;
    this->pcode = prev_pcode;

    return variables["RETVAL"];
}

// Renamed from execute() to execute_main_program()
void NeReLaBasic::execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode) {
    // ... (the entire task scheduler implementation from the previous step goes here)
}

// Updated REPL start function
void NeReLaBasic::start() {
    // ... (init messages)
    while (true) {
        // ... (get input line)
        // Tokenize the direct-mode line
        if (compiler->tokenize(*this, inputLine, 0, direct_p_code, *active_function_table) != 0) {
            Error::print();
            continue;
        }
        // Execute the direct-mode p_code synchronously
        execute_synchronous_block(direct_p_code);

        if (Error::get() != 0) {
            Error::print();
        }
    }
}

// Updated expression parser
BasicValue NeReLaBasic::parse_primary() {
    // ... (logic from previous step, but the CALLFUNC case now uses execute_function_for_value)
    if (token == Tokens::ID::CALLFUNC) {
        // ...
        if (active_function_table->count(real_func_to_call)) {
            // ... (argument parsing)
            // *** This now correctly calls the synchronous executor ***
            current_value = execute_function_for_value(func_info, args);
        }
        // ...
    }
    // ... (rest of parse_primary)
    return current_value;
}

/*
------------------------------------------------------------------
File: NeReLaBasicInterpreter.cpp (Corrected)
------------------------------------------------------------------
- The main entry point now correctly calls do_run, which triggers
  the new task-based scheduler for full programs.
*/
// ... (includes)
int main(int argc, char* argv[]) {
    // ... (COM init, DAP check)
    if (dap_mode) {
        // ... (DAP logic)
    } else {
        if (argc > 1) {
            std::string filename = argv[1];
            if (interpreter.loadSourceFromFile(filename)) {
                // This now correctly calls the version of do_run that
                // will use the new task-based execute_main_program.
                Commands::do_run(interpreter);
                // ...
            }
        } else {
            // This starts the REPL, which uses the synchronous executor.
            interpreter.start();
        }
    }
    // ... (COM uninit)
    return 0;
}
