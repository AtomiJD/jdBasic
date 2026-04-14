#pragma once
#ifdef COM

#include "value.h"

class VM;

void register_com_builtins(VM& vm);
bool com_try_get_field(const Value& obj, const std::string& field, Value& result);
bool com_try_set_field(const Value& obj, const std::string& field, const Value& val);
bool com_try_call_method(const Value& obj, const std::string& method,
                         const std::vector<Value>& args, Value& result);

#endif // COM
