#pragma once

#include "Widget.h"
#include "../core/Types.h"
#include <string>

class Renderer;

class Label : public Widget {
private:
    std::string text;
    Color color;
    FontType font_type;
    int font_size;
    
public:
    Label(float x, float y, const std::string& txt, 
          const Color& c = Color(1, 1, 1, 1),
          FontType font = FontType::DEFAULT,
          int size = 24);
    
    void set_text(const std::string& t);
    void set_color(const Color& c);
    void set_font(FontType font, int size);
    const std::string& get_text() const { return text; }
    
    void draw(Renderer& renderer) override;
};