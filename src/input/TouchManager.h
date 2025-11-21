#pragma once

#include "TouchSlot.h"
#include <string>
#include <array>
#include <vector>
#include <memory>

class Widget;

class TouchManager {
private:
    int touch_fd;
    std::string touch_device_path;
    int current_slot;
    std::array<TouchSlot, 10> touch_slots;
    float scale;
    uint32_t screen_width;
    uint32_t screen_height;
    
    std::string autodetect_touch();
    void process_multitouch_event(const struct input_event& ev, 
                                  std::vector<std::unique_ptr<Widget>>& widgets);
    void process_complete_touch_frame(std::vector<std::unique_ptr<Widget>>& widgets);

public:
    TouchManager(float render_scale, uint32_t width, uint32_t height);
    ~TouchManager();
    
    bool initialize();
    bool process_events(std::vector<std::unique_ptr<Widget>>& widgets);
    bool has_active_touch() const;
};