#pragma once
// Return kind of each builtin for the native compiler. Every type inference
// and bridge dispatch site in llvm_codegen.cpp reads this table.
// tools/check_builtin_sigs.jdb checks the names against help.txt.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

enum class BuiltinRet : uint8_t {
    Unknown,  // not listed
    F64,
    I64,
    Bool,
    Str,      // listed only for names without a trailing $
    Arr,      // flat JdbArray
    Handle,   // VM Value in the value store: maps, tensors, mixed kinds
};

enum BuiltinFlags : uint8_t {
    BF_NONE = 0,
    BF_DATE = 1,  // answers a date
};

struct BuiltinSig {
    const char* name;
    BuiltinRet ret;
    uint8_t flags;
};

inline constexpr BuiltinSig kBuiltinSigs[] = {
    // Arrays
    {"AI.EMBED", BuiltinRet::Arr, BF_NONE},
    {"AI.EMBED_LLM", BuiltinRet::Arr, BF_NONE},
    {"AI.LIST", BuiltinRet::Arr, BF_NONE},
    {"AI.TOKENIZE", BuiltinRet::Arr, BF_NONE},
    {"AI.TOPK", BuiltinRet::Arr, BF_NONE},
    {"APPEND", BuiltinRet::Arr, BF_NONE},
    {"CHUNK", BuiltinRet::Arr, BF_NONE},
    {"CONVOLVE", BuiltinRet::Arr, BF_NONE},
    {"CSVHEADER", BuiltinRet::Arr, BF_NONE},
    {"CSVREADER", BuiltinRet::Arr, BF_NONE},
    {"CUMPROD", BuiltinRet::Arr, BF_NONE},
    {"CUMSUM", BuiltinRet::Arr, BF_NONE},
    {"DATERANGE", BuiltinRet::Arr, BF_NONE},
    {"DIFF", BuiltinRet::Arr, BF_NONE},
    {"DIR$", BuiltinRet::Arr, BF_NONE},
    {"DROP", BuiltinRet::Arr, BF_NONE},
    {"DROP_WHILE", BuiltinRet::Arr, BF_NONE},
    {"ENUMERATE", BuiltinRet::Arr, BF_NONE},
    {"FFT", BuiltinRet::Arr, BF_NONE},
    {"FLATTEN", BuiltinRet::Arr, BF_NONE},
    {"GFX.HSV_RGB", BuiltinRet::Arr, BF_NONE},
    {"GFX.TEXTSIZE", BuiltinRet::Arr, BF_NONE},
    {"GRADE", BuiltinRet::Arr, BF_NONE},
    {"GUI.ITEM_RECT", BuiltinRet::Arr, BF_NONE},
    {"HISTEDGES", BuiltinRet::Arr, BF_NONE},
    {"HISTOGRAM", BuiltinRet::Arr, BF_NONE},
    {"IFFT", BuiltinRet::Arr, BF_NONE},
    {"INTEGRATE", BuiltinRet::Arr, BF_NONE},
    {"INVERT", BuiltinRet::Arr, BF_NONE},
    {"IOTA", BuiltinRet::Arr, BF_NONE},
    {"LINSPACE", BuiltinRet::Arr, BF_NONE},
    {"MAP.ITEMS", BuiltinRet::Arr, BF_NONE},
    {"MAP.KEYS", BuiltinRet::Arr, BF_NONE},
    {"MAP.VALUES", BuiltinRet::Arr, BF_NONE},
    {"MATMUL", BuiltinRet::Arr, BF_NONE},
    {"MON.SCOPE", BuiltinRet::Arr, BF_NONE},
    {"MVINS", BuiltinRet::Arr, BF_NONE},
    {"MVLET", BuiltinRet::Arr, BF_NONE},
    {"NORMALIZE", BuiltinRet::Arr, BF_NONE},
    {"ONES", BuiltinRet::Arr, BF_NONE},
    {"OS.ARGS", BuiltinRet::Arr, BF_NONE},
    {"OUTER", BuiltinRet::Arr, BF_NONE},
    {"PLACE", BuiltinRet::Arr, BF_NONE},
    {"RANGE", BuiltinRet::Arr, BF_NONE},
    {"REGEX.FINDALL", BuiltinRet::Arr, BF_NONE},
    {"REGEX_MATCH", BuiltinRet::Arr, BF_NONE},
    {"RESHAPE", BuiltinRet::Arr, BF_NONE},
    {"REVERSE", BuiltinRet::Arr, BF_NONE},
    {"RNG.FILL", BuiltinRet::Arr, BF_NONE},
    {"ROTATE", BuiltinRet::Arr, BF_NONE},
    {"SCAN", BuiltinRet::Arr, BF_NONE},
    {"SHIFT", BuiltinRet::Arr, BF_NONE},
    {"SHUFFLE", BuiltinRet::Arr, BF_NONE},
    {"SLICE", BuiltinRet::Arr, BF_NONE},
    {"SOLVE", BuiltinRet::Arr, BF_NONE},
    {"SOUND.GET_BUS_WAVE", BuiltinRet::Arr, BF_NONE},
    {"SOUND.GET_WAVE", BuiltinRet::Arr, BF_NONE},
    {"SOUND.RENDER", BuiltinRet::Arr, BF_NONE},
    {"SPLIT", BuiltinRet::Arr, BF_NONE},
    {"SPRITE.COLLISIONS", BuiltinRet::Arr, BF_NONE},
    {"SQL.COLUMNS", BuiltinRet::Arr, BF_NONE},
    {"SQL.TABLE", BuiltinRet::Arr, BF_NONE},
    {"STACK", BuiltinRet::Arr, BF_NONE},
    {"TAKE", BuiltinRet::Arr, BF_NONE},
    {"TAKE_WHILE", BuiltinRet::Arr, BF_NONE},
    {"TALLY", BuiltinRet::Arr, BF_NONE},
    {"TILED.LAYERS$", BuiltinRet::Arr, BF_NONE},
    {"TILED.SIZE", BuiltinRet::Arr, BF_NONE},
    {"TILED.TILE_SIZE", BuiltinRet::Arr, BF_NONE},
    {"TILEMAP.SIZE", BuiltinRet::Arr, BF_NONE},
    {"TRANSPOSE", BuiltinRet::Arr, BF_NONE},
    {"UNIQUE", BuiltinRet::Arr, BF_NONE},
    {"UNPACK", BuiltinRet::Arr, BF_NONE},
    {"XSORT", BuiltinRet::Arr, BF_NONE},
    {"ZEROS", BuiltinRet::Arr, BF_NONE},
    {"ZIP", BuiltinRet::Arr, BF_NONE},
    {"ZIP.LIST", BuiltinRet::Arr, BF_NONE},

    // VM objects
    {"AI.GET_HISTORY", BuiltinRet::Handle, BF_NONE},
    {"AI.RAG_QUERY_FULL", BuiltinRet::Handle, BF_NONE},
    {"AI.RAG_SEARCH", BuiltinRet::Handle, BF_NONE},
    {"AI.TOOL_LIST", BuiltinRet::Handle, BF_NONE},
    {"AWAIT", BuiltinRet::Handle, BF_NONE},
    {"CHAN.RECV", BuiltinRet::Handle, BF_NONE},
    {"CHAN.TRY_RECV", BuiltinRet::Handle, BF_NONE},
    {"DATE.PARTS", BuiltinRet::Handle, BF_NONE},
    {"EIG", BuiltinRet::Handle, BF_NONE},
    {"FILE.STAT", BuiltinRet::Handle, BF_NONE},
    {"FORM.GET", BuiltinRet::Handle, BF_NONE},
    {"GROUPBY", BuiltinRet::Handle, BF_NONE},
    {"HTTP.REQUEST", BuiltinRet::Handle, BF_NONE},
    {"JSON.PARSE$", BuiltinRet::Handle, BF_NONE},
    {"MAP.FROM", BuiltinRet::Handle, BF_NONE},
    {"MAT4.IDENTITY", BuiltinRet::Handle, BF_NONE},
    {"MAT4.LOOKAT", BuiltinRet::Handle, BF_NONE},
    {"MAT4.MUL", BuiltinRet::Handle, BF_NONE},
    {"MAT4.PERSPECTIVE", BuiltinRet::Handle, BF_NONE},
    {"MAT4.ROTATE", BuiltinRet::Handle, BF_NONE},
    {"MAT4.SCALE", BuiltinRet::Handle, BF_NONE},
    {"MAT4.TRANSLATE", BuiltinRet::Handle, BF_NONE},
    {"MON.DEVICES", BuiltinRet::Handle, BF_NONE},
    {"NET.RECVFROM", BuiltinRet::Handle, BF_NONE},
    {"OS.EXEC", BuiltinRet::Handle, BF_NONE},
    {"PY.EVAL", BuiltinRet::Handle, BF_NONE},
    {"PY.GET", BuiltinRet::Handle, BF_NONE},
    {"QR", BuiltinRet::Handle, BF_NONE},
    {"REGEX.MATCH", BuiltinRet::Handle, BF_NONE},
    {"SVD", BuiltinRet::Handle, BF_NONE},
    {"THREAD.GETRESULT", BuiltinRet::Handle, BF_NONE},
    {"TILED.OBJECTS", BuiltinRet::Handle, BF_NONE},
    {"TILED.PROPERTIES", BuiltinRet::Handle, BF_NONE},
    {"WAV.INFO", BuiltinRet::Handle, BF_NONE},
    {"WAV.READ", BuiltinRet::Handle, BF_NONE},
    {"WAV.RECORD", BuiltinRet::Handle, BF_NONE},
    {"WAV.RECSTOP", BuiltinRet::Handle, BF_NONE},
    {"ZIP.READ", BuiltinRet::Handle, BF_NONE},

    // Strings
    {"CLIPBOARD.GET$", BuiltinRet::Str, BF_NONE},
    {"GFX.POLLEVENT", BuiltinRet::Str, BF_NONE},
    {"GUI.INPUT", BuiltinRet::Str, BF_NONE},
    {"INKEY$", BuiltinRet::Str, BF_NONE},
    {"JOIN", BuiltinRet::Str, BF_NONE},
    {"SOUND.STATS", BuiltinRet::Str, BF_NONE},
    {"TUI.INPUT", BuiltinRet::Str, BF_NONE},

    // Dates, as ISO strings on the native side
    {"CDATE", BuiltinRet::Str, BF_DATE},
    {"CVDATE", BuiltinRet::Str, BF_DATE},
    {"DATE.UTC", BuiltinRet::Str, BF_DATE},
    {"DATEADD", BuiltinRet::Str, BF_DATE},
    {"EOMONTH", BuiltinRet::Str, BF_DATE},
    {"FORMAT_DATE", BuiltinRet::Str, BF_NONE},
    {"NOW", BuiltinRet::Str, BF_DATE},

    // Truth values
    {"CHAN.IS_CLOSED", BuiltinRet::Bool, BF_NONE},
    {"CHAN.IS_EOF", BuiltinRet::Bool, BF_NONE},
    {"CHAN.IS_TIMEOUT", BuiltinRet::Bool, BF_NONE},
    {"ENDSWITH", BuiltinRet::Bool, BF_NONE},
    {"FILE.AT_EOF", BuiltinRet::Bool, BF_NONE},
    {"FILE.EXISTS", BuiltinRet::Bool, BF_NONE},
    {"FORM.DOEVENTS", BuiltinRet::Bool, BF_NONE},
    {"FX.ADD", BuiltinRet::Bool, BF_NONE},
    {"FX.MIX", BuiltinRet::Bool, BF_NONE},
    {"FX.SET", BuiltinRet::Bool, BF_NONE},
    {"FX.SPLIT", BuiltinRet::Bool, BF_NONE},
    {"GFX.KEYSTATE", BuiltinRet::Bool, BF_NONE},
    {"GFX.MOUSEBUTTON", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_MAIN_MENU_BAR", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_MENU", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_MENU_BAR", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_POPUP", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_POPUP_MODAL", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_TABLE", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_TAB_BAR", BuiltinRet::Bool, BF_NONE},
    {"GUI.BEGIN_TAB_ITEM", BuiltinRet::Bool, BF_NONE},
    {"GUI.BUTTON", BuiltinRet::Bool, BF_NONE},
    {"GUI.CHECKBOX", BuiltinRet::Bool, BF_NONE},
    {"GUI.COLLAPSING_HEADER", BuiltinRet::Bool, BF_NONE},
    {"GUI.COLOR", BuiltinRet::Bool, BF_NONE},
    {"GUI.IMAGE", BuiltinRet::Bool, BF_NONE},
    {"GUI.ITEM_DEACTIVATED_AFTER_EDIT", BuiltinRet::Bool, BF_NONE},
    {"GUI.MENU_ITEM", BuiltinRet::Bool, BF_NONE},
    {"GUI.SELECTABLE", BuiltinRet::Bool, BF_NONE},
    {"GUI.TABLE_NEXT_COLUMN", BuiltinRet::Bool, BF_NONE},
    {"GUI.TABLE_SET_COLUMN_INDEX", BuiltinRet::Bool, BF_NONE},
    {"GUI.TREE_NODE", BuiltinRet::Bool, BF_NONE},
    {"JOY.BUTTON", BuiltinRet::Bool, BF_NONE},
    {"MAP.EXISTS", BuiltinRet::Bool, BF_NONE},
    {"MIDI.SEND", BuiltinRet::Bool, BF_NONE},
    {"MON.RECSTART", BuiltinRet::Bool, BF_NONE},
    {"MON.RUNNING", BuiltinRet::Bool, BF_NONE},
    {"MON.START", BuiltinRet::Bool, BF_NONE},
    {"MOUSEB", BuiltinRet::Bool, BF_NONE},
    {"OS.FEATURE", BuiltinRet::Bool, BF_NONE},
    {"PY.SET", BuiltinRet::Bool, BF_NONE},
    {"SPRITE.COLLISION", BuiltinRet::Bool, BF_NONE},
    {"SPRITE.ON_GROUND", BuiltinRet::Bool, BF_NONE},
    {"SPRITE.PLAYING", BuiltinRet::Bool, BF_NONE},
    {"SQL.CLOSE", BuiltinRet::Bool, BF_NONE},
    {"STARTSWITH", BuiltinRet::Bool, BF_NONE},
    {"THREAD.ISDONE", BuiltinRet::Bool, BF_NONE},
    {"TILED.COLLIDES", BuiltinRet::Bool, BF_NONE},
    {"TILED.LOAD", BuiltinRet::Bool, BF_NONE},
    {"TILEMAP.COLLIDES", BuiltinRet::Bool, BF_NONE},
    {"TUI.BUTTON", BuiltinRet::Bool, BF_NONE},
    {"TUI.MENUITEM", BuiltinRet::Bool, BF_NONE},
    {"TUI.MODAL_BEGIN", BuiltinRet::Bool, BF_NONE},
    {"TUI.QUIT", BuiltinRet::Bool, BF_NONE},
    {"TUI.SELECTABLE", BuiltinRet::Bool, BF_NONE},
    {"TUI.SUBMENU_BEGIN", BuiltinRet::Bool, BF_NONE},
    {"TUI.TAB_BEGIN", BuiltinRet::Bool, BF_NONE},
    {"WAV.RECSTART", BuiltinRet::Bool, BF_NONE},
    {"WAV.WRITE", BuiltinRet::Bool, BF_NONE},

    // Numbers
    {"ALL", BuiltinRet::I64, BF_NONE},
    {"ANY", BuiltinRet::I64, BF_NONE},
    {"COUNT", BuiltinRet::I64, BF_NONE},
    {"CROSS", BuiltinRet::F64, BF_NONE},
    {"DOT", BuiltinRet::F64, BF_NONE},
    {"INDEXOF", BuiltinRet::I64, BF_NONE},
    {"LEN", BuiltinRet::I64, BF_NONE},
    {"MAX", BuiltinRet::F64, BF_NONE},
    {"MEAN", BuiltinRet::F64, BF_NONE},
    {"MEDIAN", BuiltinRet::F64, BF_NONE},
    {"MIN", BuiltinRet::F64, BF_NONE},
    {"PRODUCT", BuiltinRet::F64, BF_NONE},
    {"REDUCE", BuiltinRet::F64, BF_NONE},
    {"STDEV", BuiltinRet::F64, BF_NONE},
    {"SUM", BuiltinRet::F64, BF_NONE},
    {"TUI.CHECKBOX", BuiltinRet::I64, BF_NONE},
    {"TUI.DROPDOWN", BuiltinRet::I64, BF_NONE},
    {"TUI.MENU", BuiltinRet::I64, BF_NONE},
    {"TUI.RADIO", BuiltinRet::I64, BF_NONE},
    {"TUI.SLIDER", BuiltinRet::F64, BF_NONE},
    {"TUI.SPINNER", BuiltinRet::F64, BF_NONE},
    {"VARIANCE", BuiltinRet::F64, BF_NONE},
};

// Case-insensitive; nullptr for a name the table does not list.
inline const BuiltinSig* builtin_sig(std::string_view name) {
    static const std::unordered_map<std::string, const BuiltinSig*> index = [] {
        std::unordered_map<std::string, const BuiltinSig*> m;
        for (const auto& s : kBuiltinSigs) m.emplace(s.name, &s);
        return m;
    }();
    std::string upper(name);
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    auto it = index.find(upper);
    return it == index.end() ? nullptr : it->second;
}

inline BuiltinRet builtin_ret(std::string_view name) {
    const BuiltinSig* s = builtin_sig(name);
    return s ? s->ret : BuiltinRet::Unknown;
}

inline bool builtin_is(std::string_view name, BuiltinRet ret) {
    return builtin_ret(name) == ret;
}

inline bool builtin_returns_date(std::string_view name) {
    const BuiltinSig* s = builtin_sig(name);
    return s && (s->flags & BF_DATE);
}
