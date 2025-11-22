#pragma once

#include <GLES2/gl2.h>
#include <map>

// Color struct
struct Color {
    float r, g, b, a;
    
    Color(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

// Font types
enum class FontType {
    DEFAULT,  // DejaVuSans - UI Text
    DIGITAL,  // DS Digital Bold - Numerics
    ICONS     // Tauwerk Icons
};

// Glyph for font rendering
struct Glyph {
    GLuint texture_id;
    int width, height;
    int bearing_x, bearing_y;
    int advance;
};

// Font metrics for proper text positioning
struct FontMetrics {
    int ascender;   // Max height above baseline
    int descender;  // Max depth below baseline (negative)
    int line_height;
};

// Font cache key
struct FontCacheKey {
    FontType type;
    int size;
    
    bool operator<(const FontCacheKey& other) const {
        if (type != other.type) return type < other.type;
        return size < other.size;
    }
};

// Slider behavior modes
enum class SliderMode {
    JUMP,        // Jump to touch position
    INCREMENTAL, // Relative movement from touch start
    SMOOTH       // Smooth interpolation to target
};