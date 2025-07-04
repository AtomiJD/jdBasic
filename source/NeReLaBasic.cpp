// NeReLaBasic.cpp
#include <sstream>     
#include "Commands.hpp"
#include "BuiltinFunctions.hpp" 
#include "Statements.hpp"
#include "NeReLaBasic.hpp"
#include "TextIO.hpp"
#include "Error.hpp"
#include "StringUtils.hpp"
#include "Types.hpp"
#include "DAPHandler.hpp"
#include <iostream>
#include <fstream>   // For std::ifstream
#include <string>
#include <stdexcept>
#include <cstring>
#include <conio.h>
#include <algorithm> // for std::transform, std::find_if
#include <cctype>    // for std::isspace, std::toupper

// Forward declarations for tensor math functions from BuiltinFunctions.cpp
BasicValue tensor_add(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_subtract(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_elementwise_multiply(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_power(NeReLaBasic& vm, const BasicValue& base, const BasicValue& exponent);
BasicValue tensor_scalar_divide(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
std::shared_ptr<Array> array_add(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);
std::shared_ptr<Array> array_subtract(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);


const std::string NERELA_VERSION = "0.7.2";

void register_builtin_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate);

NeReLaBasic* g_vm_instance_ptr = nullptr;

// Helper function to convert a string from the BASIC source to a number.
// Supports decimal, hexadecimal ('$'), and binary ('%').
uint16_t stringToWord(const std::string& s) {
    if (s.empty()) return 0;
    try {
        if (s[0] == '$') {
            return static_cast<uint16_t>(std::stoul(s.substr(1), nullptr, 16));
        }
        if (s[0] == '%') {
            return static_cast<uint16_t>(std::stoul(s.substr(1), nullptr, 2));
        }
        return static_cast<uint16_t>(std::stoul(s, nullptr, 10));
    }
    catch (const std::exception&) {
        // Handle cases where the number is invalid, e.g., "12A4"
        return 0;
    }
}

void trim(std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");

    if (start == std::string::npos)
        s.clear();
    else
        s = s.substr(start, end - start + 1);
}

// --- MODIFIED: The chain resolver now handles both UDTs (Maps) and COM Objects ---
std::pair<BasicValue, std::string> NeReLaBasic::resolve_dot_chain(const std::string& chain_string) {
    std::stringstream ss(chain_string);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '.')) {
        parts.push_back(segment);
    }

    if (parts.empty()) {
        Error::set(1, runtime_current_line);
        return {};
    }

    // Get the base variable (e.g., "PLAYER").
    BasicValue current_object = get_variable(*this, to_upper(parts[0]));

    // Navigate the chain up to the second-to-last part.
    for (size_t i = 1; i < parts.size() - 1; ++i) {
        std::string& part = parts[i];

        // Check if we have a UDT (Map) or a COM object
        if (std::holds_alternative<std::shared_ptr<Map>>(current_object)) {
            auto& map_ptr = std::get<std::shared_ptr<Map>>(current_object);
            if (map_ptr && map_ptr->data.count(part)) {
                current_object = map_ptr->data.at(part);
            }
            else {
                Error::set(3, runtime_current_line, "Member not found: " + part); return {};
            }
        }
#ifdef JDCOM
        else if (std::holds_alternative<ComObject>(current_object)) {
            IDispatchPtr pDisp = std::get<ComObject>(current_object).ptr;
            if (!pDisp) { Error::set(1, runtime_current_line, "Uninitialized COM object."); return {}; }
            _variant_t result_vt;
            HRESULT hr = invoke_com_method(pDisp, part, {}, result_vt, DISPATCH_PROPERTYGET);
            if (FAILED(hr)) {
                hr = invoke_com_method(pDisp, part, {}, result_vt, DISPATCH_METHOD);
                if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM member not found: " + part); return {}; }
            }
            current_object = variant_t_to_basic_value(result_vt, *this);
        }
#endif
        else {
            Error::set(15, runtime_current_line, "Dot notation can only be used on objects and user-defined types.");
            return {};
        }
    }

    if (parts.size() > 1) {
        return { current_object, parts.back() };
    }
    else {
        return { current_object, "" };
    }
}

// Constructor: Initializes the interpreter state
NeReLaBasic::NeReLaBasic() : program_p_code(65536, 0) { // Allocate 64KB of memory
    buffer.reserve(64);
    lineinput.reserve(160);
    filename.reserve(40);
    active_function_table = &main_function_table;
    register_builtin_functions(*this, *active_function_table);
    srand(static_cast<unsigned int>(time(nullptr)));

    builtin_constants["VBNEWLINE"] = std::string("\n");
    builtin_constants["PI"] = 3.14159265358979323846;
    // Initialize ERR and ERL as global variables
    builtin_constants["ERR"] = 0.0;
    builtin_constants["ERL"] = 0.0;
}

bool NeReLaBasic::loadSourceFromFile(const std::string& filename) {
    std::ifstream infile(filename);
    if (!infile) {
        TextIO::print("Error: File not found -> " + filename + "\n");
        return false;
    }
    TextIO::print("LOADING " + filename + "\n");
    // Read the entire file into the source_code string
    source_lines.clear();
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        source_lines.push_back(line);
    }
    return true;
}

void NeReLaBasic::init_screen() {
    TextIO::setColor(fgcolor, bgcolor);
    TextIO::clearScreen();
    TextIO::print("NeReLa Basic v " + NERELA_VERSION + "\n");
    TextIO::print("(c) 2025\n\n");
}

void NeReLaBasic::init_system() {
    // Let's set the program start memory location, as in the original.
    pcode = 0;
    trace = 0;

    TextIO::print("Trace is:  ");
    TextIO::print_uw(trace);
    TextIO::nl();
}

void NeReLaBasic::init_basic() {
    TextIO::nl();
    //TextIO::print("Ready\n");
}

// The main REPL
void NeReLaBasic::start() {
    init_screen();
    init_system();
    init_basic();

    std::string inputLine;

    while (true) {
        Error::clear();
        direct_p_code.clear();
        linenr = 0;
        TextIO::print("Ready\n? ");

        if (!std::getline(std::cin, inputLine) || inputLine.empty()) {
            std::cin.clear();
            continue;
        }

        // --- Special handling for RESUME ---
        std::string temp_line = inputLine;
        StringUtils::trim(temp_line);
        if (StringUtils::to_upper(temp_line) == "RESUME") {
            if (is_stopped) {
                TextIO::print("Resuming...\n");
                is_stopped = false;
                execute(program_p_code, true); // Continues from the saved pcode
                if (Error::get() != 0) Error::print();
            }
            else {
                TextIO::print("?Nothing to resume.\n");
            }
            continue; // Go back to the REPL prompt
        }

        // Tokenize the direct-mode line, passing '0' as the line number
        active_function_table = &main_function_table;
        if (tokenize(inputLine, 0, direct_p_code, *active_function_table) != 0) {
            Error::print();
            continue;
        }
        // Execute the direct-mode p_code
        execute(direct_p_code, false);

        if (Error::get() != 0) {
            Error::print();
        }
    }
}

Tokens::ID NeReLaBasic::parse(NeReLaBasic& vm, bool is_start_of_statement) {
    buffer.clear();

    // --- Step 1: Skip any leading whitespace ---
    while (prgptr < lineinput.length() && StringUtils::isspace(lineinput[prgptr])) {
        prgptr++;
    }

    // --- Step 2: Check for end of line ---
    if (prgptr >= lineinput.length()) {
        return Tokens::ID::NOCMD;
    }

    // --- Step 3: Check for each token type in order ---
    char currentChar = lineinput[prgptr];

    // Handle Comments
    if (currentChar == '\'') {
        prgptr = lineinput.length(); // Skip to end of line
        return parse(*this, is_start_of_statement); // Call parse again to get the NOCMD token
    }

    // Handle String Literals (including "")
    if (currentChar == '"') {
        prgptr++; // Consume opening "
        size_t string_start_pos = prgptr;
        while (prgptr < lineinput.length() && lineinput[prgptr] != '"') {
            prgptr++;
        }
        if (prgptr < lineinput.length() && lineinput[prgptr] == '"') {
            buffer = lineinput.substr(string_start_pos, prgptr - string_start_pos);
            prgptr++; // Consume closing "
            return Tokens::ID::STRING;
        }
        else {
            Error::set(1, runtime_current_line); // Unterminated string
            return Tokens::ID::NOCMD;
        }
    }

    // Handle Numbers
    if (StringUtils::isdigit(currentChar) || (currentChar == '_' && StringUtils::isdigit(lineinput[prgptr + 1]))) {
        size_t num_start_pos = prgptr;
        while (prgptr < lineinput.length() && (StringUtils::isdigit(lineinput[prgptr]) || lineinput[prgptr] == '.')) {
            prgptr++;
        }
        buffer = lineinput.substr(num_start_pos, prgptr - num_start_pos);
        return Tokens::ID::NUMBER;
    }

    // --- Prioritize Multi-Word Keywords (BEFORE single identifiers) ---
    // This is a simple linear scan. For more keywords, consider a more efficient
    // structure like a Trie or a specialized keyword matcher.

    // Attempt to match "ON ERROR CALL"
    if (prgptr + 13 <= lineinput.length() && // "ON ERROR CALL" is 13 chars long (including spaces)
        StringUtils::to_upper(lineinput.substr(prgptr, 13)) == "ON ERROR CALL") {
        buffer = "ON ERROR CALL"; // Store the full keyword
        prgptr += 14;           // Advance past the keyword
        return Tokens::ID::ONERRORCALL;
    }
    // Add other multi-word keywords here if you introduce them (e.g., "ON GOSUB")
    // Example: if (prgptr + X <= lineinput.length() && StringUtils::to_upper(lineinput.substr(prgptr, X)) == "ANOTHER MULTI WORD KEYWORD") { ... }


    // Handle Identifiers (Variables, Keywords, Functions)
    if (StringUtils::isletter(currentChar)) {
        size_t ident_start_pos = prgptr;
        // Loop to capture the entire qualified name (e.g., "MATH.ADD" or "X")
        while (prgptr < lineinput.length()) {
            // Capture the current part of the identifier (e.g., "MATH" or "ADD")
            size_t part_start = prgptr;
            while (prgptr < lineinput.length() && (StringUtils::isletter(lineinput[prgptr]) || StringUtils::isdigit(lineinput[prgptr]) || lineinput[prgptr] == '_')) {
                prgptr++;
            }
            // If the part is empty, it's an error (e.g. "MODULE..FUNC") but we let it pass for now
            if (prgptr == part_start) break;

            // If we see a dot, loop again for the next part
            if (prgptr < lineinput.length() && lineinput[prgptr] == '.') {
                prgptr++;
            }
            else {
                // No more dots, so the full identifier is complete
                break;
            }
        }
        buffer = lineinput.substr(ident_start_pos, prgptr - ident_start_pos);
        buffer = to_upper(buffer);

        if (vm.builtin_constants.count(buffer)) {
            return Tokens::ID::CONSTANT;
        }

        if (prgptr < lineinput.length() && lineinput[prgptr] == '$') {
            buffer += '$';
            prgptr++;
        }

        Tokens::ID keywordToken = Statements::get(buffer);
        if (keywordToken != Tokens::ID::NOCMD) {
            return keywordToken;
        }
        if (is_start_of_statement) {
            if (vm.active_function_table->count(buffer) && vm.active_function_table->at(buffer).is_procedure) {
                return Tokens::ID::CALLSUB; // It's a command-style procedure call!
            }
        }

        size_t suffix_ptr = prgptr;
        while (suffix_ptr < lineinput.length() && StringUtils::isspace(lineinput[suffix_ptr])) {
            suffix_ptr++;
        }
        char action_suffix = (suffix_ptr < lineinput.length()) ? lineinput[suffix_ptr] : '\0';

        if (is_start_of_statement && action_suffix == ':') {
            prgptr = suffix_ptr + 1;
            return Tokens::ID::LABEL;
        }
        if (action_suffix == '(') {
            prgptr = suffix_ptr;
            return Tokens::ID::CALLFUNC;
        }

        // --- MODIFIED LOGIC FOR ACCESSORS ---
        // If at the start of a statement, an identifier followed by a bracket is an assignment target.
        // This is a LET statement, so we generate a special token for the do_let command.
        if (is_start_of_statement) {
            if (action_suffix == '[') {
                prgptr = suffix_ptr;
                return Tokens::ID::ARRAY_ACCESS;
            }
            if (action_suffix == '{') {
                prgptr = suffix_ptr;
                return Tokens::ID::MAP_ACCESS;
            }
        }
        // If not at the start of a statement, it's part of an expression.
        // The brackets will be tokenized separately as C_LEFTBRACKET / C_LEFTBRACE,
        // allowing the expression evaluator to handle chained calls.

        if (action_suffix == '@') {
            prgptr = suffix_ptr + 1;
            return Tokens::ID::FUNCREF;
        }

        // If none of the above special cases match, it's a variable.
        if (buffer.back() == '$') {
            return Tokens::ID::STRVAR;
        }
        else {
            return Tokens::ID::VARIANT;
        }
    }

    // Handle Multi-Character Operators
    switch (currentChar) {
    case '<':
        if (prgptr + 1 < lineinput.length()) {
            if (lineinput[prgptr + 1] == '>') { prgptr += 2; return Tokens::ID::C_NE; }
            if (lineinput[prgptr + 1] == '=') { prgptr += 2; return Tokens::ID::C_LE; }
        }
        prgptr++;
        return Tokens::ID::C_LT;
    case '>':
        if (prgptr + 1 < lineinput.length() && lineinput[prgptr + 1] == '=') {
            prgptr += 2; return Tokens::ID::C_GE;
        }
        prgptr++;
        return Tokens::ID::C_GT;
    }

    // Handle all other single-character tokens
    prgptr++; // Consume the character
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
    Error::set(1, current_source_line);
    return Tokens::ID::NOCMD;
}

uint8_t NeReLaBasic::tokenize(const std::string& line, uint16_t lineNumber, std::vector<uint8_t>& out_p_code, FunctionTable& compilation_func_table) {
    lineinput = line;
    prgptr = 0;

    // Write the line number prefix for this line's bytecode.
    out_p_code.push_back(lineNumber & 0xFF);
    out_p_code.push_back((lineNumber >> 8) & 0xFF);

    bool is_start_of_statement = true;
    bool is_one_liner_if = false;

    // This pointer will track if a LOOP statement was found on the current line,
    // so we know to patch its jumps after the whole line is tokenized.
    DoLoopInfo* loop_to_patch_ptr = nullptr;

    // Variables to track DO/LOOP specific state for the current line
    bool encountered_do_on_line = false;
    bool encountered_loop_on_line = false;

    // Single loop to process all tokens on the line.
    while (prgptr < lineinput.length()) {

        bool is_exported = false;
        Tokens::ID token = parse(*this, is_start_of_statement);

        if (token == Tokens::ID::EXPORT) {
            is_exported = true;
            // It was an export, so get the *next* token (e.g., FUNC)
            token = parse(*this, false);
        }

        if (Error::get() != 0) return Error::get();
        if (token == Tokens::ID::NOCMD) break; // End of line reached.

        // Any token other than a comment or label means we are no longer at the start of a statement
        if (token == Tokens::ID::C_COLON) {
            is_start_of_statement = true;
        }
        else if (token != Tokens::ID::LABEL && token != Tokens::ID::REM) {
            is_start_of_statement = false;
        }

        // Use a switch for special compile-time tokens.
        switch (token) {
                // --- Ignore IMPORT and MODULE keywords during this phase ---
            case Tokens::ID::IMPORT:
            case Tokens::ID::MODULE:
                prgptr = lineinput.length(); // Skip the rest of the line
                continue;

                // Keywords that are ignored at compile-time (they are just markers).
            case Tokens::ID::TO:
            case Tokens::ID::STEP:
                continue; // Do nothing, just consume the token.

                // A comment token means we ignore the rest of the line.
            case Tokens::ID::REM:
                prgptr = lineinput.length();
                continue;

                // A label stores the current bytecode address but isn't a token itself.
            case Tokens::ID::LABEL:
                label_addresses[buffer] = out_p_code.size();
                continue;

                // Handle FUNC: parse the name, write a placeholder, and store the patch address.
            case Tokens::ID::FUNC: {
                parse(*this, is_start_of_statement); // Parse the next token, which is the function name.
                FunctionInfo info;
                info.name = to_upper(buffer);
                info.is_exported = is_exported;
                info.module_name = this->current_module_name;

                // Find parentheses to get parameter string
                size_t open_paren = line.find('(', prgptr);
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
                        if (!param.empty()) info.parameter_names.push_back(to_upper(param));
                    }
                }

                info.arity = info.parameter_names.size();
                info.start_pcode = out_p_code.size() + 3; // +3 for FUNC token and 2-byte address
                compilation_func_table[info.name] = info;

                out_p_code.push_back(static_cast<uint8_t>(token));
                func_stack.push_back(out_p_code.size()); // Store address of the placeholder
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2
                prgptr = lineinput.length(); // The rest of the line is params, so we consume it.
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
                parse(*this, is_start_of_statement);
                FunctionInfo info;
                info.name = to_upper(buffer);
                info.is_procedure = true;
                info.is_exported = is_exported; // Set the exported status!
                info.module_name = this->current_module_name;

                size_t open_paren = line.find('(', prgptr);
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
                        if (!param.empty()) info.parameter_names.push_back(to_upper(param));
                    }
                }
                else {
                    Error::set(1, current_source_line);
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
                prgptr = lineinput.length();
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
                for (char c : buffer) out_p_code.push_back(c);
                out_p_code.push_back(0); // Write the procedure name string
                // Arguments that follow will be tokenized normally.
                continue;
            }
            case Tokens::ID::GOTO: {
                // Write the GOTO command token itself
                out_p_code.push_back(static_cast<uint8_t>(token));

                // Immediately parse the next token on the line, which should be the label name
                Tokens::ID label_name_token = parse(*this, is_start_of_statement);

                // Check if we got a valid identifier for the label
                if (label_name_token == Tokens::ID::VARIANT || label_name_token == Tokens::ID::INT) {
                    // The buffer now holds the label name, so write IT to the bytecode
                    for (char c : buffer) {
                        out_p_code.push_back(c);
                    }
                    out_p_code.push_back(0); // Null terminator
                }
                else {
                    // This is an error, e.g., "GOTO 123" or "GOTO +"
                    Error::set(1, runtime_current_line); // Syntax Error
                }
                // We have now processed GOTO and its argument, so restart the main loop
                continue;
            }
            case Tokens::ID::IF: {
                // Write the IF token, then leave a 2-byte placeholder for the jump address.
                out_p_code.push_back(static_cast<uint8_t>(token));
                if_stack.push_back({ (uint16_t)out_p_code.size(), current_source_line }); // Save address of the placeholder
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2
                break; // The expression after IF will be tokenized next
            }
            case Tokens::ID::THEN: {
                // This is the key decision point. We look ahead without consuming tokens.
                size_t peek_ptr = prgptr;
                while (peek_ptr < lineinput.length() && StringUtils::isspace(lineinput[peek_ptr])) {
                    peek_ptr++;
                }
                // If nothing but a comment or whitespace follows THEN, it's a block IF.
                // Otherwise, it's a single-line IF.
                if (peek_ptr < lineinput.length() && lineinput[peek_ptr] != '\'') {
                    is_one_liner_if = true;
                }
                // We never write the THEN token to bytecode, so we just continue.
                continue;
            }
            case Tokens::ID::ELSE: {
                // A single-line IF cannot have an ELSE clause.
                if (is_one_liner_if) {
                    Error::set(1, current_source_line); // Syntax Error
                    continue; // Stop processing this token
                }
                // Pop the IF's placeholder address from the stack.
                IfStackInfo if_info = if_stack.back();
                if_stack.pop_back();

                // Now, write the ELSE token and its own placeholder for jumping past the ELSE block.
                out_p_code.push_back(static_cast<uint8_t>(token));
                if_stack.push_back({ (uint16_t)out_p_code.size(), current_source_line }); // Push address of ELSE's placeholder
                out_p_code.push_back(0); // Placeholder byte 1
                out_p_code.push_back(0); // Placeholder byte 2
                //prgptr = lineinput.length(); 

                // Go back and patch the original IF's jump to point to the instruction AFTER the ELSE's placeholder.
                uint16_t jump_target = out_p_code.size();
                out_p_code[if_info.patch_address] = jump_target & 0xFF;
                out_p_code[if_info.patch_address + 1] = (jump_target >> 8) & 0xFF;
                continue;
            }
            case Tokens::ID::ENDIF: {
                // A single-line IF does not use an explicit ENDIF.
                if (is_one_liner_if) {
                    Error::set(1, current_source_line); // Syntax Error
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
                out_p_code.push_back(static_cast<uint8_t>(token));
                for (char c : buffer) {
                    out_p_code.push_back(c);
                }
                out_p_code.push_back(0); // Null terminator

                // The arguments inside (...) will be tokenized as a normal expression
                // by subsequent loops, which is what the evaluator expects.
                continue;
            }
            case Tokens::ID::ONERRORCALL: {
                out_p_code.push_back(static_cast<uint8_t>(token));
                parse(*this, is_start_of_statement); // Parse the next token which is the function name
                for (char c : buffer) out_p_code.push_back(c);
                out_p_code.push_back(0); // Null terminator for the function name
                prgptr = lineinput.length(); // Consume rest of the line as it's just the function name
                continue;
            }
            case Tokens::ID::RESUME: { // Handle RESUME arguments during tokenization
                out_p_code.push_back(static_cast<uint8_t>(token));
                // Peek to see if RESUME is followed by NEXT or a string (label)
                size_t original_prgptr = prgptr; // Save for lookahead
                Tokens::ID next_arg_token = parse(*this, false); // Parse without consuming
                prgptr = original_prgptr; // Reset prgptr

                if (next_arg_token == Tokens::ID::NEXT) {
                    parse(*this, false); // Consume NEXT
                    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::NEXT));
                }
                else if (next_arg_token == Tokens::ID::STRING) {
                    parse(*this, false); // Consume STRING
                    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::STRING));
                    for (char c : buffer) out_p_code.push_back(c);
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
                size_t original_prgptr_before_peek = prgptr;
                Tokens::ID next_token_peek = parse(*this, false);
                prgptr = original_prgptr_before_peek; // Reset

                if (next_token_peek == Tokens::ID::WHILE || next_token_peek == Tokens::ID::UNTIL) {
                    info.is_pre_test = true;
                    info.condition_type = next_token_peek;

                    // Write the WHILE/UNTIL token (for runtime to evaluate condition)
                    out_p_code.push_back(static_cast<uint8_t>(next_token_peek));
                    parse(*this, false); // Consume WHILE/UNTIL keyword

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
                size_t original_prgptr_before_peek = prgptr;
                Tokens::ID next_token_peek = parse(*this, false);
                prgptr = original_prgptr_before_peek; // Reset

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
                    parse(*this, false); // Consume WHILE/UNTIL keyword
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
                parse(*this, is_start_of_statement);
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
                    for (char c : buffer) out_p_code.push_back(c);
                    out_p_code.push_back(0);
                }
                else if (token == Tokens::ID::NUMBER) {
                    double value = std::stod(buffer);
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
            Error::set(4, current_source_line); // Unclosed IF
        }
    }
    // Every line of bytecode ends with a carriage return token.
    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::C_CR));
    return 0; // Success
}

void NeReLaBasic::pre_scan_and_parse_types() {
    std::stringstream source_stream(this->source_code);
    std::string line;
    bool in_type_block = false;
    TypeInfo current_type_info;

    while (std::getline(source_stream, line)) {
        std::stringstream line_stream(line);
        std::string first_word;
        line_stream >> first_word;
        first_word = StringUtils::to_upper(first_word);

        if (in_type_block) {
            if (first_word == "ENDTYPE") {
                in_type_block = false;
                this->user_defined_types[current_type_info.name] = current_type_info;
            }
            else if (!first_word.empty()) {
                MemberInfo member;
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
                current_type_info = TypeInfo{};
                current_type_info.name = StringUtils::to_upper(type_name);
            }
        }
    }
}

// --- HELPER FUNCTION TO COMPILE A MODULE FROM SOURCE ---
bool NeReLaBasic::compile_module(const std::string& module_name, const std::string& module_source_code) {
    if (this->compiled_modules.count(module_name)) {
        return true; // Already compiled
    }

    TextIO::print("Compiling dependent module: " + module_name + "\n");

    // --- No temporary compiler instance. ---
    // We compile the module using our own instance's state, but direct the output
    // into the module's own data structures within the `compiled_modules` map.

    // 1. Create the entry for the new module to hold its data.
    this->compiled_modules[module_name] = BasicModule{ module_name };

    // 2. Tokenize the module's source, telling the function where to put the results.
    // We pass the module's own p_code vector and function_table by reference.
    if (this->tokenize_program(this->compiled_modules[module_name].p_code, module_source_code) != 0) {
        Error::set(1, 0); // General compilation error
        return false;
    }
    else {
        TextIO::print("OK. Modul compiled to " + std::to_string(this->compiled_modules[module_name].p_code.size()) + " bytes.\n");
    }

    return true;
}

uint8_t NeReLaBasic::tokenize_program(std::vector<uint8_t>& out_p_code, const std::string& source) {
    // 1. Reset compiler state
    out_p_code.clear();
    if_stack.clear();
    func_stack.clear();
    label_addresses.clear();
    do_loop_stack.clear();
    
    this->source_code = source; // Store source for pre-scanning

    // NEW: Pre-scan for TYPE definitions
    this->user_defined_types.clear();
    pre_scan_and_parse_types();

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
    FunctionTable* target_func_table = nullptr;
    if (is_compiling_module) {
        // If we are a module, our target is our own table inside the modules map.
        target_func_table = &this->compiled_modules[current_module_name].function_table;
    }
    else {
        // If we are the main program, our target is the main function table.
        target_func_table = &this->main_function_table;
    }

    // 4. Clear and prepare the target table for compilation.
    target_func_table->clear();
    register_builtin_functions(*this, *target_func_table);

    FunctionTable* previous_active_table = this->active_function_table;
    this->active_function_table = target_func_table;

    // 5. Compile dependencies (must be done AFTER setting our active table)
    if (!is_compiling_module) {
        for (const auto& mod_name : modules_to_import) {
            if (compiled_modules.count(mod_name)) continue;
            std::string filename = mod_name + ".jdb";
            std::ifstream mod_file(filename);
            if (!mod_file) { Error::set(6, 0); TextIO::print("? Error: Module file not found: " + filename + "\n"); return 1; }
            std::stringstream buffer;
            buffer << mod_file.rdbuf();
            if (!compile_module(mod_name, buffer.str())) {
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
            if (!compiled_modules.count(mod_name)) continue;
            const BasicModule& mod = compiled_modules.at(mod_name);
            for (const auto& pair : mod.function_table) {
                if (pair.second.is_exported) {
                    std::string mangled_name = mod_name + "." + pair.first;
                    this->main_function_table[mangled_name] = pair.second;
                }
            }
        }
    }
    // 7. Main compilation loop
    std::stringstream source_stream(source);
    current_source_line = 1;
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
            current_source_line++;
            continue;
        }
        else if (first_word == "TYPE") {
            skipping_type_block = true;
            current_source_line++;
            continue;
        }

        if (tokenize(line, current_source_line++, out_p_code, *target_func_table) != 0) {
            this->active_function_table = nullptr;
            return 1;
        }
    }

    // 8. Finalize p_code and linking
    out_p_code.push_back(0); out_p_code.push_back(0);
    out_p_code.push_back(static_cast<uint8_t>(Tokens::ID::NOCMD));

    if (!is_compiling_module) {
        // If we just compiled the main program, link the imported functions.
        for (const auto& mod_name : modules_to_import) {
            if (!compiled_modules.count(mod_name)) continue;

            const BasicModule& mod = compiled_modules.at(mod_name);
            for (const auto& pair : mod.function_table) {
                if (pair.second.is_exported) {
                    std::string mangled_name = mod_name + "." + pair.first;
                    this->main_function_table[mangled_name] = pair.second;
                }
            }
        }
    }

    this->active_function_table = previous_active_table;
    return 0;
}

BasicValue NeReLaBasic::execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {

    UINT16 old_line = 0;

    if (func_info.native_impl != nullptr) {
        return func_info.native_impl(*this, args);
    }

    size_t initial_stack_depth = call_stack.size();

    // 1. Set up the new stack frame
    NeReLaBasic::StackFrame frame;
    frame.return_p_code_ptr = this->active_p_code;
    frame.return_pcode = this->pcode;
    frame.previous_function_table_ptr = this->active_function_table;
    frame.for_stack_size_on_entry = this->for_stack.size();
    frame.function_name = func_info.name;
    frame.linenr = runtime_current_line;

    for (size_t i = 0; i < func_info.parameter_names.size(); ++i) {
        if (i < args.size()) frame.local_variables[func_info.parameter_names[i]] = args[i];
    }
    call_stack.push_back(frame);

    // 2. --- CONTEXT SWITCH ---
    if (!func_info.module_name.empty() && compiled_modules.count(func_info.module_name)) {
        this->active_p_code = &this->compiled_modules.at(func_info.module_name).p_code;
        this->active_function_table = &this->compiled_modules.at(func_info.module_name).function_table;
    }
    // 3. Set the program counter to the function's start address IN THE NEW CONTEXT
    this->pcode = func_info.start_pcode;

    // 4. Execute statements until the function returns
    this->pcode = func_info.start_pcode;
    while (call_stack.size() > initial_stack_depth && !is_stopped) {

        if (Error::get() != 0) {
            // Unwind stack on error to prevent infinite loops
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            this->active_function_table = frame.previous_function_table_ptr; // Restore context
            return false;
        }
        if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
            Error::set(25, runtime_current_line); // Missing RETURN
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            break;
        }

        if (dap_handler) {
            // 1. Handle general PAUSED state (e.g., user manually paused)
            if (debug_state == DebugState::PAUSED) {
                pause_for_debugger();
            }

            // 2. Check for regular breakpoints
            if (breakpoints.count(runtime_current_line)) {
                // Pause if a breakpoint is hit, unless we are stepping over a call
                // and are currently inside that call (stack is deeper).
                if (debug_state != DebugState::STEP_OVER ) {
                    debug_state = DebugState::PAUSED;
                    dap_handler->send_stopped_message("breakpoint", runtime_current_line, this->program_to_debug);
                    pause_for_debugger();
                    continue; // Re-evaluate the loop after un-pausing
                }
            }
        }

        old_line = runtime_current_line;
        statement();
        // --- >> DAP INTEGRATION POINT (POST-EXECUTION) << ---
        if (dap_handler && runtime_current_line > old_line) {
            // Check if a "step over" action has completed
            if (debug_state == DebugState::STEP_OVER ) {
                debug_state = DebugState::PAUSED;
                // Update line number for the message, as the next loop hasn't run yet
                dap_handler->send_stopped_message("step", runtime_current_line, this->program_to_debug);
                pause_for_debugger();
                continue; // Re-evaluate loop after command
            }
        }

    }
    return variables["RETVAL"];
}

void NeReLaBasic::execute_repl_command(const std::vector<uint8_t>& repl_p_code) {
    if (repl_p_code.empty()) {
        return;
    }

    // --- Save the state of the main program's execution context ---
    const auto* original_active_pcode = this->active_p_code;
    uint16_t original_pcode = this->pcode;

    // --- Temporarily switch context to the REPL's p-code ---
    this->active_p_code = &repl_p_code;
    this->pcode = 0; // Start at the beginning of the REPL code

    // --- Simplified execution loop for the REPL command ---
    // This loop does NOT check for debug state, breakpoints, or pause signals.
    while (this->pcode < this->active_p_code->size()) {
        // The first two bytes of tokenized code are the line number (always 0 for REPL).
        // The main `statement()` function expects `pcode` to point AFTER these bytes.
        if (this->pcode == 0) {
            this->pcode += 2;
        }

        Tokens::ID token = static_cast<Tokens::ID>((*this->active_p_code)[this->pcode]);

        // Stop if we hit the end-of-line or end-of-code markers
        if (token == Tokens::ID::NOCMD || token == Tokens::ID::C_CR) {
            break;
        }

        // Execute one statement from the REPL command
        statement();

        // If the statement caused an error, stop processing
        if (Error::get() != 0) {
            break;
        }

        // Handle multi-statement lines separated by ':'
        if (this->pcode < this->active_p_code->size() &&
            static_cast<Tokens::ID>((*this->active_p_code)[this->pcode]) == Tokens::ID::C_COLON) {
            this->pcode++; // Consume the colon and continue the loop
        }
    }

    // --- Restore the original execution context ---
    this->active_p_code = original_active_pcode;
    this->pcode = original_pcode;
}

// In NeReLaBasic.cpp
void NeReLaBasic::pause_for_debugger() {
    std::unique_lock<std::mutex> lock(dap_mutex);
    dap_command_received = false;
    dap_cv.wait(lock, [this] { return dap_command_received; });
    dap_command_received = false;
}

void NeReLaBasic::resume_from_debugger() {
    {
        std::lock_guard<std::mutex> lock(dap_mutex);
        debug_state = DebugState::RUNNING;
        dap_command_received = true;
    }
    dap_cv.notify_one();
}

void NeReLaBasic::step_over() {
    {
        std::lock_guard<std::mutex> lock(dap_mutex);
        debug_state = DebugState::STEP_OVER;
        step_over_stack_depth = call_stack.size();
        dap_command_received = true;
    }
    dap_cv.notify_one();
}

void NeReLaBasic::execute(const std::vector<uint8_t>& code_to_run, bool resume_mode) {
    // If there's no code to run, do nothing.
    if (code_to_run.empty()) {
        return;
    }

    // Set the active p_code pointer for the duration of this execution.
    auto prev_active_p_code = active_p_code;
    active_p_code = &code_to_run;

    // Only reset the program counter if we are not resuming.
    if (!resume_mode) {
        pcode = 0;
    }

    Error::clear();
    g_vm_instance_ptr = this;

    if (dap_handler) { // Check if the debugger is attached
        debug_state = DebugState::PAUSED;
        // Tell the client we are paused at the entry point.
        runtime_current_line = (*active_p_code)[0] | ((*active_p_code)[1] << 8);
        dap_handler->send_stopped_message("entry", runtime_current_line, this->program_to_debug);
        // Wait for the first "continue" or "next" command from the client.
        pause_for_debugger();
    }

    // Main execution loop
    while (pcode < active_p_code->size() && !is_stopped) {

#ifdef SDL3
        if (graphics_system.is_initialized) { // Check if graphics are active
            if (!graphics_system.handle_events()) {
                break; // Exit execution if the user closed the window
            }
        }
#endif
        if (!nopause_active) { // Only process input if NOPAUSE is NOT active
            if (_kbhit()) {
                char key = _getch(); // Get the pressed key

                // The ESC key has an ASCII value of 27
                if (key == 27) {
                    TextIO::print("\n--- BREAK ---\n");
                    break;                   // Exit the loop immediately
                }
                // Let's use the spacebar to pause
                else if (key == ' ') {
                    TextIO::print("\n--- PAUSED (Press any key to resume) ---\n");
                    _getch(); // Wait for another key press to un-pause
                    TextIO::print("--- RESUMED ---\n");
                }
            }
        }
        // --- >> DAP INTEGRATION POINT << ---
        if (debug_state == DebugState::PAUSED) {
            // We are paused, waiting for the DAP client
            std::unique_lock<std::mutex> lock(dap_mutex);
            dap_cv.wait(lock, [this] { return dap_command_received; });
            dap_command_received = false; // Reset the flag
            dap_handler->send_output_message("We are in PAUSED");
        }

        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);

        // Use the active_p_code pointer to access the bytecode

        if (resume_mode) {
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR)
                pcode++; // Consume CR
        }

        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);

        // 2. Check for regular breakpoints.
        if (breakpoints.count(runtime_current_line)) {
            // Do NOT stop at a breakpoint if we are stepping over a line
            // and are currently inside a function call (stack is deeper).
            // dap_handler->send_output_message("jdBasic: Breakpoint:" + std::to_string(step_over_stack_depth) + ", " + std::to_string(call_stack.size()));
            if (debug_state != DebugState::STEP_OVER ) {
                debug_state = DebugState::PAUSED;
                dap_handler->send_stopped_message("breakpoint", runtime_current_line, this->program_to_debug);
                dap_handler->send_output_message("Breakpoint arrived->PAUSED");
                pause_for_debugger();
                continue;
            }
        }

        pcode += 2;
        

        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
            break;
        }
        bool line_is_done = false;
        bool statement_is_run = false;
        while (!line_is_done && pcode < active_p_code->size()) {
            current_statement_start_pcode = pcode; // Capture statement start here!
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR) {
                statement();
                statement_is_run = true;
            }
            else {
                statement_is_run = false;
            }

            if (Error::get() != 0 || is_stopped) {
                line_is_done = true; // Stop processing this line on error
                continue;
            }

            // After the statement, check the token that follows.
            if (pcode < active_p_code->size()) {
                Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (next_token == Tokens::ID::C_COLON) {
                    pcode++; // Consume the separator and loop again for the next statement.
                }
                else {
                    // Token (C_CR, NOCMD) means the line is finished.
                    if (next_token == Tokens::ID::C_CR || next_token == Tokens::ID::NOCMD) {
                        line_is_done = true;
                    }
                }
            }
            if (dap_handler && statement_is_run) {
                // 1. Check if we are in STEP_OVER mode.
                if ((debug_state == DebugState::STEP_OVER) && line_is_done == true) {
                    // We should stop if the current call stack is the same size as,
                    // or smaller than, when we started the step. This means we are
                    // on the next line in the same function, or have returned from a function.
                    
                    debug_state = DebugState::PAUSED;
                    uint16_t opcode = pcode;
                    opcode++;
                    const uint16_t debug_line = (*active_p_code)[opcode] | ((*active_p_code)[opcode+1] << 8);
                    dap_handler->send_output_message("jdBasic: step received from debugger: " + std::to_string(debug_line));
                    dap_handler->send_stopped_message("step", debug_line, this->program_to_debug);
                    pause_for_debugger();
                    // After pausing, we might get another command, so re-evaluate the loop.
                    continue;
                }


            }
        }

        // --- Error handling ---
        if (Error::get() != 0 && error_handler_active && jump_to_error_handler) {
            jump_to_error_handler = false; // Reset the flag
            // Clear the actual error code so ERR/ERL can be read by the handler
            uint8_t caught_error_code = Error::get();
            Error::clear();

            // Invoke the error handling function (like a CALLSUB)
            // You can re-use your do_callsub logic or call it directly:
            if (active_function_table->count(error_handler_function_name)) {
                const auto& proc_info = active_function_table->at(error_handler_function_name);
                // Call the procedure without arguments (ERR and ERL are global vars)
                NeReLaBasic::StackFrame frame;
                frame.return_p_code_ptr = this->active_p_code;
                frame.return_pcode = this->pcode;
                frame.previous_function_table_ptr = this->active_function_table;
                frame.for_stack_size_on_entry = this->for_stack.size();
                frame.function_name = error_handler_function_name;
                frame.linenr = runtime_current_line;
                // No args to pass if ERR and ERL are global
                this->call_stack.push_back(frame);

                if (!proc_info.module_name.empty() && compiled_modules.count(proc_info.module_name)) {
                    auto& target_module = compiled_modules[proc_info.module_name];
                    this->active_p_code = &target_module.p_code;
                    this->active_function_table = &target_module.function_table;
                }
                uint16_t current_error_pcode_snapshot = pcode; // Save where the error occurred

                // Find the end of the current logical line
                while (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::NOCMD) {
                    pcode++; // Move past the current problematic statement/expression
                }
                if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                    pcode++; // Consume C_CR
                    pcode += 2; // Consume line number bytes for the *next* line
                }
                // Now pcode points to the start of the *next* statement after the error line.
                resume_pcode_next_statement = pcode;

                // Restore pcode to where it was for ERL to be accurate in the handler
                pcode = current_error_pcode_snapshot;

                Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (next_token == Tokens::ID::C_CR)
                    pcode++; // Consume the C_CR
            }
            else {
                // If the error handler function is somehow not found at runtime,
                // this is a fatal error, print the original error and stop.
                Error::set(caught_error_code, runtime_current_line); // Re-set original error
                Error::print();
                break; // Stop execution
            }
            continue; // Continue execution loop from the new pcode (the error handler)
        }
        else {
            if (Error::get() != 0 || is_stopped) {
                break;
            }
            if (pcode < active_p_code->size() && (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD)) {
                pcode++; // Consume the C_CR
            }
        }
    }

#ifdef SDL3
    // After the loop, ensure the graphics system is shut down
    graphics_system.shutdown();
    sound_system.shutdown();
#endif

    // Clear the pointer so it's not pointing to stale data
    active_p_code = prev_active_p_code;
    // Clear the global VM pointer when execution finishes
    g_vm_instance_ptr = nullptr;
}

void NeReLaBasic::statement() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]); // Peek at the token

    // In trace mode, print the token being executed.
    if (trace == 1) {
        TextIO::print("(");
        TextIO::print_uw(runtime_current_line);
        TextIO::print("/");
        TextIO::print_uwhex(static_cast<uint8_t>(token));
        TextIO::print(")");
    }

    switch (token) {

    case Tokens::ID::DIM:
        pcode++;
        Commands::do_dim(*this);
        break;

    case Tokens::ID::INPUT:
        pcode++;
        Commands::do_input(*this);
        break;

    case Tokens::ID::PRINT:
        pcode++;
        Commands::do_print(*this); // Pass a reference to ourselves
        break;

    case Tokens::ID::VARIANT:
    case Tokens::ID::INT:
    case Tokens::ID::STRVAR:
    case Tokens::ID::ARRAY_ACCESS:
    case Tokens::ID::MAP_ACCESS:
        //pcode++;
        Commands::do_let(*this);
        break;

    case Tokens::ID::GOTO:
        pcode++;
        Commands::do_goto(*this);
        break;

    case Tokens::ID::LABEL:
    case Tokens::ID::ENDIF:
        pcode++;
        // Compile-time only, do nothing at runtime.
        break;
    case Tokens::ID::IF:
        pcode++;
        Commands::do_if(*this);
        break;
    case Tokens::ID::ELSE:
        pcode++;
        Commands::do_else(*this);
        break;
    case Tokens::ID::FOR:
        pcode++;
        Commands::do_for(*this);
        break;
    case Tokens::ID::NEXT:
        pcode++;
        Commands::do_next(*this);
        break;
    case Tokens::ID::FUNC:
        pcode++;
        Commands::do_func(*this);
        break;
    case Tokens::ID::CALLFUNC:
        pcode++;
        Commands::do_callfunc(*this);
        break;
    case Tokens::ID::ENDFUNC:
        pcode++;
        Commands::do_endfunc(*this);
        break;
    case Tokens::ID::RETURN:
        pcode++;
        Commands::do_return(*this);
        break;
    case Tokens::ID::SUB:
        pcode++;
        Commands::do_sub(*this);
        break;
    case Tokens::ID::ENDSUB:
        pcode++;
        Commands::do_endsub(*this);
        break;
    case Tokens::ID::CALLSUB:
        pcode++;
        Commands::do_callsub(*this);
        break;
    case Tokens::ID::ONERRORCALL:
        pcode++;
        Commands::do_onerrorcall(*this);
        break;
    case Tokens::ID::RESUME:
        pcode++;
        Commands::do_resume(*this);
        break;
    case Tokens::ID::LOOP:
        pcode++;
        Commands::do_loop(*this);
        break;
    case Tokens::ID::DO:
        pcode++;
        Commands::do_do(*this);
        break;
    case Tokens::ID::EXIT_FOR:
        pcode++;
        Commands::do_exit_for(*this);
        break;
    case Tokens::ID::EXIT_DO:
        pcode++;
        Commands::do_exit_do(*this);
        break;
    case Tokens::ID::EDIT:
        pcode++;
        Commands::do_edit(*this);
        break;

    case Tokens::ID::LIST:
        pcode++;
        Commands::do_list(*this);
        break;

    case Tokens::ID::LOAD:
        pcode++;
        Commands::do_load(*this);
        break;

    case Tokens::ID::SAVE:
        pcode++;
        Commands::do_save(*this);
        break;

    case Tokens::ID::COMPILE:
        pcode++;
        Commands::do_compile(*this);
        break;

    case Tokens::ID::RUN:
        pcode++;
        Commands::do_run(*this);
        break;

    case Tokens::ID::STOP:
        pcode++;
        Commands::do_stop(*this);
        break;

    case Tokens::ID::TRON:
        pcode++;
        Commands::do_tron(*this);
        break;

    case Tokens::ID::TROFF:
        pcode++;
        Commands::do_troff(*this);
        break;

    case Tokens::ID::DUMP:
        pcode++;
        Commands::do_dump(*this);
        break;

    case Tokens::ID::C_CR:
        // This token is followed by a 2-byte line number. Skip it during execution.
        pcode++;
        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
        pcode += 2;
        break;

        // --- We will add more cases here for LET, IF, GOTO, etc. ---

    default:
        // If we don't recognize the token, something is wrong.
        // This could be a token that shouldn't be executed directly, like a NUMBER.
        // For now, we'll just stop. In the future, this would be a syntax error.
        pcode++;
        Error::set(1, runtime_current_line); // Syntax Error
        break;
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

// Level 5: Handles highest-precedence items
BasicValue NeReLaBasic::parse_primary() {
    BasicValue current_value;
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::VARIANT || token == Tokens::ID::INT || token == Tokens::ID::STRVAR) {
        pcode++;
        std::string var_or_qual_name = to_upper(read_string(*this));

        if (var_or_qual_name.find('.') != std::string::npos) {
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
            pcode++;
            if (func_info.arity != -1 && args.size() != func_info.arity) { Error::set(26, runtime_current_line); return {}; }
            current_value = execute_function_for_value(func_info, args);
        }
        else if (identifier_being_called.find('.') != std::string::npos) {
#ifdef JDCOM
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
#else
            Error::set(22, runtime_current_line, "Unknown function: " + identifier_being_called); return {};
#endif
        }
        else { Error::set(22, runtime_current_line, "Unknown function: " + real_func_to_call); return {}; }
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
    else { Error::set(1, runtime_current_line); return {}; }
    if (Error::get() != 0) return {};

    // --- MAIN LOOP FOR HANDLING ACCESSORS LIKE [..], {..}, .member ---
    while (true) {
        Tokens::ID accessor_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

        if (accessor_token == Tokens::ID::C_LEFTBRACKET) { // Array index access: e.g., A[i, j]
            pcode++; // Consume '['

            std::vector<size_t> indices;
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
                while (true) {
                    BasicValue index_val = evaluate_expression();
                    if (Error::get() != 0) return {};
                    indices.push_back(static_cast<size_t>(to_double(index_val)));

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

                try {
                    size_t flat_index = arr_ptr->get_flat_index(indices);
                    // The get_flat_index function already checks bounds, but an extra check is safe.
                    if (flat_index >= arr_ptr->data.size()) {
                        throw std::out_of_range("Calculated index is out of bounds.");
                    }
                    BasicValue next_val = arr_ptr->data[flat_index];
                    current_value = std::move(next_val);
                    //current_value = arr_ptr->data[flat_index]; // Update current value with the element
                }
                catch (const std::exception&) {
                    // This catches errors from get_flat_index (dimension mismatch or out of bounds)
                    Error::set(10, runtime_current_line, "Array index out of bounds or dimension mismatch.");
                    return {};
                }
            }
            else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
                if (indices.size() != 1) {
                    Error::set(15, runtime_current_line, "Multi-dimensional indexing is not supported for JSON objects."); return {};
                }
                size_t index = indices[0];
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
            if (member_token != Tokens::ID::VARIANT && member_token != Tokens::ID::INT && member_token != Tokens::ID::STRVAR) {
                Error::set(1, runtime_current_line, "Expected member name after '.'"); return {};
            }
            pcode++;
            std::string member_name = to_upper(read_string(*this));

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
        else {
            break; // No more accessors, break the loop
        }
    }
    return current_value;
}

// Level 5: Handles unary operators like - and NOT
BasicValue NeReLaBasic::parse_unary() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::C_MINUS) {
        pcode++; // Consume the '-'
        // Recursively call to handle expressions like -(5+2) or -x
        // Note: The result of a unary minus is always a number.
        return -to_double(parse_unary());
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
        if (op == Tokens::ID::C_ASTR || op == Tokens::ID::C_SLASH || op == Tokens::ID::MOD) {
            pcode++;
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
                else { // MOD
                    Error::set(1, runtime_current_line, "MOD operator is not supported for Tensors."); return {};
                }
            }
            else {
                left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
                    using LeftT = std::decay_t<decltype(l)>; using RightT = std::decay_t<decltype(r)>;

                    auto array_op = [op, this](const auto& arr, double scalar, bool arr_is_left) -> BasicValue {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = arr->shape;
                        for (const auto& elem : arr->data) {
                            double arr_val = to_double(elem); double res = 0;
                            if (op == Tokens::ID::C_ASTR) res = arr_is_left ? arr_val * scalar : scalar * arr_val;
                            else if (op == Tokens::ID::C_SLASH) {
                                if ((arr_is_left && scalar == 0.0) || (!arr_is_left && arr_val == 0.0)) { Error::set(2, runtime_current_line, "Division by zero."); return false; }
                                res = arr_is_left ? arr_val / scalar : scalar / arr_val;
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
                            else { if (val_r == 0.0) { Error::set(2, runtime_current_line, "Division by zero."); return false; } res = static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                            result_ptr->data.push_back(res);
                        }
                        return result_ptr;
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) { return array_op(l, to_double(r), true); }
                    else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) { return array_op(r, to_double(l), false); }
                    else { // scalar op
                        double val_l = to_double(l); double val_r = to_double(r);
                        if (op == Tokens::ID::C_ASTR) return val_l * val_r;
                        if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                        if (op == Tokens::ID::MOD) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                    }
                    return false;
                    }, left, right);
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
                        if (op == Tokens::ID::C_PLUS) return to_string(l) + to_string(r);
                        else { Error::set(15, runtime_current_line, "Cannot subtract strings."); return false; }
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        // This uses the same array_add/subtract helpers from BuiltinFunctions.cpp
                        // Ensure they are globally accessible or duplicate the logic here.
                        if (op == Tokens::ID::C_PLUS) return array_add(l, r); else return array_subtract(l, r);
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
                        if (op == Tokens::ID::C_PLUS) return to_double(l) + to_double(r);
                        else return to_double(l) - to_double(r);
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

// Level 1: Handles AND, OR
BasicValue NeReLaBasic::evaluate_expression() {
    BasicValue left = parse_comparison();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::AND || op == Tokens::ID::OR) {
            pcode++;
            BasicValue right = parse_comparison();
            if (op == Tokens::ID::AND) left = to_bool(left) && to_bool(right);
            else left = to_bool(left) || to_bool(right);
        }
        else break;
    }
    return left;
}