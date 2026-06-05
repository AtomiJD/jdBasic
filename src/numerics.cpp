// Eigen-backed numerical builtins: SVD, QR, DET, EIG, FFT, IFFT.
//
// Isolated in its own translation unit so Eigen's heavy template headers only
// compile here. Header-only - no runtime dependency, available in every build
// (interpreter, native -c, and the Godot embed).
#include "numerics.h"
#include "vm.h"

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <unsupported/Eigen/FFT>

#include <complex>
#include <stdexcept>
#include <vector>

namespace {

// jdBasic matrix (nested rows of f64) or vector -> Eigen real matrix.
Eigen::MatrixXd to_eigen(const Value& v) {
    auto* a = v.as_array();
    if (!a) throw std::runtime_error("expected a matrix argument");
    int rows = (int)a->elements.size();
    if (rows == 0) return Eigen::MatrixXd(0, 0);
    if (a->elements[0].type == ValueType::ARRAY) {
        int cols = (int)a->elements[0].as_array()->elements.size();
        Eigen::MatrixXd m(rows, cols);
        for (int r = 0; r < rows; r++) {
            auto* row = a->elements[r].as_array();
            for (int c = 0; c < cols; c++)
                m(r, c) = (c < (int)row->elements.size()) ? row->elements[c].to_double() : 0.0;
        }
        return m;
    }
    // 1-D vector -> column matrix.
    Eigen::MatrixXd m(rows, 1);
    for (int r = 0; r < rows; r++) m(r, 0) = a->elements[r].to_double();
    return m;
}

// Eigen real matrix -> jdBasic nested matrix.
Value from_eigen(const Eigen::MatrixXd& m) {
    Value out = Value::make_array();
    auto* oa = out.as_array();
    oa->elements.reserve(m.rows());
    for (int r = 0; r < m.rows(); r++) {
        Value row = Value::make_array();
        auto* ra = row.as_array();
        ra->elements.reserve(m.cols());
        for (int c = 0; c < m.cols(); c++) ra->elements.push_back(Value::make_f64(m(r, c)));
        oa->elements.push_back(std::move(row));
    }
    return out;
}

// Eigen real vector -> jdBasic 1-D array.
Value vec_from_eigen(const Eigen::VectorXd& v) {
    Value out = Value::make_array();
    auto* oa = out.as_array();
    oa->elements.reserve(v.size());
    for (int i = 0; i < v.size(); i++) oa->elements.push_back(Value::make_f64(v(i)));
    return out;
}

// One complex number -> jdBasic [real, imag] pair.
Value complex_pair(const std::complex<double>& c) {
    Value p = Value::make_array();
    p.as_array()->elements.push_back(Value::make_f64(c.real()));
    p.as_array()->elements.push_back(Value::make_f64(c.imag()));
    return p;
}

// Eigen complex matrix/vector -> jdBasic [n][2] (vector) or [r][c][2] (matrix).
Value complex_from_eigen(const Eigen::MatrixXcd& m) {
    Value out = Value::make_array();
    auto* oa = out.as_array();
    if (m.cols() == 1) {
        for (int r = 0; r < m.rows(); r++) oa->elements.push_back(complex_pair(m(r, 0)));
    } else {
        for (int r = 0; r < m.rows(); r++) {
            Value row = Value::make_array();
            for (int c = 0; c < m.cols(); c++) row.as_array()->elements.push_back(complex_pair(m(r, c)));
            oa->elements.push_back(std::move(row));
        }
    }
    return out;
}

// 1-D jdBasic array of reals OR [real,imag] pairs -> complex vector.
std::vector<std::complex<double>> to_complex_vec(const Value& v) {
    auto* a = v.as_array();
    if (!a) throw std::runtime_error("FFT: expected a 1-D array");
    std::vector<std::complex<double>> out;
    out.reserve(a->elements.size());
    for (auto& e : a->elements) {
        if (e.type == ValueType::ARRAY) {
            auto* p = e.as_array();
            double re = p->elements.size() > 0 ? p->elements[0].to_double() : 0.0;
            double im = p->elements.size() > 1 ? p->elements[1].to_double() : 0.0;
            out.emplace_back(re, im);
        } else {
            out.emplace_back(e.to_double(), 0.0);
        }
    }
    return out;
}

Value fft_run(const Value& v, bool inverse) {
    std::vector<std::complex<double>> in = to_complex_vec(v);
    Eigen::FFT<double> fft;
    std::vector<std::complex<double>> out;
    if (inverse) fft.inv(out, in); else fft.fwd(out, in);
    Value r = Value::make_array();
    auto* ra = r.as_array();
    ra->elements.reserve(out.size());
    for (auto& c : out) ra->elements.push_back(complex_pair(c));
    return r;
}

} // namespace

void register_numerics_builtins(VM& vm) {
    // SVD(matrix) -> { U, S, V }  (thin U/V; S is the singular-value vector).
    vm.register_native("SVD", 1, 1, [](const std::vector<Value>& args) -> Value {
        Eigen::MatrixXd M = to_eigen(args[0]);
        Eigen::JacobiSVD<Eigen::MatrixXd> svd(M, Eigen::ComputeThinU | Eigen::ComputeThinV);
        Value o = Value::make_object();
        auto* ob = o.as_object();
        ob->set("U", from_eigen(svd.matrixU()));
        ob->set("S", vec_from_eigen(svd.singularValues()));
        ob->set("V", from_eigen(svd.matrixV()));
        return o;
    });

    // QR(matrix) -> { Q, R }  (Householder; Q is the full orthogonal factor).
    vm.register_native("QR", 1, 1, [](const std::vector<Value>& args) -> Value {
        Eigen::MatrixXd M = to_eigen(args[0]);
        Eigen::HouseholderQR<Eigen::MatrixXd> qr(M);
        Eigen::MatrixXd Q = qr.householderQ();
        Eigen::MatrixXd R = qr.matrixQR().triangularView<Eigen::Upper>();
        Value o = Value::make_object();
        auto* ob = o.as_object();
        ob->set("Q", from_eigen(Q));
        ob->set("R", from_eigen(R));
        return o;
    });

    // DET(matrix) -> scalar determinant.
    vm.register_native("DET", 1, 1, [](const std::vector<Value>& args) -> Value {
        Eigen::MatrixXd M = to_eigen(args[0]);
        if (M.rows() != M.cols()) throw std::runtime_error("DET: matrix must be square");
        return Value::make_f64(M.determinant());
    });

    // EIG(square) -> { EIGENVALUES:[n][2], EIGENVECTORS:[n][n][2] } (complex).
    vm.register_native("EIG", 1, 1, [](const std::vector<Value>& args) -> Value {
        Eigen::MatrixXd M = to_eigen(args[0]);
        if (M.rows() != M.cols()) throw std::runtime_error("EIG: matrix must be square");
        Eigen::EigenSolver<Eigen::MatrixXd> es(M);
        if (es.info() != Eigen::Success) throw std::runtime_error("EIG: failed to converge");
        Value o = Value::make_object();
        auto* ob = o.as_object();
        ob->set("EIGENVALUES", complex_from_eigen(es.eigenvalues()));
        ob->set("EIGENVECTORS", complex_from_eigen(es.eigenvectors()));
        return o;
    });

    // FFT(signal) / IFFT(spectrum) -> [N][2] complex (real/imag pairs).
    // Input is a 1-D array of reals or [real,imag] pairs; any length N.
    vm.register_native("FFT", 1, 1, [](const std::vector<Value>& args) -> Value {
        return fft_run(args[0], false);
    });
    vm.register_native("IFFT", 1, 1, [](const std::vector<Value>& args) -> Value {
        return fft_run(args[0], true);
    });
}
