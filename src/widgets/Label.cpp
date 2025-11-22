#include "Label.h"
#include "../core/Renderer.h"

Label::Label(float x, float y, const std::string& txt, const Color& c,
             FontType font, int size)
    : Widget(x, y, 0, 0), text(txt), color(c), font_type(font), font_size(size) {}

void Label::set_text(const std::string& t) {
    if (t != text) {
        text = t;
        dirty = true;
    }
}

void Label::set_color(const Color& c) {
    if (color.r != c.r || color.g != c.g || color.b != c.b || color.a != c.a) {
        color = c;
        dirty = true;
    }
}

void Label::set_font(FontType font, int size) {
    if (font != font_type || size != font_size) {
        font_type = font;
        font_size = size;
        dirty = true;
    }
}

void Label::draw(Renderer& renderer) {
    if (!visible) return;
    renderer.draw_text(text, x, y, color, font_type, font_size);
}