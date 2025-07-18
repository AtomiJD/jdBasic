#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "SpriteSystem.hpp" // To interact with sprites for collision

// Forward-declare for use in TileMap
class TileMap;

class TileMapSystem {
public:
    TileMapSystem();
    ~TileMapSystem();

    void init(SDL_Renderer* renderer);
    void shutdown();

    // Loads a map from a Tiled JSON file.
    bool load_map(const std::string& map_name, const std::string& filename);

    // Draws a specific layer of the map.
    void draw_layer(const std::string& map_name, const std::string& layer_name, int world_offset_x = 0, int world_offset_y = 0);

    // Retrieves objects from an object layer for spawning entities.
    std::vector<std::map<std::string, std::string>> get_objects_by_type(const std::string& map_name, const std::string& type);

    // Checks if a sprite's rect collides with any solid tiles on a layer.
    int get_tile_id(const std::string& map_name, const std::string& layer_name, int tile_x, int tile_y) const;
    void draw_debug_collisions(int sprite_instance_id, const SpriteSystem& sprite_system, const std::string& map_name, const std::string& layer_name, float cam_x, float cam_y);
    bool check_sprite_collision(int sprite_instance_id, const SpriteSystem& sprite_system, const std::string& map_name, const std::string& layer_name);

private:
    struct Tileset {
        int first_gid;
        int tile_width;
        int tile_height;
        int tile_count;
        int columns;
        SDL_Texture* texture = nullptr;
        std::map<int, std::vector<SDL_FRect>> per_tile_collisions;
    };

    struct TileLayer {
        std::string name;
        int width;
        int height;
        std::vector<int> data;
    };

    struct ImageLayer {
        std::string name;
        SDL_Texture* texture = nullptr;
        SDL_FRect rect; // Stores the image's position and size in the world
    };

    struct MapObject {
        std::string name;
        std::string type;
        SDL_FRect rect;
        std::map<std::string, std::string> properties;
    };

    struct ObjectLayer {
        std::string name;
        std::vector<MapObject> objects;
    };

    struct TileMap {
        std::string name;
        int width; // in tiles
        int height; // in tiles
        std::vector<Tileset> tilesets;
        std::map<std::string, TileLayer> tile_layers;
        std::map<std::string, ObjectLayer> object_layers;
        std::map<std::string, ImageLayer> image_layers;
    };

    SDL_Renderer* renderer = nullptr;
    std::map<std::string, TileMap> loaded_maps;
};
#endif