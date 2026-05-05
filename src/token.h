#pragma once
#include <string>
#include <unordered_map>

enum class TokenType {
    // Literals
    INTEGER_LIT, FLOAT_LIT, STRING_LIT, IDENTIFIER,

    // Keywords
    LET, DIM, AS, PRINT, INPUT, GOTO, IF, THEN, ELSE, ELSEIF,
    END, SUB, FUNCTION, DO, WHILE, UNTIL, LOOP, RETURN, CALL,
    FOR, TO, STEP, NEXT,
    ENUM, ENDENUM,
    SWITCH, CASE, DEFAULT, ENDSWITCH,
    TRY, CATCH, FINALLY, ENDTRY, THROW_KW,
    ENDFUNC, ENDSUB, ENDIF_KW,
    TYPE_KW, ENDTYPE, THIS_KW,
    EACH, IN, SLEEP_KW, STOP_KW, COLOR_KW, LOCATE_KW, CURSOR_KW,
    CLS_KW, OPTION_KW,
    // Graphics keywords
    SCREEN_KW, SCREENFLIP_KW, DRAWCOLOR_KW, SETFONT_KW,
    PSET_KW, LINE_KW, RECT_KW, CIRCLE_KW, ELLIPSE_KW,
    ROUNDED_RECT_KW, CIRCLE_SECTOR_KW, TEXT_KW,
    PLOTRAW_KW, TOGGLE_FULLSCREEN_KW,
    // Event keywords
    ON_KW, RAISEEVENT_KW,
    // Module keywords
    IMPORT_KW, MODULE_KW, EXPORT_KW,
    // FFI keyword
    DECLARE_KW,
    CONST_KW,
    EXITFUNC, EXITDO, EXITFOR,
    CONTINUEFOR, CONTINUEDO, HELP_KW,
    LAMBDA, ARROW, PIPE, PLACEHOLDER, USE,
    ASYNC, AWAIT_KW,
    TRUE_KW, FALSE_KW,
    STATIC_KW,

    // Type keywords
    TY_BOOLEAN, TY_BYTE, TY_CHAR, TY_INT16, TY_INT32, TY_INT64,
    TY_FLOAT16, TY_FLOAT32, TY_FLOAT64, TY_STRING, TY_OBJECT,
    TY_TENSOR, TY_ARRAY,

    // Logical operators
    AND, OR, NOT, ANDALSO, ORELSE,

    // Bitwise operators
    BAND, BOR, XOR, BXOR, BNOT, SHL, SHR,

    // Comparison
    GT, LT, GE, LE, NE, EQ,

    // Arithmetic
    PLUS, MINUS, STAR, SLASH, BACKSLASH, CARET, MOD,

    // Punctuation
    LPAREN, RPAREN, LBRACKET, RBRACKET,
    COMMA, COLON, SEMICOLON, DOT, ASSIGN, AT,
    LBRACE, RBRACE,

    // Special
    NEWLINE, OF, EOF_TOKEN
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int col;
};

inline const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> kw = {
        {"LET", TokenType::LET}, {"DIM", TokenType::DIM}, {"AS", TokenType::AS},
        {"PRINT", TokenType::PRINT}, {"INPUT", TokenType::INPUT}, {"GOTO", TokenType::GOTO},
        {"IF", TokenType::IF}, {"THEN", TokenType::THEN}, {"ELSE", TokenType::ELSE},
        {"ELSEIF", TokenType::ELSEIF}, {"END", TokenType::END},
        {"SUB", TokenType::SUB}, {"FUNCTION", TokenType::FUNCTION},
        {"DO", TokenType::DO}, {"WHILE", TokenType::WHILE}, {"UNTIL", TokenType::UNTIL},
        {"LOOP", TokenType::LOOP}, {"RETURN", TokenType::RETURN}, {"CALL", TokenType::CALL},
        {"FOR", TokenType::FOR}, {"TO", TokenType::TO},
        {"STEP", TokenType::STEP}, {"NEXT", TokenType::NEXT},
        {"ENUM", TokenType::ENUM}, {"ENDENUM", TokenType::ENDENUM},
        {"SWITCH", TokenType::SWITCH}, {"CASE", TokenType::CASE},
        {"DEFAULT", TokenType::DEFAULT}, {"ENDSWITCH", TokenType::ENDSWITCH},
        {"FUNC", TokenType::FUNCTION},
        {"ENDFUNC", TokenType::ENDFUNC}, {"ENDSUB", TokenType::ENDSUB},
        {"ENDIF", TokenType::ENDIF_KW},
        {"TYPE", TokenType::TYPE_KW}, {"ENDTYPE", TokenType::ENDTYPE},
        {"THIS", TokenType::THIS_KW},
        {"EACH", TokenType::EACH}, {"IN", TokenType::IN},
        {"SLEEP", TokenType::SLEEP_KW}, {"STOP", TokenType::STOP_KW},
        {"COLOR", TokenType::COLOR_KW}, {"LOCATE", TokenType::LOCATE_KW},
        {"CURSOR", TokenType::CURSOR_KW}, {"CLS", TokenType::CLS_KW},
        {"OPTION", TokenType::OPTION_KW},
        {"EXITFUNC", TokenType::EXITFUNC}, {"EXITDO", TokenType::EXITDO},
        {"EXITFOR", TokenType::EXITFOR},
        {"CONTINUEFOR", TokenType::CONTINUEFOR}, {"CONTINUEDO", TokenType::CONTINUEDO},
        {"HELP", TokenType::HELP_KW},
        {"CONTINUELOOP", TokenType::CONTINUEDO},
        {"REACT", TokenType::IDENTIFIER},  // handled in DIM parser, not a keyword
        // Graphics keywords
        {"SCREEN", TokenType::SCREEN_KW}, {"SCREENFLIP", TokenType::SCREENFLIP_KW},
        {"DRAWCOLOR", TokenType::DRAWCOLOR_KW}, {"SETFONT", TokenType::SETFONT_KW},
        {"PSET", TokenType::PSET_KW}, {"LINE", TokenType::LINE_KW},
        {"RECT", TokenType::RECT_KW}, {"CIRCLE", TokenType::CIRCLE_KW},
        {"ELLIPSE", TokenType::ELLIPSE_KW}, {"ROUNDED_RECT", TokenType::ROUNDED_RECT_KW},
        {"CIRCLE_SECTOR", TokenType::CIRCLE_SECTOR_KW}, {"TEXT", TokenType::TEXT_KW},
        {"PLOTRAW", TokenType::PLOTRAW_KW}, {"TOGGLE_FULLSCREEN", TokenType::TOGGLE_FULLSCREEN_KW},
        {"ON", TokenType::ON_KW}, {"RAISEEVENT", TokenType::RAISEEVENT_KW},
        {"IMPORT", TokenType::IMPORT_KW}, {"MODULE", TokenType::MODULE_KW},
        {"EXPORT", TokenType::EXPORT_KW},
        {"DECLARE", TokenType::DECLARE_KW},
        {"CONST", TokenType::CONST_KW},
        {"LAMBDA", TokenType::LAMBDA}, {"USE", TokenType::USE},
        {"ASYNC", TokenType::ASYNC}, {"AWAIT", TokenType::AWAIT_KW},
        {"TRY", TokenType::TRY}, {"CATCH", TokenType::CATCH},
        {"FINALLY", TokenType::FINALLY}, {"ENDTRY", TokenType::ENDTRY},
        {"THROW", TokenType::THROW_KW},
        {"TRUE", TokenType::TRUE_KW}, {"FALSE", TokenType::FALSE_KW},
        {"STATIC", TokenType::STATIC_KW},
        {"AND", TokenType::AND}, {"OR", TokenType::OR}, {"NOT", TokenType::NOT},
        {"ANDALSO", TokenType::ANDALSO}, {"ORELSE", TokenType::ORELSE},
        {"BAND", TokenType::BAND}, {"BOR", TokenType::BOR},
        {"XOR", TokenType::XOR}, {"BXOR", TokenType::BXOR},
        {"BNOT", TokenType::BNOT},
        {"SHL", TokenType::SHL}, {"SHR", TokenType::SHR},
        {"MOD", TokenType::MOD}, {"OF", TokenType::OF},
        {"BOOLEAN", TokenType::TY_BOOLEAN}, {"BYTE", TokenType::TY_BYTE},
        {"CHAR", TokenType::TY_CHAR}, {"INT16", TokenType::TY_INT16},
        {"INT32", TokenType::TY_INT32}, {"INT64", TokenType::TY_INT64},
        {"FLOAT16", TokenType::TY_FLOAT16}, {"FLOAT32", TokenType::TY_FLOAT32},
        {"FLOAT64", TokenType::TY_FLOAT64}, {"STRING", TokenType::TY_STRING},
        {"OBJECT", TokenType::TY_OBJECT}, {"TENSOR", TokenType::TY_TENSOR},
        {"ARRAY", TokenType::TY_ARRAY},
        // Classic-BASIC type aliases — friendlier than INT64/FLOAT64
        {"INTEGER", TokenType::TY_INT64},
        {"LONG",    TokenType::TY_INT64},
        {"SHORT",   TokenType::TY_INT16},
        {"DOUBLE",  TokenType::TY_FLOAT64},
        {"SINGLE",  TokenType::TY_FLOAT32},
        {"BOOL",    TokenType::TY_BOOLEAN},
    };
    return kw;
}
