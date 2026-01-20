#include "ArrayFunctions.hpp"     
#include "NeReLaBasic.hpp"      // For the vm object, FunctionInfo, etc.
#include "AIFunctions.hpp"
#include "Commands.hpp"         // For to_double, to_string
#include "Error.hpp"            // For Error::set
#include "Types.hpp"            // For BasicValue, Array
#include "BuiltinFunctions.hpp" // For calling builtin_transpose from builtin_xsort

#include <algorithm>            // For std::sort, std::reverse, std::shuffle
#include <cmath>                // For std::abs, std::pow
#include <map>                  // For GAUSS_RULES map
#include <random>               // For std::random_device in builtin_shuffle
#include <string>               // For std::string
#include <unordered_set>        // For builtin_unique and builtin_diff
#include <vector>               // For std::vector

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

    // 3. Shape Calculation ---
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

// NORMALIZE(array) -> array
// Scales the elements of a numeric array to the range [0.0, 1.0].
BasicValue builtin_normalize(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "NORMALIZE requires 1 array argument.");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to NORMALIZE must be an array.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!source_ptr || source_ptr->data.empty()) {
        return source_ptr; // Return original if null or empty
    }

    // Find min and max values
    double min_val = to_double(source_ptr->data[0]);
    double max_val = to_double(source_ptr->data[0]);
    for (size_t i = 1; i < source_ptr->data.size(); ++i) {
        double current_val = to_double(source_ptr->data[i]);
        if (current_val < min_val) min_val = current_val;
        if (current_val > max_val) max_val = current_val;
    }

    double range = max_val - min_val;

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = source_ptr->shape;
    result_ptr->data.reserve(source_ptr->data.size());

    if (range == 0.0) { // All elements are the same
        result_ptr->data.assign(source_ptr->data.size(), 0.0);
    }
    else {
        for (const auto& val : source_ptr->data) {
            result_ptr->data.push_back((to_double(val) - min_val) / range);
        }
    }
    return result_ptr;
}

// UNIQUE(array) -> array
// Returns a new array containing only the unique elements from the source.
BasicValue builtin_unique(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "UNIQUE requires 1 array argument.");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to UNIQUE must be an array.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!source_ptr) return {};

    auto result_ptr = std::make_shared<Array>();
    std::unordered_set<std::string> seen; // Use string representation for robust uniqueness

    for (const auto& val : source_ptr->data) {
        std::string s = to_string(val);
        if (seen.find(s) == seen.end()) {
            seen.insert(s);
            result_ptr->data.push_back(val);
        }
    }

    result_ptr->shape = { result_ptr->data.size() };
    return result_ptr;
}

// SHUFFLE(array) -> array
// Returns a new array with the elements of the source array randomly shuffled.
BasicValue builtin_shuffle(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line, "SHUFFLE requires 1 array argument.");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "Argument to SHUFFLE must be an array.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!source_ptr) return {};

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = source_ptr->shape;
    result_ptr->data = source_ptr->data; // Make a copy

    // Use a high-quality random number generator for shuffling
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(result_ptr->data.begin(), result_ptr->data.end(), g);

    return result_ptr;
}

// FIND_IN_ARRAY(array, value) -> number
// Finds the first 0-based index of a value in an array. Returns -1 if not found.
BasicValue builtin_find_in_array(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "FIND_IN_ARRAY requires 2 arguments: array, value_to_find");
        return -1.0;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to FIND_IN_ARRAY must be an array.");
        return -1.0;
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    const BasicValue& value_to_find = args[1];

    if (source_ptr) {
        for (size_t i = 0; i < source_ptr->data.size(); ++i) {
            // The overloaded == for BasicValue handles the comparison
            if (source_ptr->data[i] == value_to_find) {
                return static_cast<double>(i);
            }
        }
    }
    return -1.0; // Not found
}

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

    // 3. --- Functionality: Reduce along a Dimension ---
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
                const auto& func_ref = std::get<FunctionRef>(args[0]);
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
                accumulator = vm.execute_function_for_value(func_info, func_args, func_ref.captured_env);
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
        accumulator = vm.execute_function_for_value(func_info, func_args, func_ref.captured_env);

        // If the user's function caused an error, stop and propagate it.
        if (Error::get() != 0) {
            return {};
        }
    }

    // Return the final accumulated value.
    return accumulator;
}

// SELECT(function@, array, [row_wise_bool]) -> array
// Applies a function to each element of an array.
// If the optional third argument 'row_wise_bool' is TRUE, it applies the
// function to each row of a 2D matrix instead. The result of a row-wise
// select is always a 1D array.
BasicValue builtin_select(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 2 && args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "SELECT requires 2 or 3 arguments: function_ref, array, [row_wise_bool]");
        return {};
    }
    if (!std::holds_alternative<FunctionRef>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to SELECT must be a function reference (e.g., MyFunc@).");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Second argument to SELECT must be an array.");
        return {};
    }

    // 2. --- Argument Parsing ---
    const auto& func_ref = std::get<FunctionRef>(args[0]);
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    const std::string func_name = to_upper(func_ref.name);

    // Parse the optional third argument, defaulting to element-wise.
    bool row_wise = false;
    if (args.size() == 3) {
        row_wise = to_bool(args[2]);
    }

    // 3. --- Further Validation ---
    if (!vm.active_function_table->count(func_name)) {
        Error::set(22, vm.runtime_current_line, "Function '" + func_name + "' not found for SELECT.");
        return {};
    }
    const auto& func_info = vm.active_function_table->at(func_name);
    if (func_info.arity != 1) {
        Error::set(26, vm.runtime_current_line, "Function '" + func_name + "' for SELECT must accept exactly one argument.");
        return {};
    }
    if (!source_ptr) {
        return {}; // Return empty if the source array is null
    }

    // 4. --- Mapping Logic ---
    auto result_ptr = std::make_shared<Array>();

    if (row_wise) {
        // --- Row-wise Operation ---
        if (source_ptr->shape.size() != 2) {
            Error::set(15, vm.runtime_current_line, "Row-wise SELECT requires a 2D matrix as the source array.");
            return {};
        }

        size_t rows = source_ptr->shape[0];
        size_t cols = source_ptr->shape[1];
        result_ptr->data.reserve(rows);

        for (size_t r = 0; r < rows; ++r) {
            // Create a temporary 1D array representing the current row.
            auto row_as_array = std::make_shared<Array>();
            row_as_array->shape = { cols };
            auto start_it = source_ptr->data.begin() + (r * cols);
            auto end_it = start_it + cols;
            row_as_array->data.assign(start_it, end_it);

            std::vector<BasicValue> func_args = { row_as_array };
            BasicValue mapped_value = vm.execute_function_for_value(func_info, func_args, func_ref.captured_env);
            if (Error::get() != 0) return {};

            result_ptr->data.push_back(mapped_value);
        }
        result_ptr->shape = { result_ptr->data.size() };
    }
    else {
        // --- Element-wise Operation (Default Behavior) ---
        result_ptr->shape = source_ptr->shape;
        result_ptr->data.reserve(source_ptr->data.size());

        for (const auto& element : source_ptr->data) {
            std::vector<BasicValue> func_args = { element };
            BasicValue mapped_value = vm.execute_function_for_value(func_info, func_args, func_ref.captured_env);
            if (Error::get() != 0) return {};

            result_ptr->data.push_back(mapped_value);
        }
    }

    return result_ptr;
}
// FILTER(function@, array) -> array
// Returns a new 1D array containing only elements for which the predicate function returns TRUE.
BasicValue builtin_filter(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "FILTER requires 2 arguments: function_ref, array");
        return {};
    }
    if (!std::holds_alternative<FunctionRef>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to FILTER must be a function reference (e.g., IsEven@).");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
        Error::set(15, vm.runtime_current_line, "Second argument to FILTER must be an array.");
        return {};
    }

    // 2. --- Argument Parsing ---
    const auto& func_ref = std::get<FunctionRef>(args[0]);
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[1]);
    const std::string func_name = to_upper(func_ref.name);

    // 3. --- Further Validation ---
    if (!vm.active_function_table->count(func_name)) {
        Error::set(22, vm.runtime_current_line, "Function '" + func_name + "' not found for FILTER.");
        return {};
    }
    const auto& func_info = vm.active_function_table->at(func_name);
    // A predicate function must take exactly one argument.
    if (func_info.arity != 1) {
        Error::set(26, vm.runtime_current_line, "Function '" + func_name + "' for FILTER must accept exactly one argument.");
        return {};
    }
    if (!source_ptr) {
        return {};
    }

    // 4. --- Filtering Logic ---
    auto result_ptr = std::make_shared<Array>();
    // The result data will be built up dynamically.

    for (const auto& element : source_ptr->data) {
        std::vector<BasicValue> func_args = { element };

        // Pass the captured environment (backpack) from func_ref to the executor
        BasicValue predicate_result = vm.execute_function_for_value(func_info, func_args, func_ref.captured_env);

        if (Error::get() != 0) {
            return {};
        }

        // Check if the predicate function returned a TRUE value
        if (to_bool(predicate_result)) {
            // If it did, add the *original* element to our results
            result_ptr->data.push_back(element);
        }
    }

    // The result of a filter is always a flat, 1D array.
    result_ptr->shape = { result_ptr->data.size() };

    return result_ptr;
}

// IOTA(N, [B=1], [S=1]) -> vector
// Generates a vector of N numbers starting from B with a step of S.
// B defaults to 1, and S defaults to 1 if not provided.
BasicValue builtin_iota(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Allow 1, 2, or 3 arguments (N, Base, Step)
    if (args.size() < 1 || args.size() > 3) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return {}; // Return an empty BasicValue
    }

    int count = static_cast<int>(to_double(args[0]));
    if (count < 0) count = 0;

    // Set default values for base and step
    double base = 1.0;
    double step = 1.0;

    // Read optional arguments if they are provided
    if (args.size() >= 2) {
        base = to_double(args[1]);
    }
    if (args.size() == 3) {
        step = to_double(args[2]);
    }

    auto new_array_ptr = std::make_shared<Array>();
    new_array_ptr->shape = { (size_t)count };
    new_array_ptr->data.reserve(count);

    // Modified loop: generates 'count' numbers using 'base' and 'step'.
    // The formula is base + i * step
    for (int i = 0; i < count; ++i) {
        new_array_ptr->data.push_back(base + i * step);
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
        const auto& func_ref = std::get<FunctionRef>(op_arg);
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
                BasicValue result = vm.execute_function_for_value(func_info, func_args, func_ref.captured_env);
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

// --- Sorting and Comparison Helpers ---

// A robust less-than comparison for sorting BasicValues.
// Numbers are considered "less than" strings.
bool basic_value_less(const BasicValue& a, const BasicValue& b) {
    // Both are numbers (common case)
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) < std::get<double>(b);
    }
    // Both are strings
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) < std::get<std::string>(b);
    }
    // One is a number, the other is not (treat numbers as smaller)
    if (std::holds_alternative<double>(a) && !std::holds_alternative<double>(b)) {
        return true;
    }
    if (!std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return false;
    }
    // Default case for other types
    return false;
}

// XSORT(array, [sort_key_column], [descending_bool]) -> array
// A high-performance sort.
// - For 1D arrays, it sorts the elements.
// - For 2D arrays, it sorts the rows based on the values in the 'sort_key_column'.
BasicValue builtin_xsort(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. --- Argument Validation ---
    if (args.empty() || args.size() > 3) {
        Error::set(8, vm.runtime_current_line, "XSORT requires 1 to 3 arguments: array, [sort_column_index], [descending_bool]");
        return {};
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        Error::set(15, vm.runtime_current_line, "First argument to XSORT must be an array.");
        return {};
    }
    const auto& source_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!source_ptr || source_ptr->data.empty()) {
        return source_ptr; // Return original array if it's null or empty
    }

    // 2. --- Argument Parsing ---
    int sort_key_column = -1; // -1 indicates sorting a 1D array or no column specified
    bool descending = false;

    if (args.size() > 1) {
        sort_key_column = static_cast<int>(to_double(args[1]));
    }
    if (args.size() > 2) {
        descending = to_bool(args[2]);
    }

    // --- Create a copy of the source array to modify ---
    auto result_ptr = std::make_shared<Array>(*source_ptr);

    // 3. --- Sorting Logic ---

    // --- CASE A: Sort a 1D Array (Backward Compatibility) ---
    if (source_ptr->shape.size() == 1) {
        auto comparator = [&](const BasicValue& a, const BasicValue& b) {
            return descending ? basic_value_less(b, a) : basic_value_less(a, b);
            };
        std::sort(result_ptr->data.begin(), result_ptr->data.end(), comparator);
        return result_ptr;
    }

    // --- CASE B: Sort a 2D Array by a Key Column ---
    if (source_ptr->shape.size() == 2) {
        size_t rows = source_ptr->shape[0];
        size_t cols = source_ptr->shape[1];

        if (sort_key_column < 0 || (size_t)sort_key_column >= cols) {
            Error::set(10, vm.runtime_current_line, "Sort column index is out of bounds for the matrix.");
            return {};
        }

        // Create a temporary vector of row pointers (or iterators) to sort.
        // This avoids copying all the data into a vector-of-vectors.
        std::vector<std::vector<BasicValue>::iterator> row_iterators;
        row_iterators.reserve(rows);
        for (size_t r = 0; r < rows; ++r) {
            row_iterators.push_back(result_ptr->data.begin() + (r * cols));
        }

        // Define the custom comparator lambda for sorting rows.
        auto row_comparator = [&](const auto& row_a_it, const auto& row_b_it) {
            const BasicValue& val_a = *(row_a_it + sort_key_column);
            const BasicValue& val_b = *(row_b_it + sort_key_column);
            return descending ? basic_value_less(val_b, val_a) : basic_value_less(val_a, val_b);
            };

        // Sort the iterators. This changes their order but doesn't move the actual data yet.
        std::sort(row_iterators.begin(), row_iterators.end(), row_comparator);

        // Create a new array and reconstruct the data in the sorted order.
        auto final_sorted_ptr = std::make_shared<Array>();
        final_sorted_ptr->shape = source_ptr->shape;
        final_sorted_ptr->data.reserve(source_ptr->data.size());

        for (const auto& row_it : row_iterators) {
            final_sorted_ptr->data.insert(final_sorted_ptr->data.end(), row_it, row_it + cols);
        }

        return final_sorted_ptr;
    }

    // Fallback for higher-dimensional arrays (currently unsupported)
    Error::set(15, vm.runtime_current_line, "XSORT currently only supports 1D or 2D arrays.");
    return {};
}

void register_array_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate) {
    // Helper lambda to make registration cleaner
    auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        table_to_populate[to_upper(info.name)] = info;
        };
    // --- Register APL-style Array Functions ---
    register_func("IOTA", -1, builtin_iota);
    register_func("RESHAPE", -1, builtin_reshape);
    register_func("REVERSE", 1, builtin_reverse);
    register_func("TRANSPOSE", 1, builtin_transpose);
    register_func("PRODUCT", -1, builtin_product);
    register_func("MIN", -1, builtin_min);
    register_func("MAX", -1, builtin_max);
    register_func("ANY", -1, builtin_any);
    register_func("ALL", -1, builtin_all);
    register_func("SCAN", 2, builtin_scan);
    register_func("SELECT", -1, builtin_select);
    register_func("FILTER", 2, builtin_filter);
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
    register_func("XSORT", -1, builtin_xsort);
    register_func("MVLET", 4, builtin_mvlet);
    register_func("DIFF", 2, builtin_diff);
    register_func("APPEND", 2, builtin_append);
    register_func("ROTATE", 2, builtin_rotate);
    register_func("SHIFT", -1, builtin_shift);
    register_func("CONVOLVE", 3, builtin_convolve);
    register_func("PLACE", 3, builtin_place);
    register_func("NORMALIZE", 1, builtin_normalize);
    register_func("UNIQUE", 1, builtin_unique);
    register_func("SHUFFLE", 1, builtin_shuffle);
    register_func("FIND_IN_ARRAY", 2, builtin_find_in_array);
}
