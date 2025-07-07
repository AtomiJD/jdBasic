// Commands.cpp
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <conio.h>
#include "Commands.hpp"
#include "StringUtils.hpp"
#include "NeReLaBasic.hpp" // We need the full class definition here
#include "Compiler.hpp""
#include "TextIO.hpp"
#include "Tokens.hpp"
#include "Error.hpp"
#include "Types.hpp"
#include "TextEditor.hpp"
#include "LocaleManager.hpp"
#include "BuiltinFunctions.hpp"


namespace { // Use an anonymous namespace to keep this helper local to this file
    // Forward declaration of the recursive part
    std::string array_to_string_recursive(
        const Array& arr,
        size_t& data_index, // Pass as a reference to be advanced
        size_t current_dimension
    );

    std::string float_array_to_string_recursive(
        const FloatArray& arr,
        size_t& data_index,
        size_t current_dimension
    );

    std::string value_to_string_for_array(const BasicValue& val) {
        // A specialized to_string to avoid infinite recursion if an array contains itself
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
                return "<Array>"; // Don't recursively print nested arrays
            }
            else {
                return to_string(arg); // Use the main to_string for all other types
            }
            }, val);
    }

    // Recursive helper for FloatArray
    std::string array_to_string_recursive(const Array& arr, size_t& data_index, size_t current_dimension) {
        std::stringstream ss;
        ss << "[";

        bool is_innermost_vector = (current_dimension == arr.shape.size() - 1);

        for (size_t i = 0; i < arr.shape[current_dimension]; ++i) {
            if (is_innermost_vector) {
                if (data_index < arr.data.size()) {
                    ss << value_to_string_for_array(arr.data[data_index++]);
                }
            }
            else {
                ss << array_to_string_recursive(arr, data_index, current_dimension + 1);
            }

            if (i < arr.shape[current_dimension] - 1) {
                ss << " "; // Separator between elements/sub-arrays
            }
        }
        ss << "]";
        return ss.str();
    }

    std::string float_array_to_string_recursive(const FloatArray& arr, size_t& data_index, size_t current_dimension) {
        std::stringstream ss;
        ss << "[";

        bool is_innermost_vector = (current_dimension == arr.shape.size() - 1);

        for (size_t i = 0; i < arr.shape[current_dimension]; ++i) {
            if (is_innermost_vector) {
                if (data_index < arr.data.size()) {
                    // Directly convert the double to string using the main to_string
                    ss << to_string(arr.data[data_index++]);
                }
            }
            else {
                ss << float_array_to_string_recursive(arr, data_index, current_dimension + 1);
            }

            if (i < arr.shape[current_dimension] - 1) {
                ss << " ";
            }
        }
        ss << "]";
        return ss.str();
    }

    // Wrapper for FloatArray to_string conversion
    std::string float_array_to_string(const FloatArray& arr) {
        if (arr.shape.empty() || arr.data.empty()) {
            return "[]";
        }
        size_t data_idx = 0;
        return float_array_to_string_recursive(arr, data_idx, 0);
    }
} // end anonymous namespace

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return s;
}

// Helper to read a null-terminated string from p_code memory
std::string read_string(NeReLaBasic& vm) {
    std::string s;
    while ((*vm.active_p_code)[vm.pcode] != 0) {
        s += (*vm.active_p_code)[vm.pcode++];
    }
    vm.pcode++; // Skip the null terminator
    return s;
}


// Finds a variable by walking the call stack backwards, then checking globals.
BasicValue& get_variable(NeReLaBasic& vm, const std::string& name) {
    // 1. Search backwards through the call stack for the variable.
    if (!vm.call_stack.empty()) {
        for (auto it = vm.call_stack.rbegin(); it != vm.call_stack.rend(); ++it) {
            if (it->local_variables.count(name)) {
                return it->local_variables.at(name);
            }
        }
    }

    // 2. If not found in any local scope, check the global scope.
    // Note: Using .at() would throw an error if the key doesn't exist.
    // Using operator[] will create it if it doesn't exist, which is the
    // desired behavior for BASIC (e.g., undeclared variables default to 0).
    return vm.variables[name];
}

// Sets a variable. If inside a function, it sets the variable in the
// CURRENT function's local scope. Otherwise, it sets a global variable.
void set_variable(NeReLaBasic& vm, const std::string& name, const BasicValue& value, bool force) {
    // If we are inside a function/subroutine...
    if (!vm.call_stack.empty()) {
        // First, check if a LOCAL variable with this name already exists in the current scope.
        // This is important for loops or multiple assignments to the same local variable.
        if (vm.call_stack.back().local_variables.count(name)) {
            vm.call_stack.back().local_variables[name] = value;
            return;
        }

        // Second, check if a GLOBAL variable with this name exists.
        // If so, update the global one INSTEAD of creating a new local one.
        if (vm.variables.count(name) && !force) {
            vm.variables[name] = value;
            return;
        }

        // Finally, if it's not in the local scope and not in the global scope,
        // it is a brand new variable, which we will define as local to the subroutine.
        vm.call_stack.back().local_variables[name] = value;
    }
    else {
        // Not in a subroutine, so it must be a global variable.
        vm.variables[name] = value;
    }
}

std::string to_string(const BasicValue& val) {
    // std::visit will execute the correct lambda block based on the type currently held in val
    return std::visit([](auto&& arg) -> std::string {
        // Get the actual type of the argument
        using T = std::decay_t<decltype(arg)>;

        // Use 'if constexpr' to handle each possible type in the variant
        if constexpr (std::is_same_v<T, bool>) {
            return arg ? "TRUE" : "FALSE";
        }
        else if constexpr (std::is_same_v<T, double>) {
            std::stringstream ss;
            // Get the current locale from our global manager
            ss.imbue(LocaleManager::get_current_locale());
            ss << std::fixed << std::setprecision(6) << arg;

            std::string s = ss.str();
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            char decimal_point = std::use_facet<std::numpunct<char>>(LocaleManager::get_current_locale()).decimal_point();
            if (!s.empty() && s.back() == decimal_point) {
                s.pop_back();
            }
            return s;
        }
        else if constexpr (std::is_same_v<T, int>) {
            std::stringstream ss;
            ss.imbue(LocaleManager::get_current_locale());
            ss << arg;
            return ss.str();
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        }
        else if constexpr (std::is_same_v<T, FunctionRef>) {
            // Return a descriptive string for the function reference.
            return "<Function: " + arg.name + ">";
        }
        else if constexpr (std::is_same_v<T, DateTime>) {
            // Logic to convert DateTime to a string
            auto tp = arg.time_point;
            auto in_time_t = std::chrono::system_clock::to_time_t(tp);
            std::stringstream ss;
#pragma warning(suppress : 4996)
            ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
            return ss.str();
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<JsonObject>>) {
            if (!arg) {
                return "<Null JSON>";
            }
            // Use the nlohmann::json pretty-printer (dump)
            return arg->data.dump(2); // dump with an indent of 2 spaces
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Map>>) {
            // --- CASE for handling std::shared_ptr<Map> ---
            if (!arg) {
                return "<Null Map>";
            }
            if (arg->data.empty()) {
                return "{}";
            }
            std::stringstream ss;
            ss << "{ ";
            for (auto it = arg->data.begin(); it != arg->data.end(); ++it) {
                ss << "\"" << it->first << "\": " << to_string(it->second);
                if (std::next(it) != arg->data.end()) {
                    ss << ", ";
                }
            }
            ss << " }";
            return ss.str();
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            if (!arg) {
                return "<Null Array>";
            }
            if (arg->shape.empty() || arg->data.empty()) {
                return "[]"; // An empty array
            }
            size_t data_idx = 0;
            return array_to_string_recursive(*arg, data_idx, 0);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Tensor>>) {
            if (!arg) return "<Null Tensor>";
            std::string tensor_str = "Tensor(";
            if (arg->data) {
                // MODIFIED: Call the new helper for FloatArray
                tensor_str += float_array_to_string(*(arg->data));
            }
            else {
                tensor_str += "<Null Data>";
            }
            tensor_str += ")";

            if (arg->grad && arg->grad->data) {
                tensor_str += "\n  .grad=";
                // MODIFIED: Call the new helper for the gradient's FloatArray
                tensor_str += float_array_to_string(*(arg->grad->data));
            }
            return tensor_str;
        }  
        else if constexpr (std::is_same_v<T, ThreadHandle>) {
            // Use a stringstream to build the string
            std::stringstream ss;
            ss << "[ThreadHandle:" << arg.id << "]";
            return ss.str(); // Return the built string
        }
        else if constexpr (std::is_same_v<T, TaskRef>) {
            return "<Task ID: " + std::to_string(arg.id) + ">";
        }

#ifdef JDCOM
        else if constexpr (std::is_same_v<T, ComObject>) { // This is the problematic part
            // For a ComObject, you typically can't get a meaningful string
            // without trying to query for properties like Name or ProgID.
            // For simplicity, return a placeholder string.
            if (arg.ptr) {
                // If you want more detail, you could try to get the ProgID:
                // _bstr_t progIdBstr;
                // HRESULT hr = ProgIDFromCLSID(arg.ptr->GetClassID(), &progIdBstr.GetBSTR()); // Needs GetClassID, often not directly on IDispatch
                // If getting ProgID is too complex for now, a simple placeholder is fine.
                return "<COM Object>"; // Return a descriptive string 
            }
            else {
                return "<Null COM Object>"; // Return for a null COM object 
            }
        }
#endif
        }, val);
}

void print_value(const BasicValue& val) {
    TextIO::print(to_string(val));
}

void dump_p_code(const std::vector<uint8_t>& p_code_to_dump, const std::string& name) {
    const int bytes_per_line = 16;
    TextIO::print("Dumping p_code for '" + name + "' (" + std::to_string(p_code_to_dump.size()) + " bytes):\n");

    for (size_t i = 0; i < p_code_to_dump.size(); i += bytes_per_line) {
        // Print Address
        std::stringstream ss_addr;
        ss_addr << "0x" << std::setw(4) << std::setfill('0') << std::hex << i;
        TextIO::print(ss_addr.str() + " : ");

        // Print Hex Bytes
        std::stringstream ss_hex;
        for (int j = 0; j < bytes_per_line; ++j) {
            if (i + j < p_code_to_dump.size()) {
                ss_hex << std::setw(2) << std::setfill('0') << std::hex
                    << static_cast<int>(p_code_to_dump[i + j]) << " ";
            }
            else {
                ss_hex << "   ";
            }
        }
        TextIO::print(ss_hex.str() + " : ");

        // Print ASCII characters
        std::stringstream ss_ascii;
        for (int j = 0; j < bytes_per_line; ++j) {
            if (i + j < p_code_to_dump.size()) {
                char c = p_code_to_dump[i + j];
                if (isprint(static_cast<unsigned char>(c))) {
                    ss_ascii << c;
                }
                else {
                    ss_ascii << '.';
                }
            }
        }
        TextIO::print(ss_ascii.str());
        TextIO::nl();
    }
}

void Commands::do_dim(NeReLaBasic& vm) {
    Tokens::ID var_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]);
    std::string var_name = to_upper(read_string(vm));

    Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

    if (next_token == Tokens::ID::C_LEFTBRACKET) {
        // --- Case 1: ARRAY DECLARATION (e.g., DIM A[2, 3]) ---
        vm.pcode++; // Consume '['

        std::vector<size_t> dimensions;
        while (true) {
            BasicValue size_val = vm.evaluate_expression();
            if (Error::get() != 0) return;

            double dim_double = to_double(size_val);
            if (dim_double < 0) {
                Error::set(10, vm.runtime_current_line); // Bad subscript
                return;
            }
            dimensions.push_back(static_cast<size_t>(dim_double));

            Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
            if (separator == Tokens::ID::C_COMMA) {
                vm.pcode++;
            }
            else if (separator == Tokens::ID::C_RIGHTBRACKET) {
                break;
            }
            else {
                Error::set(1, vm.runtime_current_line); // Syntax error: Expected , or ]
                return;
            }
        }
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_RIGHTBRACKET) {
            Error::set(1, vm.runtime_current_line); return;
        }

        // --- Create the Array on the heap using std::make_shared ---
        auto new_array_ptr = std::make_shared<Array>();
        new_array_ptr->shape = dimensions;

        size_t total_size = new_array_ptr->size();

        bool is_string_array = (var_name.back() == '$');
        BasicValue default_val = is_string_array ? BasicValue{ std::string("") } : BasicValue{ 0.0 };

        new_array_ptr->data.assign(total_size, default_val);

        // Store the shared_ptr in the BasicValue variant
        set_variable(vm, var_name, new_array_ptr);
    }
    else
    {
        // --- Case 2: TYPED VARIABLE DECLARATION (e.g., DIM V AS DATE) ---
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::AS) {
            Error::set(1, vm.runtime_current_line); return;
        }

        // Get the token that represents the type
        Tokens::ID type_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]);

        BasicValue default_value;
        bool type_found = true; // Assume success

        switch (type_token) {
        case Tokens::ID::INT:       default_value = 0; break;
        case Tokens::ID::DOUBLE:    default_value = 0.0; break;
        case Tokens::ID::STRTYPE:   default_value = std::string(""); break;
        case Tokens::ID::DATE:      default_value = DateTime{}; break;
        case Tokens::ID::BOOL:      default_value = false; break;
        case Tokens::ID::MAP:       default_value = std::make_shared<Map>(); break;

            // This case handles user-defined types, which are tokenized as VARIANT.
        case Tokens::ID::VARIANT: {
            // The type name is a string that follows the VARIANT token.
            std::string type_name_str = to_upper(read_string(vm));
            if (vm.user_defined_types.count(type_name_str)) {
                const auto& type_info = vm.user_defined_types.at(type_name_str);
                auto udt_instance = std::make_shared<Map>();

                for (const auto& member_pair : type_info.members) {
                    const auto& member_info = member_pair.second;
                    BasicValue member_default_val;
                    switch (member_info.type_id) {
                    case DataType::INTEGER:     member_default_val = 0; break;
                    case DataType::STRING:      member_default_val = std::string(""); break;
                    case DataType::BOOL:        member_default_val = false; break;
                    case DataType::DATETIME:    member_default_val = DateTime{}; break;
                    case DataType::MAP:         member_default_val = std::make_shared<Map>(); break;
                    default:                    member_default_val = 0.0; break;
                    }
                    udt_instance->data[member_info.name] = member_default_val;
                }
                default_value = udt_instance;
            }
            else {
                type_found = false;
                Error::set(1, vm.runtime_current_line, "Unknown type name '" + type_name_str + "'");
            }
            break;
        }
        default:
            type_found = false;
            Error::set(1, vm.runtime_current_line, "Invalid type specified for DIM AS.");
            break;
        }

        if (type_found) {
            set_variable(vm, var_name, default_value);
        }
    }
}


// --- Implementation for the IMPORT command ---
void Commands::do_dllimport(NeReLaBasic& vm) {
    // The IMPORT token has been consumed. The next token should be a string literal
    // containing the name of the module to load (e.g., "aifunc").
    if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]) != Tokens::ID::STRING) {
        Error::set(1, vm.runtime_current_line, "IMPORT command requires a string literal for the module name.");
        return;
    }
    vm.pcode++; // Consume the STRING token

    // Read the module name from the bytecode.
    std::string module_name = read_string(vm);

    // Call the new method in NeReLaBasic to handle the dynamic loading.
    // The method will handle adding the file extension (.dll or .so)
    // and setting any errors if loading fails.
    vm.load_dynamic_module(module_name);
}

void Commands::do_input(NeReLaBasic& vm) {
    // Peek at the next token to see if there is an optional prompt string.
    Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

    if (next_token == Tokens::ID::STRING) {
        // --- Case 1: Handle a prompt string ---
        BasicValue prompt = vm.evaluate_expression();
        if (Error::get() != 0) return;
        TextIO::print(to_string(prompt));

        // After the prompt, there MUST be a separator (',' or ';').
        Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
        if (separator == Tokens::ID::C_SEMICOLON) {
            vm.pcode++; // Consume the semicolon
            TextIO::print(" "); // Suppress the '?' and print a space
        }
        else if (separator == Tokens::ID::C_COMMA) {
            vm.pcode++; // Consume the comma
            TextIO::print("? "); // Print the '?'
        }
        else {
            // No separator after the prompt is a syntax error
            Error::set(1, vm.runtime_current_line);
            return;
        }
    }
    else {
        // --- Case 2: No prompt string ---
        TextIO::print("? ");
    }

    // Now, we are past the prompt and separator, so we can get the variable name.
    next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
    if (next_token != Tokens::ID::VARIANT && next_token != Tokens::ID::INT && next_token != Tokens::ID::STRVAR) {
        Error::set(1, vm.runtime_current_line); // Syntax error, expected a variable
        return;
    }
    vm.pcode++;
    std::string var_name = to_upper(read_string(vm));

    // Read a full line of input from the user.
    std::string user_input_line;
    std::getline(std::cin, user_input_line);

    // Store the value, converting type if necessary.
    if (var_name.back() == '$') {
        // It's a string variable, do a direct assignment.
        set_variable(vm, var_name, user_input_line);
    }
    else {
        // It's a numeric variable. Try to convert the input to a double.
        try {
            double num_val = std::stod(user_input_line);
            set_variable(vm, var_name, num_val);
        }
        catch (const std::exception&) {
            set_variable(vm, var_name, 0.0);
        }
    }
}
void Commands::do_print(NeReLaBasic& vm) {
    // Loop through all items in the PRINT list until the statement ends.
    while (true) {
        // Peek at the next token to decide what to do.
        Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
        if (next_token == Tokens::ID::NOCMD || next_token == Tokens::ID::C_CR || next_token == Tokens::ID::C_COLON) {
            TextIO::nl(); return;
        }

        BasicValue result = vm.evaluate_expression();
        if (Error::get() != 0) {
            return; // Stop if the expression had an error
        }
        print_value(result); // Use our helper to print the result, whatever its type

        // --- Step 2: Look ahead for a separator ---
        Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

        if (separator == Tokens::ID::C_COMMA) {
            vm.pcode++; // Consume the comma
            // Check if the line ends right after the comma
            Tokens::ID after_separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
            if (after_separator == Tokens::ID::NOCMD || after_separator == Tokens::ID::C_CR || next_token == Tokens::ID::C_COLON) {
                return; // Ends with a comma, so no newline.
            }
            TextIO::print("\t"); // Otherwise, print a tab and continue the loop.
        }
        else if (separator == Tokens::ID::C_SEMICOLON) {
            vm.pcode++; // Consume the semicolon
            // Check if the line ends right after the semicolon
            Tokens::ID after_separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
            if (after_separator == Tokens::ID::NOCMD || after_separator == Tokens::ID::C_CR || next_token == Tokens::ID::C_COLON) {
                return; // Ends with a semicolon, so no newline.
            }
            // Otherwise, just continue the loop to the next item.
        }
        else {
            // No separator after the item, so the statement is over.
            TextIO::nl();
            return;
        }
    }
}

void Commands::do_let(NeReLaBasic& vm) {
    Tokens::ID var_type_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
    vm.pcode++;
    std::string name = to_upper(read_string(vm));

    size_t dot_pos = name.find('.');

    // --- Case 1: ARRAY ELEMENT ASSIGNMENT (e.g., A[i, j] = ...) ---
    if (var_type_token == Tokens::ID::ARRAY_ACCESS) {
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_LEFTBRACKET) {
            Error::set(1, vm.runtime_current_line); return;
        }

        // Parse multiple comma-separated indices
        std::vector<size_t> indices;
        while (true) {
            BasicValue index_val = vm.evaluate_expression();
            if (Error::get() != 0) return;
            indices.push_back(static_cast<size_t>(to_double(index_val)));

            Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
            if (separator == Tokens::ID::C_RIGHTBRACKET) break;
            if (separator != Tokens::ID::C_COMMA) { Error::set(1, vm.runtime_current_line); return; }
            vm.pcode++;
        }
        vm.pcode++;

        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_EQ) {
            Error::set(1, vm.runtime_current_line); return;
        }
        BasicValue value_to_assign = vm.evaluate_expression();
        if (Error::get() != 0) return;

        // Get the array, check its type, and perform assignment
        BasicValue& array_var = get_variable(vm, name);
        if (!std::holds_alternative<std::shared_ptr<Array>>(array_var)) {
            Error::set(15, vm.runtime_current_line); // Type Mismatch
            return;
        }
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(array_var);
        if (!arr_ptr) { Error::set(15, vm.runtime_current_line); return; }

        try {
            size_t flat_index = arr_ptr->get_flat_index(indices);
            arr_ptr->data[flat_index] = value_to_assign;
        }
        catch (const std::exception&) {
            Error::set(10, vm.runtime_current_line); // Bad subscript
        }
    }
    // --- Case 2: MAP ASSIGNMENT  ---
    else if (var_type_token == Tokens::ID::MAP_ACCESS) {
        // Handle map assignment: my_map{"key"} = value
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_LEFTBRACE) {
            Error::set(1, vm.runtime_current_line); return;
        }
        // The key inside {} must be a string expression
        BasicValue key_val = vm.evaluate_expression();
        if (Error::get() != 0) return;
        std::string key = to_string(key_val);

        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_RIGHTBRACE) {
            Error::set(1, vm.runtime_current_line); return;
        }
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_EQ) {
            Error::set(1, vm.runtime_current_line); return;
        }

        BasicValue value_to_assign = vm.evaluate_expression();
        if (Error::get() != 0) return;

        BasicValue& map_var = get_variable(vm, name);
        if (!std::holds_alternative<std::shared_ptr<Map>>(map_var)) {
            Error::set(15, vm.runtime_current_line); return;
        }
        const auto& map_ptr = std::get<std::shared_ptr<Map>>(map_var);
        if (!map_ptr) { Error::set(15, vm.runtime_current_line); return; }

        // Perform the map insertion/update
        map_ptr->data[key] = value_to_assign;
    }
    // --- Case 3: WHOLE VARIABLE ASSIGNMENT (e.g., A = ...) ---
    else {
        // --- CORRECTED: Logic for dot-notation assignment ---
        if (name.find('.') != std::string::npos) {
            if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_EQ) {
                Error::set(1, vm.runtime_current_line); return;
            }
            BasicValue value_to_assign = vm.evaluate_expression();
            if (Error::get() != 0) return;

            auto [final_obj, final_member] = vm.resolve_dot_chain(name);
            if (Error::get() != 0) return;

            // Case 1: The target is a User-Defined Type (a Map)
            if (std::holds_alternative<std::shared_ptr<Map>>(final_obj)) {
                auto& map_ptr = std::get<std::shared_ptr<Map>>(final_obj);
                if (map_ptr) {
                    map_ptr->data[final_member] = value_to_assign;
                }
                else {
                    Error::set(3, vm.runtime_current_line, "Cannot assign to member of a null object.");
                }
            }
#ifdef JDCOM
            // Case 2: The target is a COM Object
            else if (std::holds_alternative<ComObject>(final_obj)) {
                IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
                if (!pDisp) { Error::set(1, vm.runtime_current_line, "Uninitialized COM object."); return; }
                _variant_t vt_value = basic_value_to_variant_t(value_to_assign);
                _variant_t result_vt_unused;
                HRESULT hr = invoke_com_method(pDisp, final_member, {}, result_vt_unused, DISPATCH_PROPERTYPUT, &vt_value);
                if (FAILED(hr)) {
                    Error::set(12, vm.runtime_current_line, "Failed to set COM property '" + final_member + "'");
                    return;
                }
            }
#endif
            else {
                Error::set(15, vm.runtime_current_line, "Dot notation can only be used on objects and user-defined types.");
            }
        }
        else {
            // It's a regular variable assignment (e.g. A = 10)
            if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_EQ) {
                Error::set(1, vm.runtime_current_line); return;
            }
            BasicValue value_to_assign = vm.evaluate_expression();
            if (Error::get() != 0) return;
            set_variable(vm, name, value_to_assign);
        }
    }
}

void Commands::do_goto(NeReLaBasic& vm) {
    // The label name was stored as a string in the bytecode after the GOTO token.
    std::string label_name = read_string(vm);

    // Look up the label in the address map
    if (vm.compiler->label_addresses.count(label_name)) {
        // Found it! Set the program counter to the stored address.
        uint16_t target_address = vm.compiler->label_addresses[label_name];
        vm.pcode = target_address;
    }
    else {
        // Error: Label not found
        Error::set(11, vm.runtime_current_line); // Use a new error code
    }
}

void Commands::do_if(NeReLaBasic& vm) {
    // The IF token has already been consumed by `statement`. `pcode` points to the 2-byte placeholder.
    uint16_t jump_placeholder_addr = vm.pcode;
    vm.pcode += 2; // Skip the placeholder to get to the expression.

    BasicValue result = vm.evaluate_expression();
    if (Error::get() != 0) return;

    if (!to_bool(result)) {
        // Condition is false, so jump. Read the address from the placeholder.
        uint8_t lsb = (*vm.active_p_code)[jump_placeholder_addr];
        uint8_t msb = (*vm.active_p_code)[jump_placeholder_addr + 1];
        vm.pcode = (msb << 8) | lsb;
    }
    // If true, we do nothing and just continue execution from the current pcode.
}

// Correct do_else implementation
void Commands::do_else(NeReLaBasic& vm) {
    // ELSE is an unconditional jump. The placeholder is right after the token.
    uint16_t jump_placeholder_addr = vm.pcode;
    uint8_t lsb = (*vm.active_p_code)[jump_placeholder_addr];
    uint8_t msb = (*vm.active_p_code)[jump_placeholder_addr + 1];
    vm.pcode = (msb << 8) | lsb;
}

void Commands::do_for(NeReLaBasic& vm) {
    // FOR [variable] = [start_expr] TO [end_expr] STEP [step_expr]

    // 1. Get the loop variable name.
    Tokens::ID var_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
    if (var_token != Tokens::ID::VARIANT && var_token != Tokens::ID::INT) {
        Error::set(1, vm.runtime_current_line); // Syntax Error
        return;
    }
    vm.pcode++;
    std::string var_name = read_string(vm);

    // 2. Expect an equals sign.
    if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_EQ) {
        Error::set(1, vm.runtime_current_line);
        return;
    }

    // 3. Evaluate the start expression and assign it.
    BasicValue start_val = vm.evaluate_expression();
    if (Error::get() != 0) return;
    set_variable(vm, var_name, start_val, true);
    // FOR always create a new local or global var!
    //if (!current_task_call_stack.empty()) {
    //    current_task_call_stack.back().local_variables[var_name] = start_val;
    //}
    //else {
    //    vm.variables[var_name] = start_val;
    //}
    //vm.variables[var_name] = start_val;

    // 4. The 'TO' keyword was skipped by the tokenizer. Evaluate the end expression.
    BasicValue end_val = vm.evaluate_expression();
    if (Error::get() != 0) return;

    // 5. --- THIS IS THE CORRECTED LOGIC FOR STEP ---
    double step_val = 1.0; // Default step is 1.

    // The TO and STEP keywords were skipped by the tokenizer.
    // If we are NOT at the end of the line, what's left must be the step expression.
    Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
    if (next_token != Tokens::ID::C_CR && next_token != Tokens::ID::NOCMD && next_token != Tokens::ID::C_COLON)
    {
        BasicValue step_expr_val = vm.evaluate_expression();
        if (Error::get() != 0) return;
        step_val = to_double(step_expr_val);
    }

    // 6. Push all the loop info onto the FOR stack.
    NeReLaBasic::ForLoopInfo loop_info;
    loop_info.variable_name = var_name;
    loop_info.end_value = to_double(end_val);
    loop_info.step_value = step_val;
    loop_info.loop_start_pcode = vm.pcode;

    vm.for_stack.push_back(loop_info);
}
void Commands::do_next(NeReLaBasic& vm) {
    // 1. Check if there is anything on the FOR stack.
    if (vm.for_stack.empty()) {
        Error::set(21, vm.runtime_current_line); // New Error: NEXT without FOR
        return;
    }

    // 2. Get the info for the current (innermost) loop.
    NeReLaBasic::ForLoopInfo& current_loop = vm.for_stack.back();

    // 3. Get the loop variable's current value and increment it by the step.
    double current_val = to_double(get_variable(vm, current_loop.variable_name));
    current_val += current_loop.step_value;
    set_variable(vm, current_loop.variable_name, current_val);
    //vm.variables[current_loop.variable_name] = current_val;

    // 4. Check if the loop is finished.
    bool loop_finished = false;
    if (current_loop.step_value > 0) { // Positive step
        if (current_val > current_loop.end_value) {
            loop_finished = true;
        }
    }
    else { // Negative step
        if (current_val < current_loop.end_value) {
            loop_finished = true;
        }
    }

    // 5. Act on the check.
    if (loop_finished) {
        // The loop is over, pop it from the stack and continue execution.
        vm.for_stack.pop_back();
    }
    else {
        // The loop continues. Jump pcode back to the start of the loop.
        vm.pcode = current_loop.loop_start_pcode;
    }
}

void Commands::do_func(NeReLaBasic& vm) {
    // The FUNC token was consumed by statement(). pcode points to its arguments.
    // In our bytecode, the argument is the 2-byte address to jump to.
    uint8_t lsb = (*vm.active_p_code)[vm.pcode++];
    uint8_t msb = (*vm.active_p_code)[vm.pcode++];
    uint16_t jump_over_address = (msb << 8) | lsb;

    // Set pcode to the target, skipping the entire function body.
    vm.pcode = jump_over_address;
}

// Commands.cpp

// ... (existing includes and helper functions)

void Commands::do_callfunc(NeReLaBasic& vm) {
    std::string identifier_being_called = to_upper(read_string(vm));

    // Check if the identifier contains a dot, which signifies a potential method call (e.g., "conn.Open").
    if (identifier_being_called.find('.') != std::string::npos) {
#ifdef JDCOM // This logic is only compiled if COM support is enabled.
        // It's a dot-chain, so resolve it to get the target object and the method name.
        auto [final_obj, final_method] = vm.resolve_dot_chain(identifier_being_called);
        if (Error::get() != 0) {
            return; // An error occurred during resolution.
        }

        // Ensure we have a COM object to call a method on.
        if (!std::holds_alternative<ComObject>(final_obj)) {
            Error::set(15, vm.runtime_current_line, "Methods can only be called on objects.");
            return;
        }

        IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
        if (!pDisp) {
            Error::set(1, vm.runtime_current_line, "Uninitialized COM object.");
            return;
        }

        // Parse arguments from the bytecode stream.
        std::vector<BasicValue> com_args;
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_LEFTPAREN) {
            Error::set(1, vm.runtime_current_line, "Syntax Error: Missing '('.");
            return;
        }
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]) != Tokens::ID::C_RIGHTPAREN) {
            while (true) {
                com_args.push_back(vm.evaluate_expression());
                if (Error::get() != 0) return;

                Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
                if (separator == Tokens::ID::C_RIGHTPAREN) break;
                if (separator != Tokens::ID::C_COMMA) {
                    Error::set(1, vm.runtime_current_line, "Syntax Error: Missing ','.");
                    return;
                }
                vm.pcode++;
            }
        }
        vm.pcode++; // Consume ')'

        _variant_t result_vt; // Result is discarded for statement calls.
        HRESULT hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_METHOD);

        if (FAILED(hr)) {
            // If the method call fails, it might be a property get with parameters (e.g., obj.Item(1)).
            // We still try this even for a statement call, as the call itself might have side effects.
            hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_PROPERTYGET);
            if (FAILED(hr)) {
                Error::set(12, vm.runtime_current_line, "Failed to call COM method or get property '" + final_method + "'");
                return;
            }
        }
#else
        // If COM is not defined, this is an error because dot notation is not supported.
        Error::set(22, vm.runtime_current_line, "Unknown function: " + identifier_being_called);
#endif
    }
    else {
        // --- Original Logic for standard jdbasic function calls ---
        std::string real_func_to_call = identifier_being_called;

        // Check for higher-order function calls (e.g., func_var(arg))
        if (!vm.active_function_table->count(real_func_to_call)) {
            BasicValue& var = get_variable(vm, identifier_being_called);
            if (std::holds_alternative<FunctionRef>(var)) {
                real_func_to_call = std::get<FunctionRef>(var).name;
            }
        }

        if (!vm.active_function_table->count(real_func_to_call)) {
            Error::set(22, vm.runtime_current_line, "Undefined function: " + real_func_to_call);
            return;
        }

        const auto& func_info = vm.active_function_table->at(real_func_to_call);
        std::vector<BasicValue> args;

        // Argument Parsing
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]) != Tokens::ID::C_LEFTPAREN) {
            Error::set(1, vm.runtime_current_line); return;
        }
        if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]) != Tokens::ID::C_RIGHTPAREN) {
            while (true) {
                args.push_back(vm.evaluate_expression());
                if (Error::get() != 0) return;
                Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
                if (separator == Tokens::ID::C_RIGHTPAREN) break;
                if (separator != Tokens::ID::C_COMMA) { Error::set(1, vm.runtime_current_line); return; }
                vm.pcode++;
            }
        }
        vm.pcode++; // Consume ')'

        if (func_info.arity != -1 && args.size() != func_info.arity) {
            Error::set(26, vm.runtime_current_line, "Incorrect number of arguments."); return;
        }

        // Unified Execution Logic
        if (func_info.native_impl != nullptr) {
            // Call native C++ function
            func_info.native_impl(vm, args);
        }
        else {
            // For a user-defined BASIC function, use the synchronous executor
            // and discard its return value.
            vm.execute_synchronous_function(func_info, args);
        }
    }
}

// --- Implementation of do_return ---
void Commands::do_return(NeReLaBasic& vm) {
    // Evaluate the return value expression, if any.
    BasicValue return_val = false; // Default return value for functions without an explicit return value.
    Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

    // Check if there is an expression to evaluate after the RETURN keyword.
    if (next_token != Tokens::ID::C_CR && next_token != Tokens::ID::C_COLON && next_token != Tokens::ID::ENDFUNC && next_token != Tokens::ID::ENDSUB) {
        return_val = vm.evaluate_expression();
        if (Error::get() != 0) {
            if (vm.current_task) vm.current_task->status = TaskStatus::ERRORED;
            return;
        }
    }

    // For ALL returns (sync or async), set the global RETVAL.
    // This ensures that synchronous calls get their return value correctly.
    // The expression evaluator that called the function will read this value.
    vm.variables["RETVAL"] = return_val;

    // Pop the stack frame for the returning function.
    if (vm.call_stack.empty()) {
        Error::set(25, vm.runtime_current_line, "RETURN without function call.");
        if (vm.current_task) vm.current_task->status = TaskStatus::ERRORED;
        return;
    }
    NeReLaBasic::StackFrame frame = vm.call_stack.back();
    vm.call_stack.pop_back();

    // Restore the FOR loop stack to its state before the function call.
    if (vm.for_stack.size() > frame.for_stack_size_on_entry) {
        vm.for_stack.resize(frame.for_stack_size_on_entry);
    }

    // --- UNIFIED CONTEXT RESTORE ---
    // This fixes the bug where pcode was not restored for synchronous calls.
    if (vm.call_stack.empty() && frame.is_async_call) {
        vm.current_task->status = TaskStatus::COMPLETED;
        // By setting the status, we signal the main scheduler to stop
        // executing this task. The scheduler will handle cleanup.
    }
    else {
        vm.pcode = frame.return_pcode;
        vm.active_p_code = frame.return_p_code_ptr;
        vm.active_function_table = frame.previous_function_table_ptr;
    }

    // --- TASK COMPLETION CHECK (Async-specific) ---
    // Additionally, if this return causes a background task's call stack to become empty,
    // mark that task as completed.
    if (vm.current_task != nullptr && frame.is_async_call && vm.call_stack.empty()) {
        vm.current_task->result = return_val; // Store the final result in the task object.
        vm.current_task->status = TaskStatus::COMPLETED;
    }
}

void Commands::do_endfunc(NeReLaBasic& vm) {
    // ENDFUNC is treated as a RETURN with a default value (false).
    do_return(vm);
}


// At runtime, SUB just jumps over the procedure body.
// This is identical to how FUNC works.
void Commands::do_sub(NeReLaBasic& vm) {
    Commands::do_func(vm);
}

void Commands::do_callsub(NeReLaBasic& vm) {
    std::string proc_name = to_upper(read_string(vm));
    int funcnotfound = false;
    if (!vm.active_function_table->count(proc_name)) {
        funcnotfound = true;
    }
    // Check if it's a COM method call by looking for a '.'
    if (proc_name.find('.') != std::string::npos && funcnotfound) {
#ifdef JDCOM
        // It's a dot-chain, so resolve it to get the object and the method name.
        auto [final_obj, final_method] = vm.resolve_dot_chain(proc_name);
        if (Error::get() != 0) return; // An error occurred during resolution

        // Ensure we have a COM object to call a method on.
        if (!std::holds_alternative<ComObject>(final_obj)) {
            Error::set(15, vm.runtime_current_line, "Methods can only be called on objects.");
            return;
        }

        IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
        if (!pDisp) {
            Error::set(1, vm.runtime_current_line, "Uninitialized COM object.");
            return;
        }

        // For a procedure call, there are no arguments and we ignore the return value.
        std::vector<BasicValue> com_args; // Empty args
        _variant_t result_vt;             // We discard the result

        // Invoke as a method.
        HRESULT hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_METHOD);
        if (FAILED(hr)) {
            Error::set(12, vm.runtime_current_line, "Failed to call COM method '" + final_method + "'");
            return;
        }
#else
        Error::set(22, vm.runtime_current_line, "Unknown procedure: " + upper_identifier);
#endif
    }
    else {
        if (funcnotfound) {
            Error::set(22, vm.runtime_current_line); return;
        }
        const auto& proc_info = vm.active_function_table->at(proc_name);
        std::vector<BasicValue> args;

        Tokens::ID token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

        if (token != Tokens::ID::C_CR) {
            if (proc_info.arity == -1 || proc_info.arity > 0) {
                while (true) {
                    args.push_back(vm.evaluate_expression());
                    if (Error::get() != 0) return;
                    Tokens::ID separator = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
                    if (separator == Tokens::ID::C_CR) break;
                    if (separator != Tokens::ID::C_COMMA) { Error::set(1, vm.runtime_current_line); return; }
                    vm.pcode++;
                }
            }
            else {
                vm.pcode++;
                vm.pcode++;
            }
        }

        if (proc_info.arity != -1 && args.size() != proc_info.arity) {
            Error::set(26, vm.runtime_current_line); return;
        }

        vm.execute_function_for_value(proc_info, args);
    }
}

// At runtime, ENDSUB handles returning from a procedure call.
// It pops the call stack and restores the program counter.
void Commands::do_endsub(NeReLaBasic& vm) {
    do_return(vm);
}

// This command will expect a function name string directly after it.
void Commands::do_onerrorcall(NeReLaBasic& vm) {
    // Read the function name from bytecode
    if (!vm.current_task) {
        Error::set(1, vm.runtime_current_line, "ON ERROR can only be used in a running program.");
        return;
    }
    std::string func_name = to_upper(read_string(vm)); // read_string handles pcode increment

    // Check if the function actually exists
    // (It must be a SUB, not a FUNC, as it's called as a procedure)
    if (vm.main_function_table.count(func_name) && vm.main_function_table.at(func_name).is_procedure) {
        vm.current_task->error_handler_function_name = func_name;
        vm.current_task->error_handler_active = true;
    }
    else {
        // Error if function not found or it's not a procedure (SUB)
        Error::set(22, vm.runtime_current_line); // Undefined function
    }
}

void Commands::do_resume(NeReLaBasic& vm) {
    // Clear the error state in the Error module (so program can continue normally)
    Error::clear();
    vm.current_task->error_handler_active = false; // Deactivate handler after RESUME

    Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

    if (next_token == Tokens::ID::NEXT) {
        vm.pcode++; // Consume NEXT token

        // Restore the execution context exactly as it was before the error handler was called
        vm.pcode = vm.resume_pcode_next_statement; // Points to the line/statement that caused the error
        // The main loop in execute() will then re-evaluate it and proceed.
        vm.runtime_current_line = vm.resume_runtime_line;
        vm.active_p_code = vm.resume_p_code_ptr;
        vm.active_function_table = vm.resume_function_table_ptr;
        vm.call_stack = vm.resume_call_stack_snapshot;
        vm.for_stack = vm.resume_for_stack_snapshot;

    }
    else if (next_token == Tokens::ID::NOCMD || next_token == Tokens::ID::C_CR) {
        // Simple RESUME (like RESUME 0 or RESUME current line)
        // Restore context as for RESUME NEXT
        vm.pcode = vm.resume_pcode;
        vm.runtime_current_line = vm.resume_runtime_line;
        vm.active_p_code = vm.resume_p_code_ptr;
        vm.active_function_table = vm.resume_function_table_ptr;
        vm.call_stack = vm.resume_call_stack_snapshot;
        vm.for_stack = vm.resume_for_stack_snapshot;

    }
    else if (next_token == Tokens::ID::STRING) { // RESUME "LABEL"
        vm.pcode++; // Consume STRING token
        std::string target_label = read_string(vm);
        if (vm.compiler->label_addresses.count(target_label)) {
            vm.pcode = vm.compiler->label_addresses[target_label];
            // For a GOTO-style resume, clear the stack to prevent unexpected returns
            vm.call_stack.clear();
            vm.for_stack.clear();
        }
        else {
            Error::set(11, vm.runtime_current_line); // Undefined label
        }
    }
    else {
        Error::set(1, vm.runtime_current_line); // Syntax Error
    }
    // The main execution loop will then continue from the new vm.pcode value.
}

void Commands::do_do(NeReLaBasic& vm) {
    // Peek the next byte to see if it's a WHILE or UNTIL token.
    Tokens::ID next_byte_as_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]); // Consume WHILE/UNTIL token

    if (next_byte_as_token == Tokens::ID::WHILE || next_byte_as_token == Tokens::ID::UNTIL) {
        // This is a pre-test loop.
        Tokens::ID condition_type = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]); 

        // Read the 2-byte jump-past-loop address (this was the placeholder the compiler reserved)
        uint16_t jump_target_if_false = (*vm.active_p_code)[vm.pcode] | ((*vm.active_p_code)[vm.pcode + 1] << 8);
        vm.pcode += 2; // Consume the 2 bytes of the target address

        BasicValue condition_result = vm.evaluate_expression();
        if (Error::get() != 0) return;

        bool condition_met = to_bool(condition_result);
        bool should_jump_past_loop = false;

        if (condition_type == Tokens::ID::WHILE) {
            should_jump_past_loop = !condition_met; // Jump if WHILE condition is FALSE
        }
        else { // UNTIL
            should_jump_past_loop = condition_met;  // Jump if UNTIL condition is TRUE
        }

        if (should_jump_past_loop) {
            vm.pcode = jump_target_if_false; // Jump past the entire loop
        }
        // If not jumping, execution continues into the loop body
    }
    // If it's not WHILE/UNTIL, it's an unconditional DO or a post-test DO.
    // Execution simply falls through to the loop body.
}

void Commands::do_loop(NeReLaBasic& vm) {
    Tokens::ID token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);
    if (token == Tokens::ID::WHILE || token == Tokens::ID::UNTIL) //skip token WHILE/UNTIL
        vm.pcode++;

    // Read the loop metadata written by the tokenizer
    bool is_pre_test = static_cast<bool>((*vm.active_p_code)[vm.pcode++]);
    Tokens::ID condition_type = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode++]);
    uint16_t loop_start_pcode_addr = (*vm.active_p_code)[vm.pcode] | ((*vm.active_p_code)[vm.pcode + 1] << 8);
    vm.pcode += 2;

    // For pre-test loops, the patching was done at compile time.
    // So if it's a pre-test loop, we don't need to read an extra 2 bytes here.
    // The previous 'do_do' already handled the conditional jump.
    // This 'do_loop' for a pre-test only needs to jump back unconditionally.

    bool should_jump_back = true; // Default for DO...LOOP and DO WHILE/UNTIL...LOOP

    if (!is_pre_test && condition_type != Tokens::ID::NOCMD) { // This is a post-test loop (DO...LOOP WHILE/UNTIL)
        BasicValue condition_result = vm.evaluate_expression();
        if (Error::get() != 0) return;

        bool condition_met = to_bool(condition_result);

        if (condition_type == Tokens::ID::WHILE) {
            should_jump_back = condition_met; // Jump back if WHILE condition is TRUE
        }
        else { // UNTIL
            should_jump_back = !condition_met; // Jump back if UNTIL condition is FALSE
        }
    }
    // If it's an unconditional DO...LOOP or a pre-test loop, `should_jump_back` remains true.

    if (should_jump_back) {
        vm.pcode = loop_start_pcode_addr; // Jump back to the start of the loop body
    }
    else {
        // Loop finished for a post-test loop, or pre-test loop already handled the jump.
        // Execution simply falls through to the next statement after LOOP.
    }
}

// Jumps execution to the address immediately following the corresponding NEXT statement.
void Commands::do_exit_for(NeReLaBasic& vm) {
    // Read the 2-byte jump address that was patched by the tokenizer.
    uint8_t lsb = (*vm.active_p_code)[vm.pcode++];
    uint8_t msb = (*vm.active_p_code)[vm.pcode++];
    uint16_t jump_target = (msb << 8) | lsb;

    // The FOR stack for this loop is still active. We must pop it
    // to correctly clean up the loop state before jumping out.
    if (!vm.for_stack.empty()) {
        vm.for_stack.pop_back();
    }
    else {
        // This should not happen if the tokenizer works correctly, but it's a safe-guard.
        Error::set(1, vm.runtime_current_line, "EXIT FOR without active FOR loop.");
        return;
    }

    vm.pcode = jump_target;
}

// Jumps execution to the address immediately following the corresponding LOOP statement.
void Commands::do_exit_do(NeReLaBasic& vm) {
    // Read the 2-byte jump address that was patched by the tokenizer.
    uint8_t lsb = (*vm.active_p_code)[vm.pcode++];
    uint8_t msb = (*vm.active_p_code)[vm.pcode++];
    uint16_t jump_target = (msb << 8) | lsb;

    // The DO stack for this loop is still active. Pop it for cleanup.
    //if (!vm.do_loop_stack.empty()) {
    //    vm.do_loop_stack.pop_back();
    //}
    //else {
    //    Error::set(1, vm.runtime_current_line, "EXIT DO without active DO loop.");
    //    return;
    //}

    vm.pcode = jump_target;
}


void Commands::do_edit(NeReLaBasic& vm) {
    std::string filename_to_edit;

    if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]) == Tokens::ID::STRING) {
        vm.pcode++;
        filename_to_edit = read_string(vm);

        std::ifstream infile(filename_to_edit);
        if (infile) {
            vm.source_lines.clear();
            std::string line;
            while (std::getline(infile, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                vm.source_lines.push_back(line);
            }
        }
        else {
            TextIO::print("New file: " + filename_to_edit + "\n");
        }
    }

    // MODIFIED: Pass the filename to the editor's constructor
    TextEditor editor(vm.source_lines, filename_to_edit);
    editor.run();

    // The old save prompt is no longer needed here, as saving is handled inside the editor
}

void Commands::do_list(NeReLaBasic& vm) {
    // Iterate through the vector and print with 1-based line numbers for readability
    for (size_t i = 0; i < vm.source_lines.size(); ++i) {
        TextIO::print(std::to_string(i + 1) + ": " + vm.source_lines[i] + "\n");
    }
}

void Commands::do_load(NeReLaBasic& vm) {
    // LOAD expects a filename
    if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]) != Tokens::ID::STRING) {
        Error::set(1, vm.runtime_current_line);
        return;
    }
    vm.pcode++; // Consume STRING token
    std::string filename = read_string(vm); // read_string now reads from (*vm.active_p_code)

    std::ifstream infile(filename); // Open in text mode
    if (!infile) {
        Error::set(6, vm.runtime_current_line);
        return;
    }

    TextIO::print("LOADING " + filename + "\n");
    // Read the entire file into the source_code string
    vm.source_lines.clear();
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vm.source_lines.push_back(line);
    }
}

void Commands::do_save(NeReLaBasic& vm) {
    if (static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]) != Tokens::ID::STRING) {
        Error::set(1, vm.runtime_current_line);
        return;
    }
    vm.pcode++; // Consume STRING token
    std::string filename = read_string(vm); // read_string now reads from (*vm.active_p_code)

    std::ofstream outfile(filename); // Open in text mode
    if (!outfile) {
        Error::set(12, vm.runtime_current_line);
        return;
    }

    TextIO::print("SAVING " + filename + "\n");
    // Write the source_code string directly to the file
    for (const auto& line : vm.source_lines) {
        outfile << line << '\n';
    }
}

void Commands::do_compile(NeReLaBasic& vm) {
    std::stringstream ss;
    for (const auto& line : vm.source_lines) {
        ss << line << '\n';
    }
    std::string source_to_compile = ss.str();

    // Compile into the main program buffer
    TextIO::print("Compiling...\n");
    if (vm.compiler->tokenize_program(vm,vm.program_p_code, source_to_compile) == 0) {
        if (!vm.compiler->if_stack.empty()) {
            // There are unclosed IF blocks. Get the line number of the last one.
            uint16_t error_line = vm.compiler->if_stack.back().source_line;
            Error::set(4, error_line); // New Error: Missing ENDIF
        }
        else {
            TextIO::print("OK. Program compiled to " + std::to_string(vm.program_p_code.size()) + " bytes.\n");
        }
    }
    else {
        // Error message is printed by tokenize_program
        TextIO::print("Compilation failed.\n");
    }
}

// Commands.cpp

void Commands::do_stop(NeReLaBasic& vm) {
    TextIO::print("\nBreak in line " + std::to_string(vm.runtime_current_line) + "\n");
    std::string captured_output = "N/A";
    bool paused = true;
    std::string inputLine;

    while (paused) {
        // We must clear any error from the previous debug command.
        Error::clear();
        TextIO::print("Ready (paused)\n? ");

        if (!std::getline(std::cin, inputLine)) {
            paused = false;
            std::cin.clear();
            continue;
        }

        // --- Handle Meta-Commands First ---
        std::string command_str = inputLine;
        StringUtils::trim(command_str);
        if (to_upper(command_str) == "RESUME") {
            paused = false;
            TextIO::print("Resuming...\n");
            continue;
        }

        // --- If not a meta-command, tokenize and execute it ---

        // Save the essential pointers of the paused program.
        auto original_pcode = vm.pcode;
        const auto* original_active_pcode = vm.active_p_code;

        // Tokenize the direct-mode line into its own p-code buffer.
        vm.direct_p_code.clear();
        if (vm.compiler->tokenize(vm, inputLine, 0, vm.direct_p_code, *vm.active_function_table) != 0) {
            Error::print(); // Print tokenization error
            continue;       // And prompt again
        }

        // Execute the single command from the direct_p_code buffer.
        // We are NOT in resume_mode for this single command execution.
        vm.execute_main_program(vm.direct_p_code, false);

        // Restore the pointers to the main program's state.
        vm.pcode = original_pcode;
        vm.active_p_code = original_active_pcode;

        // If the command itself caused a runtime error, print it.
        if (Error::get() != 0) {
            Error::print();
        }
    }
}

void Commands::do_run(NeReLaBasic& vm) {

    do_compile(vm);
    if (!vm.compiler->do_loop_stack.empty()) {
        // There are unclosed DO loops. Get the line number of the last one.
        uint16_t error_line = vm.compiler->do_loop_stack.back().source_line;
        Error::set(14, error_line); // Unclosed loop
    }
    if (!vm.compiler->if_stack.empty()) {
        // There are unclosed Ifs. Get the line number of the last one.
        uint16_t error_line = vm.compiler->if_stack.back().source_line;
        Error::set(4, error_line); // Unclosed for
    }

    if (Error::get() != 0) {
        Error::print();
        return;
    }

    // Clear variables and prepare for a clean run
    vm.variables.clear();
    vm.call_stack.clear();
    vm.for_stack.clear();
    Error::clear();
    vm.is_stopped = false; // Reset the stopped state


    vm.active_function_table = &vm.main_function_table;

    TextIO::print("Running...\n");
    // Execute from the main program buffer
    vm.execute_main_program(vm.program_p_code, false);

    // If the execution resulted in an error, print it
    if (Error::get() != 0) {
        Error::print();
        Error::clear();
    }
    vm.active_function_table = &vm.main_function_table;
}

void Commands::do_tron(NeReLaBasic& vm) {
    vm.trace = 1;
    TextIO::print("TRACE ON\n");
}

void Commands::do_troff(NeReLaBasic& vm) {
    vm.trace = 0;
    TextIO::print("TRACE OFF\n");
}

void Commands::do_dump(NeReLaBasic& vm) {
    // Peek at the next token to see if an argument was provided.
    Tokens::ID next_token = static_cast<Tokens::ID>((*vm.active_p_code)[vm.pcode]);

    if (next_token == Tokens::ID::NOCMD || next_token == Tokens::ID::C_CR) {
        // Case 1: No argument provided, dump the main program p-code.
        dump_p_code(vm.program_p_code, "main program");
        return;
    }

    // An argument was provided. Evaluate it as an expression.
    // This will handle `DUMP GLOBAL` where GLOBAL is parsed as a variable/identifier.
    BasicValue arg_val = vm.evaluate_expression();
    if (Error::get() != 0) return;

    std::string arg_str = to_upper(to_string(arg_val));

    if (arg_str == "GLOBAL") {
        TextIO::print("--- Global Variables ---\n");
        if (vm.variables.empty()) {
            TextIO::print("(No global variables defined)\n");
        }
        else {
            for (const auto& pair : vm.variables) {
                TextIO::print(pair.first + " = " + to_string(pair.second) + "\n");
            }
        }
    }
    else if (arg_str == "LOCAL") {
        TextIO::print("--- Local Variables ---\n");
        if (vm.call_stack.empty()) {
            TextIO::print("(Not inside a function/subroutine)\n");
        }
        else {
            const auto& locals = vm.call_stack.back().local_variables;
            if (locals.empty()) {
                TextIO::print("(No local variables in current scope)\n");
            }
            else {
                for (const auto& pair : locals) {
                    TextIO::print(pair.first + " = " + to_string(pair.second) + "\n");
                }
            }
        }
    }
    else {
        // Fallback to original behavior: dump p-code for a module.
        if (vm.compiled_modules.count(arg_str)) {
            dump_p_code(vm.compiled_modules.at(arg_str).p_code, arg_str);
        }
        else {
            TextIO::print("? Error: Module '" + arg_str + "' not found, or invalid DUMP argument.\n");
        }
    }
}
