#include "ai.h"
#include "vm.h"
#include "errors.h"

#ifndef ONNX
// Stub when ONNX Runtime not available
void register_ai_builtins(VM& vm) {
    vm.register_native("AI.LOAD", [](const std::vector<Value>&) -> Value {
        throw jdError(ErrCode::RUNTIME_ERROR, "AI requires ONNX build (build.bat ONNX)");
    });
}
#else // ONNX

#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <iostream>
#include <fstream>

// ── ONNX Runtime Environment (singleton) ────────────────────────

static Ort::Env* g_ort_env = nullptr;

static Ort::Env& get_env() {
    if (!g_ort_env) {
        g_ort_env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "jdBasic");
    }
    return *g_ort_env;
}

// ── Model storage ───────────────────────────────────────────────

struct OnnxModel {
    Ort::Session session{nullptr};
    Ort::AllocatorWithDefaultOptions allocator;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<std::vector<int64_t>> input_shapes;
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<ONNXTensorElementDataType> input_types;
    std::vector<ONNXTensorElementDataType> output_types;
};

static int g_next_model_id = 1;
static std::unordered_map<int, std::unique_ptr<OnnxModel>> g_models;

// ── Helpers ─────────────────────────────────────────────────────

static std::string onnx_type_name(ONNXTensorElementDataType t) {
    switch (t) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return "FLOAT32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:  return "FLOAT64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return "INT32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:   return "INT64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    return "INT8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   return "UINT8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING:  return "STRING";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:    return "BOOL";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return "FLOAT16";
        default: return "UNKNOWN";
    }
}

static std::string shape_to_string(const std::vector<int64_t>& shape) {
    std::string s = "[";
    for (size_t i = 0; i < shape.size(); i++) {
        if (i > 0) s += ", ";
        if (shape[i] < 0) s += "?"; else s += std::to_string(shape[i]);
    }
    s += "]";
    return s;
}

// Flatten a jdBasic Value (array/nested array) into a flat float vector
static void flatten_to_float(const Value& v, std::vector<float>& out) {
    if (v.type == ValueType::ARRAY) {
        for (auto& e : v.as_array()->elements) flatten_to_float(e, out);
    } else {
        out.push_back((float)v.to_double());
    }
}

// Flatten to int64
static void flatten_to_int64(const Value& v, std::vector<int64_t>& out) {
    if (v.type == ValueType::ARRAY) {
        for (auto& e : v.as_array()->elements) flatten_to_int64(e, out);
    } else {
        out.push_back(v.to_int());
    }
}

// Infer shape from nested array
static std::vector<int64_t> infer_shape(const Value& v) {
    std::vector<int64_t> shape;
    const Value* cur = &v;
    while (cur->type == ValueType::ARRAY && !cur->as_array()->elements.empty()) {
        shape.push_back((int64_t)cur->as_array()->elements.size());
        cur = &cur->as_array()->elements[0];
    }
    return shape;
}

// Build nested array from flat data + shape
static Value build_array(const float* data, const std::vector<int64_t>& shape, size_t dim, size_t& offset) {
    if (dim == shape.size() - 1) {
        // Last dimension: create flat array
        Value arr = Value::make_array();
        for (int64_t i = 0; i < shape[dim]; i++) {
            arr.as_array()->elements.push_back(Value::make_f64(data[offset++]));
        }
        return arr;
    }
    Value arr = Value::make_array();
    for (int64_t i = 0; i < shape[dim]; i++) {
        arr.as_array()->elements.push_back(build_array(data, shape, dim + 1, offset));
    }
    return arr;
}

static Value build_array_i64(const int64_t* data, const std::vector<int64_t>& shape, size_t dim, size_t& offset) {
    if (dim == shape.size() - 1) {
        Value arr = Value::make_array();
        for (int64_t i = 0; i < shape[dim]; i++) {
            arr.as_array()->elements.push_back(Value::make_i64(data[offset++]));
        }
        return arr;
    }
    Value arr = Value::make_array();
    for (int64_t i = 0; i < shape[dim]; i++) {
        arr.as_array()->elements.push_back(build_array_i64(data, shape, dim + 1, offset));
    }
    return arr;
}

// ── Register builtins ───────────────────────────────────────────

void register_ai_builtins(VM& vm) {

    // ── AI.LOAD(filename$) -> model_id ──────────────────────────

    vm.register_native("AI.LOAD", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;

        auto model = std::make_unique<OnnxModel>();
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(0); // auto
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (const char* prov = std::getenv("JDB_ONNX_PROVIDER")) {
            std::string p = prov;
            std::transform(p.begin(), p.end(), p.begin(), ::tolower);
            if (p == "cuda") {
                OrtCUDAProviderOptions cuda_opts{};
                cuda_opts.device_id = 0;
                opts.AppendExecutionProvider_CUDA(cuda_opts);
            } else if (p == "tensorrt") {
                OrtTensorRTProviderOptions trt_opts{};
                trt_opts.device_id = 0;
                opts.AppendExecutionProvider_TensorRT(trt_opts);
            }
        }

        try {
            // Convert path to wide string for Windows
#ifdef _WIN32
            std::wstring wpath(path.begin(), path.end());
            model->session = Ort::Session(get_env(), wpath.c_str(), opts);
#else
            model->session = Ort::Session(get_env(), path.c_str(), opts);
#endif
        } catch (const Ort::Exception& e) {
            throw jdError(ErrCode::RUNTIME_ERROR, "AI.LOAD failed: " + std::string(e.what()));
        }

        // Collect input info
        size_t num_inputs = model->session.GetInputCount();
        for (size_t i = 0; i < num_inputs; i++) {
            auto name = model->session.GetInputNameAllocated(i, model->allocator);
            model->input_names.push_back(name.get());
            auto type_info = model->session.GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            model->input_shapes.push_back(tensor_info.GetShape());
            model->input_types.push_back(tensor_info.GetElementType());
        }

        // Collect output info
        size_t num_outputs = model->session.GetOutputCount();
        for (size_t i = 0; i < num_outputs; i++) {
            auto name = model->session.GetOutputNameAllocated(i, model->allocator);
            model->output_names.push_back(name.get());
            auto type_info = model->session.GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            model->output_shapes.push_back(tensor_info.GetShape());
            model->output_types.push_back(tensor_info.GetElementType());
        }

        int id = g_next_model_id++;
        g_models[id] = std::move(model);
        return Value::make_i64(id);
    });

    // ── AI.INFO(model_id) -> object ─────────────────────────────

    vm.register_native("AI.INFO", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_models.find(id);
        if (it == g_models.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "AI.INFO: invalid model id");
        auto& m = *it->second;

        Value info = Value::make_object();

        // Inputs
        Value inputs = Value::make_array();
        for (size_t i = 0; i < m.input_names.size(); i++) {
            Value inp = Value::make_object();
            inp.as_object()->set("name", Value::make_string(m.input_names[i]));
            inp.as_object()->set("shape", Value::make_string(shape_to_string(m.input_shapes[i])));
            inp.as_object()->set("type", Value::make_string(onnx_type_name(m.input_types[i])));
            inputs.as_array()->elements.push_back(std::move(inp));
        }
        info.as_object()->set("inputs", std::move(inputs));

        // Outputs
        Value outputs = Value::make_array();
        for (size_t i = 0; i < m.output_names.size(); i++) {
            Value out = Value::make_object();
            out.as_object()->set("name", Value::make_string(m.output_names[i]));
            out.as_object()->set("shape", Value::make_string(shape_to_string(m.output_shapes[i])));
            out.as_object()->set("type", Value::make_string(onnx_type_name(m.output_types[i])));
            outputs.as_array()->elements.push_back(std::move(out));
        }
        info.as_object()->set("outputs", std::move(outputs));

        return info;
    });

    // ── AI.RUN(model_id, input1, [input2, ...]) -> output ───────
    // Inputs: arrays (or nested arrays) matching the model's expected shapes
    // For single-input models: AI.RUN(id, data_array)
    // For multi-input: AI.RUN(id, [input1, input2, ...])

    vm.register_native("AI.RUN", 2, -1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_models.find(id);
        if (it == g_models.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "AI.RUN: invalid model id");
        auto& m = *it->second;

        size_t num_inputs = m.input_names.size();
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Prepare input tensors
        std::vector<Ort::Value> input_tensors;
        std::vector<std::vector<float>> float_buffers;
        std::vector<std::vector<int64_t>> int64_buffers;

        for (size_t i = 0; i < num_inputs; i++) {
            const Value& input_val = (num_inputs == 1) ? args[1]
                : (args[1].type == ValueType::ARRAY && args[1].as_array()->elements.size() > i)
                    ? args[1].as_array()->elements[i]
                    : args[i + 1];

            // Determine shape: prefer model's known shape over inferred
            auto& model_shape = m.input_shapes[i];
            std::vector<int64_t> shape;
            bool model_shape_complete = !model_shape.empty();
            for (auto d : model_shape) { if (d < 0) model_shape_complete = false; }

            if (model_shape_complete) {
                // Model has fully defined shape - use it directly
                shape = model_shape;
            } else {
                // Infer shape from data, fill in dynamic dims from data
                shape = infer_shape(input_val);
                if (shape.size() == model_shape.size()) {
                    for (size_t d = 0; d < shape.size(); d++) {
                        if (model_shape[d] > 0) shape[d] = model_shape[d];
                    }
                }
            }

            // Create tensor based on expected type
            if (m.input_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
                m.input_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
                int64_buffers.emplace_back();
                flatten_to_int64(input_val, int64_buffers.back());
                if (shape.empty()) shape = {(int64_t)int64_buffers.back().size()};
                input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
                    mem_info, int64_buffers.back().data(), int64_buffers.back().size(),
                    shape.data(), shape.size()));
            } else {
                // Default: float
                float_buffers.emplace_back();
                flatten_to_float(input_val, float_buffers.back());
                if (shape.empty()) shape = {(int64_t)float_buffers.back().size()};
                input_tensors.push_back(Ort::Value::CreateTensor<float>(
                    mem_info, float_buffers.back().data(), float_buffers.back().size(),
                    shape.data(), shape.size()));
            }
        }

        // Build name arrays
        std::vector<const char*> input_name_ptrs, output_name_ptrs;
        for (auto& n : m.input_names) input_name_ptrs.push_back(n.c_str());
        for (auto& n : m.output_names) output_name_ptrs.push_back(n.c_str());

        // Run inference
        std::vector<Ort::Value> output_tensors;
        try {
            output_tensors = m.session.Run(
                Ort::RunOptions{nullptr},
                input_name_ptrs.data(), input_tensors.data(), input_tensors.size(),
                output_name_ptrs.data(), output_name_ptrs.size());
        } catch (const Ort::Exception& e) {
            throw jdError(ErrCode::RUNTIME_ERROR, "AI.RUN failed: " + std::string(e.what()));
        }

        // Convert outputs to jdBasic Values
        auto convert_output = [](Ort::Value& tensor) -> Value {
            auto info = tensor.GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape();
            auto type = info.GetElementType();
            size_t total = info.GetElementCount();

            if (total == 1) {
                // Scalar output
                if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                    return Value::make_f64(tensor.GetTensorData<float>()[0]);
                if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
                    return Value::make_i64(tensor.GetTensorData<int64_t>()[0]);
                if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE)
                    return Value::make_f64(tensor.GetTensorData<double>()[0]);
            }

            // Multi-element: build nested array matching shape
            if (shape.empty() || (shape.size() == 1 && shape[0] <= 0)) {
                shape = {(int64_t)total};
            }

            if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const float* data = tensor.GetTensorData<float>();
                size_t offset = 0;
                return build_array(data, shape, 0, offset);
            }
            if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
                const int64_t* data = tensor.GetTensorData<int64_t>();
                size_t offset = 0;
                return build_array_i64(data, shape, 0, offset);
            }
            if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE) {
                const double* data = tensor.GetTensorData<double>();
                // Convert to float array
                std::vector<float> fdata(total);
                for (size_t i = 0; i < total; i++) fdata[i] = (float)data[i];
                size_t offset = 0;
                return build_array(fdata.data(), shape, 0, offset);
            }

            // Fallback: flat float array
            Value arr = Value::make_array();
            if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const float* data = tensor.GetTensorData<float>();
                for (size_t i = 0; i < total; i++)
                    arr.as_array()->elements.push_back(Value::make_f64(data[i]));
            }
            return arr;
        };

        if (output_tensors.size() == 1) {
            return convert_output(output_tensors[0]);
        }

        // Multiple outputs: return array of outputs
        Value results = Value::make_array();
        for (auto& t : output_tensors) {
            results.as_array()->elements.push_back(convert_output(t));
        }
        return results;
    });

    // ── AI.FREE(model_id) ───────────────────────────────────────

    vm.register_native("AI.FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        g_models.erase(id);
        return Value::make_none();
    });

    // ── AI.LIST() -> array of model ids ─────────────────────────

    vm.register_native("AI.LIST", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        Value arr = Value::make_array();
        for (auto& [id, m] : g_models)
            arr.as_array()->elements.push_back(Value::make_i64(id));
        return arr;
    });

    // ── AI.TENSOR(shape_array, [data_array]) -> flat_array ──────
    // Helper to create properly shaped input tensors

    vm.register_native("AI.TENSOR", 1, 2, [](const std::vector<Value>& args) -> Value {
        // Create a zero-filled tensor with the given shape
        auto* shape_arr = args[0].as_array();
        int64_t total = 1;
        for (auto& s : shape_arr->elements) total *= s.to_int();

        Value result = Value::make_array();
        if (args.size() >= 2 && args[1].type == ValueType::ARRAY) {
            // Fill from data
            auto* data = args[1].as_array();
            for (int64_t i = 0; i < total; i++) {
                if (i < (int64_t)data->elements.size())
                    result.as_array()->elements.push_back(data->elements[i]);
                else
                    result.as_array()->elements.push_back(Value::make_f64(0.0));
            }
        } else {
            for (int64_t i = 0; i < total; i++)
                result.as_array()->elements.push_back(Value::make_f64(0.0));
        }

        // Reshape into nested array matching shape
        // For now return flat - AI.RUN handles flat arrays fine
        return result;
    });

    // ── AI.SOFTMAX(array) -> array ──────────────────────────────

    vm.register_native("AI.SOFTMAX", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        if (!arr || arr->elements.empty()) return args[0];

        // Find max for numerical stability
        double max_val = arr->elements[0].to_double();
        for (auto& e : arr->elements) max_val = std::max(max_val, e.to_double());

        double sum = 0;
        std::vector<double> exps(arr->elements.size());
        for (size_t i = 0; i < arr->elements.size(); i++) {
            exps[i] = std::exp(arr->elements[i].to_double() - max_val);
            sum += exps[i];
        }

        Value result = Value::make_array();
        for (size_t i = 0; i < exps.size(); i++)
            result.as_array()->elements.push_back(Value::make_f64(exps[i] / sum));
        return result;
    });

    // ── AI.ARGMAX(array) -> index ───────────────────────────────

    vm.register_native("AI.ARGMAX", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        if (!arr || arr->elements.empty()) return Value::make_i64(0);
        int best = 0;
        double best_val = arr->elements[0].to_double();
        for (size_t i = 1; i < arr->elements.size(); i++) {
            double v = arr->elements[i].to_double();
            if (v > best_val) { best_val = v; best = (int)i; }
        }
        return Value::make_i64(best);
    });

    // ── AI.TOPK(array, k) -> array of [index, value] ───────────

    vm.register_native("AI.TOPK", 2, 2, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        int k = (int)args[1].to_int();
        if (!arr || arr->elements.empty()) return Value::make_array();

        // Build index-value pairs
        std::vector<std::pair<int, double>> pairs;
        for (size_t i = 0; i < arr->elements.size(); i++)
            pairs.push_back({(int)i, arr->elements[i].to_double()});

        // Partial sort by value descending
        k = std::min(k, (int)pairs.size());
        std::partial_sort(pairs.begin(), pairs.begin() + k, pairs.end(),
            [](auto& a, auto& b) { return a.second > b.second; });

        Value result = Value::make_array();
        for (int i = 0; i < k; i++) {
            Value pair = Value::make_array();
            pair.as_array()->elements.push_back(Value::make_i64(pairs[i].first));
            pair.as_array()->elements.push_back(Value::make_f64(pairs[i].second));
            result.as_array()->elements.push_back(std::move(pair));
        }
        return result;
    });
}

#endif // ONNX
