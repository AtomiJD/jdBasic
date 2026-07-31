#pragma once
#ifdef HTTP
class VM;
void register_http_builtins(VM& vm);
#endif
#ifdef __EMSCRIPTEN__
class VM;
void register_wasm_net(VM& vm);
#endif
