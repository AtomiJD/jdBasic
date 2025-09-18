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
            // --- Flow Control & Structure ---
            "IF", "THEN", "ELSE", "ELSEIF", "ENDIF",
            "FOR", "TO", "STEP", "NEXT", "EACH", "IN",
            "DO", "LOOP", "WHILE", "UNTIL",
            "SWITCH", "CASE", "DEFAULT", "ENDSWITCH",
            "SUB", "ENDSUB", "FUNC", "ENDFUNC", "RETURN",
            "GOTO", "CALL",
            "EXIT", "EXITFUNC", "EXITDO", "EXITFOR", "EXITSWITCH",

            // --- Error & Async Handling ---
            "TRY", "CATCH", "FINALLY", "ENDTRY", "THROW",
            "AWAIT", "ASYNC", "THREAD",

            // --- Declarations & Types ---
            "DIM", "AS", "TYPE", "ENDTYPE", "ENUM", "ENDENUM", "THIS",
            "INTEGER", "DOUBLE", "STRING", "MAP", "ARRAY", "TENSOR", "JSON", "DATE", "BOOLEAN", "BOOL",
            "REACT",

            // --- Logical & Bitwise Operators ---
            "AND", "OR", "XOR", "NOT",
            "BAND", "BOR", "BXOR",
            "ANDALSO", "ORELSE", "IN_OPERATOR",

            // --- Math Operators & Constants ---
            "MOD", "SHL", "SHR",
            "TRUE", "FALSE", "PI", "VBNEWLINE",

            // --- IDE & System Commands ---
            "PRINT", "INPUT", "CLS", "COLOR", "LOCATE", "CURSOR",
            "REM", "STOP", "RESUME", "SLEEP", "OPTION", "EXPLICIT",
            "LIST", "RUN", "EDIT", "SAVE", "LOAD", "NEW",
            "COMPILE", "DUMP", "TRON", "TROFF",
            "SAVEWS", "LOADWS", "CLEARWS", "UNREACT",
            "LINT", "PRETTY",

            // --- Module & Dynamic Code ---
            "IMPORT", "EXPORT", "MODULE", "DLLIMPORT",
            "EXECUTE", "EVAL", "LAMBDA",

            // --- Type & System Functions ---
            "TYPEOF", "HELP", "HELP$", "SETLOCALE",
            "GETENV$", "TICK", "DATE$", "TIME$", "NOW", "DATEADD", "DATEDIFF", "CVDATE",

            // --- String Functions ---
            "LEFT$", "RIGHT$", "MID$", "LEN", "LCASE$", "UCASE$", "TRIM$",
            "STR$", "VAL", "CHR$", "ASC", "INSTR$", "SPLIT", "FRMV$", "FORMAT$",
            "REPLACE$", "REVERSE$", "INKEY$", "WAITKEY$",

            // --- Math Functions ---
            "SIN", "COS", "TAN", "SQR", "RND", "LOG", "LOG10", "FAC",
            "INT", "FLOOR", "CEIL", "ROUND", "TRUNC", "ABS", "CDBL", "CLAMP",

            // --- Array & Matrix Functions ---
            "APPEND", "DIFF", "IOTA", "SUM", "PRODUCT", "MIN", "MAX", "ANY", "ALL", "SCAN",
            "SELECT", "FILTER", "REDUCE", "TAKE", "DROP", "RESHAPE", "TRANSPOSE", "MATMUL",
            "MVLET", "INTEGRATE", "SOLVE", "INVERT", "NORMALIZE", "UNIQUE", "SHUFFLE",
            "FIND_IN_ARRAY", "DISTANCE", "STACK", "SLICE", "LERP", "GRADE", "OUTER",
            "ROTATE", "SHIFT", "XSORT", "CONVOLVE", "PLACE",

            // --- Filesystem Commands & Functions ---
            "DIR", "DIR$", "CD", "PWD", "MKDIR", "KILL",
            "TXTREADER$", "CSVREADER", "TXTWRITER", "CSVWRITER",

            // --- Map Functions ---
            "MAP.EXISTS", "MAP.KEYS", "MAP.VALUES", "MAP.DELETE", "MAP.CLEAR", "MAP.SIZE", "MAP.MERGE", "MAP.ITEMS",

            // --- JSON & COM Functions ---
            "JSON.PARSE$", "JSON.STRINGIFY$", "CREATEOBJECT",

            // --- OS Functions ---
            "OS.ARGS", "OS.EXEC", "OS.GETOS",

            // --- HTTP Functions ---
            "HTTP.GET$", "HTTP.POST$", "HTTP.PUT$", "HTTP.STATUSCODE", "HTTP.SETHEADER", "HTTP.CLEARHEADERS", "HTTP.POST_ASYNC",
            "HTTP.SERVER.START", "HTTP.SERVER.STOP", "HTTP.SERVER.ON_GET", "HTTP.SERVER.ON_POST",

            // --- Regex Functions ---
            "REGEX.MATCH", "REGEX.FINDALL", "REGEX.REPLACE",

            // --- Thread Functions ---
            "THREAD.ISDONE", "THREAD.GETRESULT",

            // --- AI & Tensor Functions ---
            "TENSOR.FROM", "TENSOR.TOARRAY", "TENSOR.BACKWARD", "TENSOR.SIGMOID", "TENSOR.RELU",
            "TENSOR.SOFTMAX", "TENSOR.CROSS_ENTROPY_LOSS", "TENSOR.TOKENIZE", "TENSOR.POSITIONAL_ENCODING",
            "TENSOR.LAYERNORM", "TENSOR.CONV2D", "TENSOR.MAXPOOL2D", "TENSOR.CREATE_LAYER",
            "TENSOR.CREATE_OPTIMIZER", "TENSOR.SAVEMODEL", "TENSOR.LOADMODEL", "TENSOR.UPDATE", "TENSOR.MATMUL",

            // --- Graphics, Sound & Input ---
            "SCREEN", "SCREENFLIP", "DRAWCOLOR", "SETFONT", "PSET", "LINE", "RECT", "CIRCLE", "ELLIPSE",
            "ROUNDED_RECT", "CIRCLE_SECTOR", "TEXT", "PLOTRAW", "TOGGLE_FULLSCREEN",
            "SCREENWIDTH", "SCREENHEIGHT", "MOUSEX", "MOUSEY", "MOUSEB",
            "SOUND.INIT", "SOUND.VOICE", "SOUND.PLAY", "SOUND.RELEASE", "SOUND.STOP",
            "SFX.LOAD", "SFX.PLAY", "MUSIC.PLAY", "MUSIC.STOP",

            // --- Turtle Graphics ---
            "TURTLE.FORWARD", "TURTLE.BACKWARD", "TURTLE.LEFT", "TURTLE.RIGHT", "TURTLE.PENUP",
            "TURTLE.PENDOWN", "TURTLE.SETPOS", "TURTLE.SETHEADING", "TURTLE.HOME",
            "TURTLE.SET_COLOR", "TURTLE.DRAW", "TURTLE.CLEAR",

            // --- Game Engine (Sprites & Tilemaps) ---
            "SPRITE.LOAD", "SPRITE.LOAD_ASEPRITE", "SPRITE.CREATE", "SPRITE.MOVE", "SPRITE.DELETE",
            "SPRITE.SET_VELOCITY", "SPRITE.UPDATE", "SPRITE.DRAW_ALL", "SPRITE.SET_ANIMATION",
            "SPRITE.SET_FLIP", "SPRITE.GET_X", "SPRITE.GET_Y", "SPRITE.COLLISION",
            "SPRITE.CREATE_GROUP", "SPRITE.ADD_TO_GROUP", "SPRITE.COLLISION_GROUP", "SPRITE.COLLISION_GROUPS",
            "TILEMAP.LOAD", "TILEMAP.DRAW_LAYER", "TILEMAP.GET_OBJECTS", "TILEMAP.COLLIDES",
            "TILEMAP.GET_TILE_ID", "TILEMAP.DRAW_DEBUG_COLLISIONS"
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
