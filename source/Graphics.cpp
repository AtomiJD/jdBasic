#ifdef SDL3
#include "Graphics.hpp"
#include "TextIO.hpp"
#include "NeReLaBasic.hpp"
#include <vector>

Graphics::Graphics() {}

Graphics::~Graphics() {
    shutdown();
}

bool Graphics::init(const std::string& title, int width, int height) {
    if (is_initialized) {
        shutdown(); // Close existing window if any
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        TextIO::print("SDL could not initialize! SDL_Error: " + std::string(SDL_GetError()) + "\n");
        return false;
    }

    if (TTF_Init() == -1) {
        TextIO::print("SDL_ttf could not initialize! SDL_Error: " + std::string(SDL_GetError()) + "\n");
        SDL_Quit();
        return false;
    }

    bool success = SDL_CreateWindowAndRenderer(title.c_str(), width, height, 0, &window, &renderer); //SDL_WINDOW_FULLSCREEN removed
    if (!success) {
        TextIO::print("Window could not be created! SDL_Error: " + std::string(SDL_GetError()) + "\n");
        return false;
    }

    // Initialize the sprite system and pass it our renderer
    sprite_system.init(renderer);

    font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 24);
    if (!font) {
        // Use TextIO for consistency, but also cerr for visibility during debugging
        std::cerr << "Failed to load font! SDL_Error: " << SDL_GetError() << std::endl;
        TextIO::print("WARNING: Failed to load font. TEXT command will not work.\n");
    }

    SDL_StartTextInput(window);

    is_initialized = true;
    clear_screen();
    update_screen();
    return true;
}

void Graphics::shutdown() {
    if (!is_initialized) return;

    SDL_StopTextInput(window);

    // Shut down the sprite system first to free its textures
    sprite_system.shutdown();

    if (font) {
        TTF_CloseFont(font);
        font = nullptr;
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    TTF_Quit();
    SDL_Quit();
    is_initialized = false;
}

void Graphics::clear_screen() {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black
    SDL_RenderClear(renderer);
}

void Graphics::update_screen() {
    if (!renderer) return;
    SDL_RenderPresent(renderer);
}

bool Graphics::handle_events(NeReLaBasic& vm) {
    if (!is_initialized) return true;

    SDL_Event event;
    // Poll for all pending events
    while (SDL_PollEvent(&event) != 0) {
        // Check if the event is the user trying to close the window
        switch (event.type) {
            case SDL_EVENT_QUIT:
                quit_event_received = true;
                vm.raise_event("QUIT", true);
                break;


            case SDL_EVENT_TEXT_INPUT: {
                // event.text.text is a null-terminated string
                // For INKEY$, we typically just care about the first character.
                if (event.text.text[0] != '\0') {
                    key_buffer.push_back(event.text.text[0]);
                    if (key_buffer.size() > 80) // If there is no inkey$ we need to clear the buffer from time to time
                        key_buffer.clear();
                }
                auto text_data = std::make_shared<Map>();
                text_data->data["text"] = std::string(event.text.text);
                vm.raise_event("KEYPRESS", text_data);
                break;
            }
            // --- Capture keydown for non-text keys like ESC ---
            case SDL_EVENT_KEY_DOWN: {
                // The value 27 is the ASCII code for the Escape key.
                if (event.key.key == SDLK_ESCAPE) {
                    key_buffer.push_back(27);
                }
                auto key_data = std::make_shared<Map>();
                key_data->data["scancode"] = static_cast<double>(event.key.scancode);
                key_data->data["keycode"] = static_cast<double>(event.key.key);
                key_data->data["repeat"] = static_cast<bool>(event.key.repeat);
                vm.raise_event("KEYDOWN", key_data);
                break;
            }
                // --- Handle mouse events ---
            case SDL_EVENT_MOUSE_MOTION: {
                auto mouse_data = std::make_shared<Map>();
                mouse_data->data["x"] = static_cast<double>(event.motion.x);
                mouse_data->data["y"] = static_cast<double>(event.motion.y);
                mouse_data->data["xrel"] = static_cast<double>(event.motion.xrel);
                mouse_data->data["yrel"] = static_cast<double>(event.motion.yrel);
                vm.raise_event("MOUSEMOVE", mouse_data);
                mouse_x = event.motion.x;
                mouse_y = event.motion.y;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                mouse_x = event.button.x;
                mouse_y = event.button.y;
                auto mouse_data = std::make_shared<Map>();
                mouse_data->data["button"] = static_cast<double>(event.button.button);
                mouse_data->data["x"] = static_cast<double>(event.button.x);
                mouse_data->data["y"] = static_cast<double>(event.button.y);
                mouse_data->data["clicks"] = static_cast<double>(event.button.clicks);
                vm.raise_event("MOUSECLICK", mouse_data);
                mouse_button_state = SDL_GetMouseState(NULL, NULL);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                mouse_x = event.button.x;
                mouse_y = event.button.y;
                auto mouse_data = std::make_shared<Map>();
                mouse_data->data["button"] = static_cast<double>(event.button.button);
                mouse_data->data["x"] = static_cast<double>(event.button.x);
                mouse_data->data["y"] = static_cast<double>(event.button.y);
                vm.raise_event("MOUSEUP", mouse_data);
                mouse_button_state = SDL_GetMouseState(NULL, NULL);
                break;
            }
        }
    }
    return !quit_event_received;
}

std::string Graphics::get_key_from_buffer() {
    if (!key_buffer.empty()) {
        char c = key_buffer.front();
        key_buffer.pop_front();
        return std::string(1, c);
    }
    return ""; // Return empty string if no key is waiting
}

int Graphics::get_mouse_x() const {
    return static_cast<int>(mouse_x);
}

int Graphics::get_mouse_y() const {
    return static_cast<int>(mouse_y);
}

bool Graphics::get_mouse_button_state(int button) const {
    if (button < 1 || button > 3) return false;
    return (mouse_button_state & SDL_BUTTON_MASK(button));
}

bool Graphics::should_quit() {
    return quit_event_received;
}

void Graphics::clear_screen(Uint8 r, Uint8 g, Uint8 b) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, r, g, b, 255); // Use specified color
    SDL_RenderClear(renderer);
}

void Graphics::text(int x, int y, const std::string& text_to_draw, Uint8 r, Uint8 g, Uint8 b) {
    if (!renderer || !font) {
        // Don't try to draw if the system isn't ready or the font failed to load
        return;
    }

    SDL_Color color = { r, g, b, 255 };

    // Create a surface from the text using the loaded font
    SDL_Surface* text_surface = TTF_RenderText_Blended(font, text_to_draw.c_str(), 0, color);
    if (!text_surface) {
        std::cerr << "Unable to render text surface! SDL_Error: " << SDL_GetError() << std::endl;
        return;
    }

    // Create a texture from the surface
    SDL_Texture* text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
    if (!text_texture) {
        std::cerr << "Unable to create texture from rendered text! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroySurface(text_surface);
        return;
    }

    // Define the destination rectangle for the text
    SDL_FRect dest_rect = { (float)x, (float)y, (float)text_surface->w, (float)text_surface->h };

    // Copy the texture to the renderer at the specified position
    SDL_RenderTexture(renderer, text_texture, nullptr, &dest_rect);

    // Clean up the temporary resources
    SDL_DestroyTexture(text_texture);
    SDL_DestroySurface(text_surface);
}

void Graphics::setDrawColor(Uint8 r, Uint8 g, Uint8 b) {
    if (!renderer) return;
    draw_color = { r, g, b, 255 };
}

// --- PSET ---
void Graphics::pset(int x, int y) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, 255);
    SDL_RenderPoint(renderer, (float)x, (float)y);
}

void Graphics::pset(int x, int y, Uint8 r, Uint8 g, Uint8 b) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderPoint(renderer, (float)x, (float)y);
}

// Vectorized PSET
void Graphics::pset(const std::shared_ptr<Array>& points, const std::shared_ptr<Array>& colors) {
    if (!renderer || !points || points->shape.size() != 2 || points->shape[1] < 2) return;

    size_t num_items = points->shape[0];
    size_t point_stride = points->shape[1];
    bool has_colors = colors && colors->shape.size() == 2 && colors->shape[0] == num_items && colors->shape[1] >= 3;
    size_t color_stride = has_colors ? colors->shape[1] : 0;

    if (!has_colors) {
        SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
    }

    for (size_t i = 0; i < num_items; ++i) {
        float x = static_cast<float>(std::get<double>(points->data[i * point_stride + 0]));
        float y = static_cast<float>(std::get<double>(points->data[i * point_stride + 1]));

        if (has_colors) {
            Uint8 r = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 0]));
            Uint8 g = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 1]));
            Uint8 b = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 2]));
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        }
        SDL_RenderPoint(renderer, x, y);
    }
}


// --- LINE ---
void Graphics::line(int x1, int y1, int x2, int y2) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, 255);
    SDL_RenderLine(renderer, (float)x1, (float)y1, (float)x2, (float)y2);
}

void Graphics::line(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderLine(renderer, (float)x1, (float)y1, (float)x2, (float)y2);
}

// Vectorized LINE
void Graphics::line(const std::shared_ptr<Array>& lines, const std::shared_ptr<Array>& colors) {
    if (!renderer || !lines || lines->shape.size() != 2 || lines->shape[1] < 4) return;

    size_t num_items = lines->shape[0];
    size_t line_stride = lines->shape[1];
    bool has_colors = colors && colors->shape.size() == 2 && colors->shape[0] == num_items && colors->shape[1] >= 3;
    size_t color_stride = has_colors ? colors->shape[1] : 0;

    if (!has_colors) {
        SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
    }

    for (size_t i = 0; i < num_items; ++i) {
        float x1 = static_cast<float>(std::get<double>(lines->data[i * line_stride + 0]));
        float y1 = static_cast<float>(std::get<double>(lines->data[i * line_stride + 1]));
        float x2 = static_cast<float>(std::get<double>(lines->data[i * line_stride + 2]));
        float y2 = static_cast<float>(std::get<double>(lines->data[i * line_stride + 3]));

        if (has_colors) {
            Uint8 r = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 0]));
            Uint8 g = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 1]));
            Uint8 b = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 2]));
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        }
        SDL_RenderLine(renderer, x1, y1, x2, y2);
    }
}


// --- RECT ---
void Graphics::rect(int x, int y, int w, int h, bool is_filled) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, 255);
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
    if (is_filled) {
        SDL_RenderFillRect(renderer, &rect);
    }
    else {
        SDL_RenderRect(renderer, &rect);
    }
}

void Graphics::rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, bool is_filled) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
    if (is_filled) {
        SDL_RenderFillRect(renderer, &rect);
    }
    else {
        SDL_RenderRect(renderer, &rect);
    }
}

// Vectorized RECT
void Graphics::rect(const std::shared_ptr<Array>& rects, bool is_filled, const std::shared_ptr<Array>& colors) {
    if (!renderer || !rects || rects->shape.size() != 2 || rects->shape[1] < 4) return;

    size_t num_items = rects->shape[0];
    size_t rect_stride = rects->shape[1];
    bool has_colors = colors && colors->shape.size() == 2 && colors->shape[0] == num_items && colors->shape[1] >= 3;
    size_t color_stride = has_colors ? colors->shape[1] : 0;

    if (!has_colors) {
        SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
    }

    std::vector<SDL_FRect> sdl_rects;
    if (!has_colors) {
        sdl_rects.resize(num_items);
    }

    for (size_t i = 0; i < num_items; ++i) {
        SDL_FRect current_rect = {
            static_cast<float>(std::get<double>(rects->data[i * rect_stride + 0])),
            static_cast<float>(std::get<double>(rects->data[i * rect_stride + 1])),
            static_cast<float>(std::get<double>(rects->data[i * rect_stride + 2])),
            static_cast<float>(std::get<double>(rects->data[i * rect_stride + 3]))
        };

        if (has_colors) {
            Uint8 r = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 0]));
            Uint8 g = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 1]));
            Uint8 b = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 2]));
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            if (is_filled) SDL_RenderFillRect(renderer, &current_rect);
            else SDL_RenderRect(renderer, &current_rect);
        }
        else {
            sdl_rects[i] = current_rect;
        }
    }

    if (!has_colors && num_items > 0) {
        if (is_filled) SDL_RenderFillRects(renderer, sdl_rects.data(), num_items);
        else SDL_RenderRects(renderer, sdl_rects.data(), num_items);
    }
}


// --- CIRCLE ---
void Graphics::circle(int center_x, int center_y, int radius) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, 255);
    for (int i = 0; i < 360; ++i) {
        float angle = i * 3.14159f / 180.0f;
        float x = center_x + radius * cos(angle);
        float y = center_y + radius * sin(angle);
        SDL_RenderPoint(renderer, x, y);
    }
}

void Graphics::circle(int center_x, int center_y, int radius, Uint8 r, Uint8 g, Uint8 b) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    for (int i = 0; i < 360; ++i) {
        float angle = i * 3.14159f / 180.0f;
        float x = center_x + radius * cos(angle);
        float y = center_y + radius * sin(angle);
        SDL_RenderPoint(renderer, x, y);
    }
}

// Vectorized CIRCLE
void Graphics::circle(const std::shared_ptr<Array>& circles, const std::shared_ptr<Array>& colors) {
    if (!renderer || !circles || circles->shape.size() != 2 || circles->shape[1] < 3) return;

    size_t num_items = circles->shape[0];
    size_t circle_stride = circles->shape[1];
    bool has_colors = colors && colors->shape.size() == 2 && colors->shape[0] == num_items && colors->shape[1] >= 3;
    size_t color_stride = has_colors ? colors->shape[1] : 0;

    if (!has_colors) {
        SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
    }

    for (size_t i = 0; i < num_items; ++i) {
        int cx = static_cast<int>(std::get<double>(circles->data[i * circle_stride + 0]));
        int cy = static_cast<int>(std::get<double>(circles->data[i * circle_stride + 1]));
        int rad = static_cast<int>(std::get<double>(circles->data[i * circle_stride + 2]));

        if (has_colors) {
            Uint8 r = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 0]));
            Uint8 g = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 1]));
            Uint8 b = static_cast<Uint8>(std::get<double>(colors->data[i * color_stride + 2]));
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        }

        for (int angle_deg = 0; angle_deg < 360; ++angle_deg) {
            float angle_rad = angle_deg * 3.14159f / 180.0f;
            float x = cx + rad * cos(angle_rad);
            float y = cy + rad * sin(angle_rad);
            SDL_RenderPoint(renderer, x, y);
        }
    }
}

void Graphics::plot_raw(int start_x, int start_y, const std::shared_ptr<Array>& color_matrix, float scaleX, float scaleY) {
    if (!renderer || !color_matrix || color_matrix->shape.size() != 2) {
        // Do nothing if graphics aren't ready, matrix is null, or not 2D
        return;
    }

    size_t height = color_matrix->shape[0];
    size_t width = color_matrix->shape[1];

    // Loop through the matrix data
    if (scaleX > 1 || scaleY > 1) {
        SDL_FRect rect = { (float)0, (float)0, (float)0, (float)0 };
        for (size_t r = 0; r < height; ++r) {
            for (size_t c = 0; c < width; ++c) {
                // Get the packed color value
                uint32_t packed_color = static_cast<uint32_t>(std::get<double>(color_matrix->data[r * width + c]));

                // Unpack the R, G, B components
                Uint8 red = (packed_color >> 16) & 0xFF;
                Uint8 green = (packed_color >> 8) & 0xFF;
                Uint8 blue = packed_color & 0xFF;
                rect = { static_cast<float>(start_x*scaleX + c * scaleX), static_cast<float>(start_y*scaleY + r * scaleX), (float)scaleX, (float)scaleY };
                // Set the draw color and plot the single point
                SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
    else {
        for (size_t r = 0; r < height; ++r) {
            for (size_t c = 0; c < width; ++c) {
                // Get the packed color value
                uint32_t packed_color = static_cast<uint32_t>(std::get<double>(color_matrix->data[r * width + c]));

                // Unpack the R, G, B components
                Uint8 red = (packed_color >> 16) & 0xFF;
                Uint8 green = (packed_color >> 8) & 0xFF;
                Uint8 blue = packed_color & 0xFF;

                // Set the draw color and plot the single point
                SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
                SDL_RenderPoint(renderer, static_cast<float>(start_x + c), static_cast<float>(start_y + r));
            }
        }
    }
}

#endif
