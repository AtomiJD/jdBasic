// --- Core Interpreter Headers ---
#include "BuiltinFunctions.hpp" // Always include the corresponding header first
#include "NeReLaBasic.hpp"      // For the main VM class and core types
#include "Commands.hpp"         // For utility functions like to_string, to_double
#include "Error.hpp"            // For Error::set()
#include "Types.hpp"            // For BasicValue, Map, Array, etc.
#include "LocaleManager.hpp"    // For locale-aware string formatting
#include "TextIO.hpp"           // For TextIO
#include "Compiler.hpp"           // For Compiler thingslocale-aware string formatting

// --- Headers for Modules to be Registered ---
// These are needed to call the registration functions from other modules.
#include "AIFunctions.hpp"
#include "ArrayFunctions.hpp"
#include "SDLFunctions.hpp"
#include "BetterCodeFunctions.hpp"

// --- Standard C++ Library Headers ---
// These are required for the functions STILL IMPLEMENTED in this file
// (e.g., String, Filesystem, Date/Time, JSON, COM functions).

// For file I/O (TXTREADER, CSVWRITER, SAVEWS, etc.)
#include <fstream>
#include <filesystem>
#include <iostream> // Often included with fstream, good to keep

// For string manipulation and formatting
#include <string>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>

// For date/time (SLEEP, TICK, NOW)
#include <chrono>
#include <thread>

// For advanced data structures and utilities
#include <vector>
#include <unordered_set>
#include <functional>
#include <future>

// For OS-specific console I/O (like INKEY$) and environment variables
#include "AppConfig.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#include <format>
#include <winsock2.h> // For networking
#include <ws2tcpip.h> // For networking
#include <iphlpapi.h> // For enumerating network adapters
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#elif defined(__EMSCRIPTEN__)
#include <codecvt> // for std::wstring_convert
#include <locale>  // for std::locale
#include <stdio.h>
#include <emscripten.h>
#include <format>
#include <deque>
#else
#include <codecvt> // for std::wstring_convert
#include <locale>  // for std::locale
#include <stdio.h>
#include <unistd.h>      // For gethostname
#include <sys/socket.h>  // For networking
#include <arpa/inet.h>   // For networking
#include <netdb.h>       // For networking
#include <ifaddrs.h> // For enumerating network adapters
#include <fmt/core.h>
#include <fmt/format.h>
#ifdef USE_SERIAL
#include <sys/ioctl.h>
#include <errno.h>
#endif
#endif

#ifdef HTTP
#include "NetworkManager.hpp"
#endif

#include "json.hpp"

#ifdef JDCOM
#include <objbase.h> // CoInitializeEx, CoUninitialize, CoCreateInstance, CLSIDFromProgID
#include <oaidl.h>   // <-- Contains definitions for IDispatch, VARIANT, SAFEARRAY, etc.
#include <comdef.h>  // _com_ptr_t, _variant_t, _bstr_t, _com_error

// Helper to convert BasicValue to _variant_t (for passing arguments to COM methods)
_variant_t basic_value_to_variant_t(const BasicValue& val) {
    return std::visit([](auto&& arg) -> _variant_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return _variant_t(arg);
        }
        else if constexpr (std::is_same_v<T, double>) {
            return _variant_t(arg);
        }
        else if constexpr (std::is_same_v<T, long long>) { // If you still use int
            _variant_t v; v.vt = VT_I8; v.llVal = arg; return v;
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            // Convert std::string to BSTR (Basic string)
            std::wstring ws = string_to_wstring(arg);
            return _variant_t(ws.c_str()); // BSTR is allocated internally by _variant_t
        }
        else if constexpr (std::is_same_v<T, ComObject>) {
            // AddRef the IDispatch pointer and return it as a VARIANT of type VT_DISPATCH
            if (arg.ptr) {
                arg.ptr->AddRef(); // _variant_t takes ownership, so we need to AddRef
                return _variant_t(static_cast<IDispatch*>(arg.ptr), true); // true = AddRef
            }
            return _variant_t(); // Empty variant for null ComObject
        }
        else {
            // Handle other types or return an error/empty variant
            return _variant_t();
        }
        }, val);
}

// Helper to convert _variant_t back to BasicValue
BasicValue variant_t_to_basic_value(const _variant_t& vt, NeReLaBasic& vm) {
    switch (vt.vt) {
        // --- Null / Empty ---
    case VT_EMPTY:
    case VT_NULL:
        // Both represent a form of "no value". In a database context,
        // VT_NULL is common. Mapping to 0.0 is a simple, pragmatic choice.
        // A more advanced option would be to add a dedicated 'null' type
        // to your BasicValue variant.
        return 0.0;

        // --- Numeric Types (all converted to double) ---
    case VT_I1:       return (double)vt.cVal;
    case VT_UI1:      return (double)vt.bVal;
    case VT_I2:       return (double)vt.iVal;
    case VT_UI2:      return (double)vt.uiVal;
    case VT_I4:       return (double)vt.lVal;
    case VT_UI4:      return (double)vt.ulVal;
    case VT_I8:       return static_cast<long long>(vt.llVal);
    case VT_UI8: {
            // If it fits in signed 64-bit, preserve as INTEGER; otherwise fall back to double
            unsigned long long u = vt.ullVal;
            if (u <= static_cast<unsigned long long>(LLONG_MAX)) return static_cast<long long>(u);
            return static_cast<double>(u);
    }
    case VT_INT:      return (double)vt.intVal;
    case VT_UINT:     return (double)vt.uintVal;
    case VT_R4:       return (double)vt.fltVal;
    case VT_R8:       return (double)vt.dblVal;

    case VT_CY: { // Currency
        double dbl_val;
        // Use the OLE Automation function to convert Currency to Double
        if (SUCCEEDED(VarR8FromCy(vt.cyVal, &dbl_val))) {
            return dbl_val;
        }
        // Fall through to error on failure
        [[fallthrough]];
    }

              // --- Boolean ---
    case VT_BOOL:
        // vt.boolVal is a VARIANT_BOOL, which is -1 for true and 0 for false.
        return (vt.boolVal != 0);

        // --- Date/Time ---
    case VT_DATE: {
        SYSTEMTIME st;
        if (VariantTimeToSystemTime(vt.date, &st) == 1) {
            FILETIME ft;
            if (SystemTimeToFileTime(&st, &ft)) {
                ULARGE_INTEGER uli;
                uli.LowPart = ft.dwLowDateTime;
                uli.HighPart = ft.dwHighDateTime;
                constexpr long long WIN_EPOCH_AS_UNIX = 116444736000000000LL;
                std::chrono::duration<long long, std::ratio<1, 10000000>> duration(uli.QuadPart - WIN_EPOCH_AS_UNIX);
                std::chrono::system_clock::time_point tp(duration);
                return DateTime{ tp };
            }
        }
        // Fall through to error on failure
        [[fallthrough]];
    }

                // --- String ---
    case VT_BSTR: {
        if (vt.bstrVal) {
            // The _bstr_t wrapper handles the conversion from BSTR to char*
            return std::string(_bstr_t(vt.bstrVal));
        }
        return std::string("");
    }

                // --- COM Object ---
    case VT_DISPATCH: {
        // The ComObject wrapper handles the IDispatch pointer
        return ComObject(vt.pdispVal);
    }

                    // --- Unhandled / Advanced Types ---
    case VT_ERROR:
    case VT_VARIANT: // Pointer to another VARIANT, requires recursion
    case VT_UNKNOWN: // IUnknown pointer, could try to QueryInterface for IDispatch
    case VT_DECIMAL: // High-precision decimal, requires VarR8FromDec
    case VT_RECORD:  // User-defined type, very complex
    default:
        // For any type you don't explicitly handle, it's safest
        // to raise a "Type Mismatch" or "Unsupported Type" error.
        Error::set(15, vm.runtime_current_line, "Unsupported COM variant type: " + std::to_string(vt.vt));
        return 0.0; // Default error value
    }
}

// CREATEOBJECT(ProgID$) -> ComObject
BasicValue builtin_create_object(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return ComObject(); // Return a null ComObject
    }

    std::string progID_str = to_string(args[0]);
    CLSID clsid;
    HRESULT hr;

    // Convert ProgID (e.g., "Excel.Application") to CLSID
    _bstr_t bstrProgID(progID_str.c_str());
    hr = CLSIDFromProgID(bstrProgID, &clsid);

    if (FAILED(hr)) {
        Error::set(1, vm.runtime_current_line); // Syntax error: Invalid ProgID
        // Optionally, print HRESULT for debugging: TextIO::print("CLSIDFromProgID failed: " + std::to_string(hr) + "\n");
        return ComObject();
    }

    IDispatch* pDisp = nullptr;
    // Create the COM object
    hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER, IID_IDispatch, (void**)&pDisp);

    if (FAILED(hr)) {
        Error::set(12, vm.runtime_current_line); // File I/O Error or general COM error
        // Optionally, print HRESULT: TextIO::print("CoCreateInstance failed: " + std::to_string(hr) + "\n");
        return ComObject();
    }

    // Wrap the IDispatch pointer in our ComObject struct (which uses _com_ptr_t)
    return ComObject(pDisp); // _com_ptr_t takes ownership, no need for pDisp->Release() here
}

// Helper function to call IDispatch::Invoke
HRESULT invoke_com_method(
    IDispatchPtr pDisp,
    const std::string& memberName,
    const std::vector<BasicValue>& args, // For method arguments OR indexed property arguments
    _variant_t& result,                   // For method return value / property get result
    WORD dwFlags,                         // DISPATCH_METHOD, DISPATCH_PROPERTYGET, DISPATCH_PROPERTYPUT
    const _variant_t* pPropertyValue // Optional for property put (the value being assigned)
) {
    if (!pDisp) return E_POINTER;

    _bstr_t bstrMember(memberName.c_str());
    DISPID dispID;
    HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &bstrMember.GetBSTR(), 1, LOCALE_USER_DEFAULT, &dispID);
    if (FAILED(hr)) {
        // Handle "Member not found" error
        return hr;
    }

    DISPPARAMS dp = { 0 };
    std::vector<_variant_t> varArgs; // Use a vector to manage _variant_t lifetimes

    // Arguments are typically passed in reverse order for COM Invoke
    // For PROPERTYPUT, the actual value to set is the FIRST (rightmost) argument.
    // For METHOD/PROPERTYGET, the arguments are processed normally.

    // If this is a PROPERTYPUT, add the property value first (this will be the last arg in COM's rgvarg)
    if (dwFlags & DISPATCH_PROPERTYPUT) {
        if (!pPropertyValue) {
            return E_INVALIDARG; // Must provide a value for PROPERTYPUT
        }
        varArgs.push_back(*pPropertyValue);
    }

    // Now add the rest of the arguments (e.g., row/column for Cells property, or method parameters)
    // These are processed in reverse order relative to their appearance in Basic code.
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        varArgs.push_back(basic_value_to_variant_t(*it));
    }

    if (!varArgs.empty()) {
        dp.rgvarg = varArgs.data();
        dp.cArgs = (UINT)varArgs.size();
    }

    // For property put, need to set the DISPID_PROPERTYPUT argument
    // This named argument ensures COM knows which argument is the property's new value.
    DISPID dispIDNamedArgs = DISPID_PROPERTYPUT; // This is a special DISPID value
    if (dwFlags & DISPATCH_PROPERTYPUT) {
        dp.rgdispidNamedArgs = &dispIDNamedArgs;
        dp.cNamedArgs = 1; // Only one named argument for the property value
    }

    result.Clear(); // Clear result variant before invoke

    // Call Invoke
    hr = pDisp->Invoke(dispID, IID_NULL, LOCALE_USER_DEFAULT, dwFlags, &dp, &result, NULL, NULL);

    return hr;
}
#endif

#ifdef __EMSCRIPTEN__
extern std::deque<int> g_inkey_buffer;
#endif

namespace fs = std::filesystem;

// We need access to the helper functions for type conversion
bool to_bool(const BasicValue& val);
double to_double(const BasicValue& val);
std::string to_string(const BasicValue& val);

// Converts a simple wildcard string (*, ?) to a valid ECMA-style regex string.
std::string wildcard_to_regex(const std::string& wildcard) {
    std::string regex_str;
    // Anchor the pattern to match the whole string.
    regex_str += '^';
    for (char c : wildcard) {
        switch (c) {
        case '*':
            regex_str += ".*"; // * matches any sequence of characters
            break;
        case '?':
            regex_str += '.';  // ? matches any single character
            break;
            // Escape special regex characters
        case '.':
        case '\\':
        case '+':
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case '^':
        case '$':
        case '|':
            regex_str += '\\';
            regex_str += c;
            break;
        default:
            regex_str += c;
        }
    }
    // Anchor the pattern to match the whole string.
    regex_str += '$';
    return regex_str;
}

// --- JSON Functionality ---

// Forward declaration for recursive conversion
nlohmann::json basic_to_json_value(const BasicValue& val);

// Helper function to convert a BasicValue into a nlohmann::json object.
nlohmann::json basic_to_json_value_for_serialize(const BasicValue& val) {
    return std::visit([](auto&& arg) -> nlohmann::json {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> || std::is_same_v<T, int> || std::is_same_v<T, long long> || std::is_same_v<T, std::string>) {
            return nlohmann::json(arg);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            if (!arg) return nlohmann::json(); // Serialize null pointer as JSON null
            // Create a JSON object to store both shape and data
            nlohmann::json j_obj;
            j_obj["__type__"] = "array";
            j_obj["shape"] = arg->shape; // Save the shape vector
            j_obj["data"] = nlohmann::json::array();
            for (const auto& elem : arg->data) {
                j_obj["data"].push_back(basic_to_json_value(elem));
            }
            return j_obj;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Map>>) {
            if (!arg) return nlohmann::json::object();
            nlohmann::json j_obj = nlohmann::json::object();
            for (const auto& pair : arg->data) {
                j_obj[pair.first] = basic_to_json_value(pair.second);
            }
            return j_obj;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<JsonObject>>) {
            return arg ? arg->data : nlohmann::json(nullptr);
        }
        else if constexpr (std::is_same_v<T, DateTime> || std::is_same_v<T, FunctionRef>) {
            return nlohmann::json(to_string(arg));
        }
        else {
            return nlohmann::json(nullptr);
        }
        }, val);
}

// Helper function to convert a BasicValue into a nlohmann::json object.
nlohmann::json basic_to_json_value(const BasicValue& val) {
    return std::visit([](auto&& arg) -> nlohmann::json {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> || std::is_same_v<T, int> || std::is_same_v<T, long long> || std::is_same_v<T, std::string>) {
            return nlohmann::json(arg);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            if (!arg) return nlohmann::json::array(); // Serialize null pointer as empty JSON array
            // Create a standard JSON array
            nlohmann::json j_arr = nlohmann::json::array();
            for (const auto& elem : arg->data) {
                j_arr.push_back(basic_to_json_value(elem)); // Recursively convert each element
            }
            return j_arr;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Map>>) {
            if (!arg) return nlohmann::json::object();
            nlohmann::json j_obj = nlohmann::json::object();
            for (const auto& pair : arg->data) {
                j_obj[pair.first] = basic_to_json_value(pair.second);
            }
            return j_obj;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<JsonObject>>) {
            return arg ? arg->data : nlohmann::json(nullptr);
        }
        else if constexpr (std::is_same_v<T, DateTime> || std::is_same_v<T, FunctionRef>) {
            return nlohmann::json(to_string(arg));
        }
        else {
            return nlohmann::json(nullptr);
        }
        }, val);
}

// Helper function to convert a nlohmann::json object to a BasicValue
BasicValue json_to_basic_value(const nlohmann::json& j) {
    if (j.is_null()) {
        return 0.0;
    }
    if (j.is_boolean()) {
        return j.get<bool>();
    }
    if (j.is_number()) {
        return j.get<double>();
    }
    if (j.is_string()) {
        return j.get<std::string>();
    }
    if (j.is_object()) {
        // Check for our special array format first
        if (j.contains("__type__") && j["__type__"] == "array") {
            auto arr_ptr = std::make_shared<Array>();
            arr_ptr->shape = j["shape"].get<std::vector<size_t>>();
            const auto& data_arr = j["data"];
            for (const auto& item : data_arr) {
                arr_ptr->data.push_back(json_to_basic_value(item));
            }
            return arr_ptr;
        }
        // Otherwise, it's a Map
        auto map_ptr = std::make_shared<Map>();
        for (auto& [key, value] : j.items()) {
            map_ptr->data[key] = json_to_basic_value(value);
        }
        return map_ptr;
    }
    if (j.is_array()) {
        // Fallback for simple JSON arrays (treats them as 1D)
        auto array_ptr = std::make_shared<Array>();
        for (const auto& item : j) {
            array_ptr->data.push_back(json_to_basic_value(item));
        }
        array_ptr->shape = { array_ptr->data.size() };
        return array_ptr;
    }
    return 0.0;
}


// JSON.PARSE$(json_string$) -> JsonObject
BasicValue builtin_json_parse(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return {}; // Return empty BasicValue
    }

    std::string json_string = to_string(args[0]);

    try {
        // Create our JsonObject wrapper
        auto json_obj_ptr = std::make_shared<JsonObject>();

        // Use the nlohmann library to parse the string
        json_obj_ptr->data = nlohmann::json::parse(json_string);

        // Return the shared pointer to our JsonObject inside the BasicValue
        return json_obj_ptr;
    }
    catch (const nlohmann::json::parse_error& e) {
        // If parsing fails, set a BASIC error and return.
        Error::set(1, vm.runtime_current_line); // Syntax Error (or a new "Invalid JSON" error)
        TextIO::print("JSON Parse Error: " + std::string(e.what())); TextIO::nl();
        return {};
    }
}

// JSON.STRINGIFY$(basic_value) -> string$
BasicValue builtin_json_stringify(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return std::string("");
    }

    const BasicValue& val_to_stringify = args[0];

    try {
        nlohmann::json j = basic_to_json_value_for_serialize(val_to_stringify);
        // dump() with no arguments creates a compact string, ideal for API calls.
        // For pretty-printing, you could use j.dump(4)
        //return j.dump();
        return j.dump(4);
    }
    catch (const std::exception& e) {
        Error::set(15, vm.runtime_current_line); // Type Mismatch or other conversion error
        TextIO::print("JSON Stringify Error: " + std::string(e.what())); TextIO::nl();
        return std::string("");
    }
}

//=========================================================
// NEW: Map Helper Functions
//=========================================================

// MAP.EXISTS(map, key$) -> boolean
BasicValue builtin_map_exists(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to MAP.EXISTS must be a Map.");
        return false;
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variable is null.");
        return false;
    }

    const std::string key = to_string(args[1]);

    return map_ptr->data.count(key) > 0;
}

// MAP.KEYS(map) -> array
BasicValue builtin_map_keys(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to MAP.KEYS must be a Map.");
        return {};
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variable is null.");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->data.reserve(map_ptr->data.size());
    
    for (const auto& pair : map_ptr->data) {
        result_ptr->data.push_back(pair.first);
    }

    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// MAP.VALUES(map) -> array
BasicValue builtin_map_values(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to MAP.VALUES must be a Map.");
        return {};
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variable is null.");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->data.reserve(map_ptr->data.size());

    for (const auto& pair : map_ptr->data) {
        result_ptr->data.push_back(pair.second);
    }

    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// MAP.DELETE(map, key$)
BasicValue builtin_map_delete(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "MAP.DELETE requires 2 arguments: map, key$");
        return false;
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to MAP.DELETE must be a Map.");
        return false;
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variable is null.");
        return false;
    }

    const std::string key = to_string(args[1]);
    map_ptr->data.erase(key); // erase() safely does nothing if the key doesn't exist

    return false; // This is a procedure
}

// MAP.CLEAR(map)
BasicValue builtin_map_clear(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "MAP.CLEAR requires 1 argument: map");
        return false;
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to MAP.CLEAR must be a Map.");
        return false;
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variable is null.");
        return false;
    }

    map_ptr->data.clear();

    return false; // This is a procedure
}

// MAP.SIZE(map) -> number
BasicValue builtin_map_size(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to MAP.SIZE must be a Map.");
        return 0.0;
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        return 0.0; // Size of a null map is 0
    }

    return static_cast<double>(map_ptr->data.size());
}

// MAP.MERGE(destination_map, source_map)
BasicValue builtin_map_merge(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "MAP.MERGE requires 2 arguments: destination_map, source_map");
        return false;
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0]) || !std::holds_alternative<std::shared_ptr<Map>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Both arguments to MAP.MERGE must be Maps.");
        return false;
    }

    const auto& dest_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    const auto& source_ptr = std::get<std::shared_ptr<Map>>(args[1]);

    if (!dest_ptr || !source_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variables for MAP.MERGE cannot be null.");
        return false;
    }

    // The insert_or_assign method is perfect for this. It inserts new elements
    // and updates existing ones.
    dest_ptr->data.insert(source_ptr->data.begin(), source_ptr->data.end());

    return false; // This is a procedure
}

// MAP.ITEMS(map) -> array
BasicValue builtin_map_items(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to MAP.ITEMS must be a Map.");
        return {};
    }

    const auto& map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    if (!map_ptr) {
        Error::set(15, vm.runtime_current_line, "Map variable is null.");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->data.reserve(map_ptr->data.size() * 2);

    for (const auto& pair : map_ptr->data) {
        result_ptr->data.push_back(pair.first);  // Key
        result_ptr->data.push_back(pair.second); // Value
    }

    result_ptr->shape = { map_ptr->data.size(), 2 };
    return result_ptr;
}

// MAP.FROM(json_object_string$) -> Map
// Creates a Map from a string formatted as a JSON object.
BasicValue builtin_map_from(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "MAP.FROM requires exactly one string argument.");
        return {}; // Return empty BasicValue, results in a null map pointer
    }

    std::string json_string = to_string(args[0]);
    if (json_string.empty()) {
        // Return a new, empty map if the string is empty.
        return std::make_shared<Map>();
    }

    try {
        // 1. Parse the string into a nlohmann::json object.
        nlohmann::json j = nlohmann::json::parse(json_string);

        // 2. Validate that the top-level entity is an object.
        if (!j.is_object()) {
            Error::set(15, vm.runtime_current_line, "Input string for MAP.FROM must be a valid JSON object (e.g., {\"key\":\"value\"}).");
            return {};
        }

        // 3. Use the existing helper to convert the JSON object to a BasicValue (which will be a Map).
        return json_to_basic_value(j);
    }
    catch (const nlohmann::json::parse_error& e) {
        // If parsing fails, set a BASIC error and return.
        Error::set(1, vm.runtime_current_line, "Invalid JSON format in string for MAP.FROM: " + std::string(e.what()));
        return {};
    }
}

//=========================================================
// C++ Implementations of our Native BASIC Functions
//=========================================================

/**
 * @brief Implements the HELP$() function.
 * @return An array of strings containing all available help topics.
 */
BasicValue builtin_help_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "HELP$ does not accept arguments.");
        return {}; // Return empty BasicValue, which will result in a null array pointer
    }

    std::ifstream help_file("help.txt");
    if (!help_file) {
        // For a function that returns a value, we don't print an error to the console.
        // We just return an empty array, which is valid in BASIC.
        auto empty_array = std::make_shared<Array>();
        empty_array->shape = { 0 };
        return empty_array;
    }

    auto result_ptr = std::make_shared<Array>();
    std::string line;
    while (std::getline(help_file, line)) {
        if (!line.empty() && line[0] == '[') {
            size_t end_pos = line.find(']');
            if (end_pos != std::string::npos) {
                result_ptr->data.push_back(line.substr(1, end_pos - 1));
            }
        }
    }

    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

/**
 * @brief Implements the HELP procedure.
 * @details If called with no arguments, it prints a multi-column list of all topics.
 * If called with a topic string, it prints the detailed help for that topic.
 */
BasicValue builtin_help(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() > 1) {
        Error::set(8, vm.runtime_current_line, "HELP accepts zero or one argument.");
        return false; // Procedures return a dummy value
    }

    std::string topic;
    if (!args.empty()) {
        topic = to_upper(to_string(args[0]));
    }

    std::ifstream help_file("help.txt");
    if (!help_file) {
        TextIO::print("Error: help.txt not found."); TextIO::nl();
        return false;
    }

    if (topic.empty()) {
        // If no topic is specified, list all available commands in multiple columns.
        TextIO::print("Available commands and functions. Use HELP \"command\" for details."); TextIO::nl(); TextIO::nl();
        std::string line;
        int command_count = 0;
        const int commands_per_line = 3;
        const int column_width = 30; // Width for each column

        while (std::getline(help_file, line)) {
            if (!line.empty() && line[0] == '[') {
                size_t end_pos = line.find(']');
                if (end_pos != std::string::npos) {
                    std::string command = line.substr(1, end_pos - 1);

                    std::stringstream ss;
                    ss << std::left << std::setw(column_width) << command;
                    TextIO::print(ss.str());

                    command_count++;
                    if (command_count % commands_per_line == 0) {
                        TextIO::nl();
                    }
                }
            }
        }
        // Print a final newline if the last line of commands wasn't full
        if (command_count % commands_per_line != 0) {
            TextIO::nl();
        }
        TextIO::nl();
        return false;
    }

    // Search for the specific topic in the help file (this part is unchanged)
    std::string search_tag = "[" + topic + "]";
    std::string line;
    bool found = false;
    while (std::getline(help_file, line)) {
        if (line == search_tag) {
            found = true;
            TextIO::nl();
            // Print the next two lines which contain Syntax and Description
            if (std::getline(help_file, line)) TextIO::print(line); TextIO::nl();
            if (std::getline(help_file, line)) TextIO::print(line); TextIO::nl();
            TextIO::nl();
            break;
        }
    }

    if (!found) {
        TextIO::print("No help found for topic: " + topic); TextIO::nl();
    }

    return false; // It's a procedure, so it returns a dummy boolean
}

BasicValue builtin_setlocale(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    std::string locale_name = to_string(args[0]);
    LocaleManager::set_current_locale(locale_name); // Call the global manager

    return false;
}

// --- String Functions ---

// LEN(string_expression) or LEN(array_variable)
BasicValue builtin_len(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, 0); // Wrong number of arguments
        return 0.0;
    }

    const BasicValue& val = args[0];

    // --- Case 1: The argument is ALREADY an array ---
    if (std::holds_alternative<std::shared_ptr<Array>>(val)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(val);
        if (arr_ptr) {
            // Create a new vector (1D Array) to hold the shape information
            auto shape_vector_ptr = std::make_shared<Array>();
            shape_vector_ptr->shape = { arr_ptr->shape.size() };
            for (size_t dim : arr_ptr->shape) {
                shape_vector_ptr->data.push_back(static_cast<double>(dim));
            }
            return shape_vector_ptr;
        }
        else {
            // This case is for a null array pointer, return 0.
            return 0.0;
        }
    }

    // --- Case 2: The argument is a string that might be a variable name ---
    if (std::holds_alternative<std::string>(val)) {
        std::string name = to_upper(std::get<std::string>(val));
        // Check if a variable with this name exists
        if (vm.variables.count(name)) {
            const BasicValue& var_val = vm.variables.at(name);
            // Check if that variable holds an array
            if (std::holds_alternative<std::shared_ptr<Array>>(var_val)) {
                const auto& arr_ptr = std::get<std::shared_ptr<Array>>(var_val);
                if (arr_ptr) {
                    auto shape_vector_ptr = std::make_shared<Array>();
                    shape_vector_ptr->shape = { arr_ptr->shape.size() };
                    for (size_t dim : arr_ptr->shape) {
                        shape_vector_ptr->data.push_back(static_cast<double>(dim));
                    }
                    return shape_vector_ptr;
                }
            }
        }
    }

    // --- Case 3: Fallback to original behavior (length of string representation) ---
    return static_cast<double>(to_string(val).length());
}

// --- String Functions ---

// A generic helper to apply an operation element-wise to an array or a scalar.
// The provided lambda 'op' can set errors via the vm_ref.
BasicValue apply_elementwise_op(NeReLaBasic& vm, const BasicValue& input, const std::function<BasicValue(NeReLaBasic&, const BasicValue&)>& op) {
    // Case 1: Input is an Array. Apply the op to each element.
    if (const auto& arr_ptr = std::get_if<std::shared_ptr<Array>>(&input)) {
        if (!*arr_ptr) return {}; // Return empty on null pointer

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = (*arr_ptr)->shape; // Result has the same shape
        result_ptr->data.reserve((*arr_ptr)->data.size());

        for (const auto& val : (*arr_ptr)->data) {
            BasicValue result = op(vm, val);
            // If the operation on an element failed, stop and return.
            if (Error::get() != 0) {
                return {};
            }
            result_ptr->data.push_back(result);
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar. Apply the op directly.
    else {
        return op(vm, input);
    }
}

// LEFT$(string_or_array, n) -> string or array
BasicValue builtin_left_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "LEFT$ requires 2 arguments.");
        return std::string("");
    }

    const BasicValue& input = args[0];
    int count = static_cast<int>(to_double(args[1]));
    if (count < 0) count = 0;

    // Helper lambda to perform the core LEFT$ operation on a single string.
    auto perform_left = [count](const std::string& source) {
        return source.substr(0, count);
        };

    // Case 1: Input is an Array. Apply the operation to each element.
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(input);
        if (!arr_ptr) return {}; // Handle null array

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = arr_ptr->shape; // Result has the same shape
        result_ptr->data.reserve(arr_ptr->data.size());

        for (const auto& val : arr_ptr->data) {
            result_ptr->data.push_back(perform_left(to_string(val)));
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar. Perform the operation directly.
    else {
        return perform_left(to_string(input));
    }
}

// RIGHT$(string_or_array, n) -> string or array
BasicValue builtin_right_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "RIGHT$ requires 2 arguments.");
        return std::string("");
    }

    const BasicValue& input = args[0];
    int count = static_cast<int>(to_double(args[1]));
    if (count < 0) count = 0;

    // Helper lambda to perform the core RIGHT$ operation on a single string.
    auto perform_right = [count](const std::string& source) {
        if (static_cast<size_t>(count) > source.length()) {
            return source;
        }
        return source.substr(source.length() - count);
        };

    // Case 1: Input is an Array. Apply the operation to each element.
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(input);
        if (!arr_ptr) return {}; // Handle null array

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = arr_ptr->shape;
        result_ptr->data.reserve(arr_ptr->data.size());

        for (const auto& val : arr_ptr->data) {
            result_ptr->data.push_back(perform_right(to_string(val)));
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar. Perform the operation directly.
    else {
        return perform_right(to_string(input));
    }
}

// MID$(string_or_array, start, [length]) -> string or array
// Returns a substring or sub-array.
// Start position is 0 - based.
BasicValue builtin_mid_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "MID$ requires 2 or 3 arguments.");
        return std::string("");
    }

    const BasicValue& input = args[0];
    int start = static_cast<int>(to_double(args[1]));
    if (start < 0) start = 0;

    // Helper lambda to perform the core MID$ operation on a single string.
    auto perform_mid = [&](const std::string& source) -> std::string {
        if (static_cast<size_t>(start) >= source.length()) {
            return ""; // Start position is out of bounds
        }
        if (args.size() == 2) { // MID$(str, start) -> get rest of string
            return source.substr(start);
        }
        else { // MID$(str, start, len)
            int length = static_cast<int>(to_double(args[2]));
            if (length < 0) length = 0;
            return source.substr(start, length);
        }
        };

    // Case 1: Input is an Array. Apply the operation to each element.
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(input);
        if (!arr_ptr) return {};

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = arr_ptr->shape;
        result_ptr->data.reserve(arr_ptr->data.size());

        for (const auto& val : arr_ptr->data) {
            result_ptr->data.push_back(perform_mid(to_string(val)));
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar. Perform the operation directly.
    else {
        return perform_mid(to_string(input));
    }
}

// Helper function to apply a string-to-string operation element-wise.
// This simplifies the implementation for LCASE$, UCASE$, and TRIM$.
BasicValue apply_string_op(const BasicValue& input, const std::function<std::string(const std::string&)>& op) {
    // Case 1: Input is an Array (vector or matrix)
    if (const auto& arr_ptr = std::get_if<std::shared_ptr<Array>>(&input)) {
        if (!*arr_ptr) return {}; // Return empty on null pointer

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = (*arr_ptr)->shape; // Result has the same shape
        result_ptr->data.reserve((*arr_ptr)->data.size());

        // Apply the operation to each element
        for (const auto& val : (*arr_ptr)->data) {
            result_ptr->data.push_back(op(to_string(val)));
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar
    else {
        return op(to_string(input));
    }
}

// LCASE$(string_or_array) -> string or array
BasicValue builtin_lcase_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "LCASE$ requires 1 argument.");
        return std::string("");
    }
    auto op = [](const std::string& s) {
        std::string lower_s = s;
        std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return lower_s;
        };
    return apply_string_op(args[0], op);
}

// UCASE$(string_or_array) -> string or array
BasicValue builtin_ucase_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "UCASE$ requires 1 argument.");
        return std::string("");
    }
    auto op = [](const std::string& s) {
        std::string upper_s = s;
        std::transform(upper_s.begin(), upper_s.end(), upper_s.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return upper_s;
        };
    return apply_string_op(args[0], op);
}

// TRIM$(string_or_array) -> string or array
BasicValue builtin_trim_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "TRIM$ requires 1 argument.");
        return std::string("");
    }
    auto op = [](const std::string& s) {
        std::string trimmed_s = s;
        size_t start = trimmed_s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return std::string(""); // String is all whitespace
        size_t end = trimmed_s.find_last_not_of(" \t\n\r");
        return trimmed_s.substr(start, end - start + 1);
        };
    return apply_string_op(args[0], op);
}

// INSTR([start], haystack$, needle$) -> number
// Finds the starting position of needle$ in haystack$.
// Positions are 0-based. Returns -1 if not found.
BasicValue builtin_instr(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "INSTR requires 2 or 3 arguments.");
        return -1.0;
    }

    size_t start_pos = 0;
    std::string haystack, needle;

    if (args.size() == 2) {
        haystack = to_string(args[0]);
        needle = to_string(args[1]);
    }
    else {
        // Start position is now 0-based
        start_pos = static_cast<size_t>(to_double(args[0]));
        haystack = to_string(args[1]);
        needle = to_string(args[2]);
    }

    if (start_pos > haystack.length()) return -1.0;

    size_t found_pos = haystack.find(needle, start_pos);

    if (found_pos == std::string::npos) {
        return -1.0; // Not found, return -1
    }
    else {
        return static_cast<double>(found_pos); // Return 0-indexed position
    }
}

// INSERT$(target$, text_to_insert$, position) -> string or array
// Fully vectorized: any argument can be a scalar or an array.
// If multiple arguments are arrays, their shapes must match.
BasicValue builtin_insert_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "INSERT$ requires 3 arguments: source$, text_to_insert$, position.");
        return std::string("");
    }

    const BasicValue& source_arg = args[0];
    const BasicValue& insert_arg = args[1];
    const BasicValue& pos_arg = args[2];

    bool source_is_array = std::holds_alternative<std::shared_ptr<Array>>(source_arg);
    bool insert_is_array = std::holds_alternative<std::shared_ptr<Array>>(insert_arg);
    bool pos_is_array = std::holds_alternative<std::shared_ptr<Array>>(pos_arg);

    // Helper lambda to perform the core insert operation on a single set of arguments.
    auto perform_single_insert = [](const std::string& source, const std::string& text_to_insert, int position) -> std::string {
        std::string result = source;
        size_t pos = static_cast<size_t>(position);

        // Clamp the position to be within the valid range [0, source.length()]
        if (position < 0) {
            pos = 0;
        }
        else if (pos > result.length()) {
            pos = result.length();
        }

        result.insert(pos, text_to_insert);
        return result;
        };

    // Case 1: All arguments are scalars.
    if (!source_is_array && !insert_is_array && !pos_is_array) {
        return perform_single_insert(to_string(source_arg), to_string(insert_arg), static_cast<int>(to_double(pos_arg)));
    }

    // Case 2: At least one argument is an array (vectorized operation).
    std::vector<size_t> shape;
    size_t total_size = 0;

    const auto& source_arr = source_is_array ? std::get<std::shared_ptr<Array>>(source_arg) : nullptr;
    const auto& insert_arr = insert_is_array ? std::get<std::shared_ptr<Array>>(insert_arg) : nullptr;
    const auto& pos_arr = pos_is_array ? std::get<std::shared_ptr<Array>>(pos_arg) : nullptr;

    // Determine the shape and size from the first available array.
    if (source_is_array) {
        if (!source_arr) { Error::set(15, vm.runtime_current_line, "Source array cannot be null."); return {}; }
        shape = source_arr->shape;
        total_size = source_arr->data.size();
    }
    else if (insert_is_array) {
        if (!insert_arr) { Error::set(15, vm.runtime_current_line, "Insert array cannot be null."); return {}; }
        shape = insert_arr->shape;
        total_size = insert_arr->data.size();
    }
    else { // pos_is_array must be true
        if (!pos_arr) { Error::set(15, vm.runtime_current_line, "Position array cannot be null."); return {}; }
        shape = pos_arr->shape;
        total_size = pos_arr->data.size();
    }

    // Validate that all provided arrays have the same shape.
    if (source_arr && source_arr->shape != shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise INSERT$."); return {}; }
    if (insert_arr && insert_arr->shape != shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise INSERT$."); return {}; }
    if (pos_arr && pos_arr->shape != shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise INSERT$."); return {}; }

    // Build the result array.
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = shape;
    result_ptr->data.reserve(total_size);

    for (size_t i = 0; i < total_size; ++i) {
        // Get the appropriate value for each argument: either from the array or broadcast the scalar.
        std::string current_source = source_is_array ? to_string(source_arr->data[i]) : to_string(source_arg);
        std::string current_insert = insert_is_array ? to_string(insert_arr->data[i]) : to_string(insert_arg);
        int current_pos = pos_is_array ? static_cast<int>(to_double(pos_arr->data[i])) : static_cast<int>(to_double(pos_arg));

        result_ptr->data.push_back(perform_single_insert(current_source, current_insert, current_pos));
    }

    return result_ptr;
}

#ifdef __EMSCRIPTEN__
extern "C" char* js_get_inkey();
#endif

// INKEY$() > string$
BasicValue builtin_inkey(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return std::string("");
    }

    // --- Context-aware logic ---
#ifdef SDL3
    // If the graphics system is initialized, try to get input from SDL first.
    if (vm.graphics_system.is_initialized) {
        std::string s = vm.graphics_system.get_key_from_buffer();
        if (!s.empty()) return s;

        // Fallback: If SDL didn't catch it (e.g. focus is on the HTML input, not canvas),
        // check the global Emscripten buffer.
#ifdef __EMSCRIPTEN__
        if (!g_inkey_buffer.empty()) {
            char c = static_cast<char>(g_inkey_buffer.front());
            g_inkey_buffer.pop_front();
            return std::string(1, c);
        }
#endif
        return "";
    }
#endif

    // --- Original console-based logic (fallback) ---
#ifdef _WIN32    
    if (_kbhit()) {
        char c = _getch();
        return std::string(1, c);
    }
#elif defined(__EMSCRIPTEN__)
    // FIX: Read from the buffer populated by process_system_events
    if (!g_inkey_buffer.empty()) {
        char c = static_cast<char>(g_inkey_buffer.front());
        g_inkey_buffer.pop_front();
        return std::string(1, c);
    }
    return std::string("");
#else
    char c = TextIO::jdgetch();
    if (c > 0) {
        return std::string(1, c);
    }
#endif
    return std::string("");
}

// WAITKEY$() -> string$
// Waits for a key to be pressed and returns it as a string.
BasicValue builtin_waitkey_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "WAITKEY$ takes no arguments.");
        return std::string("");
    }

#ifdef SDL3
    // If the graphics system is active, we must wait in a loop that
    // continues to process events, so the window doesn't freeze.
    if (vm.graphics_system.is_initialized) {
        std::string key;
        while (true) {
            // The main event handler for SDL is in the graphics system.
            // Calling this in a loop effectively waits for an event.
            if (!vm.graphics_system.handle_events(vm)) {
                // This indicates the user closed the window.
                // vm.program_ended = true; // Signal the main loop to terminate
                return std::string("");
            }

            // Check if handle_events() populated the key buffer.
            key = vm.graphics_system.get_key_from_buffer();
            if (!key.empty()) {
                return key;
            }

            // Also process any internal BASIC events that might have been queued.
            vm.process_event_queue();

            // A short delay to prevent this waiting loop from using 100% CPU.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#endif

    // Fallback to standard blocking console input if graphics are not initialized.
#if defined(_WIN32)  
    char c = _getch();
    return std::string(1, c);
#elif defined(__EMSCRIPTEN__)  
#else
    char c = getch();
    return std::string(1, c);
#endif
    
}

// REVERSE$(string_or_array) -> string or array
BasicValue builtin_reverse_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "REVERSE$ requires 1 argument.");
        return std::string("");
    }
    // Define the reverse operation as a lambda
    auto op = [](const std::string& s) {
        std::string reversed_s = s;
        std::reverse(reversed_s.begin(), reversed_s.end());
        return reversed_s;
        };
    // Use the existing helper to apply it element-wise
    return apply_string_op(args[0], op);
}

// REPLACE$(source, find, replace) -> string or array
// Fully vectorized: any argument can be a scalar or an array.
// If multiple arguments are arrays, their shapes must match.
BasicValue builtin_replace_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "REPLACE$ requires 3 arguments.");
        return std::string("");
    }

    const BasicValue& source_arg = args[0];
    const BasicValue& find_arg = args[1];
    const BasicValue& replace_arg = args[2];

    bool source_is_array = std::holds_alternative<std::shared_ptr<Array>>(source_arg);
    bool find_is_array = std::holds_alternative<std::shared_ptr<Array>>(find_arg);
    bool replace_is_array = std::holds_alternative<std::shared_ptr<Array>>(replace_arg);

    // Helper lambda to perform the core replace operation on a single set of strings.
    auto perform_single_replace = [](const std::string& source, const std::string& find_str, const std::string& replace_str) -> std::string {
        if (find_str.empty()) {
            return source; // Avoid infinite loops on empty find string
        }
        std::string result = source;
        size_t start_pos = 0;
        while ((start_pos = result.find(find_str, start_pos)) != std::string::npos) {
            result.replace(start_pos, find_str.length(), replace_str);
            start_pos += replace_str.length();
        }
        return result;
        };

    // Case 1: All arguments are scalars (the simplest case).
    if (!source_is_array && !find_is_array && !replace_is_array) {
        return perform_single_replace(to_string(source_arg), to_string(find_arg), to_string(replace_arg));
    }

    // Case 2: At least one argument is an array (vectorized operation).
    std::vector<size_t> shape;
    size_t total_size = 0;

    const auto& source_arr = source_is_array ? std::get<std::shared_ptr<Array>>(source_arg) : nullptr;
    const auto& find_arr = find_is_array ? std::get<std::shared_ptr<Array>>(find_arg) : nullptr;
    const auto& replace_arr = replace_is_array ? std::get<std::shared_ptr<Array>>(replace_arg) : nullptr;

    // Determine the shape and size from the first available array.
    if (source_is_array) {
        if (!source_arr) { Error::set(15, vm.runtime_current_line, "Source array cannot be null."); return {}; }
        shape = source_arr->shape;
        total_size = source_arr->data.size();
    }
    else if (find_is_array) {
        if (!find_arr) { Error::set(15, vm.runtime_current_line, "Find array cannot be null."); return {}; }
        shape = find_arr->shape;
        total_size = find_arr->data.size();
    }
    else { // replace_is_array must be true
        if (!replace_arr) { Error::set(15, vm.runtime_current_line, "Replace array cannot be null."); return {}; }
        shape = replace_arr->shape;
        total_size = replace_arr->data.size();
    }

    // Validate that all provided arrays have the same shape.
    if (source_arr && source_arr->shape != shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise REPLACE$."); return {}; }
    if (find_arr && find_arr->shape != shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise REPLACE$."); return {}; }
    if (replace_arr && replace_arr->shape != shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise REPLACE$."); return {}; }

    // Build the result array.
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = shape;
    result_ptr->data.reserve(total_size);

    for (size_t i = 0; i < total_size; ++i) {
        // Get the appropriate value for each argument: either from the array or broadcast the scalar.
        std::string current_source = source_is_array ? to_string(source_arr->data[i]) : to_string(source_arg);
        std::string current_find = find_is_array ? to_string(find_arr->data[i]) : to_string(find_arg);
        std::string current_replace = replace_is_array ? to_string(replace_arr->data[i]) : to_string(replace_arg);

        result_ptr->data.push_back(perform_single_replace(current_source, current_find, current_replace));
    }

    return result_ptr;
}

// VAL(string_expression_or_array) -> number or array
BasicValue builtin_val(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    auto op = [](NeReLaBasic&, const BasicValue& v) -> BasicValue {
        std::string s = to_string(v);
        try {
            // std::stod mimics classic VAL behavior by parsing until a non-numeric char.
            return std::stod(s);
        }
        catch (const std::exception&) {
            return 0.0;
        }
        };
    return apply_elementwise_op(vm, args[0], op);
}

// STR$(numeric_expression_or_array) -> string or array
BasicValue builtin_str_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return std::string("");
    }
    // The operation is simply our existing to_string helper.
    auto op = [](NeReLaBasic&, const BasicValue& v) -> BasicValue {
        return to_string(v);
        };
    return apply_elementwise_op(vm, args[0], op);
}

// SPLIT(source_string$, delimiter_string$) -> array
// Splits a string into an array of substrings based on a delimiter.
BasicValue builtin_split(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }

    std::string source = to_string(args[0]);
    std::string delimiter = to_string(args[1]);

    if (delimiter.empty()) {
        Error::set(1, vm.runtime_current_line); // Cannot split by empty delimiter
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    size_t start = 0;
    size_t end = source.find(delimiter);

    while (end != std::string::npos) {
        result_ptr->data.push_back(source.substr(start, end - start));
        start = end + delimiter.length();
        end = source.find(delimiter, start);
    }
    // Add the last token
    result_ptr->data.push_back(source.substr(start, end));

    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// CHR$(number_or_array)
BasicValue builtin_chr_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return std::string("");
    }

    // This lambda contains the core logic for converting a code point to a UTF-8 string.
    auto chr_op = [](NeReLaBasic&, const BasicValue& v) -> BasicValue {
        long code_point = static_cast<long>(to_double(v));

        if (code_point < 0 || code_point > 0x10FFFF) {
            return std::string(""); // Invalid Unicode code point
        }

#if defined(_WIN32)
        // --- Windows-specific, non-deprecated method ---
        wchar_t wstr[2];
        if (code_point <= 0xFFFF) {
            // Fits in a single wchar_t
            wstr[0] = static_cast<wchar_t>(code_point);
            wstr[1] = L'\0';
        }
        else {
            // Handle supplementary planes (e.g., emojis) by creating a surrogate pair
            // This is more advanced but correct for full Unicode support.
            // For now, we can simplify and just handle the Basic Multilingual Plane.
            // A simple approach for characters > 0xFFFF might return "" or "?".
            // However, most common characters (including all of Latin-1, etc.) are <= 0xFFFF.
            return std::string(""); // Or handle surrogate pairs if needed
        }

        // First, find the required buffer size
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], -1, NULL, 0, NULL, NULL);
        if (size_needed == 0) {
            return std::string(""); // Conversion error
        }

        // Allocate buffer and perform the conversion
        std::string utf8_str(size_needed - 1, 0); // -1 to not include the null terminator
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], -1, &utf8_str[0], size_needed, NULL, NULL);

        return utf8_str;
#else
        // --- C++17 Deprecated (but cross-platform) method for other systems ---
        // You can keep the old code here inside an #else block for Linux/macOS
        try {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
            return converter.to_bytes(static_cast<wchar_t>(code_point));
        }
        catch (const std::range_error&) {
            return std::string("");
        }
#endif
        };

    // Use the helper to apply the operation element-wise.
    return apply_elementwise_op(vm, args[0], chr_op);
}

// ASC(string_or_array)
BasicValue builtin_asc(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }

    // This lambda contains the core logic for a single element.
    auto asc_op = [](NeReLaBasic&, const BasicValue& v) -> BasicValue {
        std::string s = to_string(v);
        if (s.empty()) {
            return 0.0;
        }
        return static_cast<double>(static_cast<unsigned char>(s[0]));
        };

    // Use the helper to apply the operation element-wise.
    return apply_elementwise_op(vm, args[0], asc_op);
}

namespace { // Anonymous namespace for local helpers

    // Helper for binary bitwise operations that supports vectorization
    BasicValue apply_bitwise_op(
        const BasicValue& left,
        const BasicValue& right,
        const std::function<long long(long long, long long)>& op
    ) {
        // Case 1: Array vs Array
        if (std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
            const auto& left_ptr = std::get<std::shared_ptr<Array>>(left);
            const auto& right_ptr = std::get<std::shared_ptr<Array>>(right);
            if (!left_ptr || !right_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }
            if (left_ptr->shape != right_ptr->shape) { Error::set(15, 0, "Array shapes must match for element-wise bitwise operation."); return {}; }

            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = left_ptr->shape;
            result_ptr->data.reserve(left_ptr->data.size());

            for (size_t i = 0; i < left_ptr->data.size(); ++i) {
                long long l = static_cast<long long>(to_double(left_ptr->data[i]));
                long long r = static_cast<long long>(to_double(right_ptr->data[i]));
                result_ptr->data.push_back(static_cast<double>(op(l, r)));
            }
            return result_ptr;
        }
        // Case 2: Array vs Scalar
        else if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
            const auto& left_ptr = std::get<std::shared_ptr<Array>>(left);
            if (!left_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }
            long long r_scalar = static_cast<long long>(to_double(right));

            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = left_ptr->shape;
            result_ptr->data.reserve(left_ptr->data.size());

            for (const auto& elem : left_ptr->data) {
                long long l = static_cast<long long>(to_double(elem));
                result_ptr->data.push_back(static_cast<double>(op(l, r_scalar)));
            }
            return result_ptr;
        }
        // Case 3: Scalar vs Array
        else if (std::holds_alternative<std::shared_ptr<Array>>(right)) {
            const auto& right_ptr = std::get<std::shared_ptr<Array>>(right);
            if (!right_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }
            long long l_scalar = static_cast<long long>(to_double(left));

            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = right_ptr->shape;
            result_ptr->data.reserve(right_ptr->data.size());

            for (const auto& elem : right_ptr->data) {
                long long r = static_cast<long long>(to_double(elem));
                result_ptr->data.push_back(static_cast<double>(op(l_scalar, r)));
            }
            return result_ptr;
        }
        // Case 4: Scalar vs Scalar
        else {
            long long l = static_cast<long long>(to_double(left));
            long long r = static_cast<long long>(to_double(right));
            return static_cast<double>(op(l, r));
        }
    }
}

// LERP(start, end, alpha) -> number or array
// Performs linear interpolation.
BasicValue builtin_lerp(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "LERP requires 3 arguments: start, end, alpha");
        return 0.0;
    }

    const BasicValue& start_val = args[0];
    const BasicValue& end_val = args[1];
    double alpha = to_double(args[2]);

    auto lerp_op = [alpha](double a, double b) {
        return a + (b - a) * alpha;
        };

    bool start_is_array = std::holds_alternative<std::shared_ptr<Array>>(start_val);
    bool end_is_array = std::holds_alternative<std::shared_ptr<Array>>(end_val);

    // Case 1: Scalar LERP
    if (!start_is_array && !end_is_array) {
        return lerp_op(to_double(start_val), to_double(end_val));
    }

    // Case 2: Vectorized LERP (handles array/array, array/scalar, scalar/array)
    const auto& arr1 = start_is_array ? std::get<std::shared_ptr<Array>>(start_val) : nullptr;
    const auto& arr2 = end_is_array ? std::get<std::shared_ptr<Array>>(end_val) : nullptr;

    if (arr1 && arr2 && arr1->shape != arr2->shape) {
        Error::set(15, vm.runtime_current_line, "Array shapes must match for LERP.");
        return {};
    }

    // Determine the shape and size from whichever input is an array
    const auto& shape_ref = arr1 ? arr1->shape : arr2->shape;
    size_t total_size = arr1 ? arr1->data.size() : arr2->data.size();

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = shape_ref;
    result_ptr->data.reserve(total_size);

    for (size_t i = 0; i < total_size; ++i) {
        double s = start_is_array ? to_double(arr1->data[i]) : to_double(start_val);
        double e = end_is_array ? to_double(arr2->data[i]) : to_double(end_val);
        result_ptr->data.push_back(lerp_op(s, e));
    }
    return result_ptr;
}

#include <iostream> // Add this for std::cerr

// FORMAT$(format_string$, arg1, arg2, ...) -> string$
// Formats a string using C++20-style format specifiers.
BasicValue builtin_format_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. Argument Validation
    if (args.empty()) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return std::string("");
    }

    std::string format_string = to_string(args[0]);
    std::vector<BasicValue> format_args;
    if (args.size() > 1) {
        format_args.assign(args.begin() + 1, args.end());
    }

    std::stringstream result;
    size_t last_pos = 0;
    size_t auto_index = 0;

    // Manually iterate through the format string to handle each placeholder
    while (last_pos < format_string.length()) {
        size_t brace_pos = format_string.find('{', last_pos);

        if (brace_pos == std::string::npos) {
            result << format_string.substr(last_pos);
            break;
        }

        result << format_string.substr(last_pos, brace_pos - last_pos);

        if (brace_pos + 1 < format_string.length() && format_string[brace_pos + 1] == '{') {
            result << '{';
            last_pos = brace_pos + 2;
            continue;
        }

        size_t end_brace = format_string.find('}', brace_pos + 1);
        if (end_brace == std::string::npos) {
            result << format_string.substr(brace_pos);
            break;
        }

        std::string spec_content = format_string.substr(brace_pos + 1, end_brace - (brace_pos + 1));
        size_t arg_index;
        std::string format_specifier = "{}";

        size_t colon_pos = spec_content.find(':');
        std::string index_str = spec_content;

        if (colon_pos != std::string::npos) {
            index_str = spec_content.substr(0, colon_pos);
            format_specifier = "{:" + spec_content.substr(colon_pos + 1) + "}";
        }

        if (index_str.empty()) {
            arg_index = auto_index++;
        }
        else {
            try {
                arg_index = std::stoul(index_str);
            }
            catch (const std::exception&) {
                result << "{" << spec_content << "}";
                last_pos = end_brace + 1;
                continue;
            }
        }

        if (arg_index < format_args.size()) {
            const BasicValue& arg = format_args[arg_index];
            try {
                result << std::visit([&](auto&& value) -> std::string {
                    using T = std::decay_t<decltype(value)>;

                    auto do_format = [&](auto val_to_format) -> std::string {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
                        return std::vformat(format_specifier, std::make_format_args(val_to_format));
#else
                        return fmt::vformat(format_specifier, fmt::make_format_args(val_to_format));
#endif
                        };

                    if constexpr (std::is_same_v<T, double>) {
                        size_t spec_end_pos = format_specifier.length() - 1;
                        if (spec_end_pos > 2) {
                            char potential_type = format_specifier[spec_end_pos - 1];
                            if (std::string("dxXboB").find(potential_type) != std::string::npos) {
                                return do_format(static_cast<long long>(value));
                            }
                            if (potential_type == 'c') {
                                return do_format(static_cast<char>(static_cast<long long>(value)));
                            }
                        }
                        return do_format(value);
                    }
                    else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, long long> || std::is_same_v<T, std::string>) {
                        return do_format(value);
                    }
                    else {
                        return to_string(value);
                    }
                    }, arg);
            }
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
            catch (const std::format_error& e) {
#else
            catch (const fmt::format_error& e) {
#endif
                result << "{FORMAT ERROR: " << e.what() << "}";
            }
            }
        else {
            result << "{" << spec_content << "}";
        }
        last_pos = end_brace + 1;
        }
    return result.str();
}

// FRMV$(array, [format_string$]) -> string$
// Formats a 1D or 2D array into a string.
// If format_string$ is provided, it's used to format each row.
// Otherwise, it creates a right-aligned string matrix.
BasicValue builtin_frmv_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. Argument Validation
    if (args.empty() || args.size() > 2) {
        Error::set(8, vm.runtime_current_line, "FRMV$ requires 1 or 2 arguments: array, [format_string$]");
        return std::string("");
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to FRMV$ must be an array.");
        return std::string("");
    }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) {
        return std::string(""); // Nothing to format
    }

    // 2. Determine Shape
    size_t rows, cols;
    if (arr_ptr->shape.size() == 1) {
        rows = 1;
        cols = arr_ptr->shape[0];
    }
    else if (arr_ptr->shape.size() == 2) {
        rows = arr_ptr->shape[0];
        cols = arr_ptr->shape[1];
    }
    else {
        Error::set(15, vm.runtime_current_line, "FRMV$ only supports 1D or 2D arrays.");
        return std::string("");
    }

    if (cols == 0) {
        return std::string(""); // No columns to format
    }

    std::stringstream ss;

    // --- Handle optional format string ---
    if (args.size() == 2) {
        std::string format_string = to_string(args[1]);
        for (size_t r = 0; r < rows; ++r) {
            std::vector<BasicValue> format_args;
            format_args.push_back(format_string); // The format string itself is the first argument to builtin_format_str
            for (size_t c = 0; c < cols; ++c) {
                format_args.push_back(arr_ptr->data[r * cols + c]);
            }
            // Call the existing format function's logic for each row
            ss << std::get<std::string>(builtin_format_str(vm, format_args));
            if (r < rows - 1) {
                ss << "\n";
            }
        }
    }
    else {
        // --- Right-aligned grid ---
        // 3. Calculate Maximum Width for Each Column
        std::vector<size_t> col_widths(cols, 0);
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                std::string val_str = to_string(arr_ptr->data[r * cols + c]);
                if (val_str.length() > col_widths[c]) {
                    col_widths[c] = val_str.length();
                }
            }
        }

        // 4. Build the Formatted String
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                ss << std::right << std::setw(col_widths[c]) << to_string(arr_ptr->data[r * cols + c]);
                if (c < cols - 1) {
                    ss << " "; // Separator between columns
                }
            }
            if (r < rows - 1) {
                ss << "\n"; // Newline for the next row
            }
        }
    }

    return ss.str();
}

// --- Arithmetic Functions ---
// Helper to apply a scalar math function element-wise to an array or a scalar.
// It takes the input BasicValue and a function object that performs the scalar operation.
BasicValue apply_math_op(const BasicValue& input, const std::function<double(double)>& op) {
    // Case 1: Input is an Array (vector or matrix)
    if (const auto& arr_ptr = std::get_if<std::shared_ptr<Array>>(&input)) {
        if (!*arr_ptr) return {}; // Return empty on null pointer

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = (*arr_ptr)->shape; // Result has the same shape
        result_ptr->data.reserve((*arr_ptr)->data.size());

        // Apply the operation to each element
        for (const auto& val : (*arr_ptr)->data) {
            result_ptr->data.push_back(op(to_double(val)));
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar
    else {
        return op(to_double(input));
    }
}

// Applies a scalar math function that returns a LONG LONG element-wise.
BasicValue apply_integer_op(const BasicValue& input, const std::function<long long(double)>& op) {
    if (const auto& arr_ptr = std::get_if<std::shared_ptr<Array>>(&input)) {
        if (!*arr_ptr) return {};
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = (*arr_ptr)->shape;
        result_ptr->data.reserve((*arr_ptr)->data.size());
        for (const auto& val : (*arr_ptr)->data) {
            result_ptr->data.push_back(op(to_double(val)));
        }
        return result_ptr;
    }
    else {
        return op(to_double(input));
    }
}

// Applies a binary integer op (like bit shifts) element-wise.
BasicValue apply_binary_integer_op(const BasicValue& left, const BasicValue& right, const std::function<long long(long long, long long)>& op) {
    // Note: A full implementation would handle array/scalar combinations.
    // This simplified version assumes scalar inputs for SHL/SHR.
    long long l = to_int(left);
    long long r = to_int(right);
    return op(l, r);
}

// Applies an operation that preserves the input numeric type.
BasicValue apply_polymorphic_op(const BasicValue& input, const std::function<BasicValue(const BasicValue&)>& op) {
    if (const auto& arr_ptr = std::get_if<std::shared_ptr<Array>>(&input)) {
        if (!*arr_ptr) return {};
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = (*arr_ptr)->shape;
        result_ptr->data.reserve((*arr_ptr)->data.size());
        for (const auto& val : (*arr_ptr)->data) {
            result_ptr->data.push_back(op(val));
        }
        return result_ptr;
    }
    else {
        return op(input);
    }
}

// SIN(numeric_expression or array)
BasicValue builtin_sin(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::sin(d); });
}

// COS(numeric_expression or array)
BasicValue builtin_cos(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::cos(d); });
}

// TAN(numeric_expression or array)
BasicValue builtin_tan(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::tan(d); });
}

// SQR(numeric_expression or array) - Square Root
BasicValue builtin_sqr(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    // Apply sqrt, returning 0 for negative inputs to avoid domain errors
    return apply_math_op(args[0], [](double d) { return (d < 0) ? 0.0 : std::sqrt(d); });
}

// RND(numeric_expression or array)
BasicValue builtin_rnd(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }

    const BasicValue& input = args[0];

    // Case 1: Input is an Array. Return an array of the same shape with random numbers.
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(input);
        if (!arr_ptr) return {};

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = arr_ptr->shape;
        size_t total_size = arr_ptr->size();
        result_ptr->data.reserve(total_size);

        for (size_t i = 0; i < total_size; ++i) {
            result_ptr->data.push_back(static_cast<double>(rand()) / (RAND_MAX + 1.0));
        }
        return result_ptr;
    }
    // Case 2: Input is a scalar. Return a single random number.
    else {
        // Classic BASIC RND(1) behavior
        return static_cast<double>(rand()) / (RAND_MAX + 1.0);
    }
}

// LOG(numeric_expression or array) - Natural Logarithm
BasicValue builtin_log(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    // Use the helper, but add a domain check for non-positive numbers.
    return apply_math_op(args[0], [&](double d) {
        if (d <= 0) {
            Error::set(1, vm.runtime_current_line, "Argument to LOG must be positive.");
            return 0.0; // Return 0 on domain error
        }
        return std::log(d);
        });
}

// LOG10(numeric_expression or array) - Base-10 Logarithm
BasicValue builtin_log10(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    // Use the helper, adding the same domain check.
    return apply_math_op(args[0], [&](double d) {
        if (d <= 0) {
            Error::set(1, vm.runtime_current_line, "Argument to LOG10 must be positive.");
            return 0.0;
        }
        return std::log10(d);
        });
}

// FAC(numeric_expression or array)
BasicValue builtin_fac(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }

    // Lambda to perform the core factorial calculation
    auto factorial_op = [&](double n_double) {
        if (n_double != std::floor(n_double) || n_double < 0) return 0.0;
        long long n = static_cast<long long>(n_double);
        if (n > 170) return std::numeric_limits<double>::infinity();

        double result = 1.0;
        for (long long i = 2; i <= n; ++i) { result *= i; }
        return result;
        };

    const BasicValue& input = args[0];
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        // Vectorized case: no detailed error setting for performance
        return apply_math_op(input, factorial_op);
    }
    else {
        // Scalar case: keep the original detailed error checking
        double n_double = to_double(input);
        if (n_double != std::floor(n_double)) {
            Error::set(1, vm.runtime_current_line, "Argument to FAC must be an integer.");
            return 0.0;
        }
        long long n = static_cast<long long>(n_double);
        if (n < 0) {
            Error::set(1, vm.runtime_current_line, "Argument to FAC cannot be negative.");
            return 0.0;
        }
        if (n > 170) {
            Error::set(4, vm.runtime_current_line, "FAC argument too large, causes overflow.");
            return 0.0;
        }
        return factorial_op(n_double);
    }
}

// ABS(numeric_expression or array) -> Correctly returns integer or double
BasicValue builtin_abs(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    // Use the new polymorphic helper to preserve the numeric type
    return apply_polymorphic_op(args[0], [](const BasicValue& val) -> BasicValue {
        if (std::holds_alternative<long long>(val)) {
            return std::abs(std::get<long long>(val));
        }
        // Default to double for bools, doubles, etc.
        return std::abs(to_double(val));
        });
}

// INT(numeric_expression or array) -> Returns a long long
BasicValue builtin_int(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0LL; // Return long long literal
    }
    // Use the new integer helper
    return apply_integer_op(args[0], [](double d) { return static_cast<long long>(std::floor(d)); });
}

// CDBL(numeric_expression or array) -> Returns a double (Correct as is)
BasicValue builtin_cdbl(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return d; });
}

// FLOOR(numeric_expression or array) -> Returns a long long
BasicValue builtin_floor(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0LL;
    }
    return apply_integer_op(args[0], [](double d) { return static_cast<long long>(std::floor(d)); });
}

// CEIL(numeric_expression or array) -> Returns a long long
BasicValue builtin_ceil(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0LL;
    }
    return apply_integer_op(args[0], [](double d) { return static_cast<long long>(std::ceil(d)); });
}

// TRUNC(numeric_expression or array) -> Returns a long long
BasicValue builtin_trunc(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0LL;
    }
    return apply_integer_op(args[0], [](double d) { return static_cast<long long>(std::trunc(d)); });
}

// ROUND(number_or_array, decimals) -> number or array
BasicValue builtin_round(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "ROUND requires 2 arguments: value, decimals");
        return 0.0;
    }

    const BasicValue& input = args[0];
    int decimals = static_cast<int>(to_double(args[1]));
    double multiplier = std::pow(10.0, decimals);

    // This is the core rounding operation
    auto perform_round = [multiplier](double d) {
        return std::round(d * multiplier) / multiplier;
        };

    // Apply the operation element-wise if the input is an array
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        return apply_math_op(input, perform_round);
    }
    // Otherwise, apply it to the scalar value
    else {
        return perform_round(to_double(input));
    }
}

// CLAMP(value_or_array, min, max) -> number or array
BasicValue builtin_clamp(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "CLAMP requires 3 arguments: value, min, max");
        return 0.0;
    }

    const BasicValue& input = args[0];
    double min_val = to_double(args[1]);
    double max_val = to_double(args[2]);

    if (min_val > max_val) {
        Error::set(1, vm.runtime_current_line, "Min value cannot be greater than max value in CLAMP.");
        return 0.0;
    }

    // This is the core clamping operation
    auto perform_clamp = [min_val, max_val](double d) {
        return std::max(min_val, std::min(d, max_val));
        };

    // Apply the operation element-wise if the input is an array
    if (std::holds_alternative<std::shared_ptr<Array>>(input)) {
        return apply_math_op(input, perform_clamp);
    }
    // Otherwise, apply it to the scalar value
    else {
        return perform_clamp(to_double(input));
    }
}

// DISTANCE(point1_array, point2_array) -> number
// Calculates the Euclidean distance between two points.
BasicValue builtin_distance(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "DISTANCE requires 2 array arguments.");
        return 0.0;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Arguments to DISTANCE must be arrays.");
        return 0.0;
    }
    const auto& p1_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& p2_ptr = std::get<std::shared_ptr<Array>>(args[1]);

    if (!p1_ptr || !p2_ptr || p1_ptr->data.size() != p2_ptr->data.size()) {
        Error::set(15, vm.runtime_current_line, "Point arrays for DISTANCE must have the same number of elements.");
        return 0.0;
    }

    double sum_of_squares = 0.0;
    for (size_t i = 0; i < p1_ptr->data.size(); ++i) {
        double diff = to_double(p1_ptr->data[i]) - to_double(p2_ptr->data[i]);
        sum_of_squares += diff * diff;
    }

    return std::sqrt(sum_of_squares);
}

// SHL(value_or_array, bits_to_shift) -> Returns a long long or array
BasicValue builtin_shl(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SHL requires 2 arguments");
        return 0LL;
    }

    // Use apply_bitwise_op to handle Scalar/Scalar, Array/Scalar, etc.
    return apply_bitwise_op(args[0], args[1], [](long long val, long long bits) {
        // Fix: Clamp bits to 0..63 as per documentation and C++ safety
        if (bits < 0) bits = 0;
        if (bits > 63) bits = 63;

        return val << bits;
        });
}

// SHR(value_or_array, bits_to_shift) -> Returns a long long or array
BasicValue builtin_shr(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SHR requires 2 arguments");
        return 0LL;
    }

    return apply_bitwise_op(args[0], args[1], [](long long val, long long bits) {
        // Fix: Clamp bits to 0..63
        if (bits < 0) bits = 0;
        if (bits > 63) bits = 63;

        return val >> bits;
        });
}

// IIF(condition, value_if_true, value_if_false) -> value or array
// A vectorized version of the ternary operator.
BasicValue builtin_iif(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "IIF requires 3 arguments: condition, value_if_true, value_if_false");
        return {};
    }

    const BasicValue& cond_arg = args[0];
    const BasicValue& true_arg = args[1];
    const BasicValue& false_arg = args[2];

    bool cond_is_array = std::holds_alternative<std::shared_ptr<Array>>(cond_arg);

    // Case 1: The condition is a scalar value.
    // The result is one of the two other arguments, returned as is.
    if (!cond_is_array) {
        return to_bool(cond_arg) ? true_arg : false_arg;
    }

    // Case 2: The condition is an array.
    // The result will be a new array constructed element by element.
    const auto& cond_arr = std::get<std::shared_ptr<Array>>(cond_arg);
    if (!cond_arr) {
        Error::set(15, vm.runtime_current_line, "Condition array for IIF cannot be null.");
        return {};
    }

    bool true_is_array = std::holds_alternative<std::shared_ptr<Array>>(true_arg);
    bool false_is_array = std::holds_alternative<std::shared_ptr<Array>>(false_arg);

    const auto& true_arr = true_is_array ? std::get<std::shared_ptr<Array>>(true_arg) : nullptr;
    const auto& false_arr = false_is_array ? std::get<std::shared_ptr<Array>>(false_arg) : nullptr;

    // Validate shapes if the other arguments are also arrays
    if (true_arr && true_arr->shape != cond_arr->shape) {
        Error::set(15, vm.runtime_current_line, "Array shapes must match for IIF (condition and true_value).");
        return {};
    }
    if (false_arr && false_arr->shape != cond_arr->shape) {
        Error::set(15, vm.runtime_current_line, "Array shapes must match for IIF (condition and false_value).");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = cond_arr->shape;
    result_ptr->data.reserve(cond_arr->data.size());

    for (size_t i = 0; i < cond_arr->data.size(); ++i) {
        if (to_bool(cond_arr->data[i])) {
            // If true_arg is an array, take the corresponding element. Otherwise, broadcast the scalar.
            result_ptr->data.push_back(true_arr ? true_arr->data[i] : true_arg);
        }
        else {
            // If false_arg is an array, take the corresponding element. Otherwise, broadcast the scalar.
            result_ptr->data.push_back(false_arr ? false_arr->data[i] : false_arg);
        }
    }

    return result_ptr;
}

// --- Date and Time Functions ---
// 
// TICK() -> returns milliseconds since the program started
BasicValue builtin_tick(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // This function takes no arguments
    if (!args.empty()) {
        // Optional: Set an error for "Wrong number of arguments"
        return 0.0;
    }

    // Get the current time point from a steady (monotonic) clock
    auto now = std::chrono::steady_clock::now();

    // Calculate the duration since the clock's epoch (usually program start)
    // and return it as a double representing milliseconds.
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
        );
}
// NOW() -> returns a DateTime object for the current moment
BasicValue builtin_now(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }
    return DateTime{}; // Creates a new DateTime object, which defaults to now
}

// DATE$() -> returns the current date as a string "YYYY-MM-DD"
BasicValue builtin_date_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return std::string("");
    }
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
#pragma warning(suppress : 4996) // Suppress warning for std::localtime
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
    return ss.str();
}

// TIME$() -> returns the current time as a string "HH:MM:SS"
BasicValue builtin_time_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return std::string("");
    }
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
#pragma warning(suppress : 4996) // Suppress warning for std::localtime
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    return ss.str();
}

// Helper to safely add time units to a time_t
time_t add_to_tm(time_t base_time, int years, int months, int days, int hours, int minutes, int seconds) {
    struct tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &base_time); // Use safe version
#else
    localtime_r(&base_time, &timeinfo);
#endif    

    timeinfo.tm_year += years;
    timeinfo.tm_mon += months;
    timeinfo.tm_mday += days;
    timeinfo.tm_hour += hours;
    timeinfo.tm_min += minutes;
    timeinfo.tm_sec += seconds;

    return mktime(&timeinfo); // mktime normalizes the date/time components
}

// DATEADD(part$, number, dateValue_or_array)
BasicValue builtin_dateadd(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    std::string part = to_upper(to_string(args[0]));
    int number = static_cast<int>(to_double(args[1]));
    const BasicValue& date_input = args[2];

    // This helper lambda now uses modern chrono for all calculations.
    auto perform_date_add = [&](const DateTime& start_date) -> BasicValue {
        auto tp = start_date.time_point;
        std::chrono::system_clock::time_point new_tp;

        // Simple duration-based arithmetic
        if (part == "S") {
            new_tp = tp + std::chrono::seconds{ number };
        }
        else if (part == "N") { // "N" for minutes in classic BASIC
            new_tp = tp + std::chrono::minutes{ number };
        }
        else if (part == "H") {
            new_tp = tp + std::chrono::hours{ number };
        }
        // Calendar-based arithmetic
        else if (part == "D" || part == "M" || part == "YYYY") {
#ifdef _WIN32            
            // To correctly add calendar months/years, we must work in local time.
            const std::chrono::time_zone* current_tz;
            try {
                current_tz = std::chrono::current_zone();
            }
            catch (const std::runtime_error&) {
                current_tz = std::chrono::locate_zone("UTC");
            }

            // Decompose the time point into date and time-of-day parts in the local zone
            auto local_time = current_tz->to_local(tp);
            auto local_days = std::chrono::floor<std::chrono::days>(local_time);
            auto time_of_day = local_time - local_days;

            std::chrono::year_month_day ymd{ local_days };

            if (part == "D") {
                // Adding days is simple calendar arithmetic
                new_tp = std::chrono::sys_days{ ymd } + std::chrono::days{ number } + time_of_day;
                // Note: The above is UTC, which is fine as that's what we store.
            }
            else { // Month or Year
                if (part == "M") {
                    ymd += std::chrono::months{ number };
                }
                else { // YYYY
                    ymd += std::chrono::years{ number };
                }

                // If a date is invalid (e.g., adding 1 month to Jan 31 -> Feb 31),
                // clamp to the last valid day of that new month.
                if (!ymd.ok()) {
                    ymd = ymd.year() / ymd.month() / std::chrono::last;
                }

                // Re-assemble the date and time parts and convert back to system time (UTC)
                new_tp = current_tz->to_sys(std::chrono::local_days{ ymd } + time_of_day);
            }
#else
                // Convert time_point to time_t
                std::time_t tt = std::chrono::system_clock::to_time_t(tp);
                std::tm timeinfo;
                localtime_r(&tt, &timeinfo);

                if (part == "D") {
                    timeinfo.tm_mday += number;
                } else if (part == "M") {
                    timeinfo.tm_mon += number;
                } else { // "YYYY"
                    timeinfo.tm_year += number;
                }

                // Normalize and convert back to time_point
                std::time_t new_tt = mktime(&timeinfo);
                new_tp = std::chrono::system_clock::from_time_t(new_tt);
#endif            
        }
        else {
            Error::set(1, vm.runtime_current_line, "Invalid interval for DATEADD. Use YYYY, M, D, H, N, or S.");
            return false;
        }

        return DateTime{ new_tp };
        };

    // --- Vectorized Logic (this part remains the same) ---

    // Case 1: The third argument is an Array.
    if (std::holds_alternative<std::shared_ptr<Array>>(date_input)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(date_input);
        if (!arr_ptr) return {};

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = arr_ptr->shape;
        result_ptr->data.reserve(arr_ptr->data.size());

        for (const auto& val : arr_ptr->data) {
            if (!std::holds_alternative<DateTime>(val)) {
                Error::set(15, vm.runtime_current_line, "All elements in array for DATEADD must be DateTime objects.");
                return {};
            }
            const auto& dt = std::get<DateTime>(val);
            BasicValue new_date = perform_date_add(dt);
            if (Error::get() != 0) return {}; // Propagate error from lambda
            result_ptr->data.push_back(new_date);
        }
        return result_ptr;
    }
    // Case 2: The third argument is a scalar.
    else {
        if (!std::holds_alternative<DateTime>(date_input)) {
            Error::set(15, vm.runtime_current_line, "Third argument to DATEADD must be a DateTime object or an array of them.");
            return false;
        }
        const auto& start_date = std::get<DateTime>(date_input);
        return perform_date_add(start_date);
    }
}

// DATEDIFF(part$, date1, date2) -> number or array
// Calculates the difference between two dates in the specified unit. Now supports vectorized operations.
BasicValue builtin_datediff(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "DATEDIFF requires 3 arguments: part$, date1, date2");
        return 0.0;
    }

    std::string part = to_upper(to_string(args[0]));
    const BasicValue& date1_arg = args[1];
    const BasicValue& date2_arg = args[2];

    // Helper lambda to calculate the difference between two single DateTime objects.
    auto calculate_diff = [&](const DateTime& d1, const DateTime& d2) -> BasicValue {
        auto duration = d2.time_point - d1.time_point;
        if (part == "D") {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24.0);
        }
        else if (part == "H") {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::hours>(duration).count());
        }
        else if (part == "N") {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::minutes>(duration).count());
        }
        else if (part == "S") {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(duration).count());
        }
        Error::set(1, vm.runtime_current_line, "Invalid interval for DATEDIFF. Use D, H, N, or S.");
        return 0.0;
        };

    // Use std::visit to handle all combinations of scalar and array inputs.
    return std::visit([&](auto&& arg1, auto&& arg2) -> BasicValue {
        using T1 = std::decay_t<decltype(arg1)>;
        using T2 = std::decay_t<decltype(arg2)>;

        // Case 1: Array vs Array
        if constexpr (std::is_same_v<T1, std::shared_ptr<Array>> && std::is_same_v<T2, std::shared_ptr<Array>>) {
            if (!arg1 || !arg2) { Error::set(15, vm.runtime_current_line, "Input arrays cannot be null."); return {}; }
            if (arg1->shape != arg2->shape) { Error::set(15, vm.runtime_current_line, "Array shapes must match for element-wise DATEDIFF."); return {}; }

            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = arg1->shape;
            result_ptr->data.reserve(arg1->data.size());

            for (size_t i = 0; i < arg1->data.size(); ++i) {
                if (!std::holds_alternative<DateTime>(arg1->data[i]) || !std::holds_alternative<DateTime>(arg2->data[i])) {
                    Error::set(15, vm.runtime_current_line, "All array elements must be DateTime objects."); return {};
                }
                result_ptr->data.push_back(calculate_diff(std::get<DateTime>(arg1->data[i]), std::get<DateTime>(arg2->data[i])));
                if (Error::get() != 0) return {};
            }
            return result_ptr;
        }
        // Case 2: Scalar vs Array
        else if constexpr (std::is_same_v<T1, DateTime> && std::is_same_v<T2, std::shared_ptr<Array>>) {
            if (!arg2) { Error::set(15, vm.runtime_current_line, "Input array cannot be null."); return {}; }
            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = arg2->shape;
            result_ptr->data.reserve(arg2->data.size());

            for (const auto& elem : arg2->data) {
                if (!std::holds_alternative<DateTime>(elem)) { Error::set(15, vm.runtime_current_line, "All array elements must be DateTime objects."); return {}; }
                result_ptr->data.push_back(calculate_diff(arg1, std::get<DateTime>(elem)));
                if (Error::get() != 0) return {};
            }
            return result_ptr;
        }
        // Case 3: Array vs Scalar
        else if constexpr (std::is_same_v<T1, std::shared_ptr<Array>> && std::is_same_v<T2, DateTime>) {
            if (!arg1) { Error::set(15, vm.runtime_current_line, "Input array cannot be null."); return {}; }
            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = arg1->shape;
            result_ptr->data.reserve(arg1->data.size());

            for (const auto& elem : arg1->data) {
                if (!std::holds_alternative<DateTime>(elem)) { Error::set(15, vm.runtime_current_line, "All array elements must be DateTime objects."); return {}; }
                result_ptr->data.push_back(calculate_diff(std::get<DateTime>(elem), arg2));
                if (Error::get() != 0) return {};
            }
            return result_ptr;
        }
        // Case 4: Scalar vs Scalar
        else if constexpr (std::is_same_v<T1, DateTime> && std::is_same_v<T2, DateTime>) {
            return calculate_diff(arg1, arg2);
        }
        // Default: Invalid type combination
        else {
            Error::set(15, vm.runtime_current_line, "Invalid argument types for DATEDIFF. Must be DateTime objects or arrays of them.");
            return {};
        }
        }, date1_arg, date2_arg);
}

// CVDATE(string_expression_or_array) -> DateTime, array, or boolean false on error
// Parses a string like "YYYY-MM-DD" into a DateTime object.
BasicValue builtin_cvdate(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    auto cvdate_op = [](NeReLaBasic& vm_ref, const BasicValue& v) -> BasicValue {
        std::string date_str = to_string(v);

        int p_year, p_month, p_day;
        char dash1, dash2;
        std::stringstream ss(date_str);

        // Manually parse the components from the string
        ss >> p_year >> dash1 >> p_month >> dash2 >> p_day;

        if (ss.fail() || ss.peek() != EOF || dash1 != '-' || dash2 != '-') {
            Error::set(15, vm_ref.runtime_current_line, "Invalid date format. Expected 'YYYY-MM-DD'.");
            return false;
        }
#ifdef _WIN32
        // Use C++20's std::chrono::year_month_day for robust validation and conversion
        std::chrono::year_month_day ymd{
            std::chrono::year{p_year},
            std::chrono::month{(unsigned int)p_month},
            std::chrono::day{(unsigned int)p_day}
        };

        // The .ok() method checks if the date is valid (e.g., not February 30th)
        if (!ymd.ok()) {
            Error::set(15, vm_ref.runtime_current_line, "Invalid date components (e.g., month > 12 or invalid day).");
            return false;
        }

        // std::chrono::sys_days is a time_point that represents a whole day.
        // This will correctly convert to the time_point in your DateTime struct.
        auto time_point = std::chrono::sys_days{ ymd };
#else
        std::tm timeinfo = {};
        timeinfo.tm_year = p_year - 1900;
        timeinfo.tm_mon = p_month - 1;
        timeinfo.tm_mday = p_day;
        timeinfo.tm_hour = 0;
        timeinfo.tm_min = 0;
        timeinfo.tm_sec = 0;
        std::time_t tt = mktime(&timeinfo);
        if (tt == -1) {
            Error::set(15, vm_ref.runtime_current_line, "Invalid date components (e.g., month > 12 or invalid day).");
            return false;
        }
        auto time_point = std::chrono::system_clock::from_time_t(tt);
#endif
        return DateTime{ time_point };
    };

    return apply_elementwise_op(vm, args[0], cvdate_op);
}


// --- Procedures ---
BasicValue builtin_cls(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // This function is now context-aware.
#ifdef SDL3
    if (vm.graphics_system.is_initialized) {
        // If graphics are on, CLS clears the graphics window.
        Uint8 r = 0, g = 0, b = 0;
        if (args.size() == 3) {
            r = static_cast<Uint8>(to_double(args[0]));
            g = static_cast<Uint8>(to_double(args[1]));
            b = static_cast<Uint8>(to_double(args[2]));
        }
        vm.graphics_system.clear_screen(r, g, b);
        //vm.graphics_system.update_screen(); // CLS should be immediate
    }
    else {
        // Otherwise, it clears the text console.
        TextIO::clearScreen();
    }
#else
    TextIO::clearScreen();
#endif
    return false;
}

BasicValue builtin_locate(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    int row = static_cast<int>(to_double(args[0]));
    int col = static_cast<int>(to_double(args[1]));

    TextIO::locate(row, col);

    return false; // Procedures return a dummy value.
}

BasicValue builtin_getx(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "GETX does not accept arguments.");
        return false;
    }

    return to_double(TextIO::getCursorX());
}

BasicValue builtin_gety(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "GETY does not accept arguments.");
        return false;
    }
    return to_double(TextIO::getCursorY());
}

// SLEEP milliseconds
BasicValue builtin_sleep(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }
    int milliseconds = static_cast<int>(to_double(args[0]));
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
    return false;
}

// YIELD
// (Emscripten only) Pauses execution and yields to the browser event loop for one frame.
BasicValue builtin_yield(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "YIELD does not accept arguments.");
        return false;
    }

#ifdef __EMSCRIPTEN__
    vm.yielded_for_frame = true;
#else
    // On non-web platforms, this command can be a no-op or print a warning.
    // A short sleep is a reasonable behavior to avoid tight loops if used incorrectly.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif

    return false; // This is a procedure.
}


// CURSOR state (0 for off, 1 for on)
BasicValue builtin_cursor(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    bool state = to_bool(args[0]); // to_bool handles 0/1 conversion nicely
    TextIO::setCursor(state);
    return false;
}

BasicValue builtin_option(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }

    std::string option_str = to_upper(to_string(args[0])); // Convert argument to uppercase string

    if (option_str == "NOPAUSE") {
        vm.nopause_active = true; // Set the flag in the VM
        TextIO::print("OPTION NOPAUSE is active. Break/Pause disabled."); TextIO::nl(); // Optional feedback
    }
    else if (option_str == "PAUSE") { // Optional: allow turning pause back on
        vm.nopause_active = false;
        TextIO::print("OPTION PAUSE is active. Break/Pause enabled."); TextIO::nl();
    }
    else if (option_str == "EXPLICIT" || option_str == "EXPLICITON") {
        vm.option_explicit = true;
    }
    else if (option_str == "NOEXPLICIT" || option_str == "EXPLICITOFF") {
        vm.option_explicit = false;
    }
    // Add more else if blocks here for future options, e.g.:
    // else if (option_str == "GRAPHICSON") {
    //     // vm.graphics_enabled = true;
    // }
    // else if (option_str == "FASTIO") {
    //     // vm.fast_io_mode = true;
    // }
    else {
        Error::set(1, vm.runtime_current_line); // Syntax Error: Unknown OPTION
    }

    return false; // Procedures return a dummy value
}

// GETENV$(variable_name$) -> string$
// Reads the value of a system environment variable.
BasicValue builtin_getenv_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return std::string("");
    }

    std::string var_name = to_string(args[0]);
    if (var_name.empty()) {
        return std::string("");
    }

#ifdef _WIN32
    // --- Windows-specific, secure version ---
    char* buffer = nullptr;
    size_t size = 0;

    // _dupenv_s allocates memory for the buffer and must be freed later.
    errno_t err = _dupenv_s(&buffer, &size, var_name.c_str());

    // Check if it succeeded and the buffer is valid
    if (err == 0 && buffer != nullptr) {
        std::string value(buffer);
        free(buffer); // IMPORTANT: Free the memory allocated by _dupenv_s
        return value;
    }
    else {
        return std::string(""); // Return empty string if not found or on error
    }

#else
    // --- Standard C++ version for other platforms (Linux, macOS) ---
    char* value = std::getenv(var_name.c_str());
    if (value == nullptr) {
        return std::string(""); // Not found
    }
    else {
        return std::string(value); // Found
    }
#endif
}

// THROW [error_data]
// Manually raises an error that can be caught by a TRY...CATCH block.
BasicValue builtin_throw(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() > 1) {
        Error::set(8, vm.runtime_current_line, "THROW accepts zero or one argument.");
        return false;
    }

    BasicValue error_data = "User-defined error"; // Default message if no argument is provided.

    if (args.size() == 1) {
        error_data = args[0];
    }

    // We'll use a specific error code for all user-thrown exceptions.
    const int USER_ERROR_CODE = 1000;

    // Calling Error::set triggers the interpreter's entire error handling mechanism.
    // It will find the nearest CATCH block and transfer control, just like a built-in error.
    Error::set(USER_ERROR_CODE, vm.runtime_current_line, to_string(error_data));

    return false; // This is a procedure, so the return value is ignored.
}

// SAVEWS "basename" - Saves source code and global variables to "basename.jsws".
BasicValue builtin_savews(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "SAVEWS requires exactly one string argument for the filename.");
        return false;
    }
    // Automatically append the .jsws extension
    std::string filename = to_string(args[0]) + ".jsws";

    try {
        nlohmann::json j_workspace;

        // 1. Save the source code currently in memory
        j_workspace["source_code"] = vm.source_lines;

        // 2. Save the global variables
        auto variables_map = std::make_shared<Map>();
        // Manually copy elements to handle potential map type mismatch (e.g., map vs unordered_map)
        for (const auto& pair : vm.variables) {
            variables_map->data[pair.first] = pair.second;
        }
        j_workspace["variables"] = basic_to_json_value_for_serialize(variables_map);

        std::ofstream outfile(filename);
        if (!outfile) {
            Error::set(12, vm.runtime_current_line, "Failed to open file for writing.");
            return false;
        }

        outfile << j_workspace.dump(4); // Pretty-print JSON
        TextIO::print("Workspace saved to " + filename); TextIO::nl();

    }
    catch (const std::exception& e) {
        Error::set(1, vm.runtime_current_line, "Failed to serialize workspace: " + std::string(e.what()));
    }
    return false; // It's a procedure
}

// LOADWS "basename" - Loads source and variables from "basename.jsws".
BasicValue builtin_loadws(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "LOADWS requires exactly one string argument for the filename.");
        return false;
    }
    // Automatically append the .jsws extension
    std::string filename = to_string(args[0]) + ".jsws";

    std::ifstream infile(filename);
    if (!infile) {
        Error::set(6, vm.runtime_current_line, "Workspace file not found: " + filename);
        return false;
    }

    try {
        nlohmann::json j_workspace;
        infile >> j_workspace;

        if (!j_workspace.is_object() || !j_workspace.contains("source_code") || !j_workspace.contains("variables")) {
            Error::set(1, vm.runtime_current_line, "Invalid or corrupt workspace file format.");
            return false;
        }

        // 1. Load the source code
        vm.source_lines = j_workspace["source_code"].get<std::vector<std::string>>();

        // 2. Load the variables
        BasicValue deserialized_vars = json_to_basic_value(j_workspace["variables"]);
        if (std::holds_alternative<std::shared_ptr<Map>>(deserialized_vars)) {
            const auto& map_ptr = std::get<std::shared_ptr<Map>>(deserialized_vars);
            if (map_ptr) {
                vm.variables.clear();
                // Manually copy elements to handle potential map type mismatch
                for (const auto& pair : map_ptr->data) {
                    vm.variables[pair.first] = pair.second;
                }
            }
        }
        else {
            Error::set(1, vm.runtime_current_line, "Invalid variables format in workspace file.");
            return false;
        }

        TextIO::print("Workspace loaded from " + filename); TextIO::nl();
        TextIO::print("Source code has been loaded into memory. Type LIST to view."); TextIO::nl();

    }
    catch (const nlohmann::json::parse_error& e) {
        Error::set(1, vm.runtime_current_line, "Invalid JSON in workspace file: " + std::string(e.what()));
    }
    catch (const std::exception& e) {
        Error::set(1, vm.runtime_current_line, "Failed to load workspace: " + std::string(e.what()));
    }
    return false; // It's a procedure
}

// NEW
// Empties the source code, compiled p-code, and user-defined function tables.
BasicValue builtin_new(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "NEW does not accept arguments.");
        return false;
    }

    // 1. Clear source code from memory
    vm.source_lines.clear();

    // 2. Clear the compiled bytecode for the main program
    vm.program_p_code.clear();

    // 3. Clear all user-defined types (UDTs)
    vm.user_defined_types.clear();

    // 4. Remove all user-defined functions from the main function table,
    //    but keep the built-in C++ functions.
    for (auto it = vm.main_function_table.begin(); it != vm.main_function_table.end(); ) {
        // A user-defined function has neither a native C++ implementation
        // nor a native DLL implementation.
        if (it->second.native_impl == nullptr && it->second.native_dll_impl == nullptr) {
            it = vm.main_function_table.erase(it);
        }
        else {
            ++it;
        }
    }

    return false; // This is a procedure
}

// CLEARWS
// Empties source code, p-code, and all global variables.
BasicValue builtin_clearws(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "CLEARWS does not accept arguments.");
        return false;
    }

    // 1. Clear source code
    vm.source_lines.clear();

    // 2. Clear compiled p-code
    vm.program_p_code.clear();

    // 3. Clear all user-defined types (UDTs)
    vm.user_defined_types.clear();

    // 4. Remove user-defined functions (same as NEW)
    for (auto it = vm.main_function_table.begin(); it != vm.main_function_table.end(); ) {
        if (it->second.native_impl == nullptr && it->second.native_dll_impl == nullptr) {
            it = vm.main_function_table.erase(it);
        }
        else {
            ++it;
        }
    }

    // 5. Clear all global variables
    vm.variables.clear();

    TextIO::print("Workspace cleared."); TextIO::nl();
    return false; // This is a procedure
}

// UNREACT(name$)
//   name$ can be a plain var (e.g., "A"), a dotted member (e.g., "PLAYER.X"),
//   or special "ALL"/"*" to clear the entire reactive graph.
static BasicValue builtin_unreact(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "UNREACT requires exactly one string argument.");
        return false; // procedure
    }

    std::string target = to_upper(to_string(args[0]));
    if (target.empty()) return false;

    // Clear entire graph: UNREACT "ALL" or UNREACT "*"
    if (target == "ALL" || target == "*") {
        vm.reactive_graph.clear();
        // NOTE: we intentionally do NOT clear vm.react_variables here so sources
        // still trigger propagation in case new reactive nodes are created later.
        return false;
    }

    auto it = vm.reactive_graph.find(target);
    if (it == vm.reactive_graph.end()) {
        TextIO::print("UNREACT: no reactive expression for '" + target + "'."); TextIO::nl();
        return false;
    }

    // 1) Remove back-links from each dependency's dependents[]
    for (const auto& dep : it->second.dependencies) {
        auto dIt = vm.reactive_graph.find(dep);
        if (dIt != vm.reactive_graph.end()) {
            auto& vec = dIt->second.dependents;
            vec.erase(std::remove(vec.begin(), vec.end(), target), vec.end());
        }
    }

    // 2) Remove forward-links from each dependent's dependencies[]
    for (const auto& depd : it->second.dependents) {
        auto ddIt = vm.reactive_graph.find(depd);
        if (ddIt != vm.reactive_graph.end()) {
            auto& vec = ddIt->second.dependencies;
            vec.erase(std::remove(vec.begin(), vec.end(), target), vec.end());
        }
    }

    // 3) Remove the node itself
    vm.reactive_graph.erase(it);

    return false;
}

// --- Filesystem ---
// 

// DIR$(wildcard$, [extended_info_bool]) -> array
// Returns an array of strings (or a 2D matrix) containing filenames that match the wildcard pattern.
BasicValue builtin_dir_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1 || args.size() > 2) {
        Error::set(8, vm.runtime_current_line, "DIR$ requires 1 or 2 arguments: wildcard$ [, extended_info]");
        return {};
    }

    bool extended = false;
    if (args.size() == 2) {
        extended = to_bool(args[1]);
    }

    auto result_ptr = std::make_shared<Array>();

    // Default return is empty array (shape {0} or {0,5})
    if (extended) {
        result_ptr->shape = { 0, 5 };
    }
    else {
        result_ptr->shape = { 0 };
    }

    std::string full_pattern_str = to_string(args[0]);
    fs::path pattern_path(full_pattern_str);

    // Separate the path from the wildcard filename
    fs::path target_dir = pattern_path.has_parent_path() ? pattern_path.parent_path() : ".";
    std::string wildcard = pattern_path.has_filename() ? pattern_path.filename().string() : "*";

    // Handle case where the pattern is a specific file that exists (no wildcards)
    // This allows DIR$("C:\file.txt") to work even without *
    if (wildcard.find('*') == std::string::npos && wildcard.find('?') == std::string::npos) {
        if (fs::exists(pattern_path) && !fs::is_directory(pattern_path)) {
            target_dir = pattern_path.parent_path();
            if (target_dir.empty()) target_dir = ".";
        }
    }

    if (!fs::exists(target_dir) || !fs::is_directory(target_dir)) {
        // Return the empty array (matches user expectation of [])
        return result_ptr;
    }

    try {
        std::regex pattern(wildcard_to_regex(wildcard), std::regex::icase);

        for (const auto& entry : fs::directory_iterator(target_dir)) {
            std::string filename_str = entry.path().filename().string();

            // Skip if it doesn't match
            if (!std::regex_match(filename_str, pattern)) continue;

            if (!extended) {
                // Classic Mode: Just filenames
                result_ptr->data.push_back(filename_str);
            }
            else {
                // Extended Mode: Matrix [Name, Size, Type, Date, Attributes]

                // 1. Name
                result_ptr->data.push_back(filename_str);

                // 2. Size (Bytes)
                double fsize = 0.0;
                if (entry.is_regular_file()) {
                    try { fsize = static_cast<double>(entry.file_size()); }
                    catch (...) {}
                }
                result_ptr->data.push_back(fsize);

                // 3. Type
                std::string type = "FILE";
                if (entry.is_directory()) type = "DIR";
                else if (entry.is_symlink()) type = "LINK";
                result_ptr->data.push_back(type);

                // 4. Date (YYYY-MM-DD HH:MM:SS)
                std::string date_str = "";
                try {
                    auto ftime = entry.last_write_time();
                    // Portable-ish C++17 conversion to system time
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );
                    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
                    std::tm tm_struct;
#if defined(_WIN32)
                    localtime_s(&tm_struct, &tt);
#else
                    localtime_r(&tt, &tm_struct);
#endif
                    std::stringstream ss;
                    ss << std::put_time(&tm_struct, "%Y-%m-%d %H:%M:%S");
                    date_str = ss.str();
                }
                catch (...) {}
                result_ptr->data.push_back(date_str);

                // 5. Attributes (R=Read, W=Write, X=Exec)
                std::string attrs = "";
                try {
                    auto p = entry.status().permissions();
                    if ((p & fs::perms::owner_read) != fs::perms::none) attrs += "R";
                    if ((p & fs::perms::owner_write) != fs::perms::none) attrs += "W";
                    if ((p & fs::perms::owner_exec) != fs::perms::none) attrs += "X";
                }
                catch (...) {}
                result_ptr->data.push_back(attrs);
            }
        }

        if (extended) {
            // Update shape to Nx5
            size_t rows = result_ptr->data.size() / 5;
            result_ptr->shape = { rows, 5 };
        }
        else {
            // Update shape to N
            result_ptr->shape = { result_ptr->data.size() };
        }
    }
    catch (const std::regex_error& e) {
        Error::set(1, vm.runtime_current_line, "Invalid wildcard pattern: " + std::string(e.what()));
        return {};
    }
    catch (const fs::filesystem_error& e) {
        Error::set(12, vm.runtime_current_line, "Filesystem error: " + std::string(e.what()));
        return {};
    }

    return result_ptr;
}
// DIR [path_string]
BasicValue builtin_dir(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    try {
        fs::path target_path("."); // Default to current directory

        // On non-Windows platforms (like Emscripten), *.* only matches files with dots.
        // We default to "*" to list everything.
        std::string wildcard = "*";

        if (!args.empty()) {
            fs::path full_arg_path(to_string(args[0]));
            if (full_arg_path.has_filename() && full_arg_path.filename().string().find_first_of("*?") != std::string::npos) {
                wildcard = full_arg_path.filename().string();
                // Handle case where parent path is empty (meaning current dir)
                target_path = full_arg_path.has_parent_path() ? full_arg_path.parent_path() : ".";
            }
            else {
                target_path = full_arg_path;
            }
        }

        if (!fs::exists(target_path) || !fs::is_directory(target_path)) {
            TextIO::print("Directory not found: " + target_path.string()); TextIO::nl();
            return false;
        }

        // Create the regex object from our wildcard pattern
        std::regex pattern(wildcard_to_regex(wildcard), std::regex::icase);

        for (const auto& entry : fs::directory_iterator(target_path)) {
            std::string filename_str = entry.path().filename().string();
            // Check if the filename matches our regex pattern
            if (filename_str.length() > 0) {
                if (std::regex_match(filename_str, pattern)) {
                    std::string size_str;

                    if (entry.is_directory()) {
                        size_str = "<DIR>";
                    }
                    else if (entry.is_regular_file()) {
                        // FIX: Only attempt file_size on regular files.
                        // Calling file_size on devices (like /dev/tty) causes a hang in Emscripten.
                        try {
                            size_str = std::to_string(fs::file_size(entry));
                        }
                        catch (const fs::filesystem_error&) {
                            size_str = "???";
                        }
                    }
                    else {
                        // Symlinks, Character Devices, Sockets, etc.
                        size_str = "<SYS>";
                    }

                    TextIO::print(filename_str);

                    // Simple padding logic
                    int padding_needed = 25 - static_cast<int>(filename_str.length());
                    if (padding_needed > 0) {
                        for (int i = 0; i < padding_needed; ++i) {
                            TextIO::print(" ");
                        }
                    }
                    TextIO::print(size_str); TextIO::nl();
                }
            }
        }
    }
    catch (const std::regex_error& e) {
        TextIO::print("Invalid wildcard pattern: " + std::string(e.what())); TextIO::nl();
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error accessing directory: " + std::string(e.what())); TextIO::nl();
    }

    return false; // Procedures return a dummy value
}
// CD path_string
BasicValue builtin_cd(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, 0, "Wrong number of arguments. Usage: CD \"path\".");
        return false;
    }

    std::string path_str = to_string(args[0]);
    try {
        fs::current_path(path_str); // This function changes the current working directory
        TextIO::print("Current directory is now: " + fs::current_path().string()); TextIO::nl();
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error changing directory: " + std::string(e.what())); TextIO::nl();
    }
    return false;
}

// PWD
BasicValue builtin_pwd(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    try {
        TextIO::print(fs::current_path().string()); TextIO::nl();
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error getting current directory: " + std::string(e.what())); TextIO::nl();
    }
    return false;
}

// MKDIR path_string
BasicValue builtin_mkdir(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, 0); // Wrong number of arguments
        return false;
    }
    std::string path_str = to_string(args[0]);
    try {
        if (fs::create_directory(path_str)) {
            TextIO::print("Directory created: " + path_str); TextIO::nl();
        }
        else {
            TextIO::print("Directory already exists or error."); TextIO::nl();
        }
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error creating directory: " + std::string(e.what())); TextIO::nl();
    }
    return false;
}

// --- Path Utility Functions ---

// PATH.JOIN$(part1$, part2$, ...) -> string$
// Joins multiple path components using the OS-specific separator.
BasicValue builtin_path_join(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) {
        return std::string("");
    }

    fs::path result_path;

    // First argument is the base
    result_path = to_string(args[0]);

    // Append subsequent arguments using the /= operator which handles separators correctly
    for (size_t i = 1; i < args.size(); ++i) {
        result_path /= to_string(args[i]);
    }

    return result_path.string();
}

// PATH.EXT$(filename$) -> string$
// Returns the file extension (including the dot).
BasicValue builtin_path_ext(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "PATH.EXT$ requires exactly one argument.");
        return std::string("");
    }

    std::string path_str = to_string(args[0]);
    if (path_str.empty()) return std::string("");

    fs::path p(path_str);
    if (p.has_extension()) {
        return p.extension().string();
    }
    return std::string("");
}

// PATH.BASENAME$(filename$) -> string$
// Returns the filename component from a path (e.g. "file.txt" from "/path/to/file.txt").
BasicValue builtin_path_basename(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "PATH.BASENAME$ requires exactly one argument.");
        return std::string("");
    }

    std::string path_str = to_string(args[0]);
    if (path_str.empty()) return std::string("");

    fs::path p(path_str);
    if (p.has_filename()) {
        return p.filename().string();
    }
    return std::string("");
}

// KILL path_string (deletes a file)
BasicValue builtin_kill(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "Wrong number of arguments");
        return false;
    }
    std::string path_str = to_string(args[0]);
    try {
        if (fs::remove(path_str)) {
            TextIO::print("File deleted: " + path_str); TextIO::nl();
        }
        else {
            TextIO::print("File not found or is a non-empty directory."); TextIO::nl();
        }
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error deleting file: " + std::string(e.what())); TextIO::nl();
    }
    return false;
}

// --- Clipboard Functions ---

// CLIPBOARD.SET(text$)
// Sets the system clipboard text.
BasicValue builtin_clipboard_set(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "CLIPBOARD.SET requires exactly one string argument.");
        return false;
    }

    std::string text = to_string(args[0]);

#ifdef SDL3
    // Use SDL if available as it handles cross-platform details perfectly
    if (SDL_SetClipboardText(text.c_str()) < 0) {
        Error::set(12, vm.runtime_current_line, "Failed to set clipboard text via SDL.");
    }
#elif defined(_WIN32)
    // Fallback to Win32 API
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hg) {
            memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
    else {
        Error::set(12, vm.runtime_current_line, "Failed to open Windows clipboard.");
    }
#else
    Error::set(13, vm.runtime_current_line, "Clipboard not supported on this platform (SDL missing).");
#endif

    return false; // Procedure
}

// CLIPBOARD.GET$() -> string$
// Gets the text currently in the system clipboard.
BasicValue builtin_clipboard_get_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "CLIPBOARD.GET$ does not accept arguments.");
        return std::string("");
    }

#ifdef SDL3
    if (SDL_HasClipboardText()) {
        char* text = SDL_GetClipboardText();
        if (text) {
            std::string result(text);
            SDL_free(text);
            return result;
        }
    }
    return std::string("");
#elif defined(_WIN32)
    std::string result = "";
    if (OpenClipboard(NULL)) {
        HANDLE hg = GetClipboardData(CF_TEXT);
        if (hg) {
            char* str = static_cast<char*>(GlobalLock(hg));
            if (str) {
                result = str; // Copies data into std::string
                GlobalUnlock(hg);
            }
        }
        CloseClipboard();
    }
    return result;
#else
    return std::string("");
#endif
}

// --- High-Performance File I/O Functions ---

// TXTREADER$(filename$) -> string$
// Reads the entire content of a text file into a single string.
BasicValue builtin_txtreader_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "Wrong number of arguments");
        return std::string("");
    }
    std::string filename = to_string(args[0]);
    std::ifstream infile(filename);

    if (!infile) {
        Error::set(6, vm.runtime_current_line); // File not found
        return std::string("");
    }

    // Read the whole file into a stringstream buffer, then into a string.
    std::stringstream buffer;
    buffer << infile.rdbuf();
    return buffer.str();
}

// CSVREADER(filename$, [delimiter$], [has_header_bool]) -> array
// Reads a delimited file (like CSV) into a 2D array, preserving
// numbers as doubles and non-numeric data as strings.
BasicValue builtin_csvreader(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty() || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "Wrong number of arguments");
        return {};
    }

    // --- 1. Parse Arguments ---
    std::string filename = to_string(args[0]);
    char delimiter = ',';
    bool has_header = false;

    if (args.size() > 1) {
        std::string delim_str = to_string(args[1]);
        if (!delim_str.empty()) {
            delimiter = delim_str[0];
        }
    }
    if (args.size() > 2) {
        has_header = to_bool(args[2]);
    }

    // --- 2. Open File ---
    std::ifstream infile(filename);
    if (!infile) {
        Error::set(6, vm.runtime_current_line); // File not found
        return {};
    }

    // --- 3. Read and Parse ---
    std::vector<BasicValue> flat_data;
    size_t rows = 0;
    size_t cols = 0;
    std::string line;

    if (has_header && std::getline(infile, line)) {
        // Consume the header line and do nothing with it.
    }

    while (std::getline(infile, line)) {
        rows++;
        std::stringstream line_stream(line);
        std::string cell;
        size_t current_cols = 0;

        while (std::getline(line_stream, cell, delimiter)) {
            current_cols++;

            // --- Intelligent Type Conversion ---
            // Trim whitespace from the cell first
            size_t start = cell.find_first_not_of(" \t\r\n");
            size_t end = cell.find_last_not_of(" \t\r\n");
            std::string trimmed_cell = (start == std::string::npos) ? "" : cell.substr(start, end - start + 1);

            if (trimmed_cell.empty()) {
                flat_data.push_back(std::string(""));
                continue;
            }

            try {
                size_t pos;
                double num_val = std::stod(trimmed_cell, &pos);

                // Check if the entire string was consumed by stod.
                // This prevents "123xyz" from being read as the number 123.
                if (pos == trimmed_cell.length()) {
                    flat_data.push_back(num_val);
                }
                else {
                    flat_data.push_back(trimmed_cell); // Partial number, treat as string
                }
            }
            catch (const std::exception&) {
                // If stod fails completely, it's definitely a string.
                flat_data.push_back(trimmed_cell);
            }
        }

        // --- 4. Determine Shape and Validate ---
        if (rows == 1) {
            cols = current_cols;
        }
        else if (current_cols != cols) {
            Error::set(15, vm.runtime_current_line, "Inconsistent number of columns in CSV file.");
            return {};
        }
    }

    // --- 5. Create and Return the Array ---
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { rows, cols };
    result_ptr->data = flat_data;
    return result_ptr;
}

// TXTWRITER filename$, content$, [mode$]
// Writes the content of a string variable to a text file.
// If mode$ is "APPEND", the content is added to the end of the file.
BasicValue builtin_txtwriter(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // --- CHANGE: Check for 2 or 3 arguments ---
    if (args.size() < 2 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "TXTWRITER requires 2 or 3 arguments.");
        return false;
    }

    std::string filename = to_string(args[0]);
    std::string content = to_string(args[1]);
    bool append_mode = false;

    // --- CHANGE: Check for the optional third argument ---
    if (args.size() == 3) {
        // Convert mode to uppercase for case-insensitive comparison
        std::string mode = to_upper(to_string(args[2]));
        if (mode == "APPEND") {
            append_mode = true;
        }
    }

    // --- Open the file with the correct mode ---
    std::ofstream outfile;
    if (append_mode) {
        // Open in append mode, which adds to the end of the file.
        outfile.open(filename, std::ios::app);
    }
    else {
        // Default behavior: overwrite the file.
        outfile.open(filename);
    }

    if (!outfile) {
        Error::set(12, vm.runtime_current_line); // File I/O Error
        return false;
    }

    outfile << content;
    return false; // Procedures return a dummy value
}


// CSVWRITER filename$, array, [delimiter$], [header_array]
// Writes a 2D array to a CSV file, with an optional header row.
BasicValue builtin_csvwriter(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 4) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    // 1. Parse Arguments
    std::string filename = to_string(args[0]);
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line); // Second arg must be an array
        return false;
    }
    const auto& array_ptr = std::get<std::shared_ptr<Array>>(args[1]);

    char delimiter = ',';
    if (args.size() >= 3) {
        std::string delim_str = to_string(args[2]);
        if (!delim_str.empty()) {
            delimiter = delim_str[0];
        }
    }

    // 2. Validate Array Shape
    if (!array_ptr || array_ptr->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line); // Must be a 2D matrix
        return false;
    }

    // 3. Open File for Writing
    std::ofstream outfile(filename);
    if (!outfile) {
        Error::set(12, vm.runtime_current_line); // File I/O Error
        return false;
    }

    // Handle Optional Header Array ---
    if (args.size() == 4) {
        if (!std::holds_alternative<std::shared_ptr<Array>>(args[3])) {
            Error::set(15, vm.runtime_current_line); // Fourth arg must be an array
            return false;
        }
        const auto& header_ptr = std::get<std::shared_ptr<Array>>(args[3]);
        if (header_ptr) {
            for (size_t i = 0; i < header_ptr->data.size(); ++i) {
                outfile << to_string(header_ptr->data[i]);
                if (i < header_ptr->data.size() - 1) {
                    outfile << delimiter;
                }
            }
            outfile << '\n'; // End the header line
        }
    }

    // 4. Write Data 
    size_t rows = array_ptr->shape[0];
    size_t cols = array_ptr->shape[1];

    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            const BasicValue& val = array_ptr->data[r * cols + c];
            outfile << to_string(val);
            if (c < cols - 1) {
                outfile << delimiter;
            }
        }
        outfile << '\n';
    }

    return false;
}

// BINREADER$(filename$) -> string$
// Reads the entire content of a binary file into a single string (raw bytes, no newline translation).
BasicValue builtin_binreader_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "Wrong number of arguments");
        return std::string("");
    }

    const std::string filename = to_string(args[0]);
    std::ifstream infile(filename, std::ios::binary);

    if (!infile) {
        Error::set(6, vm.runtime_current_line); // File not found
        return std::string("");
    }

    // Read all bytes (including 0x00) into a std::string
    std::string data;
    infile.seekg(0, std::ios::end);
    std::streamoff size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    if (size > 0) {
        data.resize(static_cast<size_t>(size));
        infile.read(data.data(), size);
    }

    return data;
}

// BYTEAT(str$, index) -> INTEGER (0..255)
// Fast O(1) byte access into a string.
BasicValue builtin_byteat(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "Wrong number of arguments");
        return (long long)0;
    }

    const std::string s = to_string(args[0]);
    const long long idx = to_int(args[1]);

    if (idx < 0 || idx >= static_cast<long long>(s.size())) {
        Error::set(8, vm.runtime_current_line, "BYTEAT: index out of range");
        return (long long)0;
    }

    const unsigned char b = static_cast<unsigned char>(s[static_cast<size_t>(idx)]);
    return static_cast<long long>(b);
}

// --- Binary Data Construction & I/O ---

// Helper for endian swapping (if needed)
template <typename T>
T swap_endian(T u) {
    union {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;
    source.u = u;
    for (size_t k = 0; k < sizeof(T); k++)
        dest.u8[k] = source.u8[sizeof(T) - k - 1];
    return dest.u;
}

// PACK$(format$, v1, v2, ...) -> string$
// Packs numeric values into a binary string based on a format string.
// Format Specifiers:
//   < : Little Endian (default)
//   > : Big Endian
//   b : Byte (8-bit)
//   s : Short (16-bit)
//   i : Integer (32-bit)
//   l : Long (64-bit)
//   f : Float (32-bit)
//   d : Double (64-bit)
BasicValue builtin_pack(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) {
        Error::set(8, vm.runtime_current_line, "PACK$ requires at least a format string.");
        return std::string("");
    }

    std::string format = to_string(args[0]);
    std::string result;
    size_t arg_idx = 1;

    // Assume host is Little Endian (Standard for x86/ARM/WASM). 
    // If you are on a Big Endian mainframe, flip this default!
    bool big_endian_mode = false;

    for (char c : format) {
        if (c == ' ' || c == '\t') continue; // Ignore whitespace
        if (c == '<') { big_endian_mode = false; continue; }
        if (c == '>') { big_endian_mode = true; continue; }

        if (arg_idx >= args.size()) {
            Error::set(8, vm.runtime_current_line, "Not enough arguments for PACK$ format string.");
            return std::string("");
        }

        BasicValue val = args[arg_idx++];

        // Helper lambda to append value with correct endianness
        auto append_val = [&](auto v) {
            using T = decltype(v);
            if (big_endian_mode) {
                v = swap_endian(v);
            }
            const char* bytes = reinterpret_cast<const char*>(&v);
            result.append(bytes, sizeof(T));
            };

        switch (c) {
        case 'b': // Byte
        case 'B':
        {
            unsigned char v = static_cast<unsigned char>(to_double(val));
            result += (char)v;
        }
        break;
        case 's': // Short
        case 'S':
        {
            unsigned short v = static_cast<unsigned short>(to_double(val));
            append_val(v);
        }
        break;
        case 'i': // Integer (32-bit)
        case 'I':
        {
            unsigned int v = static_cast<unsigned int>(to_double(val));
            append_val(v);
        }
        break;
        case 'l': // Long (64-bit)
        case 'L':
        {
            // Ensure we cast to unsigned long long to strictly control bits
            unsigned long long v = static_cast<unsigned long long>(to_double(val));
            // If the input was actually a BasicValue Long Long, use it directly
            if (std::holds_alternative<long long>(val)) {
                v = static_cast<unsigned long long>(std::get<long long>(val));
            }
            append_val(v);
        }
        break;
        case 'f': // Float
        case 'F':
        {
            float v = static_cast<float>(to_double(val));
            append_val(v);
        }
        break;
        case 'd': // Double
        case 'D':
        {
            double v = to_double(val);
            append_val(v);
        }
        break;
        default:
            Error::set(1, vm.runtime_current_line, "Unknown PACK$ format specifier: " + std::string(1, c));
            return std::string("");
        }
    }

    return result;
}

// UNPACK(format$, binary_data$) -> Array
// Unpacks a binary string into an array of values based on the format string.
// Supported Format Specifiers:
//   < : Little Endian (default)
//   > : Big Endian
//   b : Byte (1 byte) -> Returns Integer
//   s : Short (2 bytes) -> Returns Integer
//   i : Integer (4 bytes) -> Returns Integer
//   l : Long (8 bytes) -> Returns Integer
//   f : Float (4 bytes) -> Returns Double
//   d : Double (8 bytes) -> Returns Double
BasicValue builtin_unpack(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "UNPACK requires 2 arguments: format$, binary_data$");
        return {};
    }

    std::string format = to_string(args[0]);
    std::string data = to_string(args[1]);

    // Create the result array
    auto result_ptr = std::make_shared<Array>();

    size_t data_offset = 0;
    bool big_endian_mode = false;

    for (char c : format) {
        if (c == ' ' || c == '\t') continue;
        if (c == '<') { big_endian_mode = false; continue; }
        if (c == '>') { big_endian_mode = true; continue; }

        // Helper to read types safely
        auto read_val = [&](auto& temp_val) -> bool {
            using T = std::decay_t<decltype(temp_val)>;
            if (data_offset + sizeof(T) > data.size()) {
                return false; // Not enough data left
            }
            // Copy bytes from string to temp variable
            std::memcpy(&temp_val, data.data() + data_offset, sizeof(T));

            // Handle Endianness
            if (big_endian_mode) {
                temp_val = swap_endian(temp_val);
            }
            data_offset += sizeof(T);
            return true;
            };

        switch (c) {
        case 'b': // Byte (unsigned char)
        case 'B':
        {
            if (data_offset + 1 > data.size()) goto error_len;
            unsigned char v = static_cast<unsigned char>(data[data_offset]);
            data_offset += 1;
            result_ptr->data.push_back(static_cast<long long>(v));
        }
        break;
        case 's': // Short (16-bit)
        case 'S':
        {
            unsigned short v;
            if (!read_val(v)) goto error_len;
            result_ptr->data.push_back(static_cast<long long>(v));
        }
        break;
        case 'i': // Integer (32-bit)
        case 'I':
        {
            unsigned int v;
            if (!read_val(v)) goto error_len;
            result_ptr->data.push_back(static_cast<long long>(v));
        }
        break;
        case 'l': // Long (64-bit)
        case 'L':
        {
            unsigned long long v;
            if (!read_val(v)) goto error_len;
            // Cast to signed long long for jdBasic INTEGER
            result_ptr->data.push_back(static_cast<long long>(v));
        }
        break;
        case 'f': // Float (32-bit)
        case 'F':
        {
            float v;
            // Endian swapping floats requires interpreting them as ints first
            if (data_offset + sizeof(float) > data.size()) goto error_len;

            uint32_t temp_int;
            std::memcpy(&temp_int, data.data() + data_offset, sizeof(uint32_t));
            if (big_endian_mode) temp_int = swap_endian(temp_int);

            std::memcpy(&v, &temp_int, sizeof(float));
            data_offset += sizeof(float);

            result_ptr->data.push_back(static_cast<double>(v));
        }
        break;
        case 'd': // Double (64-bit)
        case 'D':
        {
            double v;
            if (data_offset + sizeof(double) > data.size()) goto error_len;

            uint64_t temp_int;
            std::memcpy(&temp_int, data.data() + data_offset, sizeof(uint64_t));
            if (big_endian_mode) temp_int = swap_endian(temp_int);

            std::memcpy(&v, &temp_int, sizeof(double));
            data_offset += sizeof(double);

            result_ptr->data.push_back(v);
        }
        break;
        default:
            Error::set(1, vm.runtime_current_line, "Unknown UNPACK format specifier: " + std::string(1, c));
            return {};
        }
    }

    // Set array shape
    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;

error_len:
    Error::set(3, vm.runtime_current_line, "UNPACK: Not enough binary data for format string.");
    return {};
}

// BINWRITER filename$, data$
// Writes a raw string of bytes to a file, overwriting it.
BasicValue builtin_binwriter(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "BINWRITER requires 2 arguments: filename$, data$");
        return false;
    }

    std::string filename = to_string(args[0]);
    std::string data = to_string(args[1]);

    // Open in binary mode to prevent newline translation
    std::ofstream outfile(filename, std::ios::binary);

    if (!outfile) {
        Error::set(12, vm.runtime_current_line, "Failed to open file for binary writing.");
        return false;
    }

    outfile.write(data.data(), data.size());
    return false; // Procedure
}


// --- GUI and Graphic and more ---
// Handles: COLOR fg, bg
BasicValue builtin_color(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false; // Procedures return a dummy value
    }

    // Convert arguments to integers
    int fg = static_cast<int>(to_double(args[0]));
    int bg = static_cast<int>(to_double(args[1]));

    // Call the underlying TextIO function
    TextIO::setColor(fg, bg);

    return false; // Procedures must return something; the value is ignored.
}

// --- GRAPHICS PROCEDURES ---

#ifdef HTTP
// --- HTTP Built-in Functions ---

// HTTP.GET$(URL$)
BasicValue builtin_http_get(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return std::string("");
    }
    std::string url = to_string(args[0]);

    std::string response_body = vm.network_manager.httpGet(url);

    // Set a BASIC error if the HTTP request failed or returned a bad status code (e.g., 4xx or 5xx)
    if (vm.network_manager.last_http_status_code >= 400 || vm.network_manager.last_http_status_code == -1) {
        std::string reason = "?Network Error: " + response_body + "\n";
        Error::set(1003, vm.runtime_current_line, reason);
    }

    return response_body;
}

// HTTP.SETHEADER(HeaderName$, HeaderValue$)
BasicValue builtin_http_setheader(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false; // Procedures return dummy value
    }
    std::string header_name = to_string(args[0]);
    std::string header_value = to_string(args[1]);
    vm.network_manager.setHeader(header_name, header_value);
    return false;
}

// HTTP.CLEARHEADERS
BasicValue builtin_http_clearheaders(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line); // Too many arguments
        return false;
    }
    vm.network_manager.clearHeaders();
    return false;
}

// HTTP.STATUSCODE()
BasicValue builtin_http_statuscode(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line); // Too many arguments
        return 0.0;
    }
    return static_cast<double>(vm.network_manager.last_http_status_code);
}

// HTTP.POST$(URL$, Data$, ContentType$) -> ResponseBody$
BasicValue builtin_httppost(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return std::string("");
    }
    std::string url = to_string(args[0]);
    std::string body = to_string(args[1]);
    std::string content_type = to_string(args[2]);
    std::string response_body = vm.network_manager.httpPost(url, body, content_type);

    if (vm.network_manager.last_http_status_code >= 400 || vm.network_manager.last_http_status_code == -1) {
        std::string reason = "?Network Error: " + response_body + "\n";
        Error::set(1001, vm.runtime_current_line, reason);
    }

    return response_body;
}

// This helper now calls the public httpPost method.
void perform_http_post_internal(
    NetworkManager& nm, // Pass a reference to the network manager
    std::string url,
    std::string body,
    std::string content_type,
    std::shared_ptr<std::promise<BasicValue>> promise)
{
    try {
        // Call the public method, not the private helper
        std::string response = nm.httpPost(url, body, content_type);
        promise->set_value(response);
    }
    catch (...) {
        promise->set_exception(std::current_exception());
    }
}

// The new async built-in function that your BASIC code will AWAIT.
BasicValue builtin_http_post_async(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "HTTP.POST_ASYNC requires 3 arguments: url, body, content_type");
        return {};
    }

    std::string url = to_string(args[0]);
    std::string body = to_string(args[1]);
    std::string content_type = to_string(args[2]);

    // 1. Create a promise to get the result from the background thread.
    auto promise = std::make_shared<std::promise<BasicValue>>();
    std::future<BasicValue> future = promise->get_future();

    // 2. Launch the blocking work in a detached C++ thread.
    //    Pass the network manager by reference using std::ref.
    std::thread(perform_http_post_internal, std::ref(vm.network_manager), url, body, content_type, promise).detach();

    // 3. Create a "waiter" task for the interpreter's scheduler.
    auto waiter_task = std::make_shared<NeReLaBasic::Task>();
    waiter_task->id = vm.next_task_id++;
    waiter_task->status = TaskStatus::RUNNING;

    // 4. Correctly assign the future. Since the types now match, this is simple.
    waiter_task->result_future = std::move(future);

    // 5. Add the waiter task to the queue and return a handle to it.
    vm.task_queue[waiter_task->id] = waiter_task;
    return TaskRef{ waiter_task->id };
}

// HTTP.PUT$(URL$, Data$, ContentType$) -> ResponseBody$
BasicValue builtin_httpput(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return std::string("");
    }
    std::string url = to_string(args[0]);
    std::string body = to_string(args[1]);
    std::string content_type = to_string(args[2]);

    std::string response_body = vm.network_manager.httpPut(url, body, content_type);

    if (vm.network_manager.last_http_status_code >= 400 || vm.network_manager.last_http_status_code == -1) {
        std::string reason = "?Network Error: " + response_body + "\n";
        Error::set(1002, vm.runtime_current_line, reason);
    }

    return response_body;
}

// HTTP.SERVER.START port
BasicValue builtin_http_server_start(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "HTTP.SERVER.START requires 1 argument: port");
        return false;
    }
    int port = static_cast<int>(to_double(args[0]));
    if (port <= 0 || port > 65535) {
        Error::set(1, vm.runtime_current_line, "Invalid port number.");
        return false;
    }
    if (!vm.network_manager.startServer(port)) {
        Error::set(1004, vm.runtime_current_line, "Failed to start HTTP server."); // New error code
        return false;
    }
    return true;
}

// HTTP.SERVER.STOP
BasicValue builtin_http_server_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "HTTP.SERVER.STOP takes no arguments.");
        return false;
    }
    vm.network_manager.stopServer();
    return false;
}

// HTTP.SERVER.ON_GET path$, function_name$
BasicValue builtin_http_server_on_get(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "HTTP.SERVER.ON_GET requires 2 string arguments: path, function_name");
        return false;
    }
    std::string path = to_string(args[0]);
    std::string func_name = to_upper(to_string(args[1]));
    vm.network_manager.registerServerRoute("GET", path, func_name);
    return false;
}

// HTTP.SERVER.ON_POST path$, function_name$
BasicValue builtin_http_server_on_post(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "HTTP.SERVER.ON_POST requires 2 string arguments: path, function_name");
        return false;
    }
    std::string path = to_string(args[0]);
    std::string func_name = to_upper(to_string(args[1]));
    vm.network_manager.registerServerRoute("POST", path, func_name);
    return false;
}


#endif

// Other

// Implementation of the TYPEOF function
BasicValue builtin_typeof(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(26, vm.runtime_current_line, "TYPEOF requires exactly one argument.");
        return std::string(""); // Return empty string on error
    }

    const BasicValue& val = args[0];

    // Use std::visit to check the type held by the BasicValue variant
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return "BOOLEAN";
        }
        else if constexpr (std::is_same_v<T, long long>) {
            return "INTEGER";
        }
        else if constexpr (std::is_same_v<T, double>) {
            return "DOUBLE";
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return "STRING";
        }
        else if constexpr (std::is_same_v<T, DateTime>) {
            return "DATE";
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            return "ARRAY";
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Map>>) {
            return "MAP";
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<JsonObject>>) {
            return "JSON";
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Tensor>>) {
            return "TENSOR";
        }
#ifdef JDCOM
        else if constexpr (std::is_same_v<T, ComObject>) {
            return "COMOBJECT";
        }
#endif
        else if constexpr (std::is_same_v<T, FunctionRef>) {
            return "FUNCREF";
        }
        else if constexpr (std::is_same_v<T, TaskRef>) {
            return "TASKREF";
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<OpaqueHandle>>) {
            return arg ? arg->type_name : "NULL_HANDLE";
        }
        return "UNKNOWN";
        }, val);
}

//  Built-in to check if a thread is done.
BasicValue is_thread_done(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1 || !std::holds_alternative<ThreadHandle>(args[0])) {
        Error::set(15, vm.runtime_current_line, "IS_THREAD_DONE requires a ThreadHandle.");
        return false;
    }
    const auto& handle = std::get<ThreadHandle>(args[0]);

    std::lock_guard<std::mutex> lock(vm.background_tasks_mutex);
    auto it = vm.background_tasks.find(handle.id);
    if (it == vm.background_tasks.end()) {
        return true; // If it's not in the map, it's finished and result was taken.
    }

    // Check if the future is ready without blocking.
    auto status = it->second.wait_for(std::chrono::seconds(0));
    return (status == std::future_status::ready);
}

// Built-in to get a thread's result (this will block).
BasicValue get_thread_result(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1 || !std::holds_alternative<ThreadHandle>(args[0])) {
        Error::set(15, vm.runtime_current_line, "GET_THREAD_RESULT requires a ThreadHandle.");
        return {};
    }
    const auto& handle = std::get<ThreadHandle>(args[0]);
    std::future<BasicValue> result_future;

    {
        std::lock_guard<std::mutex> lock(vm.background_tasks_mutex);
        auto it = vm.background_tasks.find(handle.id);
        if (it == vm.background_tasks.end()) {
            Error::set(3, vm.runtime_current_line, "Thread result already retrieved or invalid handle.");
            return {};
        }
        // Move the future out of the map; a future's result can only be retrieved once.
        result_future = std::move(it->second);
        vm.background_tasks.erase(it);
    }

    // .get() will block here until the thread finishes and returns its value.
    // It will also re-throw any exception caught in the thread.
    return result_future.get();
}

// REGEX.MATCH(pattern$, text$) -> Boolean or Array
BasicValue builtin_regex_match(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "REGEX.MATCH requires 2 arguments: pattern, text");
        return false;
    }

    std::string pattern_str = to_string(args[0]);
    std::string text_str = to_string(args[1]);
    std::smatch matches;

    try {
        std::regex pattern(pattern_str);

        if (std::regex_search(text_str, matches, pattern)) {
            // If there are capture groups (...), return them as an array.
            if (matches.size() > 1) {
                auto result_ptr = std::make_shared<Array>();
                // Start at 1 to skip the full match (matches[0])
                for (size_t i = 1; i < matches.size(); ++i) {
                    result_ptr->data.push_back(matches[i].str());
                }
                result_ptr->shape = { result_ptr->data.size() };
                return result_ptr;
            }
            else {
                // No capture groups, but the whole string matched.
                return true;
            }
        }
        else {
            // The string did not match the pattern.
            return false;
        }
    }
    catch (const std::regex_error& e) {
        Error::set(1, vm.runtime_current_line, "Invalid regex pattern: " + std::string(e.what()));
        return false;
    }
}

// Regex functions

// REGEX.FINDALL(pattern$, text$) -> Array
BasicValue builtin_regex_findall(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "REGEX.FINDALL requires 2 arguments: pattern, text");
        return {};
    }

    std::string pattern_str = to_string(args[0]);
    std::string text_str = to_string(args[1]);
    auto result_ptr = std::make_shared<Array>();

    try {
        std::regex pattern(pattern_str);
        auto words_begin = std::sregex_iterator(text_str.begin(), text_str.end(), pattern);
        auto words_end = std::sregex_iterator();

        bool has_capture_groups = pattern.mark_count() > 0;

        if (has_capture_groups) {
            // Return a 2D array of captured groups
            for (auto it = words_begin; it != words_end; ++it) {
                const std::smatch& match = *it;
                auto row_ptr = std::make_shared<Array>();
                // Start at 1 to get only the captured groups
                for (size_t i = 1; i < match.size(); ++i) {
                    row_ptr->data.push_back(match[i].str());
                }
                row_ptr->shape = { row_ptr->data.size() };
                result_ptr->data.push_back(row_ptr);
            }
            result_ptr->shape = { result_ptr->data.size() };
        }
        else {
            // No capture groups, just return all full matches as a 1D array.
            for (auto it = words_begin; it != words_end; ++it) {
                result_ptr->data.push_back((*it).str());
            }
            result_ptr->shape = { result_ptr->data.size() };
        }

        return result_ptr;
    }
    catch (const std::regex_error& e) {
        Error::set(1, vm.runtime_current_line, "Invalid regex pattern: " + std::string(e.what()));
        return {};
    }
}

// REGEX.REPLACE(pattern$, text$, replacement$) -> String
BasicValue builtin_regex_replace(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "REGEX.REPLACE requires 3 arguments: pattern, text, replacement");
        return std::string("");
    }

    std::string pattern_str = to_string(args[0]);
    std::string text_str = to_string(args[1]);
    std::string replacement_str = to_string(args[2]);

    try {
        std::regex pattern(pattern_str);
        // regex_replace finds all matches and replaces them according to the format string.
        return std::regex_replace(text_str, pattern, replacement_str);
    }
    catch (const std::regex_error& e) {
        Error::set(1, vm.runtime_current_line, "Invalid regex pattern: " + std::string(e.what()));
        return std::string("");
    }
}

// --- OS Functions ---

// OS.ARGS() -> ARRAY
// Returns the command-line arguments passed to the interpreter.
BasicValue builtin_os_args(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "OS.ARGS does not accept arguments.");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->data.reserve(vm.command_line_args.size());

    for (const auto& arg : vm.command_line_args) {
        result_ptr->data.push_back(arg);
    }

    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// OS.EXEC(command$, [args_array$]) -> MAP
// Executes an external command and captures its output and exit code.
BasicValue builtin_os_exec(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty() || args.size() > 2) {
        Error::set(8, vm.runtime_current_line, "OS.EXEC requires 1 or 2 arguments: command$, [args_array$]");
        return {};
    }

    // 1. Construct the full command string
    std::string command = to_string(args[0]);
    if (args.size() == 2) {
        if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
            Error::set(15, vm.runtime_current_line, "Second argument to OS.EXEC must be an array of strings.");
            return {};
        }
        const auto& args_array = std::get<std::shared_ptr<Array>>(args[1]);
        if (args_array) {
            for (const auto& arg : args_array->data) {
                // A simple approach: add spaces and quotes around arguments
                command += " \"" + to_string(arg) + "\"";
            }
        }
    }

    std::string output;
    int exit_code = -1;

#ifdef _WIN32
    // --- Windows Implementation using CreateProcess and Pipes ---
    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &sa, 0)) {
        Error::set(1, vm.runtime_current_line, "Failed to create pipe for command output.");
        return {};
    }
    if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
        Error::set(1, vm.runtime_current_line, "Failed to set handle information.");
        return {};
    }

    PROCESS_INFORMATION piProcInfo = { 0 };
    STARTUPINFOA siStartInfo = { 0 };
    siStartInfo.cb = sizeof(STARTUPINFOA);
    siStartInfo.hStdError = hChildStd_OUT_Wr;
    siStartInfo.hStdOutput = hChildStd_OUT_Wr;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    BOOL bSuccess = CreateProcessA(NULL,
        &command[0],     // command line
        NULL,            // process security attributes
        NULL,            // primary thread security attributes
        TRUE,            // handles are inherited
        0,               // creation flags
        NULL,            // use parent's environment
        NULL,            // use parent's current directory
        &siStartInfo,
        &piProcInfo);

    if (!bSuccess) {
        Error::set(1, vm.runtime_current_line, "CreateProcess failed.");
        return {};
    }

    CloseHandle(hChildStd_OUT_Wr); // Close write end of pipe in parent

    CHAR chBuf[256];
    DWORD dwRead;
    while (ReadFile(hChildStd_OUT_Rd, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) {
        output.append(chBuf, dwRead);
    }

    WaitForSingleObject(piProcInfo.hProcess, INFINITE);

    DWORD dwExitCode;
    GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode);
    exit_code = dwExitCode;

    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);
    CloseHandle(hChildStd_OUT_Rd);
#elif defined(__EMSCRIPTEN__)  
    Error::set(8, vm.runtime_current_line, "OS.EXEC requires windows or linux.");
#else
    // --- POSIX (Linux, macOS) Implementation using popen ---
    std::string cmd_with_stderr = command + " 2>&1"; // Redirect stderr to stdout
    FILE* pipe = popen(cmd_with_stderr.c_str(), "r");
    if (!pipe) {
        Error::set(1, vm.runtime_current_line, "Failed to execute command.");
        return {};
    }
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }
#endif

    // 2. Assemble the result map
    auto result_map = std::make_shared<Map>();
    result_map->data["output"] = output;
    result_map->data["exit_code"] = static_cast<double>(exit_code);

    return result_map;
}

// OS.GETOS() -> STRING
// Returns a string identifying the current operating system ("WINDOWS", "LINUX", "MACOS").
BasicValue builtin_os_getos(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "OS.GETOS does not accept arguments.");
        return std::string("");
    }

#ifdef _WIN32
    return std::string("WINDOWS");
#elif __APPLE__
    return std::string("MACOS");
#elif __linux__
    return std::string("LINUX");
#else
    return std::string("UNKNOWN");
#endif
}

// OS.HOSTNAME$() -> STRING
// Returns the network hostname of the local machine.
BasicValue builtin_os_hostname_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "OS.HOSTNAME$ does not accept arguments.");
        return std::string("");
    }

    char hostname_buffer[256];

#ifdef _WIN32
    WSADATA wsaData;
    // It's good practice to initialize Winsock, though GetComputerName doesn't strictly require it.
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return std::string("WSAStartup failed");
    }
    DWORD size = sizeof(hostname_buffer);
    bool success = GetComputerNameA(hostname_buffer, &size);
    WSACleanup();
    if (success) {
        return std::string(hostname_buffer);
    }
#elif __linux__ || __APPLE__
    if (gethostname(hostname_buffer, sizeof(hostname_buffer)) == 0) {
        return std::string(hostname_buffer);
    }
       
#endif

    return std::string("UNKNOWN"); // Return empty string on failure
}

// OS.IP$() -> STRING
// Returns the primary local IPv4 address by iterating through network adapters.
BasicValue builtin_os_ip_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "OS.IP$ does not accept arguments.");
        return std::string("");
    }

#ifdef _WIN32
    // Use GetAdaptersAddresses for a robust solution on Windows
    std::string ip_address = "Not found";
    ULONG buffer_size = 15000; // A reasonable starting buffer size
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
    DWORD dwRetVal = 0;

    // Allocate a buffer to hold the adapter information.
    pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
    if (pAddresses == nullptr) {
        return "Memory allocation failed";
    }

    // Get the list of adapters
    dwRetVal = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &buffer_size);

    if (dwRetVal == NO_ERROR) {
        // Iterate through all adapters
        for (PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses; pCurrAddresses != NULL; pCurrAddresses = pCurrAddresses->Next) {
            // We're looking for an active, non-loopback, non-tunnel adapter.
            if (pCurrAddresses->OperStatus == IfOperStatusUp &&
                pCurrAddresses->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
                pCurrAddresses->IfType != IF_TYPE_TUNNEL)
            {
                // Iterate through IP addresses for the adapter
                for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
                    if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                        char ip_str[INET_ADDRSTRLEN];
                        sockaddr_in* sockaddr_ipv4 = (sockaddr_in*)pUnicast->Address.lpSockaddr;
                        inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
                        std::string current_ip = ip_str;

                        // Prioritize private network ranges, which are most likely the main LAN IP.
                        // This should find 192.168.0.37 and stop.
                        if (current_ip.rfind("192.168.", 0) == 0) {
                            ip_address = current_ip;
                            goto end_loop; // Found the best candidate, so we can stop.
                        }
                    }
                }
            }
        }
    }

end_loop:
    if (pAddresses) {
        free(pAddresses);
    }
    return ip_address;

#elif __linux__ || __APPLE__
    struct ifaddrs* ifAddrStruct = NULL;
    std::string ip_address = "Not found";

    getifaddrs(&ifAddrStruct);

    for (struct ifaddrs* ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        // Check for IPv4, and that the interface is up and not a loopback
        if (ifa->ifa_addr->sa_family == AF_INET && (ifa->ifa_flags & IFF_UP) && !(ifa->ifa_flags & IFF_LOOPBACK)) {
            char addressBuffer[INET_ADDRSTRLEN];
            void* tmpAddrPtr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);

            // On POSIX, interfaces like "en" (ethernet) or "wl" (wlan) are good indicators
            std::string iface_name = ifa->ifa_name;
            if (iface_name.rfind("en", 0) == 0 || iface_name.rfind("eth", 0) == 0 || iface_name.rfind("wl", 0) == 0) {
                ip_address = addressBuffer;
                break; // Found a likely candidate
            }
        }
    }
    if (ifAddrStruct != NULL) {
        freeifaddrs(ifAddrStruct);
    }
    return ip_address;
#else
    std::string ip_address = "Not found";;    
    return ip_address;
#endif
}

// OS.LOAD() -> DOUBLE
// Returns the current system-wide CPU load as a percentage (0-100).
// Note: This function's accuracy and behavior are OS-dependent.
BasicValue builtin_os_load(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "OS.LOAD does not accept arguments.");
        return 0.0;
    }

#ifdef _WIN32
    // --- Windows Implementation ---
    // This method takes two snapshots of system times with a short delay
    // to calculate the CPU usage over that interval.

    // Helper to convert FILETIME to a 64-bit integer
    auto filetime_to_ull = [](const FILETIME& ft) {
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        return uli.QuadPart;
        };

    FILETIME idleTime1, kernelTime1, userTime1;
    if (!GetSystemTimes(&idleTime1, &kernelTime1, &userTime1)) {
        Error::set(1, vm.runtime_current_line, "Failed to get system times.");
        return 0.0;
    }

    // Wait for a short interval to get a meaningful delta
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    FILETIME idleTime2, kernelTime2, userTime2;
    if (!GetSystemTimes(&idleTime2, &kernelTime2, &userTime2)) {
        Error::set(1, vm.runtime_current_line, "Failed to get system times (second sample).");
        return 0.0;
    }

    ULONGLONG idle_delta = filetime_to_ull(idleTime2) - filetime_to_ull(idleTime1);
    ULONGLONG kernel_delta = filetime_to_ull(kernelTime2) - filetime_to_ull(kernelTime1);
    ULONGLONG user_delta = filetime_to_ull(userTime2) - filetime_to_ull(userTime1);

    // Total time is the sum of time spent in kernel and user modes.
    ULONGLONG system_total = kernel_delta + user_delta;
    // Busy time is the total time minus the time spent idle.
    ULONGLONG busy_time = system_total - idle_delta;

    if (system_total == 0) {
        return 0.0; // Avoid division by zero if system is completely idle.
    }

    // Load is the percentage of time the system was busy.
    double load = static_cast<double>(busy_time) * 100.0 / system_total;

    // Clamp the value between 0 and 100 to handle potential timing anomalies.
    return std::max(0.0, std::min(load, 100.0));

#elif __linux__
    // --- Linux Implementation ---
    // This method reads /proc/stat at two different times (on subsequent calls)
    // to calculate the average CPU load since the last call.
    static long long prev_idle_time = 0;
    static long long prev_total_time = 0;

    std::ifstream stat_file("/proc/stat");
    if (!stat_file) {
        Error::set(1, vm.runtime_current_line, "Could not open /proc/stat to read CPU load.");
        return 0.0;
    }

    std::string line;
    std::getline(stat_file, line);
    stat_file.close();

    std::stringstream ss(line);
    std::string cpu_label;
    long long user, nice, system, idle, iowait, irq, softirq, steal;
    ss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    long long current_idle_time = idle + iowait;
    long long current_non_idle_time = user + nice + system + irq + softirq + steal;
    long long current_total_time = current_idle_time + current_non_idle_time;

    double cpu_load = 0.0;
    // We can only calculate load if we have a previous measurement to compare against.
    if (prev_total_time > 0) {
        long long total_delta = current_total_time - prev_total_time;
        long long idle_delta = current_idle_time - prev_idle_time;

        if (total_delta > 0) {
            cpu_load = (1.0 - static_cast<double>(idle_delta) / total_delta) * 100.0;
        }
    }

    // Save current values for the next call.
    prev_total_time = current_total_time;
    prev_idle_time = current_idle_time;

    // The first call will return 0.0, which is a reasonable starting point.
    return std::max(0.0, std::min(cpu_load, 100.0));

#else
    // Fallback for other operating systems (like macOS, Emscripten, etc.)
    TextIO::print("Warning: OS.LOAD is not implemented for this operating system."); TextIO::nl();
    return 0.0;
#endif
}

// --- System self coding functions ---
// 
// EXECUTE(code_string$)
// Compiles and executes a string of jdBasic code at runtime.
BasicValue builtin_execute(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "EXECUTE requires exactly one string argument.");
        return false;
    }

    std::string code_to_execute = to_string(args[0]);
    if (code_to_execute.empty()) {
        return false; // Nothing to do
    }

    // 1. Compile the string using the new, non-destructive snippet compiler.
    std::vector<uint8_t> temp_p_code;
    if (vm.compiler->tokenize_snippet(vm, temp_p_code, code_to_execute) != 0) {
        // The snippet compiler set an error, so we just return.
        return false;
    }

    // 2. Execute the compiled p-code block synchronously.
    try {
        vm.execute_synchronous_block(temp_p_code, true);
    }
    catch (const std::exception& e) {
        Error::set(1, vm.runtime_current_line, "Runtime error in code executed via EXECUTE: " + std::string(e.what()));
    }

    return false; // EXECUTE is a procedure.
}

// EVAL(expression_string$) -> BasicValue
// Compiles and evaluates a string as an expression, returning its value.
BasicValue builtin_eval(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "EVAL requires exactly one string argument.");
        return false;
    }

    std::string code_to_evaluate = to_string(args[0]);
    if (code_to_evaluate.empty()) {
        return false; // Return a default value for an empty expression
    }

    // 1. Compile the string into temporary p-code using the existing snippet compiler.
    std::vector<uint8_t> temp_p_code;
    if (vm.compiler->tokenize_snippet(vm, temp_p_code, code_to_evaluate) != 0) {
        // An error occurred during compilation. The compiler has already set the error message.
        return false;
    }

    // 2. Save the current execution context of the VM.
    auto prev_active_p_code = vm.active_p_code;
    auto prev_pcode = vm.pcode;

    // 3. Switch the VM's context to the new temporary p-code.
    vm.active_p_code = &temp_p_code;
    // The tokenizer adds a 2-byte line number (0,0). The evaluator starts after it.
    vm.pcode = 2;

    // 4. Run the expression evaluator on the temporary p-code.
    BasicValue result = vm.evaluate_expression();

    // 5. Restore the VM's original execution context.
    vm.active_p_code = prev_active_p_code;
    vm.pcode = prev_pcode;

    // Check for a runtime error during evaluation. If so, return a default value.
    if (Error::get() != 0) {
        return false;
    }

    // 6. Return the result of the expression.
    return result;
}

// --- CODEC Helper Functions ---

namespace CodecHelpers {

    // --- Base64 Implementation ---
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string base64_encode(const std::string& in) {
        std::string out;
        int val = 0, valb = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out.push_back(base64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    std::string base64_decode(const std::string& in) {
        std::string out;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

        int val = 0, valb = -8;
        for (unsigned char c : in) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    // --- SHA256 Implementation (Compact) ---
    // Rotates right
    inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    std::string sha256(const std::string& input) {
        // Initialize hash values (first 32 bits of the fractional parts of the square roots of the first 8 primes)
        uint32_t h[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };

        // Constants (first 32 bits of the fractional parts of the cube roots of the first 64 primes)
        const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        // Pre-processing
        std::vector<uint8_t> data(input.begin(), input.end());
        uint64_t bit_len = data.size() * 8;
        data.push_back(0x80);
        while ((data.size() * 8 + 64) % 512 != 0) data.push_back(0x00);

        // Append length as 64-bit big endian integer
        for (int i = 7; i >= 0; --i) data.push_back((bit_len >> (i * 8)) & 0xff);

        // Process chunks
        for (size_t i = 0; i < data.size(); i += 64) {
            uint32_t w[64];
            for (int j = 0; j < 16; ++j) {
                w[j] = (data[i + j * 4] << 24) | (data[i + j * 4 + 1] << 16) |
                    (data[i + j * 4 + 2] << 8) | (data[i + j * 4 + 3]);
            }
            for (int j = 16; j < 64; ++j) {
                uint32_t s0 = rotr(w[j - 15], 7) ^ rotr(w[j - 15], 18) ^ (w[j - 15] >> 3);
                uint32_t s1 = rotr(w[j - 2], 17) ^ rotr(w[j - 2], 19) ^ (w[j - 2] >> 10);
                w[j] = w[j - 16] + s0 + w[j - 7] + s1;
            }

            uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
            uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7]; // Renamed h to h_val to avoid conflict

            for (int j = 0; j < 64; ++j) {
                uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                uint32_t ch = (e & f) ^ (~e & g);
                uint32_t temp1 = h_val + S1 + ch + k[j] + w[j];
                uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t temp2 = S0 + maj;

                h_val = g; g = f; f = e; e = d + temp1;
                d = c; c = b; b = a; a = temp1 + temp2;
            }

            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
        }

        // Output to hex string
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 8; ++i) ss << std::setw(8) << h[i];
        return ss.str();
    }
} // namespace CodecHelpers

// --- Built-in CODEC Wrapper Functions ---

// CODEC.BASE64_ENCODE$(string$) -> string$
BasicValue builtin_codec_base64_encode(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "CODEC.BASE64_ENCODE$ requires 1 argument.");
        return std::string("");
    }
    return CodecHelpers::base64_encode(to_string(args[0]));
}

// CODEC.BASE64_DECODE$(string$) -> string$
BasicValue builtin_codec_base64_decode(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "CODEC.BASE64_DECODE$ requires 1 argument.");
        return std::string("");
    }
    return CodecHelpers::base64_decode(to_string(args[0]));
}

// CODEC.SHA256$(string$) -> string$
BasicValue builtin_codec_sha256(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "CODEC.SHA256$ requires 1 argument.");
        return std::string("");
    }
    return CodecHelpers::sha256(to_string(args[0]));
}

// CODEC.UUID$() -> string$
// Generates a version 4 UUID.
BasicValue builtin_codec_uuid(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "CODEC.UUID$ does not accept arguments.");
        return std::string("");
    }

    // --- Windows Implementation using COM ---
#if defined(_WIN32)
    GUID guid;
    if (CoCreateGuid(&guid) == S_OK) {
        // 36 characters + null terminator
        char guid_cstr[39];
        // Format: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
        snprintf(guid_cstr, sizeof(guid_cstr),
            "%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

        return std::string(guid_cstr);
    }
    return std::string("00000000-0000-0000-0000-000000000000"); // Fallback on failure
#else
    // --- Cross-Platform C++11 Implementation ---
    // Uses pseudo-random numbers (good enough for general usage)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4"; // UUID version 4
    for (i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen); // Variant 10xx
    for (i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (i = 0; i < 12; i++) ss << dis(gen);

    return ss.str();
#endif
}

// =========================================================
// SERIAL COMMUNICATION FUNCTIONS (USE_SERIAL)
// =========================================================
#ifdef USE_SERIAL

struct SerialHandle : public OpaqueHandle {
#ifdef _WIN32
    HANDLE hComm;
#else
    int fd;
#endif
    SerialHandle() : OpaqueHandle("SERIAL_PORT") {
#ifdef _WIN32
        hComm = INVALID_HANDLE_VALUE;
#else
        fd = -1;
#endif
    }

    ~SerialHandle() {
#ifdef _WIN32
        if (hComm != INVALID_HANDLE_VALUE) CloseHandle(hComm);
#else
        if (fd >= 0) close(fd);
#endif
    }
};

// SERIAL.OPEN(port$, baud_rate) -> handle
BasicValue builtin_serial_open(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SERIAL.OPEN requires 2 arguments: port$, baud_rate");
        return std::make_shared<OpaqueHandle>(); // Return null handle
    }

    std::string port = to_string(args[0]);
    int baud = static_cast<int>(to_double(args[1]));

    auto handle_ptr = std::make_shared<SerialHandle>();

#ifdef _WIN32
    // Windows Implementation
    std::string full_port_name = "\\\\.\\" + port; // Handle COM10+ syntax
    handle_ptr->hComm = CreateFileA(full_port_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,    // No sharing
        NULL, // No security
        OPEN_EXISTING,
        0,    // No Overlapped I/O
        NULL);

    if (handle_ptr->hComm == INVALID_HANDLE_VALUE) {
        // Return null handle (or throw error if preferred, but silent failure + null check is often better in BASIC)
        return std::make_shared<OpaqueHandle>();
    }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(handle_ptr->hComm, &dcbSerialParams)) {
        CloseHandle(handle_ptr->hComm);
        return std::make_shared<OpaqueHandle>();
    }

    dcbSerialParams.BaudRate = baud;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(handle_ptr->hComm, &dcbSerialParams)) {
        CloseHandle(handle_ptr->hComm);
        return std::make_shared<OpaqueHandle>();
    }

    // Set timeouts (Non-blocking read behavior is usually preferred for game loops)
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle_ptr->hComm, &timeouts);

#else
    // Linux/POSIX Implementation
    handle_ptr->fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (handle_ptr->fd == -1) {
        return std::make_shared<OpaqueHandle>();
    }

    // Clear flags
    fcntl(handle_ptr->fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(handle_ptr->fd, &options);

    // Set Baud Rate
    speed_t speed;
    switch (baud) {
    case 9600:   speed = B9600; break;
    case 19200:  speed = B19200; break;
    case 38400:  speed = B38400; break;
    case 57600:  speed = B57600; break;
    case 115200: speed = B115200; break;
    default:     speed = B9600; break;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // 8N1
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // No flow control
    options.c_cflag &= ~CRTSCTS;

    // Turn on READ & ignore ctrl lines
    options.c_cflag |= (CLOCAL | CREAD);

    // Raw input mode
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    // Apply
    tcsetattr(handle_ptr->fd, TCSANOW, &options);
#endif

    return std::static_pointer_cast<OpaqueHandle>(handle_ptr);
}

// SERIAL.CLOSE(handle)
BasicValue builtin_serial_close(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;

    // The shared_ptr destructor handles the actual closing.
    // We just need to ensure the variable in VM lets go of the reference.
    // In this specific C++ func, we don't modify the VM var directly, 
    // but the user should assume the handle is invalid after calling this.
    // Ideally, we'd explicitly close the internal handle here to be safe.

    if (std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        auto opaque = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
        if (opaque && opaque->type_name == "SERIAL_PORT") {
            auto serial = std::static_pointer_cast<SerialHandle>(opaque);
#ifdef _WIN32
            if (serial->hComm != INVALID_HANDLE_VALUE) {
                CloseHandle(serial->hComm);
                serial->hComm = INVALID_HANDLE_VALUE;
            }
#else
            if (serial->fd >= 0) {
                close(serial->fd);
                serial->fd = -1;
            }
#endif
        }
    }
    return false;
}

// SERIAL.WRITE(handle, data$)
BasicValue builtin_serial_write(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) return false;

    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) return false;
    auto opaque = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!opaque || opaque->type_name != "SERIAL_PORT") return false;

    auto serial = std::static_pointer_cast<SerialHandle>(opaque);
    std::string data = to_string(args[1]);

#ifdef _WIN32
    DWORD bytesWritten;
    WriteFile(serial->hComm, data.c_str(), (DWORD)data.length(), &bytesWritten, NULL);
#else
    if (serial->fd >= 0) {
        write(serial->fd, data.c_str(), data.length());
    }
#endif
    return false;
}

// SERIAL.READ$(handle, max_bytes) -> string
BasicValue builtin_serial_read(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) return std::string("");

    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) return std::string("");
    auto opaque = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!opaque || opaque->type_name != "SERIAL_PORT") return std::string("");

    auto serial = std::static_pointer_cast<SerialHandle>(opaque);
    int max_bytes = static_cast<int>(to_double(args[1]));
    if (max_bytes <= 0) return std::string("");

    std::string result;
    result.resize(max_bytes);

#ifdef _WIN32
    DWORD bytesRead = 0;
    if (ReadFile(serial->hComm, &result[0], max_bytes, &bytesRead, NULL)) {
        result.resize(bytesRead);
        return result;
    }
#else
    if (serial->fd >= 0) {
        // Non-blocking read
        int flags = fcntl(serial->fd, F_GETFL, 0);
        fcntl(serial->fd, F_SETFL, flags | O_NONBLOCK);

        ssize_t bytesRead = read(serial->fd, &result[0], max_bytes);
        if (bytesRead > 0) {
            result.resize(bytesRead);
            return result;
        }
    }
#endif
    return std::string("");
}

// SERIAL.AVAILABLE(handle) -> number
BasicValue builtin_serial_available(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return 0.0;
    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) return 0.0;
    auto opaque = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!opaque || opaque->type_name != "SERIAL_PORT") return 0.0;
    auto serial = std::static_pointer_cast<SerialHandle>(opaque);

#ifdef _WIN32
    COMSTAT status;
    DWORD errors;
    ClearCommError(serial->hComm, &errors, &status);
    return (double)status.cbInQue;
#else
    if (serial->fd >= 0) {
        int bytes = 0;
        ioctl(serial->fd, FIONREAD, &bytes);
        return (double)bytes;
    }
    return 0.0;
#endif
}

// SERIAL.FLUSH(handle)
BasicValue builtin_serial_flush(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return false;
    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) return false;
    auto opaque = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!opaque || opaque->type_name != "SERIAL_PORT") return false;
    auto serial = std::static_pointer_cast<SerialHandle>(opaque);

#ifdef _WIN32
    PurgeComm(serial->hComm, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
    if (serial->fd >= 0) {
        tcflush(serial->fd, TCIOFLUSH);
    }
#endif
    return false;
}

#endif // USE_SERIAL

#ifdef JD_IMGUI
void register_imgui_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table);
#endif

// --- The Registration Function ---
void register_builtin_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate) {
    // Helper lambda to make registration cleaner
    auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        table_to_populate[to_upper(info.name)] = info;
        };
    // --- Register Procedures ---
    auto register_proc = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        info.is_procedure = true; // Mark this as a procedure
        table_to_populate[to_upper(info.name)] = info;
        };

    // 1. Register AI functions from the dedicated module
    register_ai_functions(vm, table_to_populate);
    register_better_code_functions(vm, table_to_populate);
    register_array_functions(vm, table_to_populate);
#ifdef SDL3
    register_sdl_functions(vm, table_to_populate);
#ifdef JD_IMGUI
    register_imgui_functions(vm, table_to_populate);
#endif
#endif

    // --- Register String Functions ---
    register_func("LEFT$", 2, builtin_left_str);
    register_func("RIGHT$", 2, builtin_right_str);
    register_func("MID$", -1, builtin_mid_str); // -1 for variable args
    register_func("LEN", 1, builtin_len);
    register_func("ASC", 1, builtin_asc);
    register_func("CHR$", 1, builtin_chr_str);
    register_func("INSTR$", -1, builtin_instr); // -1 for variable args
    register_func("INSTR", -1, builtin_instr); // -1 for variable args
    register_func("INSERT$", 3, builtin_insert_str);
    register_func("LCASE$", 1, builtin_lcase_str);
    register_func("UCASE$", 1, builtin_ucase_str);
    register_func("TRIM$", 1, builtin_trim_str);
    register_func("REPLACE$", 3, builtin_replace_str);
    register_func("REVERSE$", 1, builtin_reverse_str);
    #ifndef __EMSCRIPTEN__
    register_func("WAITKEY$", 0, builtin_waitkey_str);
    #endif
    register_func("INKEY$", 0, builtin_inkey);    
    register_func("VAL", 1, builtin_val);
    register_func("STR$", 1, builtin_str_str);
    register_func("SPLIT", 2, builtin_split);
    register_func("FRMV$", -1, builtin_frmv_str);
    register_func("FORMAT$", -1, builtin_format_str);

    // --- Register Regex Functions ---
    register_func("REGEX.MATCH", 2, builtin_regex_match);
    register_func("REGEX.FINDALL", 2, builtin_regex_findall);
    register_func("REGEX.REPLACE", 3, builtin_regex_replace);

    // --- Other things
    register_func("TYPEOF", 1, builtin_typeof);
    register_func("THREAD.ISDONE", 1, is_thread_done);
    register_func("THREAD.GETRESULT", 1, get_thread_result);

    // --- Register Math Functions ---
    register_func("SIN", 1, builtin_sin);
    register_func("COS", 1, builtin_cos);
    register_func("TAN", 1, builtin_tan);
    register_func("SQR", 1, builtin_sqr);
    register_func("RND", 1, builtin_rnd);
    register_func("LOG", 1, builtin_log);
    register_func("LOG10", 1, builtin_log10);
    register_func("FAC", 1, builtin_fac);
    register_func("ABS", 1, builtin_abs);
    register_func("INT", 1, builtin_int);
    register_func("CDBL", 1, builtin_cdbl);
    register_func("FLOOR", 1, builtin_floor);
    register_func("CEIL", 1, builtin_ceil);
    register_func("TRUNC", 1, builtin_trunc);
    register_func("ROUND", 2, builtin_round);
    register_func("CLAMP", 3, builtin_clamp);
    register_func("DISTANCE", 2, builtin_distance);
    register_func("LERP", 3, builtin_lerp);
    register_func("SHL", 2, builtin_shl);
    register_func("SHR", 2, builtin_shr);
    register_func("IIF", 3, builtin_iif);

    // --- Register Time Functions ---
    register_func("TICK", 0, builtin_tick);
    register_func("NOW", 0, builtin_now);
    register_func("DATE$", 0, builtin_date_str);
    register_func("TIME$", 0, builtin_time_str);
    register_func("DATEADD", 3, builtin_dateadd);
    register_func("DATEDIFF", 3, builtin_datediff);
    register_func("CVDATE", 1, builtin_cvdate);

#ifdef JDCOM
    register_func("CREATEOBJECT", 1, builtin_create_object);
#endif
#ifdef HTTP
    // These will effectively be available as "HTTP.GET$", "HTTP.SETHEADER", etc.,
    // after the HTTP module is compiled and its exported functions are linked.
    register_func("HTTP.GET$", 1, builtin_http_get);
    register_proc("HTTP.SETHEADER", 2, builtin_http_setheader);
    register_proc("HTTP.CLEARHEADERS", 0, builtin_http_clearheaders);
    register_func("HTTP.STATUSCODE", 0, builtin_http_statuscode);
    register_func("HTTP.POST$", 3, builtin_httppost);
    register_func("HTTP.POST_ASYNC", 3, builtin_http_post_async);
    register_func("HTTP.PUT$", 3, builtin_httpput);
    register_func("HTTP.SERVER.START", 1, builtin_http_server_start);
    register_proc("HTTP.SERVER.STOP", 0, builtin_http_server_stop);
    register_proc("HTTP.SERVER.ON_GET", 2, builtin_http_server_on_get);
    register_proc("HTTP.SERVER.ON_POST", 2, builtin_http_server_on_post);
#endif

    register_func("JSON.PARSE$", 1, builtin_json_parse);
    register_func("JSON.STRINGIFY$", 1, builtin_json_stringify);

    register_func("MAP.EXISTS", 2, builtin_map_exists);
    register_func("MAP.KEYS", 1, builtin_map_keys);
    register_func("MAP.VALUES", 1, builtin_map_values);
    register_proc("MAP.DELETE", 2, builtin_map_delete);
    register_proc("MAP.CLEAR", 1, builtin_map_clear);
    register_func("MAP.SIZE", 1, builtin_map_size);
    register_proc("MAP.MERGE", 2, builtin_map_merge);
    register_func("MAP.ITEMS", 1, builtin_map_items);
    register_func("MAP.FROM", 1, builtin_map_from);

    register_proc("HELP", -1, builtin_help);
    register_func("HELP$", 0, builtin_help_str);
    register_proc("SETLOCALE", 1, builtin_setlocale);
    register_proc("CLS", -1, builtin_cls);
    register_proc("LOCATE", 2, builtin_locate);
    register_func("GETX", 0, builtin_getx);
    register_func("GETY", 0, builtin_gety);
    register_proc("SLEEP", 1, builtin_sleep);
    register_proc("YIELD", 0, builtin_yield);
    register_proc("CURSOR", 1, builtin_cursor);
    register_func("GETENV$", 1, builtin_getenv_str);
    register_proc("THROW", -1, builtin_throw);
    register_proc("CLEARWS", 0, builtin_clearws);
    register_proc("NEW", 0, builtin_new);
    register_proc("UNREACT", 1, builtin_unreact);

    register_func("DIR$", -1, builtin_dir_str);
    register_proc("PWD", 0, builtin_pwd);
    register_proc("COLOR", 2, builtin_color);
    register_func("PATH.JOIN$", -1, builtin_path_join); // -1 for variable args
    register_func("PATH.EXT$", 1, builtin_path_ext);
    register_func("PATH.BASENAME$", 1, builtin_path_basename);
    register_proc("CLIPBOARD.SET", 1, builtin_clipboard_set);
    register_func("CLIPBOARD.GET$", 0, builtin_clipboard_get_str);

    register_func("OS.ARGS", 0, builtin_os_args);
    register_func("OS.EXEC", -1, builtin_os_exec);
    register_func("OS.GETOS", 0, builtin_os_getos);
    register_func("OS.HOSTNAME$", 0, builtin_os_hostname_str);
    register_func("OS.IP$", 0, builtin_os_ip_str);
    register_func("OS.LOAD", 0, builtin_os_load);

    register_proc("EXECUTE", 1, builtin_execute);
    register_func("EVAL", 1, builtin_eval);

    register_func("CSVREADER", -1, builtin_csvreader); // -1 for optional args
    register_func("TXTREADER$", 1, builtin_txtreader_str);
    register_proc("TXTWRITER", -1, builtin_txtwriter);
    register_proc("CSVWRITER", -1, builtin_csvwriter); // -1 for optional delimiter
    register_func("BINREADER$", 1, builtin_binreader_str);
    register_proc("BINWRITER", 2, builtin_binwriter);
    register_func("BYTEAT", 2, builtin_byteat);
    register_func("PACK$", -1, builtin_pack); // Variable arguments
    register_func("UNPACK", 2, builtin_unpack);

    register_func("CODEC.BASE64_ENCODE$", 1, builtin_codec_base64_encode);
    register_func("CODEC.BASE64_DECODE$", 1, builtin_codec_base64_decode);
    register_func("CODEC.SHA256$", 1, builtin_codec_sha256);
    register_func("CODEC.UUID$", 0, builtin_codec_uuid);

#ifdef USE_SERIAL
    register_func("SERIAL.OPEN", 2, builtin_serial_open);
    register_proc("SERIAL.CLOSE", 1, builtin_serial_close);
    register_proc("SERIAL.WRITE", 2, builtin_serial_write);
    register_func("SERIAL.READ$", 2, builtin_serial_read);
    register_func("SERIAL.AVAILABLE", 1, builtin_serial_available);
    register_proc("SERIAL.FLUSH", 1, builtin_serial_flush);
#endif
}