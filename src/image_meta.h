// Image metadata on the bytes of a file: JPEG segments and PNG chunks are
// read, dropped and written without decoding the image, on every platform.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace jdb_image {

using Bytes = std::vector<uint8_t>;
using Fields = std::vector<std::pair<std::string, std::string>>;

enum Rc {
    RC_OK = 0,
    RC_WRITE = -1,
    RC_RECT = -2,
    RC_ARG = -3,
    RC_SOURCE = -6,
};

struct Meta {
    std::string format;              // "jpeg", "png", or empty
    int orientation = 0;             // 0 when the file holds none
    Fields texts;                    // EXIF text fields by their jdBasic names
    bool has_gps = false;
    double latitude = 0, longitude = 0;
    bool has_altitude = false;
    double altitude = 0;
    std::string xmp;
    bool iptc = false;
    bool has_comment = false;
    std::string comment;
    Fields png_text;                 // PNG text chunks, keyword to text
};

bool read_file(const std::string& path, Bytes& out);
bool write_file(const std::string& path, const Bytes& data);

// Fills m from a JPEG or PNG; false when the bytes are neither.
bool read_meta(const Bytes& file, Meta& m);

// The size as displayed (EXIF orientation applied), from the header of a
// JPEG, PNG, GIF or BMP; false when there is none to read.
bool display_size(const Bytes& file, int& width, int& height);

// Writes the sw x sh top-down 32-bit pixels `from` into `to` (as many) the
// way EXIF orientation 1 to 8 displays them; dw and dh get the displayed size.
void orient_pixels(const uint32_t* from, int sw, int sh, int orientation,
                   uint32_t* to, int& dw, int& dh);

// The orientation that shows a picture of `orientation` turned clockwise by
// `quarters` quarter turns.
int turned_orientation(int orientation, int quarters);

// The JPEG turned clockwise by `quarters` quarter turns without touching its
// pixels: only the EXIF orientation is written.
int rotate_jpeg(const Bytes& file, int quarters, Bytes& out);

// The EXIF orientation of a JPEG, 1 when it has none.
int jpeg_orientation(const Bytes& file);

// what is "all" or "gps".
int strip(const Bytes& file, const std::string& what, Bytes& out);

// Names: artist, copyright, description, software, datetime. "" removes.
int set_meta(const Bytes& file, const Fields& fields, Bytes& out);

}  // namespace jdb_image
