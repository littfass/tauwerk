#include "Fader.h"
#include "../core/Renderer.h"
#include <algorithm>
#include <cmath>

Fader::Fader(float x, float y, float w, float h)
    : Widget(x, y, w, h), progress(0.5f), target_progress(0.5f),
      mode(SliderMode::JUMP), smooth_speed(0.15f),
      bg_color(1, 1, 1, 1), fill_color(1, 1, 1, 1), dither_alpha(0.133f),
      name(""), value_text(""), show_value(true) {}

void Fader::set_mode(SliderMode m) { 
    mode = m; 
}

void Fader::set_smooth_speed(float speed) { 
    smooth_speed = speed; 
}

void Fader::set_name(const std::string& n) {
    name = n;
    dirty = true;
}

void Fader::set_show_value(bool show) {
    show_value = show;
    dirty = true;
}

void Fader::set_value(float v) { 
    progress = std::clamp(v, 0.0f, 1.0f);
    target_progress = progress;
    dirty = true;
}

bool Fader::is_animating() const {
    if (mode == SliderMode::SMOOTH) {
        return std::abs(target_progress - progress) > 0.001f;
    }
    return false;
}

void Fader::update(float dt) {
    if (mode == SliderMode::SMOOTH) {
        float diff = target_progress - progress;
        if (std::abs(diff) > 0.001f) {
            progress += diff * smooth_speed;
            dirty = true;
            if (std::abs(diff) < 0.005f) {
                progress = target_progress;
            }
        }
    }
    
    // Update value text
    if (show_value) {
        int percentage = (int)(progress * 100);
        value_text = std::to_string(percentage) + "%";
    }
}

void Fader::draw(Renderer& renderer) {
    if (!visible) return;
    
    // Name Label (uppercase, 21px über Fader = 16px text height + 5px gap)
    if (!name.empty()) {
        std::string upper_name = name;
        for (char& c : upper_name) c = std::toupper(c);
        renderer.draw_text(upper_name, x, y - 21, Color(1, 1, 1, 1), FontType::DEFAULT, 16);
    }
    
    // Background (dithered)
    renderer.draw_dithered(x, y, width, height, bg_color, dither_alpha);
    
    // Value Label im Fader - immer weiß
    if (show_value && !value_text.empty()) {
        float text_x = x + 10;
        float text_y = y + (height - 52) / 2;
        
        renderer.draw_text(value_text, text_x, text_y, Color(1, 1, 1, 1), FontType::DIGITAL, 52);
    }
    
    // Fill: Schwarzes Rect mit Invert-BlendMode
    // → Invertiert dunklen Dither zu hellem Fill
    // → Invertiert weißen Text zu schwarzem Text
    float fill_width = width * progress;
    if (fill_width > 0) {
        renderer.draw_rect_inverted(x, y, fill_width, height);
    }
}

bool Fader::handle_touch(int tx, int ty, bool down, int touch_id) {
    if (!visible) return false;
    
    int active_finger = get_active_finger();
    
    // ═══════════════════════════════════════════════════════════
    // TOUCH DOWN - Add finger to stack
    // ═══════════════════════════════════════════════════════════
    if (down && finger_states.find(touch_id) == finger_states.end()) {
        // New finger! Add to stack
        finger_stack.push_back(touch_id);
        
        TouchFingerState state;
        state.touch_id = touch_id;
        state.start_x = tx;
        state.last_x = tx;  // Initialize last position
        state.value_at_start = progress;
        finger_states[touch_id] = state;
        
        // Only process if this is now the active finger
        if (get_active_finger() == touch_id) {
            switch (mode) {
                case SliderMode::JUMP:
                    if (tx < x) tx = x;
                    if (tx > x + width) tx = x + width;
                    progress = (float)(tx - x) / width;
                    progress = std::clamp(progress, 0.0f, 1.0f);
                    target_progress = progress;
                    dirty = true;
                    break;
                    
                case SliderMode::INCREMENTAL:
                    // Already stored in state
                    break;
                    
                case SliderMode::SMOOTH:
                    if (tx < x) tx = x;
                    if (tx > x + width) tx = x + width;
                    target_progress = (float)(tx - x) / width;
                    target_progress = std::clamp(target_progress, 0.0f, 1.0f);
                    dirty = true;
                    break;
            }
        }
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════
    // TOUCH MOVE - Only process if this is the active finger
    // ═══════════════════════════════════════════════════════════
    if (down && finger_states.find(touch_id) != finger_states.end()) {
        // Update last known position for this finger
        auto& state = finger_states[touch_id];
        state.last_x = tx;
        
        // Only the top finger (last in stack) controls the value
        if (get_active_finger() == touch_id) {
            switch (mode) {
                case SliderMode::JUMP:
                    progress = (float)(tx - x) / width;
                    progress = std::clamp(progress, 0.0f, 1.0f);
                    target_progress = progress;
                    dirty = true;
                    break;
                    
                case SliderMode::INCREMENTAL:
                    {
                        int delta = tx - state.start_x;
                        progress = state.value_at_start + (float)delta / width;
                        progress = std::clamp(progress, 0.0f, 1.0f);
                        target_progress = progress;
                        dirty = true;
                    }
                    break;
                    
                case SliderMode::SMOOTH:
                    target_progress = (float)(tx - x) / width;
                    target_progress = std::clamp(target_progress, 0.0f, 1.0f);
                    dirty = true;
                    break;
            }
        }
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════
    // TOUCH UP - Remove finger from stack, activate previous
    // ═══════════════════════════════════════════════════════════
    if (!down && finger_states.find(touch_id) != finger_states.end()) {
        // Remove from stack
        auto it = std::find(finger_stack.begin(), finger_stack.end(), touch_id);
        if (it != finger_stack.end()) {
            finger_stack.erase(it);
        }
        finger_states.erase(touch_id);
        
        // If there's a new active finger, immediately apply its position!
        int new_active = get_active_finger();
        if (new_active != -1) {
            auto& state = finger_states[new_active];
            int new_tx = state.last_x;
            
            switch (mode) {
                case SliderMode::JUMP:
                    progress = (float)(new_tx - x) / width;
                    progress = std::clamp(progress, 0.0f, 1.0f);
                    target_progress = progress;
                    dirty = true;
                    break;
                    
                case SliderMode::INCREMENTAL:
                    // Re-anchor to current value to prevent jumps
                    state.value_at_start = progress;
                    state.start_x = new_tx;
                    break;
                    
                case SliderMode::SMOOTH:
                    target_progress = (float)(new_tx - x) / width;
                    target_progress = std::clamp(target_progress, 0.0f, 1.0f);
                    dirty = true;
                    break;
            }
        }
        
        return true;
    }
    
    return false;
}