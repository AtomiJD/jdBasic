#pragma once
#include "NeReLaBasic.hpp"


BasicValue builtin_reverse(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_slice(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_stack(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_mvlet(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_transpose(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_normalize(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_unique(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_shuffle(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_find_in_array(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_rotate(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_shift(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_convolve(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_place(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_product(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue array_sum(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_min(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_max(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_any(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_all(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_scan(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_reduce(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_select(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_filter(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_iota(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_reshape(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_outer(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_integrate(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_solve(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_invert(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_take(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_drop(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_grade(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_diff(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_append(NeReLaBasic& vm, const std::vector<BasicValue>& args);
BasicValue builtin_xsort(NeReLaBasic& vm, const std::vector<BasicValue>& args);


void register_array_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate);
