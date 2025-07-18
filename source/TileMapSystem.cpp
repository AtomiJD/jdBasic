#ifdef SDL3
#include "TileMapSystem.hpp"
#include "TextIO.hpp"
#include "json.hpp"
#include <fstream>
#include <filesystem>

// Helper function to convert a JSON value to a string, regardless of its type.
std::string json_property_to_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    else if (value.is_number()) {
        return std::to_string(value.get<double>());
    }
    else if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return ""; // Return empty for other types
}


TileMapSystem::TileMapSystem() {}
TileMapSystem::~TileMapSystem() {
    shutdown();
}

void TileMapSystem::init(SDL_Renderer* main_renderer) {
    this->renderer = main_renderer;
}

void TileMapSystem::shutdown() {
    for (auto& [map_name, map_data] : loaded_maps) {
        for (auto& tileset : map_data.tilesets) {
            if (tileset.texture) {
                SDL_DestroyTexture(tileset.texture);
            }
        }
        for (auto& [layer_name, layer_data] : map_data.image_layers) {
            if (layer_data.texture) {
                SDL_DestroyTexture(layer_data.texture);
            }
        }
    }
    loaded_maps.clear();
}

bool TileMapSystem::load_map(const std::string& map_name, const std::string& filename) {
    if (!renderer) return false;

    // 1. Parse JSON
    std::ifstream f(filename);
    if (!f.is_open()) {
        TextIO::print("Error: Could not open Tiled map file: " + filename + "\n");
        return false;
    }
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(f);
    }
    catch (const nlohmann::json::parse_error& e) {
        TextIO::print("Error: Failed to parse Tiled JSON: " + std::string(e.what()) + "\n");
        return false;
    }

    TileMap new_map;
    new_map.name = map_name;
    new_map.width = data["width"];
    new_map.height = data["height"];

    std::filesystem::path map_path(filename);
    std::string map_dir = map_path.parent_path().string();

    // 2. Load Tilesets
    for (const auto& ts_data : data["tilesets"]) {
        Tileset ts;
        ts.first_gid = ts_data["firstgid"];
        ts.tile_width = ts_data["tilewidth"];
        ts.tile_height = ts_data["tileheight"];
        ts.tile_count = ts_data["tilecount"];
        ts.columns = ts_data["columns"];

        std::filesystem::path image_path = map_dir;
        image_path /= ts_data["image"].get<std::string>();

        ts.texture = IMG_LoadTexture(renderer, image_path.string().c_str());
        if (!ts.texture) {
            TextIO::print("Error loading tileset image: " + image_path.string() + "\n");
            return false;
        }
        if (ts_data.contains("tiles")) {
            for (const auto& tile_data : ts_data["tiles"]) {
                int local_id = tile_data["id"];
                if (tile_data.contains("objectgroup") && tile_data["objectgroup"].contains("objects")) {
                    std::vector<SDL_FRect> collision_rects;
                    for (const auto& obj : tile_data["objectgroup"]["objects"]) {
                        collision_rects.push_back({
                            obj["x"].get<float>(),
                            obj["y"].get<float>(),
                            obj["width"].get<float>(),
                            obj["height"].get<float>()
                            });
                    }
                    ts.per_tile_collisions[local_id] = collision_rects;
                }
            }
        }
        new_map.tilesets.push_back(ts);
    }

    // 3. Load Layers
    for (const auto& layer_data : data["layers"]) {
        if (layer_data["type"] == "tilelayer") {
            TileLayer tl;
            tl.name = layer_data["name"];
            tl.width = layer_data["width"];
            tl.height = layer_data["height"];
            tl.data = layer_data["data"].get<std::vector<int>>();
            new_map.tile_layers[tl.name] = tl;
        }
        else if (layer_data["type"] == "objectgroup") {
            ObjectLayer ol;
            ol.name = layer_data["name"];
            for (const auto& obj_data : layer_data["objects"]) {
                MapObject mo;
                mo.name = obj_data.value("name", "");
                mo.type = obj_data.value("type", "");
                mo.rect = {
                    obj_data["x"].get<float>(),
                    obj_data["y"].get<float>(),
                    obj_data["width"].get<float>(),
                    obj_data["height"].get<float>()
                };

                if (obj_data.contains("properties")) {
                    for (const auto& prop : obj_data["properties"]) {
                        mo.properties[prop["name"]] = json_property_to_string(prop["value"]);
                        mo.properties[prop["type"]] = json_property_to_string(prop["value"]);
                    }
                }
                ol.objects.push_back(mo);
            }
            new_map.object_layers[ol.name] = ol;
        }
        else if (layer_data["type"] == "imagelayer") {
            ImageLayer il;
            il.name = layer_data["name"];

            // Construct the full path to the image
            std::filesystem::path image_path = map_dir;
            image_path /= layer_data["image"].get<std::string>();

            // Load the texture
            il.texture = IMG_LoadTexture(renderer, image_path.string().c_str());
            if (!il.texture) {
                TextIO::print("Error loading image layer texture: " + image_path.string() + "\n");
                // Handle error appropriately, maybe return false
                continue;
            }

            // Get texture dimensions and layer position
            float w, h;
            SDL_GetTextureSize(il.texture, &w, &h);
            il.rect = {
                layer_data["x"].get<float>(),
                layer_data["y"].get<float>(),
                (float)w,
                (float)h
            };

            new_map.image_layers[il.name] = il;
        }
    }

    loaded_maps[map_name] = new_map;
    return true;
}

void TileMapSystem::draw_layer(const std::string& map_name, const std::string& layer_name, int world_offset_x, int world_offset_y) {
    if (!loaded_maps.count(map_name)) return;

    const auto& map = loaded_maps[map_name];

    if (map.tile_layers.count(layer_name)) {
        const auto& layer = map.tile_layers.at(layer_name);
        for (int y = 0; y < layer.height; ++y) {
            for (int x = 0; x < layer.width; ++x) {
                int tile_gid = layer.data[y * layer.width + x];
                if (tile_gid == 0) continue;

                const Tileset* current_tileset = nullptr;
                for (const auto& ts : map.tilesets) {
                    if (tile_gid >= ts.first_gid) {
                        current_tileset = &ts;
                    }
                }
                if (!current_tileset) continue;

                int local_tile_id = tile_gid - current_tileset->first_gid;
                int ts_x = (local_tile_id % current_tileset->columns) * current_tileset->tile_width;
                int ts_y = (local_tile_id / current_tileset->columns) * current_tileset->tile_height;

                SDL_FRect src_rect = { ts_x, ts_y, current_tileset->tile_width, current_tileset->tile_height };
                SDL_FRect dest_rect = {
                    (float)(x * current_tileset->tile_width - world_offset_x),
                    (float)(y * current_tileset->tile_height - world_offset_y),
                    (float)current_tileset->tile_width,
                    (float)current_tileset->tile_height
                };

                SDL_RenderTexture(renderer, current_tileset->texture, &src_rect, &dest_rect);
            }
        }
    } else if (map.image_layers.count(layer_name)) {
        const auto& layer = map.image_layers.at(layer_name);

        // Create a destination rectangle from the layer's stored rect
        SDL_FRect dest_rect = layer.rect;

        // Apply the camera offset
        dest_rect.x -= world_offset_x;
        dest_rect.y -= world_offset_y;

        SDL_RenderTexture(renderer, layer.texture, nullptr, &dest_rect);
    }
}

std::vector<std::map<std::string, std::string>> TileMapSystem::get_objects_by_type(const std::string& map_name, const std::string& type) {
    std::vector<std::map<std::string, std::string>> found_objects;
    if (!loaded_maps.count(map_name)) {
        return found_objects;
    }

    const auto& map = loaded_maps.at(map_name);
    for (const auto& [layer_name, object_layer] : map.object_layers) {
        for (const auto& object : object_layer.objects) {
            if (object.type == type) {
                std::map<std::string, std::string> obj_props = object.properties;
                // Add standard object fields to the map for easy access in BASIC
                obj_props["name"] = object.name;
                obj_props["type"] = object.type;
                obj_props["x"] = std::to_string(object.rect.x);
                obj_props["y"] = std::to_string(object.rect.y);
                obj_props["width"] = std::to_string(object.rect.w);
                obj_props["height"] = std::to_string(object.rect.h);
                found_objects.push_back(obj_props);
            }
        }
    }
    return found_objects;
}

int TileMapSystem::get_tile_id(const std::string& map_name, const std::string& layer_name, int tile_x, int tile_y) const {
    if (!loaded_maps.count(map_name) || !loaded_maps.at(map_name).tile_layers.count(layer_name)) {
        return 0; // Map or layer not found
    }

    const auto& layer = loaded_maps.at(map_name).tile_layers.at(layer_name);

    // Bounds check
    if (tile_x < 0 || tile_x >= layer.width || tile_y < 0 || tile_y >= layer.height) {
        return 0; // Out of bounds
    }

    return layer.data[tile_y * layer.width + tile_x];
}

bool TileMapSystem::check_sprite_collision(int sprite_instance_id, const SpriteSystem& sprite_system, const std::string& map_name, const std::string& layer_name) {
    if (!loaded_maps.count(map_name) || !loaded_maps[map_name].tile_layers.count(layer_name)) {
        return false;
    }

    SDL_FRect player_coll_rect = sprite_system.get_collision_rect(sprite_instance_id);
    if (player_coll_rect.w == 0) return false;

    const auto& map = loaded_maps.at(map_name);
    const auto& layer = map.tile_layers.at(layer_name);

    int start_tile_x = static_cast<int>(player_coll_rect.x) / map.tilesets[0].tile_width;
    int end_tile_x = static_cast<int>(player_coll_rect.x + player_coll_rect.w) / map.tilesets[0].tile_width;
    int start_tile_y = static_cast<int>(player_coll_rect.y) / map.tilesets[0].tile_height;
    int end_tile_y = static_cast<int>(player_coll_rect.y + player_coll_rect.h) / map.tilesets[0].tile_height;

    for (int ty = start_tile_y; ty <= end_tile_y; ++ty) {
        for (int tx = start_tile_x; tx <= end_tile_x; ++tx) {
            if (tx < 0 || tx >= layer.width || ty < 0 || ty >= layer.height) continue;

            int gid = layer.data[ty * layer.width + tx];
            if (gid == 0) continue;

            const Tileset* tileset = nullptr;
            for (const auto& ts : map.tilesets) {
                if (gid >= ts.first_gid) {
                    tileset = &ts;
                }
            }
            if (!tileset) continue;

            int local_id = gid - tileset->first_gid;
            int tile_world_x = tx * tileset->tile_width;
            int tile_world_y = ty * tileset->tile_height;

            // Check if this tile has custom collision shapes
            if (tileset->per_tile_collisions.count(local_id)) {
                // It does! Check against each custom shape.
                for (const auto& custom_rect : tileset->per_tile_collisions.at(local_id)) {
                    SDL_FRect world_shape_rect = custom_rect;
                    world_shape_rect.x += tile_world_x;
                    world_shape_rect.y += tile_world_y;

                    if (SDL_HasRectIntersectionFloat(&player_coll_rect, &world_shape_rect)) {
                        return true; // Collision found!
                    }
                }
            }
            else {
                // It doesn't. Fall back to full-tile collision.
                SDL_FRect tile_rect = {
                    (float)tile_world_x, (float)tile_world_y,
                    (float)tileset->tile_width, (float)tileset->tile_height
                };
                if (SDL_HasRectIntersectionFloat(&player_coll_rect, &tile_rect)) {
                    return true;
                }
            }
        }
    }

    return false;
}

void TileMapSystem::draw_debug_collisions(int sprite_instance_id, const SpriteSystem& sprite_system, const std::string& map_name, const std::string& layer_name, float cam_x, float cam_y) {
    // This function mirrors check_sprite_collision but draws instead of checking
    SDL_FRect player_coll_rect = sprite_system.get_collision_rect(sprite_instance_id);
    if (player_coll_rect.w == 0) return;

    const auto& map = loaded_maps.at(map_name);
    const auto& layer = map.tile_layers.at(layer_name);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Red for all tile debug boxes

    // ... (copy the entire start_tile_x to end_tile_y calculation from the function above) ...
    int start_tile_x = static_cast<int>(player_coll_rect.x) / map.tilesets[0].tile_width;
    int end_tile_x = static_cast<int>(player_coll_rect.x + player_coll_rect.w) / map.tilesets[0].tile_width;
    int start_tile_y = static_cast<int>(player_coll_rect.y) / map.tilesets[0].tile_height;
    int end_tile_y = static_cast<int>(player_coll_rect.y + player_coll_rect.h) / map.tilesets[0].tile_height;

    for (int ty = start_tile_y; ty <= end_tile_y; ++ty) {
        for (int tx = start_tile_x; tx <= end_tile_x; ++tx) {
            if (tx < 0 || tx >= layer.width || ty < 0 || ty >= layer.height) continue;
            int gid = layer.data[ty * layer.width + tx];
            if (gid == 0) continue;
            // ... (copy the tileset and local_id logic from the function above) ...
            const Tileset* tileset = nullptr;
            for (const auto& ts : map.tilesets) { if (gid >= ts.first_gid) tileset = &ts; }
            if (!tileset) continue;
            int local_id = gid - tileset->first_gid;
            int tile_world_x = tx * tileset->tile_width;
            int tile_world_y = ty * tileset->tile_height;

            if (tileset->per_tile_collisions.count(local_id)) {
                for (const auto& custom_rect : tileset->per_tile_collisions.at(local_id)) {
                    SDL_FRect world_shape_rect = custom_rect;
                    world_shape_rect.x += tile_world_x;
                    world_shape_rect.y += tile_world_y;
                    // Apply camera for drawing
                    world_shape_rect.x -= cam_x;
                    world_shape_rect.y -= cam_y;
                    SDL_RenderRect(renderer, &world_shape_rect);
                }
            }
            else {
                SDL_FRect tile_rect = {
                    (float)tile_world_x - cam_x, (float)tile_world_y - cam_y,
                    (float)tileset->tile_width, (float)tileset->tile_height
                };
                SDL_RenderRect(renderer, &tile_rect);
            }
        }
    }
}
#endif
