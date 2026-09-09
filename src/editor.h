#pragma once
#include <vector>
#include <string>

class Editor {
public:
    // goto_line > 0 opens on that line; otherwise on the spot the file was
    // left at in this session, or at the top.
    Editor(std::vector<std::string>& lines, const std::string& filename, int goto_line = 0);
    void run();
    // F5: run the buffer on the live VM without saving. The caller decides
    // what to do with the buffer.
    bool wants_run() const { return run_requested; }
    // Ctrl-R: the file is saved; run it and come back on the error line.
    bool wants_run_return() const { return run_return_requested; }
    // The name the buffer was saved under, after a Save as.
    const std::string& file() const { return filename; }

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    int goto_line = 0;
    bool run_requested = false;
    bool run_return_requested = false;
};
