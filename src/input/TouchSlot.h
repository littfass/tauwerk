#pragma once

class Widget;

struct TouchSlot {
    int tracking_id;
    int last_tracking_id;  // Store ID before release
    int x, y;
    bool active;
    bool down_sent;
    bool has_position;
    bool pending_touch;
    bool pending_release;
    Widget* reserved_widget;
    int last_valid_x;  // Für Release außerhalb Screen
    int last_valid_y;
    
    TouchSlot() 
        : tracking_id(-1), last_tracking_id(-1), x(0), y(0), active(false), 
          down_sent(false), has_position(false),
          pending_touch(false), pending_release(false),
          reserved_widget(nullptr),
          last_valid_x(0), last_valid_y(0) {}
};