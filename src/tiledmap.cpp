#ifdef GFX
#include "tiledmap.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>

// ── Minimal XML Parser ────────────────────────────────────────────
// Handles the subset of XML used in Tiled TMX/TSX files:
//   - Elements with attributes, children, text content
//   - Self-closing tags (<tag />)
//   - <?xml ...?> declarations (skipped)
//   - No CDATA, DTD, namespaces, entities (except &amp; &lt; &gt; &quot; &apos;)

static std::string xml_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '&') {
            if (s.compare(i, 4, "&lt;") == 0)   { out += '<';  i += 4; }
            else if (s.compare(i, 4, "&gt;") == 0)   { out += '>';  i += 4; }
            else if (s.compare(i, 5, "&amp;") == 0)  { out += '&';  i += 5; }
            else if (s.compare(i, 6, "&quot;") == 0) { out += '"';  i += 6; }
            else if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 6; }
            else { out += s[i++]; }
        } else {
            out += s[i++];
        }
    }
    return out;
}

static void skip_ws(const std::string& s, size_t& p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
        ++p;
}

static std::string read_name(const std::string& s, size_t& p) {
    size_t start = p;
    while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\n' && s[p] != '\r'
           && s[p] != '>' && s[p] != '/' && s[p] != '=')
        ++p;
    return s.substr(start, p - start);
}

static std::string read_quoted(const std::string& s, size_t& p) {
    if (p >= s.size()) return "";
    char q = s[p++]; // ' or "
    size_t start = p;
    while (p < s.size() && s[p] != q) ++p;
    std::string val = s.substr(start, p - start);
    if (p < s.size()) ++p; // skip closing quote
    return xml_unescape(val);
}

static XmlNode parse_element(const std::string& s, size_t& p);

static std::vector<XmlNode> parse_children(const std::string& s, size_t& p, const std::string& parent_tag) {
    std::vector<XmlNode> children;
    std::string close_tag = "</" + parent_tag;
    while (p < s.size()) {
        skip_ws(s, p);
        if (p >= s.size()) break;
        // Check for closing tag
        if (s.compare(p, close_tag.size(), close_tag) == 0) {
            // Found closing tag - skip past >
            p += close_tag.size();
            skip_ws(s, p);
            if (p < s.size() && s[p] == '>') ++p;
            return children;
        }
        if (s[p] == '<') {
            // Skip comments <!-- -->
            if (s.compare(p, 4, "<!--") == 0) {
                auto end = s.find("-->", p + 4);
                p = (end == std::string::npos) ? s.size() : end + 3;
                continue;
            }
            // Skip <?...?>
            if (s.compare(p, 2, "<?") == 0) {
                auto end = s.find("?>", p + 2);
                p = (end == std::string::npos) ? s.size() : end + 2;
                continue;
            }
            children.push_back(parse_element(s, p));
        } else {
            // Text content - read until next '<'
            size_t start = p;
            while (p < s.size() && s[p] != '<') ++p;
            // Ignore pure whitespace text nodes, but keep CSV data
            std::string text = s.substr(start, p - start);
            if (!text.empty()) {
                // Store as text in the last child or create a text node
                // For TMX, text content is CSV data inside <data> tags
                // We'll handle this in parse_element
            }
        }
    }
    return children;
}

static XmlNode parse_element(const std::string& s, size_t& p) {
    XmlNode node;
    if (p >= s.size() || s[p] != '<') return node;
    ++p; // skip '<'
    skip_ws(s, p);

    node.tag = read_name(s, p);

    // Parse attributes
    while (p < s.size()) {
        skip_ws(s, p);
        if (p >= s.size()) break;
        if (s[p] == '/') {
            ++p;
            if (p < s.size() && s[p] == '>') ++p;
            return node; // self-closing
        }
        if (s[p] == '>') {
            ++p;
            break; // end of opening tag
        }
        // Read attribute
        XmlAttribute attr;
        attr.name = read_name(s, p);
        if (attr.name.empty()) { ++p; continue; }
        skip_ws(s, p);
        if (p < s.size() && s[p] == '=') {
            ++p;
            skip_ws(s, p);
            if (p < s.size() && (s[p] == '"' || s[p] == '\''))
                attr.value = read_quoted(s, p);
        }
        node.attributes.push_back(attr);
    }

    // Read content (text + children) until closing tag
    std::string close_tag = "</" + node.tag;
    // Collect text content
    std::string text_buf;
    while (p < s.size()) {
        skip_ws(s, p);
        if (p >= s.size()) break;
        if (s.compare(p, close_tag.size(), close_tag) == 0) {
            p += close_tag.size();
            skip_ws(s, p);
            if (p < s.size() && s[p] == '>') ++p;
            break;
        }
        if (s[p] == '<') {
            if (s.compare(p, 4, "<!--") == 0) {
                auto end = s.find("-->", p + 4);
                p = (end == std::string::npos) ? s.size() : end + 3;
                continue;
            }
            node.children.push_back(parse_element(s, p));
        } else {
            // Text content
            size_t start = p;
            while (p < s.size() && s[p] != '<') ++p;
            text_buf += s.substr(start, p - start);
        }
    }
    // Trim text
    size_t ts = text_buf.find_first_not_of(" \t\n\r");
    size_t te = text_buf.find_last_not_of(" \t\n\r");
    if (ts != std::string::npos)
        node.text = text_buf.substr(ts, te - ts + 1);
    return node;
}

XmlNode xml_parse(const std::string& xml_content) {
    XmlNode root;
    root.tag = "__root__";
    size_t p = 0;
    while (p < xml_content.size()) {
        skip_ws(xml_content, p);
        if (p >= xml_content.size()) break;
        if (xml_content[p] == '<') {
            // Skip <?xml ...?>
            if (xml_content.compare(p, 2, "<?") == 0) {
                auto end = xml_content.find("?>", p + 2);
                p = (end == std::string::npos) ? xml_content.size() : end + 2;
                continue;
            }
            // Skip comments
            if (xml_content.compare(p, 4, "<!--") == 0) {
                auto end = xml_content.find("-->", p + 4);
                p = (end == std::string::npos) ? xml_content.size() : end + 3;
                continue;
            }
            root.children.push_back(parse_element(xml_content, p));
        } else {
            ++p;
        }
    }
    return root;
}

// XmlNode helpers
std::string XmlNode::attr(const std::string& name, const std::string& def) const {
    for (auto& a : attributes)
        if (a.name == name) return a.value;
    return def;
}

int XmlNode::attr_int(const std::string& name, int def) const {
    for (auto& a : attributes)
        if (a.name == name) {
            try { return std::stoi(a.value); } catch (...) { return def; }
        }
    return def;
}

float XmlNode::attr_float(const std::string& name, float def) const {
    for (auto& a : attributes)
        if (a.name == name) {
            try { return std::stof(a.value); } catch (...) { return def; }
        }
    return def;
}

const XmlNode* XmlNode::child(const std::string& tag) const {
    for (auto& c : children)
        if (c.tag == tag) return &c;
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::children_by_tag(const std::string& tag) const {
    std::vector<const XmlNode*> result;
    for (auto& c : children)
        if (c.tag == tag) result.push_back(&c);
    return result;
}

bool XmlNode::has_attr(const std::string& name) const {
    for (auto& a : attributes)
        if (a.name == name) return true;
    return false;
}

// ── File reading helper ───────────────────────────────────────────

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── CSV parsing ───────────────────────────────────────────────────

static std::vector<uint32_t> parse_csv(const std::string& csv) {
    std::vector<uint32_t> result;
    result.reserve(1024);
    size_t p = 0;
    while (p < csv.size()) {
        // Skip whitespace and commas
        while (p < csv.size() && (csv[p] == ',' || csv[p] == ' ' || csv[p] == '\n' || csv[p] == '\r' || csv[p] == '\t'))
            ++p;
        if (p >= csv.size()) break;
        // Read number
        uint32_t val = 0;
        bool neg = false;
        if (csv[p] == '-') { neg = true; ++p; }
        while (p < csv.size() && csv[p] >= '0' && csv[p] <= '9') {
            val = val * 10 + (csv[p] - '0');
            ++p;
        }
        if (neg) val = (uint32_t)(-(int32_t)val);
        result.push_back(val);
    }
    return result;
}

// ── Path resolution ───────────────────────────────────────────────

static std::string resolve_path(const std::string& base_dir, const std::string& relative) {
    std::filesystem::path base(base_dir);
    std::filesystem::path rel(relative);
    std::filesystem::path result = base / rel;
    // Normalize (resolve .. and .)
    try {
        result = std::filesystem::weakly_canonical(result);
    } catch (...) {
        // fallback: just use the combined path
    }
    return result.string();
}

// ── TiledMapSystem ────────────────────────────────────────────────

TiledMapSystem g_tiled;

TiledMapSystem::TiledMapSystem() {}

TiledMapSystem::~TiledMapSystem() {
    shutdown();
}

void TiledMapSystem::init(SDL_Renderer* main_renderer) {
    renderer = main_renderer;
}

void TiledMapSystem::shutdown() {
    for (auto& [name, map] : loaded_maps) {
        for (auto& ts : map.tilesets) {
            if (ts.texture) SDL_DestroyTexture(ts.texture);
            for (auto& [id, tex] : ts.tile_textures)
                if (tex) SDL_DestroyTexture(tex);
        }
        for (auto& [lname, il] : map.image_layers) {
            if (il.texture) SDL_DestroyTexture(il.texture);
        }
    }
    loaded_maps.clear();
}

void TiledMapSystem::free_map(const std::string& map_name) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return;
    for (auto& ts : it->second.tilesets) {
        if (ts.texture) SDL_DestroyTexture(ts.texture);
        for (auto& [id, tex] : ts.tile_textures)
            if (tex) SDL_DestroyTexture(tex);
    }
    for (auto& [lname, il] : it->second.image_layers) {
        if (il.texture) SDL_DestroyTexture(il.texture);
    }
    loaded_maps.erase(it);
}

bool TiledMapSystem::has_map(const std::string& map_name) const {
    return loaded_maps.count(map_name) > 0;
}

// ── Tileset parsing ───────────────────────────────────────────────

void TiledMapSystem::parse_tileset_node(const XmlNode& node, TiledTileset& ts, const std::string& base_dir) {
    ts.name = node.attr("name");
    ts.tile_width = node.attr_int("tilewidth", 32);
    ts.tile_height = node.attr_int("tileheight", 32);
    ts.tile_count = node.attr_int("tilecount", 0);
    ts.columns = node.attr_int("columns", 0);
    ts.is_collection = (ts.columns == 0);

    // Atlas image (single image for entire tileset)
    auto* img_node = node.child("image");
    if (img_node && !ts.is_collection) {
        std::string img_path = resolve_path(base_dir, img_node->attr("source"));
        load_tileset_image(ts, img_path);
    }

    // Per-tile data: animations, per-tile images (collection), collision shapes
    for (auto* tile_node : node.children_by_tag("tile")) {
        int local_id = tile_node->attr_int("id");

        // Per-tile image (collection tileset) - store path for lazy loading
        auto* tile_img = tile_node->child("image");
        if (tile_img && ts.is_collection) {
            std::string img_path = resolve_path(base_dir, tile_img->attr("source"));
            ts.tile_img_paths[local_id] = img_path;
            ts.tile_img_w[local_id] = tile_img->attr_int("width", 32);
            ts.tile_img_h[local_id] = tile_img->attr_int("height", 32);
        }

        // Animation
        auto* anim_node = tile_node->child("animation");
        if (anim_node) {
            TileAnimation anim;
            for (auto* frame_node : anim_node->children_by_tag("frame")) {
                TileAnimFrame f;
                f.local_tile_id = frame_node->attr_int("tileid");
                f.duration_ms = frame_node->attr_int("duration", 100);
                anim.frames.push_back(f);
            }
            if (!anim.frames.empty())
                ts.animations[local_id] = anim;
        }

        // Collision shapes (objectgroup inside tile)
        auto* objgroup = tile_node->child("objectgroup");
        if (objgroup) {
            std::vector<SDL_FRect> rects;
            for (auto* obj : objgroup->children_by_tag("object")) {
                SDL_FRect r;
                r.x = obj->attr_float("x");
                r.y = obj->attr_float("y");
                r.w = obj->attr_float("width");
                r.h = obj->attr_float("height");
                rects.push_back(r);
            }
            if (!rects.empty())
                ts.per_tile_collisions[local_id] = rects;
        }
    }
}

bool TiledMapSystem::parse_tileset_tsx(const std::string& tsx_path, TiledTileset& ts) {
    std::string content = read_file(tsx_path);
    if (content.empty()) {
        SDL_Log("Error: Could not read TSX file: %s", tsx_path.c_str());
        return false;
    }
    XmlNode root = xml_parse(content);
    auto* tileset_node = root.child("tileset");
    if (!tileset_node) {
        SDL_Log("Error: No <tileset> element in TSX: %s", tsx_path.c_str());
        return false;
    }
    std::string base_dir = std::filesystem::path(tsx_path).parent_path().string();
    parse_tileset_node(*tileset_node, ts, base_dir);
    return true;
}

void TiledMapSystem::load_tileset_image(TiledTileset& ts, const std::string& image_path) {
    ts.texture = IMG_LoadTexture(renderer, image_path.c_str());
    if (!ts.texture) {
        SDL_Log("Error loading tileset image: %s (%s)", image_path.c_str(), SDL_GetError());
    }
}

// ── Map loading ───────────────────────────────────────────────────

bool TiledMapSystem::load_map(const std::string& map_name, const std::string& filename) {
    if (!renderer) return false;

    std::string content = read_file(filename);
    if (content.empty()) {
        SDL_Log("Error: Could not read TMX file: %s", filename.c_str());
        return false;
    }

    XmlNode root = xml_parse(content);
    auto* map_node = root.child("map");
    if (!map_node) {
        SDL_Log("Error: No <map> element in TMX: %s", filename.c_str());
        return false;
    }

    std::string map_dir = std::filesystem::path(filename).parent_path().string();

    TiledMap new_map;
    new_map.name = map_name;
    new_map.width = map_node->attr_int("width");
    new_map.height = map_node->attr_int("height");
    new_map.tile_width = map_node->attr_int("tilewidth", 32);
    new_map.tile_height = map_node->attr_int("tileheight", 32);

    // 0. Parse map-level properties
    auto* map_props = map_node->child("properties");
    if (map_props) {
        for (auto* prop : map_props->children_by_tag("property")) {
            std::string key = prop->attr("name");
            // Trim leading/trailing whitespace from key
            size_t ks = key.find_first_not_of(" \t");
            size_t ke = key.find_last_not_of(" \t");
            if (ks != std::string::npos) key = key.substr(ks, ke - ks + 1);
            std::string val = prop->attr("value");
            if (val.empty()) val = prop->text; // some properties use text content
            new_map.properties[key] = val;
        }
    }

    // 1. Load tilesets
    for (auto* ts_node : map_node->children_by_tag("tileset")) {
        TiledTileset ts;
        ts.first_gid = ts_node->attr_int("firstgid", 1);

        if (ts_node->has_attr("source")) {
            // External TSX file
            std::string tsx_path = resolve_path(map_dir, ts_node->attr("source"));
            if (!parse_tileset_tsx(tsx_path, ts)) {
                SDL_Log("Warning: Failed to load TSX: %s", tsx_path.c_str());
                continue;
            }
        } else {
            // Embedded tileset
            parse_tileset_node(*ts_node, ts, map_dir);
        }
        new_map.tilesets.push_back(std::move(ts));
    }

    // Sort tilesets by first_gid (ascending) for correct lookup
    std::sort(new_map.tilesets.begin(), new_map.tilesets.end(),
              [](const TiledTileset& a, const TiledTileset& b) { return a.first_gid < b.first_gid; });

    // 2. Load layers (in order)
    for (auto& child : map_node->children) {
        if (child.tag == "layer") {
            TiledTileLayer tl;
            tl.name = child.attr("name");
            tl.id = child.attr_int("id");
            tl.width = child.attr_int("width", new_map.width);
            tl.height = child.attr_int("height", new_map.height);
            tl.visible = child.attr("visible", "1") != "0";
            tl.opacity = child.attr_float("opacity", 1.0f);

            auto* data_node = child.child("data");
            if (data_node) {
                std::string encoding = data_node->attr("encoding", "csv");
                if (encoding == "csv") {
                    tl.data = parse_csv(data_node->text);
                }
                // TODO: base64 encoding support if needed
            }
            new_map.tile_layers[tl.name] = std::move(tl);
            new_map.layer_order.push_back({TiledLayerRef::TILE, child.attr("name")});
        }
        else if (child.tag == "objectgroup") {
            TiledObjectLayer ol;
            ol.name = child.attr("name");
            ol.id = child.attr_int("id");
            ol.visible = child.attr("visible", "1") != "0";
            ol.opacity = child.attr_float("opacity", 1.0f);

            for (auto* obj_node : child.children_by_tag("object")) {
                TiledMapObject mo;
                mo.id = obj_node->attr_int("id");
                mo.name = obj_node->attr("name");
                mo.type = obj_node->attr("type");
                mo.x = obj_node->attr_float("x");
                mo.y = obj_node->attr_float("y");
                mo.width = obj_node->attr_float("width");
                mo.height = obj_node->attr_float("height");
                if (obj_node->has_attr("gid"))
                    mo.gid = (uint32_t)obj_node->attr_int("gid");

                // Object properties
                auto* props_node = obj_node->child("properties");
                if (props_node) {
                    for (auto* prop : props_node->children_by_tag("property")) {
                        std::string pkey = prop->attr("name");
                        size_t ps = pkey.find_first_not_of(" \t");
                        size_t pe = pkey.find_last_not_of(" \t");
                        if (ps != std::string::npos) pkey = pkey.substr(ps, pe - ps + 1);
                        mo.properties[pkey] = prop->attr("value");
                    }
                }
                ol.objects.push_back(std::move(mo));
            }
            new_map.object_layers[ol.name] = std::move(ol);
            new_map.layer_order.push_back({TiledLayerRef::OBJECT, child.attr("name")});
        }
        else if (child.tag == "imagelayer") {
            TiledImageLayer il;
            il.name = child.attr("name");
            il.id = child.attr_int("id");
            il.visible = child.attr("visible", "1") != "0";
            il.opacity = child.attr_float("opacity", 1.0f);
            il.x = child.attr_float("offsetx", 0);
            il.y = child.attr_float("offsety", 0);

            auto* img_node = child.child("image");
            if (img_node) {
                std::string img_path = resolve_path(map_dir, img_node->attr("source"));
                il.texture = IMG_LoadTexture(renderer, img_path.c_str());
                if (il.texture) {
                    float w, h;
                    SDL_GetTextureSize(il.texture, &w, &h);
                    il.w = w;
                    il.h = h;
                }
            }
            new_map.image_layers[il.name] = std::move(il);
            new_map.layer_order.push_back({TiledLayerRef::IMAGE, child.attr("name")});
        }
    }

    int total_anims = 0;
    int total_tile_textures = 0;
    for (auto& ts : new_map.tilesets) {
        total_anims += (int)ts.animations.size();
        total_tile_textures += (int)ts.tile_textures.size();
        if (!ts.animations.empty())
            SDL_Log("  Tileset '%s': %d animations, %d tile textures, collection=%d",
                    ts.name.c_str(), (int)ts.animations.size(), (int)ts.tile_textures.size(), ts.is_collection);
    }
    SDL_Log("Loaded Tiled map '%s': %dx%d tiles, %d tilesets (%d anims, %d collection tiles), %d tile layers, %d object layers",
            map_name.c_str(), new_map.width, new_map.height,
            (int)new_map.tilesets.size(), total_anims, total_tile_textures,
            (int)new_map.tile_layers.size(),
            (int)new_map.object_layers.size());

    loaded_maps[map_name] = std::move(new_map);
    return true;
}

// ── Animation update ──────────────────────────────────────────────

void TiledMapSystem::update(float dt) {
    float dt_ms = dt * 1000.0f;
    for (auto& [name, map] : loaded_maps) {
        for (auto& ts : map.tilesets) {
            for (auto& [local_id, anim] : ts.animations) {
                if (anim.frames.empty()) continue;
                anim.elapsed_ms += dt_ms;
                float frame_dur = (float)anim.frames[anim.current_frame].duration_ms;
                while (anim.elapsed_ms >= frame_dur) {
                    anim.elapsed_ms -= frame_dur;
                    anim.current_frame = (anim.current_frame + 1) % (int)anim.frames.size();
                    frame_dur = (float)anim.frames[anim.current_frame].duration_ms;
                }
            }
        }
    }
}

// ── Tile resolution (animation) ───────────────────────────────────

int TiledMapSystem::resolve_animated_tile(const TiledTileset& ts, int local_id) const {
    auto it = ts.animations.find(local_id);
    if (it != ts.animations.end() && !it->second.frames.empty()) {
        return it->second.frames[it->second.current_frame].local_tile_id;
    }
    return local_id;
}

// ── Tileset lookup ────────────────────────────────────────────────

const TiledTileset* TiledMapSystem::find_tileset_for_gid(const TiledMap& map, uint32_t gid) const {
    const TiledTileset* result = nullptr;
    for (auto& ts : map.tilesets) {
        if ((int)gid >= ts.first_gid)
            result = &ts;
        else
            break;
    }
    return result;
}

TiledTileset* TiledMapSystem::find_tileset_mut(TiledMap& map, uint32_t gid) {
    TiledTileset* result = nullptr;
    for (auto& ts : map.tilesets) {
        if ((int)gid >= ts.first_gid)
            result = &ts;
        else
            break;
    }
    return result;
}

// ── Lazy-load collection tile texture ─────────────────────────

SDL_Texture* TiledMapSystem::lazy_load_tile(TiledTileset& ts, int local_id) {
    auto tex_it = ts.tile_textures.find(local_id);
    if (tex_it != ts.tile_textures.end()) return tex_it->second;
    // Not loaded yet - try to load from stored path
    auto path_it = ts.tile_img_paths.find(local_id);
    if (path_it == ts.tile_img_paths.end()) return nullptr;
    SDL_Texture* tex = IMG_LoadTexture(renderer, path_it->second.c_str());
    if (tex) {
        ts.tile_textures[local_id] = tex;
    }
    return tex;
}

// ── Drawing ───────────────────────────────────────────────────────

void TiledMapSystem::draw_tile_layer(TiledMap& map, const TiledTileLayer& layer, float cam_x, float cam_y) {
    if (!layer.visible) return;

    // Snap camera to integer pixels to prevent sub-pixel gaps between tiles
    cam_x = std::floor(cam_x);
    cam_y = std::floor(cam_y);

    int tw = map.tile_width;
    int th = map.tile_height;

    // Viewport culling: only draw visible tiles
    int screen_w = 0, screen_h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &screen_w, &screen_h);

    int start_col = std::max(0, (int)(cam_x / tw));
    int start_row = std::max(0, (int)(cam_y / th));
    int end_col = std::min(layer.width, (int)((cam_x + screen_w) / tw) + 2);
    int end_row = std::min(layer.height, (int)((cam_y + screen_h) / th) + 2);

    // Set layer opacity
    // We'll apply opacity per-tile via texture alpha mod

    for (int y = start_row; y < end_row; ++y) {
        for (int x = start_col; x < end_col; ++x) {
            int idx = y * layer.width + x;
            if (idx < 0 || idx >= (int)layer.data.size()) continue;

            uint32_t raw_gid = layer.data[idx];
            if (raw_gid == 0) continue;

            // Flip flags (top 3 bits)
            bool flip_h = (raw_gid & 0x80000000) != 0;
            bool flip_v = (raw_gid & 0x40000000) != 0;
            bool flip_d = (raw_gid & 0x20000000) != 0;
            uint32_t gid = raw_gid & 0x1FFFFFFF;

            TiledTileset* ts = find_tileset_mut(map, gid);
            if (!ts) continue;

            int local_id = (int)gid - ts->first_gid;
            // Resolve animation
            int render_id = resolve_animated_tile(*ts, local_id);

            SDL_FRect dst = {
                (float)(x * tw) - cam_x,
                (float)(y * th) - cam_y,
                (float)tw,
                (float)th
            };

            SDL_FlipMode flip = SDL_FLIP_NONE;
            if (flip_h) flip = (SDL_FlipMode)(flip | SDL_FLIP_HORIZONTAL);
            if (flip_v) flip = (SDL_FlipMode)(flip | SDL_FLIP_VERTICAL);

            if (ts->is_collection) {
                // Collection tileset: lazy-load per-tile texture
                SDL_Texture* tex = lazy_load_tile(*ts, render_id);
                if (!tex) continue;
                if (layer.opacity < 1.0f)
                    SDL_SetTextureAlphaMod(tex, (Uint8)(layer.opacity * 255));
                if (flip_h || flip_v || flip_d) {
                    float angle = flip_d ? 90.0f : 0.0f;
                    SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, angle, nullptr, flip);
                } else {
                    SDL_RenderTexture(renderer, tex, nullptr, &dst);
                }
                if (layer.opacity < 1.0f)
                    SDL_SetTextureAlphaMod(tex, 255);
            } else {
                // Atlas tileset
                if (!ts->texture || ts->columns <= 0) continue;
                int src_x = (render_id % ts->columns) * ts->tile_width;
                int src_y = (render_id / ts->columns) * ts->tile_height;
                SDL_FRect src = {
                    (float)src_x, (float)src_y,
                    (float)ts->tile_width, (float)ts->tile_height
                };
                if (layer.opacity < 1.0f)
                    SDL_SetTextureAlphaMod(ts->texture, (Uint8)(layer.opacity * 255));
                if (flip_h || flip_v || flip_d) {
                    float angle = flip_d ? 90.0f : 0.0f;
                    SDL_RenderTextureRotated(renderer, ts->texture, &src, &dst, angle, nullptr, flip);
                } else {
                    SDL_RenderTexture(renderer, ts->texture, &src, &dst);
                }
                if (layer.opacity < 1.0f)
                    SDL_SetTextureAlphaMod(ts->texture, 255);
            }
        }
    }
}

void TiledMapSystem::draw_object_layer_impl(TiledMap& map, const TiledObjectLayer& layer, float cam_x, float cam_y) {
    if (!layer.visible) return;

    for (auto& obj : layer.objects) {
        if (obj.gid == 0) continue; // Only draw tile-objects (those with a gid)

        uint32_t raw_gid = obj.gid;
        bool flip_h = (raw_gid & 0x80000000) != 0;
        bool flip_v = (raw_gid & 0x40000000) != 0;
        bool flip_d = (raw_gid & 0x20000000) != 0;
        uint32_t gid = raw_gid & 0x1FFFFFFF;

        TiledTileset* ts = find_tileset_mut(map, gid);
        if (!ts) continue;

        int local_id = (int)gid - ts->first_gid;
        int render_id = resolve_animated_tile(*ts, local_id);

        // Object position: in Tiled, y is bottom-left for tile objects
        float draw_x = obj.x - cam_x;
        float draw_y = obj.y - obj.height - cam_y;
        SDL_FRect dst = { draw_x, draw_y, obj.width, obj.height };

        SDL_FlipMode flip = SDL_FLIP_NONE;
        if (flip_h) flip = (SDL_FlipMode)(flip | SDL_FLIP_HORIZONTAL);
        if (flip_v) flip = (SDL_FlipMode)(flip | SDL_FLIP_VERTICAL);

        if (ts->is_collection) {
            SDL_Texture* tex = lazy_load_tile(*ts, render_id);
            if (!tex) continue;
            if (layer.opacity < 1.0f)
                SDL_SetTextureAlphaMod(tex, (Uint8)(layer.opacity * 255));
            if (flip_h || flip_v || flip_d) {
                float angle = flip_d ? 90.0f : 0.0f;
                SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, angle, nullptr, flip);
            } else {
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
            }
            if (layer.opacity < 1.0f)
                SDL_SetTextureAlphaMod(tex, 255);
        } else {
            if (!ts->texture || ts->columns <= 0) continue;
            int src_x = (render_id % ts->columns) * ts->tile_width;
            int src_y = (render_id / ts->columns) * ts->tile_height;
            SDL_FRect src = {
                (float)src_x, (float)src_y,
                (float)ts->tile_width, (float)ts->tile_height
            };
            if (layer.opacity < 1.0f)
                SDL_SetTextureAlphaMod(ts->texture, (Uint8)(layer.opacity * 255));
            if (flip_h || flip_v || flip_d) {
                float angle = flip_d ? 90.0f : 0.0f;
                SDL_RenderTextureRotated(renderer, ts->texture, &src, &dst, angle, nullptr, flip);
            } else {
                SDL_RenderTexture(renderer, ts->texture, &src, &dst);
            }
            if (layer.opacity < 1.0f)
                SDL_SetTextureAlphaMod(ts->texture, 255);
        }
    }
}

void TiledMapSystem::draw_image_layer(const TiledMap& map, const TiledImageLayer& layer, float cam_x, float cam_y) {
    if (!layer.visible || !layer.texture) return;
    SDL_FRect dst = { layer.x - cam_x, layer.y - cam_y, layer.w, layer.h };
    if (layer.opacity < 1.0f)
        SDL_SetTextureAlphaMod(layer.texture, (Uint8)(layer.opacity * 255));
    SDL_RenderTexture(renderer, layer.texture, nullptr, &dst);
    if (layer.opacity < 1.0f)
        SDL_SetTextureAlphaMod(layer.texture, 255);
}

void TiledMapSystem::draw_all(const std::string& map_name, float cam_x, float cam_y) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return;
    auto& map = it->second;

    for (auto& ref : map.layer_order) {
        switch (ref.type) {
            case TiledLayerRef::TILE: {
                auto tl = map.tile_layers.find(ref.name);
                if (tl != map.tile_layers.end())
                    draw_tile_layer(map, tl->second, cam_x, cam_y);
                break;
            }
            case TiledLayerRef::OBJECT: {
                auto ol = map.object_layers.find(ref.name);
                if (ol != map.object_layers.end())
                    draw_object_layer_impl(map, ol->second, cam_x, cam_y);
                break;
            }
            case TiledLayerRef::IMAGE: {
                auto il = map.image_layers.find(ref.name);
                if (il != map.image_layers.end())
                    draw_image_layer(map, il->second, cam_x, cam_y);
                break;
            }
        }
    }
}

void TiledMapSystem::draw_layer(const std::string& map_name, const std::string& layer_name, float cam_x, float cam_y) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return;
    auto& map = it->second;

    auto tl = map.tile_layers.find(layer_name);
    if (tl != map.tile_layers.end()) {
        draw_tile_layer(map, tl->second, cam_x, cam_y);
        return;
    }
    auto ol = map.object_layers.find(layer_name);
    if (ol != map.object_layers.end()) {
        draw_object_layer_impl(map, ol->second, cam_x, cam_y);
        return;
    }
    auto il = map.image_layers.find(layer_name);
    if (il != map.image_layers.end()) {
        draw_image_layer(map, il->second, cam_x, cam_y);
    }
}

void TiledMapSystem::draw_object_layer(const std::string& map_name, const std::string& layer_name, float cam_x, float cam_y) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return;
    auto ol = it->second.object_layers.find(layer_name);
    if (ol != it->second.object_layers.end())
        draw_object_layer_impl(it->second, ol->second, cam_x, cam_y);
}

// ── Queries ───────────────────────────────────────────────────────

std::vector<TiledMapObject> TiledMapSystem::get_objects(const std::string& map_name, const std::string& layer_name) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return {};
    auto ol = it->second.object_layers.find(layer_name);
    if (ol == it->second.object_layers.end()) return {};
    return ol->second.objects;
}

std::vector<std::string> TiledMapSystem::get_layer_names(const std::string& map_name) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return {};
    std::vector<std::string> names;
    for (auto& ref : it->second.layer_order)
        names.push_back(ref.name);
    return names;
}

void TiledMapSystem::get_map_pixel_size(const std::string& map_name, int& w, int& h) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) { w = 0; h = 0; return; }
    w = it->second.width * it->second.tile_width;
    h = it->second.height * it->second.tile_height;
}

void TiledMapSystem::get_tile_size(const std::string& map_name, int& tw, int& th) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) { tw = 0; th = 0; return; }
    tw = it->second.tile_width;
    th = it->second.tile_height;
}

// ── Collision ─────────────────────────────────────────────────────

bool TiledMapSystem::check_collision(const std::string& map_name, const std::string& layer_name,
                                     float rect_x, float rect_y, float rect_w, float rect_h) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return false;
    auto& map = it->second;

    auto tl = map.tile_layers.find(layer_name);
    if (tl == map.tile_layers.end()) return false;
    auto& layer = tl->second;

    int tw = map.tile_width;
    int th = map.tile_height;

    int start_col = std::max(0, (int)(rect_x / tw));
    int end_col = std::min(layer.width - 1, (int)((rect_x + rect_w) / tw));
    int start_row = std::max(0, (int)(rect_y / th));
    int end_row = std::min(layer.height - 1, (int)((rect_y + rect_h) / th));

    for (int row = start_row; row <= end_row; ++row) {
        for (int col = start_col; col <= end_col; ++col) {
            int idx = row * layer.width + col;
            if (idx < 0 || idx >= (int)layer.data.size()) continue;
            uint32_t gid = layer.data[idx] & 0x1FFFFFFF;
            if (gid == 0) continue;

            const TiledTileset* ts = find_tileset_for_gid(map, gid);
            if (!ts) continue;
            int local_id = (int)gid - ts->first_gid;

            float tile_wx = (float)(col * tw);
            float tile_wy = (float)(row * th);

            // Custom per-tile collision shapes
            auto coll_it = ts->per_tile_collisions.find(local_id);
            if (coll_it != ts->per_tile_collisions.end()) {
                for (auto& cr : coll_it->second) {
                    SDL_FRect world_r = { tile_wx + cr.x, tile_wy + cr.y, cr.w, cr.h };
                    SDL_FRect sprite_r = { rect_x, rect_y, rect_w, rect_h };
                    if (SDL_HasRectIntersectionFloat(&sprite_r, &world_r))
                        return true;
                }
            } else {
                // Full tile collision
                SDL_FRect tile_r = { tile_wx, tile_wy, (float)tw, (float)th };
                SDL_FRect sprite_r = { rect_x, rect_y, rect_w, rect_h };
                if (SDL_HasRectIntersectionFloat(&sprite_r, &tile_r))
                    return true;
            }
        }
    }
    return false;
}

std::map<std::string, std::string> TiledMapSystem::get_map_properties(const std::string& map_name) const {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return {};
    return it->second.properties;
}

int TiledMapSystem::get_tile_at(const std::string& map_name, const std::string& layer_name,
                                float pixel_x, float pixel_y) {
    auto it = loaded_maps.find(map_name);
    if (it == loaded_maps.end()) return 0;
    auto& map = it->second;
    auto tl = map.tile_layers.find(layer_name);
    if (tl == map.tile_layers.end()) return 0;
    auto& layer = tl->second;

    int col = (int)(pixel_x / map.tile_width);
    int row = (int)(pixel_y / map.tile_height);
    if (col < 0 || col >= layer.width || row < 0 || row >= layer.height) return 0;
    return (int)(layer.data[row * layer.width + col] & 0x1FFFFFFF);
}

#endif
