#include "Renderer.h"
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <poll.h>
#include <EGL/eglext.h>
#include <algorithm>

Renderer::Renderer() 
    : drm_fd(-1), connector(nullptr), crtc(nullptr), gbm_dev(nullptr),
      gbm_surf(nullptr), previous_bo(nullptr), previous_fb(0),
      egl_display(EGL_NO_DISPLAY), egl_surface(EGL_NO_SURFACE),
      egl_context(EGL_NO_CONTEXT), waiting_for_flip(false),
      width(0), height(0), render_width(0), render_height(0),
      render_scale(1.0f), display_rotation(0),
      shader_program_dither(0), shader_program_solid(0), shader_program_text(0),
      vbo(0), text_vbo(0), ft_library(nullptr), ft_initialized(false)
{
    // Shader sources
    vertex_shader = R"(
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
    
    fragment_shader_dither = R"(
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
    
    fragment_shader_solid = R"(
        precision mediump float;
        uniform vec4 color;
        void main() {
            gl_FragColor = color;
        }
    )";
    
    vertex_shader_text = R"(
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
    
    fragment_shader_text = R"(
        precision mediump float;
        uniform sampler2D tex;
        uniform vec4 color;
        varying vec2 v_texcoord;
        void main() {
            float alpha = texture2D(tex, v_texcoord).r;
            gl_FragColor = vec4(color.rgb, color.a * alpha);
        }
    )";
}

Renderer::~Renderer() {
    cleanup();
}

void Renderer::page_flip_handler(int fd, unsigned int frame, unsigned int sec, 
                                 unsigned int usec, void* data) {
    (void)fd; (void)frame; (void)sec; (void)usec;
    bool* waiting = (bool*)data;
    *waiting = false;
}

GLuint Renderer::compile_shader(GLenum type, const char* source) {
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

GLuint Renderer::create_program(const char* vs, const char* fs) {
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

int Renderer::detect_display_rotation() {
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

const char* Renderer::get_font_path(FontType type) {
    switch (type) {
        case FontType::DEFAULT:
            return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        case FontType::DIGITAL:
            return "/home/tauwerk/assets/fonts/ds_digital/ds_digi_bold.ttf";
        case FontType::ICONS:
            return "/home/tauwerk/assets/fonts/tauwerk/tauwerk.ttf";
        default:
            return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    }
}

std::map<char, Glyph>* Renderer::get_font_glyphs(FontType type, int size) {
    FontCacheKey key = {type, size};
    
    if (font_cache.find(key) != font_cache.end()) {
        return &font_cache[key];
    }
    
    if (load_font(type, size)) {
        return &font_cache[key];
    }
    
    return nullptr;
}

FontMetrics* Renderer::get_font_metrics(FontType type, int size) {
    FontCacheKey key = {type, size};
    
    if (font_metrics.find(key) != font_metrics.end()) {
        return &font_metrics[key];
    }
    
    if (load_font(type, size)) {
        return &font_metrics[key];
    }
    
    return nullptr;
}

bool Renderer::load_font(FontType type, int size) {
    if (!ft_initialized) {
        if (FT_Init_FreeType(&ft_library)) {
            std::cerr << "❌ FreeType init failed" << std::endl;
            return false;
        }
        ft_initialized = true;
    }
    
    FontCacheKey key = {type, size};
    if (font_cache.find(key) != font_cache.end()) {
        return true;
    }
    
    const char* font_path = get_font_path(type);
    
    FT_Face face;
    if (FT_New_Face(ft_library, font_path, 0, &face)) {
        std::cerr << "❌ Failed to load font: " << font_path << std::endl;
        return false;
    }
    
    FT_Set_Pixel_Sizes(face, 0, size);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    // Extract font metrics
    FontMetrics metrics;
    metrics.ascender = face->size->metrics.ascender >> 6;
    metrics.descender = face->size->metrics.descender >> 6;
    metrics.line_height = face->size->metrics.height >> 6;
    font_metrics[key] = metrics;
    
    std::map<char, Glyph> glyphs;
    
    // Load ASCII glyphs (32-127)
    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            continue;
        }
        
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
            face->glyph->bitmap.width, face->glyph->bitmap.rows,
            0, GL_LUMINANCE, GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        Glyph glyph = {
            texture,
            (int)face->glyph->bitmap.width,
            (int)face->glyph->bitmap.rows,
            face->glyph->bitmap_left,
            face->glyph->bitmap_top,
            (int)(face->glyph->advance.x >> 6)
        };
        
        glyphs[c] = glyph;
    }
    
    // Load Icon glyphs for ICONS font (Private Use Area 0xE000-0xF8FF)
    if (type == FontType::ICONS) {
        // Icon glyphs: 0xE801, 0xE803
        unsigned int icon_codepoints[] = {0xE801, 0xE803};
        
        for (unsigned int codepoint : icon_codepoints) {
            if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER)) {
                std::cerr << "⚠️  Icon glyph 0x" << std::hex << codepoint << " not found" << std::dec << std::endl;
                continue;
            }
            
            GLuint texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                face->glyph->bitmap.width, face->glyph->bitmap.rows,
                0, GL_LUMINANCE, GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            
            Glyph glyph = {
                texture,
                (int)face->glyph->bitmap.width,
                (int)face->glyph->bitmap.rows,
                face->glyph->bitmap_left,
                face->glyph->bitmap_top,
                (int)(face->glyph->advance.x >> 6)
            };
            
            // Map Unicode codepoint to UTF-8 last byte
            // 0xE801 → UTF-8: 0xEE 0xA0 0x81 → use 0x81 as key
            // 0xE803 → UTF-8: 0xEE 0xA0 0x83 → use 0x83 as key
            // This works because we check the 3rd byte in draw_text()
            char key_char = (char)(0x80 | (codepoint & 0x7F));
            glyphs[key_char] = glyph;
        }
    }
    
    font_cache[key] = glyphs;
    FT_Done_Face(face);
    
    const char* type_name = (type == FontType::DEFAULT) ? "DEFAULT" : 
                           (type == FontType::DIGITAL) ? "DIGITAL" : "ICONS";
    std::cout << "✅ Font loaded: " << type_name << " (" << size << "px)" << std::endl;
    return true;
}

bool Renderer::initialize() {
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

    // Fonts werden lazy bei Bedarf geladen
    eglSwapInterval(egl_display, 1);
    return true;
}

void Renderer::begin_frame() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::end_frame() {
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

void Renderer::draw_rect(float x, float y, float w, float h, const Color& color) {
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

void Renderer::draw_rect_inverted(float x, float y, float w, float h) {
    // Invert BlendMode für Rect-Overlay
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
    
    glUseProgram(shader_program_solid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    GLint screen_loc = glGetUniformLocation(shader_program_solid, "screen_size");
    glUniform2f(screen_loc, (float)render_width, (float)render_height);

    GLint rect_loc = glGetUniformLocation(shader_program_solid, "rect");
    glUniform4f(rect_loc, x, y, w, h);

    GLint color_loc = glGetUniformLocation(shader_program_solid, "color");
    glUniform4f(color_loc, 1.0f, 1.0f, 1.0f, 1.0f);

    GLint pos_loc = glGetAttribLocation(shader_program_solid, "position");
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Restore normal blend mode
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::draw_dithered(float x, float y, float w, float h, const Color& color, float dot_alpha) {
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

void Renderer::draw_text(const std::string& text, float x, float y, const Color& color,
                         FontType font, int size) {
    std::map<char, Glyph>* glyphs = get_font_glyphs(font, size);
    if (!glyphs || glyphs->empty()) return;
    
    FontMetrics* metrics = get_font_metrics(font, size);
    if (!metrics) return;
    
    // Adjust y to position text with top-left origin
    // y is now the TOP of the text bounding box
    float baseline_y = y + metrics->ascender;
    
    glUseProgram(shader_program_text);
    glActiveTexture(GL_TEXTURE0);
    
    GLint screen_loc = glGetUniformLocation(shader_program_text, "screen_size");
    glUniform2f(screen_loc, (float)render_width, (float)render_height);
    
    GLint color_loc = glGetUniformLocation(shader_program_text, "color");
    glUniform4f(color_loc, color.r, color.g, color.b, color.a);
    
    GLint tex_loc = glGetUniformLocation(shader_program_text, "tex");
    glUniform1i(tex_loc, 0);
    
    // UTF-8 parsing: detect 3-byte sequences for icon glyphs
    for (size_t i = 0; i < text.length(); ) {
        char c = text[i];
        
        // Check for UTF-8 3-byte sequence (0xEE 0xA0 0x8X for Private Use Area)
        if (i + 2 < text.length() &&
            (unsigned char)text[i] == 0xEE &&
            (unsigned char)text[i+1] == 0xA0 &&
            ((unsigned char)text[i+2] & 0x80) == 0x80) {
            // Extract third byte as glyph key
            c = text[i+2];
            i += 3;
        } else {
            i++;
        }
        
        if (glyphs->find(c) == glyphs->end()) continue;
        
        Glyph& glyph = (*glyphs)[c];
        
        float xpos = x + glyph.bearing_x;
        float ypos = baseline_y - glyph.bearing_y;
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

void Renderer::draw_text_inverted(const std::string& text, float x, float y,
                                   FontType font, int size) {
    std::map<char, Glyph>* glyphs = get_font_glyphs(font, size);
    if (!glyphs || glyphs->empty()) return;
    
    FontMetrics* metrics = get_font_metrics(font, size);
    if (!metrics) return;
    
    // Adjust y to position text with top-left origin
    float baseline_y = y + metrics->ascender;
    
    // Invert BlendMode: Text invertiert Hintergrund
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
    
    glUseProgram(shader_program_text);
    glActiveTexture(GL_TEXTURE0);
    
    GLint screen_loc = glGetUniformLocation(shader_program_text, "screen_size");
    glUniform2f(screen_loc, (float)render_width, (float)render_height);
    
    // Weiße Farbe für Invert-Effekt
    GLint color_loc = glGetUniformLocation(shader_program_text, "color");
    glUniform4f(color_loc, 1.0f, 1.0f, 1.0f, 1.0f);
    
    GLint tex_loc = glGetUniformLocation(shader_program_text, "tex");
    glUniform1i(tex_loc, 0);
    
    // UTF-8 parsing: detect 3-byte sequences for icon glyphs
    for (size_t i = 0; i < text.length(); ) {
        char c = text[i];
        
        // Check for UTF-8 3-byte sequence (0xEE 0xA0 0x8X for Private Use Area)
        if (i + 2 < text.length() &&
            (unsigned char)text[i] == 0xEE &&
            (unsigned char)text[i+1] == 0xA0 &&
            ((unsigned char)text[i+2] & 0x80) == 0x80) {
            // Extract third byte as glyph key
            c = text[i+2];
            i += 3;
        } else {
            i++;
        }
        
        if (glyphs->find(c) == glyphs->end()) continue;
        
        Glyph& glyph = (*glyphs)[c];
        
        float xpos = x + glyph.bearing_x;
        float ypos = baseline_y - glyph.bearing_y;
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
    
    // BlendMode zurücksetzen
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

float Renderer::get_text_width(const std::string& text, FontType font, int size) {
    std::map<char, Glyph>* glyphs = get_font_glyphs(font, size);
    if (!glyphs || glyphs->empty()) return 0.0f;
    
    float width = 0.0f;
    
    // UTF-8 parsing: same logic as draw_text
    for (size_t i = 0; i < text.length(); ) {
        char c = text[i];
        
        // Check for UTF-8 3-byte sequence
        if (i + 2 < text.length() &&
            (unsigned char)text[i] == 0xEE &&
            (unsigned char)text[i+1] == 0xA0 &&
            ((unsigned char)text[i+2] & 0x80) == 0x80) {
            c = text[i+2];
            i += 3;
        } else {
            i++;
        }
        
        if (glyphs->find(c) == glyphs->end()) continue;
        width += (*glyphs)[c].advance;
    }
    
    return width;
}

void Renderer::cleanup() {
    for (auto& cache_entry : font_cache) {
        for (auto& glyph_pair : cache_entry.second) {
            glDeleteTextures(1, &glyph_pair.second.texture_id);
        }
    }
    font_cache.clear();
    
    if (ft_initialized && ft_library) {
        FT_Done_FreeType(ft_library);
        ft_initialized = false;
    }
    
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