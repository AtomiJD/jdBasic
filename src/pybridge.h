#pragma once
class VM;
void register_python_builtins(VM& vm);
// Tear down the embedded interpreter if it was started. Safe to call when
// Python was never initialised (no-op). Invoked at process shutdown.
void python_shutdown();
