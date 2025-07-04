#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h> // <-- Include the SDL_image header
#include <string>
#include <vector>
#include <map>

// Represents a single active sprite on the screen.
struct Sprite {
    int texture_id = -1;    // The ID of the loaded texture to use.
    float x = 0.0f, y = 0.0f;     // Position on the screen.
    float vx = 0.0f, vy = 0.0f;     // Velocity for automatic movement.
    SDL_FRect rect;         // The sprite's rectangle for drawing and collision.
    bool is_active = true;
};

class SpriteSystem {
public:
    SpriteSystem();
    ~SpriteSystem();

    // Must be called after the main Graphics system is initialized.
    void init(SDL_Renderer* renderer);
    void shutdown();

    // Loads an image from a file and assigns it a type ID.
    // Returns true on success, false on failure.
    bool load_sprite_type(int type_id, const std::string& filename);

    // Creates an instance of a loaded sprite type at a specific position.
    // Returns a unique instance ID for the new sprite.
    int create_sprite(int type_id, float x, float y);

    // --- Sprite Manipulation Functions ---
    void move_sprite(int instance_id, float x, float y);
    void set_velocity(int instance_id, float vx, float vy);
    void delete_sprite(int instance_id);
    float get_x(int instance_id) const;
    float get_y(int instance_id) const;

    // --- Core Engine Functions ---

    // Updates all active sprites based on their velocity.
    void update();

    // Draws all active sprites to the screen.
    void draw_all();

    // Returns true if the rectangles of two sprites are colliding.
    bool check_collision(int instance_id1, int instance_id2);


private:
    SDL_Renderer* renderer = nullptr; // A pointer to the main renderer

    // Stores loaded textures, mapping a type ID to an SDL_Texture.
    std::map<int, SDL_Texture*> texture_atlas;

    // Stores all active sprite instances on the screen.
    std::map<int, Sprite> active_sprites;

    int next_instance_id = 0; // A simple counter to generate unique IDs.
};
#endif
