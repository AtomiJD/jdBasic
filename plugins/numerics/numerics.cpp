// numerics.cpp
#include "numerics.h"
#include "NeReLaBasic.hpp"
#include "Types.hpp"

// --- Include Eigen Headers ---
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <unsupported/Eigen/FFT>

#include <string>
#include <vector>
#include <memory>
#include <complex>

// --- Global pointers to hold the functions provided by the main app ---
namespace {
    ErrorSetFunc g_error_set = nullptr;
    ToUpperFunc g_to_upper = nullptr;
    ToStringFunc g_to_string = nullptr;
}

// === HELPER FUNCTIONS ===

// Safe function to convert any numeric BasicValue to a double.
double to_eigen_double(const BasicValue& val) {
    return std::visit([](auto&& arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) { return arg ? 1.0 : 0.0; }
        else if constexpr (std::is_same_v<T, double>) { return arg; }
        else if constexpr (std::is_same_v<T, int>) { return static_cast<double>(arg); }
        return 0.0; // Default for non-numeric types
        }, val);
}

// Helper to convert a jdBasic Array to an Eigen Matrix (for real numbers)
Eigen::MatrixXd jd_array_to_eigen_matrix(const std::shared_ptr<Array>& arr_ptr) {
    if (arr_ptr->shape.size() == 1) { // Vector
        Eigen::MatrixXd mat(arr_ptr->shape[0], 1);
        for (size_t i = 0; i < arr_ptr->data.size(); ++i) {
            mat(i, 0) = to_eigen_double(arr_ptr->data[i]);
        }
        return mat;
    }
    else { // Matrix
        Eigen::MatrixXd mat(arr_ptr->shape[0], arr_ptr->shape[1]);
        for (size_t r = 0; r < arr_ptr->shape[0]; ++r) {
            for (size_t c = 0; c < arr_ptr->shape[1]; ++c) {
                mat(r, c) = to_eigen_double(arr_ptr->data[r * arr_ptr->shape[1] + c]);
            }
        }
        return mat;
    }
}

// Helper to convert an Eigen Matrix (real) back to a jdBasic Array
BasicValue eigen_matrix_to_jd_array(const Eigen::MatrixXd& mat) {
    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { (size_t)mat.rows(), (size_t)mat.cols() };
    result_ptr->data.resize(mat.size());
    for (int r = 0; r < mat.rows(); ++r) {
        for (int c = 0; c < mat.cols(); ++c) {
            result_ptr->data[r * mat.cols() + c] = mat(r, c);
        }
    }
    return result_ptr;
}

// Helper to convert an Eigen Matrix of complex numbers to a jdBasic Array [rows x 2] or [rows x cols x 2]
BasicValue eigen_complex_to_jd_array(const Eigen::MatrixXcd& mat) {
    auto result_ptr = std::make_shared<Array>();
    if (mat.cols() == 1) { // It's a vector
        result_ptr->shape = { (size_t)mat.rows(), 2 };
    } else { // It's a matrix
        result_ptr->shape = { (size_t)mat.rows(), (size_t)mat.cols(), 2 };
    }
    
    result_ptr->data.reserve(mat.size() * 2);
    for (int r = 0; r < mat.rows(); ++r) {
        for (int c = 0; c < mat.cols(); ++c) {
            result_ptr->data.push_back(mat(r, c).real());
            result_ptr->data.push_back(mat(r, c).imag());
        }
    }
    return result_ptr;
}


// === BUILT-IN FUNCTION IMPLEMENTATIONS ===

void builtin_fft_internal(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result, bool is_inverse) {
    const char* func_name = is_inverse ? "IFFT" : "FFT";
    if (args.size() != 1) {
        g_error_set(8, vm.runtime_current_line, std::string(func_name) + " requires 1 argument.");
        *out_result = {}; return;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        g_error_set(15, vm.runtime_current_line, "Argument must be an array.");
        *out_result = {}; return;
    }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->shape.size() != 1) {
        g_error_set(15, vm.runtime_current_line, std::string(func_name) + " only supports 1D arrays.");
        *out_result = {}; return;
    }

    std::vector<std::complex<double>> time_domain_vec;
    time_domain_vec.reserve(arr_ptr->data.size());
    for (const auto& val : arr_ptr->data) {
        time_domain_vec.emplace_back(std::get<double>(val), 0.0);
    }
    
    Eigen::FFT<double> fft;
    std::vector<std::complex<double>> freq_domain_vec;
    if (is_inverse) {
        fft.inv(freq_domain_vec, time_domain_vec);
    } else {
        fft.fwd(freq_domain_vec, time_domain_vec);
    }

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { freq_domain_vec.size(), 2 };
    result_ptr->data.reserve(freq_domain_vec.size() * 2);
    for (const auto& c : freq_domain_vec) {
        result_ptr->data.push_back(c.real());
        result_ptr->data.push_back(c.imag());
    }
    *out_result = result_ptr;
}

void builtin_fft(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    builtin_fft_internal(vm, args, out_result, false);
}
void builtin_ifft(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    builtin_fft_internal(vm, args, out_result, true);
}

void builtin_eig(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) { g_error_set(8, vm.runtime_current_line, "EIG requires 1 argument."); *out_result = {}; return; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { g_error_set(15, vm.runtime_current_line, "Argument must be an array."); *out_result = {}; return; }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->shape.size() != 2 || arr_ptr->shape[0] != arr_ptr->shape[1]) {
        g_error_set(15, vm.runtime_current_line, "Argument to EIG must be a square matrix.");
        *out_result = {}; return;
    }

    Eigen::MatrixXd M = jd_array_to_eigen_matrix(arr_ptr);
    Eigen::EigenSolver<Eigen::MatrixXd> es(M);
    
    if(es.info() != Eigen::Success) {
        g_error_set(1, vm.runtime_current_line, "Eigenvalue decomposition failed to converge.");
        *out_result = {}; return;
    }

    auto result_map = std::make_shared<Map>();
    result_map->data["EIGENVALUES"] = eigen_complex_to_jd_array(es.eigenvalues());
    result_map->data["EIGENVECTORS"] = eigen_complex_to_jd_array(es.eigenvectors());
    
    *out_result = result_map;
}

void builtin_svd(NeReLaBasic& vm, const std::vector<BasicValue>& args, BasicValue* out_result) {
    if (args.size() != 1) { g_error_set(8, vm.runtime_current_line, "SVD requires 1 argument."); *out_result = {}; return; }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) { g_error_set(15, vm.runtime_current_line, "Argument must be an array."); *out_result = {}; return; }
    const auto& arr_ptr = std::get<std::shared_ptr<Array>>(args[0]);
    if (!arr_ptr || arr_ptr->shape.size() != 2) {
        g_error_set(15, vm.runtime_current_line, "Argument to SVD must be a 2D matrix.");
        *out_result = {}; return;
    }

    Eigen::MatrixXd M = jd_array_to_eigen_matrix(arr_ptr);
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(M, Eigen::ComputeThinU | Eigen::ComputeThinV);

    auto result_map = std::make_shared<Map>();
    result_map->data["U"] = eigen_matrix_to_jd_array(svd.matrixU());
    result_map->data["V"] = eigen_matrix_to_jd_array(svd.matrixV());
    result_map->data["S"] = eigen_matrix_to_jd_array(svd.singularValues());

    *out_result = result_map;
}

// === REGISTRATION LOGIC ===

void register_numerics_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table) {
    auto register_func = [&](const std::string& name, int arity, NativeDLLFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_dll_impl = func_ptr;
        table[g_to_upper(name)] = info;
    };

    register_func("FFT", 1, builtin_fft);
    register_func("IFFT", 1, builtin_ifft);
    register_func("EIG", 1, builtin_eig);
    register_func("SVD", 1, builtin_svd);
}

// The main entry point of the DLL.
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services) {
    if (!vm || !services) {
        return;
    }
    g_error_set = services->error_set;
    g_to_upper = services->to_upper;
    g_to_string = services->to_string;
    register_numerics_functions(*vm, vm->main_function_table);
}