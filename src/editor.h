#pragma once
#include <vector>
#include <string>

class Editor {
public:
    Editor(std::vector<std::string>& lines, const std::string& filename);
    void run();
    // True when the editor exited because the user pressed F5 (compile + run
    // the current buffer). The caller decides what to do with the buffer.
    bool wants_run() const { return run_requested; }

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    bool run_requested = false;
};
