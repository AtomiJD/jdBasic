#ifdef SDL3
#include "SpriteSystem.hpp"
#include "TextIO.hpp"
#include "json.hpp" // For parsing Aseprite JSON
#include <fstream>
#include <filesystem>

SpriteSystem::SpriteSystem() {}

SpriteSystem::~SpriteSystem() {
    shutdown();
}

void SpriteSystem::init(SDL_Renderer* main_renderer) {
    this->renderer = main_renderer;
}

void SpriteSystem::shutdown() {
    for (auto texture : texture_atlas) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    texture_atlas.clear();
    sprite_types.clear();
    active_sprites.clear();
    sprite_groups.clear();
}

bool SpriteSystem::load_sprite_type(int type_id, const std::string& filename) {
    if (!renderer) {
        return false;
    }
    SDL_Texture* new_texture = IMG_LoadTexture(renderer, filename.c_str());
    if (!new_texture) {
        TextIO::print("Failed to load texture '" + filename + "': " + std::string(SDL_GetError()) + "\n");
        return false;
    }
    texture_atlas.push_back(new_texture);

    SpriteType new_sprite_type;
    new_sprite_type.id = type_id;
    Animation default_anim;
    default_anim.name = "idle";
    default_anim.frames.push_back(new_texture);
    default_anim.frame_duration = 0.1f;
    new_sprite_type.animations["idle"] = default_anim;
    sprite_types[type_id] = new_sprite_type;
    return true;
}

bool SpriteSystem::load_aseprite_file(int type_id, const std::string& json_filename) {
    if (!renderer) return false;

    std::ifstream f(json_filename);
    if (!f.is_open()) {
        TextIO::print("Error: Could not open Aseprite JSON file: " + json_filename + "\n");
        return false;
    }
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(f);
    }
    catch (const nlohmann::json::parse_error& e) {
        TextIO::print("Error: Failed to parse Aseprite JSON: " + std::string(e.what()) + "\n");
        return false;
    }

    std::filesystem::path json_path(json_filename);
    std::string image_path_str = data["meta"]["image"];
    std::filesystem::path image_path = json_path.parent_path() / image_path_str;

    SDL_Texture* main_sheet = IMG_LoadTexture(renderer, image_path.string().c_str());
    if (!main_sheet) {
        TextIO::print("Error: Failed to load spritesheet image '" + image_path.string() + "': " + SDL_GetError() + "\n");
        return false;
    }

    SpriteType new_sprite_type;
    new_sprite_type.id = type_id;

    std::vector<SDL_Texture*> sorted_frame_textures;
    if (data["frames"].is_array()) { // Handle JSON Array format
        sorted_frame_textures.resize(data["frames"].size());
        for (size_t i = 0; i < data["frames"].size(); ++i) {
            auto rect_data = data["frames"][i]["frame"];
            SDL_FRect src_rect = { rect_data["x"], rect_data["y"], rect_data["w"], rect_data["h"] };
            SDL_Texture* frame_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, src_rect.w, src_rect.h);
            SDL_SetTextureBlendMode(frame_tex, SDL_BLENDMODE_BLEND);
            SDL_SetRenderTarget(renderer, frame_tex);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // fully transparent
            SDL_RenderClear(renderer);
            SDL_FRect dst_rect = { 0.0f, 0.0f, src_rect.w, src_rect.h };
            SDL_RenderTexture(renderer, main_sheet, &src_rect, &dst_rect);
            //SDL_RenderTexture(renderer, main_sheet, &src_rect, NULL);
            texture_atlas.push_back(frame_tex);
            sorted_frame_textures[i] = frame_tex;
            SDL_SetRenderTarget(renderer, NULL);

        }
    }
    else { // Handle JSON Hash format
        // Find the max frame index to size the vector correctly
        int max_frame_idx = -1;
        for (auto const& [key, val] : data["frames"].items()) {
            size_t last_space = key.find_last_of(" ");
            size_t last_dot = key.find_last_of(".");
            if (last_space != std::string::npos && last_dot != std::string::npos) {
                std::string num_str = key.substr(last_space + 1, last_dot - last_space - 1);
                max_frame_idx = std::max(max_frame_idx, std::stoi(num_str));
            }
        }
        sorted_frame_textures.resize(max_frame_idx + 1, nullptr);

        for (auto const& [key, val] : data["frames"].items()) {
            auto rect_data = val["frame"];
            SDL_FRect src_rect = { rect_data["x"], rect_data["y"], rect_data["w"], rect_data["h"] };
            SDL_Texture* frame_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, src_rect.w, src_rect.h);
            SDL_SetTextureBlendMode(frame_tex, SDL_BLENDMODE_BLEND);
            SDL_SetRenderTarget(renderer, frame_tex);
            SDL_RenderTexture(renderer, main_sheet, &src_rect, NULL);
            texture_atlas.push_back(frame_tex);

            size_t last_space = key.find_last_of(" ");
            size_t last_dot = key.find_last_of(".");
            if (last_space != std::string::npos && last_dot != std::string::npos) {
                std::string num_str = key.substr(last_space + 1, last_dot - last_space - 1);
                int frame_idx = std::stoi(num_str);
                if (frame_idx < sorted_frame_textures.size()) {
                    sorted_frame_textures[frame_idx] = frame_tex;
                }
            }
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(main_sheet);

    for (const auto& tag : data["meta"]["frameTags"]) {
        Animation anim;
        anim.name = tag["name"];
        int duration_ms = tag["duration"];
        anim.frame_duration = duration_ms > 0 ? (float)duration_ms / 1000.0f : 0.1f;
        int from = tag["from"];
        int to = tag["to"];

        for (int i = from; i <= to; ++i) {
            anim.frames.push_back(sorted_frame_textures[i]);
        }
        new_sprite_type.animations[anim.name] = anim;
    }

    sprite_types[type_id] = new_sprite_type;
    return true;
}

int SpriteSystem::create_sprite(int type_id, float x, float y) {
    if (sprite_types.find(type_id) == sprite_types.end()) {
        return -1;
    }
    int instance_id = next_instance_id++;
    auto new_sprite = std::make_unique<Sprite>();
    new_sprite->type_id = type_id;
    new_sprite->x = x;
    new_sprite->y = y;
    new_sprite->is_active = true;
    new_sprite->instance_id = instance_id;
    if (!sprite_types[type_id].animations.empty()) {
        new_sprite->current_animation_name = sprite_types[type_id].animations.begin()->first;
    }
    active_sprites[instance_id] = std::move(new_sprite);
    return instance_id;
}

void SpriteSystem::move_sprite(int instance_id, float x, float y) {
    if (active_sprites.count(instance_id)) {
        active_sprites.at(instance_id)->x = x;
        active_sprites.at(instance_id)->y = y;
    }
}

void SpriteSystem::set_velocity(int instance_id, float vx, float vy) {
    if (active_sprites.count(instance_id)) {
        active_sprites.at(instance_id)->vx = vx;
        active_sprites.at(instance_id)->vy = vy;
    }
}

void SpriteSystem::delete_sprite(int instance_id) {
    if (active_sprites.count(instance_id)) {
        active_sprites.erase(instance_id);
        for (auto& [group_id, members] : sprite_groups) {
            std::erase(members, instance_id);
        }
    }
}

void SpriteSystem::set_animation(int instance_id, const std::string& animation_name) {
    if (active_sprites.count(instance_id)) {
        Sprite* sprite = active_sprites.at(instance_id).get();
        if (sprite_types.count(sprite->type_id) && sprite_types[sprite->type_id].animations.count(animation_name)) {
            if (sprite->current_animation_name != animation_name) {
                sprite->current_animation_name = animation_name;
                sprite->current_frame = 0;
                sprite->frame_timer = 0.0f;
                Animation& anim = sprite_types[sprite->type_id].animations.at(animation_name);
                if (!anim.frames.empty() && anim.frames[0]) {
                    SDL_GetTextureSize(anim.frames[0], &sprite->rect.w, &sprite->rect.h);
                }
            }
        }
    }
}

void SpriteSystem::set_flip(int instance_id, bool flip) {
    if (active_sprites.count(instance_id)) {
        active_sprites.at(instance_id)->is_flipped = flip;
    }
}

float SpriteSystem::get_x(int instance_id) const {
    if (active_sprites.count(instance_id)) {
        return active_sprites.at(instance_id)->x;
    }
    return 0.0f;
}

float SpriteSystem::get_y(int instance_id) const {
    if (active_sprites.count(instance_id)) {
        return active_sprites.at(instance_id)->y;
    }
    return 0.0f;
}

const SDL_FRect* SpriteSystem::get_sprite_rect(int instance_id) const {
    if (active_sprites.count(instance_id)) {
        return &active_sprites.at(instance_id)->rect;
    }
    return nullptr;
}

SDL_FRect SpriteSystem::get_collision_rect(int instance_id) const {
    const SDL_FRect* p_sprite_rect = this->get_sprite_rect(instance_id);
    if (!p_sprite_rect) {
        return { 0, 0, 0, 0 };
    }

    SDL_FRect collision_rect = *p_sprite_rect;
    float y_offset = 32.0f;
    float x_inset = 16.0f; // Inset from each side

    collision_rect.y += y_offset;
    collision_rect.h -= (y_offset * 1.5f);
    collision_rect.x += x_inset;
    collision_rect.w -= (x_inset * 2.0f); // Shrink width from both sides

    return collision_rect;
}

void SpriteSystem::update(float delta_time) {
    for (auto& [id, sprite_ptr] : active_sprites) {
        Sprite* sprite = sprite_ptr.get();
        if (!sprite->is_active) continue;

        sprite->x += sprite->vx * delta_time;
        sprite->y += sprite->vy * delta_time;

        sprite->rect.x = sprite->x;
        sprite->rect.y = sprite->y;

        if (!sprite->current_animation_name.empty()) {
            // Ensure the sprite type and animation exist before proceeding
            if (sprite_types.count(sprite->type_id) &&
                sprite_types.at(sprite->type_id).animations.count(sprite->current_animation_name)) {

                // Get a pointer to the animation, not a reference
                const Animation* anim = &sprite_types.at(sprite->type_id).animations.at(sprite->current_animation_name);

                if (anim && !anim->frames.empty()) {
                    sprite->frame_timer += delta_time;
                    if (sprite->frame_timer >= anim->frame_duration && anim->frame_duration > 0) {
                        sprite->frame_timer -= anim->frame_duration;
                        sprite->current_frame = (sprite->current_frame + 1) % anim->frames.size();
                    }
                    // Sync the entire rect (x, y, w, h) with the sprite's current state
                    sprite->rect.x = sprite->x;
                    sprite->rect.y = sprite->y;
                    SDL_GetTextureSize(anim->frames[sprite->current_frame], &sprite->rect.w, &sprite->rect.h);

                }
            }
        }
    }
}

void SpriteSystem::draw_all(float cam_x, float cam_y) { // Add parameters
    if (!renderer) return;

    for (auto& [id, sprite_ptr] : active_sprites) {
        Sprite* sprite = sprite_ptr.get();
        if (!sprite->is_active || sprite->current_animation_name.empty()) continue;

        if (sprite_types.count(sprite->type_id) && sprite_types.at(sprite->type_id).animations.count(sprite->current_animation_name)) {
            const Animation& anim = sprite_types.at(sprite->type_id).animations.at(sprite->current_animation_name);
            if (sprite->current_frame >= anim.frames.size()) continue;

            SDL_Texture* tex = anim.frames[sprite->current_frame];

            if (tex) {
                //SDL_GetTextureSize(tex, &sprite->rect.w, &sprite->rect.h);
                //// Apply the camera offset here
                //sprite->rect.x = sprite->x - cam_x;
                //sprite->rect.y = sprite->y - cam_y;

                //SDL_FlipMode flip_flag = sprite->is_flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

                //bool result = SDL_RenderTextureRotated(renderer, tex, nullptr, &sprite->rect, 0.0, nullptr, flip_flag);
                //if (!result ) {
                //    TextIO::print("SDL_RenderTextureRotated failed for animation "
                //        + sprite->current_animation_name
                //        + ": " + SDL_GetError() + "\n");
                //}
                //SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Set color to bright red
                //SDL_FRect debug_rect = get_collision_rect(id);
                //debug_rect.x -= cam_x; // Apply camera offset
                //debug_rect.y -= cam_y;
                //SDL_RenderRect(renderer, &debug_rect);
                SDL_FRect render_rect = sprite->rect;
                render_rect.x -= cam_x;
                render_rect.y -= cam_y;

                SDL_FlipMode flip_flag = sprite->is_flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                SDL_RenderTextureRotated(renderer, tex, nullptr, &render_rect, 0.0, nullptr, flip_flag);
                
                //DEBUG RED BOX !
                //SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                //SDL_FRect debug_rect = get_collision_rect(id); // Gets the box in WORLD coordinates
                //debug_rect.x -= cam_x;                         // Applies camera offset for drawing
                //debug_rect.y -= cam_y;
                //SDL_RenderRect(renderer, &debug_rect);
                //END DEBUG RED BOX 
            }
            else {
                TextIO::print("No Texture: " + sprite->current_animation_name + "\n");
            }
        }
    }
}

int SpriteSystem::create_group() {
    int new_id = next_group_id++;
    sprite_groups[new_id] = {};
    return new_id;
}

void SpriteSystem::add_to_group(int group_id, int instance_id) {
    if (sprite_groups.count(group_id) && active_sprites.count(instance_id)) {
        if (std::find(sprite_groups[group_id].begin(), sprite_groups[group_id].end(), instance_id) == sprite_groups[group_id].end()) {
            sprite_groups[group_id].push_back(instance_id);
        }
    }
}

void SpriteSystem::remove_from_group(int group_id, int instance_id) {
    if (sprite_groups.count(group_id)) {
        std::erase(sprite_groups[group_id], instance_id);
    }
}

const std::vector<int>& SpriteSystem::get_sprites_in_group(int group_id) {
    static const std::vector<int> empty_vector;
    if (sprite_groups.count(group_id)) {
        return sprite_groups.at(group_id);
    }
    return empty_vector;
}

bool SpriteSystem::check_collision(int instance_id1, int instance_id2) {
    if (active_sprites.count(instance_id1) && active_sprites.count(instance_id2)) {
        const Sprite* s1 = active_sprites.at(instance_id1).get();
        const Sprite* s2 = active_sprites.at(instance_id2).get();
        if (s1->is_active && s2->is_active) {
            return SDL_HasRectIntersectionFloat(&s1->rect, &s2->rect);
        }
    }
    return false;
}

int SpriteSystem::check_collision_sprite_group(int instance_id, int group_id) {
    if (!active_sprites.count(instance_id) || !sprite_groups.count(group_id)) {
        return -1;
    }
    const Sprite* s1 = active_sprites.at(instance_id).get();
    if (!s1->is_active) return -1;

    for (int other_id : sprite_groups.at(group_id)) {
        if (instance_id == other_id) continue;
        if (check_collision(instance_id, other_id)) {
            return other_id;
        }
    }
    return -1;
}

std::pair<int, int> SpriteSystem::check_collision_groups(int group_id1, int group_id2) {
    if (!sprite_groups.count(group_id1) || !sprite_groups.count(group_id2)) {
        return { -1, -1 };
    }

    for (int id1 : sprite_groups.at(group_id1)) {
        for (int id2 : sprite_groups.at(group_id2)) {
            if (id1 == id2) continue;
            if (check_collision(id1, id2)) {
                return { id1, id2 };
            }
        }
    }
    return { -1, -1 };
}
#endif
