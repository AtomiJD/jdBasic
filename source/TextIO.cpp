// TextIO.cpp
#include "TextIO.hpp"
#include <iostream>
#include <iomanip> // For std::hex
#include <string>
#include <sstream>
#include <streambuf>
#include <cstdint> // For uint16_t, uint8_t

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#endif


#ifndef _WIN32
int is_icanon = 0;
static struct termios st;

void TextIO::deinitKey() {
    if (is_icanon) {
        tcsetattr(STDIN_FILENO,TCSAFLUSH, &st);
        is_icanon = 0;
    }
}

int TextIO::initKey() {
    struct termios raw;
    if (is_icanon)
        return 0;
    if (tcgetattr(STDIN_FILENO, &st) == -1)
        return -1;
    raw = st;  
    raw.c_iflag &= ~( BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN );
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;
    is_icanon = 1;
    return 0;
}

unsigned char TextIO::jdgetch() {
    int nread;
    unsigned char c;
    nread = read(STDIN_FILENO,&c,1);
    if (nread == 0)
        return 0;
    else
        return c;
}

ssize_t TextIO::jdwrite(int fd, const char *buf, size_t count) {
    size_t bytes_left = count;
    const char *ptr = buf;

    while (bytes_left > 0) {
        ssize_t bytes_written = write(fd, ptr, bytes_left);
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }
        bytes_left -= bytes_written;
        ptr += bytes_written;
    }
    return count;
}

#endif

void TextIO::print(const std::string& message) {
#ifdef _WIN32    
    std::cout << message;
#else
    TextIO::jdwrite(STDOUT_FILENO, message.c_str(), message.length());
#endif
}

void TextIO::print_uw(uint16_t value) {
#ifdef _WIN32
    // Explicitly set the stream to decimal mode before printing.
    std::cout << std::dec << value;
#else
    std::string s = std::to_string(value);
    TextIO::jdwrite(STDOUT_FILENO, s.c_str(), s.length());
#endif
}

void TextIO::print_uwhex(uint16_t value) {
#ifdef _WIN32
    // std::hex makes the output hexadecimal
    // std::setw and std::setfill ensure it's padded with zeros to 4 digits
    std::cout << '$' << std::hex << std::setw(4) << std::setfill('0') << std::uppercase << value;
#else
    std::stringstream ss;
    ss << '$' << std::hex << std::setw(4) << std::setfill('0') << std::uppercase << value;
    std::string s = ss.str();
    TextIO::jdwrite(STDOUT_FILENO, s.c_str(), s.length());
#endif
}

void TextIO::nl() {
#ifdef _WIN32    
    std::cout << '\n';
#else
    TextIO::jdwrite(STDOUT_FILENO, "\n\r", 2);
#endif
}

void TextIO::clearScreen() {
    const char* clear_code = "\x1B[2J\x1B[H";
#ifdef _WIN32    
    std::cout << clear_code;
#else
    TextIO::jdwrite(STDOUT_FILENO, clear_code, 7); // The string literal has a fixed length of 7
#endif
}

void TextIO::setColor(uint8_t foreground, uint8_t background) {
    int fgs = 30;
    int bgs = 40;
    if (foreground > 7) fgs = 82;
    if (background > 7) bgs = 92;
    std::string s = "\x1B[" + std::to_string(foreground + fgs) + ";" + std::to_string(background + bgs) + "m";

#ifdef _WIN32
    std::cout << s;
#else
    TextIO::jdwrite(STDOUT_FILENO, s.c_str(), s.length());
#endif
}

void TextIO::locate(int row, int col) {
    // Using standard ANSI escape codes to position the cursor.
    std::string s = "\x1B[" + std::to_string(row) + ";" + std::to_string(col) + "H";

#ifdef _WIN32
    std::cout << s;
#else
    TextIO::jdwrite(STDOUT_FILENO, s.c_str(), s.length());
#endif
}

void TextIO::setCursor(bool on) {
    const char* code = on ? "\x1B[?25h" : "\x1B[?25l"; // ANSI code to show/hide cursor
#ifdef _WIN32
    std::cout << code;
#else
    TextIO::jdwrite(STDOUT_FILENO, code, 6); // The string literals have a fixed length of 6
#endif
}

void TextIO::getCursorPosition(int& row, int& col) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        // The Win32 API returns 0-based coordinates.
        // We add 1 to match the 1-based indexing used by locate().
        col = csbi.dwCursorPosition.X + 1;
        row = csbi.dwCursorPosition.Y + 1;
    }
    else {
        // In case of an error, return -1.
        row = -1;
        col = -1;
    }
#else
    // 1. Set the terminal to raw mode to read the response character by character.
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    // 2. Send the DSR query "\x1B[6n" to request the cursor position.
    std::cout << "\x1B[6n" << std::flush;

    // 3. Read the response from stdin. The response is in the format "\x1B[<row>;<col>R".
    char buf[32] = { 0 };
    int i = 0;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    // 4. Parse the row and column from the response buffer.
    if (sscanf(buf, "\x1B[%d;%d", &row, &col) != 2) {
        row = -1;
        col = -1;
    }

    // 5. Restore the original terminal settings.
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
#endif
}

int TextIO::getCursorX() {
    int row, col;
    getCursorPosition(row, col);
    return col;
}

int TextIO::getCursorY() {
    int row, col;
    getCursorPosition(row, col);
    return row;
}