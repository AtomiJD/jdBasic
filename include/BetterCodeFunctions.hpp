#pragma once

#include "NeReLaBasic.hpp"
#include "BuiltinFunctions.hpp" 


BasicValue builtin_pretty(NeReLaBasic& vm, const std::vector<BasicValue>& args);
void register_better_code_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate);
