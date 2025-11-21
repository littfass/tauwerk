#pragma once

#include "Widget.h"
#include <vector>
#include <memory>

class Renderer;
class Fader;

class Layout {
private:
    std::vector<std::unique_ptr<Widget>> widgets;
    
public:
    template<typename T, typename... Args>
    T* add_widget(Args&&... args) {
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = widget.get();
        widgets.push_back(std::move(widget));
        return ptr;
    }
    
    void update(float dt);
    void draw(Renderer& renderer);
    bool has_animation() const;
    
    std::vector<std::unique_ptr<Widget>>& get_widgets() {
        return widgets;
    }
};