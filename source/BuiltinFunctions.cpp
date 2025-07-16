#include "Commands.hpp"
#include "NeReLaBasic.hpp"
#include "BuiltinFunctions.hpp"
#include "AIFunctions.hpp"
#include "TextIO.hpp"
#include "Error.hpp"
#include "Types.hpp"
#include "LocaleManager.hpp"
#include <thread>
#include <chrono>
#include <cmath> // For sin, cos, etc.
#include <conio.h>
#include <algorithm>    // For std::transform
#include <string>       // For std::string, std::to_string
#include <vector>       // For std::vector
#include <filesystem>   
#include <fstream>
#include <iostream>     
#include <regex>
#include <iomanip> 
#include <sstream>
#include <unordered_set>
#include <cstdlib> 
#include <format>
#include <functional>
#include <random>
#include <future>
#include <regex>
#include <cmath>


#ifdef HTTP
#include "NetworkManager.hpp"
#endif

#include "json.hpp"

#ifdef JDCOM
#include <windows.h> // Basic Windows types, HRESULT
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
        else if constexpr (std::is_same_v<T, int>) { // If you still use int
            return _variant_t(static_cast<long>(arg)); // Convert to long for VARIANT
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            // Convert std::string to BSTR (Basic string)
            return _variant_t(arg.c_str()); // BSTR is allocated internally by _variant_t
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
    case VT_I8:       return (double)vt.llVal;
    case VT_UI8:      return (double)vt.ullVal;
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

namespace {

    // Structure to hold Gauss-Legendre quadrature points and weights
    struct GaussRule {
        std::vector<double> points;
        std::vector<double> weights;
    };

    // Pre-calculated Gauss-Legendre points and weights for the interval [-1, 1]
    const std::map<int, GaussRule> GAUSS_RULES = {
        {1, {{0.0}, {2.0}}},
        {2, {{-0.5773502691896257, 0.5773502691896257}, {1.0, 1.0}}},
        {3, {{-0.7745966692414834, 0.0, 0.7745966692414834}, {0.5555555555555556, 0.8888888888888888, 0.5555555555555556}}},
        {4, {{-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526}, {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538}}},
        {5, {{-0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640}, {0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891}}}
    };

    // Solves the linear system Ax = b for x using LU Decomposition with partial pivoting.
    // - A is the n x n coefficient matrix, passed as a flat vector.
    // - b is the n x 1 known vector.
    // - n is the dimension of the system.
    // Returns the solution vector x, or an empty vector if the matrix is singular.
    // NOTE: This implementation takes A by value because it modifies it in-place.
    std::vector<double> lu_solve(std::vector<double> A, std::vector<double> b, int n) {
        std::vector<int> pivot_map(n);

        // Initialize pivot map
        for (int i = 0; i < n; ++i) {
            pivot_map[i] = i;
        }

        // --- LU Decomposition with Partial Pivoting ---
        for (int i = 0; i < n; ++i) {
            // Find the pivot row
            int max_row = i;
            for (int j = i + 1; j < n; ++j) {
                if (std::abs(A[j * n + i]) > std::abs(A[max_row * n + i])) {
                    max_row = j;
                }
            }

            // Swap rows in matrix A if necessary
            if (max_row != i) {
                for (int k = 0; k < n; ++k) {
                    std::swap(A[i * n + k], A[max_row * n + k]);
                }
                // Record the swap in the pivot map
                std::swap(pivot_map[i], pivot_map[max_row]);
            }

            // Check for singularity (or near-singularity)
            if (std::abs(A[i * n + i]) < 1e-12) {
                return {}; // Return empty vector to indicate a singular matrix
            }

            // Perform elimination for the rows below the pivot
            for (int j = i + 1; j < n; ++j) {
                A[j * n + i] /= A[i * n + i]; // Calculate the multiplier
                for (int k = i + 1; k < n; ++k) {
                    A[j * n + k] -= A[j * n + i] * A[i * n + k];
                }
            }
        }

        // --- Apply the pivot permutation to the vector b ---
        std::vector<double> x(n);
        for (int i = 0; i < n; ++i) {
            x[i] = b[pivot_map[i]];
        }

        // --- Forward Substitution (solves Ly = P*b) ---
        for (int i = 1; i < n; ++i) {
            for (int j = 0; j < i; ++j) {
                x[i] -= A[i * n + j] * x[j];
            }
        }

        // --- Backward Substitution (solves Ux = y) ---
        for (int i = n - 1; i >= 0; --i) {
            for (int j = i + 1; j < n; ++j) {
                x[i] -= A[i * n + j] * x[j];
            }
            x[i] /= A[i * n + i];
        }

        return x; // Return the solution vector
    }

    // Computes the inverse of matrix A using the LU decomposition solver.
    std::vector<double> lu_invert(const std::vector<double>& A, int n) {
        std::vector<double> inverse(n * n);

        // Solve the system A * X = I, where I is the identity matrix.
        // We do this by solving for each column of X (the inverse) one at a time.
        for (int j = 0; j < n; ++j) {
            std::vector<double> b(n, 0.0);
            b[j] = 1.0; // The j-th column of the identity matrix

            // Solve A * x_j = b for x_j (the j-th column of the inverse)
            std::vector<double> x_j = lu_solve(A, b, n);

            if (x_j.empty()) {
                return {}; // Matrix is singular, cannot invert.
            }

            // Place the solution column into the correct place in the final inverse matrix.
            for (int i = 0; i < n; ++i) {
                inverse[i * n + j] = x_j[i];
            }
        }
        return inverse;
    }
}

// --- JSON Functionality ---

// Forward declaration for recursive conversion
nlohmann::json basic_to_json_value(const BasicValue& val);

// Helper function to convert a BasicValue into a nlohmann::json object.
nlohmann::json basic_to_json_value(const BasicValue& val) {
    return std::visit([](auto&& arg) -> nlohmann::json {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> || std::is_same_v<T, int> || std::is_same_v<T, std::string>) {
            return nlohmann::json(arg);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            if (!arg) return nlohmann::json::array();
            nlohmann::json j_arr = nlohmann::json::array();
            for (const auto& elem : arg->data) {
                j_arr.push_back(basic_to_json_value(elem));
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
            // If we encounter a JsonObject, just return its internal data.
            return arg ? arg->data : nlohmann::json(nullptr);
        }
        else if constexpr (std::is_same_v<T, DateTime> || std::is_same_v<T, FunctionRef>) {
            // Convert these types to their string representation
            return nlohmann::json(to_string(arg));
        }
#ifdef JDCOM
        else if constexpr (std::is_same_v<T, ComObject>) {
            return nlohmann::json("<COM Object>");
        }
#endif
        else {
            // Should not be reached if all types are handled. Return null.
            return nlohmann::json(nullptr);
        }
        }, val);
}


// Helper function to convert a nlohmann::json object to a BasicValue
// This is essential for accessing parts of the parsed JSON from BASIC.
BasicValue json_to_basic_value(const nlohmann::json& j) {
    if (j.is_null()) {
        return 0.0; // Or handle as an error/special null type
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
        // Convert JSON object to our BASIC Map
        auto map_ptr = std::make_shared<Map>();
        for (auto& [key, value] : j.items()) {
            map_ptr->data[key] = json_to_basic_value(value);
        }
        return map_ptr;
    }
    if (j.is_array()) {
        // Convert JSON array to our BASIC Array (always 1D for simplicity here)
        auto array_ptr = std::make_shared<Array>();
        for (const auto& item : j) {
            array_ptr->data.push_back(json_to_basic_value(item));
        }
        array_ptr->shape = { array_ptr->data.size() };
        return array_ptr;
    }
    // Default fallback
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
        TextIO::print("JSON Parse Error: " + std::string(e.what()) + "\n");
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
        nlohmann::json j = basic_to_json_value(val_to_stringify);
        // dump() with no arguments creates a compact string, ideal for API calls.
        // For pretty-printing, you could use j.dump(4)
        return j.dump();
    }
    catch (const std::exception& e) {
        Error::set(15, vm.runtime_current_line); // Type Mismatch or other conversion error
        TextIO::print("JSON Stringify Error: " + std::string(e.what()) + "\n");
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


//=========================================================
// C++ Implementations of our Native BASIC Functions
//=========================================================

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


// --- SDL Integration ---

#ifdef SDL3

// This map is now defined directly in this file to resolve linker errors.
const std::map<std::string, Waveform> waveform_map = {
    {"SINE", Waveform::SINE},
    {"SQUARE", Waveform::SQUARE},
    {"SAW", Waveform::SAWTOOTH},
    {"TRIANGLE", Waveform::TRIANGLE}
};

extern const std::map<std::string, Waveform> waveform_map;

// SCREEN width, height, [title$]
// Initializes the graphics screen.
BasicValue builtin_screen(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 4) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    int width = static_cast<int>(to_double(args[0]));
    int height = static_cast<int>(to_double(args[1]));
    std::string title = "jdBasic Graphics";
    float scale = 1.0f;

    if (args.size() == 3) {
        title = to_string(args[2]);
    }

    if (args.size() == 4) {
        scale = static_cast<float>(to_double(args[3]));
    }

    // Pass all arguments, including the new scale factor, to the init method
    if (!vm.graphics_system.init(title, width, height, scale)) {
        Error::set(1, vm.runtime_current_line); // Generic error
    }

    return false; // Procedures return a dummy value
}

// SCREENFLIP
// Updates the screen to show all drawing done since the last flip.
BasicValue builtin_screenflip(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.update_screen();
    return false;
}

BasicValue builtin_drawcolor(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Check for the correct number of arguments (either 1 or 3)
    if (args.size() != 1 && args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "DRAWCOLOR requires 3 numeric arguments or 1 array argument.");
        return false;
    }

    Uint8 r, g, b;

    // Case 1: Single array argument, e.g., DRAWCOLOR [r, g, b]
    if (args.size() == 1) {
        // Ensure the argument is an array
        if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
            Error::set(15, vm.runtime_current_line, "Single argument to DRAWCOLOR must be an array.");
            return false;
        }

        const auto& color_vec_ptr = std::get<std::shared_ptr<Array>>(args[0]);

        // Ensure the array is valid and has exactly 3 elements
        if (!color_vec_ptr || color_vec_ptr->data.size() != 3) {
            Error::set(15, vm.runtime_current_line, "Color array for DRAWCOLOR must contain exactly 3 elements.");
            return false;
        }

        // Extract RGB values from the array
        r = static_cast<Uint8>(to_double(color_vec_ptr->data[0]));
        g = static_cast<Uint8>(to_double(color_vec_ptr->data[1]));
        b = static_cast<Uint8>(to_double(color_vec_ptr->data[2]));
    }
    // Case 2: Three separate scalar arguments, e.g., DRAWCOLOR r, g, b
    else {
        r = static_cast<Uint8>(to_double(args[0]));
        g = static_cast<Uint8>(to_double(args[1]));
        b = static_cast<Uint8>(to_double(args[2]));
    }

    // Call the underlying graphics system function with the extracted colors
    vm.graphics_system.setDrawColor(r, g, b);
    return false; // Procedures return a dummy value
}


// Replace the existing 'builtin_pset' function with this new version
BasicValue builtin_pset(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& points = std::get<std::shared_ptr<Array>>(args[0]);
        std::shared_ptr<Array> colors = nullptr;
        // Check for optional colors matrix
        if (args.size() == 2 && std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
            colors = std::get<std::shared_ptr<Array>>(args[1]);
        }
        else if (args.size() > 1) {
            Error::set(15, vm.runtime_current_line, "Second argument for vectorized PSET must be a color matrix.");
            return false;
        }
        vm.graphics_system.pset(points, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 2 || args.size() == 4 || args.size() > 5) {
        Error::set(8, vm.runtime_current_line, "Usage: PSET x, y, [r, g, b] OR PSET matrix, [colors]");
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));

    if (args.size() == 5) {
        Uint8 r = static_cast<Uint8>(to_double(args[2]));
        Uint8 g = static_cast<Uint8>(to_double(args[3]));
        Uint8 b = static_cast<Uint8>(to_double(args[4]));
        vm.graphics_system.pset(x, y, r, g, b);
    }
    else { // 2 args
        vm.graphics_system.pset(x, y);
    }
    return false;
}


// Replace the existing 'builtin_line' function with this new version
BasicValue builtin_line(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& lines = std::get<std::shared_ptr<Array>>(args[0]);
        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 2 && std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
            colors = std::get<std::shared_ptr<Array>>(args[1]);
        }
        else if (args.size() > 1) {
            Error::set(15, vm.runtime_current_line, "Second argument for vectorized LINE must be a color matrix.");
            return false;
        }
        vm.graphics_system.line(lines, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 4 || args.size() == 5 || args.size() == 6 || args.size() > 7) {
        Error::set(8, vm.runtime_current_line, "Usage: LINE x1, y1, x2, y2, [r, g, b] OR LINE matrix, [colors]");
        return false;
    }

    int x1 = static_cast<int>(to_double(args[0]));
    int y1 = static_cast<int>(to_double(args[1]));
    int x2 = static_cast<int>(to_double(args[2]));
    int y2 = static_cast<int>(to_double(args[3]));

    if (args.size() == 7) {
        Uint8 r = static_cast<Uint8>(to_double(args[4]));
        Uint8 g = static_cast<Uint8>(to_double(args[5]));
        Uint8 b = static_cast<Uint8>(to_double(args[6]));
        vm.graphics_system.line(x1, y1, x2, y2, r, g, b);
    }
    else { // 4 args
        vm.graphics_system.line(x1, y1, x2, y2);
    }
    return false;
}

// Replace the existing 'builtin_rect' function with this new version
BasicValue builtin_rect(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& rects = std::get<std::shared_ptr<Array>>(args[0]);
        bool is_filled = false;
        if (args.size() >= 2) is_filled = to_bool(args[1]);

        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 3 && std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
            colors = std::get<std::shared_ptr<Array>>(args[2]);
        }
        else if (args.size() > 2) {
            Error::set(15, vm.runtime_current_line, "Third argument for vectorized RECT must be a color matrix.");
            return false;
        }
        vm.graphics_system.rect(rects, is_filled, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 4 || args.size() > 8) {
        Error::set(8, vm.runtime_current_line, "Usage: RECT x, y, w, h, [r, g, b], [fill] OR RECT matrix, [fill], [colors]");
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    int w = static_cast<int>(to_double(args[2]));
    int h = static_cast<int>(to_double(args[3]));
    bool fill = false;

    if (args.size() >= 7) { // Color is provided
        Uint8 r = static_cast<Uint8>(to_double(args[4]));
        Uint8 g = static_cast<Uint8>(to_double(args[5]));
        Uint8 b = static_cast<Uint8>(to_double(args[6]));
        if (args.size() == 8) fill = to_bool(args[7]);
        vm.graphics_system.rect(x, y, w, h, r, g, b, fill);
    }
    else { // No color, just check for fill
        if (args.size() == 5) fill = to_bool(args[4]);
        vm.graphics_system.rect(x, y, w, h, fill);
    }
    return false;
}

// Replace the existing 'builtin_circle' function with this new version
BasicValue builtin_circle(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& circles = std::get<std::shared_ptr<Array>>(args[0]);
        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 2 && std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
            colors = std::get<std::shared_ptr<Array>>(args[1]);
        }
        else if (args.size() > 1) {
            Error::set(15, vm.runtime_current_line, "Second argument for vectorized CIRCLE must be a color matrix.");
            return false;
        }
        vm.graphics_system.circle(circles, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 3 || args.size() == 4 || args.size() == 5 || args.size() > 6) {
        Error::set(8, vm.runtime_current_line, "Usage: CIRCLE x, y, r, [r, g, b] OR CIRCLE matrix, [colors]");
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    int radius = static_cast<int>(to_double(args[2]));

    if (args.size() == 6) {
        Uint8 r = static_cast<Uint8>(to_double(args[3]));
        Uint8 g = static_cast<Uint8>(to_double(args[4]));
        Uint8 b = static_cast<Uint8>(to_double(args[5]));
        vm.graphics_system.circle(x, y, radius, r, g, b);
    }
    else { // 3 args
        vm.graphics_system.circle(x, y, radius);
    }
    return false;
}

// TEXT x, y, content$, [r, g, b]
// Draws a string on the graphics screen.
BasicValue builtin_text(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // We need at least 3 arguments (x, y, content$)
    // and can have up to 6 (x, y, content$, r, g, b)
    if (args.size() < 3 || args.size() > 6) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    std::string content = to_string(args[2]);

    // Default color is white
    Uint8 r = 255, g = 255, b = 255;

    // If color arguments are provided, use them
    if (args.size() == 6) {
        r = static_cast<Uint8>(to_double(args[3]));
        g = static_cast<Uint8>(to_double(args[4]));
        b = static_cast<Uint8>(to_double(args[5]));
    }

    // Call the new method in our graphics system
    vm.graphics_system.text(x, y, content, r, g, b);

    return false; // Procedures return a dummy value
}

BasicValue builtin_plotraw(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. Validate Arguments
    if (args.size() > 5) {
        Error::set(8, vm.runtime_current_line, "PLOTRAW requires 3 arguments: x, y, matrix, scaleX, scaleY");
        return false;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
        Error::set(15, vm.runtime_current_line, "Third argument to PLOTRAW must be a matrix.");
        return false;
    }

    // 2. Parse Arguments
    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    int scaleX = static_cast<int>(to_double(args[3]));
    int scaleY = static_cast<int>(to_double(args[4]));

    const auto& matrix_ptr = std::get<std::shared_ptr<Array>>(args[2]);

    // 3. Call the Graphics System Method
    vm.graphics_system.plot_raw(x, y, matrix_ptr, scaleX, scaleY);

    return false; // Procedures return a dummy value
}

// --- TURTLE GRAPHICS PROCEDURES ---

// TURTLE.FORWARD distance
BasicValue builtin_turtle_forward(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float distance = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_forward(distance);
    return false;
}

// TURTLE.BACKWARD distance
BasicValue builtin_turtle_backward(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float distance = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_backward(distance);
    return false;
}

// TURTLE.LEFT degrees
BasicValue builtin_turtle_left(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float degrees = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_left(degrees);
    return false;
}

// TURTLE.RIGHT degrees
BasicValue builtin_turtle_right(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float degrees = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_right(degrees);
    return false;
}

// TURTLE.PENUP
BasicValue builtin_turtle_penup(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_penup();
    return false;
}

// TURTLE.PENDOWN
BasicValue builtin_turtle_pendown(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_pendown();
    return false;
}

// TURTLE.SETPOS x, y
BasicValue builtin_turtle_setpos(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return false; }
    float x = static_cast<float>(to_double(args[0]));
    float y = static_cast<float>(to_double(args[1]));
    vm.graphics_system.turtle_setpos(x, y);
    return false;
}

// TURTLE.SETHEADING degrees
BasicValue builtin_turtle_setheading(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float degrees = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_setheading(degrees);
    return false;
}

// TURTLE.HOME
BasicValue builtin_turtle_home(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    // We need screen dimensions for home, but we can get them from the renderer
    int w, h;
    SDL_GetRenderOutputSize(vm.graphics_system.renderer, &w, &h);
    vm.graphics_system.turtle_home(w, h);
    return false;
}

// TURTLE.DRAW
// Redraws the entire path the turtle has taken so far.
BasicValue builtin_turtle_draw(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_draw_path();
    return false;
}

// TURTLE.CLEAR
// Clears the turtle's path memory. Does not clear the screen.
BasicValue builtin_turtle_clear(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_clear_path();
    return false;
}

// TURTLE.SET_COLOR r, g, b
BasicValue builtin_turtle_set_color(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    Uint8 r = static_cast<Uint8>(to_double(args[0]));
    Uint8 g = static_cast<Uint8>(to_double(args[1]));
    Uint8 b = static_cast<Uint8>(to_double(args[2]));
    vm.graphics_system.turtle_set_color(r, g, b);
    return false;
}

// --- SDL Sound Functions ---

// SOUND.INIT
// Initializes the sound system. Must be called before any other sound command.
BasicValue builtin_sound_init(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }
    // Assumes `sound_system` is a member of your NeReLaBasic class `vm`
    if (!vm.sound_system.init(8)) { // Initialize with 8 tracks
        Error::set(1, vm.runtime_current_line, "Failed to initialize sound system.");
    }
    return false;
}

// SOUND.VOICE track, waveform$, attack, decay, sustain, release
// Configures the sound of a specific track.
BasicValue builtin_sound_voice(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 6) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    std::string waveform_str = to_upper(to_string(args[1]));
    double attack = to_double(args[2]);
    double decay = to_double(args[3]);
    double sustain = to_double(args[4]);
    double release = to_double(args[5]);

    if (waveform_map.find(waveform_str) == waveform_map.end()) {
        Error::set(1, vm.runtime_current_line, "Invalid waveform. Use SINE, SQUARE, SAW, or TRIANGLE.");
        return false;
    }
    Waveform wave = waveform_map.at(waveform_str);

    vm.sound_system.set_voice(track, wave, attack, decay, sustain, release);
    return false;
}

// SOUND.PLAY track, frequency
// Plays a note on a given track.
BasicValue builtin_sound_play(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    double freq = to_double(args[1]);
    vm.sound_system.play_note(track, freq);
    return false;
}

// SOUND.RELEASE track
// Starts the release phase of a note on a given track.
BasicValue builtin_sound_release(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    vm.sound_system.release_note(track);
    return false;
}

// SOUND.STOP track
// Immediately silences a note on a given track.
BasicValue builtin_sound_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    vm.sound_system.stop_note(track);
    return false;
}

// MOUSEX() -> returns the current X coordinate of the mouse
BasicValue builtin_mousex(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    if (!vm.graphics_system.is_initialized) {
        return 0.0; // Return 0 if graphics are not active
    }
    return static_cast<double>(vm.graphics_system.get_mouse_x());
}

// MOUSEY() -> returns the current Y coordinate of the mouse
BasicValue builtin_mousey(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    if (!vm.graphics_system.is_initialized) {
        return 0.0;
    }
    return static_cast<double>(vm.graphics_system.get_mouse_y());
}

// MOUSEB(button_index) -> returns TRUE or FALSE if the button is pressed
// 1 = Left, 2 = Middle, 3 = Right
BasicValue builtin_mouseb(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    if (!vm.graphics_system.is_initialized) {
        return false;
    }
    int button_index = static_cast<int>(to_double(args[0]));
    return vm.graphics_system.get_mouse_button_state(button_index);
}

// --- SPRITE PROCEDURES & FUNCTIONS ---

// SPRITE.LOAD type_id, "filename.png"
//BasicValue builtin_sprite_load(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
//    if (args.size() != 2) {
//        Error::set(8, vm.runtime_current_line);
//        return false;
//    }
//    int type_id = static_cast<int>(to_double(args[0]));
//    std::string filename = to_string(args[1]);
//
//    // The sprite system is a member of the graphics system
//    if (!vm.graphics_system.sprite_system.load_sprite_type(type_id, filename)) {
//        // The C++ function already prints a detailed error.
//        Error::set(1, vm.runtime_current_line, "Failed to load sprite.");
//    }
//    return false;
//}

// SPRITE.LOAD_ASEPRITE type_id, "filename.json"
// Loads a sprite sheet and animation data from an Aseprite export.
BasicValue builtin_sprite_load_aseprite(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.LOAD_ASEPRITE requires 2 arguments: type_id, filename$.");
        return false;
    }
    int type_id = static_cast<int>(to_double(args[0]));
    std::string filename = to_string(args[1]);

    // The sprite system is now a direct member of the VM
    if (!vm.graphics_system.sprite_system.load_aseprite_file(type_id, filename)) {
        Error::set(12, vm.runtime_current_line, "Failed to load Aseprite file. Check path and JSON format.");
    }
    return false; // Procedure
}

// SPRITE.CREATE(type_id, x, y) -> instance_id
// Creates an instance of a loaded sprite type.
BasicValue builtin_sprite_create(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "SPRITE.CREATE requires 3 arguments: type_id, x, y.");
        return -1.0; // Return -1 on error
    }
    int type_id = static_cast<int>(to_double(args[0]));
    float x = static_cast<float>(to_double(args[1]));
    float y = static_cast<float>(to_double(args[2]));

    int instance_id = vm.graphics_system.sprite_system.create_sprite(type_id, x, y);
    if (instance_id == -1) {
        Error::set(1, vm.runtime_current_line, "Failed to create sprite. Ensure the type_id has been loaded.");
    }
    return static_cast<double>(instance_id);
}

// SPRITE.SET_ANIMATION instance_id, "animation_name$"
// Sets the current animation for a sprite instance.
BasicValue builtin_sprite_set_animation(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.SET_ANIMATION requires 2 arguments: instance_id, animation_name$.");
        return false;
    }
    int instance_id = static_cast<int>(to_double(args[0]));
    std::string anim_name = to_string(args[1]);
    vm.graphics_system.sprite_system.set_animation(instance_id, anim_name);
    return false; // Procedure
}

// SPRITE.SET_FLIP instance_id, flip_boolean
// Sets the horizontal flip state of a sprite.
BasicValue builtin_sprite_set_flip(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.SET_FLIP requires 2 arguments: instance_id, flip_boolean.");
        return false;
    }
    int instance_id = static_cast<int>(to_double(args[0]));
    bool flip = to_bool(args[1]);
    vm.graphics_system.sprite_system.set_flip(instance_id, flip);
    return false; // Procedure
}

// SPRITE.MOVE instance_id, x, y
BasicValue builtin_sprite_move(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    int instance_id = static_cast<int>(to_double(args[0]));
    float x = static_cast<float>(to_double(args[1]));
    float y = static_cast<float>(to_double(args[2]));
    vm.graphics_system.sprite_system.move_sprite(instance_id, x, y);
    return false;
}

// SPRITE.SET_VELOCITY instance_id, vx, vy
BasicValue builtin_sprite_set_velocity(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    int instance_id = static_cast<int>(to_double(args[0]));
    float vx = static_cast<float>(to_double(args[1]));
    float vy = static_cast<float>(to_double(args[2]));
    vm.graphics_system.sprite_system.set_velocity(instance_id, vx, vy);
    return false;
}

// SPRITE.DELETE instance_id
BasicValue builtin_sprite_delete(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    int instance_id = static_cast<int>(to_double(args[0]));
    vm.graphics_system.sprite_system.delete_sprite(instance_id);
    return false;
}

// SPRITE.UPDATE [delta_time]
// Updates all sprites. Now accepts an optional delta_time for frame-rate independent physics.
BasicValue builtin_sprite_update(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() > 1) {
        Error::set(8, vm.runtime_current_line, "SPRITE.UPDATE accepts 0 or 1 argument: [delta_time].");
        return false;
    }
    float dt = 1.0f / 60.0f; // Default to 60 FPS if no delta is provided
    if (args.size() == 1) {
        dt = static_cast<float>(to_double(args[0]));
    }
    vm.graphics_system.sprite_system.update(dt);
    return false; // Procedure
}

// SPRITE.DRAW_ALL
BasicValue builtin_sprite_draw_all(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.sprite_system.draw_all();
    return false;
}

// SPRITE.GET_X(instance_id) -> x_coordinate
BasicValue builtin_sprite_get_x(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return 0.0; }
    int instance_id = static_cast<int>(to_double(args[0]));
    return static_cast<double>(vm.graphics_system.sprite_system.get_x(instance_id));
}

// SPRITE.GET_Y(instance_id) -> y_coordinate
BasicValue builtin_sprite_get_y(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return 0.0; }
    int instance_id = static_cast<int>(to_double(args[0]));
    return static_cast<double>(vm.graphics_system.sprite_system.get_y(instance_id));
}

// SPRITE.COLLISION(id1, id2) -> boolean
BasicValue builtin_sprite_collision(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return false; }
    int id1 = static_cast<int>(to_double(args[0]));
    int id2 = static_cast<int>(to_double(args[1]));
    return vm.graphics_system.sprite_system.check_collision(id1, id2);
}

// SPRITE.ADD_TO_GROUP group_id, instance_id
// Adds a sprite to a group.
BasicValue builtin_sprite_add_to_group(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.ADD_TO_GROUP requires 2 arguments: group_id, instance_id.");
        return false;
    }
    int group_id = static_cast<int>(to_double(args[0]));
    int instance_id = static_cast<int>(to_double(args[1]));
    vm.graphics_system.sprite_system.add_to_group(group_id, instance_id);
    return false; // Procedure
}

// SPRITE.COLLISION_GROUP(instance_id, group_id) -> hit_instance_id
// Checks for collision between a single sprite and a group.
BasicValue builtin_sprite_collision_group(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.COLLISION_GROUP requires 2 arguments: instance_id, group_id.");
        return -1.0;
    }
    int instance_id = static_cast<int>(to_double(args[0]));
    int group_id = static_cast<int>(to_double(args[1]));
    int hit_id = vm.graphics_system.sprite_system.check_collision_sprite_group(instance_id, group_id);
    return static_cast<double>(hit_id);
}

// SPRITE.CREATE_GROUP() -> group_id
// Creates a new, empty sprite group.
BasicValue builtin_sprite_create_group(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "SPRITE.CREATE_GROUP takes no arguments.");
        return -1.0;
    }
    return static_cast<double>(vm.graphics_system.sprite_system.create_group());
}

// SPRITE.COLLISION_GROUPS(group_id1, group_id2) -> array[hit_id1, hit_id2]
// Checks for collision between two groups of sprites.
BasicValue builtin_sprite_collision_groups(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.COLLISION_GROUPS requires 2 arguments: group_id1, group_id2.");
        return {}; // Return empty BasicValue
    }
    int group_id1 = static_cast<int>(to_double(args[0]));
    int group_id2 = static_cast<int>(to_double(args[1]));
    std::pair<int, int> hit_pair = vm.graphics_system.sprite_system.check_collision_groups(group_id1, group_id2);

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { 2 };
    result_ptr->data.push_back(static_cast<double>(hit_pair.first));
    result_ptr->data.push_back(static_cast<double>(hit_pair.second));
    return result_ptr;
}

// -- - TILEMAP PROCEDURES & FUNCTIONS-- -

// MAP.LOAD "map_name", "filename.json"
// Loads a Tiled map file.
BasicValue builtin_map_load(NeReLaBasic & vm, const std::vector<BasicValue>&args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "MAP.LOAD requires 2 arguments: map_name$, filename$.");
        return false;
    }
    std::string map_name = to_string(args[0]);
    std::string filename = to_string(args[1]);
    if (!vm.graphics_system.tilemap_system.load_map(map_name, filename)) {
        Error::set(12, vm.runtime_current_line, "Failed to load Tiled map file.");
    }
    return false; // Procedure
}

// MAP.DRAW_LAYER "map_name", "layer_name", [world_offset_x], [world_offset_y]
// Draws a specific tile layer from a loaded map.
BasicValue builtin_map_draw_layer(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 4) {
        Error::set(8, vm.runtime_current_line, "MAP.DRAW_LAYER requires 2 to 4 arguments: map_name$, layer_name$, [offsetX], [offsetY].");
        return false;
    }
    std::string map_name = to_string(args[0]);
    std::string layer_name = to_string(args[1]);
    int offset_x = 0;
    int offset_y = 0;
    if (args.size() >= 3) {
        offset_x = static_cast<int>(to_double(args[2]));
    }
    if (args.size() == 4) {
        offset_y = static_cast<int>(to_double(args[3]));
    }
    vm.graphics_system.tilemap_system.draw_layer(map_name, layer_name, offset_x, offset_y);
    return false; // Procedure
}

// MAP.GET_OBJECTS("map_name", "object_type") -> Array of Maps
// Retrieves all objects of a certain type from an object layer.
BasicValue builtin_map_get_objects(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "MAP.GET_OBJECTS requires 2 arguments: map_name$, object_type$.");
        return {};
    }
    std::string map_name = to_string(args[0]);
    std::string object_type = to_string(args[1]);

    auto objects_data = vm.graphics_system.tilemap_system.get_objects_by_type(map_name, object_type);

    auto results_array = std::make_shared<Array>();
    results_array->shape = { objects_data.size() };

    for (const auto& obj_map : objects_data) {
        auto map_ptr = std::make_shared<Map>();
        for (const auto& pair : obj_map) {
            // Tiled properties can be bool, float, or string. We'll try to parse intelligently.
            // For now, we just treat them all as strings for simplicity.
            map_ptr->data[pair.first] = pair.second;
        }
        results_array->data.push_back(map_ptr);
    }

    return results_array;
}

// MAP.COLLIDES(sprite_id, "map_name", "layer_name") -> boolean
// Checks if a sprite is colliding with any solid tile on a given layer.
BasicValue builtin_map_collides(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "MAP.COLLIDES requires 3 arguments: sprite_id, map_name$, layer_name$.");
        return false;
    }
    int sprite_id = static_cast<int>(to_double(args[0]));
    std::string map_name = to_string(args[1]);
    std::string layer_name = to_string(args[2]);

    return vm.graphics_system.tilemap_system.check_sprite_collision(sprite_id, vm.graphics_system.sprite_system, map_name, layer_name);
}


#endif

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
BasicValue builtin_mid_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "MID$ requires 2 or 3 arguments.");
        return std::string("");
    }

    const BasicValue& input = args[0];
    int start = static_cast<int>(to_double(args[1])) - 1; // BASIC is 1-indexed
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

// INSTR([start], haystack$, needle$) - Overloaded
BasicValue builtin_instr(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 3) return 0.0;

    size_t start_pos = 0;
    std::string haystack, needle;

    if (args.size() == 2) {
        haystack = to_string(args[0]);
        needle = to_string(args[1]);
    }
    else {
        start_pos = static_cast<size_t>(to_double(args[0])) - 1;
        haystack = to_string(args[1]);
        needle = to_string(args[2]);
    }

    if (start_pos >= haystack.length()) return 0.0;

    size_t found_pos = haystack.find(needle, start_pos);

    if (found_pos == std::string::npos) {
        return 0.0; // Not found
    }
    else {
        return static_cast<double>(found_pos + 1); // Return 1-indexed position
    }
}
// INKEY$()
BasicValue builtin_inkey(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return std::string("");
    }

    // --- ADDED: Context-aware logic ---
#ifdef SDL3
    // If the graphics system is initialized, get input from SDL's event queue.
    if (vm.graphics_system.is_initialized) {
        // We need to process events to populate our buffer.
        // The main execution loop already calls handle_events(),
        // so we just need to read from our buffer here.
        return vm.graphics_system.get_key_from_buffer();
    }
#endif

    // --- Original console-based logic (fallback) ---
    // If graphics are not active, use the old conio.h method.
    if (_kbhit()) {
        char c = _getch();
        return std::string(1, c);
    }

    return std::string("");
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

// CHR$(number)
BasicValue builtin_chr_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return std::string("");
    char c = static_cast<char>(static_cast<int>(to_double(args[0])));
    return std::string(1, c);
}

// ASC(string)
BasicValue builtin_asc(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) return 0.0;
    std::string s = to_string(args[0]);
    if (s.empty()) return 0.0;
    return static_cast<double>(static_cast<unsigned char>(s[0]));
}

// REVERSE(array) -> array
// Reverses the elements of an array along its last dimension.
BasicValue builtin_reverse(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line);
        return {};
    }
    const auto& source_array_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!source_array_ptr || source_array_ptr->data.empty()) return source_array_ptr;

    auto new_array_ptr = std::make_shared<Array>();
    new_array_ptr->shape = source_array_ptr->shape;

    size_t last_dim_size = source_array_ptr->shape.back();
    size_t num_slices = source_array_ptr->data.size() / last_dim_size;

    for (size_t i = 0; i < num_slices; ++i) {
        size_t slice_start = i * last_dim_size;
        // Get a copy of the slice to reverse
        std::vector<BasicValue> slice(
            source_array_ptr->data.begin() + slice_start,
            source_array_ptr->data.begin() + slice_start + last_dim_size
        );
        // Reverse it
        std::reverse(slice.begin(), slice.end());
        // Insert the reversed slice into the new data
        new_array_ptr->data.insert(new_array_ptr->data.end(), slice.begin(), slice.end());
    }

    return new_array_ptr;
}

/**
 * @brief Extracts a slice or a range of slices from an N-dimensional array.
 * @param vm The interpreter instance.
 * @param args A vector:
 * - 3 args: array, dimension, index (extracts one slice, reduces rank by 1)
 * - 4 args: array, dimension, index, count (extracts 'count' slices, preserves rank)
 * @return A new Array.
 */
BasicValue builtin_slice(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() < 3 || args.size() > 4) {
        Error::set(8, vm.runtime_current_line, "SLICE requires 3 or 4 arguments: array, dimension, index, [count]");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to SLICE must be an array.");
        return {};
    }

    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    int dimension = static_cast<int>(to_double(args[1]));
    int start_index = static_cast<int>(to_double(args[2]));
    int count = 1;
    bool reduce_rank = (args.size() == 3); // Only reduce rank for the original 3-arg call

    if (!reduce_rank) {
        count = static_cast<int>(to_double(args[3]));
    }

    // 2. --- Further Validation ---
    if (!source_ptr || source_ptr->shape.empty()) {
        Error::set(15, vm.runtime_current_line, "Cannot slice a null or empty array."); return {};
    }
    if (dimension < 0 || (size_t)dimension >= source_ptr->shape.size()) {
        Error::set(10, vm.runtime_current_line, "Slice dimension is out of bounds."); return {};
    }
    if (start_index < 0 || count < 0 || (size_t)(start_index + count) > source_ptr->shape[dimension]) {
        Error::set(10, vm.runtime_current_line, "Slice index or count is out of bounds for the given dimension."); return {};
    }
    if (count == 0) { // Return an empty array of the correct shape
        auto empty_ptr = std::make_shared<Array>();
        empty_ptr->shape = source_ptr->shape;
        empty_ptr->shape[dimension] = 0;
        return empty_ptr;
    }

    // 3. --- New Shape Calculation ---
    std::vector<size_t> new_shape = source_ptr->shape;
    if (reduce_rank) {
        new_shape.erase(new_shape.begin() + dimension);
        if (new_shape.empty()) new_shape.push_back(1); // Handle slicing a 1D vector to a scalar
    }
    else {
        new_shape[dimension] = count; // For range slice, just change the dimension size
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = new_shape;

    // 4. --- Data Copying Logic ---
    size_t outer_dims = 1;
    for (int i = 0; i < dimension; ++i) {
        outer_dims *= source_ptr->shape[i];
    }

    size_t inner_dims = 1;
    for (size_t i = dimension + 1; i < source_ptr->shape.size(); ++i) {
        inner_dims *= source_ptr->shape[i];
    }

    size_t source_dim_size = source_ptr->shape[dimension];
    size_t chunk_to_copy_size = count * inner_dims;
    result_ptr->data.reserve(outer_dims * chunk_to_copy_size);

    for (size_t i = 0; i < outer_dims; ++i) {
        size_t block_start_pos = (i * source_dim_size * inner_dims) + (start_index * inner_dims);
        result_ptr->data.insert(result_ptr->data.end(),
            source_ptr->data.begin() + block_start_pos,
            source_ptr->data.begin() + block_start_pos + chunk_to_copy_size);
    }

    return result_ptr;
}

// STACK(dimension, array1, array2, ...) -> matrix
// Stacks 1D vectors into a 2D matrix.
BasicValue builtin_stack(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() < 3) {
        Error::set(8, vm.runtime_current_line, "STACK requires at least 3 arguments: dimension, array1, array2, ...");
        return {};
    }

    int dimension = static_cast<int>(to_double(args[0]));
    if (dimension != 0 && dimension != 1) {
        Error::set(1, vm.runtime_current_line, "First argument (dimension) to STACK must be 0 (rows) or 1 (columns).");
        return {};
    }

    // 2. --- Collect and Validate Source Arrays ---
    std::vector<std::shared_ptr<Array>> source_arrays;
    for (size_t i = 1; i < args.size(); ++i) {
        if (!std::holds_alternative<std::shared_ptr<Array>>(args[i])) {
            Error::set(15, vm.runtime_current_line, "All arguments to STACK after dimension must be arrays.");
            return {};
        }
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[i]);
        if (!arr_ptr || arr_ptr->shape.size() != 1) {
            Error::set(15, vm.runtime_current_line, "All arrays passed to STACK must be 1D vectors.");
            return {};
        }
        source_arrays.push_back(arr_ptr);
    }

    if (source_arrays.empty()) {
        return {}; // Return empty if no arrays were provided
    }

    // Verify that all vectors have the same size
    size_t required_size = source_arrays[0]->data.size();
    for (size_t i = 1; i < source_arrays.size(); ++i) {
        if (source_arrays[i]->data.size() != required_size) {
            Error::set(15, vm.runtime_current_line, "All vectors in STACK must have the same length.");
            return {};
        }
    }

    auto result_ptr = std::make_shared<Array>();

    // 3. --- Row Stacking (dimension == 0) ---
    if (dimension == 0) {
        size_t rows = source_arrays.size();
        size_t cols = required_size;
        result_ptr->shape = { rows, cols };
        result_ptr->data.reserve(rows * cols);

        // Simply append the data from each vector
        for (const auto& arr_ptr : source_arrays) {
            result_ptr->data.insert(result_ptr->data.end(), arr_ptr->data.begin(), arr_ptr->data.end());
        }
    }
    // 4. --- Column Stacking (dimension == 1) ---
    else { // dimension == 1
        size_t rows = required_size;
        size_t cols = source_arrays.size();
        result_ptr->shape = { rows, cols };
        result_ptr->data.resize(rows * cols);

        // Interleave the data from the source vectors
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                result_ptr->data[r * cols + c] = source_arrays[c]->data[r];
            }
        }
    }

    return result_ptr;
}


// MVLET(matrix, dimension, index, vector) -> matrix
// Replaces a row or column in a matrix with a vector, returning a new matrix.
BasicValue builtin_mvlet(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 4) {
        Error::set(8, vm.runtime_current_line, "MVLET requires 4 arguments: matrix, dimension, index, vector");
        return {};
    }

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) ||
        !std::holds_alternative<std::shared_ptr<Array>>(args[3])) {
        Error::set(15, vm.runtime_current_line, "First and fourth arguments to MVLET must be arrays.");
        return {};
    }

    const auto& matrix_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    int dimension = static_cast<int>(to_double(args[1]));
    int index = static_cast<int>(to_double(args[2]));
    const auto& vector_ptr = std::get<std::shared_ptr<Array>>(args[3]);

    // 2. --- Further Validation ---
    if (!matrix_ptr || matrix_ptr->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line, "First argument to MVLET must be a 2D matrix.");
        return {};
    }
    if (!vector_ptr || vector_ptr->shape.size() != 1) {
        Error::set(15, vm.runtime_current_line, "Fourth argument to MVLET must be a 1D vector.");
        return {};
    }

    size_t rows = matrix_ptr->shape[0];
    size_t cols = matrix_ptr->shape[1];

    if (dimension != 0 && dimension != 1) {
        Error::set(1, vm.runtime_current_line, "Dimension for MVLET must be 0 (row) or 1 (column).");
        return {};
    }

    // 3. --- Create a copy of the matrix to modify ---
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = matrix_ptr->shape;
    result_ptr->data = matrix_ptr->data; // Make a full copy of the data

    // 4. --- Perform the replacement logic ---
    if (dimension == 0) { // Replace a row
        if (index < 0 || (size_t)index >= rows) {
            Error::set(10, vm.runtime_current_line, "Row index out of bounds for MVLET.");
            return {};
        }
        if (vector_ptr->data.size() != cols) {
            Error::set(15, vm.runtime_current_line, "Vector length must match the number of columns to replace a row.");
            return {};
        }

        size_t start_pos = (size_t)index * cols;
        for (size_t c = 0; c < cols; ++c) {
            result_ptr->data[start_pos + c] = vector_ptr->data[c];
        }
    }
    else { // dimension == 1, Replace a column
        if (index < 0 || (size_t)index >= cols) {
            Error::set(10, vm.runtime_current_line, "Column index out of bounds for MVLET.");
            return {};
        }
        if (vector_ptr->data.size() != rows) {
            Error::set(15, vm.runtime_current_line, "Vector length must match the number of rows to replace a column.");
            return {};
        }

        for (size_t r = 0; r < rows; ++r) {
            result_ptr->data[r * cols + (size_t)index] = vector_ptr->data[r];
        }
    }

    // 5. --- Return the new matrix ---
    return result_ptr;
}


// TRANSPOSE(matrix) -> matrix
// Transposes a 2D matrix.
BasicValue builtin_transpose(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line);
        return {};
    }
    const auto& source_array_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!source_array_ptr) return source_array_ptr;

    if (source_array_ptr->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line); // Or a more specific "Invalid rank for transpose" error
        return {};
    }

    size_t rows = source_array_ptr->shape[0];
    size_t cols = source_array_ptr->shape[1];

    auto new_array_ptr = std::make_shared<Array>();
    new_array_ptr->shape = { cols, rows }; // New shape is inverted
    new_array_ptr->data.resize(rows * cols);

    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            // New position (c, r) gets data from old position (r, c)
            new_array_ptr->data[c * rows + r] = source_array_ptr->data[r * cols + c];
        }
    }

    return new_array_ptr;
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

// FRMV$(array) -> string$
// Formats a 1D or 2D array into a right-aligned string matrix.
BasicValue builtin_frmv_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. Argument Validation
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
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
    std::stringstream ss;
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

    return ss.str();
}

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

                    if constexpr (std::is_same_v<T, double>) {
                        // Check the format specifier to see if an integer type is requested.
                        size_t spec_end_pos = format_specifier.length() - 1;
                        if (spec_end_pos > 0 && format_specifier[spec_end_pos] == '}') {
                            char type_char = format_specifier[spec_end_pos - 1];
                            // Check for integer-only types: d, x, X, b, B, o, c
                            if (std::string("dxXbo").find(type_char) != std::string::npos) {
                                // It's an integer format. Cast the double to a long long.
                                return std::vformat(LocaleManager::get_current_locale(), format_specifier, std::make_format_args(static_cast<long long>(value)));
                            }
                            if (type_char == 'c') {
                                // Handle character case
                                return std::vformat(LocaleManager::get_current_locale(), format_specifier, std::make_format_args(static_cast<char>(static_cast<long long>(value))));
                            }
                        }
                        // If it's not an integer type, format as a double.
                        return std::vformat(LocaleManager::get_current_locale(), format_specifier, std::make_format_args(value));
                    }
                    else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, std::string>) {
                        // These types are fine as they are
                        return std::vformat(LocaleManager::get_current_locale(), format_specifier, std::make_format_args(value));
                    }
                    else {
                        // Fallback for complex types (Array, Map, etc.)
                        return to_string(value);
                    }
                    }, arg);
            }
            catch (const std::format_error& e) {
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

// --- Vector and Matrix functions

// --- Array Reduction Functions ---

// Helper macro to reduce boilerplate for numeric reduction functions
#define NUMERIC_REDUCTION_BOILERPLATE(function_name, error_code_on_mismatch) \
    if (args.size() != 1) { \
        Error::set(8, vm.runtime_current_line); \
        return 0.0; \
    } \
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { \
        Error::set(15, vm.runtime_current_line); \
        return 0.0; \
    } \
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]); \
    if (!arr_ptr || arr_ptr->data.empty()) { \
        return 0.0; \
    }

// ROTATE(array, shift_vector) -> array
// Cyclically shifts an N-dimensional array.
BasicValue builtin_rotate(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "ROTATE requires 2 arguments: array, shift_vector");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Both arguments to ROTATE must be arrays.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& shift_vec_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!source_ptr || !shift_vec_ptr || source_ptr->data.empty()) {
        return source_ptr; // Return original array if source/shift is null or empty
    }
    if (source_ptr->shape.size() != shift_vec_ptr->data.size()) {
        Error::set(15, vm.runtime_current_line, "Shift vector must have one element for each dimension of the source array.");
        return {};
    }

    // 2. --- Setup ---
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = source_ptr->shape;
    result_ptr->data.resize(source_ptr->data.size()); // Pre-allocate result data

    const auto& shape = source_ptr->shape;
    std::vector<long long> shifts;
    for (const auto& val : shift_vec_ptr->data) {
        shifts.push_back(static_cast<long long>(to_double(val)));
    }

    // 3. --- Main Logic ---
    // Iterate through each element of the source array by its linear index
    for (size_t source_linear_idx = 0; source_linear_idx < source_ptr->data.size(); ++source_linear_idx) {

        // Convert source linear index to N-dimensional coordinates
        std::vector<long long> source_coords(shape.size());
        size_t temp_idx = source_linear_idx;
        for (int d = shape.size() - 1; d >= 0; --d) {
            source_coords[d] = temp_idx % shape[d];
            temp_idx /= shape[d];
        }

        // Calculate destination coordinates with cyclic rotation
        std::vector<long long> dest_coords = source_coords;
        for (size_t d = 0; d < shape.size(); ++d) {
            long long dim_size = static_cast<long long>(shape[d]);
            // The (a % n + n) % n trick handles negative shifts correctly
            dest_coords[d] = (source_coords[d] + shifts[d]) % dim_size;
            if (dest_coords[d] < 0) {
                dest_coords[d] += dim_size;
            }
        }

        // Convert destination coordinates back to a linear index
        size_t dest_linear_idx = 0;
        size_t multiplier = 1;
        for (int d = shape.size() - 1; d >= 0; --d) {
            dest_linear_idx += dest_coords[d] * multiplier;
            multiplier *= shape[d];
        }

        // Copy the value
        result_ptr->data[dest_linear_idx] = source_ptr->data[source_linear_idx];
    }

    return result_ptr;
}

// SHIFT(array, shift_vector, [fill_value]) -> array
// Non-cyclically shifts an N-dimensional array.
BasicValue builtin_shift(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() < 2 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "SHIFT requires 2 or 3 arguments: array, shift_vector, [fill_value]");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "First two arguments to SHIFT must be arrays.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& shift_vec_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!source_ptr || !shift_vec_ptr || source_ptr->data.empty()) {
        return source_ptr;
    }
    if (source_ptr->shape.size() != shift_vec_ptr->data.size()) {
        Error::set(15, vm.runtime_current_line, "Shift vector must have one element for each dimension of the source array.");
        return {};
    }

    // 2. --- Setup ---
    BasicValue fill_value = 0.0; // Default fill value
    if (args.size() == 3) {
        fill_value = args[2];
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = source_ptr->shape;
    // Initialize the entire result array with the fill value
    result_ptr->data.assign(source_ptr->data.size(), fill_value);

    const auto& shape = source_ptr->shape;
    std::vector<long long> shifts;
    for (const auto& val : shift_vec_ptr->data) {
        shifts.push_back(static_cast<long long>(to_double(val)));
    }

    // 3. --- Main Logic ---
    for (size_t source_linear_idx = 0; source_linear_idx < source_ptr->data.size(); ++source_linear_idx) {

        // Convert source linear index to N-dimensional coordinates
        std::vector<long long> source_coords(shape.size());
        size_t temp_idx = source_linear_idx;
        for (int d = shape.size() - 1; d >= 0; --d) {
            source_coords[d] = temp_idx % shape[d];
            temp_idx /= shape[d];
        }

        // Calculate destination coordinates
        std::vector<long long> dest_coords = source_coords;
        bool is_in_bounds = true;
        for (size_t d = 0; d < shape.size(); ++d) {
            dest_coords[d] = source_coords[d] + shifts[d];
            // Check if the destination is out of bounds
            if (dest_coords[d] < 0 || dest_coords[d] >= static_cast<long long>(shape[d])) {
                is_in_bounds = false;
                break;
            }
        }

        if (is_in_bounds) {
            // Convert destination coordinates back to a linear index
            size_t dest_linear_idx = 0;
            size_t multiplier = 1;
            for (int d = shape.size() - 1; d >= 0; --d) {
                dest_linear_idx += dest_coords[d] * multiplier;
                multiplier *= shape[d];
            }
            // Copy the value
            result_ptr->data[dest_linear_idx] = source_ptr->data[source_linear_idx];
        }
        // If out of bounds, the value is discarded (and the cell keeps its fill_value).
    }

    return result_ptr;
}

// CONVOLVE(array, kernel, wrap_mode) -> array
// Performs a 2D convolution of an array with a kernel.
BasicValue builtin_convolve(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "CONVOLVE requires 3 arguments: array, kernel, wrap_mode");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "First two arguments to CONVOLVE must be arrays.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& kernel_ptr = std::get<std::shared_ptr<Array>>(args[1]);

    if (!source_ptr || !kernel_ptr) {
        Error::set(15, vm.runtime_current_line, "Cannot perform convolution on a null array.");
        return {};
    }
    if (source_ptr->shape.size() != 2 || kernel_ptr->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line, "CONVOLVE currently only supports 2D arrays.");
        return {};
    }

    // 2. --- Setup ---
    bool wrap_mode = to_bool(args[2]);

    long long source_h = source_ptr->shape[0];
    long long source_w = source_ptr->shape[1];
    long long kernel_h = kernel_ptr->shape[0];
    long long kernel_w = kernel_ptr->shape[1];

    // The center of the kernel (integer division)
    long long kernel_center_y = kernel_h / 2;
    long long kernel_center_x = kernel_w / 2;

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = source_ptr->shape;
    result_ptr->data.resize(source_ptr->data.size());

    // 3. --- Main Convolution Loop ---
    // Iterate over every pixel of the source/output array
    for (long long y = 0; y < source_h; ++y) {
        for (long long x = 0; x < source_w; ++x) {

            double sum = 0.0;
            // Iterate over every element of the kernel
            for (long long ky = 0; ky < kernel_h; ++ky) {
                for (long long kx = 0; kx < kernel_w; ++kx) {

                    // Calculate the corresponding coordinate in the source array to sample from.
                    // This is the source pixel that aligns with the current kernel pixel.
                    long long source_sample_y = y + ky - kernel_center_y;
                    long long source_sample_x = x + kx - kernel_center_x;

                    double source_val = 0.0;

                    // Handle edge conditions
                    if (wrap_mode) {
                        // Toroidal wrapping: use modulo arithmetic
                        long long wrapped_y = (source_sample_y % source_h + source_h) % source_h;
                        long long wrapped_x = (source_sample_x % source_w + source_w) % source_w;
                        source_val = to_double(source_ptr->data[wrapped_y * source_w + wrapped_x]);
                    }
                    else {
                        // Non-wrapping: check if the sample is within bounds
                        if (source_sample_y >= 0 && source_sample_y < source_h &&
                            source_sample_x >= 0 && source_sample_x < source_w) {
                            source_val = to_double(source_ptr->data[source_sample_y * source_w + source_sample_x]);
                        }
                        // If out of bounds, source_val remains 0.0 (zero-padding)
                    }

                    double kernel_val = to_double(kernel_ptr->data[ky * kernel_w + kx]);
                    sum += source_val * kernel_val;
                }
            }
            // Store the final calculated sum in the output array
            result_ptr->data[y * source_w + x] = sum;
        }
    }
    return result_ptr;
}

// PLACE(destination_array, source_array, coordinates_vector) -> array
// Places a source array into a destination array at a given coordinate.
BasicValue builtin_place(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "PLACE requires 3 arguments: destination_array, source_array, coords_vector");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) ||
        !std::holds_alternative<std::shared_ptr<Array>>(args[1]) ||
        !std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
        Error::set(15, vm.runtime_current_line, "All arguments to PLACE must be arrays.");
        return {};
    }

    const auto& dest_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    const auto& coords_ptr = std::get<std::shared_ptr<Array>>(args[2]);

    if (!dest_ptr || !source_ptr || !coords_ptr) {
        Error::set(15, vm.runtime_current_line, "Arguments to PLACE cannot be null arrays.");
        return {};
    }
    if (dest_ptr->shape.size() != 2 || source_ptr->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line, "PLACE currently only supports 2D arrays.");
        return {};
    }
    if (coords_ptr->data.size() != 2) {
        Error::set(15, vm.runtime_current_line, "Coordinate vector for PLACE must have two elements [row, col].");
        return {};
    }

    // 2. --- Setup and Bounds Checking ---
    long long dest_h = dest_ptr->shape[0];
    long long dest_w = dest_ptr->shape[1];
    long long source_h = source_ptr->shape[0];
    long long source_w = source_ptr->shape[1];

    long long start_row = static_cast<long long>(to_double(coords_ptr->data[0]));
    long long start_col = static_cast<long long>(to_double(coords_ptr->data[1]));

    if (start_row + source_h > dest_h || start_col + source_w > dest_w) {
        Error::set(10, vm.runtime_current_line, "Source array does not fit in destination at specified coordinates.");
        return {};
    }

    // 3. --- Create a copy and perform the placement ---
    auto result_ptr = std::make_shared<Array>(*dest_ptr); // Create a deep copy to modify

    for (long long sy = 0; sy < source_h; ++sy) {
        for (long long sx = 0; sx < source_w; ++sx) {
            long long dy = start_row + sy;
            long long dx = start_col + sx;

            // Calculate flat indices for source and destination
            size_t source_idx = sy * source_w + sx;
            size_t dest_idx = dy * dest_w + dx;

            result_ptr->data[dest_idx] = source_ptr->data[source_idx];
        }
    }

    return result_ptr;
}

// PRODUCT(array, [dimension]) -> number or array
BasicValue builtin_product(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1 || args.size() > 2) { Error::set(8, vm.runtime_current_line); return 1.0; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { Error::set(15, vm.runtime_current_line, "First argument to PRODUCT must be an array."); return 1.0; }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) { return 1.0; } // Product of empty array is 1

    if (args.size() == 1) {
        double total = 1.0;
        for (const auto& val : arr_ptr->data) { total *= to_double(val); }
        return total;
    }

    if (arr_ptr->shape.size() != 2) { Error::set(15, vm.runtime_current_line, "Dimensional reduction currently only supports 2D matrices."); return 1.0; }
    int dimension = static_cast<int>(to_double(args[1]));
    size_t rows = arr_ptr->shape[0];
    size_t cols = arr_ptr->shape[1];
    auto result_ptr = std::make_shared<Array>();

    if (dimension == 0) { // Reduce along rows
        result_ptr->shape = { 1, cols };
        result_ptr->data.assign(cols, 1.0); // Initialize with ones
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                result_ptr->data[c] = to_double(result_ptr->data[c]) * to_double(arr_ptr->data[r * cols + c]);
            }
        }
    }
    else if (dimension == 1) { // Reduce along columns
        result_ptr->shape = { rows, 1 };
        result_ptr->data.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            double row_total = 1.0;
            for (size_t c = 0; c < cols; ++c) { row_total *= to_double(arr_ptr->data[r * cols + c]); }
            result_ptr->data.push_back(row_total);
        }
    }
    else { Error::set(1, vm.runtime_current_line, "Invalid dimension for reduction. Must be 0 or 1."); return 1.0; }
    return result_ptr;
}

// SUM(array, [dimension]) -> number or array
BasicValue array_sum(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() < 1 || args.size() > 2) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return 0.0;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to SUM must be an array.");
        return 0.0;
    }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) {
        return 0.0; // Sum of empty array is 0
    }

    // 2. --- Backward Compatibility: Reduce to Scalar ---
    if (args.size() == 1) {
        double total = 0.0;
        for (const auto& val : arr_ptr->data) {
            total += to_double(val);
        }
        return total;
    }

    // 3. --- New Functionality: Reduce along a Dimension ---
    if (arr_ptr->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line, "Dimensional reduction currently only supports 2D matrices.");
        return 0.0;
    }
    int dimension = static_cast<int>(to_double(args[1]));
    size_t rows = arr_ptr->shape[0];
    size_t cols = arr_ptr->shape[1];

    auto result_ptr = std::make_shared<Array>();

    if (dimension == 0) { // Reduce along rows -> result is a row vector of size 'cols'
        result_ptr->shape = { 1, cols };
        result_ptr->data.assign(cols, 0.0); // Initialize with zeros
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                result_ptr->data[c] = to_double(result_ptr->data[c]) + to_double(arr_ptr->data[r * cols + c]);
            }
        }
    }
    else if (dimension == 1) { // Reduce along columns -> result is a column vector of size 'rows'
        result_ptr->shape = { rows, 1 };
        result_ptr->data.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            double row_total = 0.0;
            for (size_t c = 0; c < cols; ++c) {
                row_total += to_double(arr_ptr->data[r * cols + c]);
            }
            result_ptr->data.push_back(row_total);
        }
    }
    else {
        Error::set(1, vm.runtime_current_line, "Invalid dimension for reduction. Must be 0 or 1.");
        return 0.0;
    }

    return result_ptr;
}

// MIN(array, [dimension]) -> number or array
BasicValue builtin_min(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1 || args.size() > 2) { Error::set(8, vm.runtime_current_line); return 0.0; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { Error::set(15, vm.runtime_current_line, "First argument to MIN must be an array."); return 0.0; }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) { return 0.0; }

    if (args.size() == 1) {
        BasicValue min_val = arr_ptr->data[0];
        for (size_t i = 1; i < arr_ptr->data.size(); ++i) {
            if (to_double(arr_ptr->data[i]) < to_double(min_val)) { min_val = arr_ptr->data[i]; }
        }
        return min_val;
    }

    if (arr_ptr->shape.size() != 2) { Error::set(15, vm.runtime_current_line, "Dimensional reduction currently only supports 2D matrices."); return 0.0; }
    int dimension = static_cast<int>(to_double(args[1]));
    size_t rows = arr_ptr->shape[0];
    size_t cols = arr_ptr->shape[1];
    auto result_ptr = std::make_shared<Array>();

    if (dimension == 0) { // Reduce along rows
        result_ptr->shape = { 1, cols };
        result_ptr->data.assign(arr_ptr->data.begin(), arr_ptr->data.begin() + cols); // Initialize with first row
        for (size_t r = 1; r < rows; ++r) { // Start from the second row
            for (size_t c = 0; c < cols; ++c) {
                if (to_double(arr_ptr->data[r * cols + c]) < to_double(result_ptr->data[c])) { result_ptr->data[c] = arr_ptr->data[r * cols + c]; }
            }
        }
    }
    else if (dimension == 1) { // Reduce along columns
        result_ptr->shape = { rows, 1 };
        result_ptr->data.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            BasicValue row_min = arr_ptr->data[r * cols]; // Initialize with first element of the row
            for (size_t c = 1; c < cols; ++c) { // Start from second element
                if (to_double(arr_ptr->data[r * cols + c]) < to_double(row_min)) { row_min = arr_ptr->data[r * cols + c]; }
            }
            result_ptr->data.push_back(row_min);
        }
    }
    else { Error::set(1, vm.runtime_current_line, "Invalid dimension for reduction. Must be 0 or 1."); return 0.0; }
    return result_ptr;
}

// MAX(array, [dimension]) -> number or array
BasicValue builtin_max(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // This implementation is identical to MIN, just with the > operator
    if (args.size() < 1 || args.size() > 2) { Error::set(8, vm.runtime_current_line); return 0.0; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { Error::set(15, vm.runtime_current_line, "First argument to MAX must be an array."); return 0.0; }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) { return 0.0; }

    if (args.size() == 1) {
        BasicValue max_val = arr_ptr->data[0];
        for (size_t i = 1; i < arr_ptr->data.size(); ++i) {
            if (to_double(arr_ptr->data[i]) > to_double(max_val)) { max_val = arr_ptr->data[i]; }
        }
        return max_val;
    }

    if (arr_ptr->shape.size() != 2) { Error::set(15, vm.runtime_current_line, "Dimensional reduction currently only supports 2D matrices."); return 0.0; }
    int dimension = static_cast<int>(to_double(args[1]));
    size_t rows = arr_ptr->shape[0];
    size_t cols = arr_ptr->shape[1];
    auto result_ptr = std::make_shared<Array>();

    if (dimension == 0) { // Reduce along rows
        result_ptr->shape = { 1, cols };
        result_ptr->data.assign(arr_ptr->data.begin(), arr_ptr->data.begin() + cols); // Initialize with first row
        for (size_t r = 1; r < rows; ++r) { // Start from the second row
            for (size_t c = 0; c < cols; ++c) {
                if (to_double(arr_ptr->data[r * cols + c]) > to_double(result_ptr->data[c])) { result_ptr->data[c] = arr_ptr->data[r * cols + c]; }
            }
        }
    }
    else if (dimension == 1) { // Reduce along columns
        result_ptr->shape = { rows, 1 };
        result_ptr->data.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            BasicValue row_max = arr_ptr->data[r * cols]; // Initialize with first element of the row
            for (size_t c = 1; c < cols; ++c) { // Start from second element
                if (to_double(arr_ptr->data[r * cols + c]) > to_double(row_max)) { row_max = arr_ptr->data[r * cols + c]; }
            }
            result_ptr->data.push_back(row_max);
        }
    }
    else { Error::set(1, vm.runtime_current_line, "Invalid dimension for reduction. Must be 0 or 1."); return 0.0; }
    return result_ptr;
}

// Helper macro for boolean reduction functions
#define BOOLEAN_REDUCTION_BOILERPLATE(function_name, error_code_on_mismatch) \
    if (args.size() != 1) { \
        Error::set(8, vm.runtime_current_line); \
        return false; \
    } \
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { \
        Error::set(15, vm.runtime_current_line); \
        return false; \
    } \
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]); \
    if (!arr_ptr) return false;

// ANY(array, [dimension]) -> boolean or array
BasicValue builtin_any(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1 || args.size() > 2) { Error::set(8, vm.runtime_current_line); return false; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { 
        Error::set(15, vm.runtime_current_line, "First argument to ANY must be an array."); 
        return false; 
    }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) { return false; } // ANY of empty is false

    if (args.size() == 1) {
        for (const auto& val : arr_ptr->data) { if (to_bool(val)) return true; }
        return false;
    }

    if (arr_ptr->shape.size() != 2) { Error::set(15, vm.runtime_current_line, "Dimensional reduction currently only supports 2D matrices."); return false; }
    int dimension = static_cast<int>(to_double(args[1]));
    size_t rows = arr_ptr->shape[0];
    size_t cols = arr_ptr->shape[1];
    auto result_ptr = std::make_shared<Array>();

    if (dimension == 0) { // Reduce along rows
        result_ptr->shape = { 1, cols };
        result_ptr->data.assign(cols, false); // Initialize with false
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) { result_ptr->data[c] = to_bool(result_ptr->data[c]) || to_bool(arr_ptr->data[r * cols + c]); }
        }
    }
    else if (dimension == 1) { // Reduce along columns
        result_ptr->shape = { rows, 1 };
        result_ptr->data.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            bool row_any = false;
            for (size_t c = 0; c < cols; ++c) { if (to_bool(arr_ptr->data[r * cols + c])) { row_any = true; break; } }
            result_ptr->data.push_back(row_any);
        }
    }
    else { Error::set(1, vm.runtime_current_line, "Invalid dimension for reduction. Must be 0 or 1."); return false; }
    return result_ptr;
}

// ALL(array, [dimension]) -> boolean or array
BasicValue builtin_all(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1 || args.size() > 2) { Error::set(8, vm.runtime_current_line); return true; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { Error::set(15, vm.runtime_current_line, "First argument to ALL must be an array."); return true; }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->data.empty()) { return true; } // ALL of empty is true

    if (args.size() == 1) {
        for (const auto& val : arr_ptr->data) { if (!to_bool(val)) return false; }
        return true;
    }

    if (arr_ptr->shape.size() != 2) { Error::set(15, vm.runtime_current_line, "Dimensional reduction currently only supports 2D matrices."); return true; }
    int dimension = static_cast<int>(to_double(args[1]));
    size_t rows = arr_ptr->shape[0];
    size_t cols = arr_ptr->shape[1];
    auto result_ptr = std::make_shared<Array>();

    if (dimension == 0) { // Reduce along rows
        result_ptr->shape = { 1, cols };
        result_ptr->data.assign(cols, true); // Initialize with true
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) { result_ptr->data[c] = to_bool(result_ptr->data[c]) && to_bool(arr_ptr->data[r * cols + c]); }
        }
    }
    else if (dimension == 1) { // Reduce along columns
        result_ptr->shape = { rows, 1 };
        result_ptr->data.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            bool row_all = true;
            for (size_t c = 0; c < cols; ++c) { if (!to_bool(arr_ptr->data[r * cols + c])) { row_all = false; break; } }
            result_ptr->data.push_back(row_all);
        }
    }
    else { Error::set(1, vm.runtime_current_line, "Invalid dimension for reduction. Must be 0 or 1."); return true; }
    return result_ptr;
}

// SCAN(operator, array) -> array
// Performs a cumulative reduction (scan) along the last axis of an array.
BasicValue builtin_scan(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SCAN requires 2 arguments: operator, array");
        return {};
    }
    const BasicValue& op_arg = args[0];
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Second argument to SCAN must be an array.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!source_ptr || source_ptr->data.empty()) {
        return source_ptr; // Return original array if null or empty
    }

    // 2. --- Setup ---
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = source_ptr->shape;
    result_ptr->data.resize(source_ptr->data.size());

    size_t last_dim_size = source_ptr->shape.back();
    if (last_dim_size == 0) {
        return source_ptr; // Nothing to scan
    }
    size_t num_slices = source_ptr->data.size() / last_dim_size;

    // 3. --- Operator Logic ---
    // The main loop iterates through each slice (e.g., each row in a 2D matrix)
    for (size_t i = 0; i < num_slices; ++i) {
        size_t slice_start_idx = i * last_dim_size;

        // The accumulator holds the cumulative result for the current slice.
        // Initialize it with the first element of the slice.
        BasicValue accumulator = source_ptr->data[slice_start_idx];
        result_ptr->data[slice_start_idx] = accumulator;

        // Loop through the rest of the slice (from the second element onwards)
        for (size_t j = 1; j < last_dim_size; ++j) {
            size_t current_idx = slice_start_idx + j;
            const BasicValue& current_val = source_ptr->data[current_idx];

            // --- Apply the operator ---
            if (std::holds_alternative<std::string>(op_arg)) {
                const std::string op = to_upper(std::get<std::string>(op_arg));
                double acc_d = to_double(accumulator);
                double cur_d = to_double(current_val);

                if (op == "+") accumulator = acc_d + cur_d;
                else if (op == "-") accumulator = acc_d - cur_d;
                else if (op == "*") accumulator = acc_d * cur_d;
                else if (op == "/") {
                    if (cur_d == 0.0) { Error::set(2, vm.runtime_current_line); return {}; }
                    accumulator = acc_d / cur_d;
                }
                else if (op == "MIN") accumulator = std::min(acc_d, cur_d);
                else if (op == "MAX") accumulator = std::max(acc_d, cur_d);
                else { Error::set(1, vm.runtime_current_line, "Invalid operator string for SCAN: " + op); return {}; }
            }
            else if (std::holds_alternative<FunctionRef>(op_arg)) {
                const std::string func_name = to_upper(std::get<FunctionRef>(op_arg).name);
                if (!vm.active_function_table->count(func_name)) {
                    Error::set(22, vm.runtime_current_line, "Operator function '" + func_name + "' not found.");
                    return {};
                }
                const auto& func_info = vm.active_function_table->at(func_name);
                if (func_info.arity != 2) {
                    Error::set(26, vm.runtime_current_line, "Operator function '" + func_name + "' must accept exactly two arguments.");
                    return {};
                }
                std::vector<BasicValue> func_args = { accumulator, current_val };
                accumulator = vm.execute_function_for_value(func_info, func_args);
                if (Error::get() != 0) return {}; // Propagate error
            }
            else {
                Error::set(15, vm.runtime_current_line, "First argument to SCAN must be an operator string or a function reference.");
                return {};
            }

            result_ptr->data[current_idx] = accumulator;
        }
    }

    return result_ptr;
}

// REDUCE(function@, array, [initial_value]) -> value
// Performs a cumulative reduction on an array using a user-provided function.
BasicValue builtin_reduce(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() < 2 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "REDUCE requires 2 or 3 arguments: function_ref, array, [initial_value]");
        return {};
    }
    if (!std::holds_alternative<FunctionRef>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to REDUCE must be a function reference (e.g., MyFunc@).");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Second argument to REDUCE must be an array.");
        return {};
    }

    // 2. --- Argument Parsing ---
    const auto& func_ref = std::get<FunctionRef>(args[0]);
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    const std::string func_name = to_upper(func_ref.name);

    // 3. --- Further Validation ---
    // Check if the provided function exists and has the correct signature (2 arguments).
    if (!vm.active_function_table->count(func_name)) {
        Error::set(22, vm.runtime_current_line, "Function '" + func_name + "' not found for REDUCE.");
        return {};
    }
    const auto& func_info = vm.active_function_table->at(func_name);
    if (func_info.arity != 2) {
        Error::set(26, vm.runtime_current_line, "Function '" + func_name + "' must accept exactly two arguments (accumulator, current_value).");
        return {};
    }

    // Handle empty or null arrays. An initial value is required in these cases.
    if (!arr_ptr || arr_ptr->data.empty()) {
        if (args.size() == 3) {
            return args[2]; // If an initial value is given, return it.
        }
        else {
            Error::set(15, vm.runtime_current_line, "Cannot reduce a null or empty array without an initial value.");
            return {};
        }
    }

    // 4. --- Reduction Logic ---
    BasicValue accumulator;
    size_t start_index = 0;

    // Determine the starting value for the accumulator.
    if (args.size() == 3) {
        // An initial value was provided by the user.
        accumulator = args[2];
        start_index = 0;
    }
    else {
        // No initial value provided; use the first element of the array.
        accumulator = arr_ptr->data[0];
        start_index = 1;
    }

    // Iterate through the array elements, applying the user's function.
    for (size_t i = start_index; i < arr_ptr->data.size(); ++i) {
        const BasicValue& current_element = arr_ptr->data[i];

        // Prepare the two arguments to pass to the user's BASIC function.
        std::vector<BasicValue> func_args = { accumulator, current_element };

        // Execute the user's function and update the accumulator with the result.
        accumulator = vm.execute_function_for_value(func_info, func_args);

        // If the user's function caused an error, stop and propagate it.
        if (Error::get() != 0) {
            return {};
        }
    }

    // Return the final accumulated value.
    return accumulator;
}

// IOTA(N) -> vector
// Generates a vector of numbers from 1 to N.
BasicValue builtin_iota(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return {}; // Return an empty BasicValue
    }
    int count = static_cast<int>(to_double(args[0]));
    if (count < 0) count = 0;

    auto new_array_ptr = std::make_shared<Array>();
    new_array_ptr->shape = { (size_t)count };
    new_array_ptr->data.reserve(count);

    for (int i = 1; i <= count; ++i) {
        new_array_ptr->data.push_back(static_cast<double>(i));
    }

    return new_array_ptr;
}

// RESHAPE(source_array, shape_vector) -> array
// Creates a new array with a new shape from the data of a source array.
BasicValue builtin_reshape(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return {};
    }

    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line); // Type Mismatch
        return {};
    }

    const auto& source_array_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& shape_vector_ptr = std::get<std::shared_ptr<Array>>(args[1]);

    if (!source_array_ptr || !shape_vector_ptr) return {}; // Null pointers

    // Create the new shape from the shape_vector
    std::vector<size_t> new_shape;
    for (const auto& val : shape_vector_ptr->data) {
        new_shape.push_back(static_cast<size_t>(to_double(val)));
    }

    auto new_array_ptr = std::make_shared<Array>();
    new_array_ptr->shape = new_shape;
    size_t new_total_size = new_array_ptr->size();
    new_array_ptr->data.reserve(new_total_size);

    // APL's reshape cycles through the source data if needed.
    if (source_array_ptr->data.empty()) {
        new_array_ptr->data.assign(new_total_size, 0.0); // Fill with default if source is empty
    }
    else {
        for (size_t i = 0; i < new_total_size; ++i) {
            new_array_ptr->data.push_back(source_array_ptr->data[i % source_array_ptr->data.size()]);
        }
    }

    return new_array_ptr;
}


// OUTER(arrayA, arrayB, operator_string_or_funcref) -> array
BasicValue builtin_outer(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. Argument validation
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "OUTER requires 3 arguments: arrayA, arrayB, operator");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "First two arguments to OUTER must be arrays.");
        return {};
    }
    const auto& a_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& b_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!a_ptr || !b_ptr) return {};

    // 2. Prepare the result array
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = a_ptr->shape;
    result_ptr->shape.insert(result_ptr->shape.end(), b_ptr->shape.begin(), b_ptr->shape.end());
    result_ptr->data.reserve(a_ptr->data.size() * b_ptr->data.size());

    const BasicValue& op_arg = args[2];

    // 3. Check if the operator is a string
    if (std::holds_alternative<std::string>(op_arg)) {
        const std::string op = to_upper(std::get<std::string>(op_arg));
        for (const auto& val_a : a_ptr->data) {
            for (const auto& val_b : b_ptr->data) {
                double num_a = to_double(val_a);
                double num_b = to_double(val_b);
                if (op == "+") result_ptr->data.push_back(num_a + num_b);
                else if (op == "-") result_ptr->data.push_back(num_a - num_b);
                else if (op == "*") result_ptr->data.push_back(num_a * num_b);
                else if (op == "^") result_ptr->data.push_back(pow(num_a, num_b));
                else if (op == "/") {
                    if (num_b == 0.0) { Error::set(2, vm.runtime_current_line); return {}; }
                    result_ptr->data.push_back(num_a / num_b);
                }
                else if (op == "=") result_ptr->data.push_back(num_a == num_b);
                else if (op == ">") result_ptr->data.push_back(num_a > num_b);
                else if (op == "<") result_ptr->data.push_back(num_a < num_b);
                else { Error::set(1, vm.runtime_current_line, "Invalid operator string: " + op); return {}; }
            }
        }
    }
    // 4. Check if the operator is a function reference
    else if (std::holds_alternative<FunctionRef>(op_arg)) {
        const std::string func_name = to_upper(std::get<FunctionRef>(op_arg).name);
        if (!vm.active_function_table->count(func_name)) {
            Error::set(22, vm.runtime_current_line, "Operator function '" + func_name + "' not found.");
            return {};
        }
        const auto& func_info = vm.active_function_table->at(func_name);
        if (func_info.arity != 2) {
            Error::set(26, vm.runtime_current_line, "Operator function '" + func_name + "' must accept exactly two arguments.");
            return {};
        }

        for (const auto& val_a : a_ptr->data) {
            for (const auto& val_b : b_ptr->data) {
                std::vector<BasicValue> func_args = { val_a, val_b };
                BasicValue result = vm.execute_function_for_value(func_info, func_args);
                if (Error::get() != 0) return {}; // Propagate error from user function
                result_ptr->data.push_back(result);
            }
        }
    }
    else {
        Error::set(15, vm.runtime_current_line, "Third argument to OUTER must be an operator string or a function reference.");
        return {};
    }

    return result_ptr;
}

// INTEGRATE(function@, limits, rule) 
// INTEGRATE function This is the core logic. It parses arguments, performs the coordinate transformation, and loops through the Gauss points to calculate the final sum.
BasicValue builtin_integrate(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "INTEGRATE requires 3 arguments: function_ref, domain_array, order");
        return 0.0;
    }
    if (!std::holds_alternative<FunctionRef>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to INTEGRATE must be a function reference (e.g., @MyFunc).");
        return 0.0;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Second argument to INTEGRATE must be a 1D array with 2 elements for the domain.");
        return 0.0;
    }

    // 2. --- Argument Parsing ---
    const std::string func_name = to_upper(std::get<FunctionRef>(args[0]).name);
    const auto& domain_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    const int order = static_cast<int>(to_double(args[2]));

    // 3. --- Further Validation ---
    if (!vm.active_function_table->count(func_name)) {
        Error::set(22, vm.runtime_current_line, "Function '" + func_name + "' not found for integration.");
        return 0.0;
    }
    const auto& func_info = vm.active_function_table->at(func_name);
    if (func_info.arity != 1) {
        Error::set(26, vm.runtime_current_line, "Function '" + func_name + "' must accept exactly one argument.");
        return 0.0;
    }
    if (!domain_ptr || domain_ptr->data.size() != 2) {
        Error::set(15, vm.runtime_current_line, "Domain array for INTEGRATE must have exactly two elements [a, b].");
        return 0.0;
    }
    if (GAUSS_RULES.find(order) == GAUSS_RULES.end()) {
        Error::set(1, vm.runtime_current_line, "Unsupported integration order: " + std::to_string(order) + ". Supported orders are 1-5.");
        return 0.0;
    }

    // 4. --- Integration Logic ---
    const double a = to_double(domain_ptr->data[0]); // Lower limit
    const double b = to_double(domain_ptr->data[1]); // Upper limit
    const GaussRule& rule = GAUSS_RULES.at(order);

    double integral_sum = 0.0;

    // The integral of f(x) from a to b is transformed to an integral from -1 to 1.
    // The change of variable is: x = (a+b)/2 + (b-a)/2 * xi
    // The differential becomes: dx = (b-a)/2 * dxi
    // The term (b-a)/2 is the Jacobian of the transformation.
    const double jacobian = (b - a) / 2.0;

    for (size_t i = 0; i < rule.points.size(); ++i) {
        const double weight = rule.weights[i];
        const double gauss_point_xi = rule.points[i]; // This is the point in [-1, 1]

        // Map the Gauss point from the natural coordinate 'xi' to the physical coordinate 'x'
        const double physical_point_x = 0.5 * (a + b) + 0.5 * (b - a) * gauss_point_xi;

        // Prepare argument to pass to the user's BASIC function
        std::vector<BasicValue> args_to_pass = { physical_point_x };

        // Execute the user's BASIC function to get the value of the integrand f(x)
        BasicValue f_of_x_val = vm.execute_function_for_value(func_info, args_to_pass);

        // Check for errors during function execution (e.g., division by zero inside the user func)
        if (Error::get() != 0) {
            return 0.0;
        }

        integral_sum += weight * to_double(f_of_x_val);
    }

    return integral_sum * jacobian;
}

// SOLVE(matrix A, vextor b) -> vector_x
// Solves the linear system Ax = b for the unknown vector x.
BasicValue builtin_solve(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SOLVE requires 2 arguments: matrix_A, vector_b");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Both arguments to SOLVE must be arrays.");
        return {};
    }

    // 2. --- Argument Parsing ---
    const auto& a_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& b_ptr = std::get<std::shared_ptr<Array>>(args[1]);

    // 3. --- Further Validation ---
    if (!a_ptr || a_ptr->shape.size() != 2 || a_ptr->shape[0] != a_ptr->shape[1]) {
        Error::set(15, vm.runtime_current_line, "First argument to SOLVE must be a square matrix.");
        return {};
    }
    const int n = a_ptr->shape[0];
    if (!b_ptr || b_ptr->shape.size() != 1 || b_ptr->data.size() != n) {
        Error::set(15, vm.runtime_current_line, "Second argument must be a vector with the same dimension as the matrix.");
        return {};
    }

    // 4. --- Data Conversion for Solver ---
    std::vector<double> a_data;
    a_data.reserve(n * n);
    for (const auto& val : a_ptr->data) {
        a_data.push_back(to_double(val));
    }

    std::vector<double> b_data;
    b_data.reserve(n);
    for (const auto& val : b_ptr->data) {
        b_data.push_back(to_double(val));
    }

    // 5. --- Call the C++ Solver ---
    std::vector<double> solution_data = lu_solve(a_data, b_data, n);

    if (solution_data.empty()) {
        Error::set(1, vm.runtime_current_line, "Matrix is singular; system cannot be solved.");
        return {};
    }

    // 6. --- Convert Result back to a BASIC Array ---
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { (size_t)n };
    result_ptr->data.reserve(n);
    for (double val : solution_data) {
        result_ptr->data.push_back(val);
    }

    return result_ptr;
}

// INVERT(matrix) -> matrix
// Computes the inverse of a square matrix.
BasicValue builtin_invert(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "INVERT requires 1 argument: a square matrix");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to INVERT must be an array.");
        return {};
    }

    const auto& a_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!a_ptr || a_ptr->shape.size() != 2 || a_ptr->shape[0] != a_ptr->shape[1]) {
        Error::set(15, vm.runtime_current_line, "Argument to INVERT must be a square matrix.");
        return {};
    }
    const int n = a_ptr->shape[0];

    std::vector<double> a_data;
    a_data.reserve(n * n);
    for (const auto& val : a_ptr->data) {
        a_data.push_back(to_double(val));
    }

    std::vector<double> inverse_data = lu_invert(a_data, n);

    if (inverse_data.empty()) {
        Error::set(1, vm.runtime_current_line, "Matrix is singular and cannot be inverted.");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { (size_t)n, (size_t)n };
    result_ptr->data.reserve(n * n);
    for (double val : inverse_data) {
        result_ptr->data.push_back(val);
    }

    return result_ptr;
}


// --- Slicing and Sorting Functions ---

// TAKE(N, array) -> vector
// Takes the first N elements (or last N if N is negative) from an array.
BasicValue builtin_take(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) { Error::set(15, vm.runtime_current_line); return {}; }

    int count = static_cast<int>(to_double(args[0]));
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!arr_ptr) return {};

    if (count == 0) {
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = { 0 };
        return result_ptr;
    }

    auto result_ptr = std::make_shared<Array>();
    if (count > 0) { // Take from start
        size_t num_to_take = std::min((size_t)count, arr_ptr->data.size());
        result_ptr->shape = { num_to_take };
        result_ptr->data.assign(arr_ptr->data.begin(), arr_ptr->data.begin() + num_to_take);
    }
    else { // Take from end
        size_t num_to_take = std::min((size_t)(-count), arr_ptr->data.size());
        result_ptr->shape = { num_to_take };
        result_ptr->data.assign(arr_ptr->data.end() - num_to_take, arr_ptr->data.end());
    }

    return result_ptr;
}

// DROP(N, array) -> vector
// Drops the first N elements (or last N if N is negative) from an array.
BasicValue builtin_drop(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) { Error::set(15, vm.runtime_current_line); return {}; }

    int count = static_cast<int>(to_double(args[0]));
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!arr_ptr) return {};

    if (count == 0) return arr_ptr; // Return a copy of the original

    auto result_ptr = std::make_shared<Array>();
    if (count > 0) { // Drop from start
        size_t num_to_drop = std::min((size_t)count, arr_ptr->data.size());
        result_ptr->shape = { arr_ptr->data.size() - num_to_drop };
        result_ptr->data.assign(arr_ptr->data.begin() + num_to_drop, arr_ptr->data.end());
    }
    else { // Drop from end
        size_t num_to_drop = std::min((size_t)(-count), arr_ptr->data.size());
        result_ptr->shape = { arr_ptr->data.size() - num_to_drop };
        result_ptr->data.assign(arr_ptr->data.begin(), arr_ptr->data.end() - num_to_drop);
    }

    return result_ptr;
}

// GRADE(vector) -> vector
// Returns the indices that would sort the vector in ascending order.
BasicValue builtin_grade(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { Error::set(15, vm.runtime_current_line); return {}; }

    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr) return {};

    // Create a vector of pairs, storing the original index with each value.
    std::vector<std::pair<BasicValue, size_t>> indexed_values;
    indexed_values.reserve(arr_ptr->data.size());
    for (size_t i = 0; i < arr_ptr->data.size(); ++i) {
        indexed_values.push_back({ arr_ptr->data[i], i });
    }

    // Sort this vector of pairs based on the values.
    std::sort(indexed_values.begin(), indexed_values.end(),
        [](const auto& a, const auto& b) {
            // This comparison logic can be expanded to handle strings, etc.
            // For now, it compares numerically.
            return to_double(a.first) < to_double(b.first);
        }
    );

    // Create a new result array containing just the sorted original indices.
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = arr_ptr->shape;
    result_ptr->data.reserve(indexed_values.size());
    for (const auto& pair : indexed_values) {
        // APL is often 1-based, but 0-based is more common in modern languages.
        // We will stick to 0-based indices.
        result_ptr->data.push_back(static_cast<double>(pair.second));
    }

    return result_ptr;
}

// DIFF(array1, array2) -> array
// Returns a new array containing elements that are in array1 but not in array2.
BasicValue builtin_diff(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line); return {};
    }

    const auto& a_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const auto& b_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    if (!a_ptr || !b_ptr) return {};

    // 1. Create a hash set from the second array for fast lookups.
    //    We will store the string representation of each value to handle all types.
    std::unordered_set<std::string> exclusion_set;
    for (const auto& val : b_ptr->data) {
        exclusion_set.insert(to_string(val));
    }

    // 2. Iterate through the first array. If an element is NOT in the exclusion set,
    //    add it to our result.
    auto result_ptr = std::make_shared<Array>();
    for (const auto& val : a_ptr->data) {
        if (exclusion_set.find(to_string(val)) == exclusion_set.end()) {
            result_ptr->data.push_back(val);
        }
    }

    // 3. Set the shape of the resulting vector.
    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// APPEND(array, value) -> array
// Appends a value or another array to an array, returning a new array.
BasicValue builtin_append(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line); // First argument must be an array
        return {};
    }

    const auto& source_array_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const BasicValue& value_to_add = args[1];

    if (!source_array_ptr) return {};

    auto result_ptr = std::make_shared<Array>();

    // 1. Copy the data from the original source array.
    result_ptr->data = source_array_ptr->data;

    // 2. Check if the value to add is also an array.
    if (std::holds_alternative<std::shared_ptr<Array>>(value_to_add)) {
        // If so, append all its elements (flattening it).
        const auto& other_array_ptr = std::get<std::shared_ptr<Array>>(value_to_add);
        if (other_array_ptr) {
            result_ptr->data.insert(result_ptr->data.end(), other_array_ptr->data.begin(), other_array_ptr->data.end());
        }
    }
    else {
        // Otherwise, just append the single scalar value.
        result_ptr->data.push_back(value_to_add);
    }

    // 3. The result of APPEND is always a flat 1D vector.
    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
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

// ABS(numeric_expression or array)
BasicValue builtin_abs(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    // Use the existing helper to apply std::abs to scalars or array elements
    return apply_math_op(args[0], [](double d) { return std::abs(d); });
}

// INT(numeric_expression or array) - Traditional BASIC integer function (floor)
BasicValue builtin_int(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::floor(d); });
}

// FLOOR(numeric_expression or array) - Rounds down
BasicValue builtin_floor(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::floor(d); });
}

// CEIL(numeric_expression or array) - Rounds up
BasicValue builtin_ceil(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::ceil(d); });
}

// TRUNC(numeric_expression or array) - Truncates toward zero
BasicValue builtin_trunc(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    return apply_math_op(args[0], [](double d) { return std::trunc(d); });
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
    localtime_s(&timeinfo, &base_time); // Use safe version

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
        TextIO::print("OPTION NOPAUSE is active. Break/Pause disabled.\n"); // Optional feedback
    }
    else if (option_str == "PAUSE") { // Optional: allow turning pause back on
        vm.nopause_active = false;
        TextIO::print("OPTION PAUSE is active. Break/Pause enabled.\n");
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

// --- Filesystem ---
// 
// // DIR$(wildcard$) -> array
// Returns an array of strings containing filenames that match the wildcard pattern.
BasicValue builtin_dir_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "DIR$ requires exactly one argument (e.g., \"*.txt\").");
        return {};
    }

    auto result_ptr = std::make_shared<Array>();
    std::string full_pattern_str = to_string(args[0]);
    fs::path pattern_path(full_pattern_str);

    // Separate the path from the wildcard filename
    fs::path target_dir = pattern_path.has_parent_path() ? pattern_path.parent_path() : ".";
    std::string wildcard = pattern_path.has_filename() ? pattern_path.filename().string() : "*";

    if (!fs::exists(target_dir) || !fs::is_directory(target_dir)) {
        //Error::set(6, vm.runtime_current_line, "Directory or file not found: " + target_dir.string());
        return {};
    }

    try {
        // Use the existing wildcard to regex converter
        std::regex pattern(wildcard_to_regex(wildcard), std::regex::icase);

        for (const auto& entry : fs::directory_iterator(target_dir)) {
            std::string filename_str = entry.path().filename().string();
            if (std::regex_match(filename_str, pattern)) {
                result_ptr->data.push_back(filename_str);
            }
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

    // Set the shape of the resulting 1D array
    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// DIR [path_string]
BasicValue builtin_dir(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    try {
        fs::path target_path("."); // Default to current directory
        std::string wildcard = "*";   // Default to all files

        if (!args.empty()) {
            fs::path full_arg_path(to_string(args[0]));
            if (full_arg_path.has_filename() && full_arg_path.filename().string().find_first_of("*?") != std::string::npos) {
                wildcard = full_arg_path.filename().string();
                target_path = full_arg_path.has_parent_path() ? full_arg_path.parent_path() : ".";
            }
            else {
                target_path = full_arg_path;
            }
        }

        if (!fs::exists(target_path) || !fs::is_directory(target_path)) {
            TextIO::print("Directory not found: " + target_path.string() + "\n");
            return false;
        }

        // Create the regex object from our wildcard pattern
        std::regex pattern(wildcard_to_regex(wildcard), std::regex::icase); // std::regex::icase for case-insensitivity

        for (const auto& entry : fs::directory_iterator(target_path)) {
            std::string filename_str = entry.path().filename().string();

            // Check if the filename matches our regex pattern
            if (std::regex_match(filename_str, pattern)) {
                std::string size_str;
                if (!entry.is_directory()) {
                    try {
                        size_str = std::to_string(fs::file_size(entry));
                    }
                    catch (fs::filesystem_error&) {
                        size_str = "<ERR>";
                    }
                }
                else {
                    size_str = "<DIR>";
                }

                TextIO::print(filename_str);
                int padding_needed = 25 - static_cast<int>(filename_str.length());

                // Only print padding if the filename is shorter than the column width.
                if (padding_needed > 0) {
                    for (int i = 0; i < padding_needed; ++i) {
                        TextIO::print(" ");
                    }
                }
                TextIO::print(size_str + "\n");
            }
        }
    }
    catch (const std::regex_error& e) {
        TextIO::print("Invalid wildcard pattern: " + std::string(e.what()) + "\n");
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error accessing directory: " + std::string(e.what()) + "\n");
    }

    return false; // Procedures return a dummy value
}

// CD path_string
BasicValue builtin_cd(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, 0); // Wrong number of arguments
        return false;
    }

    std::string path_str = to_string(args[0]);
    try {
        fs::current_path(path_str); // This function changes the current working directory
        TextIO::print("Current directory is now: " + fs::current_path().string() + "\n");
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error changing directory: " + std::string(e.what()) + "\n");
    }
    return false;
}

// PWD
BasicValue builtin_pwd(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    try {
        TextIO::print(fs::current_path().string() + "\n");
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error getting current directory: " + std::string(e.what()) + "\n");
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
            TextIO::print("Directory created: " + path_str + "\n");
        }
        else {
            TextIO::print("Directory already exists or error.\n");
        }
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error creating directory: " + std::string(e.what()) + "\n");
    }
    return false;
}

// KILL path_string (deletes a file)
BasicValue builtin_kill(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, 0); // Wrong number of arguments
        return false;
    }
    std::string path_str = to_string(args[0]);
    try {
        if (fs::remove(path_str)) {
            TextIO::print("File deleted: " + path_str + "\n");
        }
        else {
            TextIO::print("File not found or is a non-empty directory.\n");
        }
    }
    catch (const fs::filesystem_error& e) {
        TextIO::print("Error deleting file: " + std::string(e.what()) + "\n");
    }
    return false;
}

// --- High-Performance File I/O Functions ---

// TXTREADER$(filename$) -> string$
// Reads the entire content of a text file into a single string.
BasicValue builtin_txtreader_str(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
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
// Reads a delimited file (like CSV) into a 2D array of numbers.
BasicValue builtin_csvreader(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty() || args.size() > 3) {
        Error::set(8, vm.runtime_current_line);
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

    // Handle header row if specified
    if (has_header && std::getline(infile, line)) {
        // Just consume the line and do nothing with it.
    }

    // Loop through the rest of the file
    while (std::getline(infile, line)) {
        rows++;
        std::stringstream line_stream(line);
        std::string cell;
        size_t current_cols = 0;

        while (std::getline(line_stream, cell, delimiter)) {
            current_cols++;
            try {
                // Try to convert each cell to a number.
                flat_data.push_back(std::stod(cell));
            }
            catch (const std::invalid_argument&) {
                // If conversion fails, store 0.0, a common practice.
                flat_data.push_back(0.0);
            }
        }

        // --- 4. Determine Shape and Validate ---
        if (rows == 1) {
            // This is the first data row, so it determines the number of columns.
            cols = current_cols;
        }
        else {
            // For all subsequent rows, verify they have the correct number of columns.
            if (current_cols != cols) {
                Error::set(15, vm.runtime_current_line); // Type Mismatch (or a new "Invalid file format" error)
                return {};
            }
        }
    }

    // --- 5. Create and Return the Array ---
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { rows, cols };
    result_ptr->data = flat_data;
    return result_ptr;
}

// TXTWRITER filename$, content$
// Writes the content of a string variable to a text file.
BasicValue builtin_txtwriter(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    std::string filename = to_string(args[0]);
    std::string content = to_string(args[1]);

    std::ofstream outfile(filename);
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
        else if constexpr (std::is_same_v<T, int>) {
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

        if (std::regex_match(text_str, matches, pattern)) {
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

    // --- Register String Functions ---
    register_func("LEFT$", 2, builtin_left_str);
    register_func("RIGHT$", 2, builtin_right_str);
    register_func("MID$", -1, builtin_mid_str); // -1 for variable args
    register_func("LEN", 1, builtin_len);
    register_func("ASC", 1, builtin_asc);
    register_func("CHR$", 1, builtin_chr_str);
    register_func("INSTR", -1, builtin_instr); // -1 for variable args
    register_func("LCASE$", 1, builtin_lcase_str);
    register_func("UCASE$", 1, builtin_ucase_str);
    register_func("TRIM$", 1, builtin_trim_str);
    register_func("INKEY$", 0, builtin_inkey);
    register_func("VAL", 1, builtin_val);
    register_func("STR$", 1, builtin_str_str);
    register_func("SPLIT", 2, builtin_split);
    register_func("FRMV$", 1, builtin_frmv_str);
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
    register_func("FAC", 1, builtin_fac);
    register_func("ABS", 1, builtin_abs);
    register_func("INT", 1, builtin_int);
    register_func("FLOOR", 1, builtin_floor);
    register_func("CEIL", 1, builtin_ceil);
    register_func("TRUNC", 1, builtin_trunc);

    // --- Register APL-style Array Functions ---
    register_func("IOTA", 1, builtin_iota);
    register_func("RESHAPE", -1, builtin_reshape);
    register_func("REVERSE", 1, builtin_reverse);
    register_func("TRANSPOSE", 1, builtin_transpose);
    register_func("PRODUCT", -1, builtin_product);
    register_func("MIN", -1, builtin_min);
    register_func("MAX", -1, builtin_max);
    register_func("ANY", -1, builtin_any);
    register_func("ALL", -1, builtin_all);
    register_func("SCAN", 2, builtin_scan);
    register_func("REDUCE", -1, builtin_reduce);
    register_func("MATMUL", 2, builtin_matmul);
    register_func("OUTER", 3, builtin_outer);
    register_func("INTEGRATE", 3, builtin_integrate);
    register_func("SOLVE", 2, builtin_solve);
    register_func("INVERT", 1, builtin_invert);
    register_func("TAKE", 2, builtin_take);
    register_func("DROP", 2, builtin_drop);
    register_func("GRADE", 1, builtin_grade);
    register_func("SLICE", -1, builtin_slice);
    register_func("STACK", -1, builtin_stack);
    register_func("MVLET", 4, builtin_mvlet);
    register_func("DIFF", 2, builtin_diff);
    register_func("APPEND", 2, builtin_append);
    register_func("ROTATE", 2, builtin_rotate);
    register_func("SHIFT", -1, builtin_shift);
    register_func("CONVOLVE", 3, builtin_convolve);
    register_func("PLACE", 3, builtin_place);

    // --- Register Time Functions ---
    register_func("TICK", 0, builtin_tick);
    register_func("NOW", 0, builtin_now);
    register_func("DATE$", 0, builtin_date_str);
    register_func("TIME$", 0, builtin_time_str);
    register_func("DATEADD", 3, builtin_dateadd);
    register_func("CVDATE", 1, builtin_cvdate);

    // --- Register Methods ---
#ifdef SDL3
    register_proc("SCREEN", -1, builtin_screen);
    register_proc("SCREENFLIP", 0, builtin_screenflip);
    register_proc("DRAWCOLOR", -1, builtin_drawcolor);

    register_proc("PSET", -1, builtin_pset);
    register_proc("LINE", -1, builtin_line);
    register_proc("RECT", -1, builtin_rect);
    register_proc("CIRCLE", -1, builtin_circle);
    register_proc("TEXT", -1, builtin_text);
    register_proc("PLOTRAW", -1, builtin_plotraw);

    register_proc("TURTLE.FORWARD", 1, builtin_turtle_forward);
    register_proc("TURTLE.BACKWARD", 1, builtin_turtle_backward);
    register_proc("TURTLE.LEFT", 1, builtin_turtle_left);
    register_proc("TURTLE.RIGHT", 1, builtin_turtle_right);
    register_proc("TURTLE.PENUP", 0, builtin_turtle_penup);
    register_proc("TURTLE.PENDOWN", 0, builtin_turtle_pendown);
    register_proc("TURTLE.SETPOS", 2, builtin_turtle_setpos);
    register_proc("TURTLE.SETHEADING", 1, builtin_turtle_setheading);
    register_proc("TURTLE.HOME", 0, builtin_turtle_home);
    register_proc("TURTLE.SET_COLOR", 3, builtin_turtle_set_color);
    register_proc("TURTLE.DRAW", 0, builtin_turtle_draw);
    register_proc("TURTLE.CLEAR", 0, builtin_turtle_clear);

    register_proc("SOUND.INIT", 0, builtin_sound_init);
    register_proc("SOUND.VOICE", 6, builtin_sound_voice);
    register_proc("SOUND.PLAY", 2, builtin_sound_play);
    register_proc("SOUND.RELEASE", 1, builtin_sound_release);
    register_proc("SOUND.STOP", 1, builtin_sound_stop);

    register_func("MOUSEX", 0, builtin_mousex);
    register_func("MOUSEY", 0, builtin_mousey);
    register_func("MOUSEB", 1, builtin_mouseb);

    // Sprite Procedures
    //register_proc("SPRITE.LOAD", 2, builtin_sprite_load);
    register_proc("SPRITE.LOAD_ASEPRITE", 2, builtin_sprite_load_aseprite);
    register_proc("SPRITE.MOVE", 3, builtin_sprite_move);
    register_proc("SPRITE.SET_VELOCITY", 3, builtin_sprite_set_velocity);
    register_proc("SPRITE.DELETE", 1, builtin_sprite_delete);
    register_proc("SPRITE.UPDATE", -1, builtin_sprite_update); // Now has optional arg
    register_proc("SPRITE.DRAW_ALL", 0, builtin_sprite_draw_all);
    register_proc("SPRITE.SET_ANIMATION", 2, builtin_sprite_set_animation);
    register_proc("SPRITE.SET_FLIP", 2, builtin_sprite_set_flip);
    register_proc("SPRITE.ADD_TO_GROUP", 2, builtin_sprite_add_to_group);
    register_func("SPRITE.CREATE", 3, builtin_sprite_create);
    register_func("SPRITE.GET_X", 1, builtin_sprite_get_x);
    register_func("SPRITE.GET_Y", 1, builtin_sprite_get_y);
    register_func("SPRITE.COLLISION", 2, builtin_sprite_collision);
    register_func("SPRITE.CREATE_GROUP", 0, builtin_sprite_create_group);
    register_func("SPRITE.COLLISION_GROUP", 2, builtin_sprite_collision_group);
    register_func("SPRITE.COLLISION_GROUPS", 2, builtin_sprite_collision_groups);
    register_func("SPRITE.GET_X", 1, builtin_sprite_get_x);
    register_func("SPRITE.GET_Y", 1, builtin_sprite_get_y);


    // --- Add New TileMap Functions ---
    register_proc("MAP.LOAD", 2, builtin_map_load);
    register_proc("MAP.DRAW_LAYER", -1, builtin_map_draw_layer); // Optional args
    register_func("MAP.GET_OBJECTS", 2, builtin_map_get_objects);
    register_func("MAP.COLLIDES", 3, builtin_map_collides);


#endif
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
#endif

    register_func("JSON.PARSE$", 1, builtin_json_parse);
    register_func("JSON.STRINGIFY$", 1, builtin_json_stringify);

    register_func("MAP.EXISTS", 2, builtin_map_exists);
    register_func("MAP.KEYS", 1, builtin_map_keys);
    register_func("MAP.VALUES", 1, builtin_map_values);

    register_proc("SETLOCALE", 1, builtin_setlocale);
    register_proc("CLS", -1, builtin_cls);
    register_proc("LOCATE", 2, builtin_locate);
    register_proc("SLEEP", 1, builtin_sleep);
    register_proc("OPTION", 1, builtin_option);
    register_proc("CURSOR", 1, builtin_cursor);
    register_func("GETENV$", 1, builtin_getenv_str);

    register_proc("DIR", -1, builtin_dir);  // -1 for optional argument
    register_func("DIR$", 1, builtin_dir_str);
    register_proc("CD", 1, builtin_cd);
    register_proc("PWD", 0, builtin_pwd);
    register_proc("COLOR", 2, builtin_color);
    register_proc("MKDIR", 1, builtin_mkdir);
    register_proc("KILL", 1, builtin_kill);

    register_func("CSVREADER", -1, builtin_csvreader); // -1 for optional args
    register_func("TXTREADER$", 1, builtin_txtreader_str);
    register_proc("TXTWRITER", 2, builtin_txtwriter);
    register_proc("CSVWRITER", -1, builtin_csvwriter); // -1 for optional delimiter

    // Task thing

}