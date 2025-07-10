// sqlitefunc.cpp
#include "sqlitefunc.h"
#include "NeReLaBasic.hpp"
#include "Commands.hpp"
#include "Types.hpp"
#include "Error.hpp"
#include "sqlite3.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

// --- Global pointers to hold the functions provided by the main app ---
namespace {
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
    ToStringFunc g_to_string = nullptr;
    std::string g_last_error_message;
}

// === BUILT-IN FUNCTION IMPLEMENTATIONS ===

// SQLITE.OPEN(filename$) -> OpaqueHandle
void builtin_sqlite_open(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, "SQLITE.OPEN requires 1 argument: filename$");
        *out_result = false; // Set a default error value
        return;
    }

    std::string db_path = g_to_string(args[0]);
    sqlite3* db_ptr;

    if (sqlite3_open(db_path.c_str(), &db_ptr) != SQLITE_OK) {
        g_last_error_message = sqlite3_errmsg(db_ptr);
        g_error_set(12, vm.runtime_current_line, "Can't open database: " + g_last_error_message);
        sqlite3_close(db_ptr);
        *out_result = false; // Set a default error value
        return;
    }

    auto sqlite_deleter = [](void* p) {
        if (p) {
            sqlite3_close(static_cast<sqlite3*>(p));
        }
        };

    auto handle = std::make_shared<OpaqueHandle>(
        static_cast<void*>(db_ptr),
        "SQLITEDB",
        sqlite_deleter
    );

    // Assign the new handle directly to the output parameter
    *out_result = handle;
}

// SQLITE.CLOSE(handle)
void builtin_sqlite_close(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, "SQLITE.CLOSE requires 1 argument: db_handle");
        *out_result = false;
        return;
    }

    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "Argument must be a valid SQLiteDB Handle.");
        *out_result = false;
        return;
    }

    auto& handle_ptr = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (handle_ptr && handle_ptr->ptr) {
        sqlite3* db = static_cast<sqlite3*>(handle_ptr->ptr);
        sqlite3_close(db);
        handle_ptr->ptr = nullptr; // Nullify the pointer to prevent reuse
    }
    *out_result = true;
    return ; // Return true for success
}


// SQLITE.EXEC(handle, sql$) -> boolean
void builtin_sqlite_exec(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 2) { *out_result = false; return; }

    // --- 1. Get the Opaque Handle ---
    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "First argument must be a Handle.");
        if (args.size() != 2) { *out_result = false; return; }
        *out_result = false;
        return;
    }
    const auto& handle_ptr = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);

    // --- 2. Validate the Handle ---
    if (!handle_ptr || !handle_ptr->ptr || handle_ptr->type_name != "SQLITEDB") {
        g_error_set(15, vm.runtime_current_line, "Argument is not a valid SQLiteDB Handle.");
        if (args.size() != 2) { *out_result = false; return; }
        *out_result = false;
        return;
    }

    // --- 3. Safely cast the pointer back to its real type ---
    sqlite3* db = static_cast<sqlite3*>(handle_ptr->ptr);
    std::string sql = g_to_string(args[1]);
    char* zErrMsg = nullptr;

    // --- 4. Use the pointer as normal ---
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, 0, &zErrMsg);

    if (rc != SQLITE_OK) {
        /* error handling */
        sqlite3_free(zErrMsg);
        *out_result = false;
        return;
    }
    *out_result = true;
    return;
}

// This is the C-style callback function required by sqlite3_exec for queries.
static int query_callback(void* user_data, int argc, char** argv, char** azColName) {
    // Cast the user_data pointer back to our vector of maps.
    auto* results = static_cast<std::vector<std::shared_ptr<Map>>*>(user_data);

    // Create a new map for the current row.
    auto row_map = std::make_shared<Map>();

    for (int i = 0; i < argc; i++) {
        std::string col_name = azColName[i];
        // SQLite values are text by default; try to convert to number, otherwise keep as string.
        BasicValue value;
        try {
            if (argv[i]) {
                value = std::stod(argv[i]);
            }
            else {
                value = 0.0; // Represent NULL as 0.0
            }
        }
        catch (const std::exception&) {
            value = argv[i] ? std::string(argv[i]) : std::string("");
        }
        row_map->data[col_name] = value;
    }

    // Add the completed row map to our results vector.
    results->push_back(row_map);
    return 0;
}


// SQLITE.QUERY(handle, sql$) -> Array of Maps
void builtin_sqlite_query(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 2) {
        g_error_set(8, vm.runtime_current_line, "SQLITE.QUERY requires 2 arguments: db_handle, sql$");
        *out_result = {};
        return;
    }
    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "First argument must be a valid SQLiteDB Handle.");
        *out_result = {};
        return;
    }

    auto& handle_ptr = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!handle_ptr || !handle_ptr->ptr || handle_ptr->type_name != "SQLITEDB") {
        g_error_set(1, vm.runtime_current_line, "Argument is not a valid SQLiteDB Handle.");
        *out_result = {};
        return;
    }

    sqlite3* db = static_cast<sqlite3*>(handle_ptr->ptr);
    std::string sql = g_to_string(args[1]);
    char* zErrMsg = nullptr;

    // This vector will be populated by our callback function.
    std::vector<std::shared_ptr<Map>> results_vector;

    int rc = sqlite3_exec(db, sql.c_str(), query_callback, &results_vector, &zErrMsg);

    if (rc != SQLITE_OK) {
        g_last_error_message = zErrMsg;
        g_error_set(1, vm.runtime_current_line, "SQL query error: " + g_last_error_message);
        sqlite3_free(zErrMsg);
        *out_result = {};
        return;
    }

    // Convert our C++ vector of maps into a jdBasic Array
    auto result_array = std::make_shared<Array>();
    result_array->shape = { results_vector.size() };
    for (const auto& map_ptr : results_vector) {
        result_array->data.push_back(map_ptr);
    }
    *out_result = result_array;
    return;
}

// SQLITE.ERROR$() -> string$
void builtin_sqlite_error(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (!args.empty()) {
        g_error_set(8, vm.runtime_current_line, "SQLITE.ERROR$ does not take any arguments.");
        *out_result = "";
        return;
    }
    *out_result = g_last_error_message;
    return;
}


// === REGISTRATION LOGIC ===

void register_sqlite_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
    auto register_func = [&](const std::string& name, int arity, NativeDLLFunction func_ptr, bool is_proc = false) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_dll_impl = func_ptr;
        info.is_procedure = is_proc;
        table[g_to_upper(name)] = info;
        };

    register_func("SQLITE.OPEN", 1, builtin_sqlite_open);
    register_func("SQLITE.CLOSE", 1, builtin_sqlite_close, true); // This is a procedure
    register_func("SQLITE.EXEC", 2, builtin_sqlite_exec);
    register_func("SQLITE.QUERY", 2, builtin_sqlite_query);
    register_func("SQLITE.ERROR$", 0, builtin_sqlite_error);
}


// The main entry point of the DLL.
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }

    // Store the callback function pointers
    g_error_set = services->error_set;
    g_to_upper = services->to_upper;
    g_to_string = services->to_string;
    // Register all our new functions with the interpreter
    register_sqlite_functions(*vm, vm->main_function_table);
}