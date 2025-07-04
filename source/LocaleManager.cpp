#include "LocaleManager.hpp"
#include "Error.hpp"
#include "TextIO.hpp"
#include <iostream>

namespace {
    // This static variable holds the current locale for the entire application.
    // It's initialized to the default "C" locale.
    std::locale g_current_locale("C");
}

namespace LocaleManager {

    void set_current_locale(const std::string& locale_name) {
        try {
            g_current_locale = std::locale(locale_name.c_str());
            // Also imbue the standard cout for any direct C++ printing
            std::cout.imbue(g_current_locale);
        }
        catch (const std::runtime_error&) {
            Error::set(1, 0); // Re-use "Syntax Error" or create a new "Invalid Locale" error
            TextIO::print("Error: Invalid or unsupported locale name '" + locale_name + "'\n");
        }
    }

    const std::locale& get_current_locale() {
        return g_current_locale;
    }
}