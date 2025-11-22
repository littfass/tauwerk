#pragma once

#include "Widget.h"
#include "../core/Types.h"
#include <vector>
#include <map>
#include <string>

class Renderer;

struct TouchFingerState {
    int touch_id;
    int start_x;
    int last_x;  // Track last known position
    float value_at_start;
};

class Fader : public Widget {
private:
    float progress;
    float target_progress;
    SliderMode mode;
    float smooth_speed;
    Color bg_color;
    Color fill_color;
    float dither_alpha;
    
    // Label
    std::string name;
    std::string value_text;
    bool show_value;
    
    // Multitouch: Stack of active fingers (last = top)
    std::vector<int> finger_stack;
    std::map<int, TouchFingerState> finger_states;
    
public:
    Fader(float x, float y, float w, float h);
    
    void set_mode(SliderMode m);
    void set_smooth_speed(float speed);
    void set_name(const std::string& n);
    void set_show_value(bool show);
    
    float get_value() const { return progress; }
    void set_value(float v);
    const std::string& get_name() const { return name; }
    
    bool is_animating() const;
    
    void update(float dt) override;
    void draw(Renderer& renderer) override;
    bool handle_touch(int tx, int ty, bool down, int touch_id) override;
    
private:
    int get_active_finger() const { 
        return finger_stack.empty() ? -1 : finger_stack.back(); 
    }
};