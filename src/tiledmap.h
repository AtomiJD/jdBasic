#pragma once
#ifdef GFX
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdint>

// ── Minimal XML Parser (for TMX/TSX) ──────────────────────────────

struct XmlAttribute {
    std::string name;
    std::string value;
};

struct XmlNode {
    std::string tag;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
    std::string text;  // text content between tags

    std::string attr(const std::string& name, const std::string& def = "") const;
    int attr_int(const std::string& name, int def = 0) const;
    float attr_float(const std::string& name, float def = 0.0f) const;
    const XmlNode* child(const std::string& tag) const;
    std::vector<const XmlNode*> children_by_tag(const std::string& tag) const;
    bool has_attr(const std::string& name) const;
};

XmlNode xml_parse(const std::string& xml_content);

// ── TiledMap System ───────────────────────────────────────────────

struct TileAnimFrame {
    int local_tile_id;
    int duration_ms;
};

struct TileAnimation {
    std::vector<TileAnimFrame> frames;
    float elapsed_ms = 0;
    int current_frame = 0;
};

struct TiledTileset {
    int first_gid = 0;
    std::string name;
    int tile_width = 32;
    int tile_height = 32;
    int tile_count = 0;
    int columns = 0;
    SDL_Texture* texture = nullptr;             // atlas-based tileset
    std::map<int, SDL_Texture*> tile_textures;  // collection-based (per-tile images)
    std::map<int, std::string> tile_img_paths;  // deferred paths for lazy loading
    std::map<int, int> tile_img_w;              // per-tile width (collection)
    std::map<int, int> tile_img_h;              // per-tile height (collection)
    std::map<int, TileAnimation> animations;    // local_id -> animation
    std::map<int, std::vector<SDL_FRect>> per_tile_collisions;
    bool is_collection = false;                 // true if columns==0 (individual images)
};

struct TiledTileLayer {
    std::string name;
    int id = 0;
    int width = 0;
    int height = 0;
    bool visible = true;
    float opacity = 1.0f;
    std::vector<uint32_t> data;
};

struct TiledMapObject {
    int id = 0;
    std::string name;
    std::string type;
    float x = 0, y = 0;
    float width = 0, height = 0;
    uint32_t gid = 0;  // 0 = no tile reference
    std::map<std::string, std::string> properties;
};

struct TiledObjectLayer {
    std::string name;
    int id = 0;
    bool visible = true;
    float opacity = 1.0f;
    std::vector<TiledMapObject> objects;
};

struct TiledImageLayer {
    std::string name;
    int id = 0;
    bool visible = true;
    float opacity = 1.0f;
    SDL_Texture* texture = nullptr;
    float x = 0, y = 0;
    float w = 0, h = 0;
};

// Tracks layer order (type + index into respective container)
struct TiledLayerRef {
    enum Type { TILE, OBJECT, IMAGE };
    Type type;
    std::string name;
};

struct TiledMap {
    std::string name;
    int width = 0;    // in tiles
    int height = 0;
    int tile_width = 32;
    int tile_height = 32;
    std::map<std::string, std::string> properties;  // map-level custom properties
    std::vector<TiledTileset> tilesets;
    std::map<std::string, TiledTileLayer> tile_layers;
    std::map<std::string, TiledObjectLayer> object_layers;
    std::map<std::string, TiledImageLayer> image_layers;
    std::vector<TiledLayerRef> layer_order;  // draw order
};

class TiledMapSystem {
public:
    TiledMapSystem();
    ~TiledMapSystem();

    void init(SDL_Renderer* renderer);
    void shutdown();

    bool load_map(const std::string& map_name, const std::string& filename);
    void free_map(const std::string& map_name);

    // Update animations (call each frame with delta time in seconds)
    void update(float dt);

    // Draw all tile layers in order (uses camera offset)
    void draw_all(const std::string& map_name, float cam_x, float cam_y);

    // Draw a specific layer
    void draw_layer(const std::string& map_name, const std::string& layer_name, float cam_x, float cam_y);

    // Draw object layer sprites
    void draw_object_layer(const std::string& map_name, const std::string& layer_name, float cam_x, float cam_y);

    // Query objects
    std::vector<TiledMapObject> get_objects(const std::string& map_name, const std::string& layer_name);

    // Get layer names in draw order
    std::vector<std::string> get_layer_names(const std::string& map_name);

    // Get map size in pixels
    void get_map_pixel_size(const std::string& map_name, int& w, int& h);

    // Get map tile size
    void get_tile_size(const std::string& map_name, int& tw, int& th);

    // Collision check: does sprite rect overlap any non-zero tile on layer?
    bool check_collision(const std::string& map_name, const std::string& layer_name,
                         float rect_x, float rect_y, float rect_w, float rect_h);

    // Get tile GID at pixel position
    int get_tile_at(const std::string& map_name, const std::string& layer_name,
                    float pixel_x, float pixel_y);

    bool has_map(const std::string& map_name) const;
    std::map<std::string, std::string> get_map_properties(const std::string& map_name) const;

private:
    SDL_Renderer* renderer = nullptr;
    std::map<std::string, TiledMap> loaded_maps;

    bool parse_tileset_tsx(const std::string& tsx_path, TiledTileset& ts);
    void parse_tileset_node(const XmlNode& node, TiledTileset& ts, const std::string& base_dir);
    void load_tileset_image(TiledTileset& ts, const std::string& image_path);

    const TiledTileset* find_tileset_for_gid(const TiledMap& map, uint32_t gid) const;
    TiledTileset* find_tileset_mut(TiledMap& map, uint32_t gid);
    int resolve_animated_tile(const TiledTileset& ts, int local_id) const;
    SDL_Texture* lazy_load_tile(TiledTileset& ts, int local_id);

    void draw_tile_layer(TiledMap& map, const TiledTileLayer& layer, float cam_x, float cam_y);
    void draw_object_layer_impl(TiledMap& map, const TiledObjectLayer& layer, float cam_x, float cam_y);
    void draw_image_layer(const TiledMap& map, const TiledImageLayer& layer, float cam_x, float cam_y);
};

// Global instance
extern TiledMapSystem g_tiled;
#endif
