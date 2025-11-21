#pragma once

struct TouchArea {
    float x, y, width, height;
    
    TouchArea() : x(0), y(0), width(0), height(0) {}
    TouchArea(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}
    
    bool contains(int px, int py) const {
        return px >= x && px <= x + width && 
               py >= y && py <= y + height;
    }
    
    // Calculate distance to point (for debugging/priority)
    float distance_to(int px, int py) const {
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        float dx = px - cx;
        float dy = py - cy;
        return dx * dx + dy * dy;  // Squared distance (faster, no sqrt needed)
    }
};