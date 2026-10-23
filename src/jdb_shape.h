// Shape algorithms over nested arrays, shared by the VM and the native
// runtime: each instantiates them with its own array model through an
// accessor type A.
//
//   A::Arr                   the array handle (a Value, a JdbArray*)
//   A::len(a)                the number of cells
//   A::is_row(a, i)          the cell holds an array
//   A::row(a, i)             that array
//   A::num(a, i)             the cell as a number
//   A::make(n)               a fresh array of n number cells, all zero
//   A::put_num(a, i, d)      write a number into a cell
//   A::put_bool(a, i, b)     write a truth value into a cell
//   A::put_row(a, i, r)      write an array into a cell
//   A::drop(a)               free a working array whose cells are borrowed
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace jdb_shape {

using Lane = std::function<double(const std::vector<double>&)>;

// The number of dimensions, read down the first cells.
template <class A>
int depth(typename A::Arr a) {
    int d = 1;
    while (A::len(a) > 0 && A::is_row(a, 0)) { a = A::row(a, 0); d++; }
    return d;
}

// One cell of a reduction's answer: a number or an array.
template <class A>
struct Cell {
    bool is_num;
    double d;
    typename A::Arr a;
};

template <class A>
static void put_cell(typename A::Arr out, int64_t i, const Cell<A>& c, bool as_bool) {
    if (!c.is_num) A::put_row(out, i, c.a);
    else if (as_bool) A::put_bool(out, i, c.d != 0.0);
    else A::put_num(out, i, c.d);
}

// The reduction of a along one axis: the answer has that axis taken out.
// Axis 0 of a flat array is one number; axis 0 of rows reduces, for every
// place in a row, the column of the cells at that place; a deeper axis
// reduces every row by the axis below it.
template <class A>
Cell<A> reduce_axis(typename A::Arr a, int axis, const Lane& lane, bool as_bool) {
    int64_t n = A::len(a);
    if (axis > 0) {
        typename A::Arr out = A::make(n);
        for (int64_t i = 0; i < n; i++) {
            if (!A::is_row(a, i)) { A::put_num(out, i, A::num(a, i)); continue; }
            put_cell<A>(out, i, reduce_axis<A>(A::row(a, i), axis - 1, lane, as_bool), as_bool);
        }
        return { false, 0.0, out };
    }
    if (n == 0 || !A::is_row(a, 0)) {
        std::vector<double> v((size_t)n);
        for (int64_t i = 0; i < n; i++) v[(size_t)i] = A::num(a, i);
        return { true, lane(v), typename A::Arr() };
    }
    int64_t width = A::len(A::row(a, 0));
    typename A::Arr out = A::make(width);
    for (int64_t j = 0; j < width; j++) {
        typename A::Arr col = A::make(n);
        for (int64_t i = 0; i < n; i++) {
            if (!A::is_row(a, i)) continue;
            typename A::Arr r = A::row(a, i);
            if (j >= A::len(r)) continue;
            if (A::is_row(r, j)) A::put_row(col, i, A::row(r, j));
            else A::put_num(col, i, A::num(r, j));
        }
        put_cell<A>(out, j, reduce_axis<A>(col, 0, lane, as_bool), as_bool);
        A::drop(col);
    }
    return { false, 0.0, out };
}

// ── Two arrays under a cell operation ────────────────────────────────────
//
// The accessor adds, for these walks:
//   A::Val                   a cell's value (a Value, a double)
//   A::cell(a, i)            the cell as a value, when it is not a row
//   A::put(a, i, v)          write a value into a cell
//   A::shape_error(n, m)     two lengths that do not meet; the walk then
//                            answers an empty array

template <class A>
using Op = std::function<typename A::Val(const typename A::Val&, const typename A::Val&)>;

template <class A>
typename A::Arr pair(typename A::Arr a, typename A::Arr b, const Op<A>& op);

// An array and one value: the value meets every cell, rows recursed.
template <class A>
typename A::Arr spread(typename A::Arr a, const typename A::Val& v, bool value_left, const Op<A>& op) {
    int64_t n = A::len(a);
    typename A::Arr out = A::make(n);
    for (int64_t i = 0; i < n; i++) {
        if (A::is_row(a, i)) A::put_row(out, i, spread<A>(A::row(a, i), v, value_left, op));
        else A::put(out, i, value_left ? op(v, A::cell(a, i)) : op(A::cell(a, i), v));
    }
    return out;
}

// One cell of a meets one cell of b: two rows meet row by row, a row and a
// value meet the value in every cell, two values meet under the operation.
template <class A>
static void meet(typename A::Arr out, int64_t k, typename A::Arr a, int64_t i,
                 typename A::Arr b, int64_t j, const Op<A>& op) {
    bool ra = A::is_row(a, i), rb = A::is_row(b, j);
    if (ra && rb) A::put_row(out, k, pair<A>(A::row(a, i), A::row(b, j), op));
    else if (ra) A::put_row(out, k, spread<A>(A::row(a, i), A::cell(b, j), false, op));
    else if (rb) A::put_row(out, k, spread<A>(A::row(b, j), A::cell(a, i), true, op));
    else A::put(out, k, op(A::cell(a, i), A::cell(b, j)));
}

// Two arrays under an operation: equal lengths meet cell by cell; a flat
// array meets every row of a matrix; an array of one cell gives that cell to
// every cell of the other; any other pair of lengths is a shape error.
template <class A>
typename A::Arr pair(typename A::Arr a, typename A::Arr b, const Op<A>& op) {
    int64_t n = A::len(a), m = A::len(b);
    bool a_rows = n > 0 && A::is_row(a, 0);
    bool b_rows = m > 0 && A::is_row(b, 0);
    if (a_rows && !b_rows) {
        typename A::Arr out = A::make(n);
        for (int64_t i = 0; i < n; i++) A::put_row(out, i, pair<A>(A::row(a, i), b, op));
        return out;
    }
    if (b_rows && !a_rows) {
        typename A::Arr out = A::make(m);
        for (int64_t j = 0; j < m; j++) A::put_row(out, j, pair<A>(a, A::row(b, j), op));
        return out;
    }
    if (n == m) {
        typename A::Arr out = A::make(n);
        for (int64_t i = 0; i < n; i++) meet<A>(out, i, a, i, b, i, op);
        return out;
    }
    if (n == 1) {
        typename A::Arr out = A::make(m);
        for (int64_t j = 0; j < m; j++) meet<A>(out, j, a, 0, b, j, op);
        return out;
    }
    if (m == 1) {
        typename A::Arr out = A::make(n);
        for (int64_t i = 0; i < n; i++) meet<A>(out, i, a, i, b, 0, op);
        return out;
    }
    A::shape_error(n, m);
    return A::make(0);
}

// ── IIF over an array condition ──────────────────────────────────────────

// A branch of IIF: an array, or one value for every cell.
template <class A>
struct Branch {
    bool is_arr;
    typename A::Arr a;
    typename A::Val v;
};

// The branch's cell for place i: its own cell, cyclically, or the one value.
template <class A>
static Branch<A> branch_at(const Branch<A>& b, int64_t i) {
    if (!b.is_arr || A::len(b.a) == 0) return b;
    int64_t k = i % A::len(b.a);
    if (A::is_row(b.a, k)) return { true, A::row(b.a, k), typename A::Val() };
    return { false, typename A::Arr(), A::cell(b.a, k) };
}

// IIF cell by cell, into the rows of a condition that is a matrix; a branch
// that is an array gives its own cell, a value is the same in every cell.
template <class A>
typename A::Arr pick(typename A::Arr cond, const Branch<A>& t, const Branch<A>& f,
                     const std::function<bool(const typename A::Val&)>& truth) {
    int64_t n = A::len(cond);
    typename A::Arr out = A::make(n);
    for (int64_t i = 0; i < n; i++) {
        Branch<A> ti = branch_at<A>(t, i), fi = branch_at<A>(f, i);
        if (A::is_row(cond, i)) {
            A::put_row(out, i, pick<A>(A::row(cond, i), ti, fi, truth));
            continue;
        }
        const Branch<A>& chosen = truth(A::cell(cond, i)) ? ti : fi;
        if (chosen.is_arr) A::put_row(out, i, chosen.a);
        else A::put(out, i, chosen.v);
    }
    return out;
}

// ── A function over the cells ────────────────────────────────────────────

// A function of n values applied cell by cell over its array operands, the
// shorter ones taken round again, rows recursed: SIN over an array, LEFT$
// over an array of strings and an array of counts.
template <class A>
typename A::Arr zip(const std::vector<Branch<A>>& ops,
                    const std::function<typename A::Val(const std::vector<typename A::Val>&)>& fn) {
    int64_t n = 0;
    for (auto& o : ops) if (o.is_arr && A::len(o.a) > n) n = A::len(o.a);
    typename A::Arr out = A::make(n);
    std::vector<Branch<A>> sub(ops.size());
    std::vector<typename A::Val> vals(ops.size());
    for (int64_t i = 0; i < n; i++) {
        bool rows = false;
        for (size_t k = 0; k < ops.size(); k++) {
            sub[k] = branch_at<A>(ops[k], i);
            if (sub[k].is_arr) rows = true;
        }
        if (rows) { A::put_row(out, i, zip<A>(sub, fn)); continue; }
        for (size_t k = 0; k < ops.size(); k++) vals[k] = sub[k].v;
        A::put(out, i, fn(vals));
    }
    return out;
}

// ── Order, take and unique ───────────────────────────────────────────────
//
// The accessor adds, for these walks:
//   A::copy_cell(out, k, a, i)   cell i of a into cell k of out, as it is
//   A::carry_flags(out, a)       what the array says about all its cells

// The indices 0 to n-1 in the order less puts the cells they name; cells
// that neither precedes keep their order.
inline std::vector<int64_t> order(int64_t n, const std::function<bool(int64_t, int64_t)>& less) {
    std::vector<int64_t> idx((size_t)(n < 0 ? 0 : n));
    for (int64_t i = 0; i < n; i++) idx[(size_t)i] = i;
    std::stable_sort(idx.begin(), idx.end(), less);
    return idx;
}

// The cells of a in the order the indices name.
template <class A>
typename A::Arr take(typename A::Arr a, const std::vector<int64_t>& idx) {
    typename A::Arr out = A::make((int64_t)idx.size());
    A::carry_flags(out, a);
    for (size_t k = 0; k < idx.size(); k++) A::copy_cell(out, (int64_t)k, a, idx[k]);
    return out;
}

// The indices of the first cell of every group of cells that same puts together.
inline std::vector<int64_t> unique_indices(int64_t n, const std::function<bool(int64_t, int64_t)>& same) {
    std::vector<int64_t> kept;
    for (int64_t i = 0; i < n; i++) {
        bool dup = false;
        for (int64_t j : kept) if (same(i, j)) { dup = true; break; }
        if (!dup) kept.push_back(i);
    }
    return kept;
}

// ── RESHAPE ──────────────────────────────────────────────────────────────

// The cells of a in order, rows opened as they come.
template <class A>
void flatten(typename A::Arr a, std::vector<typename A::Val>& out) {
    int64_t n = A::len(a);
    for (int64_t i = 0; i < n; i++) {
        if (A::is_row(a, i)) flatten<A>(A::row(a, i), out);
        else out.push_back(A::cell(a, i));
    }
}

// An array of the shape's dimensions from dim on, its cells taken from
// flat in order and again from the start when flat runs out.
template <class A>
typename A::Arr build(const std::vector<int64_t>& shape, size_t dim,
                      const std::vector<typename A::Val>& flat, size_t& idx) {
    int64_t n = shape[dim] < 0 ? 0 : shape[dim];
    typename A::Arr out = A::make(n);
    for (int64_t i = 0; i < n; i++) {
        if (dim + 1 < shape.size()) A::put_row(out, i, build<A>(shape, dim + 1, flat, idx));
        else { A::put(out, i, flat[idx % flat.size()]); idx++; }
    }
    return out;
}

} // namespace jdb_shape
