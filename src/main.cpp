#include <iostream>
#include <chrono>
#include <thread>

#include "core/Backlight.h"
#include "core/Renderer.h"
#include "core/Types.h"
#include "widgets/Label.h"
#include "widgets/Fader.h"
#include "widgets/Button.h"
#include "widgets/Layout.h"
#include "input/TouchManager.h"

int main() {
    std::cout << "🎨 GPU-Accelerated UI - Extended Touch Areas!" << std::endl;
    
    // System setup
    BacklightController backlight;
    backlight.set_brightness(0);
    
    // Renderer
    Renderer renderer;
    if (!renderer.initialize()) {
        std::cerr << "❌ Renderer init failed!" << std::endl;
        return 1;
    }
    
    // Touch - ← NEU! Mit Screen-Dimensionen
    TouchManager touch(renderer.get_scale(), renderer.get_width(), renderer.get_height());
    if (!touch.initialize()) {
        std::cerr << "⚠️  Touch init failed" << std::endl;
    }
    
    // UI Layout
    Layout ui;
    
    // 🎨 Create UI
    auto* title = ui.add_widget<Label>(50, 100, "Extended Touch Areas!", Color(1, 1, 1, 1));
    
    auto* fader = ui.add_widget<Fader>(100, 240, 600, 80);
    fader->set_mode(SliderMode::SMOOTH);
    fader->set_smooth_speed(0.15f);
    
    auto* value_label = ui.add_widget<Label>(370, 190, "50%", Color(1, 1, 1, 1));
    
    auto* perf_label = ui.add_widget<Label>(50, 380, "16.6 ms", Color(0.7f, 0.7f, 0.7f, 1));
    
    auto* mode_label = ui.add_widget<Label>(50, 430, "Touch anywhere!", Color(1, 1, 1, 1));
    
    // Test Button
    auto* test_button = ui.add_widget<Button>(100, 340, 200, 60, "Test Button");
    test_button->set_on_click([]() {
        std::cout << "🔘 Button clicked!" << std::endl;
    });

    auto* latch_button = ui.add_widget<Button>(350, 340, 200, 60, "LATCH", ButtonMode::LATCH);
    latch_button->set_on_click([]() {
        std::cout << "🔘 Latch toggled!" << std::endl;
    });
    
    std::cout << "\n🎮 Extended Touch Areas aktiv!" << std::endl;
    std::cout << "🎯 Touch überall → Widgets reagieren in ihrer Area!" << std::endl;
    std::cout << "🖐️  Drag über Screen-Rand → Fader bewegt sich weiter!" << std::endl;
    
    // Main Loop
    auto last_interaction_time = std::chrono::steady_clock::now();
    float avg_frame_time = 16.0f;
    bool high_fps_mode = false;
    const float IDLE_TIMEOUT = 1.0f;
    
    int last_percentage = -1;
    
    while (true) {
        auto frame_start = std::chrono::steady_clock::now();
        
        // Process Touch
        bool had_touch = touch.process_events(ui.get_widgets());
        if (had_touch || touch.has_active_touch()) {
            last_interaction_time = frame_start;
        }
        
        // Update Widgets
        ui.update(0.016f);
        
        // Check if animating
        bool is_animating = ui.has_animation();
        if (is_animating) {
            last_interaction_time = frame_start;
        }
        
        // Adaptive FPS
        auto idle_time = std::chrono::duration<float>(
            frame_start - last_interaction_time).count();
        high_fps_mode = (idle_time < IDLE_TIMEOUT) || had_touch || is_animating;
        
        // Update UI Values
        int percentage = (int)(fader->get_value() * 100);
        if (percentage != last_percentage) {
            value_label->set_text(std::to_string(percentage) + "%");
            last_percentage = percentage;
        }
        
        // Render
        renderer.begin_frame();
        ui.draw(renderer);
        renderer.end_frame();
        
        // Frame Timing
        auto frame_end = std::chrono::steady_clock::now();
        float frame_time = std::chrono::duration<float, std::milli>(
            frame_end - frame_start).count();
        
        avg_frame_time = avg_frame_time * 0.95f + frame_time * 0.05f;
        
        // Update perf label
        char perf_text[64];
        snprintf(perf_text, sizeof(perf_text), "%.1f ms (%s)", 
                avg_frame_time, high_fps_mode ? "60 FPS" : "10 FPS");
        perf_label->set_text(perf_text);
        perf_label->set_color(Color(
            high_fps_mode ? 0.5f : 0.7f,
            high_fps_mode ? 1.0f : 0.7f,
            0.7f, 1.0f
        ));
        
        // Adaptive Frame Limiting
        if (!high_fps_mode) {
            float target_frame_time = 100.0f;  // 10 FPS
            float remaining = target_frame_time - frame_time;
            if (remaining > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds((int)remaining));
            }
        }
    }
    
    std::cout << "\n✅ Complete!" << std::endl;
    return 0;
}