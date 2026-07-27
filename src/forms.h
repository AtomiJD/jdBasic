#pragma once
// Native Windows Forms builtins (Win32 common controls). Real implementation
// only under the FORMS build flag on Windows.
class VM;
void register_forms_builtins(VM& vm);
