#pragma once

#include "Widget.h"
#include "../core/Types.h"

class Renderer;

class Fader : public Widget {
private:
    float progress;
    float target_progress;
    SliderMode mode;
    float smooth_speed;
    int touch_start_x;
    float value_at_touch_start;
    bool is_touched;
    Color bg_color;
    Color fill_color;
    float dither_alpha;
    
public:
    Fader(float x, float y, float w, float h);
    
    void set_mode(SliderMode m);
    void set_smooth_speed(float speed);
    
    float get_value() const { return progress; }
    void set_value(float v);
    
    bool is_animating() const;
    
    void update(float dt) override;
    void draw(Renderer& renderer) override;
    bool handle_touch(int tx, int ty, bool down) override;
};