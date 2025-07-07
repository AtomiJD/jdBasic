#pragma once

#include "Types.hpp"
#include <string>

// Forward-declare the main interpreter class and BasicValue struct
// to avoid including the full, heavy headers in this lightweight interface.
class NeReLaBasic;

// --- Define Function Pointer Types ---
// These types match the signatures of the functions the DLL needs from the main app.
using ErrorSetFunc = void(*)(unsigned char, unsigned short, const std::string&);
using ToUpperFunc = std::string(*)(std::string);

/**
 * @brief A struct to hold all the functions the main app provides to a module.
 *
 * This acts as a "service locator" or a "callback table" that the DLL can use
 * to access core interpreter utilities without needing to link against them.
 */
struct ModuleServices {
    ErrorSetFunc error_set;
    ToUpperFunc to_upper;
    // If you get more linker errors for other functions, add their pointer types here.
};
