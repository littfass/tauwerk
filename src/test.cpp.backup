#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <cmath>
#include <poll.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <array>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <ft2build.h>
#include FT_FREETYPE_H

#ifndef DRM_MODE_PAGE_FLIP_EVENT
#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#endif

// ============================================
// CORE TYPES & HELPERS
// ============================================

struct Color {
    float r, g, b, a;
    Color(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

struct Glyph {
    GLuint texture_id;
    int width, height;
    int bearing_x, bearing_y;
    int advance;
};

enum class SliderMode {
    JUMP,
    INCREMENTAL,
    SMOOTH
};

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

// ============================================
// BACKLIGHT CONTROLLER
// ============================================

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

    ~BacklightController() { restore(); }
};

// ============================================
// FORWARD DECLARATIONS
// ============================================

class Renderer;
class TouchManager;

// ============================================
// WIDGET BASE CLASS
// ============================================

class Widget {
protected:
    float x, y, width, height;
    bool visible;
    bool dirty;  // Needs redraw
    
public:
    Widget(float x, float y, float w, float h) 
        : x(x), y(y), width(w), height(h), visible(true), dirty(true) {}
    
    virtual ~Widget() = default;
    
    // Core methods - only 2 virtual calls per frame!
    virtual void update(float dt) {}
    virtual void draw(Renderer& renderer) = 0;
    virtual bool handle_touch(int tx, int ty, bool down) { return false; }
    
    // Utility methods - inline, zero overhead
    bool is_inside(int px, int py) const {
        return visible && px >= x && px <= x + width && 
               py >= y && py <= y + height;
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

// ============================================
// RENDERER - Low-Level GPU Interface
// ============================================

class Renderer {
private:
    // DRM/GBM/EGL
    int drm_fd;
    drmModeConnector* connector;
    drmModeCrtc* crtc;
    drmModeModeInfo mode;
    uint32_t connector_id;
    struct gbm_device* gbm_dev;
    struct gbm_surface* gbm_surf;
    struct gbm_bo* previous_bo;
    uint32_t previous_fb;
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    bool waiting_for_flip;
    
    // Display
    uint32_t width, height;
    uint32_t render_width, render_height;
    float render_scale;
    int display_rotation;
    
    // OpenGL
    GLuint shader_program_dither;
    GLuint shader_program_solid;
    GLuint shader_program_text;
    GLuint vbo;
    GLuint text_vbo;
    
    // Font
    FT_Library ft_library;
    FT_Face ft_face;
    std::map<char, Glyph> glyphs;
    int font_size;
    
    // Shaders
    const char* vertex_shader = R"(
        attribute vec2 position;
        uniform vec2 screen_size;
        uniform vec4 rect;
        varying vec2 fragCoord;
        void main() {
            vec2 pixel_pos = rect.xy + position * rect.zw;
            fragCoord = pixel_pos;
            vec2 ndc = (pixel_pos / screen_size) * 2.0 - 1.0;
            ndc.y = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
        }
    )";
    
    const char* fragment_shader_dither = R"(
        precision mediump float;
        uniform vec4 color;
        uniform float dot_alpha;
        varying vec2 fragCoord;
        
        void main() {
            vec2 pos = mod(fragCoord, 4.0);
            bool is_dot = (pos.x < 2.0 && pos.y < 2.0);
            
            if (is_dot) {
                gl_FragColor = vec4(color.rgb * dot_alpha, 1.0);
            } else {
                gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            }
        }
    )";
    
    const char* fragment_shader_solid = R"(
        precision mediump float;
        uniform vec4 color;
        void main() {
            gl_FragColor = color;
        }
    )";
    
    const char* vertex_shader_text = R"(
        attribute vec2 position;
        attribute vec2 texcoord;
        uniform vec2 screen_size;
        varying vec2 v_texcoord;
        void main() {
            v_texcoord = texcoord;
            vec2 ndc = (position / screen_size) * 2.0 - 1.0;
            ndc.y = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
        }
    )";
    
    const char* fragment_shader_text = R"(
        precision mediump float;
        uniform sampler2D tex;
        uniform vec4 color;
        varying vec2 v_texcoord;
        void main() {
            float alpha = texture2D(tex, v_texcoord).r;
            gl_FragColor = vec4(color.rgb, color.a * alpha);
        }
    )";

    static void page_flip_handler(int fd, unsigned int frame, unsigned int sec, 
                                   unsigned int usec, void* data) {
        (void)fd; (void)frame; (void)sec; (void)usec;
        bool* waiting = (bool*)data;
        *waiting = false;
    }

    GLuint compile_shader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "❌ Shader error: " << log << std::endl;
            return 0;
        }
        return shader;
    }

    GLuint create_program(const char* vs, const char* fs) {
        GLuint vertex = compile_shader(GL_VERTEX_SHADER, vs);
        GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fs);
        
        if (!vertex || !fragment) return 0;
        
        GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(program, 512, nullptr, log);
            std::cerr << "❌ Link error: " << log << std::endl;
            return 0;
        }
        
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        
        return program;
    }

    int detect_display_rotation() {
        drmModePlaneRes* plane_res = drmModeGetPlaneResources(drm_fd);
        if (plane_res) {
            for (uint32_t i = 0; i < plane_res->count_planes; i++) {
                drmModePlane* plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);
                if (plane && plane->crtc_id == crtc->crtc_id) {
                    drmModeObjectProperties* props = drmModeObjectGetProperties(drm_fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
                    if (props) {
                        for (uint32_t j = 0; j < props->count_props; j++) {
                            drmModePropertyRes* prop = drmModeGetProperty(drm_fd, props->props[j]);
                            if (prop && strcmp(prop->name, "rotation") == 0) {
                                uint64_t rotation_value = props->prop_values[j];
                                drmModeFreeProperty(prop);
                                drmModeFreeObjectProperties(props);
                                drmModeFreePlane(plane);
                                drmModeFreePlaneResources(plane_res);
                                
                                if (rotation_value & (1 << 0)) return 0;
                                if (rotation_value & (1 << 1)) return 90;
                                if (rotation_value & (1 << 2)) return 180;
                                if (rotation_value & (1 << 3)) return 270;
                            }
                            if (prop) drmModeFreeProperty(prop);
                        }
                        drmModeFreeObjectProperties(props);
                    }
                    drmModeFreePlane(plane);
                }
            }
            drmModeFreePlaneResources(plane_res);
        }
        
        std::ifstream rotate_file("/sys/class/graphics/fbcon/rotate");
        if (rotate_file.good()) {
            int rot;
            rotate_file >> rot;
            return rot * 90;
        }
        
        return 0;
    }

    bool setup_font(const char* font_path, int size) {
        font_size = size;
        
        if (FT_Init_FreeType(&ft_library)) {
            std::cerr << "❌ FreeType init failed" << std::endl;
            return false;
        }
        
        if (FT_New_Face(ft_library, font_path, 0, &ft_face)) {
            std::cerr << "❌ Failed to load font: " << font_path << std::endl;
            return false;
        }
        
        FT_Set_Pixel_Sizes(ft_face, 0, size);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        
        for (unsigned char c = 32; c < 128; c++) {
            if (FT_Load_Char(ft_face, c, FT_LOAD_RENDER)) {
                continue;
            }
            
            GLuint texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                ft_face->glyph->bitmap.width, ft_face->glyph->bitmap.rows,
                0, GL_LUMINANCE, GL_UNSIGNED_BYTE, ft_face->glyph->bitmap.buffer);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            
            Glyph glyph = {
                texture,
                (int)ft_face->glyph->bitmap.width,
                (int)ft_face->glyph->bitmap.rows,
                ft_face->glyph->bitmap_left,
                ft_face->glyph->bitmap_top,
                (int)(ft_face->glyph->advance.x >> 6)
            };
            
            glyphs[c] = glyph;
        }
        
        std::cout << "✅ Font loaded: " << font_path << " (" << size << "px)" << std::endl;
        return true;
    }

public:
    Renderer() 
        : drm_fd(-1), connector(nullptr), crtc(nullptr), gbm_dev(nullptr),
          gbm_surf(nullptr), previous_bo(nullptr), previous_fb(0),
          egl_display(EGL_NO_DISPLAY), egl_surface(EGL_NO_SURFACE),
          egl_context(EGL_NO_CONTEXT), waiting_for_flip(false),
          width(0), height(0), render_width(0), render_height(0),
          render_scale(1.0f), display_rotation(0),
          shader_program_dither(0), shader_program_solid(0), shader_program_text(0),
          vbo(0), text_vbo(0), ft_library(nullptr), ft_face(nullptr), font_size(32) {}

    ~Renderer() {
        cleanup();
    }

    bool initialize() {
        // DRM Setup
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

        if (drm_fd < 0) return false;

        drmModeRes* resources = drmModeGetResources(drm_fd);
        if (!resources) return false;

        for (int i = 0; i < resources->count_connectors; i++) {
            connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
            if (connector && connector->connection == DRM_MODE_CONNECTED) {
                connector_id = connector->connector_id;
                mode = connector->modes[0];
                break;
            }
            drmModeFreeConnector(connector);
            connector = nullptr;
        }
        drmModeFreeResources(resources);

        if (!connector) return false;

        width = mode.hdisplay;
        height = mode.vdisplay;
        
        render_width = (uint32_t)(width * render_scale);
        render_height = (uint32_t)(height * render_scale);
        render_width = (render_width / 2) * 2;
        render_height = (render_height / 2) * 2;
        
        std::cout << "📺 Display: " << width << "x" << height << std::endl;
        std::cout << "🎨 Render: " << render_width << "x" << render_height 
                  << " (" << (int)(render_scale * 100) << "%)" << std::endl;

        // CRTC Setup
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
                                goto found;
                            }
                        }
                        drmModeFreeEncoder(encoder);
                    }
                }
                found:
                drmModeFreeResources(res);
            }
        }

        if (crtc_id == 0) return false;
        crtc = drmModeGetCrtc(drm_fd, crtc_id);
        if (!crtc) return false;

        display_rotation = detect_display_rotation();
        std::cout << "🔄 Display rotation: " << display_rotation << "°" << std::endl;

        // GBM Setup
        gbm_dev = gbm_create_device(drm_fd);
        if (!gbm_dev) return false;

        gbm_surf = gbm_surface_create(gbm_dev, render_width, render_height,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!gbm_surf) return false;

        // EGL Setup
        egl_display = eglGetDisplay((EGLNativeDisplayType)gbm_dev);
        if (egl_display == EGL_NO_DISPLAY) return false;

        if (!eglInitialize(egl_display, nullptr, nullptr)) return false;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;

        EGLConfig configs[64];
        EGLint num_configs = 0;
        if (!eglGetConfigs(egl_display, configs, 64, &num_configs)) return false;

        for (int i = 0; i < num_configs; i++) {
            EGLint r, g, b, a, surface_type, renderable;
            eglGetConfigAttrib(egl_display, configs[i], EGL_RED_SIZE, &r);
            eglGetConfigAttrib(egl_display, configs[i], EGL_GREEN_SIZE, &g);
            eglGetConfigAttrib(egl_display, configs[i], EGL_BLUE_SIZE, &b);
            eglGetConfigAttrib(egl_display, configs[i], EGL_ALPHA_SIZE, &a);
            eglGetConfigAttrib(egl_display, configs[i], EGL_SURFACE_TYPE, &surface_type);
            eglGetConfigAttrib(egl_display, configs[i], EGL_RENDERABLE_TYPE, &renderable);
            
            if (!(surface_type & EGL_WINDOW_BIT)) continue;
            if (!(renderable & EGL_OPENGL_ES2_BIT)) continue;
            if (r == 8 && g == 8 && b == 8 && a == 0) {
                EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
                egl_context = eglCreateContext(egl_display, configs[i], EGL_NO_CONTEXT, ctx_attrs);
                if (egl_context == EGL_NO_CONTEXT) continue;
                
                egl_surface = eglCreateWindowSurface(egl_display, configs[i], 
                                                     (EGLNativeWindowType)gbm_surf, nullptr);
                if (egl_surface != EGL_NO_SURFACE) break;
                
                eglDestroyContext(egl_display, egl_context);
                egl_context = EGL_NO_CONTEXT;
            }
        }
        
        if (egl_surface == EGL_NO_SURFACE) return false;
        if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) return false;

        // OpenGL Setup
        shader_program_dither = create_program(vertex_shader, fragment_shader_dither);
        shader_program_solid = create_program(vertex_shader, fragment_shader_solid);
        shader_program_text = create_program(vertex_shader_text, fragment_shader_text);
        
        if (!shader_program_dither || !shader_program_solid || !shader_program_text) return false;
        
        float vertices[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
        
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        glGenBuffers(1, &text_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
        
        glViewport(0, 0, render_width, render_height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        // Font Setup
        int scaled_font_size = (int)(32 * render_scale);
        const char* font_paths[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
        };
        
        bool font_loaded = false;
        for (const char* path : font_paths) {
            if (setup_font(path, scaled_font_size)) {
                font_loaded = true;
                break;
            }
        }
        
        if (!font_loaded) {
            std::cerr << "⚠️  No font loaded" << std::endl;
        }

        eglSwapInterval(egl_display, 1);
        return true;
    }

    void begin_frame() {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void end_frame() {
        glFinish();
        eglSwapBuffers(egl_display, egl_surface);

        struct gbm_bo* next_bo = gbm_surface_lock_front_buffer(gbm_surf);
        if (!next_bo) return;

        uint32_t fb_id = 0;
        uint32_t handle = gbm_bo_get_handle(next_bo).u32;
        uint32_t pitch = gbm_bo_get_stride(next_bo);

        int ret = drmModeAddFB(drm_fd, render_width, render_height, 24, 32, pitch, handle, &fb_id);
        if (ret) {
            gbm_surface_release_buffer(gbm_surf, next_bo);
            return;
        }

        if (waiting_for_flip) {
            drmEventContext ev_ctx;
            memset(&ev_ctx, 0, sizeof(ev_ctx));
            ev_ctx.version = 2;
            ev_ctx.page_flip_handler = page_flip_handler;
            
            struct pollfd pfd;
            pfd.fd = drm_fd;
            pfd.events = POLLIN;
            
            while (waiting_for_flip) {
                if (poll(&pfd, 1, 100) > 0) {
                    drmHandleEvent(drm_fd, &ev_ctx);
                }
            }
        }

        waiting_for_flip = true;
        ret = drmModePageFlip(drm_fd, crtc->crtc_id, fb_id, 
                              DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
        
        if (ret) {
            waiting_for_flip = false;
            drmModeSetCrtc(drm_fd, crtc->crtc_id, fb_id, 0, 0, 
                          &connector_id, 1, &mode);
        }

        if (previous_bo) {
            drmModeRmFB(drm_fd, previous_fb);
            gbm_surface_release_buffer(gbm_surf, previous_bo);
        }

        previous_bo = next_bo;
        previous_fb = fb_id;
    }

    // Drawing primitives
    void draw_rect(float x, float y, float w, float h, const Color& color) {
        glUseProgram(shader_program_solid);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        GLint screen_loc = glGetUniformLocation(shader_program_solid, "screen_size");
        glUniform2f(screen_loc, (float)render_width, (float)render_height);

        GLint rect_loc = glGetUniformLocation(shader_program_solid, "rect");
        glUniform4f(rect_loc, x, y, w, h);

        GLint color_loc = glGetUniformLocation(shader_program_solid, "color");
        glUniform4f(color_loc, color.r, color.g, color.b, color.a);

        GLint pos_loc = glGetAttribLocation(shader_program_solid, "position");
        glEnableVertexAttribArray(pos_loc);
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    void draw_dithered(float x, float y, float w, float h, const Color& color, float dot_alpha = 0.133f) {
        glUseProgram(shader_program_dither);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        GLint screen_loc = glGetUniformLocation(shader_program_dither, "screen_size");
        glUniform2f(screen_loc, (float)render_width, (float)render_height);

        GLint rect_loc = glGetUniformLocation(shader_program_dither, "rect");
        glUniform4f(rect_loc, x, y, w, h);

        GLint color_loc = glGetUniformLocation(shader_program_dither, "color");
        glUniform4f(color_loc, color.r, color.g, color.b, color.a);

        GLint alpha_loc = glGetUniformLocation(shader_program_dither, "dot_alpha");
        glUniform1f(alpha_loc, dot_alpha);

        GLint pos_loc = glGetAttribLocation(shader_program_dither, "position");
        glEnableVertexAttribArray(pos_loc);
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    void draw_text(const std::string& text, float x, float y, const Color& color) {
        if (glyphs.empty()) return;
        
        glUseProgram(shader_program_text);
        glActiveTexture(GL_TEXTURE0);
        
        GLint screen_loc = glGetUniformLocation(shader_program_text, "screen_size");
        glUniform2f(screen_loc, (float)render_width, (float)render_height);
        
        GLint color_loc = glGetUniformLocation(shader_program_text, "color");
        glUniform4f(color_loc, color.r, color.g, color.b, color.a);
        
        GLint tex_loc = glGetUniformLocation(shader_program_text, "tex");
        glUniform1i(tex_loc, 0);
        
        for (char c : text) {
            if (glyphs.find(c) == glyphs.end()) continue;
            
            Glyph& glyph = glyphs[c];
            
            float xpos = x + glyph.bearing_x;
            float ypos = y - (glyph.height - glyph.bearing_y);
            float w = glyph.width;
            float h = glyph.height;
            
            float u0, v0, u1, v1, u2, v2, u3, v3;
            
            switch (display_rotation) {
                case 0:
                    u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 0.0f;
                    u2 = 1.0f; v2 = 1.0f; u3 = 0.0f; v3 = 1.0f;
                    break;
                case 90:
                    u0 = 1.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
                    u2 = 0.0f; v2 = 1.0f; u3 = 0.0f; v3 = 0.0f;
                    break;
                case 180:
                    u0 = 1.0f; v0 = 1.0f; u1 = 0.0f; v1 = 1.0f;
                    u2 = 0.0f; v2 = 0.0f; u3 = 1.0f; v3 = 0.0f;
                    break;
                case 270:
                    u0 = 0.0f; v0 = 1.0f; u1 = 0.0f; v1 = 0.0f;
                    u2 = 1.0f; v2 = 0.0f; u3 = 1.0f; v3 = 1.0f;
                    break;
                default:
                    u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 0.0f;
                    u2 = 1.0f; v2 = 1.0f; u3 = 0.0f; v3 = 1.0f;
                    break;
            }
            
            float vertices[6][4] = {
                { xpos,     ypos + h,   u3, v3 },
                { xpos,     ypos,       u0, v0 },
                { xpos + w, ypos,       u1, v1 },
                { xpos,     ypos + h,   u3, v3 },
                { xpos + w, ypos,       u1, v1 },
                { xpos + w, ypos + h,   u2, v2 }
            };
            
            glBindTexture(GL_TEXTURE_2D, glyph.texture_id);
            glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            
            GLint pos_loc = glGetAttribLocation(shader_program_text, "position");
            GLint tex_loc_attr = glGetAttribLocation(shader_program_text, "texcoord");
            
            glEnableVertexAttribArray(pos_loc);
            glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
            
            glEnableVertexAttribArray(tex_loc_attr);
            glVertexAttribPointer(tex_loc_attr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 
                                (void*)(2 * sizeof(float)));
            
            glDrawArrays(GL_TRIANGLES, 0, 6);
            
            x += glyph.advance;
        }
        
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    uint32_t get_width() const { return render_width; }
    uint32_t get_height() const { return render_height; }
    float get_scale() const { return render_scale; }

    void cleanup() {
        for (auto& pair : glyphs) {
            glDeleteTextures(1, &pair.second.texture_id);
        }
        
        if (ft_face) FT_Done_Face(ft_face);
        if (ft_library) FT_Done_FreeType(ft_library);
        
        if (waiting_for_flip) {
            drmEventContext ev_ctx;
            memset(&ev_ctx, 0, sizeof(ev_ctx));
            ev_ctx.version = 2;
            ev_ctx.page_flip_handler = page_flip_handler;
            
            struct pollfd pfd;
            pfd.fd = drm_fd;
            pfd.events = POLLIN;
            
            int timeout = 0;
            while (waiting_for_flip && timeout < 10) {
                if (poll(&pfd, 1, 100) > 0) {
                    drmHandleEvent(drm_fd, &ev_ctx);
                }
                timeout++;
            }
        }
        
        if (text_vbo) glDeleteBuffers(1, &text_vbo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (shader_program_text) glDeleteProgram(shader_program_text);
        if (shader_program_dither) glDeleteProgram(shader_program_dither);
        if (shader_program_solid) glDeleteProgram(shader_program_solid);
        
        if (previous_bo) {
            drmModeRmFB(drm_fd, previous_fb);
            gbm_surface_release_buffer(gbm_surf, previous_bo);
        }
        
        if (egl_display != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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

// ============================================
// LABEL WIDGET
// ============================================

class Label : public Widget {
private:
    std::string text;
    std::string cached_text;
    Color color;
    
public:
    Label(float x, float y, const std::string& txt, const Color& c = Color(1, 1, 1, 1))
        : Widget(x, y, 0, 0), text(txt), color(c) {}
    
    void set_text(const std::string& t) {
        if (t != text) {
            text = t;
            dirty = true;
        }
    }
    
    void set_color(const Color& c) {
        if (color.r != c.r || color.g != c.g || color.b != c.b || color.a != c.a) {
            color = c;
            dirty = true;
        }
    }
    
    const std::string& get_text() const { return text; }
    
    void draw(Renderer& renderer) override {
        if (!visible) return;
        renderer.draw_text(text, x, y, color);
    }
};

// ============================================
// FADER WIDGET
// ============================================

class Fader : public Widget {
private:
    float progress;
    float target_progress;
    SliderMode mode;
    float smooth_speed;
    int touch_start_x;
    float value_at_touch_start;
    bool is_touched;
    Color bg_color;
    Color fill_color;
    float dither_alpha;
    
public:
    Fader(float x, float y, float w, float h)
        : Widget(x, y, w, h), progress(0.5f), target_progress(0.5f),
          mode(SliderMode::JUMP), smooth_speed(0.15f), 
          is_touched(false), touch_start_x(0), value_at_touch_start(0.0f),
          bg_color(1, 1, 1, 1), fill_color(1, 1, 1, 1), dither_alpha(0.133f) {}
    
    void set_mode(SliderMode m) { mode = m; }
    void set_smooth_speed(float speed) { smooth_speed = speed; }
    
    float get_value() const { return progress; }
    
    void set_value(float v) { 
        progress = std::clamp(v, 0.0f, 1.0f);
        target_progress = progress;
        dirty = true;
    }
    
    bool is_animating() const {
        if (mode == SliderMode::SMOOTH) {
            return std::abs(target_progress - progress) > 0.001f;
        }
        return false;
    }
    
    void update(float dt) override {
        if (mode == SliderMode::SMOOTH) {
            float diff = target_progress - progress;
            if (std::abs(diff) > 0.001f) {
                progress += diff * smooth_speed;
                dirty = true;
                if (std::abs(diff) < 0.005f) {
                    progress = target_progress;
                }
            }
        }
    }
    
    void draw(Renderer& renderer) override {
        if (!visible) return;
        
        // Background (dithered)
        renderer.draw_dithered(x, y, width, height, bg_color, dither_alpha);
        
        // Fill
        float fill_width = width * progress;
        if (fill_width > 0) {
            renderer.draw_rect(x, y, fill_width, height, fill_color);
        }
    }
    
    bool handle_touch(int tx, int ty, bool down) override {
        if (!visible) return false;
        
        if (down && is_inside(tx, ty)) {
            is_touched = true;
            
            switch (mode) {
                case SliderMode::JUMP:
                    progress = (float)(tx - x) / width;
                    progress = std::clamp(progress, 0.0f, 1.0f);
                    target_progress = progress;
                    dirty = true;
                    break;
                case SliderMode::INCREMENTAL:
                    touch_start_x = tx;
                    value_at_touch_start = progress;
                    break;
                case SliderMode::SMOOTH:
                    target_progress = (float)(tx - x) / width;
                    target_progress = std::clamp(target_progress, 0.0f, 1.0f);
                    dirty = true;
                    break;
            }
            return true;
        }
        
        if (!down && is_touched) {
            is_touched = false;
            return true;
        }
        
        if (is_touched) {
            switch (mode) {
                case SliderMode::JUMP:
                    if (is_inside(tx, ty)) {
                        progress = (float)(tx - x) / width;
                        progress = std::clamp(progress, 0.0f, 1.0f);
                        target_progress = progress;
                        dirty = true;
                    }
                    break;
                case SliderMode::INCREMENTAL:
                    {
                        int delta = tx - touch_start_x;
                        progress = value_at_touch_start + (float)delta / width;
                        progress = std::clamp(progress, 0.0f, 1.0f);
                        target_progress = progress;
                        dirty = true;
                    }
                    break;
                case SliderMode::SMOOTH:
                    if (is_inside(tx, ty)) {
                        target_progress = (float)(tx - x) / width;
                        target_progress = std::clamp(target_progress, 0.0f, 1.0f);
                        dirty = true;
                    }
                    break;
            }
            return true;
        }
        
        return false;
    }
};

// ============================================
// TOUCH MANAGER
// ============================================

class TouchManager {
private:
    int touch_fd;
    std::string touch_device_path;
    int current_slot;
    std::array<TouchSlot, 10> touch_slots;
    float scale;
    
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
                            if ((abs_bitmask[ABS_MT_POSITION_X/8] & (1 << (ABS_MT_POSITION_X % 8))) ||
                                (abs_bitmask[ABS_X/8] & (1 << (ABS_X % 8)))) {
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

    void process_multitouch_event(const struct input_event& ev, std::vector<std::unique_ptr<Widget>>& widgets) {
        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            process_complete_touch_frame(widgets);
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

    void process_complete_touch_frame(std::vector<std::unique_ptr<Widget>>& widgets) {
        for (auto& ts : touch_slots) {
            int scaled_x = (int)(ts.x * scale);
            int scaled_y = (int)(ts.y * scale);
            
            if (ts.pending_touch && ts.has_position) {
                ts.active = true;
                ts.down_sent = false;
                ts.pending_touch = false;
                ts.has_position = false;
                
                if (!ts.down_sent) {
                    ts.down_sent = true;
                    // Reverse order for proper overlap handling
                    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
                        if ((*it)->handle_touch(scaled_x, scaled_y, true)) {
                            break;
                        }
                    }
                }
            }
            else if (ts.pending_release) {
                if (ts.active) {
                    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
                        if ((*it)->handle_touch(scaled_x, scaled_y, false)) {
                            break;
                        }
                    }
                    ts.active = false;
                    ts.down_sent = false;
                    ts.tracking_id = -1;
                }
                ts.pending_release = false;
                ts.has_position = false;
            }
            else if (ts.active && ts.has_position) {
                for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
                    if ((*it)->handle_touch(scaled_x, scaled_y, true)) {
                        break;
                    }
                }
                ts.has_position = false;
            }
        }
    }

public:
    TouchManager(float render_scale) 
        : touch_fd(-1), current_slot(0), scale(render_scale) {}

    ~TouchManager() {
        if (touch_fd >= 0) close(touch_fd);
    }

    bool initialize() {
        touch_device_path = autodetect_touch();
        touch_fd = open(touch_device_path.c_str(), O_RDONLY | O_NONBLOCK);
        return touch_fd >= 0;
    }

    bool process_events(std::vector<std::unique_ptr<Widget>>& widgets) {
        struct input_event ev;
        bool had_events = false;
        while (read(touch_fd, &ev, sizeof(ev)) > 0) {
            process_multitouch_event(ev, widgets);
            had_events = true;
        }
        return had_events;
    }

    bool has_active_touch() const {
        for (const auto& ts : touch_slots) {
            if (ts.active) return true;
        }
        return false;
    }
};

// ============================================
// LAYOUT - Widget Container
// ============================================

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
    
    void update(float dt) {
        for (auto& w : widgets) {
            w->update(dt);
        }
    }
    
    void draw(Renderer& renderer) {
        for (auto& w : widgets) {
            w->draw(renderer);
        }
    }
    
    bool has_animation() const {
        for (const auto& w : widgets) {
            // Check if it's a Fader and animating
            if (auto* fader = dynamic_cast<Fader*>(w.get())) {
                if (fader->is_animating()) return true;
            }
        }
        return false;
    }
    
    std::vector<std::unique_ptr<Widget>>& get_widgets() {
        return widgets;
    }
};

// ============================================
// MAIN APPLICATION
// ============================================

int main() {
    std::cout << "🎨 GPU-Accelerated UI - OOP Architecture!" << std::endl;
    
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
    TouchManager touch(renderer.get_scale());
    if (!touch.initialize()) {
        std::cerr << "⚠️  Touch init failed" << std::endl;
    }
    
    // UI Layout
    Layout ui;
    
    // 🎨 Create UI - SUPER CLEAN!
    auto* title = ui.add_widget<Label>(50, 100, "GPU Fader - OOP!", Color(1, 1, 1, 1));
    
    auto* fader = ui.add_widget<Fader>(100, 240, 600, 80);
    fader->set_mode(SliderMode::JUMP);
    fader->set_smooth_speed(0.15f);
    
    auto* value_label = ui.add_widget<Label>(370, 190, "50%", Color(1, 1, 1, 1));
    
    auto* perf_label = ui.add_widget<Label>(50, 380, "16.6 ms", Color(0.7f, 0.7f, 0.7f, 1));
    
    auto* mode_label = ui.add_widget<Label>(50, 430, "Mode: JUMP", Color(1, 1, 1, 1));
    
    std::cout << "\n🎮 Adaptive Rendering (10/60 FPS)!" << std::endl;
    
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
        
        // Update Widgets (animations etc.)
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