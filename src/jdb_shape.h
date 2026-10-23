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

} // namespace jdb_shape
