#include "AppConfig.hpp"
#ifdef SDL3
#include "Commands.hpp"
#include "NeReLaBasic.hpp"
#include "SDLFunctions.hpp"
#include "Error.hpp"
#include "Types.hpp"
#include <string>       // For std::string, std::to_string
#include <vector>       // For std::vector
#include <map>

// --- SDL Integration ---

// This map is now defined directly in this file to resolve linker errors.
const std::map<std::string, Waveform> waveform_map = {
    {"SINE", Waveform::SINE},
    {"SQUARE", Waveform::SQUARE},
    {"SAW", Waveform::SAWTOOTH},
    {"TRIANGLE", Waveform::TRIANGLE}
};

extern const std::map<std::string, Waveform> waveform_map;

// SCREEN width, height, [title$]
// Initializes the graphics screen.
BasicValue builtin_screen(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 4) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }

    int width = static_cast<int>(to_double(args[0]));
    int height = static_cast<int>(to_double(args[1]));
    std::string title = "jdBasic Graphics";
    float scale = 1.0f;

    if (args.size() == 3) {
        title = to_string(args[2]);
    }

    if (args.size() == 4) {
        scale = static_cast<float>(to_double(args[3]));
    }

    // Pass all arguments, including the new scale factor, to the init method
    if (!vm.graphics_system.init(title, width, height, scale)) {
        Error::set(1, vm.runtime_current_line); // Generic error
    }

#ifdef __EMSCRIPTEN__
    emscripten_run_script("window.set_graphics_mode(true);");
#endif

    return false; // Procedures return a dummy value
}

// SCREENFLIP
// Updates the screen to show all drawing done since the last flip.
BasicValue builtin_screenflip(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
#ifdef __EMSCRIPTEN__
    // For WASM, SCREENFLIP signals that the frame is done.
    // The browser will handle the actual screen update.
    // We set a flag to tell the C++ main loop to yield control.
    vm.yielded_for_frame = true;
    vm.graphics_system.update_screen();
#else
    vm.graphics_system.update_screen();
#endif
    return false;
}

// Add these two new functions with the other built-ins
BasicValue builtin_screenwidth(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "SCREENWIDTH does not accept arguments.");
        return 0.0;
    }
    if (vm.graphics_system.is_initialized) {
        return static_cast<double>(vm.graphics_system.get_width());
    }
    return 0.0;
}

BasicValue builtin_screenheight(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "SCREENHEIGHT does not accept arguments.");
        return 0.0;
    }
    if (vm.graphics_system.is_initialized) {
        return static_cast<double>(vm.graphics_system.get_height());
    }
    return 0.0;
}

// TOGGLE_FULLSCREEN command
// Toggles between fullscreen and windowed
BasicValue builtin_toggle_fullscreen(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "TOGGLE_FULLSCREEN does not accept arguments.");
        return false;
    }
    if (vm.graphics_system.is_initialized) {
        vm.graphics_system.toggle_fullscreen();
    }
    else {
        Error::set(1, vm.runtime_current_line, "Graphics screen not initialized.");
    }
    return false; // It's a procedure
}

BasicValue builtin_drawcolor(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Check for the correct number of arguments (either 1 or 3)
    if (args.size() != 1 && args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "DRAWCOLOR requires 3 numeric arguments or 1 array argument.");
        return false;
    }

    Uint8 r, g, b;

    // Case 1: Single array argument, e.g., DRAWCOLOR [r, g, b]
    if (args.size() == 1) {
        // Ensure the argument is an array
        if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
            Error::set(15, vm.runtime_current_line, "Single argument to DRAWCOLOR must be an array.");
            return false;
        }

        const auto& color_vec_ptr = std::get<std::shared_ptr<Array>>(args[0]);

        // Ensure the array is valid and has exactly 3 elements
        if (!color_vec_ptr || color_vec_ptr->data.size() != 3) {
            Error::set(15, vm.runtime_current_line, "Color array for DRAWCOLOR must contain exactly 3 elements.");
            return false;
        }

        // Extract RGB values from the array
        r = static_cast<Uint8>(to_double(color_vec_ptr->data[0]));
        g = static_cast<Uint8>(to_double(color_vec_ptr->data[1]));
        b = static_cast<Uint8>(to_double(color_vec_ptr->data[2]));
    }
    // Case 2: Three separate scalar arguments, e.g., DRAWCOLOR r, g, b
    else {
        r = static_cast<Uint8>(to_double(args[0]));
        g = static_cast<Uint8>(to_double(args[1]));
        b = static_cast<Uint8>(to_double(args[2]));
    }

    // Call the underlying graphics system function with the extracted colors
    vm.graphics_system.setDrawColor(r, g, b);
    return false; // Procedures return a dummy value
}

BasicValue builtin_pset(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& points = std::get<std::shared_ptr<Array>>(args[0]);
        std::shared_ptr<Array> colors = nullptr;
        // Check for optional colors matrix
        if (args.size() == 2 && std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
            colors = std::get<std::shared_ptr<Array>>(args[1]);
        }
        else if (args.size() > 1) {
            Error::set(15, vm.runtime_current_line, "Second argument for vectorized PSET must be a color matrix.");
            return false;
        }
        vm.graphics_system.pset(points, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 2 || args.size() == 4 || args.size() > 5) {
        Error::set(8, vm.runtime_current_line, "Usage: PSET x, y, [r, g, b] OR PSET matrix, [colors]");
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));

    if (args.size() == 5) {
        Uint8 r = static_cast<Uint8>(to_double(args[2]));
        Uint8 g = static_cast<Uint8>(to_double(args[3]));
        Uint8 b = static_cast<Uint8>(to_double(args[4]));
        vm.graphics_system.pset(x, y, r, g, b);
    }
    else { // 2 args
        vm.graphics_system.pset(x, y);
    }
    return false;
}

BasicValue builtin_line(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& lines = std::get<std::shared_ptr<Array>>(args[0]);
        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 2 && std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
            colors = std::get<std::shared_ptr<Array>>(args[1]);
        }
        else if (args.size() > 1) {
            Error::set(15, vm.runtime_current_line, "Second argument for vectorized LINE must be a color matrix.");
            return false;
        }
        vm.graphics_system.line(lines, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 4 || args.size() == 5 || args.size() == 6 || args.size() > 7) {
        Error::set(8, vm.runtime_current_line, "Usage: LINE x1, y1, x2, y2, [r, g, b] OR LINE matrix, [colors]");
        return false;
    }

    int x1 = static_cast<int>(to_double(args[0]));
    int y1 = static_cast<int>(to_double(args[1]));
    int x2 = static_cast<int>(to_double(args[2]));
    int y2 = static_cast<int>(to_double(args[3]));

    if (args.size() == 7) {
        Uint8 r = static_cast<Uint8>(to_double(args[4]));
        Uint8 g = static_cast<Uint8>(to_double(args[5]));
        Uint8 b = static_cast<Uint8>(to_double(args[6]));
        vm.graphics_system.line(x1, y1, x2, y2, r, g, b);
    }
    else { // 4 args
        vm.graphics_system.line(x1, y1, x2, y2);
    }
    return false;
}

BasicValue builtin_rect(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& rects = std::get<std::shared_ptr<Array>>(args[0]);
        bool is_filled = false;
        if (args.size() >= 2) is_filled = to_bool(args[1]);

        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 3 && std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
            colors = std::get<std::shared_ptr<Array>>(args[2]);
        }
        else if (args.size() > 2) {
            Error::set(15, vm.runtime_current_line, "Third argument for vectorized RECT must be a color matrix.");
            return false;
        }
        vm.graphics_system.rect(rects, is_filled, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 4 || args.size() > 8) {
        Error::set(8, vm.runtime_current_line, "Usage: RECT x, y, w, h, [r, g, b], [fill] OR RECT matrix, [fill], [colors]");
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    int w = static_cast<int>(to_double(args[2]));
    int h = static_cast<int>(to_double(args[3]));
    bool fill = false;

    if (args.size() >= 7) { // Color is provided
        Uint8 r = static_cast<Uint8>(to_double(args[4]));
        Uint8 g = static_cast<Uint8>(to_double(args[5]));
        Uint8 b = static_cast<Uint8>(to_double(args[6]));
        if (args.size() == 8) fill = to_bool(args[7]);
        vm.graphics_system.rect(x, y, w, h, r, g, b, fill);
    }
    else { // No color, just check for fill
        if (args.size() == 5) fill = to_bool(args[4]);
        vm.graphics_system.rect(x, y, w, h, fill);
    }
    return false;
}

// CIRCLE x, y, r, [fill], [r, g, b] OR CIRCLE matrix, [fill], [colors]
BasicValue builtin_circle(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Case 1: Vector/Matrix arguments
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& circles = std::get<std::shared_ptr<Array>>(args[0]);
        bool is_filled = false;
        if (args.size() >= 2) is_filled = to_bool(args[1]);

        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 3 && std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
            colors = std::get<std::shared_ptr<Array>>(args[2]);
        }
        else if (args.size() > 2) {
            Error::set(15, vm.runtime_current_line, "Third argument for vectorized CIRCLE must be a color matrix.");
            return false;
        }
        vm.graphics_system.circle(circles, is_filled, colors);
        return false;
    }

    // Case 2: Scalar arguments
    if (args.size() < 3 || args.size() > 7) {
        Error::set(8, vm.runtime_current_line, "Usage: CIRCLE x, y, r, [fill], [r, g, b] OR CIRCLE matrix, [fill], [colors]");
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    int radius = static_cast<int>(to_double(args[2]));
    bool fill = false;

    if (args.size() >= 6) { // Color is provided
        fill = to_bool(args[3]);
        Uint8 r = static_cast<Uint8>(to_double(args[4]));
        Uint8 g = static_cast<Uint8>(to_double(args[5]));
        Uint8 b = static_cast<Uint8>(to_double(args[6]));
        vm.graphics_system.circle(x, y, radius, r, g, b, fill);
    }
    else { // No color, just check for fill
        if (args.size() == 4) fill = to_bool(args[3]);
        vm.graphics_system.circle(x, y, radius, fill);
    }
    return false;
}


// ELLIPSE cx, cy, rx, ry, [fill], [r, g, b] OR ELLIPSE matrix, [fill], [colors]
BasicValue builtin_ellipse(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Vectorized
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& ellipses = std::get<std::shared_ptr<Array>>(args[0]);
        bool is_filled = (args.size() >= 2) ? to_bool(args[1]) : false;
        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 3 && std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
            colors = std::get<std::shared_ptr<Array>>(args[2]);
        }
        vm.graphics_system.ellipse(ellipses, is_filled, colors);
        return false;
    }

    // Scalar
    if (args.size() < 4 || args.size() > 8) {
        Error::set(8, vm.runtime_current_line, "Usage: ELLIPSE x, y, rx, ry, [fill], [r, g, b]");
        return false;
    }
    int cx = to_double(args[0]);
    int cy = to_double(args[1]);
    int rx = to_double(args[2]);
    int ry = to_double(args[3]);
    bool fill = (args.size() >= 5) ? to_bool(args[4]) : false;

    if (args.size() == 8) {
        Uint8 r = to_double(args[5]), g = to_double(args[6]), b = to_double(args[7]);
        vm.graphics_system.ellipse(cx, cy, rx, ry, r, g, b, fill);
    }
    else {
        vm.graphics_system.ellipse(cx, cy, rx, ry, fill);
    }
    return false;
}

// ROUNDED_RECT x, y, w, h, radius, [fill], [r, g, b] OR ROUNDED_RECT matrix, [fill], [colors]
BasicValue builtin_rounded_rect(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Vectorized
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& rects = std::get<std::shared_ptr<Array>>(args[0]);
        bool is_filled = (args.size() >= 2) ? to_bool(args[1]) : false;
        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 3 && std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
            colors = std::get<std::shared_ptr<Array>>(args[2]);
        }
        vm.graphics_system.rounded_rect(rects, is_filled, colors);
        return false;
    }

    // Scalar
    if (args.size() < 5 || args.size() > 9) {
        Error::set(8, vm.runtime_current_line, "Usage: ROUNDED_RECT x, y, w, h, radius, [fill], [r, g, b]");
        return false;
    }
    int x = to_double(args[0]), y = to_double(args[1]), w = to_double(args[2]), h = to_double(args[3]), rad = to_double(args[4]);
    bool fill = (args.size() >= 6) ? to_bool(args[5]) : false;
    if (args.size() == 9) {
        Uint8 r = to_double(args[6]), g = to_double(args[7]), b = to_double(args[8]);
        vm.graphics_system.rounded_rect(x, y, w, h, rad, r, g, b, fill);
    }
    else {
        vm.graphics_system.rounded_rect(x, y, w, h, rad, fill);
    }
    return false;
}

// CIRCLE_SECTOR cx, cy, radius, start_angle, end_angle, [fill], [r, g, b] OR CIRCLE_SECTOR matrix, [fill], [colors]
BasicValue builtin_circle_sector(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // Vectorized
    if (!args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
        const auto& sectors = std::get<std::shared_ptr<Array>>(args[0]);
        bool is_filled = (args.size() >= 2) ? to_bool(args[1]) : false;
        std::shared_ptr<Array> colors = nullptr;
        if (args.size() == 3 && std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
            colors = std::get<std::shared_ptr<Array>>(args[2]);
        }
        vm.graphics_system.circle_sector(sectors, is_filled, colors);
        return false;
    }

    // Scalar
    if (args.size() < 5 || args.size() > 9) {
        Error::set(8, vm.runtime_current_line, "Usage: CIRCLE_SECTOR cx, cy, rad, start, end, [fill], [r, g, b]");
        return false;
    }
    int cx = to_double(args[0]), cy = to_double(args[1]), rad = to_double(args[2]);
    float start = to_double(args[3]), end = to_double(args[4]);
    bool fill = (args.size() >= 6) ? to_bool(args[5]) : false;

    if (args.size() == 9) {
        Uint8 r = to_double(args[6]), g = to_double(args[7]), b = to_double(args[8]);
        vm.graphics_system.circle_sector(cx, cy, rad, start, end, r, g, b, fill);
    }
    else {
        vm.graphics_system.circle_sector(cx, cy, rad, start, end, fill);
    }
    return false;
}

// SETFONT font_path$, font_size
// Sets the font and size for subsequent TEXT commands.
BasicValue builtin_setfont(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SETFONT requires 2 arguments: font_path$, font_size.");
        return false;
    }

    std::string path = to_string(args[0]);
    int size = static_cast<int>(to_double(args[1]));

    if (!vm.graphics_system.load_font(path, size)) {
        // The C++ function already prints a detailed error.
        // We can set a generic BASIC error if we want.
        Error::set(1, vm.runtime_current_line, "Failed to set font.");
    }

    return false; // This is a procedure
}

// TEXT x, y, content$, [r, g, b]
// Draws a string on the graphics screen.
BasicValue builtin_text(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // We need at least 3 arguments (x, y, content$)
    // and can have up to 6 (x, y, content$, r, g, b)
    if (args.size() < 3 || args.size() > 6) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }

    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    std::string content = to_string(args[2]);

    // Default color is white
    Uint8 r = 255, g = 255, b = 255;

    // If color arguments are provided, use them
    if (args.size() == 6) {
        r = static_cast<Uint8>(to_double(args[3]));
        g = static_cast<Uint8>(to_double(args[4]));
        b = static_cast<Uint8>(to_double(args[5]));
    }

    // Call the new method in our graphics system
    vm.graphics_system.text(x, y, content, r, g, b);

    return false; // Procedures return a dummy value
}

BasicValue builtin_plotraw(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    // 1. Validate Arguments
    if (args.size() > 5) {
        Error::set(8, vm.runtime_current_line, "PLOTRAW requires 3 arguments: x, y, matrix, scaleX, scaleY");
        return false;
    }
    if (!std::holds_alternative<std::shared_ptr<Array>>(args[2])) {
        Error::set(15, vm.runtime_current_line, "Third argument to PLOTRAW must be a matrix.");
        return false;
    }

    // 2. Parse Arguments
    int x = static_cast<int>(to_double(args[0]));
    int y = static_cast<int>(to_double(args[1]));
    int scaleX = static_cast<int>(to_double(args[3]));
    int scaleY = static_cast<int>(to_double(args[4]));

    const auto& matrix_ptr = std::get<std::shared_ptr<Array>>(args[2]);

    // 3. Call the Graphics System Method
    vm.graphics_system.plot_raw(x, y, matrix_ptr, scaleX, scaleY);

    return false; // Procedures return a dummy value
}

// --- TURTLE GRAPHICS PROCEDURES ---

// TURTLE.FORWARD distance
BasicValue builtin_turtle_forward(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float distance = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_forward(distance);
    return false;
}

// TURTLE.BACKWARD distance
BasicValue builtin_turtle_backward(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float distance = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_backward(distance);
    return false;
}

// TURTLE.LEFT degrees
BasicValue builtin_turtle_left(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float degrees = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_left(degrees);
    return false;
}

// TURTLE.RIGHT degrees
BasicValue builtin_turtle_right(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float degrees = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_right(degrees);
    return false;
}

// TURTLE.PENUP
BasicValue builtin_turtle_penup(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_penup();
    return false;
}

// TURTLE.PENDOWN
BasicValue builtin_turtle_pendown(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_pendown();
    return false;
}

// TURTLE.SETPOS x, y
BasicValue builtin_turtle_setpos(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return false; }
    float x = static_cast<float>(to_double(args[0]));
    float y = static_cast<float>(to_double(args[1]));
    vm.graphics_system.turtle_setpos(x, y);
    return false;
}

// TURTLE.SETHEADING degrees
BasicValue builtin_turtle_setheading(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    float degrees = static_cast<float>(to_double(args[0]));
    vm.graphics_system.turtle_setheading(degrees);
    return false;
}

// TURTLE.HOME
BasicValue builtin_turtle_home(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    // We need screen dimensions for home, but we can get them from the renderer
    int w, h;
    SDL_GetRenderOutputSize(vm.graphics_system.renderer, &w, &h);
    vm.graphics_system.turtle_home(w, h);
    return false;
}

// TURTLE.DRAW
// Redraws the entire path the turtle has taken so far.
BasicValue builtin_turtle_draw(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_draw_path();
    return false;
}

// TURTLE.CLEAR
// Clears the turtle's path memory. Does not clear the screen.
BasicValue builtin_turtle_clear(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line); return false; }
    vm.graphics_system.turtle_clear_path();
    return false;
}

// TURTLE.SET_COLOR r, g, b
BasicValue builtin_turtle_set_color(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    Uint8 r = static_cast<Uint8>(to_double(args[0]));
    Uint8 g = static_cast<Uint8>(to_double(args[1]));
    Uint8 b = static_cast<Uint8>(to_double(args[2]));
    vm.graphics_system.turtle_set_color(r, g, b);
    return false;
}

// --- SDL Sound Functions ---

// SOUND.INIT
// Initializes the sound system. Must be called before any other sound command.
BasicValue builtin_sound_init(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line); // Wrong number of arguments
        return false;
    }
    // Assumes `sound_system` is a member of your NeReLaBasic class `vm`
    if (!vm.sound_system.init(8)) { // Initialize with 8 tracks
        Error::set(1, vm.runtime_current_line, "Failed to initialize sound system.");
    }
    return false;
}

// SOUND.VOICE track, waveform$, attack, decay, sustain, release
// Configures the sound of a specific track.
BasicValue builtin_sound_voice(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 6) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    std::string waveform_str = to_upper(to_string(args[1]));
    double attack = to_double(args[2]);
    double decay = to_double(args[3]);
    double sustain = to_double(args[4]);
    double release = to_double(args[5]);

    if (waveform_map.find(waveform_str) == waveform_map.end()) {
        Error::set(1, vm.runtime_current_line, "Invalid waveform. Use SINE, SQUARE, SAW, or TRIANGLE.");
        return false;
    }
    Waveform wave = waveform_map.at(waveform_str);

    vm.sound_system.set_voice(track, wave, attack, decay, sustain, release);
    return false;
}

// SOUND.PLAY track, frequency
// Plays a note on a given track.
BasicValue builtin_sound_play(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    double freq = to_double(args[1]);
    vm.sound_system.play_note(track, freq);
    return false;
}

// SOUND.RELEASE track
// Starts the release phase of a note on a given track.
BasicValue builtin_sound_release(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    vm.sound_system.release_note(track);
    return false;
}

// SOUND.STOP track
// Immediately silences a note on a given track.
BasicValue builtin_sound_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int track = static_cast<int>(to_double(args[0]));
    vm.sound_system.stop_note(track);
    return false;
}

// SFX.LOAD id, "filepath.wav"
BasicValue builtin_sfx_load(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line, "SFX.LOAD requires 2 arguments: id, filepath$"); return false; }
    int id = static_cast<int>(to_double(args[0]));
    std::string path = to_string(args[1]);
    if (!vm.sound_system.load_sound(id, path)) {
        Error::set(1, vm.runtime_current_line, "Failed to load sound effect.");
    }
    return false;
}

// SFX.PLAY id
BasicValue builtin_sfx_play(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line, "SFX.PLAY requires 1 argument: id"); return false; }
    int id = static_cast<int>(to_double(args[0]));
    vm.sound_system.play_sound(id);
    return false;
}

// MUSIC.PLAY music_id, [looping_bool]
BasicValue builtin_music_play(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 1 || args.size() > 2) {
        Error::set(8, vm.runtime_current_line, "MUSIC.PLAY requires 1 or 2 arguments: sound_id, [looping_bool]");
        return false;
    }
    int id = static_cast<int>(to_double(args[0]));
    bool loop = true; // Default to looping for music
    if (args.size() == 2) {
        loop = to_bool(args[1]);
    }
    vm.sound_system.play_music(id, loop);
    return false;
}

// MUSIC.STOP
BasicValue builtin_music_stop(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) { Error::set(8, vm.runtime_current_line, "MUSIC.STOP takes no arguments."); return false; }
    vm.sound_system.stop_music();
    return false;
}

// MOUSEX() -> returns the current X coordinate of the mouse
BasicValue builtin_mousex(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    if (!vm.graphics_system.is_initialized) {
        return 0.0; // Return 0 if graphics are not active
    }
    return static_cast<double>(vm.graphics_system.get_mouse_x());
}

// MOUSEY() -> returns the current Y coordinate of the mouse
BasicValue builtin_mousey(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    if (!vm.graphics_system.is_initialized) {
        return 0.0;
    }
    return static_cast<double>(vm.graphics_system.get_mouse_y());
}

// MOUSEB(button_index) -> returns TRUE or FALSE if the button is pressed
// 1 = Left, 2 = Middle, 3 = Right
BasicValue builtin_mouseb(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    if (!vm.graphics_system.is_initialized) {
        return false;
    }
    int button_index = static_cast<int>(to_double(args[0]));
    return vm.graphics_system.get_mouse_button_state(button_index);
}

// --- SPRITE PROCEDURES & FUNCTIONS ---
#if !defined(__EMSCRIPTEN__) // SDL3 is still experimental no LOADTEXTURE support
// SPRITE.LOAD type_id, "filename.png"
BasicValue builtin_sprite_load(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line);
        return false;
    }
    int type_id = static_cast<int>(to_double(args[0]));
    std::string filename = to_string(args[1]);

    // The sprite system is a member of the graphics system
    if (!vm.graphics_system.sprite_system.load_sprite_type(type_id, filename)) {
        // The C++ function already prints a detailed error.
        Error::set(1, vm.runtime_current_line, "Failed to load sprite.");
    }
    return false;
}

// SPRITE.LOAD_ASEPRITE type_id, "filename.json"
// Loads a sprite sheet and animation data from an Aseprite export.
BasicValue builtin_sprite_load_aseprite(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.LOAD_ASEPRITE requires 2 arguments: type_id, filename$.");
        return false;
    }
    int type_id = static_cast<int>(to_double(args[0]));
    std::string filename = to_string(args[1]);

    // The sprite system is now a direct member of the VM
    if (!vm.graphics_system.sprite_system.load_aseprite_file(type_id, filename)) {
        Error::set(12, vm.runtime_current_line, "Failed to load Aseprite file. Check path and JSON format.");
    }
    return false; // Procedure
}

// SPRITE.CREATE(type_id, x, y) -> instance_id
// Creates an instance of a loaded sprite type.
BasicValue builtin_sprite_create(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "SPRITE.CREATE requires 3 arguments: type_id, x, y.");
        return -1.0; // Return -1 on error
    }
    int type_id = static_cast<int>(to_double(args[0]));
    float x = static_cast<float>(to_double(args[1]));
    float y = static_cast<float>(to_double(args[2]));

    int instance_id = vm.graphics_system.sprite_system.create_sprite(type_id, x, y);
    if (instance_id == -1) {
        Error::set(1, vm.runtime_current_line, "Failed to create sprite. Ensure the type_id has been loaded.");
    }
    return static_cast<double>(instance_id);
}

// SPRITE.SET_ANIMATION instance_id, "animation_name$"
// Sets the current animation for a sprite instance.
BasicValue builtin_sprite_set_animation(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.SET_ANIMATION requires 2 arguments: instance_id, animation_name$.");
        return false;
    }
    int instance_id = static_cast<int>(to_double(args[0]));
    std::string anim_name = to_string(args[1]);
    vm.graphics_system.sprite_system.set_animation(instance_id, anim_name);
    return false; // Procedure
}

// SPRITE.SET_FLIP instance_id, flip_boolean
// Sets the horizontal flip state of a sprite.
BasicValue builtin_sprite_set_flip(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.SET_FLIP requires 2 arguments: instance_id, flip_boolean.");
        return false;
    }
    int instance_id = static_cast<int>(to_double(args[0]));
    bool flip = to_bool(args[1]);
    vm.graphics_system.sprite_system.set_flip(instance_id, flip);
    return false; // Procedure
}

// SPRITE.MOVE instance_id, x, y
BasicValue builtin_sprite_move(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    int instance_id = static_cast<int>(to_double(args[0]));
    float x = static_cast<float>(to_double(args[1]));
    float y = static_cast<float>(to_double(args[2]));
    vm.graphics_system.sprite_system.move_sprite(instance_id, x, y);
    return false;
}

// SPRITE.SET_VELOCITY instance_id, vx, vy
BasicValue builtin_sprite_set_velocity(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    int instance_id = static_cast<int>(to_double(args[0]));
    float vx = static_cast<float>(to_double(args[1]));
    float vy = static_cast<float>(to_double(args[2]));
    vm.graphics_system.sprite_system.set_velocity(instance_id, vx, vy);
    return false;
}

// SPRITE.DELETE instance_id
BasicValue builtin_sprite_delete(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return false; }
    int instance_id = static_cast<int>(to_double(args[0]));
    vm.graphics_system.sprite_system.delete_sprite(instance_id);
    return false;
}

// SPRITE.UPDATE [delta_time]
// Updates all sprites. Now accepts an optional delta_time for frame-rate independent physics.
BasicValue builtin_sprite_update(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() > 1) {
        Error::set(8, vm.runtime_current_line, "SPRITE.UPDATE accepts 0 or 1 argument: [delta_time].");
        return false;
    }
    float dt = 1.0f / 60.0f; // Default to 60 FPS if no delta is provided
    if (args.size() == 1) {
        dt = static_cast<float>(to_double(args[0]));
    }
    vm.graphics_system.sprite_system.update(dt);
    return false; // Procedure
}

// SPRITE.DRAW_ALL [cam_x], [cam_y]
BasicValue builtin_sprite_draw_all(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() > 2) { // Allow 0 or 2 arguments
        Error::set(8, vm.runtime_current_line, "SPRITE.DRAW_ALL takes 0 or 2 arguments: [cam_x], [cam_y]");
        return false;
    }
    float cam_x = 0.0f;
    float cam_y = 0.0f;
    if (args.size() == 2) {
        cam_x = static_cast<float>(to_double(args[0]));
        cam_y = static_cast<float>(to_double(args[1]));
    }
    vm.graphics_system.sprite_system.draw_all(cam_x, cam_y);
    return false;
}

// SPRITE.GET_X(instance_id) -> x_coordinate
BasicValue builtin_sprite_get_x(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return 0.0; }
    int instance_id = static_cast<int>(to_double(args[0]));
    return static_cast<double>(vm.graphics_system.sprite_system.get_x(instance_id));
}

// SPRITE.GET_Y(instance_id) -> y_coordinate
BasicValue builtin_sprite_get_y(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 1) { Error::set(8, vm.runtime_current_line); return 0.0; }
    int instance_id = static_cast<int>(to_double(args[0]));
    return static_cast<double>(vm.graphics_system.sprite_system.get_y(instance_id));
}

// SPRITE.COLLISION(id1, id2) -> boolean
BasicValue builtin_sprite_collision(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) { Error::set(8, vm.runtime_current_line); return false; }
    int id1 = static_cast<int>(to_double(args[0]));
    int id2 = static_cast<int>(to_double(args[1]));
    return vm.graphics_system.sprite_system.check_collision(id1, id2);
}

// SPRITE.ADD_TO_GROUP group_id, instance_id
// Adds a sprite to a group.
BasicValue builtin_sprite_add_to_group(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.ADD_TO_GROUP requires 2 arguments: group_id, instance_id.");
        return false;
    }
    int group_id = static_cast<int>(to_double(args[0]));
    int instance_id = static_cast<int>(to_double(args[1]));
    vm.graphics_system.sprite_system.add_to_group(group_id, instance_id);
    return false; // Procedure
}

// SPRITE.COLLISION_GROUP(instance_id, group_id) -> hit_instance_id
// Checks for collision between a single sprite and a group.
BasicValue builtin_sprite_collision_group(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.COLLISION_GROUP requires 2 arguments: instance_id, group_id.");
        return -1.0;
    }
    int instance_id = static_cast<int>(to_double(args[0]));
    int group_id = static_cast<int>(to_double(args[1]));
    int hit_id = vm.graphics_system.sprite_system.check_collision_sprite_group(instance_id, group_id);
    return static_cast<double>(hit_id);
}

// SPRITE.CREATE_GROUP() -> group_id
// Creates a new, empty sprite group.
BasicValue builtin_sprite_create_group(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (!args.empty()) {
        Error::set(8, vm.runtime_current_line, "SPRITE.CREATE_GROUP takes no arguments.");
        return -1.0;
    }
    return static_cast<double>(vm.graphics_system.sprite_system.create_group());
}

// SPRITE.COLLISION_GROUPS(group_id1, group_id2) -> array[hit_id1, hit_id2]
// Checks for collision between two groups of sprites.
BasicValue builtin_sprite_collision_groups(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "SPRITE.COLLISION_GROUPS requires 2 arguments: group_id1, group_id2.");
        return {}; // Return empty BasicValue
    }
    int group_id1 = static_cast<int>(to_double(args[0]));
    int group_id2 = static_cast<int>(to_double(args[1]));
    std::pair<int, int> hit_pair = vm.graphics_system.sprite_system.check_collision_groups(group_id1, group_id2);

    auto result_ptr = std::make_shared<Array>();
    result_ptr->shape = { 2 };
    result_ptr->data.push_back(static_cast<double>(hit_pair.first));
    result_ptr->data.push_back(static_cast<double>(hit_pair.second));
    return result_ptr;
}

// -- - TILEMAP PROCEDURES & FUNCTIONS-- -

// MAP.LOAD "map_name", "filename.json"
// Loads a Tiled map file.
BasicValue builtin_map_load(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "MAP.LOAD requires 2 arguments: map_name$, filename$.");
        return false;
    }
    std::string map_name = to_string(args[0]);
    std::string filename = to_string(args[1]);
    if (!vm.graphics_system.tilemap_system.load_map(map_name, filename)) {
        Error::set(12, vm.runtime_current_line, "Failed to load Tiled map file.");
    }
    return false; // Procedure
}

// MAP.DRAW_LAYER "map_name", "layer_name", [world_offset_x], [world_offset_y]
// Draws a specific tile layer from a loaded map.
BasicValue builtin_map_draw_layer(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() < 2 || args.size() > 4) {
        Error::set(8, vm.runtime_current_line, "MAP.DRAW_LAYER requires 2 to 4 arguments: map_name$, layer_name$, [offsetX], [offsetY].");
        return false;
    }
    std::string map_name = to_string(args[0]);
    std::string layer_name = to_string(args[1]);
    int offset_x = 0;
    int offset_y = 0;
    if (args.size() >= 3) {
        offset_x = static_cast<int>(to_double(args[2]));
    }
    if (args.size() == 4) {
        offset_y = static_cast<int>(to_double(args[3]));
    }
    vm.graphics_system.tilemap_system.draw_layer(map_name, layer_name, offset_x, offset_y);
    return false; // Procedure
}

// MAP.GET_OBJECTS("map_name", "object_type") -> Array of Maps
// Retrieves all objects of a certain type from an object layer.
BasicValue builtin_map_get_objects(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 2) {
        Error::set(8, vm.runtime_current_line, "MAP.GET_OBJECTS requires 2 arguments: map_name$, object_type$.");
        return {};
    }
    std::string map_name = to_string(args[0]);
    std::string object_type = to_string(args[1]);

    auto objects_data = vm.graphics_system.tilemap_system.get_objects_by_type(map_name, object_type);

    auto results_array = std::make_shared<Array>();
    results_array->shape = { objects_data.size() };

    for (const auto& obj_map : objects_data) {
        auto map_ptr = std::make_shared<Map>();
        for (const auto& pair : obj_map) {
            // Tiled properties can be bool, float, or string. We'll try to parse intelligently.
            // For now, we just treat them all as strings for simplicity.
            map_ptr->data[pair.first] = pair.second;
        }
        results_array->data.push_back(map_ptr);
    }

    return results_array;
}

// MAP.COLLIDES(sprite_id, "map_name", "layer_name") -> boolean
// Checks if a sprite is colliding with any solid tile on a given layer.
BasicValue builtin_map_collides(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) {
        Error::set(8, vm.runtime_current_line, "MAP.COLLIDES requires 3 arguments: sprite_id, map_name$, layer_name$.");
        return false;
    }
    int sprite_id = static_cast<int>(to_double(args[0]));
    std::string map_name = to_string(args[1]);
    std::string layer_name = to_string(args[2]);

    return vm.graphics_system.tilemap_system.check_sprite_collision(sprite_id, vm.graphics_system.sprite_system, map_name, layer_name);
}

BasicValue builtin_map_get_tile_id(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 4) {
        Error::set(8, vm.runtime_current_line);
        return 0.0;
    }
    std::string map_name = to_string(args[0]);
    std::string layer_name = to_string(args[1]);
    int tile_x = static_cast<int>(to_double(args[2]));
    int tile_y = static_cast<int>(to_double(args[3]));

    int tile_id = vm.graphics_system.tilemap_system.get_tile_id(map_name, layer_name, tile_x, tile_y);
    return static_cast<double>(tile_id);
}

// MAP.DRAW_DEBUG_COLLISIONS player_id, "map", "layer"
BasicValue builtin_map_draw_debug(NeReLaBasic& vm, const std::vector<BasicValue>& args) {
    if (args.size() != 3) { Error::set(8, vm.runtime_current_line); return false; }
    int sprite_id = static_cast<int>(to_double(args[0]));
    std::string map_name = to_string(args[1]);
    std::string layer_name = to_string(args[2]);

    // We need the camera values, which are in BASIC variables.
    // This is a simple way to get them from the VM.
    float cam_x = to_double(vm.variables["CAM_X"]);
    float cam_y = to_double(vm.variables["CAM_Y"]);

    vm.graphics_system.tilemap_system.draw_debug_collisions(
        sprite_id, vm.graphics_system.sprite_system, map_name, layer_name, cam_x, cam_y
    );
    return false;
}
#endif // EMSCRIPTEN

void register_sdl_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate) {
    // Helper lambda to make registration cleaner
    auto register_func = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        table_to_populate[to_upper(info.name)] = info;
        };
    // --- Register Procedures ---
    auto register_proc = [&](const std::string& name, int arity, NeReLaBasic::NativeFunction func_ptr) {
        NeReLaBasic::FunctionInfo info;
        info.name = name;
        info.arity = arity;
        info.native_impl = func_ptr;
        info.is_procedure = true; // Mark this as a procedure
        table_to_populate[to_upper(info.name)] = info;
        };

    register_proc("SCREEN", -1, builtin_screen);
    register_proc("SCREENFLIP", 0, builtin_screenflip);
    register_func("SCREENWIDTH", 0, builtin_screenwidth);
    register_func("SCREENHEIGHT", 0, builtin_screenheight);
    register_proc("TOGGLE_FULLSCREEN", 0, builtin_toggle_fullscreen);
    register_proc("DRAWCOLOR", -1, builtin_drawcolor);
    register_proc("SETFONT", 2, builtin_setfont);

    register_proc("PSET", -1, builtin_pset);
    register_proc("LINE", -1, builtin_line);
    register_proc("RECT", -1, builtin_rect);
    register_proc("CIRCLE", -1, builtin_circle);
    register_proc("ELLIPSE", -1, builtin_ellipse);
    register_proc("ROUNDED_RECT", -1, builtin_rounded_rect);
    register_proc("CIRCLE_SECTOR", -1, builtin_circle_sector);
    register_proc("TEXT", -1, builtin_text);
    register_proc("PLOTRAW", -1, builtin_plotraw);

    register_proc("TURTLE.FORWARD", 1, builtin_turtle_forward);
    register_proc("TURTLE.BACKWARD", 1, builtin_turtle_backward);
    register_proc("TURTLE.LEFT", 1, builtin_turtle_left);
    register_proc("TURTLE.RIGHT", 1, builtin_turtle_right);
    register_proc("TURTLE.PENUP", 0, builtin_turtle_penup);
    register_proc("TURTLE.PENDOWN", 0, builtin_turtle_pendown);
    register_proc("TURTLE.SETPOS", 2, builtin_turtle_setpos);
    register_proc("TURTLE.SETHEADING", 1, builtin_turtle_setheading);
    register_proc("TURTLE.HOME", 0, builtin_turtle_home);
    register_proc("TURTLE.SET_COLOR", 3, builtin_turtle_set_color);
    register_proc("TURTLE.DRAW", 0, builtin_turtle_draw);
    register_proc("TURTLE.CLEAR", 0, builtin_turtle_clear);

    register_proc("SOUND.INIT", 0, builtin_sound_init);
    register_proc("SOUND.VOICE", 6, builtin_sound_voice);
    register_proc("SOUND.PLAY", 2, builtin_sound_play);
    register_proc("SOUND.RELEASE", 1, builtin_sound_release);
    register_proc("SOUND.STOP", 1, builtin_sound_stop);

    register_proc("SFX.LOAD", 2, builtin_sfx_load);
    register_proc("SFX.PLAY", 1, builtin_sfx_play);
    register_proc("MUSIC.PLAY", -1, builtin_music_play);
    register_proc("MUSIC.STOP", 0, builtin_music_stop);

    register_func("MOUSEX", 0, builtin_mousex);
    register_func("MOUSEY", 0, builtin_mousey);
    register_func("MOUSEB", 1, builtin_mouseb);

    // Sprite Procedures
#if !defined(__EMSCRIPTEN__) // SDL3 is still experimental no LOADTEXTURE support   
    register_proc("SPRITE.LOAD", 2, builtin_sprite_load);
    register_proc("SPRITE.LOAD_ASEPRITE", 2, builtin_sprite_load_aseprite);
    register_proc("SPRITE.MOVE", 3, builtin_sprite_move);
    register_proc("SPRITE.SET_VELOCITY", 3, builtin_sprite_set_velocity);
    register_proc("SPRITE.DELETE", 1, builtin_sprite_delete);
    register_proc("SPRITE.UPDATE", -1, builtin_sprite_update); // Now has optional arg
    register_proc("SPRITE.DRAW_ALL", 2, builtin_sprite_draw_all);
    register_proc("SPRITE.SET_ANIMATION", 2, builtin_sprite_set_animation);
    register_proc("SPRITE.SET_FLIP", 2, builtin_sprite_set_flip);
    register_proc("SPRITE.ADD_TO_GROUP", 2, builtin_sprite_add_to_group);
    register_func("SPRITE.CREATE", 3, builtin_sprite_create);
    register_func("SPRITE.GET_X", 1, builtin_sprite_get_x);
    register_func("SPRITE.GET_Y", 1, builtin_sprite_get_y);
    register_func("SPRITE.COLLISION", 2, builtin_sprite_collision);
    register_func("SPRITE.CREATE_GROUP", 0, builtin_sprite_create_group);
    register_func("SPRITE.COLLISION_GROUP", 2, builtin_sprite_collision_group);
    register_func("SPRITE.COLLISION_GROUPS", 2, builtin_sprite_collision_groups);

    // --- Add New TileMap Functions ---
    register_proc("TILEMAP.LOAD", 2, builtin_map_load);
    register_proc("TILEMAP.DRAW_LAYER", -1, builtin_map_draw_layer); // Optional args
    register_func("TILEMAP.GET_OBJECTS", 2, builtin_map_get_objects);
    register_func("TILEMAP.COLLIDES", 3, builtin_map_collides);
    register_func("TILEMAP.GET_TILE_ID", 4, builtin_map_get_tile_id);
    register_proc("TILEMAP.DRAW_DEBUG_COLLISIONS", 3, builtin_map_draw_debug);
#endif
    
}
#endif