#include "AIFunctions.hpp"
#include "Error.hpp"
#include "Types.hpp"
#include "BuiltinFunctions.hpp" // For functions like builtin_transpose
#include "Commands.hpp"       // For utility functions like to_double, to_string
#include <random>
#include <functional>
#include <unordered_set>
#include <cmath>
#include <fstream>
#include <limits>
#include <algorithm>
#include <memory>
#include <omp.h>

// Forward declarations for functions defined in this file
BasicValue tensor_add(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_subtract(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_elementwise_multiply(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_power(NeReLaBasic& vm, const BasicValue& base, const BasicValue& exponent);
BasicValue tensor_scalar_divide(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
std::shared_ptr<Array> array_add(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);
std::shared_ptr<Array> array_subtract(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);

//std::shared_ptr<FloatArray> float_array_add(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b);
//std::shared_ptr<FloatArray> float_array_subtract(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b);
//std::shared_ptr<FloatArray> float_array_elementwise_multiply(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b);
//std::shared_ptr<FloatArray> float_array_scalar_multiply(double scalar, const std::shared_ptr<FloatArray>& arr);
//std::shared_ptr<FloatArray> float_array_sum_along_axis(const std::shared_ptr<FloatArray>& arr, int axis);

// --- Helper Namespace for AI-specific utilities ---
namespace {

    // A numerically stable implementation of the sigmoid function.
    double sigmoid(double x) {
        return 1.0 / (1.0 + std::exp(-x));
    }

    /**
         * @brief Creates a FloatArray with a given shape, initialized with random values.
         */
    std::shared_ptr<FloatArray> create_randomized_float_array(const std::vector<size_t>& shape, size_t fan_in, size_t fan_out) {
        auto array_ptr = std::make_shared<FloatArray>();
        array_ptr->shape = shape;

        double limit = std::sqrt(6.0 / (fan_in + fan_out));
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> distr(-limit, limit);

        size_t total_elements = array_ptr->size();
        array_ptr->data.reserve(total_elements);

        for (size_t i = 0; i < total_elements; ++i) {
            array_ptr->data.push_back(distr(gen));
        }
        return array_ptr;
    }

    std::shared_ptr<FloatArray> transpose_float_array(const std::shared_ptr<FloatArray>& arr) {
        if (!arr || arr->shape.size() != 2) return nullptr;
        size_t rows = arr->shape[0];
        size_t cols = arr->shape[1];
        auto result = std::make_shared<FloatArray>();
        result->shape = { cols, rows };
        result->data.resize(rows * cols);
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                result->data[c * rows + r] = arr->data[r * cols + c];
            }
        }
        return result;
    }

    std::shared_ptr<FloatArray> float_array_matmul(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b) {
        if (!a || !b || a->shape.size() != 2 || b->shape.size() != 2) return nullptr;
        size_t rA = a->shape[0], cA = a->shape[1], rB = b->shape[0], cB = b->shape[1];
        if (cA != rB) return nullptr;

        auto result_ptr = std::make_shared<FloatArray>();
        result_ptr->shape = { rA, cB };
        result_ptr->data.assign(rA * cB, 0.0);
#pragma omp parallel for collapse(2)
        for (auto r = 0; r < rA; ++r) {
            for (auto c = 0; c < cB; ++c) {
                for (auto i = 0; i < cA; ++i) {
                    result_ptr->data[r * cB + c] += a->data[r * cA + i] * b->data[i * cB + c];
                }
            }
        }
        return result_ptr;
    }

    // --- All helper functions now operate on FloatArray and std::vector<double> ---

    std::shared_ptr<FloatArray> float_array_elementwise_multiply(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b) {
        if (!a || !b || a->shape != b->shape) return nullptr;
        auto result_ptr = std::make_shared<FloatArray>();
        result_ptr->shape = a->shape;
        result_ptr->data.reserve(a->data.size());
        for (size_t i = 0; i < a->data.size(); ++i) {
            result_ptr->data.push_back(a->data[i] * b->data[i]);
        }
        return result_ptr;
    }

    std::shared_ptr<FloatArray> float_array_scalar_multiply(double scalar, const std::shared_ptr<FloatArray>& arr) {
        if (!arr) return nullptr;
        auto result_ptr = std::make_shared<FloatArray>();
        result_ptr->shape = arr->shape;
        result_ptr->data.reserve(arr->data.size());
        for (const auto& elem : arr->data) {
            result_ptr->data.push_back(elem * scalar);
        }
        return result_ptr;
    }

    std::shared_ptr<FloatArray> float_array_sum_along_axis(const std::shared_ptr<FloatArray>& arr, int axis) {
        if (!arr || arr->shape.size() != 2) return nullptr;
        size_t rows = arr->shape[0];
        size_t cols = arr->shape[1];
        auto result_ptr = std::make_shared<FloatArray>();

        if (axis == 0) {
            result_ptr->shape = { 1, cols };
            result_ptr->data.assign(cols, 0.0);
            for (size_t c = 0; c < cols; ++c) {
                for (size_t r = 0; r < rows; ++r) {
                    result_ptr->data[c] += arr->data[r * cols + c];
                }
            }
        }
        else if (axis == 1) {
            result_ptr->shape = { rows, 1 };
            result_ptr->data.reserve(rows);
            for (size_t r = 0; r < rows; ++r) {
                double row_sum = 0.0;
                for (size_t c = 0; c < cols; ++c) {
                    row_sum += arr->data[r * cols + c];
                }
                result_ptr->data.push_back(row_sum);
            }
        }
        else {
            return nullptr;
        }
        return result_ptr;
    }

    std::shared_ptr<FloatArray> float_array_add(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b) {
        if (!a || !b) return nullptr;

        if (b->size() == 1 && a->size() > 1) {
            double scalar = b->data[0];
            auto result_ptr = std::make_shared<FloatArray>();
            result_ptr->shape = a->shape;
            result_ptr->data.reserve(a->size());
            for (double elem : a->data) {
                result_ptr->data.push_back(elem + scalar);
            }
            return result_ptr;
        }
        if (a->size() == 1 && b->size() > 1) {
            double scalar = a->data[0];
            auto result_ptr = std::make_shared<FloatArray>();
            result_ptr->shape = b->shape;
            result_ptr->data.reserve(b->size());
            for (double elem : b->data) {
                result_ptr->data.push_back(scalar + elem);
            }
            return result_ptr;
        }

        if (a->shape.size() == 2 && b->shape.size() == 2 && a->shape[0] > b->shape[0] && b->shape[0] == 1 && a->shape[1] == b->shape[1]) {
            auto result_ptr = std::make_shared<FloatArray>(*a);
            for (size_t r = 0; r < a->shape[0]; ++r) {
                for (size_t c = 0; c < a->shape[1]; ++c) {
                    result_ptr->data[r * a->shape[1] + c] += b->data[c];
                }
            }
            return result_ptr;
        }

        if (a->shape == b->shape) {
            auto result_ptr = std::make_shared<FloatArray>();
            result_ptr->shape = a->shape;
            result_ptr->data.reserve(a->size());
            for (size_t i = 0; i < a->size(); ++i) {
                result_ptr->data.push_back(a->data[i] + b->data[i]);
            }
            return result_ptr;
        }
        return nullptr;
    }

    std::shared_ptr<FloatArray> float_array_subtract(const std::shared_ptr<FloatArray>& a, const std::shared_ptr<FloatArray>& b) {
        if (!a || !b) return nullptr;

        if (a->shape == b->shape) {
            auto result_ptr = std::make_shared<FloatArray>();
            result_ptr->shape = a->shape;
            result_ptr->data.reserve(a->size());
            for (size_t i = 0; i < a->size(); ++i) {
                result_ptr->data.push_back(a->data[i] - b->data[i]);
            }
            return result_ptr;
        }
        return nullptr;
    }

    std::shared_ptr<FloatArray> rotate180(const std::shared_ptr<FloatArray>& kernel) {
        if (!kernel || kernel->shape.size() != 4) return nullptr;
        auto rotated_kernel = std::make_shared<FloatArray>(*kernel);
        size_t h = kernel->shape[2];
        size_t w = kernel->shape[3];
        size_t plane_size = h * w;

        for (size_t i = 0; i < kernel->data.size() / plane_size; ++i) {
            size_t plane_start = i * plane_size;
            std::reverse(rotated_kernel->data.begin() + plane_start, rotated_kernel->data.begin() + plane_start + plane_size);
        }
        return rotated_kernel;
    }

} // end anonymous namespace

// --- Core Tensor Operations ---

/**
  * @brief Helper to add two arrays together, element-wise, with broadcasting.
  */
std::shared_ptr<Array> array_add(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b) {
    if (!a || !b) return nullptr;

    // --- Broadcasting a scalar ---
    if (b->size() == 1 && a->size() > 1) {
        double scalar = to_double(b->data[0]);
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = a->shape;
        result_ptr->data.reserve(a->size());
        for (const auto& elem : a->data) {
            result_ptr->data.push_back(to_double(elem) + scalar);
        }
        return result_ptr;
    }
    if (a->size() == 1 && b->size() > 1) {
        double scalar = to_double(a->data[0]);
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = b->shape;
        result_ptr->data.reserve(b->size());
        for (const auto& elem : b->data) {
            result_ptr->data.push_back(scalar + to_double(elem));
        }
        return result_ptr;
    }

    // --- Broadcasting a row vector [1, C] to a matrix [R, C] ---
    if (a->shape.size() == 2 && b->shape.size() == 2 && b->shape[0] == 1 && a->shape[1] == b->shape[1]) {
        // This handles Matrix + Vector
        auto result_ptr = std::make_shared<Array>(*a); // Copy the larger matrix
        for (size_t r = 0; r < a->shape[0]; ++r) {
            for (size_t c = 0; c < a->shape[1]; ++c) {
                result_ptr->data[r * a->shape[1] + c] = to_double(a->data[r * a->shape[1] + c]) + to_double(b->data[c]);
            }
        }
        return result_ptr;
    }
    if (b->shape.size() == 2 && a->shape.size() == 2 && a->shape[0] == 1 && b->shape[1] == a->shape[1]) {
        // This handles Vector + Matrix
        auto result_ptr = std::make_shared<Array>(*b); // Copy the larger matrix
        for (size_t r = 0; r < b->shape[0]; ++r) {
            for (size_t c = 0; c < b->shape[1]; ++c) {
                result_ptr->data[r * b->shape[1] + c] = to_double(a->data[c]) + to_double(b->data[r * b->shape[1] + c]);
            }
        }
        return result_ptr;
    }

    // --- Standard element-wise addition for arrays of the exact same shape ---
    if (a->shape == b->shape) {
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = a->shape;
        result_ptr->data.reserve(a->size());
        for (size_t i = 0; i < a->size(); ++i) {
            result_ptr->data.push_back(to_double(a->data[i]) + to_double(b->data[i]));
        }
        return result_ptr;
    }

    // If no broadcasting rule applies and shapes are different, it's an error.
    return nullptr;
}


/**
 * @brief Helper to subtract one array from another, element-wise, with broadcasting.
 */
std::shared_ptr<Array> array_subtract(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b) {
    if (!a || !b) return nullptr;

    // Broadcasting a scalar from the right
    if (b->size() == 1 && a->size() > 1) {
        double scalar = to_double(b->data[0]);
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = a->shape;
        result_ptr->data.reserve(a->size());
        for (const auto& elem : a->data) {
            result_ptr->data.push_back(to_double(elem) - scalar);
        }
        return result_ptr;
    }
    // Broadcasting a scalar from the left
    if (a->size() == 1 && b->size() > 1) {
        double scalar = to_double(a->data[0]);
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = b->shape;
        result_ptr->data.reserve(b->size());
        for (const auto& elem : b->data) {
            result_ptr->data.push_back(scalar - to_double(elem));
        }
        return result_ptr;
    }

    // Broadcasting a row vector from the right
    if (a->shape.size() == 2 && b->shape.size() == 2 && a->shape[0] > 1 && b->shape[0] == 1 && a->shape[1] == b->shape[1]) {
        auto result_ptr = std::make_shared<Array>(*a); // Copy matrix
        for (size_t r = 0; r < a->shape[0]; ++r) {
            for (size_t c = 0; c < a->shape[1]; ++c) {
                result_ptr->data[r * a->shape[1] + c] = to_double(a->data[r * a->shape[1] + c]) - to_double(b->data[c]);
            }
        }
        return result_ptr;
    }

    // Standard element-wise subtraction
    if (a->shape != b->shape) {
        return nullptr; // Error: shapes not compatible
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = a->shape;
    result_ptr->data.reserve(a->size());
    for (size_t i = 0; i < a->size(); ++i) {
        result_ptr->data.push_back(to_double(a->data[i]) - to_double(b->data[i]));
    }
    return result_ptr;
}

/**
 * @brief Adds two Tensors, handling broadcasting and creating the backward pass.
 */
BasicValue tensor_add(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b) {
    const auto& a_tensor = std::get<std::shared_ptr<Tensor>>(a);
    const auto& b_tensor = std::get<std::shared_ptr<Tensor>>(b);

    auto result_data = float_array_add(a_tensor->data, b_tensor->data);
    if (!result_data) {
        Error::set(15, vm.runtime_current_line, "Tensor shapes not compatible for addition.");
        return {};
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_data;
    result_tensor->parents = { a_tensor, b_tensor };

    result_tensor->backward_fn = [a_tensor, b_tensor](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto grad_a = output_grad;
        auto grad_b = output_grad;

        if (a_tensor->data->size() > b_tensor->data->size()) {
            if (b_tensor->data->shape.size() == 2 && b_tensor->data->shape[0] == 1) {
                auto summed_grad_data = float_array_sum_along_axis(output_grad->data, 0);
                auto summed_grad_tensor = std::make_shared<Tensor>();
                summed_grad_tensor->data = summed_grad_data;
                grad_b = summed_grad_tensor;
            }
        }

        return { grad_a, grad_b };
        };

    return result_tensor;
}

/**
 * @brief Subtracts two Tensors and creates the backward pass.
 */
BasicValue tensor_subtract(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b) {
    const auto& a_tensor = std::get<std::shared_ptr<Tensor>>(a);
    const auto& b_tensor = std::get<std::shared_ptr<Tensor>>(b);

    auto result_data = float_array_subtract(a_tensor->data, b_tensor->data);
    if (!result_data) {
        Error::set(15, vm.runtime_current_line, "Tensor shapes not compatible for subtraction.");
        return {};
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_data;
    result_tensor->parents = { a_tensor, b_tensor };

    result_tensor->backward_fn = [](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto neg_grad_data = float_array_scalar_multiply(-1.0, output_grad->data);
        auto neg_grad_tensor = std::make_shared<Tensor>();
        neg_grad_tensor->data = neg_grad_data;
        return { output_grad, neg_grad_tensor };
        };
    return result_tensor;
}

/**
 * @brief Element-wise multiplication of two Tensors and creates the backward pass.
 */
BasicValue tensor_elementwise_multiply(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b) {
    const auto& a_tensor = std::get<std::shared_ptr<Tensor>>(a);
    const auto& b_tensor = std::get<std::shared_ptr<Tensor>>(b);

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = float_array_elementwise_multiply(a_tensor->data, b_tensor->data);
    if (!result_tensor->data) {
        Error::set(15, vm.runtime_current_line, "Tensor shapes not compatible for element-wise multiplication.");
        return {};
    }

    result_tensor->parents = { a_tensor, b_tensor };
    result_tensor->backward_fn = [a_tensor, b_tensor](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto grad_a_tensor = std::make_shared<Tensor>();
        grad_a_tensor->data = float_array_elementwise_multiply(output_grad->data, b_tensor->data);

        auto grad_b_tensor = std::make_shared<Tensor>();
        grad_b_tensor->data = float_array_elementwise_multiply(output_grad->data, a_tensor->data);

        return { grad_a_tensor, grad_b_tensor };
        };
    return result_tensor;
}

/**
 * @brief Raises a Tensor to a scalar power and creates the backward pass.
 */
BasicValue tensor_power(NeReLaBasic& vm, const BasicValue& base, const BasicValue& exponent) {
    const auto& base_tensor = std::get<std::shared_ptr<Tensor>>(base);
    const double exp_val = to_double(exponent);

    auto result_tensor = std::make_shared<Tensor>();
    auto result_data = std::make_shared<FloatArray>();
    result_data->shape = base_tensor->data->shape;
    result_data->data.reserve(base_tensor->data->size());
    for (const auto& val : base_tensor->data->data) {
        result_data->data.push_back(std::pow(val, exp_val));
    }
    result_tensor->data = result_data;
    result_tensor->parents = { base_tensor };

    result_tensor->backward_fn = [base_tensor, exp_val](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto u_pow_n_minus_1 = std::make_shared<FloatArray>();
        u_pow_n_minus_1->shape = base_tensor->data->shape;
        u_pow_n_minus_1->data.reserve(base_tensor->data->size());
        for (const auto& val : base_tensor->data->data) {
            u_pow_n_minus_1->data.push_back(std::pow(val, exp_val - 1.0));
        }

        auto n_times_u = float_array_scalar_multiply(exp_val, u_pow_n_minus_1);
        auto final_grad_data = float_array_elementwise_multiply(output_grad->data, n_times_u);

        auto final_grad_tensor = std::make_shared<Tensor>();
        final_grad_tensor->data = final_grad_data;
        return { final_grad_tensor };
        };

    return result_tensor;
}

/**
 * @brief Divides a Tensor by a scalar and creates the backward pass.
 */
BasicValue tensor_scalar_divide(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b) {
    const auto& tensor_a = std::get<std::shared_ptr<Tensor>>(a);
    const double scalar_b = to_double(b);

    if (scalar_b == 0.0) {
        Error::set(2, vm.runtime_current_line, "Division by zero.");
        return {};
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = float_array_scalar_multiply(1.0 / scalar_b, tensor_a->data);
    result_tensor->parents = { tensor_a };

    result_tensor->backward_fn = [scalar_b](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto final_grad = std::make_shared<Tensor>();
        final_grad->data = float_array_scalar_multiply(1.0 / scalar_b, output_grad->data);
        return { final_grad };
        };
    return result_tensor;
}


BasicValue builtin_matmul(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }

    if (std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        const auto& a_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);
        const auto& b_tensor = std::get<std::shared_ptr<Tensor>>(args[1]);

        auto result_ptr = float_array_matmul(a_tensor->data, b_tensor->data);
        if (!result_ptr) {
            Error::set(15, vm.runtime_current_line, "Inner dimensions must match for MATMUL."); return {};
        }

        auto result_tensor = std::make_shared<Tensor>();
        result_tensor->data = result_ptr;
        result_tensor->parents = { a_tensor, b_tensor };

        result_tensor->backward_fn = [a_tensor, b_tensor](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
            // grad(A) = grad_C @ B.T
            auto b_transposed = transpose_float_array(b_tensor->data);
            auto grad_a_data = float_array_matmul(output_grad->data, b_transposed);

            // grad(B) = A.T @ grad_C
            auto a_transposed = transpose_float_array(a_tensor->data);
            auto grad_b_data = float_array_matmul(a_transposed, output_grad->data);

            auto grad_a_tensor = std::make_shared<Tensor>();
            grad_a_tensor->data = grad_a_data;
            auto grad_b_tensor = std::make_shared<Tensor>();
            grad_b_tensor->data = grad_b_data;

            return std::vector<std::shared_ptr<Tensor>>{ grad_a_tensor, grad_b_tensor };
            };

        return result_tensor;
    }
    else if (std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        // --- This block handles simple Arrays without gradients ---

        const auto& a_ptr = std::get<std::shared_ptr<Array>>(args[0]);
        const auto& b_ptr = std::get<std::shared_ptr<Array>>(args[1]);
        if (!a_ptr || !b_ptr || a_ptr->shape.size() != 2 || b_ptr->shape.size() != 2) {
            Error::set(15, vm.runtime_current_line, "MATMUL arguments must be 2D matrices."); return {};
        }
        size_t rA = a_ptr->shape[0], cA = a_ptr->shape[1], rB = b_ptr->shape[0], cB = b_ptr->shape[1];
        if (cA != rB) { Error::set(15, vm.runtime_current_line, "Inner dimensions must match."); return {}; }
        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = { rA, cB };
        result_ptr->data.resize(rA * cB, 0.0);
        for (size_t r = 0; r < rA; ++r) for (size_t c = 0; c < cB; ++c) for (size_t i = 0; i < cA; ++i)
            result_ptr->data[r * cB + c] = to_double(result_ptr->data[r * cB + c]) + to_double(a_ptr->data[r * cA + i]) * to_double(b_ptr->data[i * cB + c]);
        return result_ptr;

    }
    else {
        Error::set(15, vm.runtime_current_line, "Argument to MATMULT must be an Array ot Tensor."); return {};
    }
    return {};
}

// --- CNN and Utility Functions ---

namespace {
    // Helper to rotate a 4D kernel by 180 degrees, needed for convolution backprop.
    std::shared_ptr<Array> rotate180(const std::shared_ptr<Array>& kernel) {
        if (!kernel || kernel->shape.size() != 4) return nullptr;
        auto rotated_kernel = std::make_shared<Array>(*kernel); // Make a copy
        size_t h = kernel->shape[2];
        size_t w = kernel->shape[3];
        size_t plane_size = h * w;

        for (size_t i = 0; i < kernel->data.size() / plane_size; ++i) {
            size_t plane_start = i * plane_size;
            std::reverse(rotated_kernel->data.begin() + plane_start, rotated_kernel->data.begin() + plane_start + plane_size);
        }
        return rotated_kernel;
    }
} // end anonymous namespace

BasicValue builtin_conv2d(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 5) { Error::set(8, vm.runtime_current_line, "CONV2D requires 5 arguments."); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0]) ||
        !std::holds_alternative<std::shared_ptr<Tensor>>(args[1]) ||
        !std::holds_alternative<std::shared_ptr<Tensor>>(args[2])) {
        Error::set(15, vm.runtime_current_line, "Arguments 1-3 to TENSOR.CONV2D must be a Tensor."); return {};
    }
    const auto& input_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);
    const auto& kernel_tensor = std::get<std::shared_ptr<Tensor>>(args[1]);
    const auto& bias_tensor = std::get<std::shared_ptr<Tensor>>(args[2]);
    int stride = static_cast<int>(to_double(args[3]));
    int padding = static_cast<int>(to_double(args[4]));

    size_t in_channels = input_tensor->data->shape[1];
    size_t in_height = input_tensor->data->shape[2];
    size_t in_width = input_tensor->data->shape[3];
    size_t out_channels = kernel_tensor->data->shape[0];
    size_t kernel_h = kernel_tensor->data->shape[2];
    size_t kernel_w = kernel_tensor->data->shape[3];
    size_t out_height = static_cast<size_t>(std::floor((in_height + 2 * padding - kernel_h) / stride)) + 1;
    size_t out_width = static_cast<size_t>(std::floor((in_width + 2 * padding - kernel_w) / stride)) + 1;

    auto result_array = std::make_shared<FloatArray>();
    result_array->shape = { 1, out_channels, out_height, out_width };
    result_array->data.assign(out_channels * out_height * out_width, 0.0);

    for (size_t oc = 0; oc < out_channels; ++oc) {
        for (size_t y = 0; y < out_height; ++y) {
            for (size_t x = 0; x < out_width; ++x) {
                double sum = 0.0;
                for (size_t ic = 0; ic < in_channels; ++ic) {
                    for (size_t ky = 0; ky < kernel_h; ++ky) {
                        for (size_t kx = 0; kx < kernel_w; ++kx) {
                            int input_y = y * stride + ky - padding;
                            int input_x = x * stride + kx - padding;
                            if (input_y >= 0 && input_y < in_height && input_x >= 0 && input_x < in_width) {
                                double input_val = input_tensor->data->data[ic * (in_height * in_width) + input_y * in_width + input_x];
                                double kernel_val = kernel_tensor->data->data[oc * (in_channels * kernel_h * kernel_w) + ic * (kernel_h * kernel_w) + ky * kernel_w + kx];
                                sum += input_val * kernel_val;
                            }
                        }
                    }
                }
                sum += bias_tensor->data->data[oc];
                result_array->data[oc * (out_height * out_width) + y * out_width + x] = sum;
            }
        }
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_array;
    result_tensor->parents = { input_tensor, kernel_tensor, bias_tensor };

    result_tensor->backward_fn = [=](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto d_input_arr = std::make_shared<FloatArray>();
        d_input_arr->shape = input_tensor->data->shape;
        d_input_arr->data.assign(input_tensor->data->size(), 0.0);

        auto d_kernel_arr = std::make_shared<FloatArray>();
        d_kernel_arr->shape = kernel_tensor->data->shape;
        d_kernel_arr->data.assign(kernel_tensor->data->size(), 0.0);

        auto d_bias_arr = std::make_shared<FloatArray>();
        d_bias_arr->shape = bias_tensor->data->shape;
        d_bias_arr->data.assign(bias_tensor->data->size(), 0.0);

        auto rotated_kernel = rotate180(kernel_tensor->data);

        for (size_t oc = 0; oc < out_channels; ++oc) {
            for (size_t y = 0; y < out_height; ++y) {
                for (size_t x = 0; x < out_width; ++x) {
                    double grad_out_val = output_grad->data->data[oc * (out_height * out_width) + y * out_width + x];
                    d_bias_arr->data[oc] += grad_out_val;

                    for (size_t ic = 0; ic < in_channels; ++ic) {
                        for (size_t ky = 0; ky < kernel_h; ++ky) {
                            for (size_t kx = 0; kx < kernel_w; ++kx) {
                                int input_y = y * stride + ky - padding;
                                int input_x = x * stride + kx - padding;

                                if (input_y >= 0 && input_y < in_height && input_x >= 0 && input_x < in_width) {
                                    // Gradient for the kernel
                                    d_kernel_arr->data[oc * (in_channels * kernel_h * kernel_w) + ic * (kernel_h * kernel_w) + ky * kernel_w + kx] += input_tensor->data->data[ic * (in_height * in_width) + input_y * in_width + input_x] * grad_out_val;

                                    // Gradient for the input
                                    d_input_arr->data[ic * (in_height * in_width) + input_y * in_width + input_x] += rotated_kernel->data[oc * (in_channels * kernel_h * kernel_w) + ic * (kernel_h * kernel_w) + ky * kernel_w + kx] * grad_out_val;
                                }
                            }
                        }
                    }
                }
            }
        }

        auto d_input = std::make_shared<Tensor>(); d_input->data = d_input_arr;
        auto d_kernel = std::make_shared<Tensor>(); d_kernel->data = d_kernel_arr;
        auto d_bias = std::make_shared<Tensor>(); d_bias->data = d_bias_arr;
        return { d_input, d_kernel, d_bias };
        };
    return result_tensor;
}

BasicValue builtin_maxpool2d(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line, "MAXPOOL2D requires 3 arguments."); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument 1 to TENSOR.MAXPOOL2 must be a Tensor."); return {};
    }
    const auto& input = std::get<std::shared_ptr<Tensor>>(args[0]);
    int pool_size = static_cast<int>(to_double(args[1]));
    int stride = static_cast<int>(to_double(args[2]));

    size_t channels = input->data->shape[1];
    size_t in_height = input->data->shape[2];
    size_t in_width = input->data->shape[3];
    size_t out_height = static_cast<size_t>(std::floor((in_height - pool_size) / stride)) + 1;
    size_t out_width = static_cast<size_t>(std::floor((in_width - pool_size) / stride)) + 1;

    auto result_array = std::make_shared<FloatArray>();
    result_array->shape = { 1, channels, out_height, out_width };
    result_array->data.resize(channels * out_height * out_width);

    auto indices_array = std::make_shared<FloatArray>();
    indices_array->shape = result_array->shape;
    indices_array->data.resize(result_array->size());

    for (size_t c = 0; c < channels; ++c) {
        for (size_t y = 0; y < out_height; ++y) {
            for (size_t x = 0; x < out_width; ++x) {
                double max_val = -std::numeric_limits<double>::infinity();
                size_t max_index = 0;
                for (int py = 0; py < pool_size; ++py) {
                    for (int px = 0; px < pool_size; ++px) {
                        int input_y = y * stride + py;
                        int input_x = x * stride + px;
                        size_t current_index = c * (in_height * in_width) + input_y * in_width + input_x;
                        double current_val = input->data->data[current_index];
                        if (current_val > max_val) {
                            max_val = current_val;
                            max_index = current_index;
                        }
                    }
                }
                result_array->data[c * (out_height * out_width) + y * out_width + x] = max_val;
                indices_array->data[c * (out_height * out_width) + y * out_width + x] = static_cast<double>(max_index);
            }
        }
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_array;
    result_tensor->parents = { input };

    result_tensor->backward_fn = [input_shape = input->data->shape, indices_array](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto input_grad_array = std::make_shared<FloatArray>();
        input_grad_array->shape = input_shape;
        input_grad_array->data.assign(input_grad_array->size(), 0.0);
        for (size_t i = 0; i < output_grad->data->data.size(); ++i) {
            size_t max_idx = static_cast<size_t>(output_grad->data->data[i]);
            double current_grad = input_grad_array->data[max_idx];
            double new_grad = output_grad->data->data[i];
            input_grad_array->data[max_idx] = current_grad + new_grad;
        }
        auto final_grad = std::make_shared<Tensor>();
        final_grad->data = input_grad_array;
        return { final_grad };
        };
    return result_tensor;
}


BasicValue builtin_layer_norm(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "LAYER_NORM requires 3 arguments: input_tensor, gain_tensor, bias_tensor.");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0]) || 
        !std::holds_alternative<std::shared_ptr<Tensor>>(args[1]) ||
        !std::holds_alternative<std::shared_ptr<Tensor>>(args[2])) {
        Error::set(15, vm.runtime_current_line, "Arguments to TENSOR.LAYER_NORM must be a Tensor."); return {};
    }
    const auto& x = std::get<std::shared_ptr<Tensor>>(args[0]);
    const auto& gain = std::get<std::shared_ptr<Tensor>>(args[1]);
    const auto& bias = std::get<std::shared_ptr<Tensor>>(args[2]);
    const double epsilon = 1e-5;

    if (x->data->shape.size() != 2) {
        Error::set(15, vm.runtime_current_line, "LAYER_NORM currently supports 2D tensors only."); return {};
    }

    size_t rows = x->data->shape[0];
    size_t cols = x->data->shape[1];

    auto native_result_data = std::make_shared<FloatArray>();
    native_result_data->shape = x->data->shape;
    native_result_data->data.resize(x->data->size());

    auto var_array = std::make_shared<FloatArray>();
    auto x_norm_array = std::make_shared<FloatArray>();
    var_array->shape = { rows, 1 };
    var_array->data.reserve(rows);
    x_norm_array->shape = x->data->shape;
    x_norm_array->data.resize(x->data->size());

    for (size_t r = 0; r < rows; ++r) {
        size_t row_start = r * cols;
        double sum = 0.0;
        for (size_t c = 0; c < cols; ++c) sum += x->data->data[row_start + c];
        double mean = sum / cols;

        double var_sum = 0.0;
        for (size_t c = 0; c < cols; ++c) var_sum += std::pow(x->data->data[row_start + c] - mean, 2);
        double variance = var_sum / cols;
        var_array->data.push_back(variance);

        double inv_std = 1.0 / std::sqrt(variance + epsilon);

        for (size_t c = 0; c < cols; ++c) {
            double x_norm_val = (x->data->data[row_start + c] - mean) * inv_std;
            x_norm_array->data[row_start + c] = x_norm_val;
            native_result_data->data[row_start + c] = gain->data->data[c] * x_norm_val + bias->data->data[c];
        }
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = native_result_data;
    result_tensor->parents = { x, gain, bias };

    result_tensor->backward_fn = [=](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto dx = std::make_shared<FloatArray>();
        auto dgain = std::make_shared<FloatArray>();
        auto dbias = std::make_shared<FloatArray>();
        dx->shape = x->data->shape;
        dgain->shape = gain->data->shape;
        dbias->shape = bias->data->shape;
        dx->data.assign(x->data->size(), 0.0);
        dgain->data.assign(gain->data->size(), 0.0);
        dbias->data.assign(bias->data->size(), 0.0);

        for (size_t r = 0; r < rows; ++r) {
            size_t row_start = r * cols;
            double inv_std = 1.0 / std::sqrt(var_array->data[r] + epsilon);

            double d_x_norm_mean = 0.0;
            double d_x_norm_x_norm_mean = 0.0;

            for (size_t c = 0; c < cols; ++c) {
                double dout = output_grad->data->data[row_start + c];
                double x_norm_val = x_norm_array->data[row_start + c];

                dbias->data[c] += dout;
                dgain->data[c] += dout * x_norm_val;

                double d_x_norm_i = dout * gain->data->data[c];
                d_x_norm_mean += d_x_norm_i;
                d_x_norm_x_norm_mean += d_x_norm_i * x_norm_val;
            }
            d_x_norm_mean /= cols;
            d_x_norm_x_norm_mean /= cols;

            for (size_t c = 0; c < cols; ++c) {
                double d_x_norm_i = output_grad->data->data[row_start + c] * gain->data->data[c];
                double x_norm_i = x_norm_array->data[row_start + c];
                dx->data[row_start + c] += ((d_x_norm_i - d_x_norm_mean) - x_norm_i * d_x_norm_x_norm_mean) * inv_std;
            }
        }
        auto dx_tensor = std::make_shared<Tensor>(); dx_tensor->data = dx;
        auto dgain_tensor = std::make_shared<Tensor>(); dgain_tensor->data = dgain;
        auto dbias_tensor = std::make_shared<Tensor>(); dbias_tensor->data = dbias;
        return { dx_tensor, dgain_tensor, dbias_tensor };
        };

    return result_tensor;
}

BasicValue builtin_sigmoid(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to TENSOR.SIGMOID must be a Tensor."); return {};
    }
    
    const auto& input_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);

    auto result_array = std::make_shared<FloatArray>();
    result_array->shape = input_tensor->data->shape;
    result_array->data.reserve(input_tensor->data->size());
    for (const auto& val : input_tensor->data->data) {
        result_array->data.push_back(1.0 / (1.0 + std::exp(-val)));
    }
    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_array;
    result_tensor->parents = { input_tensor };

    std::weak_ptr<Tensor> result_tensor_weak(result_tensor);
    result_tensor->backward_fn = [result_tensor_weak](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto result_tensor = result_tensor_weak.lock();
        if (!result_tensor) return { nullptr };

        auto ones_array = std::make_shared<FloatArray>();
        ones_array->shape = result_tensor->data->shape;
        ones_array->data.assign(result_tensor->data->size(), 1.0);
        auto one_minus_sigmoid = float_array_subtract(ones_array, result_tensor->data);
        auto derivative = float_array_elementwise_multiply(result_tensor->data, one_minus_sigmoid);
        auto final_grad_data = float_array_elementwise_multiply(output_grad->data, derivative);
        auto final_grad_tensor = std::make_shared<Tensor>();
        final_grad_tensor->data = final_grad_data;
        return { final_grad_tensor };
        };
    return result_tensor;
}

BasicValue tensor_sum(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty()) { Error::set(8, vm.runtime_current_line); return {}; }
    const auto& input_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);

    double total = 0.0;
    for (const auto& val : input_tensor->data->data) {
        total += val;
    }
    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = std::make_shared<FloatArray>();
    result_tensor->data->shape = { 1 };
    result_tensor->data->data = { total };
    result_tensor->parents = { input_tensor };

    result_tensor->backward_fn = [input_tensor](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        double grad_val = output_grad->data->data[0];
        auto grad_array = std::make_shared<FloatArray>();
        grad_array->shape = input_tensor->data->shape;
        grad_array->data.assign(input_tensor->data->size(), grad_val);
        auto final_grad_tensor = std::make_shared<Tensor>();
        final_grad_tensor->data = grad_array;
        return { final_grad_tensor };
        };
    return result_tensor;
}

// --- DISPATCHER FUNCTION for SUM ---
// This will be the public-facing function registered with the interpreter.
BasicValue builtin_sum(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.empty() || args.size() > 2) {
        Error::set(8, vm.runtime_current_line, "SUM requires 1 or 2 arguments.");
        return {};
    }

    // --- Type Dispatching ---
    if (std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        if (args.size() > 1) {
            Error::set(1, vm.runtime_current_line, "Dimensional reduction is not yet supported for Tensors in SUM.");
            return {};
        }
        return tensor_sum(vm, args);
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        // Your original array-based SUM logic (which handles dimensional reduction) can go here.
        // For now, we'll just call the simple scalar sum version.
        return array_sum(vm, args);
    }

    Error::set(15, vm.runtime_current_line, "Argument to SUM must be an Array or a Tensor.");
    return {};
}

// --- JSON Serialization Helpers ---
namespace {
    // Forward declaration for recursive calls
    nlohmann::json tensor_basic_value_to_json(const BasicValue& val);
    BasicValue tensor_json_to_basic_value(const nlohmann::json& j);

    nlohmann::json tensor_basic_value_to_json(const BasicValue& val) {
        return std::visit([](auto&& arg) -> nlohmann::json {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::shared_ptr<Map>>) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& pair : arg->data) {
                    obj[pair.first] = tensor_basic_value_to_json(pair.second);
                }
                return obj;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& item : arg->data) {
                    arr.push_back(tensor_basic_value_to_json(item));
                }
                return arr;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<Tensor>>) {
                if (!arg || !arg->data) return nlohmann::json(); // null
                nlohmann::json tensor_obj;
                tensor_obj["__type__"] = "tensor";
                tensor_obj["shape"] = arg->data->shape;
                tensor_obj["data"] = arg->data->data;
                return tensor_obj;
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
            else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> || std::is_same_v<T, int> || std::is_same_v<T, std::string>) {
                return nlohmann::json(arg);
            }
            else {
                // For primitive types that nlohmann::json can handle directly
                return nlohmann::json(nullptr);
            }
            }, val);
    }

    BasicValue tensor_json_to_basic_value(const nlohmann::json& j) {
        if (j.is_null()) return std::make_shared<Map>(); // Or handle as an error
        if (j.is_boolean()) return j.get<bool>();
        if (j.is_number()) return j.get<double>();
        if (j.is_string()) return j.get<std::string>();
        if (j.is_array()) {
            auto arr = std::make_shared<Array>();
            for (const auto& item : j) {
                arr->data.push_back(tensor_json_to_basic_value(item));
            }
            // *** THE BUG IS HERE: The shape of the array is not being set. ***
            // We need to set the shape for the deserialized array.
            arr->shape = { arr->data.size() };
            return arr;
        }
        if (j.is_object()) {
            if (j.contains("__type__") && j["__type__"] == "tensor") {
                auto tensor = std::make_shared<Tensor>();
                tensor->data = std::make_shared<FloatArray>();
                tensor->data->shape = j["shape"].get<std::vector<size_t>>();
                tensor->data->data = j["data"].get<std::vector<double>>();
                return tensor;
            }
            else {
                auto map = std::make_shared<Map>();
                for (auto& [key, value] : j.items()) {
                    map->data[key] = tensor_json_to_basic_value(value);
                }
                return map;
            }
        }
        return {}; // Should not happen
    }
}

BasicValue builtin_save_model(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return false; }
    const auto& model_map_ptr = std::get<std::shared_ptr<Map>>(args[0]);
    const std::string filename = std::get<std::string>(args[1]);

    nlohmann::json j_model = tensor_basic_value_to_json(model_map_ptr);

    std::ofstream outfile(filename);
    if (!outfile) { Error::set(12, vm.runtime_current_line); return false; }
    outfile << j_model.dump(4);
    return true;
}

BasicValue builtin_load_model(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return {}; }
    const std::string filename = std::get<std::string>(args[0]);
    std::ifstream infile(filename);
    if (!infile) { Error::set(6, vm.runtime_current_line); return {}; }

    nlohmann::json j_model;
    try {
        infile >> j_model;
    }
    catch (const nlohmann::json::parse_error& e) {
        Error::set(1, vm.runtime_current_line, "Invalid JSON in model file.");
        return {};
    }
    return tensor_json_to_basic_value(j_model);
}
// --- Factory and Utility Functions ---
// --- Bridge and Factory Functions ---

BasicValue builtin_to_tensor(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to TENSOR.FROM must be an Array."); return {};
    }
    const auto& generic_array_ptr = std::get<std::shared_ptr<Array>>(args[0]);

    auto float_array_ptr = std::make_shared<FloatArray>();
    float_array_ptr->shape = generic_array_ptr->shape;
    float_array_ptr->data.reserve(generic_array_ptr->size());
    for (const auto& val : generic_array_ptr->data) {
        float_array_ptr->data.push_back(to_double(val));
    }

    auto tensor_ptr = std::make_shared<Tensor>();
    tensor_ptr->data = float_array_ptr;
    return tensor_ptr;
}

BasicValue builtin_toarray(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to TENSOR.TOARRAY must be a Tensor."); return {};
    }
    const auto& tensor_ptr = std::get<std::shared_ptr<Tensor>>(args[0]);

    auto generic_array_ptr = std::make_shared<Array>();
    generic_array_ptr->shape = tensor_ptr->data->shape;
    generic_array_ptr->data.reserve(tensor_ptr->data->size());
    for (double val : tensor_ptr->data->data) {
        generic_array_ptr->data.push_back(val);
    }
    return generic_array_ptr;
}

BasicValue builtin_create_layer(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::string>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument 1 to TENSOR.CREATE_LAYER must be a String."); return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Argument 1 to TENSOR.CREATE_LAYER must be a Map."); return {};
    }
    std::string layer_type = to_upper(std::get<std::string>(args[0]));
    const auto& options_map_ptr = std::get<std::shared_ptr<Map>>(args[1]);
    auto layer_result_ptr = std::make_shared<Map>();
    layer_result_ptr->data["type"] = layer_type;
    auto& options = options_map_ptr->data;

    try {
        if (layer_type == "DENSE") {
            size_t input_size = static_cast<size_t>(to_double(options.at("input_size")));
            size_t units = static_cast<size_t>(to_double(options.at("units")));
            auto weights_tensor = std::make_shared<Tensor>();
            weights_tensor->data = create_randomized_float_array({ input_size, units }, input_size, units);
            auto bias_tensor = std::make_shared<Tensor>();
            bias_tensor->data = std::make_shared<FloatArray>();
            bias_tensor->data->shape = { 1, units };
            bias_tensor->data->data.assign(units, 0.0);
            layer_result_ptr->data["weights"] = weights_tensor;
            layer_result_ptr->data["bias"] = bias_tensor;
        }
        else if (layer_type == "EMBEDDING") {
            size_t vocab_size = static_cast<size_t>(to_double(options.at("vocab_size")));
            size_t embedding_dim = static_cast<size_t>(to_double(options.at("embedding_dim")));
            auto weights_tensor = std::make_shared<Tensor>();
            weights_tensor->data = create_randomized_float_array({ vocab_size, embedding_dim }, vocab_size, embedding_dim);
            layer_result_ptr->data["weights"] = weights_tensor;
        }
        else if (layer_type == "LAYER_NORM") {
            size_t dim = static_cast<size_t>(to_double(options.at("dim")));
            auto gain_tensor = std::make_shared<Tensor>();
            gain_tensor->data = std::make_shared<FloatArray>();
            gain_tensor->data->shape = { 1, dim };
            gain_tensor->data->data.assign(dim, 1.0);
            auto bias_tensor = std::make_shared<Tensor>();
            bias_tensor->data = std::make_shared<FloatArray>();
            bias_tensor->data->shape = { 1, dim };
            bias_tensor->data->data.assign(dim, 0.0);
            layer_result_ptr->data["gain"] = gain_tensor;
            layer_result_ptr->data["bias"] = bias_tensor;
        }
        else if (layer_type == "ATTENTION") {
            size_t embedding_dim = static_cast<size_t>(to_double(options.at("embedding_dim")));
            auto w_q = std::make_shared<Tensor>();
            w_q->data = create_randomized_float_array({ embedding_dim, embedding_dim }, embedding_dim, embedding_dim);
            auto w_k = std::make_shared<Tensor>();
            w_k->data = create_randomized_float_array({ embedding_dim, embedding_dim }, embedding_dim, embedding_dim);
            auto w_v = std::make_shared<Tensor>();
            w_v->data = create_randomized_float_array({ embedding_dim, embedding_dim }, embedding_dim, embedding_dim);
            layer_result_ptr->data["Wq"] = w_q;
            layer_result_ptr->data["Wk"] = w_k;
            layer_result_ptr->data["Wv"] = w_v;
        }
    }
    catch (const std::out_of_range& e) {
        Error::set(1, vm.runtime_current_line, "Missing required option for layer " + layer_type);
        return {};
    }
    return layer_result_ptr;
}

// --- MODIFIED: This function now creates different types of optimizers ---
BasicValue builtin_create_optimizer(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "CREATE_OPTIMIZER requires 2 arguments: type$, options_map");
        return {};
    }
    if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::shared_ptr<Map>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Invalid arguments for CREATE_OPTIMIZER.");
        return {};
    }

    std::string optimizer_type = to_upper(std::get<std::string>(args[0]));
    const auto& options_map_ptr = std::get<std::shared_ptr<Map>>(args[1]);
    if (!options_map_ptr) {
        Error::set(3, vm.runtime_current_line, "Optimizer options map cannot be null.");
        return {};
    }

    auto optimizer_result_ptr = std::make_shared<Map>();
    optimizer_result_ptr->data["type"] = optimizer_type;

    try {
        if (optimizer_type == "SGD") {
            optimizer_result_ptr->data["learning_rate"] = options_map_ptr->data.at("learning_rate");
        }
        else if (optimizer_type == "ADAM") {
            optimizer_result_ptr->data["learning_rate"] = options_map_ptr->data.at("learning_rate");
            // Set default values for Adam if not provided
            optimizer_result_ptr->data["beta1"] = options_map_ptr->data.count("beta1") ? options_map_ptr->data.at("beta1") : 0.9;
            optimizer_result_ptr->data["beta2"] = options_map_ptr->data.count("beta2") ? options_map_ptr->data.at("beta2") : 0.999;
            optimizer_result_ptr->data["epsilon"] = options_map_ptr->data.count("epsilon") ? options_map_ptr->data.at("epsilon") : 1e-8;
            optimizer_result_ptr->data["t"] = 0.0; // Timestep starts at 0
        }
        else {
            Error::set(1, vm.runtime_current_line, "Unknown optimizer type: " + optimizer_type);
            return {};
        }
    }
    catch (const std::out_of_range& e) {
        Error::set(1, vm.runtime_current_line, "Missing required option for " + optimizer_type + " optimizer.");
        return {};
    }
    return optimizer_result_ptr;
}

// --- MODIFIED: This function now acts as a dispatcher ---
BasicValue builtin_optimizer_update(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }

    const auto& model_map = std::get<std::shared_ptr<Map>>(args[0]);
    const auto& optimizer_map = std::get<std::shared_ptr<Map>>(args[1]);

    std::string optimizer_type = to_string(optimizer_map->data.at("type"));
    std::string update_func_name = optimizer_type + ".UPDATE";

    // Find the correct update function (e.g., "ADAM.UPDATE") in the function table
    if (vm.main_function_table.count(update_func_name)) {
        const auto& func_info = vm.main_function_table.at(update_func_name);
        if (func_info.native_dll_impl != nullptr) {
            BasicValue result;
            // Call it using the new "output pointer" style
            func_info.native_dll_impl(vm, args, &result);
            return result;
        }
        // Priority 2: Check for the old internal function pointer
        else if (func_info.native_impl != nullptr) {
            // Call it using the old "return by value" style
            return func_info.native_impl(vm, args);
        }
    }

    Error::set(22, vm.runtime_current_line, "Optimizer update function not found for type: " + optimizer_type);
    return {};
}

// --- Autodiff and Training Functions ---

BasicValue builtin_backward(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0]) ) {
        Error::set(15, vm.runtime_current_line, "Argument to TENSOR.BACKWARD must be Tensor.");
        return {};
    }
    auto loss_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);
    if (!loss_tensor || !loss_tensor->data) { Error::set(3, vm.runtime_current_line); return false; }

    std::vector<std::shared_ptr<Tensor>> sorted_graph;
    std::unordered_set<const Tensor*> visited;
    std::function<void(std::shared_ptr<Tensor>)> visit =
        [&](std::shared_ptr<Tensor> node) {
        if (!node || visited.count(node.get())) return;
        visited.insert(node.get());
        for (auto& parent : node->parents) {
            visit(parent);
        }
        sorted_graph.push_back(node);
        };
    visit(loss_tensor);

    loss_tensor->grad = std::make_shared<Tensor>();
    loss_tensor->grad->data = std::make_shared<FloatArray>();
    loss_tensor->grad->data->shape = loss_tensor->data->shape;
    loss_tensor->grad->data->data.assign(loss_tensor->data->size(), 1.0);

    for (auto it = sorted_graph.rbegin(); it != sorted_graph.rend(); ++it) {
        auto& node = *it;
        if (node->backward_fn && node->grad) {
            auto parent_grads = node->backward_fn(node->grad);

            for (size_t i = 0; i < node->parents.size(); ++i) {
                auto& parent = node->parents[i];
                if (i < parent_grads.size() && parent_grads[i] && parent_grads[i]->data) {
                    if (parent->grad && parent->grad->data) {
                        parent->grad->data = float_array_add(parent->grad->data, parent_grads[i]->data);
                    }
                    else {
                        parent->grad = parent_grads[i];
                    }
                }
            }
        }

        if (!node->parents.empty()) {
            node->grad = nullptr;
        }
    }
    return false;
}


BasicValue builtin_update(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Map>>(args[0]) || !std::holds_alternative<std::shared_ptr<Map>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Arguments to TENSOR.UPDATE must be MAPS.");
        return {};
    }
    const auto& model_map = std::get<std::shared_ptr<Map>>(args[0]);
    const auto& optimizer_map = std::get<std::shared_ptr<Map>>(args[1]);
    double lr = to_double(optimizer_map->data.at("learning_rate"));

    std::function<void(std::shared_ptr<Map>)> process_layer =
        [&](std::shared_ptr<Map> layer_map) {
        for (auto const& [param_name, param_val] : layer_map->data) {
            if (std::holds_alternative<std::shared_ptr<Tensor>>(param_val)) {
                const auto& param_tensor = std::get<std::shared_ptr<Tensor>>(param_val);
                if (param_tensor && param_tensor->grad && param_tensor->grad->data) {
                    auto delta = float_array_scalar_multiply(lr, param_tensor->grad->data);
                    param_tensor->data = float_array_subtract(param_tensor->data, delta);
                    param_tensor->grad = nullptr;
                }
            }
        }
        };

    for (const auto& model_pair : model_map->data) {
        const auto& value = model_pair.second;
        if (std::holds_alternative<std::shared_ptr<Map>>(value)) {
            process_layer(std::get<std::shared_ptr<Map>>(value));
        }
        else if (std::holds_alternative<std::shared_ptr<Array>>(value)) {
            const auto& layers_array = std::get<std::shared_ptr<Array>>(value);
            for (const auto& layer_val : layers_array->data) {
                if (std::holds_alternative<std::shared_ptr<Map>>(layer_val)) {
                    process_layer(std::get<std::shared_ptr<Map>>(layer_val));
                }
            }
        }
    }
    return model_map;
}

// ... other functions like SAVE/LOAD MODEL ...

// --- LLM Specific Functions ---

BasicValue builtin_tensor_tokenize(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    const auto& text = std::get<std::string>(args[0]);
    const auto& vocab_map_val = args[1];
    if (!std::holds_alternative<std::shared_ptr<Map>>(vocab_map_val)) {
        Error::set(15, vm.runtime_current_line, "Second argument to TOKENIZE must be a Map.");
        return {};
    }
    const auto& vocab_map = std::get<std::shared_ptr<Map>>(vocab_map_val);

    auto tokens_array = std::make_shared<Array>();
    tokens_array->shape = { text.length() };
    tokens_array->data.reserve(text.length());

    for (char c : text) {
        std::string s(1, c);
        if (vocab_map->data.count(s)) {
            tokens_array->data.push_back(vocab_map->data.at(s));
        }
        else {
            tokens_array->data.push_back(0.0);
        }
    }
    return tokens_array;
}

BasicValue builtin_tensor_positional_encoding(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    int seq_len = static_cast<int>(to_double(args[0]));
    int d_model = static_cast<int>(to_double(args[1]));

    auto pe_array = std::make_shared<FloatArray>();
    pe_array->shape = { (size_t)seq_len, (size_t)d_model };
    pe_array->data.resize(seq_len * d_model);

    for (int pos = 0; pos < seq_len; ++pos) {
        for (int i = 0; i < d_model; ++i) {
            double div_term = std::pow(10000.0, (2.0 * (i / 2)) / d_model);
            if (i % 2 == 0) {
                pe_array->data[pos * d_model + i] = std::sin(pos / div_term);
            }
            else {
                pe_array->data[pos * d_model + i] = std::cos(pos / div_term);
            }
        }
    }
    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = pe_array;
    return result_tensor;
}

BasicValue builtin_tensor_relu(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(15, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to TENSOR.RELU must be a Tensor."); return {};
    }
    const auto& input_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);

    auto result_data = std::make_shared<FloatArray>();
    result_data->shape = input_tensor->data->shape;
    result_data->data.reserve(input_tensor->data->size());
    for (double val : input_tensor->data->data) {
        result_data->data.push_back(std::max(0.0, val));
    }
    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_data;
    result_tensor->parents = { input_tensor };

    result_tensor->backward_fn = [input_tensor](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto grad_data = std::make_shared<FloatArray>();
        grad_data->shape = input_tensor->data->shape;
        grad_data->data.reserve(input_tensor->data->size());
        for (size_t i = 0; i < input_tensor->data->size(); ++i) {
            double derivative = input_tensor->data->data[i] > 0 ? 1.0 : 0.0;
            grad_data->data.push_back(derivative * output_grad->data->data[i]);
        }
        auto final_grad = std::make_shared<Tensor>();
        final_grad->data = grad_data;
        return { final_grad };
        };
    return result_tensor;
}

BasicValue builtin_tensor_softmax(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to TENSOR.SOFTMAX must be a Tensor."); return {};
    }
    const auto& input_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);
    bool is_causal = false;
    if (args.size() > 1) {
        is_causal = to_bool(args[1]);
    }

    auto result_data = std::make_shared<FloatArray>();
    result_data->shape = input_tensor->data->shape;
    size_t rows = (input_tensor->data->shape.size() > 1) ? input_tensor->data->shape[0] : 1;
    size_t cols = (input_tensor->data->shape.size() > 1) ? input_tensor->data->shape[1] : input_tensor->data->shape[0];
    result_data->data.resize(rows * cols);

    for (size_t i = 0; i < rows; ++i) {
        double max_val = -std::numeric_limits<double>::infinity();
        size_t row_start = i * cols;
        size_t end = is_causal ? i + 1 : cols;

        for (size_t j = 0; j < end; ++j) {
            max_val = std::max(max_val, input_tensor->data->data[row_start + j]);
        }

        double sum = 0.0;
        for (size_t j = 0; j < end; ++j) {
            double exp_val = std::exp(input_tensor->data->data[row_start + j] - max_val);
            result_data->data[row_start + j] = exp_val;
            sum += exp_val;
        }

        if (sum > 0) {
            for (size_t j = 0; j < end; ++j) {
                result_data->data[row_start + j] /= sum;
            }
        }
        // Zero out the masked part
        for (size_t j = end; j < cols; ++j) {
            result_data->data[row_start + j] = 0.0;
        }
    }

    auto result_tensor = std::make_shared<Tensor>();
    result_tensor->data = result_data;
    result_tensor->parents = { input_tensor };

    std::weak_ptr<Tensor> result_tensor_weak(result_tensor);
    result_tensor->backward_fn = [result_tensor_weak, is_causal](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto result_tensor = result_tensor_weak.lock();
        if (!result_tensor) return { nullptr };

        auto y = result_tensor->data;
        auto g = output_grad->data;
        auto grad_data = std::make_shared<FloatArray>();
        grad_data->shape = y->shape;
        grad_data->data.resize(y->size());

        size_t rows = y->shape[0];
        size_t cols = y->shape[1];

        for (size_t r = 0; r < rows; ++r) {
            size_t row_start = r * cols;
            size_t end = is_causal ? r + 1 : cols;
            double dot_product = 0.0;
            for (size_t c = 0; c < end; ++c) {
                dot_product += y->data[row_start + c] * g->data[row_start + c];
            }
            for (size_t c = 0; c < end; ++c) {
                grad_data->data[row_start + c] = y->data[row_start + c] * (g->data[row_start + c] - dot_product);
            }
        }
        auto final_grad = std::make_shared<Tensor>();
        final_grad->data = grad_data;
        return { final_grad };
        };
    return result_tensor;
}


BasicValue builtin_tensor_cross_entropy_loss(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return {}; }
    if (!std::holds_alternative<std::shared_ptr<Tensor>>(args[0]) || !std::holds_alternative<std::shared_ptr<Tensor>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Arguments to TENSOR.CROSS_ENTRY_LOSS must be a Tensor."); return {};
    }
    const auto& logits_tensor = std::get<std::shared_ptr<Tensor>>(args[0]);
    const auto& actual_tensor = std::get<std::shared_ptr<Tensor>>(args[1]);

    if (logits_tensor->data->shape != actual_tensor->data->shape) {
        Error::set(15, vm.runtime_current_line); return {};
    }

    size_t batch_size = logits_tensor->data->shape[0];
    size_t vocab_size = logits_tensor->data->shape[1];

    auto probs_val = builtin_tensor_softmax(vm, { logits_tensor });
    if (Error::get() != 0) { return {}; }
    auto probs_tensor = std::get<std::shared_ptr<Tensor>>(probs_val);

    double total_loss = 0.0;
    for (size_t i = 0; i < batch_size; ++i) {
        size_t row_start = i * vocab_size;
        for (size_t j = 0; j < vocab_size; ++j) {
            if (actual_tensor->data->data[row_start + j] == 1.0) {
                double prob = std::max(1e-9, probs_tensor->data->data[row_start + j]);
                total_loss -= std::log(prob);
                break;
            }
        }
    }

    auto loss_tensor = std::make_shared<Tensor>();
    loss_tensor->data = std::make_shared<FloatArray>();
    loss_tensor->data->shape = { 1 };
    loss_tensor->data->data = { total_loss / batch_size };
    loss_tensor->parents = { logits_tensor, actual_tensor };

    loss_tensor->backward_fn = [=, &vm](std::shared_ptr<Tensor> output_grad) -> std::vector<std::shared_ptr<Tensor>> {
        auto probas_data = std::get<std::shared_ptr<Tensor>>(builtin_tensor_softmax(vm, { logits_tensor }))->data;
        auto grad_data = float_array_subtract(probas_data, actual_tensor->data);

        double incoming_grad_scalar = output_grad->data->data[0];
        auto scaled_grad_data = float_array_scalar_multiply(incoming_grad_scalar / batch_size, grad_data);

        auto grad_tensor = std::make_shared<Tensor>();
        grad_tensor->data = scaled_grad_data;

        return { grad_tensor, nullptr };
        };

    return loss_tensor;
}

// The main registration function for this module
void register_ai_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate) {
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

    // Core Tensor Ops
    register_func("TENSOR.FROM", 1, builtin_to_tensor);
    register_func("TENSOR.TOARRAY", 1, builtin_toarray);
    register_func("TENSOR.MATMUL", 2, builtin_matmul);
    register_func("SUM", -1, builtin_sum);

    // Factories & Model I/O
    register_func("TENSOR.CREATE_LAYER", 2, builtin_create_layer);
    register_func("TENSOR.CREATE_OPTIMIZER", 2, builtin_create_optimizer);
    register_proc("TENSOR.SAVEMODEL", 2, builtin_save_model);
    register_func("TENSOR.LOADMODEL", 1, builtin_load_model);

    // Training
    register_proc("TENSOR.BACKWARD", 1, builtin_backward);
    register_func("TENSOR.UPDATE", 2, builtin_update);

    // Activation & Layer Functions
    register_func("TENSOR.SIGMOID", 1, builtin_sigmoid);
    register_func("TENSOR.RELU", 1, builtin_tensor_relu);
    register_func("TENSOR.SOFTMAX", -1, builtin_tensor_softmax);
    register_func("TENSOR.LAYERNORM", 3, builtin_layer_norm);
    register_func("TENSOR.CONV2D", 5, builtin_conv2d);
    register_func("TENSOR.MAXPOOL2D", 3, builtin_maxpool2d);

    // Loss & LLM helpers
    register_func("TENSOR.CROSS_ENTROPY_LOSS", 2, builtin_tensor_cross_entropy_loss);
    register_func("TENSOR.TOKENIZE", 2, builtin_tensor_tokenize);
    register_func("TENSOR.POSITIONAL_ENCODING", 2, builtin_tensor_positional_encoding);
}