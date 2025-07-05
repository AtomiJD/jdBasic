#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <vector>
#include <deque>
#include "Types.hpp"
#include "SpriteSystem.hpp" 

class Graphics {
public:
    Graphics();
    ~Graphics();

    bool init(const std::string& title, int width, int height);
    void shutdown();
    void update_screen(); // Shows whatever has been drawn
    void clear_screen();  // Clears the screen to the default color
    void clear_screen(Uint8 r, Uint8 g, Uint8 b); 
    void pset(int x, int y, Uint8 r, Uint8 g, Uint8 b); 
    void line(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b);
    void rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, bool is_filled);
    void circle(int center_x, int center_y, int radius, Uint8 r, Uint8 g, Uint8 b);
    void text(int x, int y, const std::string& text_to_draw, Uint8 r, Uint8 g, Uint8 b);
    void plot_raw(int start_x, int start_y, const std::shared_ptr<Array>& color_matrix, float scale = 1.0f, float scaleY = 1.0f);

    int get_mouse_x() const;
    int get_mouse_y() const;
    bool get_mouse_button_state(int button) const;

    bool handle_events(); 
    bool should_quit();   

    std::string get_key_from_buffer();

    bool is_initialized = false;
    bool quit_event_received = false;

    SpriteSystem sprite_system;

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    std::deque<char> key_buffer;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    Uint32 mouse_button_state = 0;
};
#endif