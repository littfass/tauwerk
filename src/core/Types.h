#pragma once

#include <GLES2/gl2.h>

// Color struct
struct Color {
    float r, g, b, a;
    
    Color(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

// Glyph for font rendering
struct Glyph {
    GLuint texture_id;
    int width, height;
    int bearing_x, bearing_y;
    int advance;
};

// Slider behavior modes
enum class SliderMode {
    JUMP,        // Jump to touch position
    INCREMENTAL, // Relative movement from touch start
    SMOOTH       // Smooth interpolation to target
};