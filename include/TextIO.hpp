// TextIO.hpp
#pragma once
#include <iostream>
#include <iomanip> // For std::hex
#include <string>
#include <sstream>
#include <streambuf>
#include <cstdint> // For uint16_t, uint8_t

// The CoutRedirector class from above
class CoutRedirector {
private:
    std::stringstream m_targetStream;
    std::streambuf* m_originalBuffer;
public:
    CoutRedirector() {
        m_originalBuffer = std::cout.rdbuf();
        std::cout.rdbuf(m_targetStream.rdbuf());
    }
    ~CoutRedirector() {
        std::cout.rdbuf(m_originalBuffer);
    }
    std::string getString() const {
        return m_targetStream.str();
    }
};

// A namespace for all text input/output related functions
namespace TextIO {
    void print(const std::string& message);
    void print_uw(uint16_t value);
    void print_uwhex(uint16_t value);
    void nl(); // Newline
    void clearScreen();
    void setColor(uint8_t foreground, uint8_t background);
    void locate(int row, int col);
    void setCursor(bool on);
}