#pragma once

class Renderer;

class Widget {
protected:
    float x, y, width, height;
    bool visible;
    bool dirty;
    static constexpr int touch_buffer = 5;  // ← NEU! Fixed 5px buffer
    
public:
    Widget(float x, float y, float w, float h) 
        : x(x), y(y), width(w), height(h), visible(true), dirty(true) {}
    
    virtual ~Widget() = default;
    
    virtual void update(float dt) {}
    virtual void draw(Renderer& renderer) = 0;
    virtual bool handle_touch(int tx, int ty, bool down) { return false; }
    
    // ← NEU! Simple touch area mit 5px buffer
    bool is_in_touch_area(int tx, int ty) const {
        return visible && 
               tx >= x - touch_buffer && 
               tx < x + width + touch_buffer &&
               ty >= y - touch_buffer && 
               ty < y + height + touch_buffer;
    }
    
    // Für Checks ob innerhalb Widget (ohne buffer)
    bool is_inside(int tx, int ty) const {
        return visible && 
               tx >= x && tx <= x + width && 
               ty >= y && ty <= y + height;
    }
    
    void set_position(float nx, float ny) { 
        if (x != nx || y != ny) {
            x = nx; 
            y = ny;
            dirty = true;
        }
    }
    
    void set_size(float w, float h) {
        if (width != w || height != h) {
            width = w;
            height = h;
            dirty = true;
        }
    }
    
    void set_visible(bool v) { 
        if (visible != v) {
            visible = v;
            dirty = true;
        }
    }
    
    bool is_visible() const { return visible; }
    bool needs_redraw() const { return dirty; }
    void mark_clean() { dirty = false; }
    void mark_dirty() { dirty = true; }
    
    float get_x() const { return x; }
    float get_y() const { return y; }
    float get_width() const { return width; }
    float get_height() const { return height; }
};