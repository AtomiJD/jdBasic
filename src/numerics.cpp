// Eigen-backed numerical builtins: MATMUL, MATMUL_T, BMM, SOLVE, INVERT,
// SVD, QR, DET, EIG, FFT, IFFT.
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
    auto number = [](const Value& c) {
        if (c.type == ValueType::ARRAY || c.type == ValueType::STRING || c.type == ValueType::OBJECT)
            throw std::runtime_error("expected a matrix of numbers");
        return c.to_double();
    };
    if (a->elements[0].type == ValueType::ARRAY) {
        int cols = (int)a->elements[0].as_array()->elements.size();
        Eigen::MatrixXd m(rows, cols);
        for (int r = 0; r < rows; r++) {
            if (a->elements[r].type != ValueType::ARRAY)
                throw std::runtime_error("expected a matrix of numbers");
            auto* row = a->elements[r].as_array();
            for (int c = 0; c < cols; c++)
                m(r, c) = (c < (int)row->elements.size()) ? number(row->elements[c]) : 0.0;
        }
        return m;
    }
    // 1-D vector -> column matrix.
    Eigen::MatrixXd m(rows, 1);
    for (int r = 0; r < rows; r++) m(r, 0) = number(a->elements[r]);
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

// The shape of a matrix argument: its rows and the cells of its first row.
// A flat array is a vector: no columns, and to_eigen reads it as a column.
struct Shape { int rows; int cols; bool flat; };

Shape shape_of(const Value& v, const char* fn) {
    auto* a = v.as_array();
    if (!a) throw std::runtime_error(std::string(fn) + ": expected a matrix argument");
    Shape s{ (int)a->elements.size(), 0, true };
    if (s.rows > 0 && a->elements[0].type == ValueType::ARRAY) {
        s.flat = false;
        s.cols = (int)a->elements[0].as_array()->elements.size();
    }
    return s;
}

std::string shape_text(const Shape& s) {
    if (s.flat) return std::to_string(s.rows);
    return std::to_string(s.rows) + "x" + std::to_string(s.cols);
}

// A three-dimensional array: rows whose cells are rows.
bool is_batch(const Value& v) {
    auto* a = v.as_array();
    if (!a || a->elements.empty() || a->elements[0].type != ValueType::ARRAY) return false;
    auto* row = a->elements[0].as_array();
    return !row->elements.empty() && row->elements[0].type == ValueType::ARRAY;
}

// The product of two matrix arguments, the left one transposed when asked.
// A flat array on the right is a column, one on the left a row, and the
// answer of a vector side is a flat array; two vectors are not a product.
Value matrix_product(const char* fn, const Value& a, const Value& b, bool transpose_left) {
    Shape sa = shape_of(a, fn), sb = shape_of(b, fn);
    if (sa.flat && sb.flat)
        throw std::runtime_error(std::string(fn) + ": two vectors; the dot product is SUM(a * b)");
    Eigen::MatrixXd L = to_eigen(a);
    if (sa.flat || transpose_left) L.transposeInPlace();
    Eigen::MatrixXd R = to_eigen(b);
    if (L.cols() != R.rows()) {
        std::string left = transpose_left && !sa.flat
            ? std::to_string(sa.cols) + "x" + std::to_string(sa.rows) : shape_text(sa);
        throw std::runtime_error(std::string(fn) + ": incompatible dimensions, " + left + " and " + shape_text(sb));
    }
    Eigen::MatrixXd P = L * R;
    if (sa.flat) return vec_from_eigen(P.row(0).transpose());
    if (sb.flat) return vec_from_eigen(P.col(0));
    return from_eigen(P);
}

Eigen::MatrixXd square_of(const Value& v, const char* fn) {
    Eigen::MatrixXd M = to_eigen(v);
    if (M.rows() != M.cols())
        throw std::runtime_error(std::string(fn) + ": matrix must be square");
    return M;
}

} // namespace

void register_numerics_builtins(VM& vm) {
    // MATMUL(a, b) -> a * b; MATMUL_T(a, b) -> TRANSPOSE(a) * b without the copy.
    vm.register_native("MATMUL", 2, 2, [](const std::vector<Value>& args) -> Value {
        return matrix_product("MATMUL", args[0], args[1], false);
    });
    vm.register_native("MATMUL_T", 2, 2, [](const std::vector<Value>& args) -> Value {
        return matrix_product("MATMUL_T", args[0], args[1], true);
    });

    // BMM(a, b): the product per batch of two [B][M][K] x [B][K][N] arrays;
    // a side that is one matrix meets every batch of the other.
    vm.register_native("BMM", 2, 2, [](const std::vector<Value>& args) -> Value {
        bool ba = is_batch(args[0]), bb = is_batch(args[1]);
        if (!ba && !bb) return matrix_product("BMM", args[0], args[1], false);
        auto& la = args[0].as_array()->elements;
        auto& lb = args[1].as_array()->elements;
        size_t n = ba ? la.size() : lb.size();
        if (ba && bb && la.size() != lb.size())
            throw std::runtime_error("BMM: batch sizes differ, " + std::to_string(la.size()) +
                                     " and " + std::to_string(lb.size()));
        Value out = Value::make_array();
        auto& cells = out.as_array()->elements;
        cells.reserve(n);
        for (size_t i = 0; i < n; i++)
            cells.push_back(matrix_product("BMM", ba ? la[i] : args[0], bb ? lb[i] : args[1], false));
        return out;
    });

    // SOLVE(a, b) -> x with a * x = b; b is a vector or a matrix of right-hand sides.
    vm.register_native("SOLVE", 2, 2, [](const std::vector<Value>& args) -> Value {
        Eigen::MatrixXd M = square_of(args[0], "SOLVE");
        Shape sb = shape_of(args[1], "SOLVE");
        if (sb.rows != M.rows())
            throw std::runtime_error("SOLVE: incompatible dimensions, " + shape_text(Shape{(int)M.rows(), (int)M.cols(), false}) +
                                     " and " + shape_text(sb));
        Eigen::FullPivLU<Eigen::MatrixXd> lu(M);
        if (!lu.isInvertible()) throw std::runtime_error("SOLVE: singular matrix");
        Eigen::MatrixXd X = lu.solve(to_eigen(args[1]));
        if (sb.flat) return vec_from_eigen(X.col(0));
        return from_eigen(X);
    });

    // INVERT(a) -> the inverse of a square matrix.
    vm.register_native("INVERT", 1, 1, [](const std::vector<Value>& args) -> Value {
        Eigen::FullPivLU<Eigen::MatrixXd> lu(square_of(args[0], "INVERT"));
        if (!lu.isInvertible()) throw std::runtime_error("INVERT: singular matrix");
        return from_eigen(lu.inverse());
    });

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
