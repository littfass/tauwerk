#include "Button.h"
#include "../core/Renderer.h"

Button::Button(float x, float y, float w, float h, const std::string& txt, ButtonMode m)
    : Widget(x, y, w, h), text(txt), is_pressed(false), latch_state(false),
      mode(m), bg_color(1, 1, 1, 1), text_color(0, 0, 0, 1), dither_alpha(0.133f) {}

void Button::set_text(const std::string& t) {
    if (t != text) {
        text = t;
        dirty = true;
    }
}

void Button::set_on_click(std::function<void()> callback) {
    on_click = callback;
}

void Button::set_mode(ButtonMode m) {
    mode = m;
    if (mode == ButtonMode::MOMENTARY) {
        latch_state = false;
    }
}

void Button::draw(Renderer& renderer) {
    if (!visible) return;
    
    bool show_pressed = false;
    
    if (mode == ButtonMode::MOMENTARY) {
        show_pressed = is_pressed;
    } else if (mode == ButtonMode::LATCH) {
        show_pressed = latch_state;
    }
    
    if (show_pressed) {
        renderer.draw_dithered(x, y, width, height, bg_color, dither_alpha);
    } else {
        renderer.draw_rect(x, y, width, height, bg_color);
    }
    
    // Draw text centered
    if (!text.empty()) {
        float text_x = x + width / 2 - text.length() * 8;
        float text_y = y + height / 2 + 8;
        renderer.draw_text(text, text_x, text_y, text_color);
    }
}

bool Button::handle_touch(int tx, int ty, bool down) {
    if (!visible) return false;
    
    if (mode == ButtonMode::MOMENTARY) {
        if (down && !is_pressed) {
            is_pressed = true;
            dirty = true;
            return true;
        }
        
        if (down && is_pressed) {
            return true;
        }
        
        if (!down && is_pressed) {
            is_pressed = false;
            dirty = true;
            
            if (is_inside(tx, ty) && on_click) {
                on_click();
            }
            return true;
        }
    } 
    else if (mode == ButtonMode::LATCH) {
        if (down && !is_pressed) {
            is_pressed = true;
            latch_state = !latch_state;
            dirty = true;
            
            if (on_click) {
                on_click();
            }
            return true;
        }
        
        if (down && is_pressed) {
            return true;
        }
        
        if (!down && is_pressed) {
            is_pressed = false;
            return true;
        }
    }
    
    return false;
}