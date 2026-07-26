#pragma once

#ifdef FTXUI

#include <memory>
#include <vector>

class VM;

// Run the FTXUI-based REPL with one VM per workspace. The caller (main)
// constructs and pre-registers each VM exactly the way it sets up the
// legacy console workspaces - same dynamic-code hooks, same builtins,
// same OS args - and hands them off via std::move. The REPL adopts
// ownership and rebinds vm.on_output per workspace to its own output
// buffer. Phase 2b ships 4 workspaces (F1-F4) by convention.
int run_repl_ftxui(std::vector<std::unique_ptr<VM>> vms);

#endif
