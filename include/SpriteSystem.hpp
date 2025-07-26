#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm> // For std::find and std::erase

// Represents a single animation sequence.
struct Animation {
    std::string name;
    std::vector<SDL_Texture*> frames; // Direct pointers to textures for this animation
    float frame_duration = 0.1f;      // Time in seconds for each frame
};

// Represents a sprite type, loaded from a file (e.g., an Aseprite JSON).
struct SpriteType {
    int id = -1;
    std::map<std::string, Animation> animations; // "idle", "walk", "attack"
};

// Represents a single active sprite instance on the screen.
struct Sprite {
    int type_id = -1;
    int instance_id = -1;
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    SDL_FRect rect;
    bool is_active = true;

    // Animation state
    std::string current_animation_name;
    int current_frame = 0;
    float frame_timer = 0.0f;
    bool is_flipped = false; // For horizontal flipping
};

class SpriteSystem {
public:
    SpriteSystem();
    ~SpriteSystem();

    void init(SDL_Renderer* renderer);
    void shutdown();


    bool load_sprite_type(int type_id, const std::string& filename);

    // --- Aseprite Loading ---
    bool load_aseprite_file(int type_id, const std::string& json_filename);

    // --- Sprite Creation ---
    int create_sprite(int type_id, float x, float y);

    // --- Sprite Manipulation ---
    void move_sprite(int instance_id, float x, float y);
    void set_velocity(int instance_id, float vx, float vy);
    void delete_sprite(int instance_id);
    void set_animation(int instance_id, const std::string& animation_name);
    void set_flip(int instance_id, bool flip);
    float get_x(int instance_id) const;
    float get_y(int instance_id) const;
    const SDL_FRect* get_sprite_rect(int instance_id) const;
    SDL_FRect get_collision_rect(int instance_id) const;

    // --- Grouping ---
    int create_group();
    void add_to_group(int group_id, int instance_id);
    void remove_from_group(int group_id, int instance_id);
    const std::vector<int>& get_sprites_in_group(int group_id);

    // --- Core Engine Functions ---
    void update(float delta_time);
    void draw_all(float cam_x = 0.0f, float cam_y = 0.0f);

    // --- Collision Detection ---
    bool check_collision(int instance_id1, int instance_id2);
    int check_collision_sprite_group(int instance_id, int group_id);
    std::pair<int, int> check_collision_groups(int group_id1, int group_id2);

private:
    SDL_Renderer* renderer = nullptr;

    // Atlas now stores individual frame textures, managed internally.
    std::vector<SDL_Texture*> texture_atlas;

    // Maps a type ID to a fully defined SpriteType (with animations)
    std::map<int, SpriteType> sprite_types;

    // Use a map of unique_ptr for better memory management of sprites
    std::map<int, std::unique_ptr<Sprite>> active_sprites;

    // --- Grouping Data ---
    std::map<int, std::vector<int>> sprite_groups;
    int next_group_id = 0;
    int next_instance_id = 0;
};
#endif