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
                    }
                }
                ol.objects.push_back(mo);
            }
            new_map.object_layers[ol.name] = ol;
        }
    }

    loaded_maps[map_name] = new_map;
    return true;
}

void TileMapSystem::draw_layer(const std::string& map_name, const std::string& layer_name, int world_offset_x, int world_offset_y) {
    if (!loaded_maps.count(map_name) || !loaded_maps[map_name].tile_layers.count(layer_name)) {
        return;
    }

    const auto& map = loaded_maps[map_name];
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

// NOTE: This implementation assumes you will add a public `get_sprite_rect` method
// to your SpriteSystem class that returns a const SDL_FRect*.
// Example: const SDL_FRect* SpriteSystem::get_sprite_rect(int instance_id) const;
bool TileMapSystem::check_sprite_collision(int sprite_instance_id, const SpriteSystem& sprite_system, const std::string& map_name, const std::string& layer_name) {
    if (!loaded_maps.count(map_name) || !loaded_maps[map_name].tile_layers.count(layer_name)) {
        return false;
    }

    // This is a placeholder for getting the sprite's rect.
    // You must implement a way to get this from your SpriteSystem.
    // For now, we'll simulate getting it based on x/y, but width/height will be missing.
    // A proper implementation requires a function like `sprite_system.get_sprite_rect(id)`.
    SDL_FRect sprite_rect = { sprite_system.get_x(sprite_instance_id), sprite_system.get_y(sprite_instance_id), 32, 32 }; // Assuming 32x32 for now


    const auto& map = loaded_maps.at(map_name);
    const auto& layer = map.tile_layers.at(layer_name);
    if (map.tilesets.empty()) return false;

    int tile_w = map.tilesets[0].tile_width;
    int tile_h = map.tilesets[0].tile_height;

    // Determine the range of tiles to check based on the sprite's bounding box
    int start_x = static_cast<int>(sprite_rect.x) / tile_w;
    int end_x = static_cast<int>(sprite_rect.x + sprite_rect.w) / tile_w;
    int start_y = static_cast<int>(sprite_rect.y) / tile_h;
    int end_y = static_cast<int>(sprite_rect.y + sprite_rect.h) / tile_h;

    // Loop through only the potentially colliding tiles
    for (int y = start_y; y <= end_y; ++y) {
        for (int x = start_x; x <= end_x; ++x) {
            // Bounds check for the tile coordinates
            if (x >= 0 && x < layer.width && y >= 0 && y < layer.height) {
                if (layer.data[y * layer.width + x] != 0) { // Check if the tile is not empty
                    SDL_FRect tile_rect = { (float)(x * tile_w), (float)(y * tile_h), (float)tile_w, (float)tile_h };
                    if (SDL_HasRectIntersectionFloat(&sprite_rect, &tile_rect)) {
                        return true; // Collision detected
                    }
                }
            }
        }
    }

    return false; // No collision
}
#endif
