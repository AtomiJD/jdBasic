// Compiler.hpp
#pragma once

#include "Tokens.hpp"
#include "NeReLaBasic.hpp"
#include "BuiltinFunctions.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <map>

// Forward-declare the main class to avoid circular includes
class NeReLaBasic;

class Compiler {
public:
    Compiler(); // Constructor

    /**
     * @brief Compiles an entire source string into a p-code vector.
     * @param vm The main interpreter instance, passed for accessing shared state.
     * @param out_p_code The vector to write the resulting bytecode into.
     * @param source The full source code as a single string.
     * @return 0 on success, non-zero on error.
     */
    uint8_t tokenize_program(NeReLaBasic& vm, std::vector<uint8_t>& out_p_code, const std::string& source);
    uint8_t tokenize_lambda(NeReLaBasic& vm, std::vector<uint8_t>& out_p_code, const std::string& source, NeReLaBasic::FunctionTable& compilation_func_table, uint16_t start_line);


    // --- Compiler-Specific State ---
    // These members are now part of the Compiler, not the VM.

    // For IF...ELSE...ENDIF blocks
    struct IfStackInfo {
        uint16_t patch_address;
        uint16_t source_line;
    };
    std::vector<IfStackInfo> if_stack;

    // For FOR...NEXT loops
    struct CompilerForLoopInfo {
        uint16_t source_line;
        std::vector<uint16_t> exit_patch_locations;
    };
    std::vector<CompilerForLoopInfo> compiler_for_stack;

    // For DO...LOOP structures
    struct DoLoopInfo {
        uint16_t loop_start_pcode_addr;
        uint16_t condition_pcode_addr;
        bool is_pre_test;
        Tokens::ID condition_type;
        uint16_t source_line;
        std::vector<uint16_t> exit_patch_locations;
    };
    std::vector<DoLoopInfo> do_loop_stack;

    // --- TRY...CATCH...FINALLY blocks ---
    struct TryBlockInfo {
        uint16_t source_line;
        // The address of the 2-byte placeholder for the CATCH block's address.
        uint16_t catch_addr_placeholder;
        // The address of the 2-byte placeholder for the FINALLY block's address.
        uint16_t finally_addr_placeholder;

        // Stores addresses of JUMP placeholders that need to be patched to point to the FINALLY block.
        std::vector<uint16_t> jump_to_finally_patches;
        // Stores addresses of JUMP placeholders that need to be patched to point *past* the FINALLY block.
        uint16_t jump_past_finally_patch = 0;

        bool has_catch = false;
        bool has_finally = false;
    };
    std::vector<TryBlockInfo> try_stack;

    // For tracking FUNC/SUB declarations and patching jumps
    std::vector<uint16_t> func_stack;

    // Maps label names to their bytecode address
    std::unordered_map<std::string, uint16_t> label_addresses;

    // This struct will hold the information we need to compile a lambda later.
    struct PendingLambda {
        std::string name;           // The unique generated name (e.g., "__LAMBDA_1")
        std::string source_code;    // The synthetic "FUNC...ENDFUNC" source
        uint16_t source_line;       // The original line number for error reporting
    };

    std::vector<PendingLambda> pending_lambdas; // Our "pending work" list

    // State for module compilation
    bool is_compiling_module = false;
    std::string current_module_name;

    std::string current_type_context;
    bool in_method_block = false;

    /**
     * @brief Tokenizes a single line of source code into p-code.
     */
    uint8_t tokenize(NeReLaBasic& vm, const std::string& line, uint16_t lineNumber, std::vector<uint8_t>& out_p_code, NeReLaBasic::FunctionTable& compilation_func_table, bool multiline=false, bool fromrepl = false);

    /**
     * @brief (Lexer) Parses the next token from the current line in the VM's state.
     */
    Tokens::ID parse(NeReLaBasic& vm, bool is_start_of_statement);
    
    /**
     * @brief Compiles a dependent module file.
     */
    bool compile_module(NeReLaBasic& vm, const std::string& module_name, const std::string& module_source_code);

    /**
     * @brief Scans source code for TYPE...ENDTYPE blocks to populate the UDT map.
     */
    void pre_scan_and_parse_types(NeReLaBasic& vm);
private:

};