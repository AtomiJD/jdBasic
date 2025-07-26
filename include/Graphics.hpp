#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <vector>
#include <deque>
#include "Types.hpp"
#include "SpriteSystem.hpp" 
#include "TileMapSystem.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


class NeReLaBasic;

struct TurtleLine {
    float x1, y1, x2, y2;
    SDL_Color color;
};

class Graphics {
public:
    Graphics();
    ~Graphics();

    bool init(const std::string& title, int width, int height, float scale = 1.0f);
    void shutdown();
    bool load_font(const std::string& font_path, int font_size);

    void update_screen(); // Shows whatever has been drawn
    void clear_screen();  // Clears the screen to the default color
    void clear_screen(Uint8 r, Uint8 g, Uint8 b); 

    void setDrawColor(Uint8 r, Uint8 g, Uint8 b);

    // Scalar drawing functions (updated)
    void pset(int x, int y);
    void line(int x1, int y1, int x2, int y2);
    void rect(int x, int y, int w, int h, bool is_filled);
    void circle(int center_x, int center_y, int radius);

    // Overloads for scalar drawing with explicit color
    void pset(int x, int y, Uint8 r, Uint8 g, Uint8 b);
    void line(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b);
    void rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, bool is_filled);
    void circle(int center_x, int center_y, int radius, Uint8 r, Uint8 g, Uint8 b);

    // New vectorized drawing functions
    void pset(const std::shared_ptr<Array>& points, const std::shared_ptr<Array>& colors);
    void line(const std::shared_ptr<Array>& lines, const std::shared_ptr<Array>& colors);
    void rect(const std::shared_ptr<Array>& rects, bool is_filled, const std::shared_ptr<Array>& colors);
    void circle(const std::shared_ptr<Array>& circles, const std::shared_ptr<Array>& colors);

    void text(int x, int y, const std::string& text_to_draw, Uint8 r, Uint8 g, Uint8 b);
    void plot_raw(int start_x, int start_y, const std::shared_ptr<Array>& color_matrix, float scale = 1.0f, float scaleY = 1.0f);

    int get_mouse_x() const;
    int get_mouse_y() const;
    bool get_mouse_button_state(int button) const;

    bool handle_events(NeReLaBasic& vm);
    bool should_quit();   

    std::string get_key_from_buffer();

    void turtle_home(int screen_width, int screen_height);
    void turtle_forward(float distance);
    void turtle_backward(float distance);
    void turtle_left(float degrees);
    void turtle_right(float degrees);
    void turtle_penup();
    void turtle_pendown();
    void turtle_setpos(float x, float y);
    void turtle_setheading(float degrees);
    void turtle_set_color(Uint8 r, Uint8 g, Uint8 b);
    void turtle_draw_path();
    void turtle_clear_path();

    bool is_initialized = false;
    bool quit_event_received = false;

    SpriteSystem sprite_system;
    TileMapSystem tilemap_system;

    SDL_Renderer* renderer = nullptr;

private:
    SDL_Window* window = nullptr;
    
    TTF_Font* font = nullptr;
    std::deque<char> key_buffer;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    Uint32 mouse_button_state = 0;
    SDL_Color draw_color = { 255, 255, 255, 255 }; // Default to white

    // TURTLE STATE VARIABLES 
    float turtle_x = 0.0f;
    float turtle_y = 0.0f;
    float turtle_angle = 0.0f; // 0 degrees = facing right
    bool pen_down = true;
    std::vector<TurtleLine> turtle_path;
    SDL_Color pen_color = { 255, 255, 255, 255 }; // Default to white
};
#endif