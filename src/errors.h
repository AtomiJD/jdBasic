#pragma once
#include <string>
#include <iostream>

// Error codes
enum class ErrCode {
    NONE = 0,
    SYNTAX_ERROR = 10,
    TYPE_MISMATCH = 11,
    DIVISION_BY_ZERO = 12,
    INDEX_OUT_OF_RANGE = 13,
    UNDEFINED_VARIABLE = 20,
    UNDEFINED_FUNCTION = 22,
    WRONG_ARG_COUNT = 23,
    WRONG_ARG_TYPE = 24,
    FILE_NOT_FOUND = 30,
    FILE_WRITE_ERROR = 31,
    STACK_OVERFLOW = 40,
    STACK_UNDERFLOW = 41,
    USER_ERROR = 50,       // THROW
    COM_ERROR = 60,
    HTTP_ERROR = 70,
    RUNTIME_ERROR = 99
};

// Global config
inline bool g_color_errors = true;   // false for VS Code / plain text mode

inline void print_error(ErrCode code, const std::string& msg, int line = 0) {
    if (g_color_errors)
        std::cerr << "\033[91mError #" << (int)code << ":\033[0m ";
    else
        std::cerr << "Error #" << (int)code << ": ";
    std::cerr << msg;
    if (line > 0) std::cerr << " at line " << line;
    std::cerr << std::endl;
}

// Structured runtime error with code
class jdError : public std::runtime_error {
public:
    ErrCode code;
    int line;
    jdError(ErrCode c, const std::string& msg, int ln = 0)
        : std::runtime_error(msg), code(c), line(ln) {}
};
