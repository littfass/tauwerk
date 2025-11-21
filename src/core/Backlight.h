#pragma once

#include <string>
#include <fstream>
#include <algorithm>

class BacklightController {
private:
    std::string brightness_path;
    int original_brightness;
    int max_brightness;

public:
    BacklightController() : original_brightness(-1), max_brightness(255) {
        const char* paths[] = {
            "/sys/class/backlight/rpi_backlight/brightness",
            "/sys/class/backlight/10-0045/brightness",
            "/sys/class/backlight/backlight/brightness"
        };

        for (const char* path : paths) {
            std::ifstream test(path);
            if (test.good()) {
                brightness_path = path;
                break;
            }
        }

        if (!brightness_path.empty()) {
            std::string max_path = brightness_path;
            size_t pos = max_path.rfind("brightness");
            if (pos != std::string::npos) {
                max_path.replace(pos, 10, "max_brightness");
                std::ifstream max_file(max_path);
                if (max_file.good()) max_file >> max_brightness;
            }
            std::ifstream in(brightness_path);
            if (in.good()) in >> original_brightness;
        }
    }

    void set_brightness(int value) {
        if (brightness_path.empty()) return;
        value = std::clamp(value, 0, max_brightness);
        std::ofstream out(brightness_path);
        if (out.good()) out << value;
    }

    void restore() {
        if (original_brightness >= 0) set_brightness(original_brightness);
    }

    ~BacklightController() { 
        restore(); 
    }
};