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
#include <unistd.h>  // For STDIN_FILENO, read, write
#include <termios.h> // For termios
#include <cstdio>    // For sscanf
#endif


#ifndef _WIN32
#include <ncurses.h>
/**
 * @brief Checks if a key has been pressed on the console.
 * * This function is a non-blocking equivalent of conio.h's _kbhit().
 * It uses ncurses's non-blocking getch() to peek at the input buffer.
 * If a character is present, it is pushed back onto the buffer
 * so it can be read by a subsequent getch() call.
 * * @return int Returns 1 if a key has been pressed, 0 otherwise.
 */
int TextIO::kbhit() {
    // Set getch() to be non-blocking
    nodelay(stdscr, TRUE);

    int ch = getch();

    if (ch != ERR) {
        // A key was hit. Push it back onto the input stream
        // so it can be read by the next call to getch().
        ungetch(ch);
        nodelay(stdscr, FALSE); // Restore blocking mode
        return 1;
    } else {
        nodelay(stdscr, FALSE); // Restore blocking mode
        return 0;
    }
}
#endif

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