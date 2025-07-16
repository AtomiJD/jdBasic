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

    std::map<std::string, SDL_Texture*> frame_textures;
    for (auto const& [frame_name, frame_data] : data["frames"].items()) {
        auto rect_data = frame_data["frame"];
        SDL_FRect src_rect = { rect_data["x"], rect_data["y"], rect_data["w"], rect_data["h"] };
        SDL_Texture* frame_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, src_rect.w, src_rect.h);
        SDL_SetTextureBlendMode(frame_tex, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer, frame_tex);
        SDL_RenderTexture(renderer, main_sheet, &src_rect, NULL);
        texture_atlas.push_back(frame_tex);
        frame_textures[frame_name] = frame_tex;
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
            for (auto const& [key, val] : data["frames"].items()) {
                if (val.contains("frame") && val["frame"].contains("x") && val["frame"].contains("w") && val["frame"]["w"] != 0) {
                    if (val["frame"]["x"].get<int>() / val["frame"]["w"].get<int>() == i) {
                        if (frame_textures.count(key)) {
                            anim.frames.push_back(frame_textures[key]);
                            break;
                        }
                    }
                }
            }
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

void SpriteSystem::update(float delta_time) {
    for (auto& [id, sprite_ptr] : active_sprites) {
        Sprite* sprite = sprite_ptr.get();
        if (!sprite->is_active) continue;

        sprite->x += sprite->vx * delta_time;
        sprite->y += sprite->vy * delta_time;

        if (!sprite->current_animation_name.empty()) {
            if (sprite_types.count(sprite->type_id) && sprite_types.at(sprite->type_id).animations.count(sprite->current_animation_name)) {
                Animation& anim = sprite_types.at(sprite->type_id).animations.at(sprite->current_animation_name);
                if (!anim.frames.empty()) {
                    sprite->frame_timer += delta_time;
                    if (sprite->frame_timer >= anim.frame_duration && anim.frame_duration > 0) {
                        sprite->frame_timer -= anim.frame_duration;
                        sprite->current_frame = (sprite->current_frame + 1) % anim.frames.size();
                    }
                }
            }
        }
    }
}

void SpriteSystem::draw_all() {
    if (!renderer) return;

    for (auto& [id, sprite_ptr] : active_sprites) {
        Sprite* sprite = sprite_ptr.get();
        if (!sprite->is_active || sprite->current_animation_name.empty()) continue;

        if (sprite_types.count(sprite->type_id) && sprite_types.at(sprite->type_id).animations.count(sprite->current_animation_name)) {
            const Animation& anim = sprite_types.at(sprite->type_id).animations.at(sprite->current_animation_name);
            if (sprite->current_frame >= anim.frames.size()) continue;

            SDL_Texture* tex = anim.frames[sprite->current_frame];
            if (tex) {
                SDL_GetTextureSize(tex, &sprite->rect.w, &sprite->rect.h);
                sprite->rect.x = sprite->x;
                sprite->rect.y = sprite->y;

                SDL_FlipMode flip_flag = sprite->is_flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                SDL_RenderTextureRotated(renderer, tex, nullptr, &sprite->rect, 0.0, nullptr, flip_flag);
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
