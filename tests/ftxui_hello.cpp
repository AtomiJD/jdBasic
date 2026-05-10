// FTXUI compile-smoke for the jdBasic build chain.
// Boots a tiny interactive TUI: a teal-bordered box with a centred
// "Hello jdBasic!" label and a single Quit button. ESC or Q exits.
//
// Build (once libs/ftxui/build/ftxui.lib exists via build_ftxui.bat):
//   build_ftxui_hello.bat
//
// This file is intentionally tiny — it proves the includes resolve, the
// static lib links, and the Win console can host an FTXUI session.

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

int main() {
    using namespace ftxui;

    auto screen = ScreenInteractive::TerminalOutput();

    auto quit_button = Button("Quit", screen.ExitLoopClosure(),
                              ButtonOption::Animated(Color::Cyan3));

    auto layout = Container::Vertical({ quit_button });

    auto renderer = Renderer(layout, [&] {
        return vbox({
            text("jdBasic FTXUI smoke") | bold | center | color(Color::Cyan3),
            separator(),
            text("Hello, " + std::string("jdBasic!")) | center,
            text("(press ESC or click Quit)") | dim | center,
            separator(),
            quit_button->Render() | center,
        }) | border | size(WIDTH, EQUAL, 50) | center;
    });

    auto with_esc = CatchEvent(renderer, [&](Event e) {
        if (e == Event::Escape || e == Event::Character('q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(with_esc);
    return 0;
}
