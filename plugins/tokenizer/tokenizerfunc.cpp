#include "tokenizerfunc.h"
#include "NeReLaBasic.hpp"
#include "Types.hpp"
#include "Error.hpp"

// 1. Include the new header-only library
// (Assumes Modern-Text-Tokenizer.hpp is in your include path)
#include "Modern-Text-Tokenizer.hpp"

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

// --- Global pointers to callbacks ---
namespace {
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
    ToStringFunc g_to_string = nullptr;

    // --- 3. Helper Functions ---

    /**
     * @brief Helper to convert std::vector<int> to a jdBasic Array
     */
    std::shared_ptr<Array> VectorIntToBasicArray(const std::vector<int>& vec) {
        auto arr_ptr = std::make_shared<Array>();
        arr_ptr->shape = { vec.size() };
        arr_ptr->data.reserve(vec.size());
        for (int val : vec) {
            arr_ptr->data.push_back(static_cast<double>(val));
        }
        return arr_ptr;
    }

    /**
     * @brief Wraps a Tokenizer in a jdBasic OpaqueHandle.
     */
    BasicValue TokenizerToHandle(MecanikDev::TextTokenizer* tokenizer) {
        auto deleter = [](void* p) {
            delete static_cast<MecanikDev::TextTokenizer*>(p);
        };
        return std::make_shared<OpaqueHandle>(tokenizer, "TOKENIZER", deleter);
    }

    /**
     * @brief Converts a jdBasic OpaqueHandle back into a Tokenizer reference.
     */
    MecanikDev::TextTokenizer& HandleToTokenizer(NeReLaBasic& vm, const BasicValue& val) {
        if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(val)) {
            g_error_set(15, vm.runtime_current_line, "Argument is not a valid TOKENIZER handle.");
            throw std::runtime_error("Invalid tokenizer handle");
        }
        const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(val);
        if (handle->type_name != "TOKENIZER") {
            g_error_set(15, vm.runtime_current_line, "Handle is not of type TOKENIZER.");
            throw std::runtime_error("Invalid tokenizer handle type");
        }
        return *static_cast<MecanikDev::TextTokenizer*>(handle->ptr);
    }


    // --- 4. Built-in Function Implementations ---

    // TOKENIZER.NEW() -> Handle
    BasicValue builtin_tokenizer_new(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 0) {
            g_error_set(8, vm.runtime_current_line, "TOKENIZER.NEW takes no arguments.");
            return {};
        }
        MecanikDev::TextTokenizer* tokenizer = new MecanikDev::TextTokenizer();
        return TokenizerToHandle(tokenizer);
    }

    // TOKENIZER.SET_LOWERCASE(handle, bool)
    BasicValue builtin_tokenizer_set_lowercase(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 2) { /* error */ return {}; }
        try {
            MecanikDev::TextTokenizer& tokenizer = HandleToTokenizer(vm, args[0]);
            tokenizer.set_lowercase(to_bool(args[1]));
            return true;
        } catch (const std::exception& e) { /* ... */ return {}; }
    }
    
    // TOKENIZER.SET_SPLIT_ON_PUNCTUATION(handle, bool)
    BasicValue builtin_tokenizer_set_split_on_punctuation(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 2) { /* error */ return {}; }
        try {
            MecanikDev::TextTokenizer& tokenizer = HandleToTokenizer(vm, args[0]);
            tokenizer.set_split_on_punctuation(to_bool(args[1]));
            return true;
        } catch (const std::exception& e) { /* ... */ return {}; }
    }

    // TOKENIZER.LOAD_VOCAB(handle, vocab_file$)
    BasicValue builtin_tokenizer_load_vocab(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 2) { /* error */ return {}; }
        try {
            MecanikDev::TextTokenizer& tokenizer = HandleToTokenizer(vm, args[0]);
            std::string path = g_to_string(args[1]);
            bool success = tokenizer.load_vocab(path);
            if (!success) {
                g_error_set(15, vm.runtime_current_line, "Failed to load vocab file: " + path);
                return false;
            }
            return true;
        } catch (const std::exception& e) { /* ... */ return {}; }
    }

    // TOKENIZER.GET_PAD_ID(handle) -> Number
    BasicValue builtin_tokenizer_get_pad_id(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 1) { /* error */ return {}; }
        try {
            MecanikDev::TextTokenizer& tokenizer = HandleToTokenizer(vm, args[0]);
            return static_cast<double>(tokenizer.get_pad_id());
        } catch (const std::exception& e) { /* ... */ return {}; }
    }

    // TOKENIZER.ENCODE_SEQUENCE(handle, text$, max_len, add_specials_bool) -> Map
    BasicValue builtin_tokenizer_encode_sequence(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
        if (args.size() != 4) {
             g_error_set(8, vm.runtime_current_line, "Requires: handle, text$, max_len, add_specials_bool");
             return {};
        }
        try {
            MecanikDev::TextTokenizer& tokenizer = HandleToTokenizer(vm, args[0]);
            std::string text = g_to_string(args[1]);
            int max_len = static_cast<int>(to_double(args[2]));
            bool add_specials = to_bool(args[3]);

            // 1. Encode text, this adds [CLS]/[SEP] and truncates
            std::vector<int> ids = tokenizer.encode_sequence(text, max_len, add_specials);

            // 2. Manually create the attention mask (all 1s for these tokens)
            std::vector<int> mask(ids.size(), 1);

            // 3. Manually pad the sequence to max_length
            // This is critical for batching in PyTorch
            int num_to_pad = max_len - static_cast<int>(ids.size());
            if (num_to_pad > 0) {
                int pad_id = tokenizer.get_pad_id();
                for (int i = 0; i < num_to_pad; ++i) {
                    ids.push_back(pad_id);
                    mask.push_back(0); // '0' for padding tokens in the mask
                }
            }

            // 4. Package into a jdBasic Map
            auto result_map = std::make_shared<Map>();
            result_map->data["input_ids"] = VectorIntToBasicArray(ids);
            result_map->data["attention_mask"] = VectorIntToBasicArray(mask);
            
            return result_map;

        } catch (const std::exception& e) {
            g_error_set(15, vm.runtime_current_line, e.what());
            return {};
        }
    }


    // --- 5. Registration Logic ---
    void register_tokenizer_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
        auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr, bool is_proc = false) {
            NeReLaBasic::FunctionInfo info;
            info.name = name;
            info.arity = arity;
            info.native_impl = func_ptr;
            info.is_procedure = is_proc;
            table[g_to_upper(name)] = info;
        };

        register_func("TOKENIZER.NEW", 0, builtin_tokenizer_new);
        register_func("TOKENIZER.SET_LOWERCASE", 2, builtin_tokenizer_set_lowercase, true); // Procedure
        register_func("TOKENIZER.SET_SPLIT_ON_PUNCTUATION", 2, builtin_tokenizer_set_split_on_punctuation, true); // Procedure
        register_func("TOKENIZER.LOAD_VOCAB", 2, builtin_tokenizer_load_vocab, true); // Procedure
        register_func("TOKENIZER.GET_PAD_ID", 1, builtin_tokenizer_get_pad_id);
        register_func("TOKENIZER.ENCODE_SEQUENCE", 4, builtin_tokenizer_encode_sequence);
    }

} // anonymous namespace


/**
 * @brief The main entry point of the DLL.
 */
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }

    // Store the callback function pointers
    g_error_set = services->error_set;
    g_to_upper = services->to_upper;
    g_to_string = services->to_string;

    // Register all our new Tokenizer functions
    register_tokenizer_functions(*vm, vm->main_function_table);
}
