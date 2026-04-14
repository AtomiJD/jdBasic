#ifdef COM
// COM Automation support for jdBasic
// Only compiled when COM is defined

#include "value.h"
#include "vm.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <oleauto.h>
#include <comdef.h>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// ── ComObj destructor ────────────────────────────────────────

ComObj::~ComObj() {
    if (dispatch) {
        static_cast<IDispatch*>(dispatch)->Release();
        dispatch = nullptr;
    }
}

// ── Value::make_com ──────────────────────────────────────────

Value Value::make_com(void* idispatch) {
    Value v;
    v.type = ValueType::OBJECT;
    v.i64 = 0;
    auto* co = new ComObj();
    co->dispatch = idispatch;
    if (idispatch) static_cast<IDispatch*>(idispatch)->AddRef();
    v.obj = co; // refcount already 1 from HeapObject default
    return v;
}

// ── Helpers ──────────────────────────────────────────────────

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    w.pop_back();
    return w;
}

static std::string from_wide(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], sz, NULL, NULL);
    s.pop_back();
    return s;
}

// Convert Value → VARIANT
static VARIANT value_to_variant(const Value& v) {
    VARIANT var;
    VariantInit(&var);
    switch (v.type) {
        case ValueType::BOOLEAN:
            var.vt = VT_BOOL;
            var.boolVal = v.boolean ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case ValueType::INT16: case ValueType::INT32:
            var.vt = VT_I4;
            var.lVal = (LONG)v.to_int();
            break;
        case ValueType::INT64: case ValueType::BYTE:
            var.vt = VT_I4;
            var.lVal = (LONG)v.to_int();
            break;
        case ValueType::FLOAT32: case ValueType::FLOAT64: case ValueType::FLOAT16:
            var.vt = VT_R8;
            var.dblVal = v.to_double();
            break;
        case ValueType::STRING: {
            var.vt = VT_BSTR;
            std::wstring ws = to_wide(v.as_string()->data);
            var.bstrVal = SysAllocString(ws.c_str());
            break;
        }
        case ValueType::OBJECT: {
#ifdef COM
            auto* com = const_cast<Value&>(v).as_com();
            if (com && com->dispatch) {
                var.vt = VT_DISPATCH;
                var.pdispVal = static_cast<IDispatch*>(com->dispatch);
                var.pdispVal->AddRef();
                break;
            }
#endif
            var.vt = VT_EMPTY;
            break;
        }
        default:
            if (v.type == ValueType::NONE) { var.vt = VT_EMPTY; }
            else { var.vt = VT_R8; var.dblVal = v.to_double(); }
            break;
    }
    return var;
}

// Convert VARIANT → Value
static Value variant_to_value(const VARIANT& var) {
    // Handle VT_BYREF: dereference and recurse on the underlying value.
    // ADO Recordset Field.Value commonly returns VARIANT pointers.
    if (var.vt & VT_BYREF) {
        VARIANT deref;
        VariantInit(&deref);
        if (SUCCEEDED(VariantCopyInd(&deref, const_cast<VARIANT*>(&var)))) {
            Value v = variant_to_value(deref);
            VariantClear(&deref);
            return v;
        }
        return Value::make_none();
    }
    switch (var.vt) {
        case VT_EMPTY: case VT_NULL:
            return Value::make_none();
        case VT_BOOL:
            return Value::make_bool(var.boolVal != VARIANT_FALSE);
        case VT_I1:    return Value::make_i64(var.cVal);
        case VT_UI1:   return Value::make_i64(var.bVal);
        case VT_I2:    return Value::make_i64(var.iVal);
        case VT_UI2:   return Value::make_i64(var.uiVal);
        case VT_I4:    return Value::make_i64(var.lVal);
        case VT_UI4:   return Value::make_i64((int64_t)var.ulVal);
        case VT_INT:   return Value::make_i64(var.intVal);
        case VT_UINT:  return Value::make_i64((int64_t)var.uintVal);
        case VT_I8:    return Value::make_i64(var.llVal);
        case VT_UI8:   return Value::make_i64((int64_t)var.ullVal);
        case VT_R4:    return Value::make_f64(var.fltVal);
        case VT_R8:    return Value::make_f64(var.dblVal);
        case VT_CY: {
            // Currency: 64-bit fixed-point, 4 decimal places
            return Value::make_f64((double)var.cyVal.int64 / 10000.0);
        }
        case VT_DECIMAL: {
            // Decimal → double via VariantChangeType
            VARIANT conv; VariantInit(&conv);
            if (SUCCEEDED(VariantChangeType(&conv, const_cast<VARIANT*>(&var), 0, VT_R8))) {
                double d = conv.dblVal;
                VariantClear(&conv);
                return Value::make_f64(d);
            }
            return Value::make_none();
        }
        case VT_DATE: {
            // OLE date → ISO-ish string. Most code paths just want a printable
            // form; converting to BSTR uses the user's locale which we want.
            VARIANT conv; VariantInit(&conv);
            if (SUCCEEDED(VariantChangeType(&conv, const_cast<VARIANT*>(&var), 0, VT_BSTR))) {
                Value r = Value::make_string(from_wide(conv.bstrVal ? conv.bstrVal : L""));
                VariantClear(&conv);
                return r;
            }
            return Value::make_none();
        }
        case VT_BSTR:
            return Value::make_string(from_wide(var.bstrVal ? var.bstrVal : L""));
        case VT_DISPATCH:
            if (var.pdispVal) return Value::make_com(var.pdispVal);
            return Value::make_none();
        case VT_UNKNOWN:
            // IUnknown* — we don't wrap these as COM (need IDispatch for invoke)
            return Value::make_none();
        case VT_ERROR:
            return Value::make_i64(var.scode);
        default: {
            // Try ChangeType to string as a last resort. Use VariantChangeType
            // which makes a temporary copy — never modify the input variant.
            VARIANT conv;
            VariantInit(&conv);
            if (SUCCEEDED(VariantChangeType(&conv, const_cast<VARIANT*>(&var), 0, VT_BSTR))) {
                Value r = Value::make_string(from_wide(conv.bstrVal ? conv.bstrVal : L""));
                VariantClear(&conv);
                return r;
            }
            return Value::make_none();
        }
    }
}

// ── COM dispatch helpers ─────────────────────────────────────

static DISPID get_dispid(IDispatch* pDisp, const std::string& name) {
    std::wstring wname = to_wide(name);
    LPOLESTR oleName = const_cast<LPOLESTR>(wname.c_str());
    DISPID dispid;
    HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &oleName, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) throw std::runtime_error("COM: Unknown member '" + name + "'");
    return dispid;
}

// COM property get or method call with no args
Value com_get(IDispatch* pDisp, const std::string& name) {
    DISPID dispid = get_dispid(pDisp, name);
    DISPPARAMS dp = { NULL, NULL, 0, 0 };
    VARIANT result;
    VariantInit(&result);
    EXCEPINFO ei = {};
    HRESULT hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYGET | DISPATCH_METHOD, &dp, &result, &ei, NULL);
    if (FAILED(hr)) {
        if (ei.bstrDescription) {
            std::string msg = from_wide(ei.bstrDescription);
            SysFreeString(ei.bstrDescription);
            throw std::runtime_error("COM: " + msg);
        }
        throw std::runtime_error("COM: Failed to get '" + name + "'");
    }
    Value v = variant_to_value(result);
    VariantClear(&result);
    return v;
}

// COM property put
void com_put(IDispatch* pDisp, const std::string& name, const Value& val) {
    DISPID dispid = get_dispid(pDisp, name);
    VARIANT arg = value_to_variant(val);
    DISPID namedArg = DISPID_PROPERTYPUT;
    DISPPARAMS dp = { &arg, &namedArg, 1, 1 };
    EXCEPINFO ei = {};
    HRESULT hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYPUT, &dp, NULL, &ei, NULL);
    VariantClear(&arg);
    if (FAILED(hr)) {
        if (ei.bstrDescription) {
            std::string msg = from_wide(ei.bstrDescription);
            SysFreeString(ei.bstrDescription);
            throw std::runtime_error("COM: " + msg);
        }
        throw std::runtime_error("COM: Failed to set '" + name + "'");
    }
}

// COM method call with args
Value com_call(IDispatch* pDisp, const std::string& name, const std::vector<Value>& args) {
    DISPID dispid = get_dispid(pDisp, name);

    // COM args are in reverse order
    std::vector<VARIANT> vargs(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        vargs[args.size() - 1 - i] = value_to_variant(args[i]);
    }

    DISPPARAMS dp = {};
    dp.rgvarg = vargs.empty() ? NULL : vargs.data();
    dp.cArgs = (UINT)vargs.size();

    VARIANT result;
    VariantInit(&result);
    EXCEPINFO ei = {};
    HRESULT hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
        DISPATCH_METHOD | DISPATCH_PROPERTYGET, &dp, &result, &ei, NULL);

    for (auto& v : vargs) VariantClear(&v);

    if (FAILED(hr)) {
        if (ei.bstrDescription) {
            std::string msg = from_wide(ei.bstrDescription);
            SysFreeString(ei.bstrDescription);
            throw std::runtime_error("COM: " + msg);
        }
        throw std::runtime_error("COM: Failed to call '" + name + "'");
    }
    Value v = variant_to_value(result);
    VariantClear(&result);
    return v;
}

// ── Register COM natives ─────────────────────────────────────

void register_com_builtins(VM& vm) {
    vm.register_native("CREATEOBJECT", [](const std::vector<Value>& args) -> Value {
        std::string progId = args[0].as_string()->data;
        std::wstring wProgId = to_wide(progId);

        CLSID clsid;
        HRESULT hr = CLSIDFromProgID(wProgId.c_str(), &clsid);
        if (FAILED(hr))
            throw std::runtime_error("COM: Cannot find ProgID '" + progId + "'");

        IDispatch* pDisp = nullptr;
        hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER,
            IID_IDispatch, (void**)&pDisp);
        if (FAILED(hr) || !pDisp)
            throw std::runtime_error("COM: Cannot create object '" + progId + "'");

        // CoCreateInstance already gives us a ref, make_com adds another → release one
        Value v = Value::make_com(pDisp);
        pDisp->Release(); // balance the AddRef in make_com
        return v;
    });

    // RELEASEOBJECT: explicitly release COM references on given objects
    vm.register_native("RELEASEOBJECT", [](const std::vector<Value>& args) -> Value {
        for (auto& a : args) {
            auto* com = const_cast<Value&>(a).as_com();
            if (com && com->dispatch) {
                static_cast<IDispatch*>(com->dispatch)->Release();
                com->dispatch = nullptr;
            }
        }
        return Value::make_none();
    });

    // RELEASEALL: aggressively release ALL COM references everywhere
    vm.register_native("RELEASEALL", [&vm](const std::vector<Value>& args) -> Value {
        (void)args;
        // Helper: release COM from any Value
        auto release_val = [](Value& v) {
            if (v.type == ValueType::OBJECT && v.obj) {
                auto* com = dynamic_cast<ComObj*>(v.obj);
                if (com && com->dispatch) {
                    static_cast<IDispatch*>(com->dispatch)->Release();
                    com->dispatch = nullptr;
                }
                heap_release(v.obj);
                v.obj = nullptr;
                v.type = ValueType::NONE;
            }
        };
        // Release in globals
        auto& globals = const_cast<std::vector<Value>&>(vm.get_globals());
        for (auto& g : globals) release_val(g);
        // Force GC by calling CoFreeUnusedLibraries
        CoFreeUnusedLibraries();
        return Value::make_none();
    });
}

// ── COM-aware field access (called from VM) ──────────────────

// We must distinguish two failure modes:
//   (a) "this isn't a COM object I know about / no such member" — the
//       caller should fall through to non-COM handling.
//   (b) "the member exists, but the COM call itself failed (bad SQL,
//       file not found, ...)" — the error must propagate so the user
//       sees a real message instead of a silent NONE / Undefined function
//       which leaves the value stack inconsistent and crashes the VM.
//
// get_dispid throws std::runtime_error starting with "COM: Unknown member"
// for case (a). Real call failures throw "COM: <description>" from
// EXCEPINFO or "COM: Failed to ..." for FAILED HRESULTs without info.

static bool is_unknown_member_error(const std::string& msg) {
    return msg.rfind("COM: Unknown member", 0) == 0;
}

bool com_try_get_field(const Value& obj, const std::string& field, Value& result) {
    auto* com = const_cast<Value&>(obj).as_com();
    if (!com || !com->dispatch) return false;
    try {
        result = com_get(static_cast<IDispatch*>(com->dispatch), field);
        return true;
    } catch (const std::exception& e) {
        if (is_unknown_member_error(e.what())) return false;
        throw; // real COM error → propagate
    }
}

bool com_try_set_field(const Value& obj, const std::string& field, const Value& val) {
    auto* com = const_cast<Value&>(obj).as_com();
    if (!com || !com->dispatch) return false;
    try {
        com_put(static_cast<IDispatch*>(com->dispatch), field, val);
        return true;
    } catch (const std::exception& e) {
        if (is_unknown_member_error(e.what())) return false;
        throw;
    }
}

bool com_try_call_method(const Value& obj, const std::string& method,
                         const std::vector<Value>& args, Value& result) {
    auto* com = const_cast<Value&>(obj).as_com();
    if (!com || !com->dispatch) return false;
    try {
        result = com_call(static_cast<IDispatch*>(com->dispatch), method, args);
        return true;
    } catch (const std::exception& e) {
        if (is_unknown_member_error(e.what())) return false;
        throw;
    }
}

#endif // COM
