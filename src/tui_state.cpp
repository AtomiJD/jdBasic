#ifdef TUI

#include "tui_state.h"
// Pulled in here (not in the header) so the unique_ptr<ScreenInteractive>
// destructor can be instantiated against the complete type — keeping the
// header light for callers that only need TuiState fields by pointer.
#include <ftxui/component/screen_interactive.hpp>

namespace jdb_tui {

TuiState& state() {
    static TuiState s;
    return s;
}

void set_host_screen(ftxui::ScreenInteractive* screen) {
    state().host_screen = screen;
}

} // namespace jdb_tui

#endif // TUI
