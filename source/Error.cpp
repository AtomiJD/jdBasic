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
    if (current_error_code == 0) { // Only store the first error
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage; // Store the custom message
        if (!custom_error_message.empty()) {
            g_vm_instance_ptr->builtin_constants["ERRMSG"] = custom_error_message;
        }

        if (g_vm_instance_ptr && g_vm_instance_ptr->error_handler_active) {
            // Set the built-in error variables (ERR and ERL)
            g_vm_instance_ptr->err_code = static_cast<double>(errorCode);
            g_vm_instance_ptr->erl_line = static_cast<double>(lineNumber);
            g_vm_instance_ptr->builtin_constants["ERR"] = g_vm_instance_ptr->err_code;
            g_vm_instance_ptr->builtin_constants["ERL"] = g_vm_instance_ptr->erl_line;
            // Save current context for RESUME
            g_vm_instance_ptr->resume_runtime_line = g_vm_instance_ptr->runtime_current_line;
            g_vm_instance_ptr->resume_p_code_ptr = g_vm_instance_ptr->active_p_code;
            g_vm_instance_ptr->resume_function_table_ptr = g_vm_instance_ptr->active_function_table;
            g_vm_instance_ptr->resume_call_stack_snapshot = g_vm_instance_ptr->call_stack;
            g_vm_instance_ptr->resume_for_stack_snapshot = g_vm_instance_ptr->for_stack;

            // The main execution loop will calculate resume_pcode_next_statement
            // for RESUME NEXT. For simple RESUME (retry current), use current_statement_start_pcode.
            g_vm_instance_ptr->resume_pcode = g_vm_instance_ptr->current_statement_start_pcode; // <<< NEW

            // Signal the main execution loop to jump to the error handler
            g_vm_instance_ptr->jump_to_error_handler = true;
        }
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