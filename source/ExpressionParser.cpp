#include "NeReLaBasic.hpp"
#include "Error.hpp"
#include "Commands.hpp" // For to_string, to_double, etc.
#include "Types.hpp"
#include "Tokens.hpp"
#include "BuiltinFunctions.hpp" // For tensor/array math functions

// Forward declarations for tensor math from other files if needed
BasicValue tensor_add(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_subtract(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_elementwise_multiply(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_power(NeReLaBasic& vm, const BasicValue& base, const BasicValue& exponent);
BasicValue tensor_scalar_divide(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
std::shared_ptr<Array> array_add(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);
std::shared_ptr<Array> array_subtract(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);

// A generic helper to apply any binary operation element-wise.
// It handles scalar-scalar, array-scalar, scalar-array, and array-array operations.
static BasicValue apply_binary_op(
    const BasicValue& left,
    const BasicValue& right,
    const std::function<BasicValue(const BasicValue&, const BasicValue&)>& op
) {
    // Case 1: Array-Array operation
    if (std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
        const auto& left_ptr = std::get<std::shared_ptr<Array>>(left);
        const auto& right_ptr = std::get<std::shared_ptr<Array>>(right);
        if (!left_ptr || !right_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }
        if (left_ptr->shape != right_ptr->shape) { Error::set(15, 0, "Array shapes must match for element-wise operation."); return {}; }

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = left_ptr->shape;
        result_ptr->data.reserve(left_ptr->data.size());

        for (size_t i = 0; i < left_ptr->data.size(); ++i) {
            result_ptr->data.push_back(op(left_ptr->data[i], right_ptr->data[i]));
        }
        return result_ptr;
    }
    // Case 2: Array-Scalar operation
    else if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
        const auto& left_ptr = std::get<std::shared_ptr<Array>>(left);
        if (!left_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = left_ptr->shape;
        result_ptr->data.reserve(left_ptr->data.size());

        for (const auto& elem : left_ptr->data) {
            result_ptr->data.push_back(op(elem, right));
        }
        return result_ptr;
    }
    // Case 3: Scalar-Array operation
    else if (std::holds_alternative<std::shared_ptr<Array>>(right)) {
        const auto& right_ptr = std::get<std::shared_ptr<Array>>(right);
        if (!right_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = right_ptr->shape;
        result_ptr->data.reserve(right_ptr->data.size());

        for (const auto& elem : right_ptr->data) {
            result_ptr->data.push_back(op(left, elem));
        }
        return result_ptr;
    }
    // Case 4: Scalar-Scalar operation
    else {
        return op(left, right);
    }
}

// --- CLASS MEMBER FUNCTION FOR PARSING ARRAY LITERALS ---
BasicValue NeReLaBasic::parse_array_literal() {
    // We expect the current token to be '['
    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTBRACKET) {
        Error::set(1, runtime_current_line); // Should not happen if called correctly
        return {};
    }

    std::vector<BasicValue> elements;
    // Loop until we find the closing bracket ']'
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
        while (true) {
            // Recursively call evaluate_expression, which can now handle nested literals
            elements.push_back(evaluate_expression());
            if (Error::get() != 0) return {};

            Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
            if (separator == Tokens::ID::C_RIGHTBRACKET) break;
            if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; }
            pcode++;
        }
    }
    //if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
    //    Error::set(16, runtime_current_line); return false;
    //}
    //else
    pcode++; // Consume ']'

    // --- Now, construct the Array object from the parsed elements ---
    auto new_array_ptr = std::make_shared<Array>();
    if (elements.empty()) {
        new_array_ptr->shape = { 0 };
        return new_array_ptr;
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(elements[0])) {
        const auto& first_sub_array_ptr = std::get<std::shared_ptr<Array>>(elements[0]);
        new_array_ptr->shape.push_back(elements.size());
        if (first_sub_array_ptr) {
            for (size_t dim : first_sub_array_ptr->shape) {
                new_array_ptr->shape.push_back(dim);
            }
        }

        for (const auto& el : elements) {
            if (!std::holds_alternative<std::shared_ptr<Array>>(el)) { Error::set(15, runtime_current_line); return{}; }
            const auto& sub_array_ptr = std::get<std::shared_ptr<Array>>(el);
            if (!sub_array_ptr || sub_array_ptr->shape != first_sub_array_ptr->shape) { Error::set(15, runtime_current_line); return{}; }
            new_array_ptr->data.insert(new_array_ptr->data.end(), sub_array_ptr->data.begin(), sub_array_ptr->data.end());
        }
    }
    else {
        new_array_ptr->shape = { elements.size() };
        new_array_ptr->data = elements;
    }
    return new_array_ptr;
}

// --- FUNCTION TO PARSE MAP LITERALS ---
BasicValue NeReLaBasic::parse_map_literal() {
    // We expect the current token to be '{'
    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTBRACE) {
        Error::set(1, runtime_current_line, "Expected '{' to start map literal.");
        return {};
    }

    auto new_map_ptr = std::make_shared<Map>();

    // Check for an empty map {}
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTBRACE) {
        pcode++; // Consume '}'
        return new_map_ptr;
    }

    while (true) {
        // 1. Parse Key (must be a string)
        BasicValue key_val = evaluate_expression();
        if (Error::get() != 0) return {};
        std::string key_str = to_string(key_val);

        // 2. Expect Colon
        if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_COLON) {
            Error::set(1, runtime_current_line, "Expected ':' after map key.");
            return {};
        }

        // 3. Parse Value
        BasicValue value = evaluate_expression();
        if (Error::get() != 0) return {};

        // 4. Add to map
        new_map_ptr->data[key_str] = value;

        // 5. Check for separator
        Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (separator == Tokens::ID::C_RIGHTBRACE) {
            pcode++; // Consume '}'
            break; // End of map
        }
        if (separator != Tokens::ID::C_COMMA) {
            Error::set(1, runtime_current_line, "Expected ',' or '}' in map literal.");
            return {};
        }
        pcode++; // Consume ','
    }

    return new_map_ptr;
}

std::vector<BasicValue> NeReLaBasic::parse_argument_list() {
    std::vector<BasicValue> args;
    // Expect '(' to start the argument list
    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) {
        Error::set(1, runtime_current_line, "Expected '('.");
        return {};
    }

    // Check for empty argument list: ()
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTPAREN) {
        pcode++; // Consume ')'
        return args;
    }

    // Loop through the arguments
    while (true) {
        // *** Check for the placeholder '?' ***
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::PLACEHOLDER) {
            pcode++; // Consume '?'
            if (!is_in_pipe_call) {
                Error::set(1, runtime_current_line, "Placeholder '?' can only be used on the right side of a pipe '|>' operator.");
                return {};
            }
            args.push_back(piped_value_for_call);
        }
        else {
            // Original logic: evaluate the expression for the argument.
            args.push_back(evaluate_expression());
        }

        if (Error::get() != 0) return {};

        Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (separator == Tokens::ID::C_RIGHTPAREN) {
            break; // End of list
        }
        if (separator != Tokens::ID::C_COMMA) {
            Error::set(1, runtime_current_line, "Expected ',' or ')' in argument list.");
            return {};
        }
        pcode++; // Consume ','
    }

    pcode++; // Consume ')'
    return args;
}

// Level 5: Handles highest-precedence items
BasicValue NeReLaBasic::parse_primary() {
    BasicValue current_value;
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::THREAD) {
        pcode++; // Consume BSYNC
        // Expect a function call right after
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::CALLFUNC) {
            Error::set(1, runtime_current_line, "THREAD must be followed by a function call.");
            return {};
        }
        pcode++; // Consume CALLFUNC

        std::string func_name = to_upper(read_string(*this));
        if (!active_function_table->count(func_name)) {
            Error::set(22, runtime_current_line, "Function not found for THREAD: " + func_name);
            return {};
        }
        const auto& func_info = active_function_table->at(func_name);

        // Parse arguments just like a normal function call
        std::vector<BasicValue> args;
        if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
            while (true) {
                args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (separator == Tokens::ID::C_RIGHTPAREN) break;
                if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
            }
        }
        pcode++; // Consume ')'

        // Launch the function in a background thread and get the handle
        return launch_bsync_function(func_info, args);
    }
    else if (token == Tokens::ID::OP_START_TASK) {
        pcode++; // consume OP_START_TASK
        std::string func_name = to_upper(read_string(*this));
        if (!active_function_table->count(func_name)) {
            Error::set(22, runtime_current_line, "Async function not found: " + func_name);
            return {};
        }
        const auto& func_info = active_function_table->at(func_name);
        std::vector<BasicValue> args;
        if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
            while (true) {
                args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (separator == Tokens::ID::C_RIGHTPAREN) break;
                if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
            }
        }
        pcode++; // Consume ')'

        auto new_task = std::make_shared<Task>();
        new_task->id = next_task_id++;
        new_task->status = TaskStatus::RUNNING;
        if (!func_info.module_name.empty() && compiled_modules.count(func_info.module_name)) {
            new_task->p_code_ptr = &compiled_modules.at(func_info.module_name).p_code;
        }
        else {
            new_task->p_code_ptr = &program_p_code;
        }
        new_task->p_code_counter = func_info.start_pcode + 1; //Dirty JD was here!

        StackFrame frame;
        frame.function_name = func_name;
        frame.is_async_call = func_info.is_async;
        frame.previous_function_table_ptr = this->active_function_table; //JD ??
        for (size_t i = 0; i < func_info.parameter_names.size(); ++i) {
            if (i < args.size()) frame.local_variables[func_info.parameter_names[i]] = args[i];
        }
        new_task->call_stack.push_back(frame);
        task_queue[new_task->id] = new_task;
        current_value = TaskRef{ new_task->id };

    }
    else {
        if (token == Tokens::ID::VARIANT || token == Tokens::ID::INT || token == Tokens::ID::STRVAR) {
            pcode++;
            std::string var_or_qual_name = to_upper(read_string(*this));
            size_t dot_pos = var_or_qual_name.find('.');

            if (dot_pos != std::string::npos) {
                std::string enum_name = var_or_qual_name.substr(0, dot_pos);
                std::string member_name = var_or_qual_name.substr(dot_pos + 1);

                // Check if the first part is a known enum
                auto enum_it = user_defined_enums.find(enum_name);
                if (enum_it != user_defined_enums.end()) {
                    // It is an enum! Now check for the member.
                    auto member_it = enum_it->second.find(member_name);
                    if (member_it != enum_it->second.end()) {
                        // Member found! The value is the integer.
                        current_value = static_cast<double>(member_it->second);
                    }
                    else {
                        Error::set(3, runtime_current_line, "Enum member '" + member_name + "' not found in '" + enum_name + "'.");
                        return {};
                    }
                }
                else {
                    auto [final_obj, final_member] = resolve_dot_chain(var_or_qual_name);
                    if (Error::get() != 0) return {};
                    if (final_member.empty()) { current_value = final_obj; }
                    else if (std::holds_alternative<std::shared_ptr<Map>>(final_obj)) {
                        auto& map_ptr = std::get<std::shared_ptr<Map>>(final_obj);
                        if (map_ptr && map_ptr->data.count(final_member)) {
                            current_value = map_ptr->data.at(final_member);
                        }
                        else { Error::set(3, runtime_current_line, "Member not found: " + final_member); return {}; }
                    }
                    else if (std::holds_alternative<std::shared_ptr<Tensor>>(final_obj)) {
                        const auto& tensor_ptr = std::get<std::shared_ptr<Tensor>>(final_obj);
                        if (!tensor_ptr) {
                            Error::set(3, runtime_current_line, "Cannot access member of a null Tensor.");
                            return {};
                        }
                        if (final_member == "GRAD") {
                            if (tensor_ptr->grad) {
                                current_value = tensor_ptr->grad;
                            }
                            else {
                                // If .grad is accessed but doesn't exist yet, return a valid but empty Tensor.
                                current_value = std::make_shared<Tensor>();
                            }
                        }
                        else {
                            Error::set(3, runtime_current_line, "Invalid member for Tensor: " + final_member + ". Only .grad is supported.");
                            return {};
                        }
                    }
#ifdef JDCOM
                    else if (std::holds_alternative<ComObject>(final_obj)) {
                        IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
                        _variant_t result_vt;
                        HRESULT hr = invoke_com_method(pDisp, final_member, {}, result_vt, DISPATCH_PROPERTYGET);
                        if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM property not found: " + final_member); return {}; }
                        current_value = variant_t_to_basic_value(result_vt, *this);
                    }
#endif
                    else { Error::set(15, runtime_current_line, "Invalid object for dot notation."); return {}; }
                }
            }
            else {
                current_value = get_variable(*this, var_or_qual_name);
            }
        }
        else if (token == Tokens::ID::CALLFUNC) {
            pcode++;
            std::string identifier_being_called = to_upper(read_string(*this));
            std::string real_func_to_call = identifier_being_called;

            if (!active_function_table->count(real_func_to_call)) {
                BasicValue& var = get_variable(*this, identifier_being_called);
                if (std::holds_alternative<FunctionRef>(var)) {
                    real_func_to_call = std::get<FunctionRef>(var).name;
                }
            }

            if (active_function_table->count(real_func_to_call)) {
                const auto& func_info = active_function_table->at(real_func_to_call);

                std::vector<BasicValue> args = parse_argument_list();
                if (Error::get() != 0) return {};

                if (func_info.arity != -1 && args.size() != func_info.arity) {
                    Error::set(26, runtime_current_line);
                    return {};
                }
                current_value = execute_function_for_value(func_info, args);
            }
            else if (identifier_being_called.find('.') != std::string::npos) {
                size_t dot_pos = identifier_being_called.find('.');
                std::string object_name = to_upper(identifier_being_called.substr(0, dot_pos));
                std::string method_name = to_upper(identifier_being_called.substr(dot_pos + 1));
                BasicValue& object_instance_val = get_variable(*this, object_name);
                auto jt = std::holds_alternative<std::shared_ptr<Map>>(object_instance_val);
                if (variables.contains(object_name) && jt) {
                    // 1. Get the object instance
                    //BasicValue& object_instance_val = get_variable(*this, object_name);
                    //if (!std::holds_alternative<std::shared_ptr<Map>>(object_instance_val)) {
                    //    Error::set(15, runtime_current_line, "Methods can only be called on objects.");
                    //    return {};
                    //}
                    auto object_instance_ptr = std::get<std::shared_ptr<Map>>(object_instance_val);

                    // 2. Get its type and find the method
                    std::string type_name = object_instance_ptr->type_name_if_udt;
                    if (type_name.empty() || !user_defined_types.count(type_name)) {
                        Error::set(15, runtime_current_line, "Object has no type information.");
                        return {};
                    }
                    const auto& type_info = user_defined_types.at(type_name);
                    if (!type_info.methods.count(method_name)) {
                        Error::set(22, runtime_current_line, "Method '" + method_name + "' not found in type '" + type_name + "'.");
                        return {};
                    }

                    // 3. Get the mangled function name and its FunctionInfo
                    std::string mangled_name = type_name + "." + method_name;
                    const auto& func_info = active_function_table->at(mangled_name);

                    // 4. Parse arguments
                    auto args = parse_argument_list();
                    if (Error::get() != 0) return {};

                    // 5. *** SET THE 'THIS' CONTEXT ***
                    this_stack.push_back(object_instance_ptr);

                    // 6. Execute the function
                    current_value = execute_function_for_value(func_info, args);

                    // 7. *** CLEAN UP THE 'THIS' CONTEXT ***
                    this_stack.pop_back();
                }
#ifdef JDCOM
                else
                {
                    auto [final_obj, final_method] = resolve_dot_chain(identifier_being_called);
                    if (Error::get() != 0) return {};
                    if (!std::holds_alternative<ComObject>(final_obj)) {
                        Error::set(15, runtime_current_line, "Methods can only be called on COM objects."); return {};
                    }
                    IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
                    if (!pDisp) { Error::set(1, runtime_current_line, "Uninitialized COM object."); return {}; }
                    std::vector<BasicValue> com_args;
                    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
                    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
                        while (true) {
                            com_args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                            Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                            if (separator == Tokens::ID::C_RIGHTPAREN) break;
                            if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
                        }
                    }
                    pcode++;
                    _variant_t result_vt;
                    HRESULT hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_METHOD);
                    if (FAILED(hr)) {
                        hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_PROPERTYGET);
                        if (FAILED(hr)) { Error::set(12, runtime_current_line, "Failed to call COM method or get property '" + final_method + "'"); return {}; }
                    }
                    current_value = variant_t_to_basic_value(result_vt, *this);
                }
#else
                Error::set(22, runtime_current_line, "Unknown function: " + identifier_being_called); return {};
#endif
            }
            else {
                Error::set(22, runtime_current_line, "Unknown function: " + real_func_to_call); return {};
            }
        }
        else if (token == Tokens::ID::THIS_KEYWORD) {
            pcode++; // Consume the token
            if (this_stack.empty()) {
                Error::set(1, runtime_current_line, "'this' can only be used inside a method.");
                return {};
            }
            // The current value is a reference to the object on top of the stack
            current_value = this_stack.back();
        }
        else if (token == Tokens::ID::JD_TRUE) {
            pcode++; current_value = true;
        }
        else if (token == Tokens::ID::JD_FALSE) {
            pcode++; current_value = false;
        }
        else if (token == Tokens::ID::NUMBER) {
            pcode++; double value;
            memcpy(&value, &(*active_p_code)[pcode], sizeof(double));
            pcode += sizeof(double);
            current_value = value;
        }
        else if (token == Tokens::ID::INTEGER_LITERAL) {
            pcode++;
            long long value;
            memcpy(&value, &(*active_p_code)[pcode], sizeof(long long));
            pcode += sizeof(long long);
            current_value = value;
        }
        else if (token == Tokens::ID::STRING) {
            pcode++; current_value = read_string(*this);
        }
        else if (token == Tokens::ID::CONSTANT) {
            pcode++; current_value = builtin_constants.at(read_string(*this));
        }
        else if (token == Tokens::ID::FUNCREF) {
            pcode++; current_value = FunctionRef{ to_upper(read_string(*this)) };
        }
        else if (token == Tokens::ID::C_LEFTBRACKET) {
            current_value = parse_array_literal();
        }
        else if (token == Tokens::ID::C_LEFTBRACE) {
            current_value = parse_map_literal();
        }
        else if (token == Tokens::ID::C_LEFTPAREN) {
            pcode++; current_value = evaluate_expression();
            if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTPAREN) { Error::set(18, runtime_current_line); return {}; }
        }
        else {
            Error::set(1, runtime_current_line);
            return {};
        }
        if (Error::get() != 0) return {};
    }

    // --- MAIN LOOP FOR HANDLING ACCESSORS LIKE [..], {..}, .member ---
    while (true) {
        Tokens::ID accessor_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

        if (accessor_token == Tokens::ID::C_LEFTBRACKET) { // Array index access: e.g., A[i, j]
            pcode++; // Consume '['

            // This now supports vectorized indexing for read access.
            std::vector<BasicValue> index_expressions;
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
                while (true) {
                    index_expressions.push_back(evaluate_expression());
                    if (Error::get() != 0) return {};

                    Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                    if (separator == Tokens::ID::C_RIGHTBRACKET) break;
                    if (separator != Tokens::ID::C_COMMA) {
                        Error::set(1, runtime_current_line, "Expected ',' or ']' in array index.");
                        return {};
                    }
                    pcode++; // Consume ','
                }
            }
            if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTBRACKET) {
                Error::set(16, runtime_current_line, "Missing ']' in array access.");
                return {};
            }

            if (std::holds_alternative<std::shared_ptr<Array>>(current_value)) {
                const auto& arr_ptr = std::get<std::shared_ptr<Array>>(current_value);
                if (!arr_ptr) { Error::set(15, runtime_current_line, "Attempt to index a null array."); return {}; }

                // NEW: Check if this is a vectorized or scalar read operation
                bool is_vectorized_read = false;
                for (const auto& expr : index_expressions) {
                    if (std::holds_alternative<std::shared_ptr<Array>>(expr)) {
                        is_vectorized_read = true;
                        break;
                    }
                }

                if (is_vectorized_read) {
                    // Vectorized Read Logic
                    long long num_reads = -1;
                    for (const auto& expr : index_expressions) {
                        if (std::holds_alternative<std::shared_ptr<Array>>(expr)) {
                            const auto& index_arr = std::get<std::shared_ptr<Array>>(expr);
                            if (num_reads == -1) {
                                num_reads = index_arr->data.size();
                            }
                            else if (num_reads != static_cast<long long>(index_arr->data.size())) {
                                Error::set(15, runtime_current_line, "Index arrays for reading must have the same length.");
                                return {};
                            }
                        }
                    }

                    if (num_reads == -1) {
                        Error::set(15, runtime_current_line, "Vectorized read requires at least one index array.");
                        return {};
                    }

                    auto result_ptr = std::make_shared<Array>();
                    result_ptr->shape = { (size_t)num_reads }; // The result is always a 1D vector
                    result_ptr->data.reserve(num_reads);

                    std::vector<size_t> current_coords(index_expressions.size());
                    for (long long i = 0; i < num_reads; ++i) {
                        for (size_t dim = 0; dim < index_expressions.size(); ++dim) {
                            if (std::holds_alternative<std::shared_ptr<Array>>(index_expressions[dim])) {
                                current_coords[dim] = static_cast<size_t>(to_double(std::get<std::shared_ptr<Array>>(index_expressions[dim])->data[i]));
                            }
                            else {
                                current_coords[dim] = static_cast<size_t>(to_double(index_expressions[dim]));
                            }
                        }
                        try {
                            size_t flat_index = arr_ptr->get_flat_index(current_coords);
                            result_ptr->data.push_back(arr_ptr->data[flat_index]);
                        }
                        catch (const std::exception&) {
                            Error::set(10, runtime_current_line, "Array index out of bounds during vectorized read.");
                            return {};
                        }
                    }
                    current_value = result_ptr; // The result of the operation is the new array
                }
                else {
                    // Original logic for scalar read
                    std::vector<size_t> indices;
                    for (const auto& expr : index_expressions) {
                        indices.push_back(static_cast<size_t>(to_double(expr)));
                    }
                    try {
                        size_t flat_index = arr_ptr->get_flat_index(indices);
                        if (flat_index >= arr_ptr->data.size()) {
                            throw std::out_of_range("Calculated index is out of bounds.");
                        }
                        BasicValue next_val = arr_ptr->data[flat_index];
                        current_value = std::move(next_val);
                    }
                    catch (const std::exception&) {
                        Error::set(10, runtime_current_line, "Array index out of bounds or dimension mismatch.");
                        return {};
                    }
                }
            }
            else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
                if (index_expressions.size() != 1) {
                    Error::set(15, runtime_current_line, "Multi-dimensional indexing is not supported for JSON objects."); return {};
                }
                size_t index = static_cast<size_t>(to_double(index_expressions[0]));
                const auto& json_ptr = std::get<std::shared_ptr<JsonObject>>(current_value);
                if (!json_ptr || !json_ptr->data.is_array() || index >= json_ptr->data.size()) { Error::set(10, runtime_current_line, "JSON index out of bounds."); return {}; }
                current_value = json_to_basic_value(json_ptr->data[index]);
            }
            else { Error::set(15, runtime_current_line, "Indexing '[]' can only be used on an Array."); return {}; }

        }
        else if (accessor_token == Tokens::ID::C_LEFTBRACE) { // Map key access: {key}
            pcode++; // Consume '{'
            BasicValue key_val = evaluate_expression();
            if (Error::get() != 0) return {};
            std::string key = to_string(key_val);
            if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTBRACE) { Error::set(17, runtime_current_line, "Missing '}' in map access."); return {}; }

            if (std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
                const auto& map_ptr = std::get<std::shared_ptr<Map>>(current_value);
                if (!map_ptr || map_ptr->data.find(key) == map_ptr->data.end()) { Error::set(3, runtime_current_line, "Map key not found: " + key); return {}; }
                BasicValue next_val = map_ptr->data.at(key);
                current_value = std::move(next_val);
            }
            else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
                const auto& json_ptr = std::get<std::shared_ptr<JsonObject>>(current_value);
                if (!json_ptr || !json_ptr->data.is_object() || !json_ptr->data.contains(key)) { Error::set(3, runtime_current_line, "JSON key not found: " + key); return {}; }
                current_value = json_to_basic_value(json_ptr->data.at(key));
            }
            else { Error::set(15, runtime_current_line, "Key access '{}' can only be used on a Map or JSON object."); return {}; }

        }
        else if (accessor_token == Tokens::ID::C_DOT) {
            pcode++; // Consume '.'
            Tokens::ID member_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
            if (member_token != Tokens::ID::VARIANT && member_token != Tokens::ID::INT && member_token != Tokens::ID::STRVAR && member_token != Tokens::ID::CALLFUNC) {
                Error::set(1, runtime_current_line, "Expected member name after '.'"); return {};
            }
            pcode++; // Consume the variant/int/strvar token
            std::string member_name = to_upper(read_string(*this));

            // --- Look ahead for a function call ---
            Tokens::ID after_member_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

            if (after_member_token == Tokens::ID::C_LEFTPAREN) {
                // --- Case A: It's a METHOD CALL, e.g., .GETALL() ---
                if (!std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
                    Error::set(15, runtime_current_line, "Methods can only be called on objects.");
                    return {};
                }
                auto object_instance_ptr = std::get<std::shared_ptr<Map>>(current_value);
                if (!object_instance_ptr) { Error::set(1, runtime_current_line, "Cannot call method on a null object."); return {}; }

                std::string type_name = object_instance_ptr->type_name_if_udt;
                if (type_name.empty() || !user_defined_types.count(type_name)) {
                    Error::set(15, runtime_current_line, "Object has no type information for method call."); return {};
                }
                const auto& type_info = user_defined_types.at(type_name);
                if (!type_info.methods.count(member_name)) {
                    Error::set(22, runtime_current_line, "Method '" + member_name + "' not found in type '" + type_name + "'."); return {};
                }

                std::string mangled_name = type_name + "." + member_name;
                const auto& func_info = active_function_table->at(mangled_name);

                auto args = parse_argument_list(); // This parses the '()' and arguments
                if (Error::get() != 0) return {};

                this_stack.push_back(object_instance_ptr);
                current_value = execute_function_for_value(func_info, args); // Execute and update current_value
                this_stack.pop_back();

            }
            else {
                // --- Case B: It's a DATA MEMBER ACCESS, e.g., .Name ---
                if (std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
                    const auto& map_ptr = std::get<std::shared_ptr<Map>>(current_value);
                    if (!map_ptr || map_ptr->data.find(member_name) == map_ptr->data.end()) { Error::set(3, runtime_current_line, "Member '" + member_name + "' not found in object."); return {}; }
                    current_value = map_ptr->data.at(member_name);
                }
#ifdef JDCOM
                else if (std::holds_alternative<ComObject>(current_value)) {
                    IDispatchPtr pDisp = std::get<ComObject>(current_value).ptr;
                    _variant_t result_vt;
                    HRESULT hr = invoke_com_method(pDisp, member_name, {}, result_vt, DISPATCH_PROPERTYGET);
                    if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM property '" + member_name + "' not found or failed to get."); return {}; }
                    current_value = variant_t_to_basic_value(result_vt, *this);
                }
#endif
                else { Error::set(15, runtime_current_line, "Member access '.' can only be used on an object."); return {}; }
            }
        }
        else {
            break; // No more accessors, break the loop
        }
    }
    return current_value;
}

// Level 5: Handles unary operators like - and NOT
BasicValue NeReLaBasic::parse_unary() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::AWAIT) {
        pcode++; // Consume AWAIT
        BasicValue task_ref_val = parse_unary(); // AWAIT has high precedence
        if (Error::get() != 0) return {};

        if (!std::holds_alternative<TaskRef>(task_ref_val)) {
            Error::set(15, runtime_current_line, "Can only AWAIT a TaskRef object.");
            return {};
        }
        int task_id_to_await = std::get<TaskRef>(task_ref_val).id;

        if (task_completed.count(task_id_to_await)) {
            auto& task_to_await = task_completed.at(task_id_to_await);
            if (task_to_await->status == TaskStatus::COMPLETED) {
                auto r = task_to_await->result;
                task_completed.erase(task_id_to_await);
                return r;
            }
            else {
                current_task->status = TaskStatus::PAUSED_ON_AWAIT;
                current_task->awaiting_task = task_to_await;
                current_task->yielded_execution = true;
                return false; // Dummy value, will be re-evaluated
            }
        }
        else {
            // Task not found, assume it's done.
            return false; // Return default value
        }
    }

    if (token == Tokens::ID::C_MINUS) {
        pcode++; // Consume the '-'
        BasicValue value = parse_unary(); // Evaluate the expression first

        return std::visit([](auto&& arg) -> BasicValue {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, long long>) {
                return -arg; // Integer negation
            }
            if constexpr (std::is_same_v<T, std::string>) {
                // If the value is a string, split it into an array of characters.
                const std::string& s = arg;
                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = { s.length() };
                result_ptr->data.reserve(s.length());
                for (char c : s) {
                    result_ptr->data.push_back(std::string(1, c));
                }
                return result_ptr;
            }
            // Fallback for doubles, bools, etc.
            return -to_double(arg); // Floating-point negation
            }, value);
    }
    if (token == Tokens::ID::NOT) {
        pcode++; // Consume 'NOT'
        // The result of NOT is always a boolean.
        return !to_bool(parse_unary());
    }

    // If there is no unary operator, parse the primary expression.
    return parse_primary();
}

BasicValue NeReLaBasic::parse_power() {
    BasicValue left = parse_unary();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::C_CARET) {
            pcode++;
            BasicValue right = parse_unary();

            if (std::holds_alternative<std::shared_ptr<Tensor>>(left)) {
                if (std::holds_alternative<std::shared_ptr<Tensor>>(right)) {
                    Error::set(1, runtime_current_line, "Tensor-to-Tensor power is not supported."); return {};
                }
                left = tensor_power(*this, left, right);
            }
            else {
                left = std::visit([this](auto&& l, auto&& r) -> BasicValue {
                    using LeftT = std::decay_t<decltype(l)>;
                    using RightT = std::decay_t<decltype(r)>;

                    auto array_op = [this](const auto& arr, double scalar, bool arr_is_left) -> BasicValue {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = arr->shape;
                        for (const auto& elem : arr->data) {
                            double arr_val = to_double(elem);
                            double res = arr_is_left ? std::pow(arr_val, scalar) : std::pow(scalar, arr_val);
                            result_ptr->data.push_back(res);
                        }
                        return result_ptr;
                        };

                    if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Shape mismatch for element-wise power."); return false; }
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = l->shape;
                        for (size_t i = 0; i < l->data.size(); ++i) { result_ptr->data.push_back(std::pow(to_double(l->data[i]), to_double(r->data[i]))); }
                        return result_ptr;
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) { return array_op(l, to_double(r), true); }
                    else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) { return array_op(r, to_double(l), false); }
                    else { return std::pow(to_double(l), to_double(r)); }
                    }, left, right);
            }
        }
        else { break; }
    }
    return left;
}

// Level 3: Handles *, /, and MOD
BasicValue NeReLaBasic::parse_factor() {
    BasicValue left = parse_power();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::C_ASTR || op == Tokens::ID::C_SLASH || op == Tokens::ID::C_BACKSLASH || op == Tokens::ID::MOD || op == Tokens::ID::FUNCREF) {
            pcode++;

            // --- Handle user-defined operators ---
            if (op == Tokens::ID::FUNCREF) {
                std::string func_name = to_upper(read_string(*this));

                BasicValue right = parse_power();

                if (active_function_table->count(func_name)) {
                    const auto& func_info = active_function_table->at(func_name);
                    if (func_info.arity != 2) {
                        Error::set(26, runtime_current_line, "Custom operator function '" + func_name + "' must take 2 arguments.");
                        return {};
                    }
                    // Case 1: Array @ Scalar
                    if (std::holds_alternative<std::shared_ptr<Array>>(left) && !std::holds_alternative<std::shared_ptr<Array>>(right)) {
                        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(left);
                        auto result_ptr = std::make_shared<Array>(); // Create a new array for the results
                        result_ptr->shape = arr_ptr->shape; // The result has the same shape

                        for (const auto& element : arr_ptr->data) {
                            // Call the BASIC function for each element against the scalar
                            BasicValue element_result = execute_function_for_value(func_info, { element, right });
                            if (Error::get() != 0) return {};
                            result_ptr->data.push_back(element_result);
                        }
                        left = result_ptr; // The new array is now our result
                    }
                    // Case 2: Scalar @ Array
                    else if (!std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
                        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(right);
                        auto result_ptr = std::make_shared<Array>();
                        result_ptr->shape = arr_ptr->shape;

                        for (const auto& element : arr_ptr->data) {
                            // Call the BASIC function for the scalar against each element
                            BasicValue element_result = execute_function_for_value(func_info, { left, element });
                            if (Error::get() != 0) return {};
                            result_ptr->data.push_back(element_result);
                        }
                        left = result_ptr;
                    }
                    // Case 3: Array @ Array
                    else if (std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
                        const auto& left_arr_ptr = std::get<std::shared_ptr<Array>>(left);
                        const auto& right_arr_ptr = std::get<std::shared_ptr<Array>>(right);

                        if (left_arr_ptr->shape != right_arr_ptr->shape) {
                            Error::set(15, runtime_current_line, "Array shape mismatch for operator '" + func_name + "'");
                            return {};
                        }

                        auto result_ptr = std::make_shared<Array>();
                        result_ptr->shape = left_arr_ptr->shape;

                        for (size_t i = 0; i < left_arr_ptr->data.size(); ++i) {
                            // Call the BASIC function for each pair of elements
                            BasicValue element_result = execute_function_for_value(func_info, { left_arr_ptr->data[i], right_arr_ptr->data[i] });
                            if (Error::get() != 0) return {};
                            result_ptr->data.push_back(element_result);
                        }
                        left = result_ptr;
                    }
                    // Case 4: Scalar @ Scalar (The original behavior)
                    else {
                        left = execute_function_for_value(func_info, { left, right });
                        if (Error::get() != 0) return {};
                    }

                }
                else {
                    Error::set(22, runtime_current_line, "Undefined function used as operator: " + func_name);
                    return {};
                }
            }
            else
            {
                BasicValue right = parse_power();

                bool is_left_tensor = std::holds_alternative<std::shared_ptr<Tensor>>(left);

                if (is_left_tensor) {
                    if (op == Tokens::ID::C_ASTR) {
                        if (!std::holds_alternative<std::shared_ptr<Tensor>>(right)) { Error::set(15, runtime_current_line, "Tensor can only be element-wise multiplied by another Tensor."); return {}; }
                        left = tensor_elementwise_multiply(*this, left, right);
                    }
                    else if (op == Tokens::ID::C_SLASH) {
                        if (std::holds_alternative<std::shared_ptr<Tensor>>(right)) { Error::set(1, runtime_current_line, "Tensor-by-Tensor division is not supported."); return {}; }
                        left = tensor_scalar_divide(*this, left, right);
                    }
                    else if (op == Tokens::ID::C_BACKSLASH) {
                        Error::set(1, runtime_current_line, "Integer division (\\) is not supported for Tensors.");
                        return {};

                    }
                    else { // MOD
                        Error::set(1, runtime_current_line, "MOD operator is not supported for Tensors."); return {};
                    }
                }
                else {
                    left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
                        using LeftT = std::decay_t<decltype(l)>; using RightT = std::decay_t<decltype(r)>;

                        if constexpr ((std::is_same_v<LeftT, std::string> && !std::is_same_v<RightT, std::string> && !std::is_same_v<RightT, std::shared_ptr<Array>>) ||
                            (!std::is_same_v<LeftT, std::string> && !std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::string>)) {

                            if (op == Tokens::ID::C_ASTR) { // String repetition
                                std::string s;
                                int count;
                                if constexpr (std::is_same_v<LeftT, std::string>) {
                                    s = l;
                                    count = static_cast<int>(to_double(r));
                                }
                                else {
                                    s = r;
                                    count = static_cast<int>(to_double(l));
                                }
                                if (count < 0) count = 0;
                                std::stringstream ss;
                                for (int i = 0; i < count; ++i) {
                                    ss << s;
                                }
                                return ss.str();
                            }

                            if (op == Tokens::ID::C_SLASH) { // String slicing
                                if constexpr (std::is_same_v<LeftT, std::string>) { // e.g. "Atomi" / 2
                                    std::string s = l;
                                    int count = static_cast<int>(to_double(r));
                                    if (count < 0) count = 0;
                                    if (static_cast<size_t>(count) > s.length()) return s;
                                    return s.substr(s.length() - count);
                                }
                                else { // e.g. 2 / "Atomi"
                                    std::string s = r;
                                    int count = static_cast<int>(to_double(l));
                                    if (count < 0) count = 0;
                                    return s.substr(0, count);
                                }
                            }
                        }

                        auto array_op = [op, this](const auto& arr, double scalar, bool arr_is_left) -> BasicValue {
                            auto result_ptr = std::make_shared<Array>(); result_ptr->shape = arr->shape;
                            for (const auto& elem : arr->data) {
                                double arr_val = to_double(elem); double res = 0;
                                if (op == Tokens::ID::C_ASTR) res = arr_is_left ? arr_val * scalar : scalar * arr_val;
                                else if (op == Tokens::ID::C_SLASH) {
                                    if ((arr_is_left && scalar == 0.0) || (!arr_is_left && arr_val == 0.0)) { Error::set(2, runtime_current_line, "Division by zero."); return false; }
                                    res = arr_is_left ? arr_val / scalar : scalar / arr_val;
                                }
                                else if (op == Tokens::ID::C_BACKSLASH) { // integer division, trunc toward 0
                                    if ((arr_is_left && scalar == 0.0) || (!arr_is_left && arr_val == 0.0)) {
                                        Error::set(2, runtime_current_line, "Division by zero."); return false;
                                    }
                                    double q = arr_is_left ? (arr_val / scalar) : (scalar / arr_val);
                                    // store as integer but Array holds doubles; keep numeric value integral
                                    res = static_cast<double>(static_cast<long long>(q));
                                }
                                else { // MOD
                                    if ((arr_is_left && scalar == 0.0) || (!arr_is_left && arr_val == 0.0)) { Error::set(2, runtime_current_line, "Division by zero."); return false; }
                                    res = static_cast<double>(arr_is_left ? static_cast<long long>(arr_val) % static_cast<long long>(scalar) : static_cast<long long>(scalar) % static_cast<long long>(arr_val));
                                }
                                result_ptr->data.push_back(res);
                            }
                            return result_ptr;
                            };

                        if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                            if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Shape mismatch for element-wise operation."); return false; }
                            auto result_ptr = std::make_shared<Array>(); result_ptr->shape = l->shape;
                            for (size_t i = 0; i < l->data.size(); ++i) {
                                double val_l = to_double(l->data[i]); double val_r = to_double(r->data[i]); double res = 0;
                                if (op == Tokens::ID::C_ASTR) res = val_l * val_r;
                                else if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line, "Division by zero."); return false; } res = val_l / val_r; }
                                else if (op == Tokens::ID::C_BACKSLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line, "Division by zero."); return false; } res = static_cast<double>(static_cast<long long>(val_l / val_r)); }
                                else { if (val_r == 0.0) { Error::set(2, runtime_current_line, "Division by zero."); return false; } res = static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                                result_ptr->data.push_back(res);
                            }
                            return result_ptr;
                        }
                        else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) { return array_op(l, to_double(r), true); }
                        else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) { return array_op(r, to_double(l), false); }
                        else { // scalar op
                            if constexpr (std::is_same_v<LeftT, double> || std::is_same_v<RightT, double>) {
                                double val_l = to_double(l); double val_r = to_double(r);
                                if (op == Tokens::ID::C_ASTR) return val_l * val_r;
                                if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                                if (op == Tokens::ID::C_BACKSLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } double q = val_l / val_r; return static_cast<long long>(q); }
                                if (op == Tokens::ID::MOD) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                            }
                            else {
                                if (op == Tokens::ID::C_ASTR) {
                                    int left_i = to_int(l);
                                    int right_i = to_int(r);
                                    return left_i * right_i;
                                }

                                if (op == Tokens::ID::C_SLASH) { double val_l = to_double(l); double val_r = to_double(r); if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                                if (op == Tokens::ID::C_BACKSLASH) { long long val_l = to_int(l); long long val_r = to_int(r); if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                                if (op == Tokens::ID::MOD) { double val_l = to_double(l); double val_r = to_double(r); if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                            }
                        }
                        return false;
                        }, left, right);
                }
            }
        }
        else { break; }
    }
    return left;
}

// Level 2: Handles + and -
BasicValue NeReLaBasic::parse_term() {
    BasicValue left = parse_factor();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::C_PLUS || op == Tokens::ID::C_MINUS) {
            pcode++;
            BasicValue right = parse_factor();
            if (std::holds_alternative<std::shared_ptr<Tensor>>(left) || std::holds_alternative<std::shared_ptr<Tensor>>(right)) {
                if (!std::holds_alternative<std::shared_ptr<Tensor>>(left) || !std::holds_alternative<std::shared_ptr<Tensor>>(right)) {
                    Error::set(15, runtime_current_line, "Cannot mix Tensor and non-Tensor types in add/subtract."); return {};
                }
                if (op == Tokens::ID::C_PLUS) { left = tensor_add(*this, left, right); }
                else { left = tensor_subtract(*this, left, right); }
            }
            else {
                left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
                    using LeftT = std::decay_t<decltype(l)>; using RightT = std::decay_t<decltype(r)>;
                    if constexpr (std::is_same_v<LeftT, std::string> || std::is_same_v<RightT, std::string>) {
                        if (op == Tokens::ID::C_PLUS) {
                            return to_string(l) + to_string(r);
                        }
                        else { // Subtraction is now string replacement
                            std::string s_left = to_string(l);
                            std::string s_right = to_string(r);
                            if (s_right.empty()) return s_left; // Avoid infinite loop
                            size_t pos = s_left.find(s_right);
                            while (pos != std::string::npos) {
                                s_left.erase(pos, s_right.length());
                                pos = s_left.find(s_right, pos);
                            }
                            return s_left;
                        }
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        // This uses the same array_add/subtract helpers from BuiltinFunctions.cpp
                        // Ensure they are globally accessible or duplicate the logic here.
                        if (!l || !r || l->data.empty()) { // Handle null or empty arrays
                            if (op == Tokens::ID::C_PLUS) return r; else return l;
                        }
                        // Check if either array contains strings to decide the operation type
                        bool is_string_op = std::holds_alternative<std::string>(l->data[0]) || std::holds_alternative<std::string>(r->data[0]);

                        if (op == Tokens::ID::C_PLUS && is_string_op) {
                            // Perform element-wise string concatenation
                            if (l->shape != r->shape) { Error::set(15, 0, "Array shapes must match for element-wise operation."); return {}; }
                            auto result_ptr = std::make_shared<Array>();
                            result_ptr->shape = l->shape;
                            result_ptr->data.reserve(l->data.size());
                            for (size_t i = 0; i < l->data.size(); ++i) {
                                result_ptr->data.push_back(to_string(l->data[i]) + to_string(r->data[i]));
                            }
                            return result_ptr;
                        }
                        else {
                            // Fallback to existing numeric operations
                            if (op == Tokens::ID::C_PLUS) return array_add(l, r); else return array_subtract(l, r);
                        }
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = l->shape;
                        double scalar = to_double(r);
                        for (const auto& elem : l->data) { result_ptr->data.push_back(op == Tokens::ID::C_PLUS ? to_double(elem) + scalar : to_double(elem) - scalar); }
                        return result_ptr;
                    }
                    else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = r->shape;
                        double scalar = to_double(l);
                        for (const auto& elem : r->data) { result_ptr->data.push_back(op == Tokens::ID::C_PLUS ? scalar + to_double(elem) : scalar - to_double(elem)); }
                        return result_ptr;
                    }
                    else {
                        if constexpr (std::is_same_v<LeftT, double> || std::is_same_v<RightT, double>) {
                            double left_d = to_double(l);
                            double right_d = to_double(r);
                            if (op == Tokens::ID::C_PLUS) return left_d + right_d;
                            else return left_d - right_d;
                        }
                        // Otherwise, perform integer math
                        else {
                            int left_i = to_int(l);
                            int right_i = to_int(r);
                            if (op == Tokens::ID::C_PLUS) return left_i + right_i;
                            else return left_i - right_i;
                        }
                    }
                    }, left, right);
            }
        }
        else { break; }
    }
    return left;
}

// Level 2: Handles <, >, = with element-wise array operations
BasicValue NeReLaBasic::parse_comparison() {
    BasicValue left = parse_term(); // parse_term handles + and -

    Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    // Check if the next token is ANY of our comparison operators
    if (op == Tokens::ID::C_EQ || op == Tokens::ID::C_LT || op == Tokens::ID::C_GT ||
        op == Tokens::ID::C_NE || op == Tokens::ID::C_LE || op == Tokens::ID::C_GE)
    {
        pcode++; // Consume the operator
        BasicValue right = parse_term();

        // Use std::visit to handle all type combinations
        left = std::visit([op, this, &left, &right](auto&& l, auto&& r) -> BasicValue {
            using LeftT = std::decay_t<decltype(l)>;
            using RightT = std::decay_t<decltype(r)>;

            // --- ARRAY COMPARISON LOGIC ---

            // Case 1: Array-Array comparison
            if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                if (!l || !r) { Error::set(15, runtime_current_line, "Comparison with null array."); return false; }
                if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Array shape mismatch in comparison."); return false; }

                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = l->shape;
                result_ptr->data.reserve(l->data.size());

                for (size_t i = 0; i < l->data.size(); ++i) {
                    double left_val = to_double(l->data[i]);
                    double right_val = to_double(r->data[i]);
                    bool result = false;
                    switch (op) {
                    case Tokens::ID::C_EQ: result = (left_val == right_val); break;
                    case Tokens::ID::C_NE: result = (left_val != right_val); break;
                    case Tokens::ID::C_LT: result = (left_val < right_val); break;
                    case Tokens::ID::C_GT: result = (left_val > right_val); break;
                    case Tokens::ID::C_LE: result = (left_val <= right_val); break;
                    case Tokens::ID::C_GE: result = (left_val >= right_val); break;
                    }
                    result_ptr->data.push_back(result);
                }
                return result_ptr;
            }
            // Case 2: Array-Scalar comparison
            else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) {
                if (!l) { Error::set(15, runtime_current_line, "Comparison with null array."); return false; }
                double scalar = to_double(r);
                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = l->shape;
                result_ptr->data.reserve(l->data.size());
                for (const auto& elem : l->data) {
                    double left_val = to_double(elem);
                    bool result = false;
                    switch (op) {
                    case Tokens::ID::C_EQ: result = (left_val == scalar); break;
                    case Tokens::ID::C_NE: result = (left_val != scalar); break;
                    case Tokens::ID::C_LT: result = (left_val < scalar); break;
                    case Tokens::ID::C_GT: result = (left_val > scalar); break;
                    case Tokens::ID::C_LE: result = (left_val <= scalar); break;
                    case Tokens::ID::C_GE: result = (left_val >= scalar); break;
                    }
                    result_ptr->data.push_back(result);
                }
                return result_ptr;
            }
            // Case 3: Scalar-Array comparison
            else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) {
                if (!r) { Error::set(15, runtime_current_line, "Comparison with null array."); return false; }
                double scalar = to_double(l);
                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = r->shape;
                result_ptr->data.reserve(r->data.size());
                for (const auto& elem : r->data) {
                    double right_val = to_double(elem);
                    bool result = false;
                    switch (op) {
                    case Tokens::ID::C_EQ: result = (scalar == right_val); break;
                    case Tokens::ID::C_NE: result = (scalar != right_val); break;
                    case Tokens::ID::C_LT: result = (scalar < right_val); break;
                    case Tokens::ID::C_GT: result = (scalar > right_val); break;
                    case Tokens::ID::C_LE: result = (scalar <= right_val); break;
                    case Tokens::ID::C_GE: result = (scalar >= right_val); break;
                    }
                    result_ptr->data.push_back(result);
                }
                return result_ptr;
            }

            // --- EXISTING SCALAR COMPARISON LOGIC (Unchanged) ---

            // Check the type of the ORIGINAL variant objects.
            else if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                // If either is a string, we compare them as strings.
                if (op == Tokens::ID::C_EQ) return to_string(l) == to_string(r);
                if (op == Tokens::ID::C_NE) return to_string(l) != to_string(r);
                if (op == Tokens::ID::C_LT) return to_string(l) < to_string(r);
                if (op == Tokens::ID::C_GT) return to_string(l) > to_string(r);
                if (op == Tokens::ID::C_LE) return to_string(l) <= to_string(r);
                if (op == Tokens::ID::C_GE) return to_string(l) >= to_string(r);
            }
            // Priority 2: If BOTH operands are DateTime, compare their internal time_points.
            else if (std::holds_alternative<DateTime>(left) && std::holds_alternative<DateTime>(right)) {
                const auto& dt_l = std::get<DateTime>(left);
                const auto& dt_r = std::get<DateTime>(right);
                if (op == Tokens::ID::C_EQ) return dt_l.time_point == dt_r.time_point;
                if (op == Tokens::ID::C_NE) return dt_l.time_point != dt_r.time_point;
                if (op == Tokens::ID::C_LT) return dt_l.time_point < dt_r.time_point;
                if (op == Tokens::ID::C_GT) return dt_l.time_point > dt_r.time_point;
                if (op == Tokens::ID::C_LE) return dt_l.time_point <= dt_r.time_point;
                if (op == Tokens::ID::C_GE) return dt_l.time_point >= dt_r.time_point;
            }
            else {
                // Otherwise, both are numeric (double or bool), so we compare them as numbers.
                if (op == Tokens::ID::C_EQ) return to_double(l) == to_double(r);
                if (op == Tokens::ID::C_NE) return to_double(l) != to_double(r);
                if (op == Tokens::ID::C_LT) return to_double(l) < to_double(r);
                if (op == Tokens::ID::C_GT) return to_double(l) > to_double(r);
                if (op == Tokens::ID::C_LE) return to_double(l) <= to_double(r);
                if (op == Tokens::ID::C_GE) return to_double(l) >= to_double(r);
            }
            return false; // Should not be reached
            }, left, right);
    }

    return left;
}

BasicValue NeReLaBasic::parse_membership() {
    BasicValue left = parse_comparison(); // This is the "needle"

    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::IN_OPERATOR) {
        pcode++; // Consume the 'IN' token
        BasicValue right = parse_comparison(); // This is the "haystack"

        // Use std::visit to handle the different types for the haystack
        left = std::visit([this, &left](auto&& haystack_arg) -> BasicValue {
            using HaystackT = std::decay_t<decltype(haystack_arg)>;

            // Case 1: Check if a key exists in a Map
            if constexpr (std::is_same_v<HaystackT, std::shared_ptr<Map>>) {
                if (!haystack_arg) {
                    Error::set(15, runtime_current_line, "Cannot use IN on a null Map.");
                    return false;
                }
                std::string key_to_find = to_string(left);
                return haystack_arg->data.count(key_to_find) > 0;
            }
            // Case 2: Check if a value exists in an Array
            else if constexpr (std::is_same_v<HaystackT, std::shared_ptr<Array>>) {
                if (!haystack_arg) {
                    Error::set(15, runtime_current_line, "Cannot use IN on a null Array.");
                    return false;
                }
                for (const auto& element : haystack_arg->data) {
                    // This comparison handles different types by converting to string as a fallback.
                    // A more strict comparison could be implemented if needed.
                    if (std::holds_alternative<double>(left) && std::holds_alternative<double>(element)) {
                        if (std::get<double>(left) == std::get<double>(element)) return true;
                    }
                    else if (to_string(left) == to_string(element)) {
                        return true;
                    }
                }
                return false; // Not found after checking all elements
            }
            // Case 3 (Bonus): Check if a substring exists in a String
            else if constexpr (std::is_same_v<HaystackT, std::string>) {
                std::string needle_str = to_string(left);
                return haystack_arg.find(needle_str) != std::string::npos;
            }
            // Default: The haystack is an unsupported type
            else {
                Error::set(15, runtime_current_line, "IN operator requires a Map, Array, or String on the right-hand side.");
                return false;
            }
            }, right);

        if (Error::get() != 0) return {};
    }
    return left;
}

BasicValue NeReLaBasic::parse_pipe() {
    BasicValue left = parse_membership();

    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_PIPE) {
        pcode++; // Consume '|>'

        // Set the pipe context before evaluating the RHS
        is_in_pipe_call = true;
        piped_value_for_call = left;

        // The RHS is a full expression, which will typically be a function call.
        // Our modified argument parser will now see the context.
        left = parse_comparison();

        // Reset the pipe context after the RHS has been evaluated
        is_in_pipe_call = false;

        if (Error::get() != 0) return {};
    }
    return left;
}

// Level 1c: Bitwise AND (Highest bitwise precedence)
BasicValue NeReLaBasic::parse_bitwise_and() {
    BasicValue left = parse_pipe(); // Calls the next higher precedence level
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::BAND) {
        pcode++;
        BasicValue right = parse_pipe();

        left = std::visit([](auto&& l, auto&& r) -> BasicValue {
            // Vectorized logic for bitwise operators
            auto bitwise_op = [](const BasicValue& v1, const BasicValue& v2) {
                //long long n1 = static_cast<long long>(to_double(v1));
                //long long n2 = static_cast<long long>(to_double(v2));
                //return static_cast<double>(n1 & n2);
                return static_cast<long long>(to_int(v1) & to_int(v2));
                };
            return apply_binary_op(l, r, bitwise_op);
            }, left, right);
    }
    return left;
}

// Level 1b: Bitwise XOR
BasicValue NeReLaBasic::parse_bitwise_xor() {
    BasicValue left = parse_bitwise_and(); // Calls the next higher precedence level
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::BXOR) {
        pcode++;
        BasicValue right = parse_bitwise_and();

        left = std::visit([](auto&& l, auto&& r) -> BasicValue {
            auto bitwise_op = [](const BasicValue& v1, const BasicValue& v2) {
                //long long n1 = static_cast<long long>(to_double(v1));
                //long long n2 = static_cast<long long>(to_double(v2));
                //return static_cast<double>(n1 ^ n2);
                return static_cast<long long>(to_int(v1) ^ to_int(v2));
                };
            return apply_binary_op(l, r, bitwise_op);
            }, left, right);
    }
    return left;
}

// Level 1a: Bitwise OR (Lowest bitwise precedence)
BasicValue NeReLaBasic::parse_bitwise_or() {
    BasicValue left = parse_bitwise_xor(); // Calls the next higher precedence level
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::BOR) {
        pcode++;
        BasicValue right = parse_bitwise_xor();

        left = std::visit([](auto&& l, auto&& r) -> BasicValue {
            auto bitwise_op = [](const BasicValue& v1, const BasicValue& v2) {
                //long long n1 = static_cast<long long>(to_double(v1));
                //long long n2 = static_cast<long long>(to_double(v2));
                //return static_cast<double>(n1 | n2);
                return static_cast<long long>(to_int(v1) | to_int(v2));
                };
            return apply_binary_op(l, r, bitwise_op);
            }, left, right);
    }
    return left;
}

// --- The Expression Skipping Implementation ---
// Base Case: Skips a primary element like a literal, variable, function call, or parenthesized expression.
void NeReLaBasic::skip_primary() {
    if (pcode >= active_p_code->size()) return;

    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    // Most tokens are a single byte, so we can pre-increment.
    // Cases that need more work will advance pcode further.
    pcode++;

    switch (token) {
    case Tokens::ID::NUMBER:
        pcode += sizeof(double); // Skip the 8-byte double literal
        break;
    case Tokens::ID::INTEGER_LITERAL:
        pcode += sizeof(long long); // Skip the long long literal
        break;
    case Tokens::ID::STRING:
    case Tokens::ID::VARIANT:
    case Tokens::ID::FUNCREF:
    case Tokens::ID::CONSTANT:
        read_string(*this); // Reads the string to advance pcode past it
        break;
    case Tokens::ID::CALLFUNC:
        read_string(*this); // Skip function name
        // Now, explicitly skip the argument list that MUST follow.
        if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_LEFTPAREN) {
            pcode++; // Skip '('
            if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
                while (true) {
                    skip_expression(); // Recursively skip each argument expression
                    if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTPAREN) break;
                    pcode++; // Skip comma
                }
            }
            if (pcode < active_p_code->size()) pcode++; // Skip ')'
        }
        break;

        // CORRECTED LOGIC: C_LEFTPAREN is only for parenthesized expressions.
    case Tokens::ID::C_LEFTPAREN:
        skip_expression(); // Skip the inner expression
        if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTPAREN) {
            pcode++; // Skip ')'
        }
        break;
    case Tokens::ID::C_LEFTBRACKET: // Skip Array Literal
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
            while (true) {
                skip_expression();
                if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTBRACKET) break;
                pcode++; // Skip comma
            }
        }
        pcode++; // Skip ']'
        break;
    case Tokens::ID::C_LEFTBRACE: // Skip Map Literal
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACE) {
            while (true) {
                skip_expression(); // Skip key
                pcode++; // Skip colon
                skip_expression(); // Skip value
                if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTBRACE) break;
                pcode++; // Skip comma
            }
        }
        pcode++; // Skip '}'
        break;
        // For simple tokens (TRUE, FALSE, THIS, etc.), just consuming the token is enough.
    default:
        break;
    }

    // After skipping the primary, we must also skip any chained accessors like `[...]`, `{. A..}`, or `.MEMBER`
    while (true) {
        Tokens::ID accessor_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (accessor_token == Tokens::ID::C_LEFTBRACKET || accessor_token == Tokens::ID::C_LEFTBRACE) {
            pcode++; // consume '[' or '{'
            skip_expression();
            pcode++; // consume ']' or '}'
        }
        else if (accessor_token == Tokens::ID::C_DOT) {
            pcode++; // consume '.'
            pcode++; // consume VARIANT/CALLFUNC token
            read_string(*this); // consume member name
        }
        else {
            break;
        }
    }
}

// Skips unary operators and their operands
void NeReLaBasic::skip_unary() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
    if (token == Tokens::ID::C_MINUS || token == Tokens::ID::NOT || token == Tokens::ID::AWAIT) {
        pcode++; // Skip the unary operator
    }
    skip_primary();
}

// Skips a chain of binary operations for a given precedence level.
void NeReLaBasic::skip_binary_op_chain(std::function<void()> skip_higher_precedence, const std::vector<Tokens::ID>& operators) {
    skip_higher_precedence(); // Skip the first operand
    while (pcode < active_p_code->size()) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        bool is_op = false;
        for (auto valid_op : operators) {
            if (op == valid_op) {
                is_op = true;
                break;
            }
        }

        if (is_op) {
            pcode++; // Consume the operator
            if (op == Tokens::ID::FUNCREF) { read_string(*this); } // Custom ops have a name
            skip_higher_precedence(); // Skip the next operand
        }
        else {
            break;
        }
    }
}

void NeReLaBasic::skip_power() { skip_binary_op_chain([this] { skip_unary(); }, { Tokens::ID::C_CARET }); }
void NeReLaBasic::skip_factor() { skip_binary_op_chain([this] { skip_power(); }, { Tokens::ID::C_ASTR, Tokens::ID::C_SLASH, Tokens::ID::MOD, Tokens::ID::FUNCREF, Tokens::ID::C_BACKSLASH }); }
void NeReLaBasic::skip_term() { skip_binary_op_chain([this] { skip_factor(); }, { Tokens::ID::C_PLUS, Tokens::ID::C_MINUS }); }
void NeReLaBasic::skip_comparison() { skip_binary_op_chain([this] { skip_term(); }, { Tokens::ID::C_EQ, Tokens::ID::C_NE, Tokens::ID::C_LT, Tokens::ID::C_GT, Tokens::ID::C_LE, Tokens::ID::C_GE }); }
void NeReLaBasic::skip_membership() { skip_binary_op_chain([this] { skip_comparison(); }, { Tokens::ID::IN_OPERATOR }); }
void NeReLaBasic::skip_pipe() { skip_binary_op_chain([this] { skip_membership(); }, { Tokens::ID::C_PIPE }); }
void NeReLaBasic::skip_bitwise_and() { skip_binary_op_chain([this] { skip_pipe(); }, { Tokens::ID::BAND }); }
void NeReLaBasic::skip_bitwise_xor() { skip_binary_op_chain([this] { skip_bitwise_and(); }, { Tokens::ID::BXOR }); }
void NeReLaBasic::skip_bitwise_or() { skip_binary_op_chain([this] { skip_bitwise_xor(); }, { Tokens::ID::BOR }); }
void NeReLaBasic::skip_logical_and() { skip_binary_op_chain([this] { skip_bitwise_or(); }, { Tokens::ID::AND, Tokens::ID::ANDALSO }); }
void NeReLaBasic::skip_logical_or() { skip_binary_op_chain([this] { skip_logical_and(); }, { Tokens::ID::OR, Tokens::ID::ORELSE }); }

// Top-level function, now correctly structured
void NeReLaBasic::skip_expression() {
    skip_logical_or();
    while (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::XOR) {
        pcode++;
        skip_logical_or();
    }
}

// Level for logical AND / ANDALSO
BasicValue NeReLaBasic::parse_logical_and() {
    BasicValue left = parse_bitwise_or();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::AND) {
            pcode++;
            BasicValue right = parse_bitwise_or();
            auto and_op = [](const BasicValue& v1, const BasicValue& v2) { return to_bool(v1) && to_bool(v2); };
            left = apply_binary_op(left, right, and_op);
        }
        else if (op == Tokens::ID::ANDALSO) {
            pcode++;
            if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
                Error::set(15, runtime_current_line, "ANDALSO operator does not support array operands.");
                return {};
            }
            if (to_bool(left)) {
                left = parse_bitwise_or(); // Evaluate RHS
            }
            else {
                skip_expression(); // Skip RHS
            }
        }
        else {
            break;
        }
    }
    return left;
}

BasicValue NeReLaBasic::parse_logical_or() {
    BasicValue left = parse_logical_and();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::OR) {
            pcode++;
            BasicValue right = parse_logical_and();
            auto or_op = [](const BasicValue& v1, const BasicValue& v2) { return to_bool(v1) || to_bool(v2); };
            left = apply_binary_op(left, right, or_op);
        }
        else if (op == Tokens::ID::ORELSE) {
            pcode++;
            if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
                Error::set(15, runtime_current_line, "ORELSE operator does not support array operands.");
                return {};
            }
            if (!to_bool(left)) {
                left = parse_logical_and(); // Evaluate RHS
            }
            else {
                skip_expression(); // Skip RHS
            }
        }
        else {
            break;
        }
    }
    return left;
}

// Top-level expression function
BasicValue NeReLaBasic::evaluate_expression() {
    BasicValue left = parse_logical_or();

    // The loop now only handles XOR, which is non-short-circuiting
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::XOR) {
        pcode++;
        BasicValue right = parse_logical_or();
        // Use the generic helper for vectorized XOR
        auto xor_op = [](const BasicValue& v1, const BasicValue& v2) -> BasicValue {
            return to_bool(v1) != to_bool(v2);
            };
        left = apply_binary_op(left, right, xor_op);
    }
    return left;
}
