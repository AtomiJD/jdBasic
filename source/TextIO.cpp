// TextIO.cpp
#include "TextIO.hpp"
#include <iostream>
#include <iomanip> // For std::hex
#include <string>
#include <sstream>
#include <streambuf>
#include <cstdint> // For uint16_t, uint8_t



void TextIO::print(const std::string& message) {
    std::cout << message;
}

void TextIO::print_uw(uint16_t value) {
    // Explicitly set the stream to decimal mode before printing.
    std::cout << std::dec << value;
}

void TextIO::print_uwhex(uint16_t value) {
    // std::hex makes the output hexadecimal
    // std::setw and std::setfill ensure it's padded with zeros to 4 digits
    std::cout << '$' << std::hex << std::setw(4) << std::setfill('0') << std::uppercase << value;
}

void TextIO::nl() {
    std::cout << '\n';
}

void TextIO::clearScreen() {
    // This is OS-dependent. For now, a simple simulation.
    // On Windows, you could use: system("cls");
    std::cout << "\x1B[2J\x1B[H";
}

void TextIO::setColor(uint8_t foreground, uint8_t background) {
    int fgs = 30;
    int bgs = 40;
    if (foreground > 7) fgs = 82;
    if (background > 7) bgs = 92;
    std::cout << "\x1B[" + std::to_string(foreground+fgs) + ";" + std::to_string(background+bgs) + "m";
}

void TextIO::locate(int row, int col) {
    // Using standard ANSI escape codes to position the cursor.
    // BASIC is typically 1-indexed, so we don't need to subtract 1.
    std::cout << "\x1B[" << row << ";" << col << "H";
}

void TextIO::setCursor(bool on) {
    if (on) {
        std::cout << "\033[?25h"; // ANSI code to show cursor
    }
    else {
        std::cout << "\033[?25l"; // ANSI code to hide cursor
    }
}