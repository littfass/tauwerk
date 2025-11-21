#include "Fader.h"
#include "../core/Renderer.h"
#include <algorithm>
#include <cmath>

Fader::Fader(float x, float y, float w, float h)
    : Widget(x, y, w, h), progress(0.5f), target_progress(0.5f),
      mode(SliderMode::JUMP), smooth_speed(0.15f), 
      is_touched(false), touch_start_x(0), value_at_touch_start(0.0f),
      bg_color(1, 1, 1, 1), fill_color(1, 1, 1, 1), dither_alpha(0.133f) {}

void Fader::set_mode(SliderMode m) { 
    mode = m; 
}

void Fader::set_smooth_speed(float speed) { 
    smooth_speed = speed; 
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
}

void Fader::draw(Renderer& renderer) {
    if (!visible) return;
    
    // Background (dithered)
    renderer.draw_dithered(x, y, width, height, bg_color, dither_alpha);
    
    // Fill
    float fill_width = width * progress;
    if (fill_width > 0) {
        renderer.draw_rect(x, y, fill_width, height, fill_color);
    }
}

bool Fader::handle_touch(int tx, int ty, bool down) {
    if (!visible) return false;
    
    // ← NEU! Touch Down - Initialisierung
    if (down && !is_touched) {
        is_touched = true;
        
        switch (mode) {
            case SliderMode::JUMP:
                // Clamp to widget bounds für initialen Wert
                if (tx < x) tx = x;
                if (tx > x + width) tx = x + width;
                progress = (float)(tx - x) / width;
                progress = std::clamp(progress, 0.0f, 1.0f);
                target_progress = progress;
                dirty = true;
                break;
                
            case SliderMode::INCREMENTAL:
                touch_start_x = tx;
                value_at_touch_start = progress;
                break;
                
            case SliderMode::SMOOTH:
                if (tx < x) tx = x;
                if (tx > x + width) tx = x + width;
                target_progress = (float)(tx - x) / width;
                target_progress = std::clamp(target_progress, 0.0f, 1.0f);
                dirty = true;
                break;
        }
        return true;
    }
    
    // ← NEU! Touch Move - UNBEGRENZT (auch außerhalb Widget/Screen!)
    if (down && is_touched) {
        switch (mode) {
            case SliderMode::JUMP:
                // ← NEU! Keine Bounds-Checks mehr - funktioniert überall!
                progress = (float)(tx - x) / width;
                progress = std::clamp(progress, 0.0f, 1.0f);
                target_progress = progress;
                dirty = true;
                break;
                
            case SliderMode::INCREMENTAL:
                {
                    int delta = tx - touch_start_x;
                    progress = value_at_touch_start + (float)delta / width;
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
        return true;
    }
    
    // ← NEU! Touch Up - Egal wo!
    if (!down && is_touched) {
        is_touched = false;
        return true;
    }
    
    return false;
}