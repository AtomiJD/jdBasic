#include "jdhf.h"
#include "NeReLaBasic.hpp"    // Access to the VM
#include "Types.hpp"          // Access to BasicValue, Map, Array
#include "Error.hpp"          // Access to ErrorSetFunc
// We need to_double, to_bool, etc. which are in Types.hpp
// We also need g_vm_instance_ptr from Error.cpp
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>

// --- Global pointers to callbacks ---
namespace {
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
    ToStringFunc g_to_string = nullptr;
    // --- THIS IS THE NEW POINTER TO THE EXE'S FUNCTION ---
    ExecSyncFunc g_exec_sync_func = nullptr;


    // --- Helper to look up a jdBasic function ---
    const NeReLaBasic::FunctionInfo& GetFuncInfo(NeReLaBasic* vm, const std::string& name) {
        std::string upper_name = g_to_upper(name);
        if (vm->main_function_table.count(upper_name)) {
            return vm->main_function_table.at(upper_name);
        }
        // You might need to check module tables if they are not merged
        throw std::runtime_error("jdBasic function not found: " + name);
    }

    /**
     * @brief The C++ class that holds all state for a training job.
     */
    class JdTrainer {
    public:
        // We store a pointer to the VM to make callbacks
        NeReLaBasic* vm_;

        // jdBasic objects
        BasicValue model_map_;
        BasicValue optimizer_handle_;
        BasicValue train_data_;
        BasicValue val_data_;

        // Pre-looked-up FunctionInfo structs for speed
        const NeReLaBasic::FunctionInfo& get_batch_func_;
        const NeReLaBasic::FunctionInfo& model_forward_func_;
        const NeReLaBasic::FunctionInfo& loss_func_;
        const NeReLaBasic::FunctionInfo& eval_func_;
        const NeReLaBasic::FunctionInfo& py_zero_grad_func_;
        const NeReLaBasic::FunctionInfo& py_backward_func_;
        const NeReLaBasic::FunctionInfo& py_step_func_;
        const NeReLaBasic::FunctionInfo& py_toarray_func_;

        int batch_size_;
        int epochs_;

        // Constructor
        JdTrainer(NeReLaBasic* vm, const BasicValue& config_map) :
            vm_(vm),
            // Look up functions by name during construction
            get_batch_func_(GetFuncInfo(vm, g_to_string(std::get<std::shared_ptr<Map>>(config_map)->data.at("get_batch_func")))),
            model_forward_func_(GetFuncInfo(vm, g_to_string(std::get<std::shared_ptr<Map>>(config_map)->data.at("model_forward_func")))),
            loss_func_(GetFuncInfo(vm, g_to_string(std::get<std::shared_ptr<Map>>(config_map)->data.at("loss_func")))),
            eval_func_(GetFuncInfo(vm, g_to_string(std::get<std::shared_ptr<Map>>(config_map)->data.at("eval_func")))),
            py_zero_grad_func_(GetFuncInfo(vm, "PYTORCH.OPTIMIZER_ZERO_GRAD")),
            py_backward_func_(GetFuncInfo(vm, "PYTORCH.BACKWARD")),
            py_step_func_(GetFuncInfo(vm, "PYTORCH.OPTIMIZER_STEP")),
            py_toarray_func_(GetFuncInfo(vm, "PYTORCH.TOARRAY"))
        {
            if (!std::holds_alternative<std::shared_ptr<Map>>(config_map)) {
                throw std::runtime_error("Config must be a Map.");
            }
            const auto& config = std::get<std::shared_ptr<Map>>(config_map);

            // Get all the required objects from the config map
            model_map_ = config->data.at("model");
            optimizer_handle_ = config->data.at("optimizer");
            train_data_ = config->data.at("train_data");
            val_data_ = config->data.at("val_data");

            batch_size_ = static_cast<int>(to_double(config->data.at("batch_size")));
            epochs_ = static_cast<int>(to_double(config->data.at("epochs")));
        }

        // The main training loop, now in C++
        void Train() {
            if (!std::holds_alternative<std::shared_ptr<Array>>(train_data_)) {
                g_error_set(15, vm_->runtime_current_line, "Trainer 'train_data' must be an Array.");
                return;
            }

            const auto& train_array_ptr = std::get<std::shared_ptr<Array>>(train_data_);
            if (train_array_ptr->shape.empty()) {
                g_error_set(15, vm_->runtime_current_line, "Trainer 'train_data' array has no shape.");
                return;
            }
            // num_batches is based on the FIRST dimension (rows), not total elements
            size_t num_rows = train_array_ptr->shape[0];
            int num_batches = num_rows / batch_size_;

            BasicValue dummy_result;

            for (int epoch = 0; epoch < epochs_; ++epoch) {
                std::cout << "\n--- Epoch " << (epoch + 1) << " / " << epochs_ << " ---" << std::endl;
                double total_loss = 0;

                for (int i = 0; i < num_batches; ++i) {
                    // --- a) Get Batch (Call back to jdBasic) ---
                    std::vector<BasicValue> get_batch_args = { train_data_, (double)i, (double)batch_size_ };
                    // FIX: Call the function using the stored pointer
                    BasicValue batch_map = (vm_->*g_exec_sync_func)(get_batch_func_, get_batch_args);
                    // FIX: Check the VM's public error flag
                    if (vm_->jump_to_catch_pending) return;

                    // --- b) Forward Pass (Call back to jdBasic) ---
                    const auto& batch_data = std::get<std::shared_ptr<Map>>(batch_map);
                    std::vector<BasicValue> forward_args = { model_map_, batch_data->data.at("input_ids"), batch_data->data.at("attention_mask") };
                    BasicValue logits_tensor = (vm_->*g_exec_sync_func)(model_forward_func_, forward_args);
                    if (vm_->jump_to_catch_pending) return;

                    // --- c) Zero Gradients (Call C++ function) ---
                    std::vector<BasicValue> zero_grad_args = { optimizer_handle_ };
                    // FIX: Call the native_impl pointer directly
                    dummy_result = py_zero_grad_func_.native_impl(*vm_, zero_grad_args);
                    if (vm_->jump_to_catch_pending) return;

                    // --- d) Calculate Loss (Call back to jdBasic) ---
                    std::vector<BasicValue> loss_args = { logits_tensor, batch_data->data.at("labels") };
                    BasicValue loss_tensor = (vm_->*g_exec_sync_func)(loss_func_, loss_args);
                    if (vm_->jump_to_catch_pending) return;

                    // --- e) Backward Pass (Call C++ function) ---
                    std::vector<BasicValue> backward_args = { loss_tensor };
                    dummy_result = py_backward_func_.native_impl(*vm_, backward_args);
                    if (vm_->jump_to_catch_pending) return;

                    // --- f) Optimizer Step (Call C++ function) ---
                    std::vector<BasicValue> step_args = { optimizer_handle_ };
                    dummy_result = py_step_func_.native_impl(*vm_, step_args);
                    if (vm_->jump_to_catch_pending) return;

                    // --- g) Print Progress ---
                    BasicValue loss_array;
                    loss_array = py_toarray_func_.native_impl(*vm_, { loss_tensor });
                    if (vm_->jump_to_catch_pending) return;

                    // FIX: Use 'to_double' helper from Types.hpp
                    double loss_val = to_double(std::get<std::shared_ptr<Array>>(loss_array)->data[0]);
                    total_loss += loss_val;

                    if (i % 50 == 0) {
                        std::cout << "Epoch " << (epoch + 1) << ", Batch " << i << "/" << num_batches << ", Loss: " << loss_val << std::endl;
                    }
                }

                std::cout << "Epoch " << (epoch + 1) << " Average Loss: " << (total_loss / num_batches) << std::endl;

                // --- h) Evaluate (Call back to jdBasic) ---
                std::vector<BasicValue> eval_args = { model_map_, val_data_ };
                BasicValue accuracy_val = (vm_->*g_exec_sync_func)(eval_func_, eval_args);
                if (vm_->jump_to_catch_pending) return;

                std::cout << "Epoch " << (epoch + 1) << " Validation Accuracy: " << (to_double(accuracy_val) * 100.0) << "%" << std::endl;
            }
        }
    };

    // We store our trainer instances in a global vector
    std::vector<std::unique_ptr<JdTrainer>> g_trainers;

    // JDTRAINER.NEW(config_map) -> Handle
    BasicValue builtin_jdhf_new_trainer(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 1) {
            g_error_set(8, vm.runtime_current_line, "JDTRAINER.NEW requires 1 argument: config_map");
            return {};
        }
        try {
            // Pass the vm pointer to the trainer's constructor
            auto trainer = std::make_unique<JdTrainer>(&vm, args[0]);
            g_trainers.push_back(std::move(trainer));
            return static_cast<double>(g_trainers.size() - 1); // Return index as handle
        }
        catch (const std::exception& e) {
            g_error_set(15, vm.runtime_current_line, e.what());
            return {};
        }
    }

    // JDTRAINER.TRAIN(handle)
    BasicValue builtin_jdhf_train(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 1) { /* error */ return {}; }
        try {
            size_t index = static_cast<size_t>(to_double(args[0]));
            if (index >= g_trainers.size()) { /* error */ return {}; }

            g_trainers[index]->Train(); // Run the main loop
            return true;
        }
        catch (const std::exception& e) {
            g_error_set(15, vm.runtime_current_line, e.what());
            return {};
        }
    }

    // JDTRAINER.GET_MODEL(handle) -> Map
    BasicValue builtin_jdhf_get_model(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 1) { /* error */ return {}; }
        try {
            size_t index = static_cast<size_t>(to_double(args[0]));
            if (index >= g_trainers.size()) { /* error */ return {}; }

            return g_trainers[index]->model_map_; // Return the jdBasic model Map
        }
        catch (const std::exception& e) {
            g_error_set(15, vm.runtime_current_line, e.what());
            return {};
        }
    }

    // Registration
    void register_jdhf_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
        // Matched signature from NeReLaBasic.hpp (NativeFunction)
        auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
            NeReLaBasic::FunctionInfo info;
            info.name = name;
            info.arity = arity;
            info.native_impl = func_ptr;
            table[g_to_upper(name)] = info;
            };

        register_func("JDTRAINER.NEW", 1, builtin_jdhf_new_trainer);
        register_func("JDTRAINER.TRAIN", 1, builtin_jdhf_train);
        register_func("JDTRAINER.GET_MODEL", 1, builtin_jdhf_get_model);
    }

} // anonymous namespace

// Main DLL entry point
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }
    g_error_set = services->error_set;
    g_to_upper = services->to_upper;
    g_to_string = services->to_string;
    g_exec_sync_func = services->exec_sync_func;

    // Check if the pointer was assigned
    if (!g_exec_sync_func) {
        g_error_set(22, 0, "jdhf.dll requires a newer jdBasic.exe that provides 'exec_sync_func' service.");
        return;
    }

    register_jdhf_functions(*vm, vm->main_function_table);
}

