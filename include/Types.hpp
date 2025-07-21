#pragma once
#ifdef JDCOM
// COM support
#include <windows.h> // Basic Windows types, HRESULT
#include <objbase.h> // CoInitializeEx, CoUninitialize, CoCreateInstance, CLSIDFromProgID
#include <oaidl.h>   // <-- Contains definitions for IDispatch, VARIANT, SAFEARRAY, etc.
#include <comdef.h>  // _com_ptr_t, _variant_t, _bstr_t, _com_error
#endif

#include <variant>
#include <string>
#include <chrono>
#include <vector>     
#include <numeric>    // for std::accumulate
#include <stdexcept>  // for exceptions
#include <future>
#include <map>
#include "json.hpp" 

// Forward-declare the Array struct so BasicValue can know it exists.
struct Array;
struct Map;
struct JsonObject; 
struct Tensor;

// An enum to represent the declared type of a variable.
enum class DataType {
    DEFAULT,
    BOOL,
    INTEGER,
    DOUBLE,
    STRING,
    DATETIME,
    MAP,
    JSON 
};

// A struct to hold our date/time value, based on std::chrono.
struct DateTime {
    std::chrono::system_clock::time_point time_point;

    // Default constructor initializes to the current time.
    DateTime() : time_point(std::chrono::system_clock::now()) {}

    // Constructor from a time_point.
    DateTime(const std::chrono::system_clock::time_point& tp) : time_point(tp) {}

#ifdef _WIN32    
    bool operator==(const DateTime&) const = default;
#else
    bool operator==(const DateTime& other) const {
        return time_point == other.time_point;
    }
#endif

};

#ifdef JDCOM
// Structype to hold COM IDispatch pointers
struct ComObject {
    IDispatchPtr  ptr;

    // Default constructor
    ComObject() : ptr(nullptr) {}

    // Constructor from an IDispatch pointer.
    // This constructor takes ownership of the raw pDisp,
    // assuming it already has a reference (e.g., from CoCreateInstance).
    // The 'false' parameter tells _com_ptr_t NOT to call AddRef again.
    // Ensure IDispatch is fully defined (via oaidl.h) before this is parsed by compiler.
    ComObject(IDispatch* pDisp) : ptr(pDisp) {} // This is the correct form.

    // Copy constructor (performs AddRef implicitly via _com_ptr_t's copy ctor)
    ComObject(const ComObject& other) : ptr(other.ptr) {}

    // Move constructor
    ComObject(ComObject&& other) noexcept : ptr(std::move(other.ptr)) {}

    // Copy assignment (handles AddRef/Release implicitly)
    ComObject& operator=(const ComObject& other) {
        if (this != &other) {
            ptr = other.ptr;
        }
        return *this;
    }

    // Move assignment
    ComObject& operator=(ComObject&& other) noexcept {
        if (this != &other) {
            ptr = std::move(other.ptr);
        }
        return *this;
    }

    // Comparison (for simplicity, just compare pointers or disallow direct comparison in Basic)
    bool operator==(const ComObject& other) const {
        return ptr == other.ptr;
    }
    bool operator!=(const ComObject& other) const {
        return ptr != other.ptr;
    }
    // Add other comparison operators if meaningful
};
#endif

struct OpaqueHandle {
    void* ptr = nullptr;
    std::string type_name;
    std::function<void(void*)> deleter;

    // Constructor
    OpaqueHandle(void* p, std::string t, std::function<void(void*)> d)
        : ptr(p), type_name(std::move(t)), deleter(std::move(d)) {}

    // Destructor that calls the custom deleter
    ~OpaqueHandle() {
        if (ptr && deleter) {
            deleter(ptr);
            ptr = nullptr;
        }
    }

    // Disable copying
    OpaqueHandle(const OpaqueHandle&) = delete;
    OpaqueHandle& operator=(const OpaqueHandle&) = delete;

    // Allow moving
    OpaqueHandle(OpaqueHandle&& other) noexcept
        : ptr(other.ptr), type_name(std::move(other.type_name)), deleter(std::move(other.deleter)) {
        other.ptr = nullptr;
        other.deleter = nullptr;
    }
    OpaqueHandle& operator=(OpaqueHandle&& other) noexcept {
        if (this != &other) {
            if (ptr && deleter) deleter(ptr);
            ptr = other.ptr;
            type_name = std::move(other.type_name);
            deleter = std::move(other.deleter);
            other.ptr = nullptr;
            other.deleter = nullptr;
        }
        return *this;
    }
};

// This struct represents a "pointer" or "reference" to a function.
// We just store the function's name.
struct FunctionRef {
    std::string name;
    // This lets std::variant compare it if needed.
#ifdef _WIN32    
    bool operator==(const FunctionRef&) const = default;
#else
    bool operator==(const FunctionRef& other) const {
        return name == other.name;
    }
#endif
};

struct JsonObject {
    nlohmann::json data;
};

struct TaskRef {
    int id = -1;
#ifdef _WIN32    
    bool operator==(const TaskRef&) const = default;
#else
    bool operator==(const TaskRef& other) const {
        return id == other.id;
    }
#endif    
};

struct ThreadHandle {
    std::thread::id id; // Use the thread's ID as a unique identifier
#ifdef _WIN32    
    bool operator==(const ThreadHandle&) const = default;
#else
    bool operator==(const ThreadHandle& other) const {
        return id == other.id;
    }
#endif    
};

// --- Use a std::shared_ptr to break the circular dependency ---
#ifdef JDCOM
using BasicValue = std::variant<bool, double, std::string, FunctionRef, int, DateTime, std::shared_ptr<Array>, std::shared_ptr<Map>, std::shared_ptr<JsonObject>, ComObject, std::shared_ptr<Tensor>, TaskRef, ThreadHandle, std::shared_ptr<OpaqueHandle> >;
#else
using BasicValue = std::variant<bool, double, std::string, FunctionRef, int, DateTime, std::shared_ptr<Array>, std::shared_ptr<Map>, std::shared_ptr<JsonObject>, std::shared_ptr<Tensor>, TaskRef, ThreadHandle, std::shared_ptr<OpaqueHandle> >;
#endif


// --- A structure to represent N-dimensional arrays ---
struct Array {
    std::vector<BasicValue> data; // The raw data, stored in a flat "raveled" format.
    std::vector<size_t> shape;    // The dimensions of the array. e.g., {5} for a vector, {2, 3} for a 2x3 matrix.

    // Default constructor for an empty array
    Array() = default;

    // A helper to calculate the total number of elements
    size_t size() const {
        if (shape.empty()) return 0;
        // Use std::accumulate to multiply all dimensions together
        return std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());
    }

    // Helper to get the index into the flat 'data' vector from dimensional indices
    size_t get_flat_index(const std::vector<size_t>& indices) const {
        if (indices.size() != shape.size()) {
            // This indicates a programmer error or a runtime error that should be caught
            throw std::runtime_error("Mismatched number of dimensions for indexing.");
        }
        size_t flat_index = 0;
        size_t multiplier = 1;
        for (int i = shape.size() - 1; i >= 0; --i) {
            if (indices[i] >= shape[i]) {
                // This is a runtime error (Index out of bounds)
                throw std::out_of_range("Array index out of bounds.");
            }
            flat_index += indices[i] * multiplier;
            multiplier *= shape[i];
        }
        return flat_index;
    }

    // This lets std::variant compare it if needed.
#ifdef _WIN32    
    bool operator==(const Array&) const = default;
#else
    bool operator==(const Array& other) const {
        return data == other.data;
    }
#endif     
};

// --- A structure to represent a Map (associative array) ---
struct Map {
    std::map<std::string, BasicValue> data;
    std::string type_name_if_udt; // Stores the name of the UDT, e.g., "T_SPRITE"
};

using GradFunc = std::function<std::vector<std::shared_ptr<Tensor>>(std::shared_ptr<Tensor>)>;

 struct FloatArray {
     std::vector<double> data;
     std::vector<size_t> shape;

     size_t size() const {
         if (shape.empty()) return 0;
         return std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());
     }
 };


struct Tensor {
    std::shared_ptr<FloatArray> data; // The actual matrix/vector data
    std::shared_ptr<Tensor> grad; // The gradient, also a Tensor

    // --- For Autodiff ---
    std::vector<std::shared_ptr<Tensor>> parents; // Tensors this one was created from
    GradFunc backward_fn = nullptr; // The function to compute the gradient

    // A flag to prevent re-computing gradients in complex graphs
    bool has_been_processed = false;
};

//==============================================================================
// HELPER FUNCTIONS
// We define them here as 'inline' so they can be used across
// multiple .cpp files without causing linker errors.
//==============================================================================

// Helper to convert a BasicValue to a double for math operations.
// This is called "coercion". It treats booleans as 1.0 or 0.0.
inline double to_double(const BasicValue& val) {
    if (std::holds_alternative<double>(val)) {
        return std::get<double>(val);
    }
    if (std::holds_alternative<int>(val)) { 
        return static_cast<double>(std::get<int>(val));
    }
    if (std::holds_alternative<bool>(val)) {
        return std::get<bool>(val) ? 1.0 : 0.0;
    }
    // Check if it holds a pointer to an array
    if (std::holds_alternative<std::shared_ptr<Array>>(val)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(val);
        if (arr_ptr && arr_ptr->data.size() == 1) {
            // Coerce single-element array to a number
            return to_double(arr_ptr->data[0]);
        }
    }
    return 0.0;
}

// Helper to convert a BasicValue to a boolean for logic operations.
// It treats any non-zero number as true.
inline bool to_bool(const BasicValue& val) {
    if (std::holds_alternative<bool>(val)) {
        return std::get<bool>(val);
    }
    if (std::holds_alternative<double>(val)) {
        return std::get<double>(val) != 0.0;
    }
    // Check if it holds a pointer to an array
    if (std::holds_alternative<std::shared_ptr<Array>>(val)) {
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(val);
        if (arr_ptr && arr_ptr->data.size() == 1) {
            // Coerce single-element array to a bool
            return to_bool(arr_ptr->data[0]);
        }
    }
    return false;
}

