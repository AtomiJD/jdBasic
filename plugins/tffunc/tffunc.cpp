// tensorflow_func.cpp
#include "tffunc.h"
#include "NeReLaBasic.hpp"
#include "Types.hpp"
#include "Error.hpp"

// Include the TensorFlow C API header
#include "tensorflow/c/c_api.h"

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <numeric>

// --- Global pointers to hold the functions provided by the main app ---
namespace {
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
    ToStringFunc g_to_string = nullptr;

    // A helper to check TF_Status and set a jdBasic error if needed.
    bool CheckTFStatus(NeReLaBasic& vm, TF_Status* status, const std::string& context_msg) {
        if (TF_GetCode(status) != TF_OK) {
            std::string error_message = context_msg + ": " + TF_Message(status);
            g_error_set(12, vm.runtime_current_line, error_message); // Use a generic I/O error code
            return false;
        }
        return true;
    }
    // TensorFlow will call this to free the memory we allocate for a tensor's data.
    void FreeTensorData(void* data, size_t length, void* arg) {
        free(data);
    }

    // Helper to convert TF_DataType enum to a string for jdBasic
    std::string TFDataTypeToString(TF_DataType dtype) {
        switch (dtype) {
        case TF_FLOAT: return "FLOAT";
        case TF_DOUBLE: return "DOUBLE";
        case TF_INT32: return "INT32";
        case TF_UINT8: return "UINT8";
        case TF_INT16: return "INT16";
        case TF_INT8: return "INT8";
        case TF_INT64: return "INT64";
        case TF_STRING: return "STRING";
        case TF_BOOL: return "BOOL";
        default: return "UNKNOWN";
        }
    }

    // Helper to convert a string from jdBasic to a TF_DataType enum
    TF_DataType StringToTFDataType(const std::string& str_type) {
        std::string upper_type = g_to_upper(str_type);
        if (upper_type == "FLOAT") return TF_FLOAT;
        if (upper_type == "DOUBLE") return TF_DOUBLE;
        if (upper_type == "INT32") return TF_INT32;
        if (upper_type == "INT64") return TF_INT64;
        // Default to FLOAT if unrecognized
        g_error_set(1, 0, "Unsupported or unknown data type: " + str_type + ". Defaulting to FLOAT.");
        return TF_FLOAT;
    }

    // Helper to set attributes on an operation from a jdBasic Map
    void SetOperationAttributes(NeReLaBasic& vm, TF_OperationDescription* desc, const std::shared_ptr<Map>& attr_map) {
        if (!attr_map) return;

        TF_Status* status = TF_NewStatus();
        for (const auto& pair : attr_map->data) {
            const std::string& attr_name = pair.first;
            const BasicValue& attr_value = pair.second;

            if (std::holds_alternative<std::string>(attr_value)) {
                std::string str_val = g_to_string(attr_value);
                if (str_val.rfind("DT_", 0) == 0) {
                    TF_DataType dtype = StringToTFDataType(str_val.substr(3));
                    TF_SetAttrType(desc, attr_name.c_str(), dtype);
                }
                else {
                    TF_SetAttrString(desc, attr_name.c_str(), str_val.c_str(), str_val.length());
                }
            }
            else if (std::holds_alternative<bool>(attr_value)) {
                TF_SetAttrBool(desc, attr_name.c_str(), to_bool(attr_value));
            }
            else if (std::holds_alternative<double>(attr_value)) {
                TF_SetAttrFloat(desc, attr_name.c_str(), static_cast<float>(to_double(attr_value)));
            }
            else if (std::holds_alternative<int>(attr_value)) {
                TF_SetAttrInt(desc, attr_name.c_str(), std::get<int>(attr_value));
            }
            // +++ NEW: HANDLE 'shape' ATTRIBUTE (as an Array) +++
            else if (std::holds_alternative<std::shared_ptr<Array>>(attr_value)) {
                const auto& arr_ptr = std::get<std::shared_ptr<Array>>(attr_value);
                std::vector<int64_t> dims;
                if (arr_ptr) { // Ensure the array pointer is not null
                    for (const auto& val : arr_ptr->data) {
                        dims.push_back(static_cast<int64_t>(to_double(val)));
                    }
                }
                TF_SetAttrShape(desc, attr_name.c_str(), dims.data(), dims.size());
            }
            // +++ NEW: HANDLE 'value' ATTRIBUTE (as a Tensor Handle) for Const ops +++
            else if (std::holds_alternative<std::shared_ptr<OpaqueHandle>>(attr_value)) {
                const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(attr_value);
                if (handle && handle->type_name == "TF_TENSOR") {
                    TF_Tensor* tensor = static_cast<TF_Tensor*>(handle->ptr);
                    TF_SetAttrTensor(desc, attr_name.c_str(), tensor, status);
                    if (!CheckTFStatus(vm, status, "Failed to set tensor attribute " + attr_name)) {
                        break; // Stop processing attributes on error
                    }
                }
            }
            else {
                g_error_set(15, vm.runtime_current_line, "Unsupported attribute type for attribute: " + attr_name);
            }
        }
        TF_DeleteStatus(status);
    }

    void NoOpDeallocator(void* data, size_t len, void* arg) {}
}

// === HELPER FUNCTIONS for Data Conversion ===

// Helper to convert a jdBasic Array to a TF_Tensor
TF_Tensor* BasicArrayToTensor(NeReLaBasic& vm, const std::shared_ptr<Array>& arr_ptr, TF_DataType dtype) {
    if (!arr_ptr) {
        g_error_set(15, vm.runtime_current_line, "Cannot convert a null jdBasic Array to a Tensor.");
        return nullptr;
    }

    // --- FIX: Correctly use the shape from the jdBasic Array ---
    std::vector<int64_t> dims;
    dims.reserve(arr_ptr->shape.size());
    for (size_t d : arr_ptr->shape) {
        dims.push_back(static_cast<int64_t>(d));
    }

    size_t total_elements = arr_ptr->size();
    size_t total_bytes = 0;
    void* tensor_data = nullptr;
    size_t i = 0;

    // Allocate buffer and copy data based on the requested type
    switch (dtype) {
    case TF_FLOAT: {
        total_bytes = total_elements * sizeof(float);
        tensor_data = malloc(total_bytes);
        if (!tensor_data) break;
        float* p = static_cast<float*>(tensor_data);
        for (const auto& val : arr_ptr->data) p[i++] = static_cast<float>(to_double(val));
        break;
    }
    case TF_DOUBLE: {
        total_bytes = total_elements * sizeof(double);
        tensor_data = malloc(total_bytes);
        if (!tensor_data) break;
        double* p = static_cast<double*>(tensor_data);
        for (const auto& val : arr_ptr->data) p[i++] = to_double(val);
        break;
    }
    case TF_INT32: {
        total_bytes = total_elements * sizeof(int32_t);
        tensor_data = malloc(total_bytes);
        if (!tensor_data) break;
        int32_t* p = static_cast<int32_t*>(tensor_data);
        for (const auto& val : arr_ptr->data) p[i++] = static_cast<int32_t>(to_double(val));
        break;
    }
    case TF_INT64: {
        total_bytes = total_elements * sizeof(int64_t);
        tensor_data = malloc(total_bytes);
        if (!tensor_data) break;
        int64_t* p = static_cast<int64_t*>(tensor_data);
        for (const auto& val : arr_ptr->data) p[i++] = static_cast<int64_t>(to_double(val));
        break;
    }
    default: {
        g_error_set(15, vm.runtime_current_line, "Unsupported data type for array conversion.");
        return nullptr;
    }
    }

    if (!tensor_data) {
        g_error_set(7, vm.runtime_current_line, "Failed to allocate memory for Tensor data.");
        return nullptr;
    }

    // Now, TF_NewTensor is called with the correct multi-dimensional shape
    return TF_NewTensor(dtype, dims.data(), dims.size(), tensor_data, total_bytes, &FreeTensorData, nullptr);
}

// Helper to convert a TF_Tensor to a jdBasic Array
std::shared_ptr<Array> TensorToBasicArray(NeReLaBasic& vm, TF_Tensor* tensor) {
    TF_DataType dtype = TF_TensorType(tensor);
    int num_dims = TF_NumDims(tensor);
    size_t total_elements = 1;

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape.resize(num_dims);

    for (int i = 0; i < num_dims; ++i) {
        result_ptr->shape[i] = TF_Dim(tensor, i);
        total_elements *= result_ptr->shape[i];
    }

    const void* tensor_data = TF_TensorData(tensor);
    if (!tensor_data) {
        g_error_set(15, vm.runtime_current_line, "Tensor has no data buffer.");
        return nullptr;
    }

    result_ptr->data.reserve(total_elements);

    // Handle conversion from TF types to jdBasic's double
    if (dtype == TF_FLOAT) {
        const float* data_ptr = static_cast<const float*>(tensor_data);
        for (size_t i = 0; i < total_elements; ++i) {
            result_ptr->data.push_back(static_cast<double>(data_ptr[i]));
        }
    }
    else if (dtype == TF_DOUBLE) {
        const double* data_ptr = static_cast<const double*>(tensor_data);
        for (size_t i = 0; i < total_elements; ++i) {
            result_ptr->data.push_back(data_ptr[i]);
        }
    }
    else {
        g_error_set(15, vm.runtime_current_line, "Unsupported tensor data type for conversion to Array.");
        return nullptr;
    }

    return result_ptr;
}


// === BUILT-IN FUNCTION IMPLEMENTATIONS ===

// TENSORFLOW.VERSION() -> string$
void builtin_tf_version(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (!args.empty()) {
        g_error_set(8, vm.runtime_current_line, "TENSORFLOW.VERSION takes no arguments.");
        *out_result = ""; return;
    }
    *out_result = std::string(TF_Version());
}

// TENSORFLOW.NEW_GRAPH() -> Handle
void builtin_tf_new_graph(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (!args.empty()) {
        g_error_set(8, vm.runtime_current_line, "TENSORFLOW.NEW_GRAPH takes no arguments.");
        *out_result = false; return;
    }

    TF_Graph* graph = TF_NewGraph();
    auto deleter = [](void* p) { if (p) TF_DeleteGraph(static_cast<TF_Graph*>(p)); };

    *out_result = std::make_shared<OpaqueHandle>(graph, "TF_GRAPH", deleter);
}

// TENSORFLOW.NEW_SESSION(graph_handle) -> Handle
void builtin_tf_new_session(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, "TENSORFLOW.NEW_SESSION requires a graph handle.");
        *out_result = false; return;
    }
    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "Argument must be a valid TF_GRAPH Handle.");
        *out_result = false; return;
    }

    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!graph_handle || graph_handle->type_name != "TF_GRAPH") {
        g_error_set(15, vm.runtime_current_line, "Argument is not a valid TF_GRAPH Handle.");
        *out_result = false; return;
    }
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);

    TF_Status* status = TF_NewStatus();
    TF_SessionOptions* opts = TF_NewSessionOptions();
    TF_Session* session = TF_NewSession(graph, opts, status);
    TF_DeleteSessionOptions(opts);

    if (!CheckTFStatus(vm, status, "Failed to create session")) {
        TF_DeleteStatus(status);
        *out_result = false; return;
    }
    TF_DeleteStatus(status);

    auto deleter = [](void* p) {
        if (p) {
            TF_Status* s = TF_NewStatus();
            TF_CloseSession(static_cast<TF_Session*>(p), s);
            TF_DeleteSession(static_cast<TF_Session*>(p), s);
            TF_DeleteStatus(s);
        }
        };

    *out_result = std::make_shared<OpaqueHandle>(session, "TF_SESSION", deleter);
}

// TENSORFLOW.LOAD_GRAPH_FROM_FILE(graph_handle, filename$) -> Boolean
void builtin_tf_load_graph(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 2) {
        g_error_set(8, vm.runtime_current_line, "Requires a graph handle and a filename.");
        *out_result = false; return;
    }
    // Argument validation
    if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "Argument 1 must be a valid TF_GRAPH Handle.");
        *out_result = false; return;
    }
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!graph_handle || graph_handle->type_name != "TF_GRAPH") {
        g_error_set(15, vm.runtime_current_line, "Argument 1 is not a valid TF_GRAPH Handle.");
        *out_result = false; return;
    }
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);
    std::string filename = g_to_string(args[1]);

    // Read the file into a buffer
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        g_error_set(6, vm.runtime_current_line, "Failed to open GraphDef file: " + filename);
        *out_result = false; return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        g_error_set(12, vm.runtime_current_line, "Failed to read GraphDef file: " + filename);
        *out_result = false; return;
    }

    // Create TF_Buffer and import the graph
    TF_Buffer* graph_def = TF_NewBufferFromString(buffer.data(), buffer.size());
    TF_Status* status = TF_NewStatus();
    TF_ImportGraphDefOptions* opts = TF_NewImportGraphDefOptions();

    TF_GraphImportGraphDef(graph, graph_def, opts, status);

    TF_DeleteImportGraphDefOptions(opts);
    TF_DeleteBuffer(graph_def);

    if (!CheckTFStatus(vm, status, "Failed to import graph definition")) {
        TF_DeleteStatus(status);
        *out_result = false; return;
    }

    TF_DeleteStatus(status);
    *out_result = true;
}

// TENSORFLOW.NEW_TENSOR_FROM_ARRAY(array) -> Handle
void builtin_tf_new_tensor(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() < 1 || args.size() > 2 || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        g_error_set(8, vm.runtime_current_line, "Requires an Array and an optional data type string.");
        *out_result = false; return;
    }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);

    TF_DataType dtype = TF_FLOAT; // Default to float
    if (args.size() == 2) {
        dtype = StringToTFDataType(g_to_string(args[1]));
    }

    TF_Tensor* tensor = BasicArrayToTensor(vm, arr_ptr, dtype);

    if (!tensor) {
        *out_result = false; return;
    }

    auto deleter = [](void* p) { if (p) TF_DeleteTensor(static_cast<TF_Tensor*>(p)); };
    *out_result = std::make_shared<OpaqueHandle>(tensor, "TF_TENSOR", deleter);
}

// TENSORFLOW.NEW_ARRAY_FROM_TENSOR(tensor_handle) -> Array
void builtin_tf_array_from_tensor(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(8, vm.runtime_current_line, "Requires a Tensor Handle.");
        *out_result = {}; return;
    }
    const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!handle || handle->type_name != "TF_TENSOR") {
        g_error_set(15, vm.runtime_current_line, "Argument is not a valid TF_TENSOR Handle.");
        *out_result = {}; return;
    }

    TF_Tensor* tensor = static_cast<TF_Tensor*>(handle->ptr);
    *out_result = TensorToBasicArray(vm, tensor);
}


// TENSORFLOW.SESSION_RUN(session_handle, inputs_map, outputs_array) -> result_map
void builtin_tf_session_run(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 5) {
        g_error_set(8, vm.runtime_current_line, "SESSION_RUN requires: session, graph, inputs_map, outputs_array, targets_array.");
        *out_result = {}; return;
    }

    // --- 1. Validate Handles and Types ---
    const auto& session_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[1]);
    const auto& inputs_map_ptr = std::get<std::shared_ptr<Map>>(args[2]);
    const auto& outputs_arr_ptr = std::get<std::shared_ptr<Array>>(args[3]);
    const auto& targets_arr_ptr = std::get<std::shared_ptr<Array>>(args[4]);

    if (!session_handle || session_handle->type_name != "TF_SESSION" ||
        !graph_handle || graph_handle->type_name != "TF_GRAPH") {
        g_error_set(15, vm.runtime_current_line, "Invalid Session or Graph handle.");
        *out_result = {}; return;
    }

    TF_Session* session = static_cast<TF_Session*>(session_handle->ptr);
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);

    // --- 2. Prepare Inputs ---
    std::vector<TF_Output> input_ops;
    std::vector<TF_Tensor*> input_tensors;
    if (inputs_map_ptr) {
        // +++ THIS IS THE RESTORED LOGIC +++
        input_ops.reserve(inputs_map_ptr->data.size());
        input_tensors.reserve(inputs_map_ptr->data.size());
        for (const auto& pair : inputs_map_ptr->data) {
            std::string op_name = pair.first;
            const auto& tensor_handle = std::get<std::shared_ptr<OpaqueHandle>>(pair.second);

            size_t colon_pos = op_name.find(':');
            if (colon_pos == std::string::npos) { /* error handling */ continue; }
            std::string name = op_name.substr(0, colon_pos);
            int index = std::stoi(op_name.substr(colon_pos + 1));

            TF_Operation* op = TF_GraphOperationByName(graph, name.c_str());
            if (op == nullptr) { /* error handling */ continue; }

            input_ops.push_back({ op, index });
            input_tensors.push_back(static_cast<TF_Tensor*>(tensor_handle->ptr));
        }
    }

    // --- 3. Prepare Outputs ---
    std::vector<TF_Output> output_ops;
    std::vector<TF_Tensor*> output_tensors;
    if (outputs_arr_ptr) {
        // +++ THIS IS THE RESTORED LOGIC +++
        output_ops.reserve(outputs_arr_ptr->data.size());
        output_tensors.resize(outputs_arr_ptr->data.size(), nullptr);
        for (const auto& out_val : outputs_arr_ptr->data) {
            std::string op_name = g_to_string(out_val);
            size_t colon_pos = op_name.find(':');
            if (colon_pos == std::string::npos) { /* error handling */ continue; }
            std::string name = op_name.substr(0, colon_pos);
            int index = std::stoi(op_name.substr(colon_pos + 1));

            TF_Operation* op = TF_GraphOperationByName(graph, name.c_str());
            if (op == nullptr) { /* error handling */ continue; }
            output_ops.push_back({ op, index });
        }
    }

    // --- 4. Prepare Targets ---
    std::vector<const TF_Operation*> target_ops;
    if (targets_arr_ptr) {
        target_ops.reserve(targets_arr_ptr->data.size());
        for (const auto& target_val : targets_arr_ptr->data) {
            if (!std::holds_alternative<std::shared_ptr<OpaqueHandle>>(target_val)) continue;
            const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(target_val);
            if (handle && handle->type_name == "TF_OP_OUTPUT") {
                TF_Output* output = static_cast<TF_Output*>(handle->ptr);
                target_ops.push_back(output->oper);
            }
        }
    }

    // --- 5. Run the Session ---
    TF_Status* status = TF_NewStatus();
    TF_SessionRun(session,
        nullptr, // RunOptions
        input_ops.data(), input_tensors.data(), input_ops.size(),
        output_ops.data(), output_tensors.data(), output_ops.size(),
        target_ops.data(), target_ops.size(), // Pass the target ops
        nullptr, // RunMetadata
        status
    );

    if (!CheckTFStatus(vm, status, "Failed during TF_SessionRun")) {
        TF_DeleteStatus(status);
        *out_result = {}; return;
    }
    TF_DeleteStatus(status);

    // --- 6. Package Results ---
    auto results_map = std::make_shared<Map>();
    if (outputs_arr_ptr) {
        for (size_t i = 0; i < output_tensors.size(); ++i) {
            TF_Tensor* tensor = output_tensors[i];
            if (tensor) {
                auto deleter = [](void* p) { if (p) TF_DeleteTensor(static_cast<TF_Tensor*>(p)); };
                auto tensor_handle = std::make_shared<OpaqueHandle>(tensor, "TF_TENSOR", deleter);
                results_map->data[g_to_string(outputs_arr_ptr->data[i])] = tensor_handle;
            }
        }
    }
    *out_result = results_map;
}

// TENSORFLOW.GET_TENSOR_SHAPE(tensor_handle) -> Array
void builtin_tf_get_tensor_shape(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(8, vm.runtime_current_line, "Requires a Tensor Handle.");
        *out_result = {}; return;
    }
    const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!handle || handle->type_name != "TF_TENSOR") {
        g_error_set(15, vm.runtime_current_line, "Argument is not a valid TF_TENSOR Handle.");
        *out_result = {}; return;
    }

    TF_Tensor* tensor = static_cast<TF_Tensor*>(handle->ptr);
    int num_dims = TF_NumDims(tensor);

    auto shape_array = std::make_shared<Array>();
    shape_array->shape = { (size_t)num_dims };
    shape_array->data.reserve(num_dims);

    for (int i = 0; i < num_dims; ++i) {
        shape_array->data.push_back(static_cast<double>(TF_Dim(tensor, i)));
    }

    *out_result = shape_array;
}

// TENSORFLOW.GET_TENSOR_DTYPE(tensor_handle) -> String
void builtin_tf_get_tensor_dtype(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<OpaqueHandle>>(args[0])) {
        g_error_set(8, vm.runtime_current_line, "Requires a Tensor Handle.");
        *out_result = ""; return;
    }
    const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    if (!handle || handle->type_name != "TF_TENSOR") {
        g_error_set(15, vm.runtime_current_line, "Argument is not a valid TF_TENSOR Handle.");
        *out_result = ""; return;
    }

    TF_Tensor* tensor = static_cast<TF_Tensor*>(handle->ptr);
    *out_result = TFDataTypeToString(TF_TensorType(tensor));
}

// === TIER 2 BUILT-IN FUNCTIONS ===

// Helper to create the TF_OP_OUTPUT handle
BasicValue CreateOpOutputHandle(TF_Operation* op, int index = 0) {
    // We must allocate TF_Output on the heap because the OpaqueHandle
    // will own it and needs a stable address to store in its void*.
    TF_Output* output = new TF_Output{ op, index };
    auto deleter = [](void* p) { if (p) delete static_cast<TF_Output*>(p); };
    return std::make_shared<OpaqueHandle>(output, "TF_OP_OUTPUT", deleter);
}

// TENSORFLOW.PLACEHOLDER(graph_handle, dtype$, shape_array, op_name$) -> handle
void builtin_tf_placeholder(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 4) {
        g_error_set(8, vm.runtime_current_line, "PLACEHOLDER requires graph_handle, dtype$, shape_array, op_name$");
        *out_result = false; return;
    }
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);

    TF_DataType dtype = StringToTFDataType(g_to_string(args[1]));
    const auto& shape_array = std::get<std::shared_ptr<Array>>(args[2]);
    std::string op_name = g_to_string(args[3]);

    TF_Status* status = TF_NewStatus();
    TF_OperationDescription* desc = TF_NewOperation(graph, "Placeholder", op_name.c_str());

    TF_SetAttrType(desc, "dtype", dtype);

    std::vector<int64_t> dims;
    for (const auto& val : shape_array->data) {
        dims.push_back(static_cast<int64_t>(to_double(val)));
    }
    TF_SetAttrShape(desc, "shape", dims.data(), dims.size());

    TF_Operation* op = TF_FinishOperation(desc, status);
    if (!CheckTFStatus(vm, status, "Failed to create Placeholder")) {
        TF_DeleteStatus(status);
        *out_result = false; return;
    }
    TF_DeleteStatus(status);

    *out_result = CreateOpOutputHandle(op);
}

// TENSORFLOW.CONSTANT(graph_handle, tensor_handle, op_name$) -> handle
void builtin_tf_constant(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 4) {
        g_error_set(8, vm.runtime_current_line, "CONSTANT requires 4 arguments: graph, value, dtype$, op_name$");
        *out_result = false; return;
    }
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);
    const BasicValue& value = args[1];
    TF_DataType dtype = StringToTFDataType(g_to_string(args[2]));
    std::string op_name = g_to_string(args[3]);

    TF_Tensor* tensor = nullptr;
    TF_Status* status = TF_NewStatus();

    // Intelligently create either a SCALAR or an ARRAY tensor
    if (std::holds_alternative<std::shared_ptr<Array>>(value)) {
        // Value is a jdBasic Array, create a tensor from it
        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(value);
        tensor = BasicArrayToTensor(vm, arr_ptr, dtype);
    }
    else {
        // Value is a scalar number, create a rank-0 tensor
        void* scalar_data = malloc(TF_DataTypeSize(dtype));
        if (!scalar_data) {
            g_error_set(7, vm.runtime_current_line, "Out of memory for scalar constant.");
            TF_DeleteStatus(status); *out_result = false; return;
        }

        // Populate the buffer based on the requested type
        switch (dtype) {
        case TF_FLOAT:  *static_cast<float*>(scalar_data) = to_double(value); break;
        case TF_DOUBLE: *static_cast<double*>(scalar_data) = to_double(value); break;
        case TF_INT32:  *static_cast<int32_t*>(scalar_data) = to_double(value); break;
        case TF_INT64:  *static_cast<int64_t*>(scalar_data) = to_double(value); break;
        default:
            g_error_set(15, vm.runtime_current_line, "Unsupported scalar dtype for CONSTANT.");
            free(scalar_data);
            TF_DeleteStatus(status); *out_result = false; return;
        }

        tensor = TF_NewTensor(dtype, nullptr, 0, scalar_data, TF_DataTypeSize(dtype), &FreeTensorData, nullptr);
    }

    if (!tensor) {
        g_error_set(15, vm.runtime_current_line, "Failed to create tensor for constant.");
        TF_DeleteStatus(status); *out_result = false; return;
    }

    TF_OperationDescription* desc = TF_NewOperation(graph, "Const", op_name.c_str());
    TF_SetAttrTensor(desc, "value", tensor, status);
    if (!CheckTFStatus(vm, status, "Failed to set 'value' attribute for Constant")) {
        TF_DeleteTensor(tensor); TF_DeleteStatus(status); *out_result = false; return;
    }

    TF_SetAttrType(desc, "dtype", TF_TensorType(tensor));
    TF_DeleteTensor(tensor);

    TF_Operation* op = TF_FinishOperation(desc, status);
    if (!CheckTFStatus(vm, status, "Failed to create Constant")) {
        TF_DeleteStatus(status); *out_result = false; return;
    }

    TF_DeleteStatus(status);
    *out_result = CreateOpOutputHandle(op);
}

// TENSORFLOW.ADD_OPERATION(graph, op_type$, op_name$, inputs_array, attrs_map) -> handle
void builtin_tf_add_operation(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 5) {
        g_error_set(8, vm.runtime_current_line, "ADD_OPERATION requires 5 arguments.");
        *out_result = false; return;
    }
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);
    std::string op_type = g_to_string(args[1]);
    std::string op_name = g_to_string(args[2]);
    const auto& inputs_array = std::get<std::shared_ptr<Array>>(args[3]);
    const auto& attrs_map_ptr = std::get<std::shared_ptr<Map>>(args[4]);

    TF_Status* status = TF_NewStatus();
    TF_OperationDescription* desc = TF_NewOperation(graph, op_type.c_str(), op_name.c_str());

    // Add regular data inputs
    if (inputs_array) {
        for (const auto& input_val : inputs_array->data) {
            const auto& input_handle = std::get<std::shared_ptr<OpaqueHandle>>(input_val);
            if (input_handle && input_handle->type_name == "TF_OP_OUTPUT") {
                TF_Output* input_op_output = static_cast<TF_Output*>(input_handle->ptr);
                TF_AddInput(desc, *input_op_output);
            }
        }
    }

    // Handle attributes and control inputs from the map
    if (attrs_map_ptr) {
        auto attrs_copy = attrs_map_ptr->data;

        // Check for and specifically handle the "control_inputs" key
        if (attrs_copy.count("control_inputs")) {
            const auto& control_inputs_val = attrs_copy.at("control_inputs");
            if (std::holds_alternative<std::shared_ptr<Array>>(control_inputs_val)) {
                const auto& control_array = std::get<std::shared_ptr<Array>>(control_inputs_val);
                for (const auto& control_val : control_array->data) {
                    const auto& handle = std::get<std::shared_ptr<OpaqueHandle>>(control_val);
                    if (handle && handle->type_name == "TF_OP_OUTPUT") {
                        TF_Output* output = static_cast<TF_Output*>(handle->ptr);
                        TF_AddControlInput(desc, output->oper);
                    }
                }
            }
            // Remove it from the map so SetOperationAttributes doesn't see it
            attrs_copy.erase("control_inputs");
        }

        // Pass the remaining items to the attribute setter
        auto remaining_attrs_map = std::make_shared<Map>();
        remaining_attrs_map->data = attrs_copy;
        SetOperationAttributes(vm, desc, remaining_attrs_map);
    }

    TF_Operation* op = TF_FinishOperation(desc, status);
    if (!CheckTFStatus(vm, status, "Failed to create operation: " + op_type)) {
        TF_DeleteStatus(status);
        *out_result = false; return;
    }
    TF_DeleteStatus(status);

    *out_result = CreateOpOutputHandle(op);
}

// TENSORFLOW.ADD_GRADIENTS(graph, loss_handle, vars_array) -> array of handles
void builtin_tf_add_gradients(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 3) { *out_result = {}; return; }
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    const auto& y_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[1]);
    const auto& x_array = std::get<std::shared_ptr<Array>>(args[2]);
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);

    // Prepare loss output (y)
    TF_Output* y_output = static_cast<TF_Output*>(y_handle->ptr);

    // Prepare variables array (x)
    std::vector<TF_Output> x_outputs;
    for (const auto& x_val : x_array->data) {
        const auto& x_handle = std::get<std::shared_ptr<OpaqueHandle>>(x_val);
        x_outputs.push_back(*static_cast<TF_Output*>(x_handle->ptr));
    }

    // Prepare gradients array (dy)
    std::vector<TF_Output> dy_outputs(x_outputs.size());

    TF_Status* status = TF_NewStatus();
    TF_AddGradients(graph, y_output, 1, x_outputs.data(), x_outputs.size(), nullptr, status, dy_outputs.data());

    if (!CheckTFStatus(vm, status, "Failed to add gradients")) {
        TF_DeleteStatus(status);
        *out_result = {}; return;
    }
    TF_DeleteStatus(status);

    // Package results
    auto results_array = std::make_shared<Array>();
    results_array->shape = { dy_outputs.size() };
    for (const auto& grad_out : dy_outputs) {
        results_array->data.push_back(CreateOpOutputHandle(grad_out.oper, grad_out.index));
    }
    *out_result = results_array;
}

// TENSORFLOW.GLOBAL_VARIABLES_INITIALIZER(graph) -> handle
void builtin_tf_global_variables_initializer(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, "GLOBAL_VARIABLES_INITIALIZER requires a graph handle.");
        *out_result = false; return;
    }
    const auto& graph_handle = std::get<std::shared_ptr<OpaqueHandle>>(args[0]);
    TF_Graph* graph = static_cast<TF_Graph*>(graph_handle->ptr);

    TF_Status* status = TF_NewStatus();
    TF_OperationDescription* desc = TF_NewOperation(graph, "NoOp", "init");

    size_t pos = 0;
    TF_Operation* op;
    while ((op = TF_GraphNextOperation(graph, &pos)) != nullptr) {
        if (strcmp(TF_OperationOpType(op), "VarHandleOp") == 0) {
            TF_AddControlInput(desc, op);
        }
    }

    TF_Operation* init_op = TF_FinishOperation(desc, status);
    if (!CheckTFStatus(vm, status, "Failed to create initializer")) {
        TF_DeleteStatus(status);
        *out_result = false; return;
    }
    TF_DeleteStatus(status);

    *out_result = CreateOpOutputHandle(init_op);
}



// === REGISTRATION LOGIC ===

void register_tensorflow_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
    auto register_func = [&](const std::string& name, int arity, NativeDLLFunction func_ptr, bool is_proc = false) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_dll_impl = func_ptr;
        info.is_procedure = is_proc;
        table[g_to_upper(name)] = info;
        };

    // Core Functions
    register_func("TENSORFLOW.VERSION", 0, builtin_tf_version);
    register_func("TENSORFLOW.NEW_GRAPH", 0, builtin_tf_new_graph);
    register_func("TENSORFLOW.NEW_SESSION", 1, builtin_tf_new_session);

    // Graph Loading
    register_func("TENSORFLOW.LOAD_GRAPH_FROM_FILE", 2, builtin_tf_load_graph, true); // Procedure

    // Tensor Conversion & Inspection
    register_func("TENSORFLOW.NEW_TENSOR_FROM_ARRAY", -1, builtin_tf_new_tensor); // Arity -1 for optional args
    register_func("TENSORFLOW.NEW_ARRAY_FROM_TENSOR", 1, builtin_tf_array_from_tensor);
    register_func("TENSORFLOW.GET_TENSOR_SHAPE", 1, builtin_tf_get_tensor_shape);
    register_func("TENSORFLOW.GET_TENSOR_DTYPE", 1, builtin_tf_get_tensor_dtype);

    // Execution
    register_func("TENSORFLOW.SESSION_RUN", 5, builtin_tf_session_run);

    // --- Tier 2 Functions (New) ---
    register_func("TENSORFLOW.PLACEHOLDER", 4, builtin_tf_placeholder);
    register_func("TENSORFLOW.CONSTANT", 4, builtin_tf_constant);
    register_func("TENSORFLOW.ADD_OPERATION", 5, builtin_tf_add_operation);
    register_func("TENSORFLOW.ADD_GRADIENTS", 3, builtin_tf_add_gradients);
    register_func("TENSORFLOW.GLOBAL_VARIABLES_INITIALIZER", 1, builtin_tf_global_variables_initializer);

}

// The main entry point of the DLL.
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }

    // Store the callback function pointers provided by the host application
    g_error_set = services->error_set;
    g_to_upper = services->to_upper;
    g_to_string = services->to_string;

    // Register all our new TensorFlow functions with the interpreter's main function table.
    register_tensorflow_functions(*vm, vm->main_function_table);
}