#ifdef SDL3
#include "SpriteSystem.hpp"
#include "TextIO.hpp" // For error printing

SpriteSystem::SpriteSystem() {}

SpriteSystem::~SpriteSystem() {
    shutdown();
}

void SpriteSystem::init(SDL_Renderer* main_renderer) {
    // Store the pointer to the main graphics renderer
    this->renderer = main_renderer;
    // In SDL3, you don't need to call IMG_Init. It's handled automatically.
}

void SpriteSystem::shutdown() {
    // Free all loaded textures to prevent memory leaks
    for (auto const& [id, texture] : texture_atlas) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    texture_atlas.clear();
    active_sprites.clear();
    // In SDL3, IMG_Quit is also not necessary.
}

bool SpriteSystem::load_sprite_type(int type_id, const std::string& filename) {
    if (!renderer) {
        return false;
    }

    if (texture_atlas.count(type_id) && texture_atlas[type_id] != nullptr) {
        SDL_DestroyTexture(texture_atlas[type_id]);
    }

    SDL_Texture* new_texture = IMG_LoadTexture(renderer, filename.c_str());

    if (!new_texture) {
        // --- UPDATED: Use SDL_GetError() for all SDL-related libraries in version 3 ---
        TextIO::print("Failed to load texture '" + filename + "': " + std::string(SDL_GetError()) + "\n");
        texture_atlas[type_id] = nullptr;
        return false;
    }

    texture_atlas[type_id] = new_texture;
    return true;
}

int SpriteSystem::create_sprite(int type_id, float x, float y) {
    if (texture_atlas.find(type_id) == texture_atlas.end() || texture_atlas[type_id] == nullptr) {
        return -1;
    }

    Sprite new_sprite;
    new_sprite.texture_id = type_id;
    new_sprite.x = x;
    new_sprite.y = y;
    new_sprite.is_active = true;

    // --- UPDATED to modern SDL3 function ---
    SDL_GetTextureSize(texture_atlas[type_id], &new_sprite.rect.w, &new_sprite.rect.h);
    new_sprite.rect.x = x;
    new_sprite.rect.y = y;

    int instance_id = next_instance_id++;
    active_sprites[instance_id] = new_sprite;

    return instance_id;
}

void SpriteSystem::move_sprite(int instance_id, float x, float y) {
    if (active_sprites.count(instance_id)) {
        active_sprites[instance_id].x = x;
        active_sprites[instance_id].y = y;
    }
}

void SpriteSystem::set_velocity(int instance_id, float vx, float vy) {
    if (active_sprites.count(instance_id)) {
        active_sprites[instance_id].vx = vx;
        active_sprites[instance_id].vy = vy;
    }
}

void SpriteSystem::delete_sprite(int instance_id) {
    if (active_sprites.count(instance_id)) {
        active_sprites[instance_id].is_active = false;
    }
}

float SpriteSystem::get_x(int instance_id) const {
    if (active_sprites.count(instance_id)) {
        return active_sprites.at(instance_id).x;
    }
    return 0.0f;
}

float SpriteSystem::get_y(int instance_id) const {
    if (active_sprites.count(instance_id)) {
        return active_sprites.at(instance_id).y;
    }
    return 0.0f;
}

void SpriteSystem::update() {
    for (auto& [id, sprite] : active_sprites) {
        if (sprite.is_active) {
            sprite.x += sprite.vx;
            sprite.y += sprite.vy;
            sprite.rect.x = sprite.x;
            sprite.rect.y = sprite.y;
        }
    }
}

void SpriteSystem::draw_all() {
    if (!renderer) return;

    for (const auto& [id, sprite] : active_sprites) {
        if (sprite.is_active) {
            SDL_Texture* tex = texture_atlas[sprite.texture_id];
            if (tex) {
                SDL_RenderTexture(renderer, tex, nullptr, &sprite.rect);
            }
        }
    }
}

bool SpriteSystem::check_collision(int instance_id1, int instance_id2) {
    if (active_sprites.count(instance_id1) && active_sprites.count(instance_id2) &&
        active_sprites[instance_id1].is_active && active_sprites[instance_id2].is_active)
    {
        // --- UPDATED: Manual AABB collision check for robustness ---
        const SDL_FRect& r1 = active_sprites[instance_id1].rect;
        const SDL_FRect& r2 = active_sprites[instance_id2].rect;

        // Check for overlap on both axes
        bool x_overlap = (r1.x < r2.x + r2.w) && (r1.x + r1.w > r2.x);
        bool y_overlap = (r1.y < r2.y + r2.h) && (r1.y + r1.h > r2.y);

        return x_overlap && y_overlap;
    }
    return false;
}
#endif
