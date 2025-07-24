// Statements.cpp
#include "Statements.hpp"
#include <string>
#include <unordered_map>
#include <cctype> // For toupper
#include <algorithm>

namespace {
    // Helper function to convert a string to uppercase for case-insensitive matching.
    std::string to_upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::toupper(c); }
        );
        return s;
    }

    // The keyword lookup table. Using std::unordered_map is very efficient.
    const std::unordered_map<std::string, Tokens::ID> keyword_map = {
        {"LIST",    Tokens::ID::LIST},
        {"RUN",     Tokens::ID::RUN},
        {"EDIT",    Tokens::ID::EDIT},
        {"SAVE",     Tokens::ID::SAVE},
        {"LOAD",    Tokens::ID::LOAD},
        {"DIM",     Tokens::ID::DIM},
        {"FUNC",    Tokens::ID::FUNC},
        {"ENDFUNC", Tokens::ID::ENDFUNC},
        {"SUB",    Tokens::ID::SUB},
        {"ENDSUB", Tokens::ID::ENDSUB},
        {"AWAIT",   Tokens::ID::AWAIT},
        {"ASYNC",   Tokens::ID::ASYNC},
        {"THREAD",   Tokens::ID::THREAD},
        {"TYPE",    Tokens::ID::TYPE},      
        {"ENDTYPE",Tokens::ID::ENDTYPE},  
        {"FOR",     Tokens::ID::FOR},
        {"IF",      Tokens::ID::IF},
        {"THEN",    Tokens::ID::THEN},
        {"ELSE",    Tokens::ID::ELSE},
        {"ELSEIF",  Tokens::ID::ELSEIF},
        {"ENDIF",   Tokens::ID::ENDIF},
        {"AND",     Tokens::ID::AND},
        {"OR",      Tokens::ID::OR},
        {"NOT",     Tokens::ID::NOT},
        {"MOD",     Tokens::ID::MOD},
        {"XOR",     Tokens::ID::XOR},
        {"TRUE",    Tokens::ID::JD_TRUE},
        {"FALSE",   Tokens::ID::JD_FALSE},
        {"GOTO",    Tokens::ID::GOTO},
        {"PRINT",   Tokens::ID::PRINT},
        {"RETURN",  Tokens::ID::RETURN},
        {"TO",      Tokens::ID::TO},
        {"NEXT",    Tokens::ID::NEXT},
        {"STEP",    Tokens::ID::STEP},
        {"DO",      Tokens::ID::DO},     
        {"LOOP",    Tokens::ID::LOOP},   
        {"WHILE",   Tokens::ID::WHILE},  
        {"UNTIL",   Tokens::ID::UNTIL},  
        {"EXITFOR", Tokens::ID::EXIT_FOR}, 
        {"EXITDO",  Tokens::ID::EXIT_DO},  
        {"INPUT",   Tokens::ID::INPUT},
        {"TRON",    Tokens::ID::TRON},
        {"TROFF",   Tokens::ID::TROFF},
        {"DUMP",    Tokens::ID::DUMP},
        {"COMPILE", Tokens::ID::COMPILE},
        {"REM",     Tokens::ID::REM},
        {"MODULE",  Tokens::ID::MODULE},
        {"EXPORT",  Tokens::ID::EXPORT},
        {"IMPORT",  Tokens::ID::IMPORT},
        {"DLLIMPORT",  Tokens::ID::DLLIMPORT},
        {"AS",      Tokens::ID::AS},
        {"MAP",     Tokens::ID::MAP},
        {"JSON",    Tokens::ID::JSON},
        {"DATE",    Tokens::ID::DATE},    
        {"BOOLEAN", Tokens::ID::BOOL},   
        {"BOOL",    Tokens::ID::BOOL},   
        {"INTEGER", Tokens::ID::INT},
        {"DOUBLE",  Tokens::ID::DOUBLE},
        {"STRING",  Tokens::ID::STRTYPE},
        {"TENSOR",  Tokens::ID::TENSOR},
        {"STOP",    Tokens::ID::STOP},    
        {"RESUME",  Tokens::ID::RESUME},  
        {"ON",      Tokens::ID::ON},
        {"CALL",    Tokens::ID::CALL},
        {"RAISEEVENT", Tokens::ID::RAISEEVENT},
        {"LAMBDA",  Tokens::ID::LAMBDA},
        {"THIS",    Tokens::ID::THIS_KEYWORD},
        {"BAND",    Tokens::ID::BAND},
        {"BOR",     Tokens::ID::BOR},
        {"BXOR",    Tokens::ID::BXOR},
        {"ANDALSO", Tokens::ID::ANDALSO},
        {"ORELSE",  Tokens::ID::ORELSE},
        {"TRY",     Tokens::ID::TRY},
        {"CATCH",   Tokens::ID::CATCH},
        {"FINALLY", Tokens::ID::FINALLY},
        {"ENDTRY",  Tokens::ID::ENDTRY},
        {"END",     Tokens::ID::END}
    };
} // end anonymous namespace

Tokens::ID Statements::get(const std::string& statement) {
    // Convert the input to uppercase to make the BASIC dialect case-insensitive.
    std::string upper_statement = to_upper(statement);

    // map.find() returns an iterator to the element, or map.end() if not found.
    auto it = keyword_map.find(upper_statement);

    if (it != keyword_map.end()) {
        // We found the keyword in the map, return its token.
        return it->second; // it->second is the value (the Token::ID)
    }

    // It's not a keyword.
    return Tokens::ID::NOCMD;
}