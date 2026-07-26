#pragma once
//
// TUI.* - FTXUI bridge for jdBasic. See doc/ftxui_plan.md.
//
// Always provides register_tui_natives(VM&). When /DTUI is OFF the
// function is a no-op (defined in tui.cpp's #else branch) so main.cpp
// can call it unconditionally if it wants - the call site in main.cpp
// is itself guarded today to keep symbol noise out of non-TUI builds.

class VM;

void register_tui_natives(VM& vm);
