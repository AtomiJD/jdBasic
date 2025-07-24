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
    if (g_vm_instance_ptr == nullptr) {
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage;
        return;
    }

    NeReLaBasic* vm = g_vm_instance_ptr;

    // Check if there is an active error handler on the stack
    if (!vm->handler_stack.empty()) {
        // Get the handler for the current TRY block
        NeReLaBasic::ExceptionHandler handler = vm->handler_stack.back();

        // Store error info for the CATCH block to access via ERR, ERL, etc.
        current_error_code = errorCode;
        error_line_number = lineNumber;
        custom_error_message = customMessage;
        vm->variables["ERR"] = static_cast<double>(errorCode);
        vm->variables["ERL"] = static_cast<double>(lineNumber);
        vm->variables["ERRMSG$"] = getMessage(errorCode) + (customMessage.empty() ? "" : ", " + customMessage);
        vm->variables["STACK$"] = vm->get_stacktrace();

        // UNWIND THE STACKS to the state they were in when TRY was entered
        if (vm->call_stack.size() > handler.call_stack_depth) {
            vm->call_stack.resize(handler.call_stack_depth);
        }
        if (vm->for_stack.size() > handler.for_stack_depth) {
            vm->for_stack.resize(handler.for_stack_depth);
        }

        // Set the pending jump flags for the main execution loop to handle.
        vm->jump_to_catch_pending = true;
        if (handler.catch_address != 0) {
            vm->pending_catch_address = handler.catch_address;
        }
        else {
            // If no CATCH, jump to FINALLY (or past the block if no FINALLY)
            vm->pending_catch_address = handler.finally_address;
        }
        return;
    }

    // No handler found, this is an unhandled exception.
    // Set the global error state to halt the program.
    current_error_code = errorCode;
    error_line_number = lineNumber;
    custom_error_message = customMessage;
    if (vm->current_task) {
        vm->current_task->status = TaskStatus::ERRORED;
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