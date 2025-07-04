#pragma once
#pragma once
#include "NeReLaBasic.hpp"
#include "BuiltinFunctions.hpp" 
#include <vector>

// Forward declarations for all AI-related built-in functions
BasicValue builtin_to_tensor(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_toarray(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_create_layer(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_create_optimizer(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_conv2d(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_maxpool2d(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sigmoid(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_matmul(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_sum(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_backward(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_update(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_save_model(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_load_model(NeReLaBasic& vm, const std::vector<BasicValue>& args);

BasicValue builtin_tensor_tokenize(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_tensor_positional_encoding(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_tensor_softmax(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_tensor_relu(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_tensor_cross_entropy_loss(NeReLaBasic& vm, const std::vector<BasicValue>& args);

// The main registration function for this module
void register_ai_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate);
