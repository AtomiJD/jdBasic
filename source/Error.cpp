// Error.cpp
#include "Error.hpp"
#include "TextIO.hpp" // We need this to print the error messages.
#include "NeReLaBasic.hpp"
#include <vector>
#include <string> // Required for std::string

extern NeReLaBasic* g_vm_instance_ptr; // Initialize to nullptr

namespace {
    // These variables hold the current error state.
    // They are in an anonymous namespace, making them accessible only within this file.
    uint8_t current_error_code = 0;
    uint16_t error_line_number = 0;
    std::string custom_error_message = ""; // NEW: For custom error messages.

    // A table of error messages. We can expand this as we go.
    // Using a vector of strings makes it easy to manage.
    const std::vector<std::string> errorMessages = {
            "OK",                               // 0
            "Syntax Error",                     // 1
            "Calculation Error",                // 2
            "Variable not found",               // 3
            "Unclosed IF/ENDIF",                // 4
            "Unclosed FUNC/ENDFUNC",            // 5
            "File not found",                   // 6
            "Function/Sub name not found",      // 7
            "Wrong number of arguments",        // 8
            "RETURN without GOSUB/CALL",        // 9
            "Array out of bounds",              // 10
            "Undefined label",                  // 11
            "File I/O Error",                   // 12
            "Invalid token in expression",      // 13
            "Unclosed loop",                    // 14
            "Type Mismatch",                    // 15
            "Syntax Error, ] missing",          // 16
            "Syntax Error, } missing",          // 17
            "Syntax Error, ) missing",          // 18
            "Syntax Error, , missing",          // 19
            "Reserved 20",                      // 20
            "NEXT without FOR",                 // 21
            "Undefined function",               // 22
            "RETURN without function call",     // 23
            "Bad array subscript",              // 24
            "Function or Sub is missing RETURN or END", // 25
            "Incorrect number of arguments"     // 26
    };
}

void Error::set(uint8_t errorCode, uint16_t lineNumber, const std::string& customMessage) {
    if (g_vm_instance_ptr == nullptr || g_vm_instance_ptr->current_task == nullptr) {
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage;
        return;
    }

    NeReLaBasic* vm = g_vm_instance_ptr;
    NeReLaBasic::Task* task = vm->current_task;

    if (task->error_handler_active && vm->is_processing_event) {
        // A second error occurred inside the error handler itself. This is fatal.
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage;
        task->status = TaskStatus::ERRORED;
        return;
    }

    // Check if an error handler is defined for this task.
    if (task->error_handler_active && !task->error_handler_function_name.empty()) {
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage;

        task->jump_to_error_handler = true;

        // Save execution context
        task->resume_pcode = vm->current_statement_start_pcode;
        task->resume_runtime_line = lineNumber;
        task->resume_p_code_ptr = vm->active_p_code;
        task->resume_function_table_ptr = vm->active_function_table;
        task->resume_call_stack_snapshot = vm->call_stack;
        task->resume_for_stack_snapshot = vm->for_stack;

        std::string full_message = getMessage(errorCode) + (customMessage.empty() ? "" : ", " + customMessage);

        // --- NEW: Create and store the structured Map for the argument ---
        auto error_data_map = std::make_shared<Map>();
        error_data_map->data["CODE"] = static_cast<double>(errorCode);
        error_data_map->data["LINE"] = static_cast<double>(lineNumber);
        error_data_map->data["MESSAGE"] = full_message;
        vm->current_error_data = error_data_map;

        // Set the global error variables for the handler to access
        vm->variables["ERR"] = static_cast<double>(errorCode);
        vm->variables["ERL"] = static_cast<double>(lineNumber);
        vm->variables["ERRMSG$"] = getMessage(errorCode) + (customMessage.empty() ? "" : ", " + customMessage);
        vm->variables["STACK$"] = vm->get_stacktrace();

        // Calculate where RESUME NEXT should jump to.
        uint16_t next_pcode = vm->pcode;
        while (next_pcode < vm->active_p_code->size()) {
            if (static_cast<Tokens::ID>((*vm->active_p_code)[next_pcode]) == Tokens::ID::C_CR) {
                next_pcode+=3; // Move past C_CR to the next line's metadata
                break;
            }
            next_pcode++;
        }
        task->resume_pcode_next_statement = next_pcode;

    }
    else {
        // This is an untrappable error (no handler set).
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage;
    }
}

uint8_t Error::get() {
    return current_error_code;
}

void Error::clear() {
    current_error_code = 0;
    error_line_number = 0;
    custom_error_message.clear(); // Clear the custom message as well
}

std::string Error::getMessage(uint8_t errorCode) {
    if (errorCode < errorMessages.size()) {
        return errorMessages[errorCode];
    }
    return "Unknown Error";
}

void Error::print() {
    if (current_error_code != 0) {
        std::string message;

        // Check if the errorCode has a standard message.
        if (current_error_code < errorMessages.size()) {
            message = errorMessages[current_error_code];
            if (!custom_error_message.empty()) {
                message = message + ", " + custom_error_message;
            }
        } else if (!custom_error_message.empty()) {
            message = custom_error_message;
        } else // Otherwise, use a generic fallback.
        {
            message = "Unknown Error";
        }

        TextIO::print("? Error #" + std::to_string(current_error_code) + "," + message);
        if (error_line_number > 0) {
            TextIO::print(" IN LINE " + std::to_string(error_line_number));
        }
        TextIO::nl();
    }
}