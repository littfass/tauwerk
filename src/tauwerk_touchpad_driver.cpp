#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <csignal>
#include <poll.h>
#include <algorithm>
#include <array>
#include <set>

// DRM/KMS Headers
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>

// ═══════════════════════════════════════════════════════════════════════
// SHARED MEMORY STRUCTURES
// ═══════════════════════════════════════════════════════════════════════

struct UIElement {
    int id;
    int x, y, width, height;
    int type;
    bool pressed;
    int value;
    bool visible;
    std::string text;
    int color;
    std::set<int> active_touches;
    int visual_position;
    int target_position;
    double animation_progress;
};

struct PythonCommand {
    int type;
    int id;
    int element_type;
    int x, y, width, height;
    int value;
    bool visible;
    char text[64];
    int color;
};

struct UIEvent {
    int type;
    int id;
    int value;
    int timestamp;
};

// ═══════════════════════════════════════════════════════════════════════
// GPU BACKEND WITH OPENGL TEXTURE RENDERING
// ═══════════════════════════════════════════════════════════════════════

class GPUBackend {
private:
    int drm_fd;
    drmModeConnector* connector;
    drmModeCrtc* crtc;
    drmModeModeInfo mode;
    uint32_t connector_id;
    struct gbm_device* gbm_dev;
    struct gbm_surface* gbm_surf;
    struct gbm_bo* bo;
    struct gbm_bo* previous_bo;
    uint32_t previous_fb;
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    EGLConfig egl_config;
    
    std::vector<uint32_t> back_buffer;
    uint32_t width;
    uint32_t height;
    
    // OpenGL Resources
    GLuint texture_id;
    GLuint shader_program;
    GLuint vbo;
    
    const char* vertex_shader_src = R"(
        attribute vec2 position;
        attribute vec2 texcoord;
        varying vec2 v_texcoord;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            v_texcoord = texcoord;
        }
    )";
    
    const char* fragment_shader_src = R"(
        precision mediump float;
        varying vec2 v_texcoord;
        uniform sampler2D texture;
        void main() {
            gl_FragColor = texture2D(texture, v_texcoord);
        }
    )";

    GLuint compile_shader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "❌ Shader compilation error: " << log << std::endl;
            return 0;
        }
        return shader;
    }

    bool setup_opengl() {
        GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
        GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
        
        if (!vertex_shader || !fragment_shader) return false;
        
        shader_program = glCreateProgram();
        glAttachShader(shader_program, vertex_shader);
        glAttachShader(shader_program, fragment_shader);
        glLinkProgram(shader_program);
        
        GLint success;
        glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(shader_program, 512, nullptr, log);
            std::cerr << "❌ Program linking error: " << log << std::endl;
            return false;
        }
        
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        
        float vertices[] = {
            -1.0f, -1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 1.0f,
            -1.0f,  1.0f,  0.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 0.0f,
        };
        
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        std::cout << "✅ OpenGL rendering pipeline initialized" << std::endl;
        return true;
    }

public:
    GPUBackend() : drm_fd(-1), connector(nullptr), crtc(nullptr), 
                   gbm_dev(nullptr), gbm_surf(nullptr), bo(nullptr),
                   previous_bo(nullptr), previous_fb(0),
                   egl_display(EGL_NO_DISPLAY), egl_surface(EGL_NO_SURFACE),
                   egl_context(EGL_NO_CONTEXT), width(0), height(0),
                   texture_id(0), shader_program(0), vbo(0) {}

    bool initialize() {
        const char* drm_devices[] = {"/dev/dri/card1", "/dev/dri/card2", "/dev/dri/card0"};
        
        for (const char* device : drm_devices) {
            drm_fd = open(device, O_RDWR | O_CLOEXEC);
            if (drm_fd >= 0) {
                drmModeRes* res = drmModeGetResources(drm_fd);
                if (res && res->count_connectors > 0) {
                    bool has_connected = false;
                    for (int i = 0; i < res->count_connectors; i++) {
                        drmModeConnector* test_conn = drmModeGetConnector(drm_fd, res->connectors[i]);
                        if (test_conn && test_conn->connection == DRM_MODE_CONNECTED && test_conn->count_modes > 0) {
                            has_connected = true;
                            drmModeFreeConnector(test_conn);
                            break;
                        }
                        if (test_conn) drmModeFreeConnector(test_conn);
                    }
                    drmModeFreeResources(res);
                    if (has_connected) break;
                }
                close(drm_fd);
                drm_fd = -1;
            }
        }

        if (drm_fd < 0) {
            std::cerr << "❌ Cannot open DRM device" << std::endl;
            return false;
        }

        drmModeRes* resources = drmModeGetResources(drm_fd);
        if (!resources) return false;

        for (int i = 0; i < resources->count_connectors; i++) {
            connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
            if (connector && connector->connection == DRM_MODE_CONNECTED) {
                connector_id = connector->connector_id;
                mode = connector->modes[0];
                std::cout << "✅ DRM connector: " << mode.hdisplay << "x" << mode.vdisplay << std::endl;
                break;
            }
            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(resources);

        if (!connector) return false;

        width = mode.hdisplay;
        height = mode.vdisplay;

        uint32_t crtc_id = 0;
        if (connector->encoder_id) {
            drmModeEncoder* encoder = drmModeGetEncoder(drm_fd, connector->encoder_id);
            if (encoder) {
                crtc_id = encoder->crtc_id;
                drmModeFreeEncoder(encoder);
            }
        }

        if (crtc_id == 0) {
            drmModeRes* res = drmModeGetResources(drm_fd);
            if (res) {
                for (int i = 0; i < connector->count_encoders; i++) {
                    drmModeEncoder* encoder = drmModeGetEncoder(drm_fd, connector->encoders[i]);
                    if (encoder) {
                        for (int j = 0; j < res->count_crtcs; j++) {
                            if (encoder->possible_crtcs & (1 << j)) {
                                crtc_id = res->crtcs[j];
                                drmModeFreeEncoder(encoder);
                                goto crtc_found;
                            }
                        }
                        drmModeFreeEncoder(encoder);
                    }
                }
                crtc_found:
                drmModeFreeResources(res);
            }
        }

        if (crtc_id == 0) return false;

        crtc = drmModeGetCrtc(drm_fd, crtc_id);
        if (!crtc) return false;

        gbm_dev = gbm_create_device(drm_fd);
        if (!gbm_dev) return false;

        gbm_surf = gbm_surface_create(gbm_dev, width, height,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!gbm_surf) return false;

        egl_display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm_dev, NULL);
        if (egl_display == EGL_NO_DISPLAY) return false;

        if (!eglInitialize(egl_display, nullptr, nullptr)) return false;

        EGLint config_attrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE
        };
        EGLint num_configs;
        if (!eglChooseConfig(egl_display, config_attrs, &egl_config, 1, &num_configs)) return false;

        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;

        EGLint context_attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attrs);
        if (egl_context == EGL_NO_CONTEXT) return false;

        egl_surface = eglCreatePlatformWindowSurface(egl_display, egl_config, gbm_surf, nullptr);
        if (egl_surface == EGL_NO_SURFACE) return false;

        if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) return false;

        back_buffer.resize(width * height);
        std::fill(back_buffer.begin(), back_buffer.end(), 0x001A1A1A);

        if (!setup_opengl()) return false;

        std::cout << "✅ GPU Backend initialized (DRM/KMS)" << std::endl;
        return true;
    }

    void draw_pixel(int x, int y, uint32_t color) {
        if (x < 0 || x >= (int)width || y < 0 || y >= (int)height) return;
        back_buffer[y * width + x] = color;
    }

    void draw_rect(int x, int y, int w, int h, uint32_t color) {
        if (x < 0 || y < 0 || x + w > (int)width || y + h > (int)height) return;
        for (int py = y; py < y + h; py++) {
            for (int px = x; px < x + w; px++) {
                back_buffer[py * width + px] = color;
            }
        }
    }

    void clear_screen(uint32_t color = 0x00000000) {
        std::fill(back_buffer.begin(), back_buffer.end(), color);
    }

    void present() {
        // 1. Upload back_buffer to OpenGL texture
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                     GL_RGBA, GL_UNSIGNED_BYTE, back_buffer.data());
        
        // 2. Render fullscreen quad with texture
        glUseProgram(shader_program);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        
        GLint pos_attrib = glGetAttribLocation(shader_program, "position");
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
        
        GLint tex_attrib = glGetAttribLocation(shader_program, "texcoord");
        glEnableVertexAttribArray(tex_attrib);
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
        
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        // 3. EGL swap
        eglSwapBuffers(egl_display, egl_surface);

        // 4. DRM page flip
        bo = gbm_surface_lock_front_buffer(gbm_surf);
        if (!bo) return;

        uint32_t fb_id = 0;
        uint32_t handle = gbm_bo_get_handle(bo).u32;
        uint32_t pitch = gbm_bo_get_stride(bo);

        int ret = drmModeAddFB(drm_fd, width, height, 24, 32, pitch, handle, &fb_id);
        if (ret) {
            gbm_surface_release_buffer(gbm_surf, bo);
            return;
        }

        drmModeSetCrtc(drm_fd, crtc->crtc_id, fb_id, 0, 0, &connector_id, 1, &mode);

        if (previous_bo) {
            drmModeRmFB(drm_fd, previous_fb);
            gbm_surface_release_buffer(gbm_surf, previous_bo);
        }

        previous_bo = bo;
        previous_fb = fb_id;
    }

    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }

    ~GPUBackend() {
        if (texture_id) glDeleteTextures(1, &texture_id);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (shader_program) glDeleteProgram(shader_program);
        if (previous_bo) {
            drmModeRmFB(drm_fd, previous_fb);
            gbm_surface_release_buffer(gbm_surf, previous_bo);
        }
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        if (egl_display != EGL_NO_DISPLAY) eglTerminate(egl_display);
        if (gbm_surf) gbm_surface_destroy(gbm_surf);
        if (gbm_dev) gbm_device_destroy(gbm_dev);
        if (crtc) drmModeFreeCrtc(crtc);
        if (connector) drmModeFreeConnector(connector);
        if (drm_fd >= 0) close(drm_fd);
    }
};

// ═══════════════════════════════════════════════════════════════════════
// TAUWERK TOUCH UI (Touch-Logik unverändert!)
// ═══════════════════════════════════════════════════════════════════════

class TauwerkTouchUI {
private:
    // GPU Backend (ersetzt Framebuffer)
    GPUBackend gpu;

    // Touch Management (UNVERÄNDERT)
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

    int touch_fd;
    std::string touch_device_path;
    bool multitouch;
    bool ignore_singletouch;
    int current_slot;

    std::unordered_map<int, UIElement> elements;
    std::atomic<bool> running{true};
    bool needs_redraw;

    std::array<TouchSlot, 10> touch_slots;
    std::unordered_map<std::string, CollisionRect> collision_elements;
    std::unordered_map<int, std::string> active_touches;
    std::unordered_map<int, std::string> hovered_touches;
    std::unordered_map<int, std::string> touch_start_elements;

    // Shared Memory (UNVERÄNDERT)
    PythonCommand* command_buffer;
    UIEvent* event_buffer;
    std::atomic<int> command_read_index{0};
    std::atomic<int> event_write_index{0};
    const int BUFFER_SIZE = 256;
    int command_shm_fd;
    int event_shm_fd;

    int fps_limit;
    int frame_count;
    std::chrono::steady_clock::time_point last_fps_check;

public:
    TauwerkTouchUI() : touch_fd(-1), command_shm_fd(-1), event_shm_fd(-1),
                      command_buffer(nullptr), event_buffer(nullptr),
                      needs_redraw(true), multitouch(false), 
                      ignore_singletouch(false), current_slot(0),
                      fps_limit(60), frame_count(0) {
        last_fps_check = std::chrono::steady_clock::now();
    }

    bool initialize() {
        if (!gpu.initialize()) {
            std::cerr << "❌ GPU initialization failed" << std::endl;
            return false;
        }

        if (!setup_touch_input()) {
            std::cerr << "❌ Touch setup failed" << std::endl;
            return false;
        }

        if (!setup_shared_memory()) {
            std::cerr << "❌ Shared memory setup failed" << std::endl;
            return false;
        }

        std::cout << "✅ Tauwerk Touch UI (DRM) initialized" << std::endl;
        return true;
    }

    // ═══════════════════════════════════════════════════════════════════
    // TOUCH INPUT (KOMPLETT UNVERÄNDERT)
    // ═══════════════════════════════════════════════════════════════════

    bool setup_touch_input() {
        touch_device_path = autodetect_touch();
        if (touch_device_path.empty()) {
            std::cerr << "❌ No touch device found" << std::endl;
            return false;
        }

        touch_fd = open(touch_device_path.c_str(), O_RDONLY | O_NONBLOCK);
        if (touch_fd < 0) {
            std::cerr << "❌ Cannot open touch device" << std::endl;
            return false;
        }

        multitouch = true;
        ignore_singletouch = true;
        std::cout << "✅ Touch: " << touch_device_path << std::endl;
        return true;
    }

    std::string autodetect_touch() {
        for (int i = 0; i < 10; i++) {
            std::string path = "/dev/input/event" + std::to_string(i);
            int fd = open(path.c_str(), O_RDONLY);
            if (fd >= 0) {
                unsigned long bitmask[EV_MAX/8 + 1];
                memset(bitmask, 0, sizeof(bitmask));
                
                if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bitmask) >= 0) {
                    if (bitmask[EV_ABS/8] & (1 << (EV_ABS % 8))) {
                        unsigned long abs_bitmask[ABS_MAX/8 + 1];
                        memset(abs_bitmask, 0, sizeof(abs_bitmask));
                        
                        if (ioctl(fd, EVIOCGBIT(EV_ABS, ABS_MAX), abs_bitmask) >= 0) {
                            if (abs_bitmask[ABS_MT_POSITION_X/8] & (1 << (ABS_MT_POSITION_X % 8))) {
                                close(fd);
                                return path;
                            }
                        }
                    }
                }
                close(fd);
            }
        }
        return "/dev/input/event3";
    }

    // ═══════════════════════════════════════════════════════════════════
    // SHARED MEMORY (KOMPLETT UNVERÄNDERT)
    // ═══════════════════════════════════════════════════════════════════

    bool setup_shared_memory() {
        command_shm_fd = shm_open("/tauwerk_ui_commands", O_CREAT | O_RDWR, 0666);
        if (command_shm_fd < 0) return false;
        
        size_t cmd_size = sizeof(PythonCommand) * BUFFER_SIZE + 4 * sizeof(int);
        ftruncate(command_shm_fd, cmd_size);
        
        command_buffer = (PythonCommand*)mmap(nullptr, cmd_size,
                                             PROT_READ | PROT_WRITE,
                                             MAP_SHARED, command_shm_fd, 0);
        if (command_buffer == MAP_FAILED) return false;

        event_shm_fd = shm_open("/tauwerk_ui_events", O_CREAT | O_RDWR, 0666);
        if (event_shm_fd < 0) return false;
        
        size_t event_size = sizeof(UIEvent) * BUFFER_SIZE + 4 * sizeof(int);
        ftruncate(event_shm_fd, event_size);
        
        event_buffer = (UIEvent*)mmap(nullptr, event_size,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, event_shm_fd, 0);
        if (event_buffer == MAP_FAILED) return false;

        int* cmd_control = (int*)(command_buffer + BUFFER_SIZE);
        cmd_control[0] = 0; cmd_control[1] = 0; cmd_control[2] = 0x54415557;
        
        int* event_control = (int*)(event_buffer + BUFFER_SIZE);
        event_control[0] = 0; event_control[1] = 0; event_control[2] = 0x54415557;

        std::cout << "✅ Shared Memory initialized" << std::endl;
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

    // ═══════════════════════════════════════════════════════════════════
    // COLLISION & ELEMENT MANAGEMENT (UNVERÄNDERT)
    // ═══════════════════════════════════════════════════════════════════

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
            if (it->second == uid) it = active_touches.erase(it);
            else ++it;
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
        
        if (state == 1 && !current_element.empty()) { // TOUCH_DOWN
            active_touches[finger_id] = current_element;
            touch_start_elements[finger_id] = current_element;
            return current_element;
        }
        else if (state == 3 && active_touches.find(finger_id) != active_touches.end()) { // DRAG
            return active_touches[finger_id];
        }
        else if (state == 4) { // TOUCH_UP
            std::string element = active_touches[finger_id];
            active_touches.erase(finger_id);
            touch_start_elements.erase(finger_id);
            return element;
        }
        
        return "";
    }

    // ═══════════════════════════════════════════════════════════════════
    // PYTHON COMMAND PROCESSING (UNVERÄNDERT)
    // ═══════════════════════════════════════════════════════════════════

    void process_python_commands() {
        int* control = (int*)(command_buffer + BUFFER_SIZE);
        int current_write = control[0];
        int current_read = command_read_index.load();

        while (current_read != current_write) {
            PythonCommand& cmd = command_buffer[current_read];
            
            switch (cmd.type) {
                case 0: { // CREATE
                    UIElement element;
                    element.id = cmd.id;
                    element.type = cmd.element_type;
                    element.x = cmd.x;
                    element.y = cmd.y;
                    element.width = cmd.width;
                    element.height = cmd.height;
                    element.value = cmd.value;
                    element.visual_position = (cmd.value * cmd.width) / 100;
                    element.target_position = element.visual_position;
                    element.animation_progress = 1.0;
                    element.visible = cmd.visible;
                    element.text = std::string(cmd.text);
                    element.color = cmd.color;
                    element.pressed = false;
                    
                    elements[cmd.id] = element;
                    
                    std::string uid = "element_" + std::to_string(cmd.id);
                    register_element(uid, cmd.x, cmd.y, cmd.width, cmd.height);
                    
                    needs_redraw = true;
                    break;
                }
                
                case 1: { // UPDATE
                    if (elements.find(cmd.id) != elements.end()) {
                        elements[cmd.id].value = cmd.value;
                        elements[cmd.id].text = std::string(cmd.text);
                        elements[cmd.id].visible = cmd.visible;
                        
                        if (elements[cmd.id].type == 1) {
                            int new_pos = (cmd.value * elements[cmd.id].width) / 100;
                            elements[cmd.id].target_position = new_pos;
                            elements[cmd.id].visual_position = new_pos;
                            elements[cmd.id].animation_progress = 1.0;
                        }
                        
                        needs_redraw = true;
                    }
                    break;
                }
                
                case 2: { // DELETE
                    std::string uid = "element_" + std::to_string(cmd.id);
                    unregister_element(uid);
                    elements.erase(cmd.id);
                    needs_redraw = true;
                    break;
                }
                
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

    // ═══════════════════════════════════════════════════════════════════
    // TOUCH EVENT PROCESSING (UNVERÄNDERT)
    // ═══════════════════════════════════════════════════════════════════

    void process_touch_events() {
        struct input_event ev;
        while (read(touch_fd, &ev, sizeof(ev)) > 0) {
            if (multitouch) {
                process_multitouch_event(ev);
            }
        }
    }

    void process_multitouch_event(const struct input_event& ev) {
        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            process_complete_touch_frame();
        }
        else if (ev.type == EV_ABS) {
            switch (ev.code) {
                case ABS_MT_SLOT:
                    current_slot = ev.value;
                    break;
                case ABS_MT_TRACKING_ID:
                    if (current_slot < touch_slots.size()) {
                        if (ev.value == -1) {
                            touch_slots[current_slot].pending_release = true;
                        } else {
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
                    int tracking_id = ts.tracking_id;
                    handle_touch_up(slot, tracking_id);
                    ts.active = false;
                    ts.down_sent = false;
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
                    
                    if (new_value != element.value) {
                        element.value = new_value;
                        send_ui_event(2, element_id, element.value);
                    }
                    
                    element.target_position = std::max(0, std::min(element.width, relative_x));
                    element.animation_progress = 0.0;
                    needs_redraw = true;
                }
            }
        }
    }

    void handle_touch_up(int slot, int tracking_id) {
        auto& ts = touch_slots[slot];
        int x = ts.x;
        int y = ts.y;
        
        int valid_tracking_id = (tracking_id == -1) ? slot : tracking_id;
        std::string element_uid = process_touch_event(valid_tracking_id, 4, x, y);
        
        if (!element_uid.empty()) {
            int element_id = std::stoi(element_uid.substr(8));
            
            if (elements.find(element_id) != elements.end()) {
                auto& element = elements[element_id];
                element.active_touches.erase(valid_tracking_id);
                
                if (element.active_touches.empty()) {
                    element.pressed = false;
                    
                    if (element.type == 0) {
                        send_ui_event(1, element_id, 0);
                    }
                    needs_redraw = true;
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // ANIMATION (UNVERÄNDERT)
    // ═══════════════════════════════════════════════════════════════════

    void update_fader_animations() {
        auto now = std::chrono::steady_clock::now();
        static auto last_update = now;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_update);
        double delta_time = elapsed.count() / 1000000.0;
        last_update = now;
        
        const double ANIMATION_SPEED = 20.0;
        
        for (auto& pair : elements) {
            auto& element = pair.second;
            
            if (element.type == 1 && element.animation_progress < 1.0) {
                element.animation_progress += delta_time * ANIMATION_SPEED;
                element.animation_progress = std::min(1.0, element.animation_progress);
                
                double t = element.animation_progress;
                element.visual_position = static_cast<int>(
                    element.visual_position + (element.target_position - element.visual_position) * t
                );
                
                needs_redraw = true;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // RENDERING (Angepasst für GPU Backend)
    // ═══════════════════════════════════════════════════════════════════

    void render_ui() {
        gpu.clear_screen(0x001A1A1A); // Grau
        
        for (const auto& pair : elements) {
            const auto& element = pair.second;
            if (!element.visible) continue;
            
            if (element.type == 0) { // BUTTON
                uint32_t color = element.pressed ? 0xFF333333 : element.color;
                gpu.draw_rect(element.x, element.y, element.width, element.height, color);
            }
            else if (element.type == 1) { // FADER
                gpu.draw_rect(element.x, element.y, element.width, element.height, 0xFF333333);
                
                int fader_width = element.visual_position;
                if (fader_width > 0) {
                    gpu.draw_rect(element.x, element.y, fader_width, element.height, element.color);
                }
            }
        }
        
        gpu.present();
    }

    void update_fps_counter() {
        frame_count++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_fps_check);
        
        if (elapsed.count() >= 2) {
            double fps = frame_count / elapsed.count();
            std::cout << "📊 FPS: " << fps << std::endl;
            frame_count = 0;
            last_fps_check = now;
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // MAIN LOOP
    // ═══════════════════════════════════════════════════════════════════

    void run() {
        std::cout << "▶ Tauwerk Touch UI (DRM) started" << std::endl;
        
        render_ui();
        
        auto last_frame = std::chrono::steady_clock::now();
        const auto frame_interval = std::chrono::milliseconds(16); // 60 FPS
        
        while (running) {
            auto frame_start = std::chrono::steady_clock::now();
            
            process_touch_events();
            process_python_commands();
            update_fader_animations();
            
            auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - last_frame);
            if (time_since_last >= frame_interval) {
                if (needs_redraw) {
                    render_ui();
                    needs_redraw = false;
                }
                update_fps_counter();
                last_frame = frame_start;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void stop() {
        running = false;
        
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
        
        std::cout << "■ Tauwerk Touch UI stopped" << std::endl;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════

TauwerkTouchUI* g_touch_ui = nullptr;

void signal_handler(int signal) {
    std::cout << "Signal " << signal << " received, shutting down..." << std::endl;
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