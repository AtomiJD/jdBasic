#pragma once

#include <functional>
#include <vector>
#include <string>
#include "NeReLaBasic.hpp"
#include "json.hpp" // Include for nlohmann::json type

// Forward-declare the main classes/structs to avoid circular dependencies
class NeReLaBasic;
using FunctionTable = std::unordered_map<std::string, NeReLaBasic::FunctionInfo>;


// This is the single public function declaration for this file.
// It takes a reference to the interpreter and a reference to the specific
// function table (e.g., the main table or a module's table) that it should populate.
void register_builtin_functions(NeReLaBasic& vm, FunctionTable& table_to_populate);
BasicValue builtin_transpose(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue array_sum(NeReLaBasic& vm, const std::vector<BasicValue>& args);

BasicValue json_to_basic_value(const nlohmann::json& j);
nlohmann::json basic_to_json_value(const BasicValue& val);

#ifdef JDCOM
_variant_t basic_value_to_variant_t(const BasicValue& val);
BasicValue variant_t_to_basic_value(const _variant_t& vt, NeReLaBasic& vm);
BasicValue builtin_create_object(NeReLaBasic& vm, const std::vector<BasicValue>& args);
HRESULT invoke_com_method(
    IDispatchPtr pDisp,
    const std::string& memberName,
    const std::vector<BasicValue>& args, // For actual method arguments
    _variant_t& result,                   // For method return value / property get result
    WORD dwFlags,                         // DISPATCH_METHOD, DISPATCH_PROPERTYGET, DISPATCH_PROPERTYPUT
    const _variant_t* pPropertyValue = nullptr // NEW: Optional for property put
);
#endif
