#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm> // for std::transform
#include <cctype>    // for ::toupper

// This class acts as a centralized repository for all jdBasic keywords,
// commands, and built-in function names.
class KeywordRepository {
public:
    // Returns a reference to the master set of all reserved words in UPPERCASE.
    static const std::unordered_set<std::string>& get_all_keywords() {
        static const std::unordered_set<std::string> all_keywords = {
            // Keywords
            "PRINT", "INPUT", "DIM", "AS", "IF", "THEN", "ELSE", "ELSEIF", "ENDIF",
            "FOR", "TO", "STEP", "NEXT", "EACH", "IN", "DO", "LOOP", "UNTIL", "WHILE",
            "SWITCH", "CASE", "DEFAULT", "ENDSWITCH", "TRY", "CATCH", "FINALLY", "ENDTRY",
            "SUB", "ENDSUB", "FUNC", "ENDFUNC", "RETURN", "EXIT", "EXITFUNC", "EXITDO", "EXITFOR",
            "TYPE", "ENDTYPE", "ENUM", "ENDENUM", "OPTION", "STRICT", "EXPLICIT",
            "AND", "OR", "XOR", "NOT", "BAND", "BOR", "BXOR", "SHL", "SHR", "MOD",
            "TRUE", "FALSE", "GOTO", "CALL", "REM", "IMPORT", "EXPORT", "MODULE", "DLLIMPORT",

            // Type Names
            "INTEGER", "DOUBLE", "STRING", "MAP", "ARRAY", "TENSOR", "JSON", "DATE", "BOOLEAN", "BOOL",

            // Built-in Functions & Procedures
            "SIN", "COS", "TAN", "SQR", "RND", "LOG", "LOG10", "FAC", "INT", "FLOOR", "CEIL", "ROUND", "TRUNC", "ABS",
            "LEFT$", "RIGHT$", "MID$", "LEN", "LCASE$", "UCASE$", "TRIM$", "STR$", "VAL", "CHR$", "ASC", "INSTR$", "SPLIT", "FRMV$", "FORMAT$", "REPLACE$", "REVERSE$",
            "APPEND", "DIFF", "IOTA", "SUM", "PRODUCT", "MIN", "MAX", "ANY", "ALL", "SCAN", "SELECT", "FILTER", "REDUCE",
            "TAKE", "DROP", "RESHAPE", "REVERSE", "TRANSPOSE", "MATMUL", "MVLET", "INTEGRATE", "SOLVE", "INVERT",
            "NORMALIZE", "UNIQUE", "SHUFFLE", "FIND_IN_ARRAY", "DISTANCE", "STACK", "SLICE", "LERP", "GRADE", "OUTER",
            "ROTATE", "SHIFT", "XSORT", "CONVOLVE", "PLACE",
            "TXTREADER$", "CSVREADER", "TXTWRITER", "CSVWRITER",
            "GETENV$", "TICK", "DATE$", "TIME$", "NOW", "DATEADD", "DATEDIFF", "CVDATE",
            "TYPEOF", "HELP", "HELP$", "SETLOCALE", "CLS", "LOCATE", "GETX", "GETY", "SLEEP", "CURSOR", "THROW",
            "SAVEWS", "LOADWS", "CLEARWS", "NEW", "UNREACT",
            "DIR", "DIR$", "CD", "PWD", "COLOR", "MKDIR", "KILL",
            "OS.ARGS", "OS.EXEC", "OS.GETOS",
            "EXECUTE", "EVAL",
            "JSON.PARSE$", "JSON.STRINGIFY$", "CREATEOBJECT",
            "MAP.EXISTS", "MAP.KEYS", "MAP.VALUES", "MAP.DELETE", "MAP.CLEAR", "MAP.SIZE", "MAP.MERGE", "MAP.ITEMS",
            "HTTP.GET$", "HTTP.POST$", "HTTP.PUT$", "HTTP.STATUSCODE", "HTTP.SETHEADER", "HTTP.CLEARHEADERS",
            "HTTP.SERVER.START", "HTTP.SERVER.STOP", "HTTP.SERVER.ON_GET", "HTTP.SERVER.ON_POST",
            "TENSOR.FROM", "TENSOR.TOARRAY", "TENSOR.BACKWARD", "TENSOR.SIGMOID", "TENSOR.RELU", "TENSOR.SOFTMAX",
            "TENSOR.CROSS_ENTROPY_LOSS", "TENSOR.TOKENIZE", "TENSOR.POSITIONAL_ENCODING", "TENSOR.LAYERNORM",
            "TENSOR.CONV2D", "TENSOR.MAXPOOL2D",
            "THREAD.ISDONE", "THREAD.GETRESULT",
            "REGEX.MATCH", "REGEX.FINDALL", "REGEX.REPLACE", "AWAIT",
            "INKEY$", "WAITKEY$",
            "PI", "VBNEWLINE", "LINT", "PRETTY"
        };
        return all_keywords;
    }

    // A helper function to check if a word is a keyword, case-insensitively.
    static bool is_keyword(const std::string& word) {
        std::string upper_word = word;
        std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return get_all_keywords().count(upper_word);
    }

    // Function for the WASM/Monaco editor to get all keywords as a single string.
    static std::string get_keywords_for_monaco() {
        std::string result = "";
        for (const auto& kw : get_all_keywords()) {
            result += kw + " ";
        }
        if (!result.empty()) {
            result.pop_back(); // Remove trailing space
        }
        return result;
    }
};
