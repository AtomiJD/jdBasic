// Compiler.cpp
#include "Compiler.hpp"
#include "NeReLaBasic.hpp" // Now safe to include the full header
//#include "Parser.hpp"
#include "Error.hpp"
#include "Statements.hpp"
#include "StringUtils.hpp"
#include "TextIO.hpp"
#include <sstream>
#include <fstream>

Compiler::Compiler() {
    // Constructor can be used to initialize any specific compiler state if needed.
}

// Move the body of NeReLaBasic::parse here
Tokens::ID Compiler::parse(NeReLaBasic& vm, bool is_start_of_statement) {
    // The entire body of the original parse function goes here.
    // Replace member access:
    //  'buffer' -> 'vm.buffer'
    //  'prgptr' -> 'vm.prgptr'
    //  'lineinput' -> 'vm.lineinput'
    //  'active_function_table' -> 'vm.active_function_table'
    //  'builtin_constants' -> 'vm.builtin_constants'
    //  'Error::set(1, runtime_current_line)' -> 'Error::set(1, vm.runtime_current_line)'
    // ...etc.
    vm.buffer.clear();

    // --- Step 1: Skip any leading whitespace ---
    while (vm.prgptr < vm.lineinput.length() && StringUtils::isspace(vm.lineinput[vm.prgptr])) {
        vm.prgptr++;
    }

    // --- Step 2: Check for end of line ---
    if (vm.prgptr >= vm.lineinput.length()) {
        return Tokens::ID::NOCMD;
    }

    // --- Step 3: Check for each token type in order ---
    char currentChar = vm.lineinput[vm.prgptr];

    // Handle Comments
    if (currentChar == '\'') {
        vm.prgptr = vm.lineinput.length(); // Skip to end of line
        return parse(vm, is_start_of_statement); // Call parse again to get the NOCMD token
    }

    // Handle String Literals (including "")
    if (currentChar == '"') {
        vm.prgptr++; // Consume opening "
        size_t string_start_pos = vm.prgptr;
        while (vm.prgptr < vm.lineinput.length() && vm.lineinput[vm.prgptr] != '"') {
            vm.prgptr++;
        }
        if (vm.prgptr < vm.lineinput.length() && vm.lineinput[vm.prgptr] == '"') {
            vm.buffer = vm.lineinput.substr(string_start_pos, vm.prgptr - string_start_pos);
            vm.prgptr++; // Consume closing "
            return Tokens::ID::STRING;
        }
        else {
            Error::set(1, vm.runtime_current_line); // Unterminated string
            return Tokens::ID::NOCMD;
        }
    }

    // Handle Numbers
    if (StringUtils::isdigit(currentChar) || (currentChar == '_' && StringUtils::isdigit(vm.lineinput[vm.prgptr + 1]))) {
        size_t num_start_pos = vm.prgptr;
        while (vm.prgptr < vm.lineinput.length() && (StringUtils::isdigit(vm.lineinput[vm.prgptr]) || vm.lineinput[vm.prgptr] == '.')) {
            vm.prgptr++;
        }
        vm.buffer = vm.lineinput.substr(num_start_pos, vm.prgptr - num_start_pos);
        return Tokens::ID::NUMBER;
    }

    // --- Prioritize Multi-Word Keywords (BEFORE single identifiers) ---
    // This is a simple linear scan. For more keywords, consider a more efficient
    // structure like a Trie or a specialized keyword matcher.

    // Attempt to match "ON ERROR CALL"
    if (vm.prgptr + 13 <= vm.lineinput.length() && // "ON ERROR CALL" is 13 chars long (including spaces)
        StringUtils::to_upper(vm.lineinput.substr(vm.prgptr, 13)) == "ON ERROR CALL") {
        vm.buffer = "ON ERROR CALL"; // Store the full keyword
        vm.prgptr += 14;           // Advance past the keyword
        return Tokens::ID::ONERRORCALL;
    }
    // Add other multi-word keywords here if you introduce them (e.g., "ON GOSUB")
    // Example: if (prgptr + X <= lineinput.length() && StringUtils::to_upper(lineinput.substr(prgptr, X)) == "ANOTHER MULTI WORD KEYWORD") { ... }


    // Handle Identifiers (Variables, Keywords, Functions)
    if (StringUtils::isletter(currentChar)) {
        size_t ident_start_pos = vm.prgptr;
        // Loop to capture the entire qualified name (e.g., "MATH.ADD" or "X")
        while (vm.prgptr < vm.lineinput.length()) {
            // Capture the current part of the identifier (e.g., "MATH" or "ADD")
            size_t part_start = vm.prgptr;
            while (vm.prgptr < vm.lineinput.length() && (StringUtils::isletter(vm.lineinput[vm.prgptr]) || StringUtils::isdigit(vm.lineinput[vm.prgptr]) || vm.lineinput[vm.prgptr] == '_')) {
                vm.prgptr++;
            }
            // If the part is empty, it's an error (e.g. "MODULE..FUNC") but we let it pass for now
            if (vm.prgptr == part_start) break;

            // If we see a dot, loop again for the next part
            if (vm.prgptr < vm.lineinput.length() && vm.lineinput[vm.prgptr] == '.') {
                vm.prgptr++;
            }
            else {
                // No more dots, so the full identifier is complete
                break;
            }
        }
        vm.buffer = vm.lineinput.substr(ident_start_pos, vm.prgptr - ident_start_pos);
        vm.buffer = StringUtils::to_upper(vm.buffer);

        if (vm.builtin_constants.count(vm.buffer)) {
            return Tokens::ID::CONSTANT;
        }

        if (vm.prgptr < vm.lineinput.length() && vm.lineinput[vm.prgptr] == '$') {
            vm.buffer += '$';
            vm.prgptr++;
        }

        Tokens::ID keywordToken = Statements::get(vm.buffer);
        if (keywordToken != Tokens::ID::NOCMD) {
            return keywordToken;
        }

        if (is_start_of_statement) {
            if (vm.active_function_table->count(vm.buffer) && vm.active_function_table->at(vm.buffer).is_procedure) {
                return Tokens::ID::CALLSUB; // It's a command-style procedure call!
            }
        }

        size_t suffix_ptr = vm.prgptr;
        while (suffix_ptr < vm.lineinput.length() && StringUtils::isspace(vm.lineinput[suffix_ptr])) {
            suffix_ptr++;
        }
        char action_suffix = (suffix_ptr < vm.lineinput.length()) ? vm.lineinput[suffix_ptr] : '\0';

        if (is_start_of_statement && action_suffix == ':') {
            vm.prgptr = suffix_ptr + 1;
            return Tokens::ID::LABEL;
        }

        if (action_suffix == '(') {
            vm.prgptr = suffix_ptr;
            return Tokens::ID::CALLFUNC;
        }

        // --- MODIFIED LOGIC FOR ACCESSORS ---
        // If at the start of a statement, an identifier followed by a bracket is an assignment target.
        // This is a LET statement, so we generate a special token for the do_let command.
        if (is_start_of_statement) {
            if (action_suffix == '[') {
                vm.prgptr = suffix_ptr;
                return Tokens::ID::ARRAY_ACCESS;
            }
            if (action_suffix == '{') {
                vm.prgptr = suffix_ptr;
                return Tokens::ID::MAP_ACCESS;
            }
        }

        if (is_start_of_statement && action_suffix != '=') {
            // If it's explicitly called with parens, it's a function call statement.
            if (action_suffix == '(') {
                vm.prgptr = suffix_ptr;
                return Tokens::ID::CALLFUNC;
            }
            // Otherwise, it's a procedure-style call like `MySub` or `obj.Quit`.
            return Tokens::ID::CALLSUB;
        }
        // If not at the start of a statement, it's part of an expression.
        // The brackets will be tokenized separately as C_LEFTBRACKET / C_LEFTBRACE,
        // allowing the expression evaluator to handle chained calls.

        if (action_suffix == '@') {
            vm.prgptr = suffix_ptr + 1;
            return Tokens::ID::FUNCREF;
        }

        // If none of the above special cases match, it's a variable.
        if (vm.buffer.back() == '$') {
            return Tokens::ID::STRVAR;
        }
        else {
            return Tokens::ID::VARIANT;
        }
    }

    // Handle Multi-Character Operators
    switch (currentChar) {
    case '<':
        if (vm.prgptr + 1 < vm.lineinput.length()) {
            if (vm.lineinput[vm.prgptr + 1] == '>') { vm.prgptr += 2; return Tokens::ID::C_NE; }
            if (vm.lineinput[vm.prgptr + 1] == '=') { vm.prgptr += 2; return Tokens::ID::C_LE; }
        }
        vm.prgptr++;
        return Tokens::ID::C_LT;
    case '>':
        if (vm.prgptr + 1 < vm.lineinput.length() && vm.lineinput[vm.prgptr + 1] == '=') {
            vm.prgptr += 2; return Tokens::ID::C_GE;
        }
        vm.prgptr++;
        return Tokens::ID::C_GT;
    }

    // Handle all other single-character tokens
    vm.prgptr++; // Consume the character
    switch (currentChar) {
    case ',': return Tokens::ID::C_COMMA;
    case ';': return Tokens::ID::C_SEMICOLON;
    case '+': return Tokens::ID::C_PLUS;
    case '-': return Tokens::ID::C_MINUS;
    case '*': return Tokens::ID::C_ASTR;
    case '/': return Tokens::ID::C_SLASH;
    case '(': return Tokens::ID::C_LEFTPAREN;
    case ')': return Tokens::ID::C_RIGHTPAREN;
    case '=': return Tokens::ID::C_EQ;
    case '[': return Tokens::ID::C_LEFTBRACKET;
    case ']': return Tokens::ID::C_RIGHTBRACKET;
    case '{': return Tokens::ID::C_LEFTBRACE;
    case '}': return Tokens::ID::C_RIGHTBRACE;
    case '.': return Tokens::ID::C_DOT;
    case ':': return Tokens::ID::C_COLON;
    case '^': return Tokens::ID::C_CARET;
    }

    // If we get here, the character is not recognized.
    Error::set(1, vm.current_source_line);
    return Tokens::ID::NOCMD;
}

uint8_t Compiler::tokenize(NeReLaBasic& vm, const std::string& line, uint16_t lineNumber, std::vector<uint8_t>& out_p_code, NeReLaBasic::FunctionTable& compilation_func_table) {

    vm.lineinput = line;
    vm.prgptr = 0;

    // Write the line number prefix for this line's bytecode.
    out_p_code.push_back(lineNumber & 0xFF);
    out_p_code.push_back((lineNumber >> 8) & 0xFF);

    bool is_start_of_statement = true;
    bool is_one_liner_if = false;
    int brace_nesting_level = 0;
    int bracket_nesting_level = 0;

    // This pointer will track if a LOOP statement was found on the current line,
    // so we know to patch its jumps after the whole line is tokenized.
    DoLoopInfo* loop_to_patch_ptr = nullptr;

    // Variables to track DO/LOOP specific state for the current line
    bool encountered_do_on_line = false;
    bool encountered_loop_on_line = false;

    // Single loop to process all tokens on the line.
    while (vm.prgptr < vm.lineinput.length()) {

        bool is_exported = false;
        Tokens::ID token = parse(vm, is_start_of_statement);

        if (token == Tokens::ID::EXPORT) {
            is_exported = true;
            // It was an export, so get the *next* token (e.g., FUNC)
            token = parse(vm, false);
        }

        bool is_async_func = false;
        if (token == Tokens::ID::ASYNC) {
            // We found ASYNC. The next token should be FUNC or SUB.
            is_async_func = true;
            token = parse(vm, false); // Get the next token
        }

        if (Error::get() != 0) return Error::get();
        if (token == Tokens::ID::NOCMD) break; // End of line reached.

        // *** NEW: Update nesting level based on token ***
        if (token == Tokens::ID::C_LEFTBRACE) brace_nesting_level++;
        if (token == Tokens::ID::C_RIGHTBRACE && brace_nesting_level > 0) brace_nesting_level--;
        if (token == Tokens::ID::C_LEFTBRACKET) bracket_nesting_level++;
        if (token == Tokens::ID::C_RIGHTBRACKET && bracket_nesting_level > 0) bracket_nesting_level--;

        // *** MODIFIED: Only treat colon as a statement separator if at the top level ***
        if (token == Tokens::ID::C_COLON && brace_nesting_level == 0 && bracket_nesting_level == 0) {
            is_start_of_statement = true;
        }

        else if (token != Tokens::ID::LABEL && token != Tokens::ID::REM) {
            is_start_of_statement = false;
        }

        // Use a switch for special compile-time tokens.
        switch (token) {
            // --- Ignore IMPORT and MODULE keywords during this phase ---
            case Tokens::ID::IMPORT:
            case Tokens::ID::EXPORT:
                vm.prgptr = vm.lineinput.length(); // Skip the rest of the line
                continue;

                // Keywords that are ignored at compile-time (they are just markers).
            case Tokens::ID::TO:
            case Tokens::ID::STEP:
                continue; // Do nothing, just consume the token.

                // A comment token means we ignore the rest of the line.
            case Tokens::ID::REM:
                vm.prgptr = vm.lineinput.length();
                continue;

                // A label stores the current bytecode address but isn't a token itself.
            case Tokens::ID::LABEL:
                label_addresses[vm.buffer] = out_p_code.size();
                continue;

                // Handle FUNC: parse the name, write a placeholder, and store the patch address.
            case Tokens::ID::FUNC: {
                parse(vm, is_start_of_statement); // Parse the next token, which is the function name.
                NeReLaBasic::FunctionInfo info;
                info.name = StringUtils::to_upper(vm.buffer);
                info.is_exported = is_exported;
                info.is_async = is_async_func;
                info.module_name = this->current_module_name;

                // Find parentheses to get parameter string
                size_t open_paren = line.find('(', vm.prgptr);
                size_t close_paren = line.rfind(')');
                if (open_paren != std::string::npos && close_paren != std::string::npos) {
                    auto trim = [](std::string& s) {
                        s.erase(0, s.find_first_not_of(" \t\n\r"));
                        s.erase(s.find_last_not_of(" \t\n\r") + 1);
                        };
                    std::string params_str = line.substr(open_paren + 1, close_paren - (open_paren + 1));
                    std::stringstream pss(params_str);
                    std::string param;
                    while (std::getline(pss, param, ',')) {
                        trim(param);
                        if (param.length() > 2 && param.substr(param.length() - 2) == "[]") {
                            param.resize(param.length() - 2);
                        }
                        if (!param.empty()) info.parameter_names.push_back(StringUtils::to_upper(param));
                    }
                }

                info.arity = info.parameter_names.size();
                info.start_pcode = out_p_code.size() + 3; // +3 for FUNC token and 2-byte address
                compilation_func_table[info.name] = info;

                out_p_code.push_back(static_cast<uint8_t>(token));
                func_stack.push_back(out_p_code.size()); // Store address of the placeholder
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2
                vm.prgptr = vm.lineinput.length(); // The rest of the line is params, so we consume it.
                continue;
            }

                                 // Handle ENDFUNC: pop the stored address and patch the jump offset.
            case Tokens::ID::ENDFUNC: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                if (!func_stack.empty()) {
                    uint16_t func_jump_addr = func_stack.back();
                    func_stack.pop_back();
                    uint16_t jump_target = out_p_code.size();
                    out_p_code[func_jump_addr] = jump_target & 0xFF;
                    out_p_code[func_jump_addr + 1] = (jump_target >> 8) & 0xFF;
                }
                continue;
            }
            case Tokens::ID::SUB: {
                // The 'SUB' token has been consumed. The next token must be the name.
                parse(vm, is_start_of_statement);
                NeReLaBasic::FunctionInfo info;
                info.name = StringUtils::to_upper(vm.buffer);
                info.is_procedure = true;
                info.is_exported = is_exported; // Set the exported status!
                info.is_async = is_async_func;
                info.module_name = this->current_module_name;

                size_t open_paren = line.find('(', vm.prgptr);
                size_t close_paren = line.rfind(')');
                if (open_paren != std::string::npos && close_paren != std::string::npos) {
                    auto trim = [](std::string& s) {
                        s.erase(0, s.find_first_not_of(" \t\n\r"));
                        s.erase(s.find_last_not_of(" \t\n\r") + 1);
                        };
                    std::string params_str = line.substr(open_paren + 1, close_paren - (open_paren + 1));
                    std::stringstream pss(params_str);
                    std::string param;
                    while (std::getline(pss, param, ',')) {
                        trim(param);
                        if (param.length() > 2 && param.substr(param.length() - 2) == "[]") {
                            param.resize(param.length() - 2);
                        }
                        if (!param.empty()) info.parameter_names.push_back(StringUtils::to_upper(param));
                    }
                }
                else {
                    Error::set(1, vm.current_source_line);
                }

                info.arity = info.parameter_names.size();
                info.start_pcode = out_p_code.size() + 3; // +3 for SUB token and 2-byte address
                compilation_func_table[info.name] = info;

                // Write the SUB token and its placeholder jump address
                out_p_code.push_back(static_cast<uint8_t>(token));
                func_stack.push_back(out_p_code.size());
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2

                // We have processed the entire line.
                vm.prgptr = vm.lineinput.length();
                continue;
            }
            case Tokens::ID::ENDSUB: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                if (!func_stack.empty()) {
                    uint16_t func_jump_addr = func_stack.back();
                    func_stack.pop_back();
                    uint16_t jump_target = out_p_code.size();
                    out_p_code[func_jump_addr] = jump_target & 0xFF;
                    out_p_code[func_jump_addr + 1] = (jump_target >> 8) & 0xFF;
                }
                continue;
            }
            case Tokens::ID::CALLSUB: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                for (char c : vm.buffer) out_p_code.push_back(c);
                out_p_code.push_back(0); // Write the procedure name string
                // Arguments that follow will be tokenized normally.
                continue;
            }
            case Tokens::ID::GOTO: {
                // Write the GOTO command token itself
                out_p_code.push_back(static_cast<uint8_t>(token));

                // Immediately parse the next token on the line, which should be the label name
                Tokens::ID label_name_token = parse(vm, is_start_of_statement);

                // Check if we got a valid identifier for the label
                if (label_name_token == Tokens::ID::VARIANT || label_name_token == Tokens::ID::INT) {
                    // The buffer now holds the label name, so write IT to the bytecode
                    for (char c : vm.buffer) {
                        out_p_code.push_back(c);
                    }
                    out_p_code.push_back(0); // Null terminator
                }
                else {
                    // This is an error, e.g., "GOTO 123" or "GOTO +"
                    Error::set(1, vm.runtime_current_line); // Syntax Error
                }
                // We have now processed GOTO and its argument, so restart the main loop
                continue;
            }
            case Tokens::ID::IF: {
                // Write the IF token, then leave a 2-byte placeholder for the jump address.
                out_p_code.push_back(static_cast<uint8_t>(token));
                if_stack.push_back({ (uint16_t)out_p_code.size(), vm.current_source_line }); // Save address of the placeholder
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2
                break; // The expression after IF will be tokenized next
            }
            case Tokens::ID::THEN: {
                // This is the key decision point. We look ahead without consuming tokens.
                size_t peek_ptr = vm.prgptr;
                while (peek_ptr < vm.lineinput.length() && StringUtils::isspace(vm.lineinput[peek_ptr])) {
                    peek_ptr++;
                }
                // If nothing but a comment or whitespace follows THEN, it's a block IF.
                // Otherwise, it's a single-line IF.
                if (peek_ptr < vm.lineinput.length() && vm.lineinput[peek_ptr] != '\'') {
                    is_one_liner_if = true;
                }
                // We never write the THEN token to bytecode, so we just continue.
                continue;
            }
            case Tokens::ID::ELSE: {
                // A single-line IF cannot have an ELSE clause.
                if (is_one_liner_if) {
                    Error::set(1, vm.current_source_line); // Syntax Error
                    continue; // Stop processing this token
                }
                // Pop the IF's placeholder address from the stack.
                IfStackInfo if_info = if_stack.back();
                if_stack.pop_back();

                // Now, write the ELSE token and its own placeholder for jumping past the ELSE block.
                out_p_code.push_back(static_cast<uint8_t>(token));
                if_stack.push_back({ (uint16_t)out_p_code.size(), vm.current_source_line }); // Push address of ELSE's placeholder
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2
                //vm.prgptr = vm.lineinput.length(); 

                // Go back and patch the original IF's jump to point to the instruction AFTER the ELSE's placeholder.
                uint16_t jump_target = out_p_code.size();
                out_p_code[if_info.patch_address] = jump_target & 0xFF;
                out_p_code[if_info.patch_address + 1] = (jump_target >> 8) & 0xFF;
                continue;
            }
            case Tokens::ID::ENDIF: {
                // A single-line IF does not use an explicit ENDIF.
                if (is_one_liner_if) {
                    Error::set(1, vm.current_source_line); // Syntax Error
                    continue; // Stop processing this token
                }
                // Pop the corresponding IF or ELSE placeholder address from the stack.
                IfStackInfo last_if_info = if_stack.back();
                if_stack.pop_back();

                // Patch it to point to the current location.
                uint16_t jump_target = out_p_code.size();
                out_p_code[last_if_info.patch_address] = jump_target & 0xFF;
                out_p_code[last_if_info.patch_address + 1] = (jump_target >> 8) & 0xFF;

                // Don't write the ENDIF token, it's a compile-time marker.
                continue;
            }
            case Tokens::ID::CALLFUNC: {
                // The buffer holds the function name (e.g., "lall").
                // Write the CALLFUNC token, then write the function name string.
                std::string func_name = StringUtils::to_upper(vm.buffer);
                if (compilation_func_table.count(func_name) && compilation_func_table.at(func_name).is_async) {
                    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::OP_START_TASK));
                }
                else {
                    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::CALLFUNC));
                }
                for (char c : vm.buffer) out_p_code.push_back(c);
                out_p_code.push_back(0);
                continue;
            }
            case Tokens::ID::ONERRORCALL: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                parse(vm, is_start_of_statement); // Parse the next token which is the function name
                for (char c : vm.buffer) out_p_code.push_back(c);
                out_p_code.push_back(0); // Null terminator for the function name
                vm.prgptr = vm.lineinput.length(); // Consume rest of the line as it's just the function name
                continue;
            }
            case Tokens::ID::RESUME: { // Handle RESUME arguments during tokenization
                out_p_code.push_back(static_cast<uint8_t>(token));
                // Peek to see if RESUME is followed by NEXT or a string (label)
                size_t original_prgptr = vm.prgptr; // Save for lookahead
                Tokens::ID next_arg_token = parse(vm, false); // Parse without consuming
                vm.prgptr = original_prgptr; // Reset vm.prgptr

                if (next_arg_token == Tokens::ID::NEXT) {
                    parse(vm, false); // Consume NEXT
                    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::NEXT));
                }
                else if (next_arg_token == Tokens::ID::STRING) {
                    parse(vm, false); // Consume STRING
                    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::STRING));
                    for (char c : vm.buffer) out_p_code.push_back(c);
                    out_p_code.push_back(0); // Null terminator
                }
                // If no argument (simple RESUME), nothing more needs to be written to pcode.
                continue;
            }
            case Tokens::ID::DO: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                DoLoopInfo info;
                info.source_line = lineNumber;
                info.loop_start_pcode_addr = out_p_code.size() - 1; // Start of loop body (after DO token)

                // Peek for WHILE/UNTIL condition after DO
                size_t original_prgptr_before_peek = vm.prgptr;
                Tokens::ID next_token_peek = parse(vm, false);
                vm.prgptr = original_prgptr_before_peek; // Reset

                if (next_token_peek == Tokens::ID::WHILE || next_token_peek == Tokens::ID::UNTIL) {
                    info.is_pre_test = true;
                    info.condition_type = next_token_peek;

                    // Write the WHILE/UNTIL token (for runtime to evaluate condition)
                    out_p_code.push_back(static_cast<uint8_t>(next_token_peek));
                    parse(vm, false); // Consume WHILE/UNTIL keyword

                    // Write 2-byte placeholder for jump-past-loop.
                    // This will be patched by the corresponding LOOP.
                    info.condition_pcode_addr = out_p_code.size(); // Address of this placeholder 
                    out_p_code.push_back(0); // Placeholder byte 1 (LSB)
                    out_p_code.push_back(0); // Placeholder byte 2 (MSB)

                    do_loop_stack.push_back(info);
                }
                else {
                    // Simple DO loop (no condition or post-test condition)
                    info.is_pre_test = false;
                    info.condition_type = Tokens::ID::NOCMD;
                    info.condition_pcode_addr = 0; // Not applicable for this type of DO

                    do_loop_stack.push_back(info);
                }
                continue;
            }

            case Tokens::ID::LOOP: {
                // First, write the LOOP token.
                out_p_code.push_back(static_cast<uint8_t>(token));

                // Set the pointer to the loop on the stack that needs patching later.
                loop_to_patch_ptr = &do_loop_stack.back();

                if (do_loop_stack.empty()) {
                    Error::set(14, lineNumber); // Unclosed loop / LOOP without DO
                    return 1;
                }
                DoLoopInfo current_do_loop_info = do_loop_stack.back();
                // ! do_loop_stack.pop_back();

                // Peek for WHILE/UNTIL after LOOP
                size_t original_prgptr_before_peek = vm.prgptr;
                Tokens::ID next_token_peek = parse(vm, false);
                vm.prgptr = original_prgptr_before_peek; // Reset

                if (next_token_peek == Tokens::ID::WHILE || next_token_peek == Tokens::ID::UNTIL) {
                    // This is a post-test loop.
                    if (current_do_loop_info.is_pre_test) {
                        Error::set(1, lineNumber); // Syntax Error: Condition on both DO and LOOP
                        return 1;
                    }
                    current_do_loop_info.is_pre_test = false; // Confirm it's post-test
                    current_do_loop_info.condition_type = next_token_peek;

                    // Write the WHILE/UNTIL token (for runtime to evaluate condition)
                    out_p_code.push_back(static_cast<uint8_t>(next_token_peek));
                    parse(vm, false); // Consume WHILE/UNTIL keyword
                }
                // If no condition, condition_type remains NOCMD.

                // Write the loop metadata for runtime (is_pre_test, condition_type, loop_start_pcode_addr)
                out_p_code.push_back(static_cast<uint8_t>(current_do_loop_info.is_pre_test));
                out_p_code.push_back(static_cast<uint8_t>(current_do_loop_info.condition_type)); // This will be NOCMD if no explicit condition
                out_p_code.push_back(current_do_loop_info.loop_start_pcode_addr & 0xFF);
                out_p_code.push_back((current_do_loop_info.loop_start_pcode_addr >> 8) & 0xFF);

                uint16_t jump_target = out_p_code.size(); // The address immediately after this LOOP
                // *** CRITICAL PATCHING STEP ***
                // If this was a pre-test loop (DO WHILE/UNTIL), its placeholder for jumping *past* the loop
                // needs to be patched *now* to point to the instruction *after* the LOOP statement.
                if (current_do_loop_info.is_pre_test && current_do_loop_info.condition_pcode_addr != 0) {
                    out_p_code[current_do_loop_info.condition_pcode_addr] = jump_target & 0xFF;
                    out_p_code[current_do_loop_info.condition_pcode_addr + 1] = (jump_target >> 8) & 0xFF;
                }

                continue;
            }
            // CASES FOR EXIT ---
            case Tokens::ID::EXIT_FOR: {
                if (compiler_for_stack.empty()) {
                    Error::set(1, lineNumber, "EXITFOR without FOR.");
                    return 1;
                }
                out_p_code.push_back(static_cast<uint8_t>(token));
                compiler_for_stack.back().exit_patch_locations.push_back(out_p_code.size());
                out_p_code.push_back(0);
                out_p_code.push_back(0);
                continue;
            }
            case Tokens::ID::EXIT_DO: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                // Add current location to the patch list for the current DO loop
                do_loop_stack.back().exit_patch_locations.push_back(out_p_code.size());
                // Write a 2-byte placeholder
                out_p_code.push_back(0);
                out_p_code.push_back(0);
                continue;
            }
            case Tokens::ID::FOR: {
                // Push a new loop info struct onto the *compiler* stack.
                compiler_for_stack.push_back({ lineNumber });
                // Write the FOR token, the rest is handled by the expression parser at runtime.
                out_p_code.push_back(static_cast<uint8_t>(token));
                break;
            }

            case Tokens::ID::WHILE:
            case Tokens::ID::UNTIL:
                // These tokens are only consumed if they directly follow DO or LOOP.
                // If they appear elsewhere, it's a syntax error or part of an expression.
                // The `parse` method should return these as keywords if encountered standalone.
                // For DO/LOOP, they are handled in the DO/LOOP cases above.
                // If this is reached, it means WHILE/UNTIL is not correctly placed.
                // However, they *can* be part of an expression (e.g. `IF x WHILE y THEN`).
                // To clarify, this assumes they are *only* used with DO/LOOP.
                // If used in expressions, this case will require further logic.
                // For now, let's assume they are handled by their parent DO/LOOP.
                // If they appear here, it implies a syntax error.
                Error::set(1, lineNumber); // Syntax Error: WHILE/UNTIL out of place
                return 1;

            case Tokens::ID::NEXT: {
                if (compiler_for_stack.empty()) {
                    Error::set(21, lineNumber, "NEXT without FOR.");
                    return 1;
                }
                CompilerForLoopInfo& loop_info = compiler_for_stack.back();

                // First, write the NEXT token to the bytecode.
                out_p_code.push_back(static_cast<uint8_t>(token));

                // Now, get the address AFTER the NEXT token. This is the correct jump target for EXITFOR.
                uint16_t exit_jump_target = out_p_code.size();

                // Patch all pending EXIT FOR locations for this loop to jump past the NEXT command.
                for (uint16_t patch_addr : loop_info.exit_patch_locations) {
                    out_p_code[patch_addr] = exit_jump_target & 0xFF;
                    out_p_code[patch_addr + 1] = (exit_jump_target >> 8) & 0xFF;
                }

                compiler_for_stack.pop_back();

                // Consume the variable name after NEXT from the source line, as it's not needed in bytecode.
                parse(vm, is_start_of_statement);
                continue;
            }

            case Tokens::ID::AS:
                // This is a keyword we need at runtime for DIM.
                // Write the token to the bytecode stream.
                out_p_code.push_back(static_cast<uint8_t>(token));
                continue; // Continue to the next token
            
            default: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                if (token == Tokens::ID::STRING || token == Tokens::ID::VARIANT ||
                    token == Tokens::ID::FUNCREF || token == Tokens::ID::ARRAY_ACCESS || token == Tokens::ID::MAP_ACCESS ||
                    token == Tokens::ID::CALLFUNC || token == Tokens::ID::CONSTANT || token == Tokens::ID::STRVAR)
                {
                    for (char c : vm.buffer) out_p_code.push_back(c);
                    out_p_code.push_back(0);
                }
                else if (token == Tokens::ID::NUMBER) {
                    double value = std::stod(vm.buffer);
                    uint8_t double_bytes[sizeof(double)];
                    memcpy(double_bytes, &value, sizeof(double));
                    out_p_code.insert(out_p_code.end(), double_bytes, double_bytes + sizeof(double));
                }
            }
        } // switch
    }

    // --- This runs AFTER all tokens on the line have been processed ---
    if (loop_to_patch_ptr) {
        // Now, out_p_code contains the fully tokenized line, including the LOOP's condition.
        // The current size is the correct address for jumping past the loop.
        uint16_t after_loop_addr = out_p_code.size();

        // Patch any EXITDO jumps that were found inside this loop.
        for (uint16_t patch_addr : loop_to_patch_ptr->exit_patch_locations) {
            out_p_code[patch_addr] = after_loop_addr & 0xFF;
            out_p_code[patch_addr + 1] = (after_loop_addr >> 8) & 0xFF;
        }

        // Patch the pre-test jump for a DO WHILE/UNTIL ... LOOP
        if (loop_to_patch_ptr->is_pre_test && loop_to_patch_ptr->condition_pcode_addr != 0) {
            out_p_code[loop_to_patch_ptr->condition_pcode_addr] = after_loop_addr & 0xFF;
            out_p_code[loop_to_patch_ptr->condition_pcode_addr + 1] = (after_loop_addr >> 8) & 0xFF;
        }

        // Now that patching is complete, pop the loop info from the stack.
        do_loop_stack.pop_back();
    }

    if (is_one_liner_if) {
        if (!if_stack.empty()) {
            // This performs the ENDIF logic implicitly by patching the jump address.
            IfStackInfo last_if_info = if_stack.back();
            
            if_stack.pop_back();

            uint16_t jump_target = out_p_code.size();
            out_p_code[last_if_info.patch_address] = jump_target & 0xFF;
            out_p_code[last_if_info.patch_address + 1] = (jump_target >> 8) & 0xFF;
            
        }
        else {
            // This indicates a compiler logic error, but we can safeguard against it.
            Error::set(4, vm.current_source_line); // Unclosed IF
        }
    }
    // Every line of bytecode ends with a carriage return token.
    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::C_CR));
    return 0; // Success

}

// Move the body of NeReLaBasic::compile_module here
bool Compiler::compile_module(NeReLaBasic& vm, const std::string& module_name, const std::string& module_source_code) {
    // The entire body of the original compile_module function goes here.
    // Replace member access with 'vm.' or direct member access as appropriate.
    // e.g., 'this->tokenize_program(...)'
    if (vm.compiled_modules.count(module_name)) {
        return true; // Already compiled
    }

    TextIO::print("Compiling dependent module: " + module_name + "\n");

    // --- No temporary compiler instance. ---
    // We compile the module using our own instance's state, but direct the output
    // into the module's own data structures within the `compiled_modules` map.

    // 1. Create the entry for the new module to hold its data.
    vm.compiled_modules[module_name] = NeReLaBasic::BasicModule{ module_name };

    // 2. Tokenize the module's source, telling the function where to put the results.
    // We pass the module's own p_code vector and function_table by reference.
    if (this->tokenize_program(vm, vm.compiled_modules[module_name].p_code, module_source_code) != 0) {
        Error::set(1, 0); // General compilation error
        return false;
    }
    else {
        TextIO::print("OK. Modul compiled to " + std::to_string(vm.compiled_modules[module_name].p_code.size()) + " bytes.\n");
    }

    return true;
}

// Move the body of NeReLaBasic::pre_scan_and_parse_types here
void Compiler::pre_scan_and_parse_types(NeReLaBasic& vm) {
    // The entire body of the original pre_scan_and_parse_types function goes here.
    // e.g., 'vm.source_code', 'vm.user_defined_types'
    std::stringstream source_stream(vm.source_code);
    std::string line;
    bool in_type_block = false;
    NeReLaBasic::TypeInfo current_type_info;

    while (std::getline(source_stream, line)) {
        std::stringstream line_stream(line);
        std::string first_word;
        line_stream >> first_word;
        first_word = StringUtils::to_upper(first_word);

        if (in_type_block) {
            if (first_word == "ENDTYPE") {
                in_type_block = false;
                vm.user_defined_types[current_type_info.name] = current_type_info;
            }
            else if (!first_word.empty()) {
                NeReLaBasic::MemberInfo member;
                member.name = first_word;

                std::string as_word, type_word;
                line_stream >> as_word >> type_word;

                if (StringUtils::to_upper(as_word) == "AS") {
                    type_word = StringUtils::to_upper(type_word);
                    if (type_word == "STRING") member.type_id = DataType::STRING;
                    else if (type_word == "INTEGER") member.type_id = DataType::INTEGER;
                    else if (type_word == "DOUBLE") member.type_id = DataType::DOUBLE;
                    else if (type_word == "BOOLEAN") member.type_id = DataType::BOOL;
                    else if (type_word == "DATE") member.type_id = DataType::DATETIME;
                    else if (type_word == "MAP") member.type_id = DataType::MAP;
                    else if (type_word == "ARRAY") {
                        continue; // Silently ignore array members for now
                    }
                    else if (type_word == "TENSOR") {
                        continue; // Silently ignore array members for now
                    }

                }
                current_type_info.members[member.name] = member;
            }
        }
        else {
            if (first_word == "TYPE") {
                in_type_block = true;
                std::string type_name;
                line_stream >> type_name;
                current_type_info = NeReLaBasic::TypeInfo{};
                current_type_info.name = StringUtils::to_upper(type_name);
            }
        }
    }
}

// Move the body of NeReLaBasic::tokenize_program here
uint8_t Compiler::tokenize_program(NeReLaBasic& vm, std::vector<uint8_t>& out_p_code, const std::string& source) {
    // The entire body of the original tokenize_program function goes here.
    // Resetting compiler state is now done on 'this' object:
    //  'if_stack.clear();'
    //  'func_stack.clear();'
    //  'label_addresses.clear();'
    //  'do_loop_stack.clear();'
    //  ... and so on
    // The call to the single-line tokenizer is now:
    //  'if (this->tokenize(vm, line, vm.current_source_line++, out_p_code, *target_func_table) != 0) { ... }'
        // 1. Reset compiler state
    out_p_code.clear();
    if_stack.clear();
    func_stack.clear();
    label_addresses.clear();
    do_loop_stack.clear();

    vm.source_code = source; // Store source for pre-scanning

    // NEW: Pre-scan for TYPE definitions
    vm.user_defined_types.clear();
    this->pre_scan_and_parse_types(vm);

    // 2. Pre-scan to find imports and determine if we are a module
    is_compiling_module = false;
    current_module_name = "";
    std::vector<std::string> modules_to_import;
    std::stringstream pre_scan_stream(source);
    std::string line;
    while (std::getline(pre_scan_stream, line)) {
        StringUtils::trim(line);
        std::string line_upper = StringUtils::to_upper(line);
        if (line_upper.rfind("EXPORT MODULE", 0) == 0) {
            is_compiling_module = true;
            std::string temp = line_upper.substr(13);
            StringUtils::trim(temp);
            current_module_name = temp;
        }
        else if (line_upper.rfind("IMPORT", 0) == 0) {
            std::string temp = line_upper.substr(6);
            StringUtils::trim(temp);
            modules_to_import.push_back(temp);
        }
    }

    // 3. Get a pointer to the correct FunctionTable to populate.
    NeReLaBasic::FunctionTable* target_func_table = nullptr;
    if (is_compiling_module) {
        // If we are a module, our target is our own table inside the modules map.
        target_func_table = &vm.compiled_modules[current_module_name].function_table;
    }
    else {
        // If we are the main program, our target is the main function table.
        target_func_table = &vm.main_function_table;
    }

    // 4. Clear and prepare the target table for compilation.
    target_func_table->clear();
    register_builtin_functions(vm, *target_func_table);

    FunctionTable* previous_active_table = vm.active_function_table;
    vm.active_function_table = target_func_table;

    // 5. Compile dependencies (must be done AFTER setting our active table)
    if (!is_compiling_module) {
        for (const auto& mod_name : modules_to_import) {
            if (vm.compiled_modules.count(mod_name)) continue;
            std::string filename = mod_name + ".jdb";
            std::ifstream mod_file(filename);
            if (!mod_file) { 
                std::filesystem::path program_path(vm.program_to_debug);
                std::filesystem::path module_path = program_path.parent_path() / filename;

                mod_file.open(module_path); // Attempt to open the file at the new path

                if (!mod_file) {
                    Error::set(6, 0);
                    TextIO::print("? Error: Module file not found in current directory or source directory: " + filename + "\n");
                    return 1;
                }
            }
            std::stringstream buffer;
            buffer << mod_file.rdbuf();
            if (!compile_module(vm, mod_name, buffer.str())) {
                TextIO::print("? Error: Failed to compile module: " + mod_name + "\n");
                return 1;
            }
        }
        is_compiling_module = false;
        current_module_name = "";
    }

    // 6. LINK FIRST, if this is the main program
    if (!is_compiling_module) {
        for (const auto& mod_name : modules_to_import) {
            if (!vm.compiled_modules.count(mod_name)) continue;
            const NeReLaBasic::BasicModule& mod = vm.compiled_modules.at(mod_name);
            for (const auto& pair : mod.function_table) {
                if (pair.second.is_exported) {
                    std::string mangled_name = mod_name + "." + pair.first;
                    vm.main_function_table[mangled_name] = pair.second;
                }
            }
        }
    }
    // 7. Main compilation loop
    std::stringstream source_stream(source);
    vm.current_source_line = 1;
    bool skipping_type_block = false;

    while (std::getline(source_stream, line)) {
        std::stringstream temp_stream(line);
        std::string first_word;
        temp_stream >> first_word;
        first_word = StringUtils::to_upper(first_word);

        // This is the corrected logic for skipping the TYPE blocks
        if (skipping_type_block) {
            if (first_word == "ENDTYPE") {
                skipping_type_block = false;
            }
            vm.current_source_line++;
            continue;
        }
        else if (first_word == "TYPE") {
            skipping_type_block = true;
            vm.current_source_line++;
            continue;
        }

        if (tokenize(vm, line, vm.current_source_line++, out_p_code, *target_func_table) != 0) {
            vm.active_function_table = nullptr;
            return 1;
        }
    }

    // 8. Finalize p_code and linking
    out_p_code.push_back(0); out_p_code.push_back(0);
    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::NOCMD));

    if (!is_compiling_module) {
        // If we just compiled the main program, link the imported functions.
        for (const auto& mod_name : modules_to_import) {
            if (!vm.compiled_modules.count(mod_name)) continue;

            const NeReLaBasic::BasicModule& mod = vm.compiled_modules.at(mod_name);
            for (const auto& pair : mod.function_table) {
                if (pair.second.is_exported) {
                    std::string mangled_name = mod_name + "." + pair.first;
                    vm.main_function_table[mangled_name] = pair.second;
                }
            }
        }
    }

    vm.active_function_table = previous_active_table;
    return 0;
}
