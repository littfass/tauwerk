// tauwerk_touchpad_driver.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <csignal>
#include <cstdlib>
#include <poll.h>
#include <algorithm>
#include <array>
#include <set>

// Shared Memory Strukturen für UI Kommunikation
struct UIElement {
  int id;
  int x, y, width, height;
  int type;         // 0=BUTTON, 1=FADER, 2=LABEL, 3=TOGGLE
  bool pressed;
  int value;
  bool visible;
  std::string text;
  int color;
  std::set<int> active_touches;
  int visual_position;      // Aktuelle angezeigte Position
  int target_position;      // Zielposition für Tweening
  double animation_progress; // Fortschritt der Animation (0.0 - 1.0)
};

struct PythonCommand {
  int type;         // 0=CREATE, 1=UPDATE, 2=DELETE, 3=SHOW, 4=HIDE
  int id;
  int element_type;
  int x, y, width, height;
  int value;
  bool visible;
  char text[64];
  int color;
};

struct UIEvent {
  int type;         // 0=BUTTON_DOWN, 1=BUTTON_UP, 2=FADER_CHANGE, 3=TOUCH_MOVE
  int id;
  int value;
  int timestamp;
};

class TauwerkTouchUI {
private:
  // Touch Management
  struct TouchSlot {
    int tracking_id;
    int x, y;
    bool active;
    bool down_sent;
    bool has_position;
    bool pending_touch;
    bool pending_release;
    
    TouchSlot() : tracking_id(-1), x(0), y(0), active(false), 
                 down_sent(false), has_position(false),
                 pending_touch(false), pending_release(false) {}
  };
  
  struct CollisionRect {
    int x, y, width, height;
    std::string uid;
    
    bool contains(int px, int py) const {
      return (px >= x && px <= x + width && py >= y && py <= y + height);
    }
  };

  enum class FaderMode {
      JUMP = 0,
      INCREMENTAL = 1,  
      SMOOTH = 2
  };

  // Framebuffer MIT DOUBLE BUFFERING
  struct FramebufferInfo {
    int fd;
    uint8_t* front_buffer;
    std::vector<uint8_t> back_buffer;  // ✅ DOUBLE BUFFERING
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t stride;
    size_t buffer_size;
  } fb;

  // Touch Input
  int touch_fd;
  std::string touch_device_path;
  bool multitouch;
  bool ignore_singletouch;
  int current_slot;

  // UI State
  std::unordered_map<int, UIElement> elements;
  std::atomic<bool> running{true};
  bool needs_redraw;

  // Touch Management
  std::array<TouchSlot, 10> touch_slots;
  std::unordered_map<std::string, CollisionRect> collision_elements;
  std::unordered_map<int, std::string> active_touches;
  std::unordered_map<int, std::string> hovered_touches;
  std::unordered_map<int, std::string> touch_start_elements;

  // Shared Memory
  PythonCommand* command_buffer;
  UIEvent* event_buffer;
  std::atomic<int> command_read_index{0};
  std::atomic<int> event_write_index{0};
  const int BUFFER_SIZE = 256;

  int command_shm_fd;
  int event_shm_fd;

  // Performance
  int fps_limit;
  int frame_count;
  std::chrono::steady_clock::time_point last_fps_check;

  // Animation State
  struct AnimationTest {
    bool enabled;
    int x, y, width, height;
    double current_width;
    int direction;
    std::chrono::steady_clock::time_point last_update;
  } animation_test;

public:
  TauwerkTouchUI() : touch_fd(-1), command_shm_fd(-1), event_shm_fd(-1), 
                    command_buffer(nullptr), event_buffer(nullptr), 
                    needs_redraw(true), multitouch(false), ignore_singletouch(false), 
                    current_slot(0), fps_limit(60), frame_count(0) {
    last_fps_check = std::chrono::steady_clock::now();
  }

  bool initialize() {
    if (!setup_framebuffer()) {
      std::cout << "❌ Framebuffer setup failed" << std::endl;
      return false;
    }

    if (!setup_touch_input()) {
      std::cout << "❌ Touch input setup failed" << std::endl;
      return false;
    }

    if (!setup_shared_memory()) {
      std::cout << "❌ Shared memory setup failed" << std::endl;
      return false;
    }

    // ✅ Animation Test initialisieren
    initialize_animation_test();

    std::cout << "☰ Tauwerk Touch UI initialized" << std::endl;
    std::cout << "╰ Display: " << fb.width << "x" << fb.height << std::endl;
    std::cout << "╰ Touch: " << touch_device_path << std::endl;
    std::cout << "╰ Multitouch: " << (multitouch ? "Yes" : "No") << std::endl;
    std::cout << "╰ Double Buffering: ENABLED" << std::endl;
    std::cout << "╰ Animation Test: ENABLED" << std::endl;
    return true;
  }

  bool setup_framebuffer() {
    fb.fd = open("/dev/fb0", O_RDWR);
    if (fb.fd < 0) {
      std::cerr << "❌ Cannot open framebuffer" << std::endl;
      return false;
    }

    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;

    if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &var_info) < 0) {
      std::cerr << "❌ Cannot get variable screen info" << std::endl;
      close(fb.fd);
      return false;
    }

    if (ioctl(fb.fd, FBIOGET_FSCREENINFO, &fix_info) < 0) {
      std::cerr << "❌ Cannot get fixed screen info" << std::endl;
      close(fb.fd);
      return false;
    }

    fb.width = var_info.xres;
    fb.height = var_info.yres;
    fb.bpp = var_info.bits_per_pixel;
    fb.stride = fix_info.line_length;
    fb.buffer_size = fb.stride * fb.height;

    // ✅ FRONT BUFFER (Hardware)
    fb.front_buffer = (uint8_t*)mmap(nullptr, fb.buffer_size, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, fb.fd, 0);
    
    if (fb.front_buffer == MAP_FAILED) {
      std::cerr << "❌ Failed to mmap framebuffer" << std::endl;
      close(fb.fd);
      return false;
    }

    // ✅ BACK BUFFER (RAM) - DOUBLE BUFFERING
    fb.back_buffer.resize(fb.buffer_size);
    std::fill(fb.back_buffer.begin(), fb.back_buffer.end(), 0x1A); // Grau background

    std::cout << "✅ Framebuffer: " << fb.width << "x" << fb.height << std::endl;
    std::cout << "✅ Double Buffering: Enabled (" << (fb.buffer_size / 1024) << " KB back buffer)" << std::endl;
    return true;
  }

  // ✅ DOUBLE BUFFERING: draw_pixel arbeitet nur im BACK BUFFER
  void draw_pixel(int x, int y, int color) {
    if (x < 0 || x >= fb.width || y < 0 || y >= fb.height) return;
    
    int offset = y * fb.stride + x * (fb.bpp / 8);
    
    // ✅ NUR IM BACK BUFFER - SUPER SCHNELL
    fb.back_buffer[offset] = color & 0xFF;           // B
    fb.back_buffer[offset + 1] = (color >> 8) & 0xFF;  // G
    fb.back_buffer[offset + 2] = (color >> 16) & 0xFF; // R
    if (fb.bpp == 32) {
      fb.back_buffer[offset + 3] = 0xFF;
    }
  }

  // ✅ DOUBLE BUFFERING: Buffer swap
  void swap_buffers() {
    // ✅ EINMAL pro Frame: Back Buffer → Front Buffer
    memcpy(fb.front_buffer, fb.back_buffer.data(), fb.buffer_size);
  }

  void draw_rect(int x, int y, int width, int height, int color) {
    if (x < 0 || y < 0 || x + width > fb.width || y + height > fb.height) return;
    
    uint8_t b = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    
    for (int py = y; py < y + height; py++) {
      int offset = py * fb.stride + x * (fb.bpp / 8);
      for (int px = 0; px < width; px++) {
        int pixel_offset = offset + px * (fb.bpp / 8);
        fb.back_buffer[pixel_offset] = b;
        fb.back_buffer[pixel_offset + 1] = g;
        fb.back_buffer[pixel_offset + 2] = r;
      }
    }
  }

  void draw_rect_border(int x, int y, int width, int height, int color) {
    draw_rect(x, y, width, 2, color); // Top
    draw_rect(x, y + height - 2, width, 2, color); // Bottom
    draw_rect(x, y, 2, height, color); // Left
    draw_rect(x + width - 2, y, 2, height, color); // Right
  }

  void clear_screen(int color = 0x000000) {
      memset(fb.back_buffer.data(), 0x000000, fb.buffer_size);
      swap_buffers();
  }

  bool is_multitouch_device(int fd) {
    return true; // ✅ Vereinfacht - wie besprochen
  }

  bool setup_touch_input() {
    touch_device_path = autodetect_touch();
    if (touch_device_path.empty()) {
      std::cerr << "❌ No touch device found" << std::endl;
      return false;
    }

    touch_fd = open(touch_device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) {
      std::cerr << "❌ Cannot open touch device: " << touch_device_path << std::endl;
      return false;
    }

    std::cout << "✅ Touch device: " << touch_device_path << std::endl;

    multitouch = true; // ✅ Hardcoded wie besprochen
    ignore_singletouch = true;
    
    std::cout << "📊 Multitouch: Yes (hardcoded)" << std::endl;

    print_touch_capabilities();
    return true;
  }

  void print_touch_capabilities() {
    struct input_absinfo abs_info;
    
    if (ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_info) >= 0) {
      std::cout << "📊 Touch X range: " << abs_info.minimum << " to " << abs_info.maximum 
                << " (resolution: " << abs_info.resolution << ")" << std::endl;
    }
    
    if (ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_info) >= 0) {
      std::cout << "📊 Touch Y range: " << abs_info.minimum << " to " << abs_info.maximum
                << " (resolution: " << abs_info.resolution << ")" << std::endl;
    }
  }

  std::string autodetect_touch() {
    std::cout << "🔍 Scanning for touch devices..." << std::endl;
    
    for (int i = 0; i < 10; i++) {
      std::string path = "/dev/input/event" + std::to_string(i);
      int fd = open(path.c_str(), O_RDONLY);
      if (fd >= 0) {
        unsigned long bitmask[EV_MAX/8 + 1];
        memset(bitmask, 0, sizeof(bitmask));
        
        if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bitmask) >= 0) {
          bool has_touch = false;
          
          if (bitmask[EV_ABS/8] & (1 << (EV_ABS % 8))) {
            unsigned long abs_bitmask[ABS_MAX/8 + 1];
            memset(abs_bitmask, 0, sizeof(abs_bitmask));
            
            if (ioctl(fd, EVIOCGBIT(EV_ABS, ABS_MAX), abs_bitmask) >= 0) {
              if ((abs_bitmask[ABS_MT_POSITION_X/8] & (1 << (ABS_MT_POSITION_X % 8))) ||
                  (abs_bitmask[ABS_X/8] & (1 << (ABS_X % 8)))) {
                has_touch = true;
              }
            }
          }
          
          if (has_touch) {
            char name[256] = "Unknown";
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            std::cout << "✅ Found touch device: " << path << " - " << name << std::endl;
            close(fd);
            return path;
          }
        }
        close(fd);
      }
    }
    
    std::cout << "❌ No touch device found - falling back to event3" << std::endl;
    return "/dev/input/event3";
  }

  bool setup_shared_memory() {
    command_shm_fd = shm_open("/tauwerk_ui_commands", O_CREAT | O_RDWR, 0666);
    if (command_shm_fd < 0) {
      std::cerr << "❌ Failed to create command shared memory" << std::endl;
      return false;
    }
    
    size_t cmd_total_size = sizeof(PythonCommand) * BUFFER_SIZE + 4 * sizeof(int);
    ftruncate(command_shm_fd, cmd_total_size);
    
    command_buffer = (PythonCommand*)mmap(nullptr, cmd_total_size, 
                                         PROT_READ | PROT_WRITE, 
                                         MAP_SHARED, command_shm_fd, 0);
    if (command_buffer == MAP_FAILED) {
      std::cerr << "❌ Failed to mmap command shared memory" << std::endl;
      return false;
    }
    
    event_shm_fd = shm_open("/tauwerk_ui_events", O_CREAT | O_RDWR, 0666);
    if (event_shm_fd < 0) {
      std::cerr << "❌ Failed to create event shared memory" << std::endl;
      return false;
    }
    
    size_t event_total_size = sizeof(UIEvent) * BUFFER_SIZE + 4 * sizeof(int);
    ftruncate(event_shm_fd, event_total_size);
    
    event_buffer = (UIEvent*)mmap(nullptr, event_total_size,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED, event_shm_fd, 0);
    if (event_buffer == MAP_FAILED) {
      std::cerr << "❌ Failed to mmap event shared memory" << std::endl;
      return false;
    }
    
    int* cmd_control = (int*)(command_buffer + BUFFER_SIZE);
    cmd_control[0] = 0;
    cmd_control[1] = 0;
    cmd_control[2] = 0x54415557;
    
    int* event_control = (int*)(event_buffer + BUFFER_SIZE);  
    event_control[0] = 0;
    event_control[1] = 0;
    event_control[2] = 0x54415557;
    
    std::cout << "✅ Shared Memory Bridges initialized" << std::endl;
    return true;
  }

  void send_ui_event(int type, int id, int value) {
    int index = event_write_index.load();
    UIEvent& event = event_buffer[index];
    
    event.type = type;
    event.id = id;
    event.value = value;
    event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
    
    int new_index = (index + 1) % BUFFER_SIZE;
    event_write_index.store(new_index);
    
    int* control = (int*)(event_buffer + BUFFER_SIZE);
    control[0] = new_index;
  }

  void register_element(const std::string& uid, int x, int y, int width, int height) {
    CollisionRect rect;
    rect.uid = uid;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    collision_elements[uid] = rect;
  }
  
  void unregister_element(const std::string& uid) {
    for (auto it = active_touches.begin(); it != active_touches.end(); ) {
      if (it->second == uid) {
        it = active_touches.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = hovered_touches.begin(); it != hovered_touches.end(); ) {
      if (it->second == uid) {
        it = hovered_touches.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = touch_start_elements.begin(); it != touch_start_elements.end(); ) {
      if (it->second == uid) {
        it = touch_start_elements.erase(it);
      } else {
        ++it;
      }
    }
    collision_elements.erase(uid);
  }
  
  std::string process_touch_event(int finger_id, int state, int x, int y) {
    std::string current_element;
    for (const auto& pair : collision_elements) {
      if (pair.second.contains(x, y)) {
        current_element = pair.first;
        break;
      }
    }
    
    if (state == 1) { // TOUCH_DOWN
      if (!current_element.empty() && active_touches.find(finger_id) == active_touches.end()) {
        active_touches[finger_id] = current_element;
        touch_start_elements[finger_id] = current_element;
        hovered_touches.erase(finger_id);
        return current_element;
      } else if (!current_element.empty()) {
        hovered_touches[finger_id] = current_element;
        return current_element;
      }
    } 
    else if (state == 3) { // TOUCH_DRAG
      if (active_touches.find(finger_id) != active_touches.end()) {
        return active_touches[finger_id];
      } else if (hovered_touches.find(finger_id) != hovered_touches.end()) {
        return hovered_touches[finger_id];
      }
    }
    else if (state == 4) { // TOUCH_UP
      std::string element_to_notify;
      if (active_touches.find(finger_id) != active_touches.end()) {
        element_to_notify = active_touches[finger_id];
      } else if (hovered_touches.find(finger_id) != hovered_touches.end()) {
        element_to_notify = hovered_touches[finger_id];
      } else if (touch_start_elements.find(finger_id) != touch_start_elements.end()) {
        element_to_notify = touch_start_elements[finger_id];
      }
      
      if (!element_to_notify.empty()) {
        active_touches.erase(finger_id);
        hovered_touches.erase(finger_id);
        touch_start_elements.erase(finger_id);
        return element_to_notify;
      }
    }
    
    return "";
  }

  void process_python_commands() {
    int* control = (int*)(command_buffer + BUFFER_SIZE);
    int current_write = control[0];
    int current_read = command_read_index.load();

    while (current_read != current_write) {
      PythonCommand& cmd = command_buffer[current_read];
      
      switch (cmd.type) {
        case 0: // CREATE
          {
            UIElement element;
            element.id = cmd.id;
            element.type = cmd.element_type;
            element.x = cmd.x;
            element.y = cmd.y; 
            element.width = cmd.width;
            element.height = cmd.height;
            element.value = cmd.value;
            element.visual_position = (cmd.value * cmd.width) / 100;
            element.target_position = element.visual_position; // ✅ Initial synchron
            element.animation_progress = 1.0; // ✅ Keine aktive Animation
            element.visible = cmd.visible;
            element.text = std::string(cmd.text);
            element.color = cmd.color;
            element.pressed = false;
            
            elements[cmd.id] = element;
            
            std::string uid = "element_" + std::to_string(cmd.id);
            register_element(uid, cmd.x, cmd.y, cmd.width, cmd.height);
            
            needs_redraw = true;
          }
          break;
          
        case 1: // UPDATE
          if (elements.find(cmd.id) != elements.end()) {
            elements[cmd.id].value = cmd.value;
            elements[cmd.id].text = std::string(cmd.text);
            elements[cmd.id].visible = cmd.visible;
            
            // ✅ Auch bei externen Updates Zielposition synchronisieren
            if (elements[cmd.id].type == 1) {
              int new_visual_pos = (cmd.value * elements[cmd.id].width) / 100;
              elements[cmd.id].target_position = new_visual_pos;
              elements[cmd.id].visual_position = new_visual_pos;
              elements[cmd.id].animation_progress = 1.0;
            }
            
            needs_redraw = true;
          }
          break;
          
        case 2: // DELETE
          {
            std::string uid = "element_" + std::to_string(cmd.id);
            unregister_element(uid);
            elements.erase(cmd.id);
            needs_redraw = true;
          }
          break;
          
        case 3: // SHOW
          if (elements.find(cmd.id) != elements.end()) {
            elements[cmd.id].visible = true;
            needs_redraw = true;
          }
          break;
          
        case 4: // HIDE  
          if (elements.find(cmd.id) != elements.end()) {
            elements[cmd.id].visible = false;
            needs_redraw = true;
          }
          break;
      }
      
      current_read = (current_read + 1) % BUFFER_SIZE;
    }
    
    command_read_index.store(current_read);
    control[1] = current_read;
  }

  void process_touch_events() {
    struct input_event ev;

    while (read(touch_fd, &ev, sizeof(ev)) > 0) {
      if (multitouch) {
        process_multitouch_event(ev);
      } else {
        process_singletouch_event(ev);
      }
    }
  }

  void process_multitouch_event(const struct input_event& ev) {
      if (ev.type == EV_SYN) {
          if (ev.code == SYN_REPORT) {
              process_complete_touch_frame();
          }
      }
      else if (ev.type == EV_ABS) {
          switch (ev.code) {
              case ABS_MT_SLOT:
                  current_slot = ev.value;
                  break;
                  
              case ABS_MT_TRACKING_ID:
                  if (current_slot < touch_slots.size()) {
                      if (ev.value == -1) {
                          // ✅ Touch-Up: tracking_id auf -1, aber slot beibehalten
                          touch_slots[current_slot].pending_release = true;
                      } else {
                          // ✅ Touch-Down: tracking_id setzen
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

  void process_singletouch_event(const struct input_event& ev) {
    if (ignore_singletouch) return;
    
    if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
      process_complete_touch_frame();
    }
    else if (ev.type == EV_ABS) {
      if (ev.code == ABS_X) {
        touch_slots[0].x = ev.value;
        touch_slots[0].has_position = true;
      } else if (ev.code == ABS_Y) {
        touch_slots[0].y = ev.value;
        touch_slots[0].has_position = true;
      }
    }
    else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
      if (ev.value == 1) {
        touch_slots[0].pending_touch = true;
        touch_slots[0].tracking_id = 0;
      } else if (ev.value == 0) {
        touch_slots[0].pending_release = true;
      }
    }
  }

  void process_complete_touch_frame() {
      for (int slot = 0; slot < touch_slots.size(); ++slot) {
          auto& ts = touch_slots[slot];
          
          if (ts.pending_touch && ts.has_position) {
              ts.active = true;
              ts.down_sent = false;
              ts.pending_touch = false;
              ts.has_position = false;
              
              if (!ts.down_sent) {
                  ts.down_sent = true;
                  handle_touch_down(slot, ts.tracking_id);
              }
          }
          else if (ts.pending_release) {
              if (ts.active) {
                  // ✅ Wichtige Änderung: tracking_id vom aktiven Slot verwenden, nicht den -1 Wert
                  int tracking_id_to_use = ts.tracking_id;
                  handle_touch_up(slot, tracking_id_to_use);
                  ts.active = false;
                  ts.down_sent = false;
                  // ✅ tracking_id erst nach dem Event zurücksetzen
                  ts.tracking_id = -1;
              }
              ts.pending_release = false;
              ts.has_position = false;
          }
          else if (ts.active && ts.has_position) {
              handle_touch_move(slot, ts.tracking_id);
              ts.has_position = false;
          }
      }
  }

  // In handle_touch_down - Auch hier Zielposition setzen
  void handle_touch_down(int slot, int tracking_id) {
    auto& ts = touch_slots[slot];
    int x = ts.x;
    int y = ts.y;
    
    std::string element_uid = process_touch_event(tracking_id, 1, x, y);
    
    if (!element_uid.empty()) {
      int element_id = std::stoi(element_uid.substr(8));
      
      if (elements.find(element_id) != elements.end()) {
        auto& element = elements[element_id];
        element.active_touches.insert(tracking_id);
        
        if (element.type == 0) { // BUTTON
          element.pressed = true;
          send_ui_event(0, element_id, 1);
        } else if (element.type == 1) { // FADER
          element.pressed = true;
          int relative_x = x - element.x;
          element.value = std::max(0, std::min(100, (relative_x * 100) / element.width));
          
          // ✅ ZIELPOSITION für Tweening setzen
          element.target_position = std::max(0, std::min(element.width, relative_x));
          element.animation_progress = 0.0;
          
          send_ui_event(2, element_id, element.value);
        }
        needs_redraw = true;
      }
    }
  }

void handle_touch_move(int slot, int tracking_id) {
    auto& ts = touch_slots[slot];
    int x = ts.x;
    int y = ts.y;
    
    std::string element_uid = process_touch_event(tracking_id, 3, x, y);
    
    if (!element_uid.empty()) {
      int element_id = std::stoi(element_uid.substr(8));
      
      if (elements.find(element_id) != elements.end()) {
        auto& element = elements[element_id];
        if (element.pressed && element.type == 1) {
          int relative_x = x - element.x;
          int new_value = std::max(0, std::min(100, (relative_x * 100) / element.width));
          
          // ✅ Wert nur bei Änderung senden
          if (new_value != element.value) {
            element.value = new_value;
            send_ui_event(2, element_id, element.value);
          }
          
          // ✅ ZIELPOSITION für Tweening setzen
          element.target_position = std::max(0, std::min(element.width, relative_x));
          element.animation_progress = 0.0; // Animation starten
          
          needs_redraw = true;
        }
      }
    }
  }

  void handle_touch_up(int slot, int tracking_id) {
      auto& ts = touch_slots[slot];
      int x = ts.x;
      int y = ts.y;
      
      std::cout << "TOUCHUP slot:" << slot << " tracking_id:" << tracking_id << " pos:" << x << "," << y << std::endl;
      
      // ✅ tracking_id validieren (kann -1 sein bei manchen Geräten)
      int valid_tracking_id = (tracking_id == -1) ? slot : tracking_id;
      
      std::string element_uid = process_touch_event(valid_tracking_id, 4, x, y);
      
      if (!element_uid.empty()) {
          int element_id = std::stoi(element_uid.substr(8));
          std::cout << "TOUCHUP element:" << element_id << std::endl;
          
          if (elements.find(element_id) != elements.end()) {
              auto& element = elements[element_id];
              element.active_touches.erase(valid_tracking_id);
              
              if (element.active_touches.empty()) {
                  element.pressed = false;
                  
                  if (element.type == 0) { // BUTTON
                      send_ui_event(1, element_id, 0);
                      std::cout << "TOUCHUP BUTTON released:" << element_id << std::endl;
                  }
                  needs_redraw = true;
              }
          }
      }
  }

  // Neue Methode für Fader-Animation
  void update_fader_animations() {
    auto now = std::chrono::steady_clock::now();
    static auto last_update = now;
    
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_update);
    double delta_time = elapsed.count() / 1000000.0;
    last_update = now;
    
    const double ANIMATION_SPEED = 20.0; // Je höher, desto schneller
    
    for (auto& pair : elements) {
      auto& element = pair.second;
      
      if (element.type == 1 && element.animation_progress < 1.0) { // Fader mit aktiver Animation
        // ✅ Progress basierend auf Zeit und Geschwindigkeit
        element.animation_progress += delta_time * ANIMATION_SPEED;
        element.animation_progress = std::min(1.0, element.animation_progress);
        
        // ✅ Glatte Interpolation zwischen aktueller und Zielposition
        double t = linear_step(element.animation_progress); // Glättungsfunktion
        element.visual_position = static_cast<int>(
          element.visual_position + (element.target_position - element.visual_position) * t
        );
        
        needs_redraw = true;
      }
    }
  }

  // Glättungsfunktion für natürlichere Bewegung
  double smooth_step(double t) {
    // Cubic easing in/out - fühlt sich natürlich an
    return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
  }

  // Alternative: Lineare Interpolation (einfacher)
  double linear_step(double t) {
    return t;
  }

  // ✅ ANIMATION METHODEN
  void initialize_animation_test() {
    animation_test.enabled = true;
    animation_test.x = 0;
    animation_test.y = 200;
    animation_test.width = 800;
    animation_test.height = 100;
    animation_test.current_width = 0;
    animation_test.direction = 1;
    animation_test.last_update = std::chrono::steady_clock::now();
  }

  void update_animation() {
    if (!animation_test.enabled) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - animation_test.last_update);
    
    if (elapsed.count() > 1000) {
      double delta_time = elapsed.count() / 1000000.0;
      double pixels_per_second = 400.0;
      double pixel_increment = pixels_per_second * delta_time;
      
      animation_test.current_width += animation_test.direction * pixel_increment;
      
      if (animation_test.current_width >= animation_test.width) {
        animation_test.current_width = animation_test.width;
        animation_test.direction = -1;
      } else if (animation_test.current_width <= 0) {
        animation_test.current_width = 0;
        animation_test.direction = 1;
      }
      
      animation_test.last_update = now;
      needs_redraw = true;
    }
  }

  void render_animation() {
    if (!animation_test.enabled) return;
    
    // 1. Animationsbereich im BACK BUFFER löschen
    draw_rect(animation_test.x, animation_test.y, animation_test.width, animation_test.height, 0x000000);
    
    // 2. Aktuelle animierte Breite zeichnen
    int current_pixel_width = static_cast<int>(animation_test.current_width);
    if (current_pixel_width > 0) {
      draw_rect(animation_test.x, animation_test.y, current_pixel_width, animation_test.height, 0xFFFFFF);
    }
    
    // 3. Rahmen zeichnen
    //draw_rect_border(animation_test.x, animation_test.y, animation_test.width, animation_test.height, 0x666666);
  }

  void render_ui() {
    for (const auto& pair : elements) {
      const auto& element = pair.second;
      if (!element.visible) continue;
      
      if (element.type == 0) { // BUTTON
        int color = element.pressed ? 0x333333 : element.color;
        draw_rect(element.x, element.y, element.width, element.height, color);
      } else if (element.type == 1) { // FADER
        draw_rect(element.x, element.y, element.width, element.height, 0x333333);
        
        // ✅ VISUELLE POSITION verwenden statt Wert-basierter Breite
        int fader_width = element.visual_position;
        if (fader_width > 0) {
          draw_rect(element.x, element.y, fader_width, element.height, element.color);
        }
      }
    }
  }

  // ✅ DOUBLE BUFFERING: Komplettes Frame rendern
  void render_complete_frame() {
    auto render_start = std::chrono::steady_clock::now();
    
    // 1. BACK BUFFER ultra-schnell clearen
    memset(fb.back_buffer.data(), 0x1A, fb.buffer_size);
    
    // 2. UI mit optimierten Rechtecken rendern
    render_ui();
    
    // 3. Animation rendern
    //render_animation_fast();
    
    // 4. Buffer swap
    swap_buffers();
    
    auto render_end = std::chrono::steady_clock::now();
    auto render_time = std::chrono::duration_cast<std::chrono::microseconds>(render_end - render_start);
    
    // Debug: Renderzeit überwachen
    static int frame_counter = 0;
    if (++frame_counter % 60 == 0) {
      std::cout << "⚡ Render time: " << render_time.count() << "µs" << std::endl;
    }
  }

  void update_fps_counter() {
    frame_count++;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_fps_check);
    
    if (elapsed.count() >= 2) {
      double fps = frame_count / elapsed.count();
      std::cout << "📊 FPS: " << fps << " | Temp: " << "N/A" << std::endl;
      frame_count = 0;
      last_fps_check = now;
    }
  }

  void run() {
    std::cout << "▶ Tauwerk Touch UI started - DOUBLE BUFFERING" << std::endl;
    
    // ✅ Initiales Rendering
    render_complete_frame();
    
    auto last_frame = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::milliseconds(16); // 60 FPS


    
    while (running) {
      auto frame_start = std::chrono::steady_clock::now();
      
      process_touch_events();
      process_python_commands();
      update_fader_animations();
      //update_animation();
      
      auto time_since_last_frame = std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - last_frame);
      if (time_since_last_frame >= frame_interval) {
        if (needs_redraw) {
          render_complete_frame();
          needs_redraw = false;
        }
        update_fps_counter();
        last_frame = frame_start;
      }
      
      // ✅ CPU entlasten
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "■ Tauwerk Touch UI main loop ended." << std::endl;
  }

  void stop() {
    running = false;
    
    // ✅ Back Buffer mit Schwarz füllen für sauberes Exit
    std::fill(fb.back_buffer.begin(), fb.back_buffer.end(), 0x00);
    swap_buffers();
    
    if (fb.front_buffer) munmap(fb.front_buffer, fb.buffer_size);
    if (fb.fd >= 0) close(fb.fd);
    if (touch_fd >= 0) close(touch_fd);
    
    if (command_buffer) munmap(command_buffer, sizeof(PythonCommand) * BUFFER_SIZE + 4 * sizeof(int));
    if (event_buffer) munmap(event_buffer, sizeof(UIEvent) * BUFFER_SIZE + 4 * sizeof(int));
    
    if (command_shm_fd >= 0) {
      close(command_shm_fd);
      shm_unlink("/tauwerk_ui_commands");
    }
    if (event_shm_fd >= 0) {
      close(event_shm_fd);
      shm_unlink("/tauwerk_ui_events");
    }
    
    std::cout << "■ Tauwerk Touch UI stopped." << std::endl;
  }
};

// Signal Handler
TauwerkTouchUI* g_touch_ui = nullptr;

void signal_handler(int signal) {
  std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
  if (g_touch_ui) {
    g_touch_ui->stop();
  }
  exit(0);
}

int main() {
  TauwerkTouchUI touch_ui;
  g_touch_ui = &touch_ui;
  
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  
  if (!touch_ui.initialize()) {
    return -1;
  }
  
  touch_ui.run();
  touch_ui.stop();
  
  return 0;
}