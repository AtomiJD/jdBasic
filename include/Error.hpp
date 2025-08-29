// Error.hpp
#pragma once
#include <cstdint>
#include <string>

class NeReLaBasic;

namespace Error {
    // Sets the current error code.
    void set(uint16_t errorCode, uint16_t lineNumber, const std::string& customMessage = "");

    // Gets the current error code.
    uint16_t get();

    // Clears the current error (sets it to 0).
    void clear();

    // Prints the message for the current error.
    void print();

    // A helper to get the message for a specific code.
    std::string getMessage(uint16_t errorCode);
}