#pragma once
#include <vector>
#include <string>

class Editor {
public:
    Editor(std::vector<std::string>& lines, const std::string& filename);
    void run();

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
};
