#include "TouchManager.h"
#include "../widgets/Widget.h"
#include "../widgets/Button.h"
#include "../widgets/Fader.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <cstring>

TouchManager::TouchManager(float render_scale, uint32_t width, uint32_t height) 
    : touch_fd(-1), current_slot(0), scale(render_scale),
      screen_width(width), screen_height(height) {}

TouchManager::~TouchManager() {
    if (touch_fd >= 0) close(touch_fd);
}

std::string TouchManager::autodetect_touch() {
    for (int i = 0; i < 10; i++) {
        std::string path = "/dev/input/event" + std::to_string(i);
        int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            unsigned long bitmask[EV_MAX/8 + 1];
            memset(bitmask, 0, sizeof(bitmask));
            
            if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bitmask) >= 0) {
                if (bitmask[EV_ABS/8] & (1 << (EV_ABS % 8))) {
                    unsigned long abs_bitmask[ABS_MAX/8 + 1];
                    memset(abs_bitmask, 0, sizeof(abs_bitmask));
                    
                    if (ioctl(fd, EVIOCGBIT(EV_ABS, ABS_MAX), abs_bitmask) >= 0) {
                        if ((abs_bitmask[ABS_MT_POSITION_X/8] & (1 << (ABS_MT_POSITION_X % 8))) ||
                            (abs_bitmask[ABS_X/8] & (1 << (ABS_X % 8)))) {
                            close(fd);
                            return path;
                        }
                    }
                }
            }
            close(fd);
        }
    }
    return "/dev/input/event3";
}

bool TouchManager::initialize() {
    touch_device_path = autodetect_touch();
    touch_fd = open(touch_device_path.c_str(), O_RDONLY | O_NONBLOCK);
    return touch_fd >= 0;
}

void TouchManager::process_multitouch_event(const struct input_event& ev, 
                                            std::vector<std::unique_ptr<Widget>>& widgets) {
    if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
        process_complete_touch_frame(widgets);
    }
    else if (ev.type == EV_ABS) {
        switch (ev.code) {
            case ABS_MT_SLOT:
                current_slot = ev.value;
                break;
            case ABS_MT_TRACKING_ID:
                if (current_slot < touch_slots.size()) {
                    if (ev.value == -1) {
                        touch_slots[current_slot].tracking_id = -1;
                        touch_slots[current_slot].pending_release = true;
                    } else {
                        touch_slots[current_slot].tracking_id = ev.value;
                        touch_slots[current_slot].pending_touch = true;
                    }
                }
                break;
            case ABS_MT_POSITION_X:
                if (current_slot < touch_slots.size()) {
                    touch_slots[current_slot].x = ev.value;
                    touch_slots[current_slot].has_position = true;
                }
                break;
            case ABS_MT_POSITION_Y:
                if (current_slot < touch_slots.size()) {
                    touch_slots[current_slot].y = ev.value;
                    touch_slots[current_slot].has_position = true;
                }
                break;
        }
    }
}

void TouchManager::process_complete_touch_frame(std::vector<std::unique_ptr<Widget>>& widgets) {
    for (auto& ts : touch_slots) {
        int scaled_x = (int)(ts.x * scale);
        int scaled_y = (int)(ts.y * scale);
        
        // Track letzte gültige Position im Screen
        bool in_bounds = (scaled_x >= 0 && scaled_x < (int)screen_width &&
                         scaled_y >= 0 && scaled_y < (int)screen_height);
        
        if (in_bounds) {
            ts.last_valid_x = scaled_x;
            ts.last_valid_y = scaled_y;
        }
        
        // ═══════════════════════════════════════════════════════════
        // TOUCH DOWN
        // ═══════════════════════════════════════════════════════════
        if (ts.pending_touch && ts.has_position) {
            ts.active = true;
            ts.pending_touch = false;
            ts.has_position = false;
            
            if (!ts.down_sent && ts.reserved_widget == nullptr) {
                ts.down_sent = true;
                
                // ← NEU! Simple 5px buffer check
                for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
                    if ((*it)->is_in_touch_area(scaled_x, scaled_y)) {
                        if ((*it)->handle_touch(scaled_x, scaled_y, true)) {
                            ts.reserved_widget = it->get();
                            break;
                        }
                    }
                }
            }
        }
        // ═══════════════════════════════════════════════════════════
        // TOUCH RELEASE
        // ═══════════════════════════════════════════════════════════
        else if (ts.pending_release && ts.tracking_id == -1) {
            if (ts.active && ts.reserved_widget) {
                ts.reserved_widget->handle_touch(ts.last_valid_x, ts.last_valid_y, false);
                ts.reserved_widget = nullptr;
            }
            ts.active = false;
            ts.down_sent = false;
            ts.pending_release = false;
            ts.has_position = false;
        }
        // ═══════════════════════════════════════════════════════════
        // TOUCH MOVE
        // ═══════════════════════════════════════════════════════════
        else if (ts.active && ts.has_position) {
            
            // Kein Widget? Slide-to-Activate!
            if (ts.reserved_widget == nullptr) {
                for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
                    if ((*it)->is_in_touch_area(scaled_x, scaled_y)) {
                        if ((*it)->handle_touch(scaled_x, scaled_y, true)) {
                            ts.reserved_widget = it->get();
                            break;
                        }
                    }
                }
            } 
            // Widget bereits reserviert!
            else {
                bool in_touch_area = ts.reserved_widget->is_in_touch_area(scaled_x, scaled_y);
                
                if (!in_touch_area) {
                    // ← Verlassen! Button vs Fader check
                    Button* btn = dynamic_cast<Button*>(ts.reserved_widget);
                    
                    if (btn) {
                        // Button: Release & freigeben
                        ts.reserved_widget->handle_touch(ts.last_valid_x, ts.last_valid_y, false);
                        ts.reserved_widget = nullptr;
                    } else {
                        // Fader: Weiter senden (clippt intern)
                        ts.reserved_widget->handle_touch(scaled_x, scaled_y, true);
                    }
                } else {
                    // Innerhalb - normal senden
                    ts.reserved_widget->handle_touch(scaled_x, scaled_y, true);
                }
            }
            
            ts.has_position = false;
        }
    }
}

bool TouchManager::process_events(std::vector<std::unique_ptr<Widget>>& widgets) {
    struct input_event ev;
    bool had_events = false;
    while (read(touch_fd, &ev, sizeof(ev)) > 0) {
        process_multitouch_event(ev, widgets);
        had_events = true;
    }
    return had_events;
}

bool TouchManager::has_active_touch() const {
    for (const auto& ts : touch_slots) {
        if (ts.active) return true;
    }
    return false;
}