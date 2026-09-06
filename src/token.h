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
    INTERP_STRING,          // $"text {{ expr }} text"
    COALESCE,               // ?? - the left side unless it is NONE
    ASYNC, AWAIT_KW,
    TRUE_KW, FALSE_KW,
    STATIC_KW,

    // Type keywords
    TY_BOOLEAN, TY_BYTE, TY_CHAR, TY_INT16, TY_INT32, TY_INT64,
    TY_FLOAT16, TY_FLOAT32, TY_FLOAT64, TY_STRING, TY_OBJECT,
    TY_TENSOR, TY_ARRAY, TY_ANY,

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

// The keywords, sorted by name so a lookup is a binary search over a
// table in flash; nothing is built at boot. keywords() below serves the
// desktop tools that want a map to walk.
struct KeywordEntry { const char* name; TokenType type; };

inline const KeywordEntry* keyword_table(size_t* count) {
    static const KeywordEntry kw[] = {
        {"AND",             TokenType::AND},
        {"ANDALSO",         TokenType::ANDALSO},
        {"ARRAY",           TokenType::TY_ARRAY},
        {"AS",              TokenType::AS},
        {"ASYNC",           TokenType::ASYNC},
        {"AWAIT",           TokenType::AWAIT_KW},
        {"BAND",            TokenType::BAND},
        {"BNOT",            TokenType::BNOT},
        {"BOOL",            TokenType::TY_BOOLEAN},
        {"BOOLEAN",         TokenType::TY_BOOLEAN},
        {"BOR",             TokenType::BOR},
        {"BXOR",            TokenType::BXOR},
        {"BYTE",            TokenType::TY_BYTE},
        {"CALL",            TokenType::CALL},
        {"CASE",            TokenType::CASE},
        {"CATCH",           TokenType::CATCH},
        {"CHAR",            TokenType::TY_CHAR},
        {"CIRCLE",          TokenType::CIRCLE_KW},
        {"CIRCLE_SECTOR",   TokenType::CIRCLE_SECTOR_KW},
        {"CLS",             TokenType::CLS_KW},
        {"COLOR",           TokenType::COLOR_KW},
        {"CONST",           TokenType::CONST_KW},
        {"CONTINUEDO",      TokenType::CONTINUEDO},
        {"CONTINUEFOR",     TokenType::CONTINUEFOR},
        {"CONTINUELOOP",    TokenType::CONTINUEDO},
        {"CURSOR",          TokenType::CURSOR_KW},
        {"DECLARE",         TokenType::DECLARE_KW},
        {"DEFAULT",         TokenType::DEFAULT},
        {"DIM",             TokenType::DIM},
        {"DO",              TokenType::DO},
        {"DOUBLE",          TokenType::TY_FLOAT64},
        {"DRAWCOLOR",       TokenType::DRAWCOLOR_KW},
        {"DYNAMIC",         TokenType::TY_ANY},
        {"EACH",            TokenType::EACH},
        {"ELLIPSE",         TokenType::ELLIPSE_KW},
        {"ELSE",            TokenType::ELSE},
        {"ELSEIF",          TokenType::ELSEIF},
        {"END",             TokenType::END},
        {"ENDENUM",         TokenType::ENDENUM},
        {"ENDFUNC",         TokenType::ENDFUNC},
        {"ENDIF",           TokenType::ENDIF_KW},
        {"ENDSUB",          TokenType::ENDSUB},
        {"ENDSWITCH",       TokenType::ENDSWITCH},
        {"ENDTRY",          TokenType::ENDTRY},
        {"ENDTYPE",         TokenType::ENDTYPE},
        {"ENUM",            TokenType::ENUM},
        {"EXITDO",          TokenType::EXITDO},
        {"EXITFOR",         TokenType::EXITFOR},
        {"EXITFUNC",        TokenType::EXITFUNC},
        {"EXPORT",          TokenType::EXPORT_KW},
        {"FALSE",           TokenType::FALSE_KW},
        {"FINALLY",         TokenType::FINALLY},
        {"FLOAT16",         TokenType::TY_FLOAT16},
        {"FLOAT32",         TokenType::TY_FLOAT32},
        {"FLOAT64",         TokenType::TY_FLOAT64},
        {"FOR",             TokenType::FOR},
        {"FUNC",            TokenType::FUNCTION},
        {"FUNCTION",        TokenType::FUNCTION},
        {"GOTO",            TokenType::GOTO},
        {"HELP",            TokenType::HELP_KW},
        {"IF",              TokenType::IF},
        {"IMPORT",          TokenType::IMPORT_KW},
        {"IN",              TokenType::IN},
        {"INPUT",           TokenType::INPUT},
        {"INT16",           TokenType::TY_INT16},
        {"INT32",           TokenType::TY_INT32},
        {"INT64",           TokenType::TY_INT64},
        {"INTEGER",         TokenType::TY_INT64},
        {"LAMBDA",          TokenType::LAMBDA},
        {"LET",             TokenType::LET},
        {"LINE",            TokenType::LINE_KW},
        {"LOCATE",          TokenType::LOCATE_KW},
        {"LONG",            TokenType::TY_INT64},
        {"LOOP",            TokenType::LOOP},
        {"MOD",             TokenType::MOD},
        {"MODULE",          TokenType::MODULE_KW},
        {"NEXT",            TokenType::NEXT},
        {"NOT",             TokenType::NOT},
        {"OBJECT",          TokenType::TY_OBJECT},
        {"OF",              TokenType::OF},
        {"ON",              TokenType::ON_KW},
        {"OPTION",          TokenType::OPTION_KW},
        {"OR",              TokenType::OR},
        {"ORELSE",          TokenType::ORELSE},
        {"PLOTRAW",         TokenType::PLOTRAW_KW},
        {"PRINT",           TokenType::PRINT},
        {"PSET",            TokenType::PSET_KW},
        {"RAISEEVENT",      TokenType::RAISEEVENT_KW},
        {"REACT",           TokenType::IDENTIFIER},
        {"RECT",            TokenType::RECT_KW},
        {"RETURN",          TokenType::RETURN},
        {"ROUNDED_RECT",    TokenType::ROUNDED_RECT_KW},
        {"SCREEN",          TokenType::SCREEN_KW},
        {"SCREENFLIP",      TokenType::SCREENFLIP_KW},
        {"SETFONT",         TokenType::SETFONT_KW},
        {"SHL",             TokenType::SHL},
        {"SHORT",           TokenType::TY_INT16},
        {"SHR",             TokenType::SHR},
        {"SINGLE",          TokenType::TY_FLOAT32},
        {"SLEEP",           TokenType::SLEEP_KW},
        {"STATIC",          TokenType::STATIC_KW},
        {"STEP",            TokenType::STEP},
        {"STOP",            TokenType::STOP_KW},
        {"STRING",          TokenType::TY_STRING},
        {"SUB",             TokenType::SUB},
        {"SWITCH",          TokenType::SWITCH},
        {"TENSOR",          TokenType::TY_TENSOR},
        {"TEXT",            TokenType::TEXT_KW},
        {"THEN",            TokenType::THEN},
        {"THIS",            TokenType::THIS_KW},
        {"THROW",           TokenType::THROW_KW},
        {"TO",              TokenType::TO},
        {"TOGGLE_FULLSCREEN", TokenType::TOGGLE_FULLSCREEN_KW},
        {"TRUE",            TokenType::TRUE_KW},
        {"TRY",             TokenType::TRY},
        {"TYPE",            TokenType::TYPE_KW},
        {"UNTIL",           TokenType::UNTIL},
        {"USE",             TokenType::USE},
        {"WHILE",           TokenType::WHILE},
        {"XOR",             TokenType::XOR},
    };
    *count = sizeof kw / sizeof kw[0];
    return kw;
}

inline bool keyword_lookup(const std::string& upper, TokenType& out) {
    size_t n;
    const KeywordEntry* kw = keyword_table(&n);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = upper.compare(kw[mid].name);
        if (c == 0) { out = kw[mid].type; return true; }
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return false;
}

inline const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> map = [] {
        std::unordered_map<std::string, TokenType> m;
        size_t n;
        const KeywordEntry* kw = keyword_table(&n);
        for (size_t i = 0; i < n; i++) m.emplace(kw[i].name, kw[i].type);
        return m;
    }();
    return map;
}
