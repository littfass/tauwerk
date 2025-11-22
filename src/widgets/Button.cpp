#include "Button.h"
#include "../core/Renderer.h"
#include <algorithm>
#include <iostream>

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

bool Button::handle_touch(int tx, int ty, bool down, int touch_id) {
    if (!visible) return false;
    
    // Check if this touch is already tracked
    auto it = std::find(active_touches.begin(), active_touches.end(), touch_id);
    bool is_tracked = (it != active_touches.end());
    
    // DEBUG: Print touch info
    std::cout << "BTN[" << text << "] touch: (" << tx << "," << ty << ") down=" << down 
              << " id=" << touch_id << " tracked=" << is_tracked 
              << " stack_before=" << active_touches.size();
    
    // Remember state BEFORE modification
    bool was_pressed = !active_touches.empty();
    
    // ═══════════════════════════════════════════════════════════
    // UPDATE FINGER STACK
    // ═══════════════════════════════════════════════════════════
    if (down && !is_tracked) {
        // TOUCH DOWN or MOVE - Add finger to stack
        active_touches.push_back(touch_id);
    } else if (!down && is_tracked) {
        // TOUCH UP - Remove finger from stack
        active_touches.erase(it);
    } else if (down && is_tracked) {
        // TOUCH MOVE - Already tracked, nothing to do
        return true;
    } else {
        // Not our touch
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════
    // STATE CHANGE DETECTION
    // ═══════════════════════════════════════════════════════════
    bool is_now_pressed = !active_touches.empty();
    
    if (was_pressed != is_now_pressed) {
        dirty = true;
        std::cout << " -> STATE_CHANGE: " << (is_now_pressed ? "PRESSED" : "RELEASED") << std::endl;
        
        if (is_now_pressed) {
            // Transition: RELEASED → PRESSED (onPress)
            is_pressed = true;
            
            if (mode == ButtonMode::LATCH) {
                latch_state = !latch_state;
                if (on_click) {
                    on_click();
                }
            }
        } else {
            // Transition: PRESSED → RELEASED (onRelease)
            is_pressed = false;
            
            if (mode == ButtonMode::MOMENTARY && on_click) {
                on_click();
            }
        }
    } else {
        std::cout << " -> no state change" << std::endl;
    }
    
    return true;
}