#pragma once

#include "Widget.h"
#include "../core/Types.h"
#include <string>

class Renderer;

class Label : public Widget {
private:
    std::string text;
    Color color;
    
public:
    Label(float x, float y, const std::string& txt, const Color& c = Color(1, 1, 1, 1));
    
    void set_text(const std::string& t);
    void set_color(const Color& c);
    const std::string& get_text() const { return text; }
    
    void draw(Renderer& renderer) override;
};