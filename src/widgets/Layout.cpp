#include "Layout.h"
#include "Fader.h"
#include "../core/Renderer.h"

void Layout::update(float dt) {
    for (auto& w : widgets) {
        w->update(dt);
    }
}

void Layout::draw(Renderer& renderer) {
    for (auto& w : widgets) {
        w->draw(renderer);
    }
}

bool Layout::has_animation() const {
    for (const auto& w : widgets) {
        // Check if it's a Fader and animating
        if (auto* fader = dynamic_cast<Fader*>(w.get())) {
            if (fader->is_animating()) return true;
        }
    }
    return false;
}