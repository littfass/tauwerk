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
    std::cout << "🎨 Tauwerk UI starting..." << std::endl;
    
    // System setup
    BacklightController backlight;
    backlight.set_brightness(0);
    
    // Renderer
    Renderer renderer;
    if (!renderer.initialize()) {
        std::cerr << "❌ Renderer init failed!" << std::endl;
        return 1;
    }
    
    // Touch
    TouchManager touch(renderer.get_scale(), renderer.get_width(), renderer.get_height());
    if (!touch.initialize()) {
        std::cerr << "❌ Touch init failed!" << std::endl;
        return 1;
    }
    
    // UI Layout
    Layout ui;
    
    // 🎨 Create UI
    const float WIDGET_HEIGHT = 60;
    
    // Title Icon (\E803 in UTF-8) - Tauwerk Logo
    auto* title = ui.add_widget<Label>(50, 30, "\xEE\xA0\x83", 
        Color(1, 1, 1, 1), FontType::ICONS, 80);
    
    // Fader mit Label und integriertem Value-Display
    auto* fader = ui.add_widget<Fader>(50, 150, 700, WIDGET_HEIGHT);
    fader->set_name("Master Volume");
    fader->set_mode(SliderMode::SMOOTH);
    fader->set_smooth_speed(0.15f);
    fader->set_value(0.75f);
    
    // Buttons mit einheitlicher Höhe
    auto* play_button = ui.add_widget<Button>(50, 250, 150, WIDGET_HEIGHT, "PLAY");
    play_button->set_name("Transport");
    play_button->set_on_click([]() {
        // Play/Stop
    });

    auto* record_button = ui.add_widget<Button>(220, 250, 150, WIDGET_HEIGHT, "REC", ButtonMode::LATCH);
    record_button->set_name("Record");
    record_button->set_on_click([]() {
        // Record toggle
    });
    
    auto* sync_button = ui.add_widget<Button>(390, 250, 150, WIDGET_HEIGHT, "SYNC");
    sync_button->set_name("MIDI Sync");
    
    // Weitere Fader
    auto* tempo_fader = ui.add_widget<Fader>(50, 360, 340, WIDGET_HEIGHT);
    tempo_fader->set_name("Tempo");
    tempo_fader->set_value(0.5f);
    
    auto* swing_fader = ui.add_widget<Fader>(410, 360, 340, WIDGET_HEIGHT);
    swing_fader->set_name("Swing");
    swing_fader->set_value(0.5f);
    
    // Performance-Info
    auto* perf_label = ui.add_widget<Label>(650, 10, "16.6 ms", 
        Color(0.7f, 0.7f, 0.7f, 1), FontType::DEFAULT, 16);
    
    // Main Loop
    auto last_interaction_time = std::chrono::steady_clock::now();
    float avg_frame_time = 16.0f;
    bool high_fps_mode = false;
    const float IDLE_TIMEOUT = 1.0f;
    
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
    
    std::cout << "✅ Shutdown complete" << std::endl;
    return 0;
}