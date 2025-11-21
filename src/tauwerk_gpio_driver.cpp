// tauwerk_gpio_driver.cpp - MIT INI PARSING
#include <iostream>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <csignal>
#include <cstdlib>

class TauwerkGPIODriver {
private:
    // [Alle structs und Variablen gleich wie vorher...]
    struct GPIOHandle {
        int fd;
        struct gpiohandle_request request;
    };

    struct EncoderState {
        int last_a;
        int last_b;
        int value;
        int direction;
        std::chrono::steady_clock::time_point last_time;
        int pin_a;
        int pin_b;
    };

    struct ButtonState {
        int last_state;
        std::chrono::steady_clock::time_point last_time;
        int pin;
    };

    std::atomic<bool> running{true};
    std::unordered_map<int, GPIOHandle> gpio_handles;
    std::unordered_map<int, EncoderState> encoders;
    std::unordered_map<int, ButtonState> buttons;
    int shm_fd;
    int* shared_buffer;
    std::atomic<int> write_index{0};
    const int BUFFER_SIZE = 256;
    const int DEBOUNCE_MS = 5;

public:
    TauwerkGPIODriver() : shm_fd(-1), shared_buffer(nullptr) {}
    
    bool initialize() {
        setup_shared_memory();
        
        // ✅ JETZT MIT INI KONFIGURATION
        if (!load_ini_config()) {
            std::cout << "❌ Using fallback hardware configuration" << std::endl;
            // Fallback
            setup_encoder("select", 4, 12);
            setup_button("push", 16);
            setup_button("back", 20);
            setup_button("confirm", 21);
        }

        std::cout << "☰ Tauwerk GPIO Driver initialized (INI config)" << std::endl;
        return true;
    }

    // ✅ NEUE METHODE: INI KONFIGURATION LADEN
    bool load_ini_config() {
      std::ifstream config_file("/home/tauwerk/config/hardware.ini");
      if (!config_file.is_open()) return false;
      
      std::string line;
      bool found_config = false;
      
      while (std::getline(config_file, line)) {
        // Trim
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        // Skip comments/empty/sections
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string val = line.substr(eq_pos + 1);
        
        // Trim
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);
        
        // Encoder: controller.encoder.name = pin1,pin2
        size_t encoder_pos = key.find(".encoder.");
        if (encoder_pos != std::string::npos && encoder_pos > 0) {
          std::string controller_id = key.substr(0, encoder_pos);
          std::string encoder_name = key.substr(encoder_pos + 9);
          
          size_t comma = val.find(',');
          if (comma != std::string::npos) {
            int pin_a = std::stoi(val.substr(0, comma));
            int pin_b = std::stoi(val.substr(comma + 1));
            std::string full_name = controller_id + "_" + encoder_name;
            setup_encoder(full_name, pin_a, pin_b);
            found_config = true;
          }
        }
        // Button: controller.buttons.name = pin  
        else if (key.find(".buttons.") != std::string::npos) {
          size_t buttons_pos = key.find(".buttons.");
          if (buttons_pos > 0) {
            std::string controller_id = key.substr(0, buttons_pos);
            std::string button_name = key.substr(buttons_pos + 9);
            
            int pin = std::stoi(val);
            std::string full_name = controller_id + "_" + button_name;
            setup_button(full_name, pin);
            found_config = true;
          }
        }
      }
      
      return found_config;
    }
    
    // [Rest der Methoden UNVERÄNDERT...]
    void setup_encoder(const std::string& name, int pin_a, int pin_b) {
        setup_gpio_input(name + "_a", pin_a);
        setup_gpio_input(name + "_b", pin_b);
        encoders[pin_a] = {0, 0, 0, 0, std::chrono::steady_clock::now(), pin_a, pin_b};
        std::cout << "╰ Encoder " << name << " pins: " << pin_a << ", " << pin_b << std::endl;
    }
    
    void setup_button(const std::string& name, int pin) {
        setup_gpio_input(name, pin);
        buttons[pin] = {0, std::chrono::steady_clock::now(), pin};
        std::cout << "╰ Button " << name << " pin: " << pin << std::endl;
    }
    
    void setup_shared_memory() {
        shm_fd = shm_open("/tauwerk_gpio", O_CREAT | O_RDWR, 0666);
        if (shm_fd < 0) {
            std::cerr << "❌ Failed to create shared memory" << std::endl;
            return;
        }
        
        size_t total_size = BUFFER_SIZE * 4 * sizeof(int) + 4 * sizeof(int);
        ftruncate(shm_fd, total_size);
        
        shared_buffer = (int*)mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shared_buffer == MAP_FAILED) {
            std::cerr << "❌ Failed to mmap shared memory" << std::endl;
            return;
        }
        
        memset(shared_buffer, 0, total_size);
        shared_buffer[BUFFER_SIZE * 4 + 2] = 0x54415557;
        
        std::cout << "☰ Shared Memory initialized (" << total_size << " bytes)" << std::endl;
    }
    
    // [setup_gpio_input, read_gpio, write_event, poll_encoders, poll_buttons, run, stop... alles gleich]
    void setup_gpio_input(const std::string& name, int pin) {
        struct gpiohandle_request req;
        memset(&req, 0, sizeof(req));
        
        req.lineoffsets[0] = pin;
        req.lines = 1;
        req.flags = GPIOHANDLE_REQUEST_INPUT;
        strcpy(req.consumer_label, name.c_str());
        
        int chip_fd = open("/dev/gpiochip0", O_RDWR);
        if (chip_fd < 0) {
            std::cerr << "     Failed to open gpiochip0 for pin " << pin << std::endl;
            return;
        }
        
        if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
            std::cerr << "     Failed to setup GPIO pin " << pin << std::endl;
            close(chip_fd);
            return;
        }
        
        close(chip_fd);
        gpio_handles[pin] = {req.fd, req};
        std::cout << "     GPIO pin " << pin << " (" << name << ") configured" << std::endl;
    }
    
    int read_gpio(int pin) {
        auto it = gpio_handles.find(pin);
        if (it == gpio_handles.end()) return -1;
        
        struct gpiohandle_data data;
        memset(&data, 0, sizeof(data));
        
        if (ioctl(it->second.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
            return -1;
        }
        
        return data.values[0];
    }
    
    void write_event(int type, int pin, int value) {
        int index = write_index.load();
        int buffer_offset = index * 4;
        
        shared_buffer[buffer_offset] = type;
        shared_buffer[buffer_offset + 1] = pin;
        shared_buffer[buffer_offset + 2] = value;
        shared_buffer[buffer_offset + 3] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        const char* type_str = (type == 0) ? "ENCODER" : "BUTTON";
        std::cout << "╰ " << type_str << " (" << pin 
                  << ") " << value << " | MEM " << index << std::endl;
        
        int new_index = (index + 1) % BUFFER_SIZE;
        write_index.store(new_index);
        shared_buffer[BUFFER_SIZE * 4] = new_index;
    }
    
    void poll_encoders() {
        for (auto& pair : encoders) {
            auto& state = pair.second;
            int a_val = read_gpio(state.pin_a);
            int b_val = read_gpio(state.pin_b);
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.last_time).count();

            if (a_val != state.last_a) {
                if ((a_val == 1) && (b_val != 1)) {
                    if((state.direction == -1) && elapsed < 20) continue;
                    state.value += 1;
                    state.direction = 1;
                    state.last_time = now;
                    write_event(0, state.pin_a, 1);
                }
            }
            if (b_val != state.last_b) {
                if ((b_val == 1) && (a_val != 1)) {
                    if((state.direction == 1) && elapsed < 20) continue;
                    state.value -= 1;
                    state.direction = -1;
                    state.last_time = now;
                    write_event(0, state.pin_a, -1);
                }
            }
            
            state.last_a = a_val;
            state.last_b = b_val;
        }
    }
    
    void poll_buttons() {
        for (auto& pair : buttons) {
            auto& state = pair.second;
            int current_state = read_gpio(state.pin);
            if (current_state < 0) continue;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.last_time).count();

            if (elapsed < DEBOUNCE_MS) continue;

            int inverted_state = (current_state == 0) ? 1 : 0;

            if (state.last_state != inverted_state) {
                write_event(1, state.pin, inverted_state);
                state.last_state = inverted_state;
                state.last_time = now;
            }
        }
    }
    
    void run() {
        std::cout << "▶ Tauwerk GPIO Driver started..." << std::endl;
        std::cout << "☰ Polling at 1kHz - Waiting for hardware events..." << std::endl;
        
        while (running) {
            poll_encoders();
            poll_buttons();
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }
    }
    
    void stop() {
        running = false;
        
        for (auto& handle : gpio_handles) {
            close(handle.second.fd);
        }
        
        if (shared_buffer) {
            munmap(shared_buffer, 4096);
        }
        if (shm_fd >= 0) {
            close(shm_fd);
            shm_unlink("/tauwerk_gpio");
        }
        
        std::cout << "■ Tauwerk GPIO Driver stopped." << std::endl;
    }
};

// [Main unchanged...]
TauwerkGPIODriver* g_driver = nullptr;

void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    if (g_driver) {
        g_driver->stop();
    }
    exit(0);
}

int main() {
    TauwerkGPIODriver driver;
    g_driver = &driver;
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    if (!driver.initialize()) {
        return -1;
    }
    
    driver.run();
    driver.stop();
    
    return 0;
}