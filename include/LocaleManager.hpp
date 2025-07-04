#pragma once
#include <string>
#include <locale>

namespace LocaleManager {
    // Sets the global locale for number formatting.
    void set_current_locale(const std::string& locale_name);

    // Gets the currently active locale.
    const std::locale& get_current_locale();
}
