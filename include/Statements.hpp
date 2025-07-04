// Statements.hpp
#pragma once
#include <string>
#include "Tokens.hpp"

namespace Statements {
    // Looks up a string to see if it's a reserved keyword.
    // If it is, it returns the corresponding token ID.
    // Otherwise, it returns NOCMD.
    Tokens::ID get(const std::string& statement);
}