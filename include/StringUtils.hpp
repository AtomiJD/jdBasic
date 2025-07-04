// StringUtils.hpp
#pragma once
#include <string>

namespace StringUtils {
    // Checks if a character is a whitespace character.
    bool isspace(char c);

    // Checks if a character is an alphabet letter.
    bool isletter(char c);

    // Checks if a character is a digit.
    bool isdigit(char c);

    // Checks if a string contains a specific character.
    bool contains(const std::string& str, char c);

    // Removes leading and trailing whitespace from a string.
    void strip(std::string& str);

    // Removes leading whitespace.
    void ltrim(std::string& s);

    // Removes trailing whitespace.
    void rtrim(std::string& s);

    // Removes leading and trailing whitespace.
    void trim(std::string& s);

    // Converts a string to uppercase.
    std::string to_upper(std::string s);
}