// PYTHON / PY.* builtins — an embedded CPython interpreter.
//
//   PYTHON$(code$)        run a code block, return its captured stdout
//   PY.EVAL(expr$)        evaluate one expression, return the value natively
//   PY.SET(name$, value)  inject a jdBasic value into the Python namespace
//   PY.GET(name$)         read a Python variable back as a jdBasic value
//   PY.DIR$([target$])    list namespace names, or members of an object
//   PY.HELP$(target$)     inspect.getdoc() of a module/function
//
// One persistent namespace (the `__jdb__` module dict) survives across every
// call, mirroring how the workshop VM keeps state warm. Values convert
// recursively: list<->array, dict<->object/map, str/int/float/bool scalars.
// Any Python object exposing tolist() (numpy arrays, array.array) lands as a
// native jdBasic array, shape- and dtype-agnostic.
//
// Interpreter-only: the CPython C-API can't be driven from a compiled -c
// binary, so llvm_codegen rejects PYTHON$/PY.* at compile time.

#ifdef PYTHON
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#ifdef _WIN32
#include <windows.h>
#endif
#endif

#include "vm.h"
#include "value.h"
#include "pybridge.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

#ifdef PYTHON

namespace {

std::once_flag g_init_once;
bool g_py_ready = false;

#ifdef _WIN32
// Expand %VAR% style references in a path read from the environment.
std::wstring expand_env(const std::wstring& in) {
    DWORD need = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (need == 0) return in;
    std::wstring out(need, L'\0');
    ExpandEnvironmentStringsW(in.c_str(), out.data(), need);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
#endif

// Boot CPython once. Resolves the interpreter home so the standard library is
// found: JDB_PYTHON_HOME wins, otherwise the per-user pythoncore package the
// build is wired against. Leaves the GIL released (PyGILState_Ensure reclaims
// it per call) so any thread — including the MCP worker — can drive Python.
void init_python() {
    PyConfig config;
    PyConfig_InitPythonConfig(&config);

#ifdef _WIN32
    std::wstring home;
    if (const char* env = std::getenv("JDB_PYTHON_HOME"); env && *env) {
        home = expand_env(utf8_to_wide(env));
    } else {
        home = expand_env(L"%LOCALAPPDATA%\\python\\pythoncore-3.14-64");
    }
    if (!home.empty()) {
        PyStatus st = PyConfig_SetString(&config, &config.home, home.c_str());
        if (PyStatus_Exception(st)) PyConfig_Clear(&config);
    }
#else
    if (const char* env = std::getenv("JDB_PYTHON_HOME"); env && *env) {
        PyConfig_SetBytesString(&config, &config.home, env);
    }
#endif

    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        g_py_ready = false;
        return;
    }
    g_py_ready = true;
    PyEval_SaveThread();  // drop the GIL; per-call GILState reclaims it
}

bool ensure_python() {
    std::call_once(g_init_once, init_python);
    return g_py_ready;
}

// RAII GIL hold for the duration of one builtin call.
struct GilGuard {
    PyGILState_STATE state;
    GilGuard() : state(PyGILState_Ensure()) {}
    ~GilGuard() { PyGILState_Release(state); }
};

// The persistent user namespace. Borrowed dict; __builtins__ is injected so
// bare print/len/range work inside a fresh module dict.
PyObject* jdb_dict() {
    PyObject* mod = PyImport_AddModule("__jdb__");  // borrowed
    PyObject* dict = PyModule_GetDict(mod);          // borrowed
    if (!PyDict_GetItemString(dict, "__builtins__")) {
        PyObject* builtins = PyImport_AddModule("builtins");  // borrowed
        PyDict_SetItemString(dict, "__builtins__", builtins);
    }
    return dict;
}

// Pull the current Python exception into a C++ message and clear it.
std::string take_py_error() {
    if (!PyErr_Occurred()) return "";
    PyObject *etype = nullptr, *evalue = nullptr, *etb = nullptr;
    PyErr_Fetch(&etype, &evalue, &etb);
    PyErr_NormalizeException(&etype, &evalue, &etb);
    std::string msg = "Python error";
    if (evalue) {
        PyObject* s = PyObject_Str(evalue);
        if (s) {
            const char* c = PyUnicode_AsUTF8(s);
            if (c) msg = c;
            Py_DECREF(s);
        }
    }
    Py_XDECREF(etype);
    Py_XDECREF(evalue);
    Py_XDECREF(etb);
    PyErr_Clear();
    return msg;
}

// ── jdBasic Value -> Python object (new reference) ───────────
PyObject* value_to_py(const Value& v) {
    switch (v.type) {
        case ValueType::NONE:
            Py_RETURN_NONE;
        case ValueType::BOOLEAN:
            return PyBool_FromLong(v.boolean ? 1 : 0);
        case ValueType::BYTE:
        case ValueType::INT16:
        case ValueType::INT32:
        case ValueType::INT64:
            return PyLong_FromLongLong((long long)v.to_int());
        case ValueType::FLOAT16:
        case ValueType::FLOAT32:
        case ValueType::FLOAT64:
            return PyFloat_FromDouble(v.to_double());
        case ValueType::STRING:
            return PyUnicode_FromString(v.as_string()->data.c_str());
        case ValueType::ARRAY: {
            auto* a = v.as_array();
            PyObject* list = PyList_New((Py_ssize_t)a->elements.size());
            for (size_t i = 0; i < a->elements.size(); i++)
                PyList_SetItem(list, (Py_ssize_t)i, value_to_py(a->elements[i]));
            return list;
        }
        case ValueType::OBJECT: {
            auto* o = v.as_object();
            PyObject* dict = PyDict_New();
            for (auto& [k, val] : o->fields) {
                PyObject* item = value_to_py(val);
                PyDict_SetItemString(dict, k.c_str(), item);
                Py_DECREF(item);
            }
            return dict;
        }
        case ValueType::TENSOR: {
            // Legacy internal type, not user-facing — flatten to a list.
            auto* t = v.as_tensor();
            PyObject* list = PyList_New((Py_ssize_t)t->data.size());
            for (size_t i = 0; i < t->data.size(); i++)
                PyList_SetItem(list, (Py_ssize_t)i, PyFloat_FromDouble(t->data[i]));
            return list;
        }
    }
    Py_RETURN_NONE;
}

// ── Python object -> jdBasic Value (borrowed reference in) ───
Value py_to_value(PyObject* o) {
    if (!o || o == Py_None) return Value::make_none();
    if (PyBool_Check(o)) return Value::make_bool(o == Py_True);
    if (PyLong_Check(o)) return Value::make_i64((int64_t)PyLong_AsLongLong(o));
    if (PyFloat_Check(o)) return Value::make_f64(PyFloat_AsDouble(o));
    if (PyUnicode_Check(o)) {
        const char* s = PyUnicode_AsUTF8(o);
        return Value::make_string(s ? s : "");
    }
    if (PyList_Check(o) || PyTuple_Check(o)) {
        bool tuple = PyTuple_Check(o);
        Py_ssize_t n = tuple ? PyTuple_Size(o) : PyList_Size(o);
        Value arr = Value::make_array();
        auto* a = arr.as_array();
        a->elements.reserve((size_t)n);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject* it = tuple ? PyTuple_GetItem(o, i) : PyList_GetItem(o, i);
            a->elements.push_back(py_to_value(it));
        }
        return arr;
    }
    if (PyDict_Check(o)) {
        Value obj = Value::make_object();
        auto* ob = obj.as_object();
        PyObject *k, *val;
        Py_ssize_t pos = 0;
        while (PyDict_Next(o, &pos, &k, &val)) {
            std::string key;
            if (PyUnicode_Check(k)) {
                const char* c = PyUnicode_AsUTF8(k);
                key = c ? c : "";
            } else {
                PyObject* s = PyObject_Str(k);
                if (s) { const char* c = PyUnicode_AsUTF8(s); key = c ? c : ""; Py_DECREF(s); }
            }
            ob->set(key, py_to_value(val));
        }
        return obj;
    }
    // numpy arrays, array.array, and anything else exposing tolist() convert
    // through Python into native (possibly nested) jdBasic arrays — shape- and
    // dtype-agnostic. jdBasic has no Tensor type, so a plain numeric array is
    // the right landing place.
    if (PyObject_HasAttrString(o, "tolist")) {
        PyObject* lst = PyObject_CallMethod(o, "tolist", nullptr);
        if (lst) {
            Value v = py_to_value(lst);
            Py_DECREF(lst);
            return v;
        }
        PyErr_Clear();
    }
    // Last resort: stringify so the caller at least sees something.
    PyObject* s = PyObject_Str(o);
    if (s) {
        const char* c = PyUnicode_AsUTF8(s);
        Value v = Value::make_string(c ? c : "");
        Py_DECREF(s);
        return v;
    }
    return Value::make_none();
}

[[noreturn]] void fail_no_python() {
    throw std::runtime_error(
        "PYTHON support is not compiled in (build with the PYTHON flag)");
}

}  // namespace

void python_shutdown() {
    if (g_py_ready && Py_IsInitialized()) {
        PyGILState_Ensure();
        Py_Finalize();
        g_py_ready = false;
    }
}

void register_python_builtins(VM& vm) {
    vm.register_native("PYTHON$", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!ensure_python()) fail_no_python();
        GilGuard gil;
        std::string code = args[0].to_string();

        PyObject* io = PyImport_ImportModule("io");
        if (!io) throw std::runtime_error("PYTHON$: " + take_py_error());
        PyObject* buf = PyObject_CallMethod(io, "StringIO", nullptr);
        Py_DECREF(io);
        if (!buf) throw std::runtime_error("PYTHON$: " + take_py_error());

        PyObject* sys = PyImport_ImportModule("sys");
        PyObject* old_out = PyObject_GetAttrString(sys, "stdout");
        PyObject* old_err = PyObject_GetAttrString(sys, "stderr");
        PyObject_SetAttrString(sys, "stdout", buf);
        PyObject_SetAttrString(sys, "stderr", buf);

        PyObject* dict = jdb_dict();
        PyObject* res = PyRun_String(code.c_str(), Py_file_input, dict, dict);
        std::string err = res ? "" : take_py_error();
        Py_XDECREF(res);

        PyObject_SetAttrString(sys, "stdout", old_out);
        PyObject_SetAttrString(sys, "stderr", old_err);
        Py_XDECREF(old_out);
        Py_XDECREF(old_err);
        Py_DECREF(sys);

        std::string out;
        PyObject* got = PyObject_CallMethod(buf, "getvalue", nullptr);
        if (got) {
            const char* c = PyUnicode_AsUTF8(got);
            if (c) out = c;
            Py_DECREF(got);
        }
        Py_DECREF(buf);

        if (!err.empty()) {
            if (!out.empty()) err = out + "\n" + err;
            throw std::runtime_error("Python: " + err);
        }
        if (!out.empty() && out.back() == '\n') out.pop_back();
        return Value::make_string(out);
    });

    vm.register_native("PY.EVAL", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!ensure_python()) fail_no_python();
        GilGuard gil;
        std::string expr = args[0].to_string();
        PyObject* dict = jdb_dict();
        PyObject* res = PyRun_String(expr.c_str(), Py_eval_input, dict, dict);
        if (!res) throw std::runtime_error("PY.EVAL: " + take_py_error());
        Value v = py_to_value(res);
        Py_DECREF(res);
        return v;
    });

    vm.register_native("PY.SET", 2, 2, [](const std::vector<Value>& args) -> Value {
        if (!ensure_python()) fail_no_python();
        GilGuard gil;
        std::string name = args[0].to_string();
        PyObject* val = value_to_py(args[1]);
        int rc = PyDict_SetItemString(jdb_dict(), name.c_str(), val);
        Py_DECREF(val);
        if (rc != 0) throw std::runtime_error("PY.SET: " + take_py_error());
        return Value::make_bool(true);
    });

    vm.register_native("PY.GET", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!ensure_python()) fail_no_python();
        GilGuard gil;
        std::string name = args[0].to_string();
        PyObject* val = PyDict_GetItemString(jdb_dict(), name.c_str());  // borrowed
        if (!val) throw std::runtime_error("PY.GET: name '" + name + "' is not defined");
        return py_to_value(val);
    });

    vm.register_native("PY.DIR$", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (!ensure_python()) fail_no_python();
        GilGuard gil;
        std::string target = args.empty() ? "" : args[0].to_string();
        std::string cmd;
        if (target.empty()) {
            cmd = "__jdb_dir__ = ', '.join(sorted(k for k in list(globals().keys()) "
                  "if not k.startswith('__')))";
        } else {
            cmd = "try:\n"
                  "    __jdb_dir__ = ', '.join(k for k in dir(" + target + ") if not k.startswith('_'))\n"
                  "except Exception as __e:\n"
                  "    __jdb_dir__ = 'Error: ' + str(__e)\n";
        }
        PyObject* dict = jdb_dict();
        PyObject* res = PyRun_String(cmd.c_str(), Py_file_input, dict, dict);
        if (!res) throw std::runtime_error("PY.DIR$: " + take_py_error());
        Py_DECREF(res);
        PyObject* out = PyDict_GetItemString(dict, "__jdb_dir__");  // borrowed
        std::string s = (out && PyUnicode_Check(out)) ? PyUnicode_AsUTF8(out) : "";
        PyDict_DelItemString(dict, "__jdb_dir__");
        return Value::make_string(s);
    });

    vm.register_native("PY.HELP$", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!ensure_python()) fail_no_python();
        GilGuard gil;
        std::string target = args[0].to_string();
        std::string cmd =
            "import inspect\n"
            "try:\n"
            "    __jdb_doc__ = inspect.getdoc(" + target + ") or 'No documentation found.'\n"
            "except Exception as __e:\n"
            "    __jdb_doc__ = 'Error: ' + str(__e)\n";
        PyObject* dict = jdb_dict();
        PyObject* res = PyRun_String(cmd.c_str(), Py_file_input, dict, dict);
        if (!res) throw std::runtime_error("PY.HELP$: " + take_py_error());
        Py_DECREF(res);
        PyObject* out = PyDict_GetItemString(dict, "__jdb_doc__");  // borrowed
        std::string s = (out && PyUnicode_Check(out)) ? PyUnicode_AsUTF8(out) : "";
        PyDict_DelItemString(dict, "__jdb_doc__");
        return Value::make_string(s);
    });
}

#else  // PYTHON not compiled in

void python_shutdown() {}

void register_python_builtins(VM&) {}

#endif
