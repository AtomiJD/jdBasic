    if (std::holds_alternative<std::string>(op_arg)) {
        const std::string op = to_upper(std::get<std::string>(op_arg));
#pragma omp parallel for
        for (const auto& val_a : a_ptr->data) {
            for (const auto& val_b : b_ptr->data) {
                double num_a = to_double(val_a);
                double num_b = to_double(val_b);
                if (op == "+") result_ptr->data.push_back(num_a + num_b);
                else if (op == "-") result_ptr->data.push_back(num_a - num_b);
                else if (op == "*") result_ptr->data.push_back(num_a * num_b);
                else if (op == "^") result_ptr->data.push_back(pow(num_a, num_b));
                else if (op == "/") {
                    if (num_b == 0.0) {
                        Error::set(2, vm.runtime_current_line);
                        error_found = true;
                        continue;
                    }
                    result_ptr->data.push_back(num_a / num_b);
                }
                else if (op == "=") result_ptr->data.push_back(num_a == num_b);
                else if (op == ">") result_ptr->data.push_back(num_a > num_b);
                else if (op == "<") result_ptr->data.push_back(num_a < num_b);
                else { Error::set(1, vm.runtime_current_line, "Invalid operator string: " + op); 
                    error_found = true;
                    continue;
                }
            }
        }
        if (error_found) {
            return {};
        }
    }