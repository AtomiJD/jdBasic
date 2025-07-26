#include "aifunc.h"
#include "NeReLaBasic.hpp" 
#include "AIFunctions.hpp"   
#include "Types.hpp"
#include "Error.hpp"
#include "Commands.hpp"      

#include <string>
#include <vector>
#include <memory>
#include <cmath> 

// --- Global pointers to hold the functions provided by the main app ---
namespace {
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
}

// Helper to get a nested map from the optimizer state.
std::shared_ptr<Map> get_or_create_nested_map(std::shared_ptr<Map> base_map, const std::string & key) {
    if (!base_map) return nullptr;

    if (base_map->data.find(key) == base_map->data.end() || !std::holds_alternative<std::shared_ptr<Map>>(base_map->data[key])) {
        base_map->data[key] = std::make_shared<Map>();
    }
    return std::get<std::shared_ptr<Map>>(base_map->data[key]);
}

BasicValue builtin_adam_update(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        g_error_set(8, vm.runtime_current_line, "ADAM.UPDATE requires 2 arguments: model, optimizer_state");
        return {};
    }

    const auto& model_map = std::get<std::shared_ptr<Map>>(args[0]);
    auto& optimizer_map = std::get<std::shared_ptr<Map>>(args[1]);

    // Extract optimizer parameters
    double lr = to_double(optimizer_map->data.at("learning_rate"));
    double beta1 = to_double(optimizer_map->data.at("beta1"));
    double beta2 = to_double(optimizer_map->data.at("beta2"));
    double epsilon = to_double(optimizer_map->data.at("epsilon"));
    int t = static_cast<int>(to_double(optimizer_map->data.at("t"))) + 1;
    optimizer_map->data["t"] = static_cast<double>(t);

    auto m_state = get_or_create_nested_map(optimizer_map, "m");
    auto v_state = get_or_create_nested_map(optimizer_map, "v");
    if (!m_state || !v_state) {
        g_error_set(3, vm.runtime_current_line, "Could not access optimizer state.");
        return {};
    }

    // Iterate through layers in the model (e.g., "layer1", "layer2")
    for (const auto& layer_pair : model_map->data) {
        const std::string& layer_name = layer_pair.first;
        const BasicValue& layer_val = layer_pair.second;

        // A layer must be a map containing parameters like "weights" and "bias"
        if (!std::holds_alternative<std::shared_ptr<Map>>(layer_val)) {
            continue;
        }
        auto& layer_map = std::get<std::shared_ptr<Map>>(layer_val);

        // Get or create state maps for the current layer
        auto m_layer_state = get_or_create_nested_map(m_state, layer_name);
        auto v_layer_state = get_or_create_nested_map(v_state, layer_name);
        if (!m_layer_state || !v_layer_state) continue;

        // Iterate through parameters in the layer (e.g., "weights", "bias")
        for (const auto& param_pair : layer_map->data) {
            const std::string& param_name = param_pair.first;
            const BasicValue& param_val = param_pair.second;

            // Parameter must be a Tensor to be updated
            if (std::holds_alternative<std::shared_ptr<Tensor>>(param_val)) {
                auto& param_tensor = std::get<std::shared_ptr<Tensor>>(param_val);

                // Skip if tensor has no gradient data
                if (!param_tensor || !param_tensor->grad || !param_tensor->grad->data) {
                    continue;
                }

                // Initialize optimizer state for this tensor if it's the first time
                if (m_layer_state->data.find(param_name) == m_layer_state->data.end()) {
                    // --- FIX: Create two SEPARATE zero-filled arrays for m and v ---
                    auto m_zero_data = std::make_shared<FloatArray>();
                    m_zero_data->shape = param_tensor->data->shape;
                    m_zero_data->data.assign(param_tensor->data->size(), 0.0);

                    auto m_tensor_init = std::make_shared<Tensor>();
                    m_tensor_init->data = m_zero_data;
                    m_layer_state->data[param_name] = m_tensor_init;

                    auto v_zero_data = std::make_shared<FloatArray>();
                    v_zero_data->shape = param_tensor->data->shape;
                    v_zero_data->data.assign(param_tensor->data->size(), 0.0);

                    auto v_tensor_init = std::make_shared<Tensor>();
                    v_tensor_init->data = v_zero_data;
                    v_layer_state->data[param_name] = v_tensor_init;
                }

                // Retrieve state tensors and gradient
                auto& m_tensor = std::get<std::shared_ptr<Tensor>>(m_layer_state->data[param_name]);
                auto& v_tensor = std::get<std::shared_ptr<Tensor>>(v_layer_state->data[param_name]);
                auto& m = m_tensor->data;
                auto& v = v_tensor->data;
                auto& grad = param_tensor->grad->data;

                // Apply Adam update rule to each element of the tensor
                for (size_t i = 0; i < param_tensor->data->size(); ++i) {
                    // Update biased first moment estimate
                    m->data[i] = beta1 * m->data[i] + (1.0 - beta1) * grad->data[i];
                    // Update biased second raw moment estimate
                    v->data[i] = beta2 * v->data[i] + (1.0 - beta2) * std::pow(grad->data[i], 2);
                    // Compute bias-corrected first moment estimate
                    double m_hat = m->data[i] / (1.0 - std::pow(beta1, t));
                    // Compute bias-corrected second raw moment estimate
                    double v_hat = v->data[i] / (1.0 - std::pow(beta2, t));
                    // Update parameters
                    param_tensor->data->data[i] -= lr * m_hat / (std::sqrt(v_hat) + epsilon);
                }
                // Clear the gradient after updating
                param_tensor->grad = nullptr;
            }
        }
    }
    return model_map;
}
void register_adam_optimizer(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
    auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        table[g_to_upper(name)] = info;
        };

    register_func("ADAM.UPDATE", 2, builtin_adam_update);
}


/**
 * @brief The main entry point of the DLL.
 */
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }

    g_error_set = services->error_set;
    g_to_upper = services->to_upper;

    register_adam_optimizer(*vm, vm->main_function_table);
}
