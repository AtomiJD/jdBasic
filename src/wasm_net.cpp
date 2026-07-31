// Browser networking for the Emscripten build: HTTP.GET$ / HTTP.POST$ /
// HTTP.STATUSCODE on top of the fetch API. httplib needs raw sockets, which
// the browser does not have; fetch is the only road. Asyncify suspends the
// VM while the promise resolves, so these natives block exactly like the
// desktop httplib ones and game code runs unchanged on both targets.
#ifdef __EMSCRIPTEN__
#include "vm.h"
#include <emscripten.h>
#include <cstdlib>
#include <stdexcept>
#include <string>

// Runs one fetch. Returns a malloc'd UTF-8 body (caller frees) or 0 on a
// network-level failure (DNS, refused, CORS, timeout). The HTTP status and
// any error text are parked on Module for the sync getters below.
EM_ASYNC_JS(char*, jdb_fetch_run,
            (const char* url, const char* method, const char* body,
             const char* ctype), {
    try {
        const opts = { method: UTF8ToString(method) };
        if (opts.method !== 'GET') {
            opts.body = UTF8ToString(body);
            opts.headers = { 'Content-Type': UTF8ToString(ctype) };
        }
        if (typeof AbortSignal !== 'undefined' && AbortSignal.timeout)
            opts.signal = AbortSignal.timeout(15000);
        const resp = await fetch(UTF8ToString(url), opts);
        Module.jdbHttpStatus = resp.status;
        Module.jdbHttpError = '';
        const text = await resp.text();
        const n = lengthBytesUTF8(text) + 1;
        const p = _malloc(n);
        stringToUTF8(text, p, n);
        return p;
    } catch (e) {
        Module.jdbHttpStatus = 0;
        Module.jdbHttpError = '' + e;
        return 0;
    }
});

EM_JS(int, jdb_fetch_status, (), {
    return Module.jdbHttpStatus | 0;
});

EM_JS(char*, jdb_fetch_error, (), {
    const t = Module.jdbHttpError ? '' + Module.jdbHttpError : '';
    const n = lengthBytesUTF8(t) + 1;
    const p = _malloc(n);
    stringToUTF8(t, p, n);
    return p;
});

static std::string wasm_fetch(const std::string& url, const char* method,
                              const std::string& body,
                              const std::string& ctype) {
    char* p = jdb_fetch_run(url.c_str(), method, body.c_str(), ctype.c_str());
    if (!p) {
        char* e = jdb_fetch_error();
        std::string msg = e ? e : "";
        std::free(e);
        throw std::runtime_error(std::string("HTTP ") + method + " failed: " +
                                 url + " (" + msg + ")");
    }
    std::string out(p);
    std::free(p);
    return out;
}

void register_wasm_net(VM& vm) {
    vm.register_native("HTTP.GET$", 1, 1,
                       [](const std::vector<Value>& args) -> Value {
        return Value::make_string(
            wasm_fetch(args[0].as_string()->data, "GET", "", ""));
    });

    vm.register_native("HTTP.POST$", 2, 3,
                       [](const std::vector<Value>& args) -> Value {
        std::string ctype = (args.size() >= 3) ? args[2].as_string()->data
                                               : "application/json";
        return Value::make_string(
            wasm_fetch(args[0].as_string()->data, "POST",
                       args[1].as_string()->data, ctype));
    });

    vm.register_native("HTTP.STATUSCODE", 0, 0,
                       [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_i64(jdb_fetch_status());
    });
}
#endif  // __EMSCRIPTEN__
