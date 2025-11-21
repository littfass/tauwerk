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
    FT_Face ft_face;
    std::map<char, Glyph> glyphs;
    int font_size;
    
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
    bool setup_font(const char* font_path, int size);

public:
    Renderer();
    ~Renderer();
    
    bool initialize();
    void cleanup();
    
    void begin_frame();
    void end_frame();
    
    // Drawing primitives
    void draw_rect(float x, float y, float w, float h, const Color& color);
    void draw_dithered(float x, float y, float w, float h, const Color& color, float dot_alpha = 0.133f);
    void draw_text(const std::string& text, float x, float y, const Color& color);
    
    // Getters
    uint32_t get_width() const { return render_width; }
    uint32_t get_height() const { return render_height; }
    float get_scale() const { return render_scale; }
};