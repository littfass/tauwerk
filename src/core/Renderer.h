#pragma once

#include <string>
#include <map>
#include <cstdint>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "Types.h"

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
    std::map<FontCacheKey, std::map<char, Glyph>> font_cache;
    std::map<FontCacheKey, FontMetrics> font_metrics;
    bool ft_initialized;
    
    // Shader sources
    const char* vertex_shader;
    const char* fragment_shader_dither;
    const char* fragment_shader_solid;
    const char* vertex_shader_text;
    const char* fragment_shader_text;

    // Private methods
    static void page_flip_handler(int fd, unsigned int frame, unsigned int sec, 
                                   unsigned int usec, void* data);
    GLuint compile_shader(GLenum type, const char* source);
    GLuint create_program(const char* vs, const char* fs);
    int detect_display_rotation();
    bool load_font(FontType type, int size);
    const char* get_font_path(FontType type);
    std::map<char, Glyph>* get_font_glyphs(FontType type, int size);
    FontMetrics* get_font_metrics(FontType type, int size);

public:
    Renderer();
    ~Renderer();
    
    bool initialize();
    void cleanup();
    
    void begin_frame();
    void end_frame();
    
    // Drawing primitives
    void draw_rect(float x, float y, float w, float h, const Color& color);
    void draw_rect_inverted(float x, float y, float w, float h);
    void draw_dithered(float x, float y, float w, float h, const Color& color, float dot_alpha = 0.133f);
    void draw_text(const std::string& text, float x, float y, const Color& color, 
                   FontType font = FontType::DEFAULT, int size = 24);
    void draw_text_inverted(const std::string& text, float x, float y,
                            FontType font = FontType::DEFAULT, int size = 24);
    float get_text_width(const std::string& text, FontType font = FontType::DEFAULT, int size = 24);
    
    // Getters
    uint32_t get_width() const { return render_width; }
    uint32_t get_height() const { return render_height; }
    float get_scale() const { return render_scale; }
};