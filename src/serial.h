#pragma once
#ifdef USE_SERIAL
class VM;
void register_serial_builtins(VM& vm);
#endif
