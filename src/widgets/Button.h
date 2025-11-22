#pragma once

#include "Widget.h"
#include "../core/Types.h"
#include <string>
#include <functional>

enum class ButtonMode {
    MOMENTARY,
    LATCH
};

class Renderer;

class Button : public Widget {
private:
    std::string text;
    bool is_pressed;
    bool latch_state;
    ButtonMode mode;
    Color bg_color;
    Color text_color;
    float dither_alpha;
    std::function<void()> on_click;
    
    // Multitouch: Track active fingers
    std::vector<int> active_touches;
    
public:
    Button(float x, float y, float w, float h, const std::string& txt = "", 
           ButtonMode mode = ButtonMode::MOMENTARY);
    
    void set_text(const std::string& t);
    void set_on_click(std::function<void()> callback);
    void set_mode(ButtonMode m);
    
    bool is_pressed_state() const { return is_pressed; }
    bool get_latch_state() const { return latch_state; }
    
    // ← NEU! Kann Widget freigegeben werden bei Leave?
    bool can_release_on_leave() const { return true; }
    
    void draw(Renderer& renderer) override;
    bool handle_touch(int tx, int ty, bool down, int touch_id) override;
};