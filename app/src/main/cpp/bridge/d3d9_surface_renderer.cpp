#include "d3d9_surface_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ventura {

namespace {

constexpr uint32_t kFvfXyzRhw  = 0x004u;
constexpr uint32_t kFvfNormal  = 0x010u;
constexpr uint32_t kFvfDiffuse = 0x040u;
constexpr uint32_t kFvfTex1    = 0x100u;

constexpr uint32_t kFourCcDxt1 = 0x31545844u; // 'DXT1'
constexpr uint32_t kFourCcDxt3 = 0x33545844u; // 'DXT3'
constexpr uint32_t kFourCcDxt5 = 0x35545844u; // 'DXT5'

struct PackedGpuVertex {
    float x, y, z, w;
    uint8_t r, g, b, a;
    float u, v;
};

void SetIdentityMatrix(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void MultiplyMatrix4x4(const float* a, const float* b, float* out) {
    float res[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            res[r * 4 + c] =
                a[r * 4 + 0] * b[0 * 4 + c] +
                a[r * 4 + 1] * b[1 * 4 + c] +
                a[r * 4 + 2] * b[2 * 4 + c] +
                a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
    std::memcpy(out, res, 16 * sizeof(float));
}

void UnpackRgb565(uint16_t c, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint32_t rv = (c >> 11) & 0x1Fu;
    uint32_t gv = (c >> 5) & 0x3Fu;
    uint32_t bv = c & 0x1Fu;
    *r = static_cast<uint8_t>((rv * 255u + 15u) / 31u);
    *g = static_cast<uint8_t>((gv * 255u + 31u) / 63u);
    *b = static_cast<uint8_t>((bv * 255u + 15u) / 31u);
}

bool GetGlyphPixel5x7(char c, int gx, int gy) {
    if (c <= 32 || c > 126 || gx < 0 || gx >= 5 || gy < 0 || gy >= 7) return false;
    static const uint8_t kFont5x7[95][5] = {
        {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
        {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
        {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
        {0x00,0x41,0x22,0x1C,0x00}, {0x08,0x2A,0x1C,0x2A,0x08}, {0x08,0x08,0x3E,0x08,0x08},
        {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
        {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
        {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
        {0x00,0x56,0x36,0x00,0x00}, {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14},
        {0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
        {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x01,0x01},
        {0x3E,0x41,0x41,0x51,0x32}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
        {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x04,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
        {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
        {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F}, {0x63,0x14,0x08,0x14,0x63},
        {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x00,0x7F,0x41,0x41},
        {0x02,0x04,0x08,0x10,0x20}, {0x41,0x41,0x7F,0x00,0x00}, {0x04,0x02,0x01,0x02,0x04},
        {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
        {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
        {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C},
        {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
        {0x00,0x7F,0x10,0x28,0x44}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
        {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
        {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
        {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
        {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
        {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
        {0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08}
    };
    uint8_t col_bits = kFont5x7[static_cast<int>(c) - 32][gx];
    return ((col_bits >> gy) & 1u) != 0;
}

}  // namespace

D3D9SurfaceRenderer::D3D9SurfaceRenderer() {
    SetIdentityMatrix(world_matrix_);
    SetIdentityMatrix(view_matrix_);
    SetIdentityMatrix(proj_matrix_);
    SetIdentityMatrix(mvp_matrix_);
}

D3D9SurfaceRenderer::~D3D9SurfaceRenderer() {
    DetachWindow();
}

void D3D9SurfaceRenderer::DecompressS3tcToRgba8(
    uint32_t width,
    uint32_t height,
    uint32_t d3d_format,
    const uint8_t* src_bytes,
    uint32_t src_size,
    std::vector<uint8_t>& out_rgba
) {
    out_rgba.assign(static_cast<size_t>(width) * height * 4u, 0xFFu);
    if (!src_bytes || src_size == 0 || width == 0 || height == 0) return;

    if (d3d_format == 21 || d3d_format == 22) {
        uint32_t count = std::min<uint32_t>(width * height, src_size / 4u);
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t b = src_bytes[i * 4 + 0];
            uint8_t g = src_bytes[i * 4 + 1];
            uint8_t r = src_bytes[i * 4 + 2];
            uint8_t a = (d3d_format == 22) ? 0xFFu : src_bytes[i * 4 + 3];
            out_rgba[i * 4 + 0] = r;
            out_rgba[i * 4 + 1] = g;
            out_rgba[i * 4 + 2] = b;
            out_rgba[i * 4 + 3] = a;
        }
        return;
    }

    if (d3d_format != kFourCcDxt1 && d3d_format != kFourCcDxt3 && d3d_format != kFourCcDxt5) {
        return;
    }

    uint32_t blocks_x = (width + 3u) / 4u;
    uint32_t blocks_y = (height + 3u) / 4u;
    uint32_t block_size = (d3d_format == kFourCcDxt1) ? 8u : 16u;
    uint32_t offset = 0;

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            if (offset + block_size > src_size) return;
            const uint8_t* block = src_bytes + offset;
            offset += block_size;

            uint8_t alpha_table[16];
            std::memset(alpha_table, 255, sizeof(alpha_table));

            const uint8_t* color_block = block;
            if (d3d_format == kFourCcDxt3) {
                for (int i = 0; i < 8; ++i) {
                    uint8_t b = block[i];
                    alpha_table[i * 2 + 0] = static_cast<uint8_t>((b & 0x0Fu) * 17u);
                    alpha_table[i * 2 + 1] = static_cast<uint8_t>((b >> 4) * 17u);
                }
                color_block = block + 8;
            } else if (d3d_format == kFourCcDxt5) {
                uint8_t a0 = block[0];
                uint8_t a1 = block[1];
                uint8_t apal[8];
                apal[0] = a0;
                apal[1] = a1;
                if (a0 > a1) {
                    for (int i = 1; i <= 6; ++i) {
                        apal[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1 + 3) / 7);
                    }
                } else {
                    for (int i = 1; i <= 4; ++i) {
                        apal[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1 + 2) / 5);
                    }
                    apal[6] = 0;
                    apal[7] = 255;
                }
                uint64_t abits = 0;
                for (int i = 0; i < 6; ++i) {
                    abits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                }
                for (int i = 0; i < 16; ++i) {
                    alpha_table[i] = apal[(abits >> (i * 3)) & 7u];
                }
                color_block = block + 8;
            }

            uint16_t c0 = static_cast<uint16_t>(color_block[0] | (color_block[1] << 8));
            uint16_t c1 = static_cast<uint16_t>(color_block[2] | (color_block[3] << 8));
            uint8_t r[4], g[4], b[4], a[4];
            UnpackRgb565(c0, &r[0], &g[0], &b[0]);
            UnpackRgb565(c1, &r[1], &g[1], &b[1]);
            a[0] = a[1] = a[2] = a[3] = 255;

            if (c0 > c1 || d3d_format != kFourCcDxt1) {
                r[2] = static_cast<uint8_t>((2 * r[0] + r[1] + 1) / 3);
                g[2] = static_cast<uint8_t>((2 * g[0] + g[1] + 1) / 3);
                b[2] = static_cast<uint8_t>((2 * b[0] + b[1] + 1) / 3);
                r[3] = static_cast<uint8_t>((r[0] + 2 * r[1] + 1) / 3);
                g[3] = static_cast<uint8_t>((g[0] + 2 * g[1] + 1) / 3);
                b[3] = static_cast<uint8_t>((b[0] + 2 * b[1] + 1) / 3);
            } else {
                r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2);
                g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2);
                b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2);
                r[3] = g[3] = b[3] = 0;
                a[3] = 0;
            }

            uint32_t lut = static_cast<uint32_t>(color_block[4]) |
                           (static_cast<uint32_t>(color_block[5]) << 8) |
                           (static_cast<uint32_t>(color_block[6]) << 16) |
                           (static_cast<uint32_t>(color_block[7]) << 24);

            for (uint32_t py = 0; py < 4; ++py) {
                uint32_t dst_y = by * 4 + py;
                if (dst_y >= height) continue;
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t dst_x = bx * 4 + px;
                    if (dst_x >= width) continue;
                    uint32_t idx = py * 4 + px;
                    uint32_t code = (lut >> (idx * 2)) & 3u;
                    size_t out_idx = (static_cast<size_t>(dst_y) * width + dst_x) * 4u;
                    out_rgba[out_idx + 0] = r[code];
                    out_rgba[out_idx + 1] = g[code];
                    out_rgba[out_idx + 2] = b[code];
                    out_rgba[out_idx + 3] = (d3d_format == kFourCcDxt1) ? a[code] : alpha_table[idx];
                }
            }
        }
    }
}

bool D3D9SurfaceRenderer::AttachWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef __ANDROID__
    if (window == pending_window_) {
        if (window != nullptr) {
            surface_width_ = std::max(1, ANativeWindow_getWidth(window));
            surface_height_ = std::max(1, ANativeWindow_getHeight(window));
        }
        return window != nullptr;
    }
    if (pending_window_ != nullptr) {
        ANativeWindow_release(pending_window_);
    }
    pending_window_ = window;
    if (pending_window_ != nullptr) {
        ANativeWindow_acquire(pending_window_);
        surface_width_ = std::max(1, ANativeWindow_getWidth(pending_window_));
        surface_height_ = std::max(1, ANativeWindow_getHeight(pending_window_));
    }
    window_changed_ = true;
#else
    pending_window_ = window;
    window_changed_ = true;
#endif
    return window != nullptr;
}

void D3D9SurfaceRenderer::DetachWindow() {
    AttachWindow(nullptr);
}

void D3D9SurfaceRenderer::ShutdownOnWorkerThread() {
    std::lock_guard<std::mutex> lock(mutex_);
    DestroyEglLocked();
#ifdef __ANDROID__
    if (active_window_ != nullptr) {
        ANativeWindow_release(active_window_);
        active_window_ = nullptr;
    }
    if (pending_window_ != nullptr) {
        ANativeWindow_release(pending_window_);
        pending_window_ = nullptr;
    }
#else
    active_window_ = nullptr;
    pending_window_ = nullptr;
#endif
    window_changed_ = false;
}

bool D3D9SurfaceRenderer::EnsureEglCurrentLocked() {
#ifdef __ANDROID__
    if (window_changed_) {
        DestroyEglLocked();
        if (active_window_ != nullptr) {
            ANativeWindow_release(active_window_);
            active_window_ = nullptr;
        }
        if (pending_window_ != nullptr) {
            active_window_ = pending_window_;
            ANativeWindow_acquire(active_window_);
            surface_width_ = std::max(1, ANativeWindow_getWidth(active_window_));
            surface_height_ = std::max(1, ANativeWindow_getHeight(active_window_));
        }
        window_changed_ = false;
    }

    if (active_window_ == nullptr) return false;

    if (egl_display_ == EGL_NO_DISPLAY) {
        egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl_display_ == EGL_NO_DISPLAY) return false;

        EGLint major = 0, minor = 0;
        if (!eglInitialize(egl_display_, &major, &minor)) {
            egl_display_ = EGL_NO_DISPLAY;
            return false;
        }

        const EGLint config_attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint num_configs = 0;
        if (!eglChooseConfig(egl_display_, config_attribs, &config, 1, &num_configs) || num_configs < 1) {
            return false;
        }

        const EGLint ctx_attribs3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, ctx_attribs3);
        if (egl_context_ == EGL_NO_CONTEXT) {
            const EGLint ctx_attribs2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
            egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, ctx_attribs2);
        }
        if (egl_context_ == EGL_NO_CONTEXT) return false;

        egl_surface_ = eglCreateWindowSurface(egl_display_, config, active_window_, nullptr);
        if (egl_surface_ == EGL_NO_SURFACE) return false;
    }

    if (eglGetCurrentContext() != egl_context_) {
        if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
            return false;
        }
    }

    surface_width_ = std::max(1, ANativeWindow_getWidth(active_window_));
    surface_height_ = std::max(1, ANativeWindow_getHeight(active_window_));

    if (shader_program_ == 0) {
        CompileShadersAndFontLocked();
    }
    return true;
#else
    return true;
#endif
}

bool D3D9SurfaceRenderer::CompileShadersAndFontLocked() {
#ifdef __ANDROID__
    const char* vs_src =
        "attribute vec4 a_position;\n"
        "attribute vec4 a_color;\n"
        "attribute vec2 a_texcoord;\n"
        "uniform mat4 u_mvp;\n"
        "uniform int u_is_rhw;\n"
        "uniform vec2 u_screen_size;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    if (u_is_rhw != 0) {\n"
        "        float ndc_x = (a_position.x / u_screen_size.x) * 2.0 - 1.0;\n"
        "        float ndc_y = 1.0 - (a_position.y / u_screen_size.y) * 2.0;\n"
        "        gl_Position = vec4(ndc_x, ndc_y, clamp(a_position.z, 0.0, 1.0), 1.0);\n"
        "    } else {\n"
        "        gl_Position = u_mvp * vec4(a_position.xyz, 1.0);\n"
        "    }\n"
        "    v_color = a_color;\n"
        "    v_texcoord = a_texcoord;\n"
        "}\n";

    const char* fs_src =
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "uniform sampler2D u_tex;\n"
        "void main() {\n"
        "    vec4 texel = texture2D(u_tex, v_texcoord);\n"
        "    vec4 out_color = texel * v_color;\n"
        "    if (out_color.a < 0.02) discard;\n"
        "    gl_FragColor = out_color;\n"
        "}\n";

    auto compile_shader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vs);
    glAttachShader(shader_program_, fs);
    glBindAttribLocation(shader_program_, 0, "a_position");
    glBindAttribLocation(shader_program_, 1, "a_color");
    glBindAttribLocation(shader_program_, 2, "a_texcoord");
    glLinkProgram(shader_program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_mvp_loc_ = glGetUniformLocation(shader_program_, "u_mvp");
    u_is_rhw_loc_ = glGetUniformLocation(shader_program_, "u_is_rhw");
    u_screen_size_loc_ = glGetUniformLocation(shader_program_, "u_screen_size");
    u_tex_loc_ = glGetUniformLocation(shader_program_, "u_tex");

    glGenBuffers(1, &dynamic_vbo_);
    glGenBuffers(1, &dynamic_ibo_);

    glGenTextures(1, &white_tex_);
    glBindTexture(GL_TEXTURE_2D, white_tex_);
    const uint8_t white_px[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    std::vector<uint8_t> atlas(128 * 64 * 4, 0);
    for (int ch = 32; ch <= 126; ++ch) {
        int idx = ch - 32;
        int cell_x = (idx % 16) * 8;
        int cell_y = (idx / 16) * 8;
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (GetGlyphPixel5x7(static_cast<char>(ch), gx, gy)) {
                    int px = cell_x + gx + 1;
                    int py = cell_y + gy + 1;
                    size_t off = static_cast<size_t>(py * 128 + px) * 4u;
                    atlas[off + 0] = 255;
                    atlas[off + 1] = 255;
                    atlas[off + 2] = 255;
                    atlas[off + 3] = 255;
                }
            }
        }
    }
    glGenTextures(1, &font_tex_);
    glBindTexture(GL_TEXTURE_2D, font_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif
    return true;
}

void D3D9SurfaceRenderer::DestroyEglLocked() {
#ifdef __ANDROID__
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }
        if (egl_context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display_, egl_context_);
            egl_context_ = EGL_NO_CONTEXT;
        }
        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }
#endif
    shader_program_ = 0;
    font_tex_ = 0;
    white_tex_ = 0;
    dynamic_vbo_ = 0;
    dynamic_ibo_ = 0;
}

uint32_t D3D9SurfaceRenderer::CreateVertexBuffer(
    MemoryArena& arena,
    uint32_t length_bytes,
    uint32_t fvf,
    uint32_t vtbl_va
) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t obj_va = next_com_resource_va_;
    next_com_resource_va_ += 16;
    arena.WriteU32(obj_va, vtbl_va);

    uint32_t buf_va = arena.HeapAllocSim(std::max(16u, length_bytes), false);
    D3D9VertexBufferResource res;
    res.handle_va = obj_va;
    res.data_va = buf_va;
    res.size_bytes = length_bytes;
    res.fvf = fvf;
    vertex_buffers_[obj_va] = res;
    return obj_va;
}

uint32_t D3D9SurfaceRenderer::LockVertexBuffer(uint32_t vb_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vertex_buffers_.find(vb_obj_va);
    if (it == vertex_buffers_.end()) return 0;
    it->second.dirty = true;
    return it->second.data_va;
}

void D3D9SurfaceRenderer::UnlockVertexBuffer(uint32_t vb_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vertex_buffers_.find(vb_obj_va);
    if (it != vertex_buffers_.end()) {
        it->second.dirty = true;
    }
}

uint32_t D3D9SurfaceRenderer::CreateIndexBuffer(
    MemoryArena& arena,
    uint32_t length_bytes,
    uint32_t format,
    uint32_t vtbl_va
) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t obj_va = next_com_resource_va_;
    next_com_resource_va_ += 16;
    arena.WriteU32(obj_va, vtbl_va);

    uint32_t buf_va = arena.HeapAllocSim(std::max(16u, length_bytes), false);
    D3D9IndexBufferResource res;
    res.handle_va = obj_va;
    res.data_va = buf_va;
    res.size_bytes = length_bytes;
    res.format = format;
    index_buffers_[obj_va] = res;
    return obj_va;
}

uint32_t D3D9SurfaceRenderer::LockIndexBuffer(uint32_t ib_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_buffers_.find(ib_obj_va);
    if (it == index_buffers_.end()) return 0;
    it->second.dirty = true;
    return it->second.data_va;
}

void D3D9SurfaceRenderer::UnlockIndexBuffer(uint32_t ib_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_buffers_.find(ib_obj_va);
    if (it != index_buffers_.end()) {
        it->second.dirty = true;
    }
}

uint32_t D3D9SurfaceRenderer::CreateTexture(
    MemoryArena& arena,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t vtbl_va
) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t obj_va = next_com_resource_va_;
    next_com_resource_va_ += 16;
    arena.WriteU32(obj_va, vtbl_va);

    uint32_t w = std::max(1u, width);
    uint32_t h = std::max(1u, height);
    uint32_t pitch = w * 4u;
    uint32_t byte_size = pitch * h;
    if (format == kFourCcDxt1) {
        pitch = std::max(1u, (w + 3u) / 4u) * 8u;
        byte_size = pitch * std::max(1u, (h + 3u) / 4u);
    } else if (format == kFourCcDxt3 || format == kFourCcDxt5) {
        pitch = std::max(1u, (w + 3u) / 4u) * 16u;
        byte_size = pitch * std::max(1u, (h + 3u) / 4u);
    }

    uint32_t buf_va = arena.HeapAllocSim(std::max(16u, byte_size), true);
    D3D9TextureResource tex;
    tex.handle_va = obj_va;
    tex.data_va = buf_va;
    tex.width = w;
    tex.height = h;
    tex.format = format;
    tex.pitch = pitch;
    tex.byte_size = byte_size;
    textures_[obj_va] = tex;
    return obj_va;
}

bool D3D9SurfaceRenderer::LockTextureRect(uint32_t tex_obj_va, uint32_t* out_pitch, uint32_t* out_bits_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = textures_.find(tex_obj_va);
    if (it == textures_.end()) return false;
    if (out_pitch) *out_pitch = it->second.pitch;
    if (out_bits_va) *out_bits_va = it->second.data_va;
    it->second.dirty = true;
    return true;
}

void D3D9SurfaceRenderer::UnlockTextureRect(const MemoryArena& arena, uint32_t tex_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = textures_.find(tex_obj_va);
    if (it == textures_.end()) return;
    it->second.dirty = true;
#ifdef __ANDROID__
    if (EnsureEglCurrentLocked()) {
        D3D9TextureResource& tex = it->second;
        const uint8_t* raw = arena.GetHostPointer(tex.data_va);
        if (raw && arena.IsValidRange(tex.data_va, tex.byte_size)) {
            std::vector<uint8_t> rgba;
            DecompressS3tcToRgba8(tex.width, tex.height, tex.format, raw, tex.byte_size, rgba);
            if (tex.gl_tex == 0) {
                glGenTextures(1, &tex.gl_tex);
            }
            glBindTexture(GL_TEXTURE_2D, tex.gl_tex);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                static_cast<GLsizei>(tex.width),
                static_cast<GLsizei>(tex.height),
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                rgba.data()
            );
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            tex.dirty = false;
        }
    }
#else
    (void)arena;
#endif
}

void D3D9SurfaceRenderer::SetTransform(uint32_t state_type, const float* matrix16) {
    if (!matrix16) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_type == 2) {
        std::memcpy(view_matrix_, matrix16, 16 * sizeof(float));
    } else if (state_type == 3) {
        std::memcpy(proj_matrix_, matrix16, 16 * sizeof(float));
    } else if (state_type >= 256 || state_type == 1) {
        std::memcpy(world_matrix_, matrix16, 16 * sizeof(float));
    }
    mvp_dirty_ = true;
}

void D3D9SurfaceRenderer::SetRenderState(uint32_t state_type, uint32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_type == 7) {
        depth_test_enabled_ = (value != 0);
    } else if (state_type == 27) {
        alpha_blend_enabled_ = (value != 0);
    }
}

void D3D9SurfaceRenderer::SetFVF(uint32_t fvf) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fvf != 0) active_fvf_ = fvf;
}

void D3D9SurfaceRenderer::SetStreamSource(
    uint32_t stream_num,
    uint32_t vb_obj_va,
    uint32_t offset_bytes,
    uint32_t stride
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_num == 0) {
        active_vb_va_ = vb_obj_va;
        active_vb_offset_ = offset_bytes;
        if (stride > 0) active_vb_stride_ = stride;
    }
}

void D3D9SurfaceRenderer::SetIndices(uint32_t ib_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ib_va_ = ib_obj_va;
}

void D3D9SurfaceRenderer::SetTexture(uint32_t stage, uint32_t tex_obj_va) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stage == 0) {
        active_tex0_va_ = tex_obj_va;
    }
}

void D3D9SurfaceRenderer::OnD3D9Clear(uint32_t argb_color) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_clear_color_ = argb_color;
#ifdef __ANDROID__
    if (EnsureEglCurrentLocked()) {
        float r = static_cast<float>((argb_color >> 16) & 0xFFu) / 255.0f;
        float g = static_cast<float>((argb_color >> 8) & 0xFFu) / 255.0f;
        float b = static_cast<float>(argb_color & 0xFFu) / 255.0f;
        glViewport(0, 0, surface_width_, surface_height_);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
#endif
}

void D3D9SurfaceRenderer::OnD3D9BeginScene() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_draw_calls_ = 0;
    current_triangles_ = 0;
    frame_texts_.clear();
}

void D3D9SurfaceRenderer::RenderVerticesInternalLocked(
    const uint8_t* vertex_bytes,
    uint32_t stride,
    uint32_t fvf,
    const uint8_t* index_bytes,
    bool index32,
    uint32_t prim_type,
    uint32_t start_elem,
    uint32_t prim_count
) {
    if (!vertex_bytes || stride < 12 || prim_count == 0) return;
    current_draw_calls_++;
    current_triangles_ += prim_count;

#ifdef __ANDROID__
    if (!EnsureEglCurrentLocked() || shader_program_ == 0) return;

    uint32_t vertex_count = 0;
    GLenum gl_mode = GL_TRIANGLES;
    if (prim_type == 4) {
        vertex_count = prim_count * 3;
        gl_mode = GL_TRIANGLES;
    } else if (prim_type == 5) {
        vertex_count = prim_count + 2;
        gl_mode = GL_TRIANGLE_STRIP;
    } else if (prim_type == 6) {
        vertex_count = prim_count + 2;
        gl_mode = GL_TRIANGLE_FAN;
    } else {
        return;
    }

    bool is_rhw = (fvf & kFvfXyzRhw) != 0;
    bool has_normal = !is_rhw && ((fvf & kFvfNormal) != 0);
    bool has_diffuse = (fvf & kFvfDiffuse) != 0;
    bool has_tex1 = (fvf & kFvfTex1) != 0;

    std::vector<PackedGpuVertex> packed(vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        uint32_t v_idx = start_elem + i;
        if (index_bytes != nullptr) {
            if (index32) {
                std::memcpy(&v_idx, index_bytes + (start_elem + i) * 4, sizeof(uint32_t));
            } else {
                uint16_t idx16 = 0;
                std::memcpy(&idx16, index_bytes + (start_elem + i) * 2, sizeof(uint16_t));
                v_idx = idx16;
            }
        }
        const uint8_t* vp = vertex_bytes + (v_idx * stride);
        PackedGpuVertex& dst = packed[i];
        std::memcpy(&dst.x, vp + 0, 4);
        std::memcpy(&dst.y, vp + 4, 4);
        std::memcpy(&dst.z, vp + 8, 4);
        uint32_t cursor = 12;
        if (is_rhw) {
            std::memcpy(&dst.w, vp + 12, 4);
            cursor = 16;
        } else {
            dst.w = 1.0f;
            if (has_normal) cursor += 12;
        }

        if (has_diffuse && cursor + 4 <= stride) {
            uint32_t argb = 0;
            std::memcpy(&argb, vp + cursor, 4);
            dst.b = static_cast<uint8_t>(argb & 0xFFu);
            dst.g = static_cast<uint8_t>((argb >> 8) & 0xFFu);
            dst.r = static_cast<uint8_t>((argb >> 16) & 0xFFu);
            dst.a = static_cast<uint8_t>((argb >> 24) & 0xFFu);
            cursor += 4;
        } else {
            dst.r = dst.g = dst.b = dst.a = 255;
        }

        if (has_tex1 && cursor + 8 <= stride) {
            std::memcpy(&dst.u, vp + cursor, 4);
            std::memcpy(&dst.v, vp + cursor + 4, 4);
        } else {
            dst.u = 0.0f;
            dst.v = 0.0f;
        }
    }

    if (mvp_dirty_) {
        float wv[16];
        MultiplyMatrix4x4(world_matrix_, view_matrix_, wv);
        MultiplyMatrix4x4(wv, proj_matrix_, mvp_matrix_);
        mvp_dirty_ = false;
    }

    glUseProgram(shader_program_);
    glUniformMatrix4fv(u_mvp_loc_, 1, GL_FALSE, mvp_matrix_);
    glUniform1i(u_is_rhw_loc_, is_rhw ? 1 : 0);
    glUniform2f(u_screen_size_loc_, static_cast<float>(surface_width_), static_cast<float>(surface_height_));

    GLuint bound_tex = white_tex_;
    if (active_tex0_va_ != 0) {
        auto tit = textures_.find(active_tex0_va_);
        if (tit != textures_.end() && tit->second.gl_tex != 0) {
            bound_tex = tit->second.gl_tex;
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bound_tex);
    glUniform1i(u_tex_loc_, 0);

    if (alpha_blend_enabled_) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    if (depth_test_enabled_ && !is_rhw) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glBindBuffer(GL_ARRAY_BUFFER, dynamic_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(packed.size() * sizeof(PackedGpuVertex)),
        packed.data(),
        GL_STREAM_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(16));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(20));

    glDrawArrays(gl_mode, 0, static_cast<GLsizei>(vertex_count));
#else
    (void)fvf;
    (void)index_bytes;
    (void)index32;
    (void)prim_type;
    (void)start_elem;
#endif
}

void D3D9SurfaceRenderer::DrawPrimitive(
    const MemoryArena& arena,
    uint32_t prim_type,
    uint32_t start_vertex,
    uint32_t prim_count
) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto vbit = vertex_buffers_.find(active_vb_va_);
    if (vbit == vertex_buffers_.end()) return;
    const uint8_t* vb_ptr = arena.GetHostPointer(vbit->second.data_va + active_vb_offset_);
    uint32_t fvf = (vbit->second.fvf != 0) ? vbit->second.fvf : active_fvf_;
    RenderVerticesInternalLocked(vb_ptr, active_vb_stride_, fvf, nullptr, false, prim_type, start_vertex, prim_count);
}

void D3D9SurfaceRenderer::DrawIndexedPrimitive(
    const MemoryArena& arena,
    uint32_t prim_type,
    int32_t base_vertex_idx,
    uint32_t /*min_vertex_idx*/,
    uint32_t /*num_vertices*/,
    uint32_t start_index,
    uint32_t prim_count
) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto vbit = vertex_buffers_.find(active_vb_va_);
    auto ibit = index_buffers_.find(active_ib_va_);
    if (vbit == vertex_buffers_.end() || ibit == index_buffers_.end()) {
        current_draw_calls_++;
        current_triangles_ += prim_count;
        return;
    }

    int64_t vb_byte_off = static_cast<int64_t>(active_vb_offset_) +
                          static_cast<int64_t>(base_vertex_idx) * static_cast<int64_t>(active_vb_stride_);
    const uint8_t* vb_ptr = arena.GetHostPointer(vbit->second.data_va + static_cast<uint32_t>(std::max<int64_t>(0, vb_byte_off)));
    const uint8_t* ib_ptr = arena.GetHostPointer(ibit->second.data_va);
    bool index32 = (ibit->second.format == 102);
    uint32_t fvf = (vbit->second.fvf != 0) ? vbit->second.fvf : active_fvf_;

    RenderVerticesInternalLocked(vb_ptr, active_vb_stride_, fvf, ib_ptr, index32, prim_type, start_index, prim_count);
}

void D3D9SurfaceRenderer::DrawPrimitiveUP(
    const MemoryArena& arena,
    uint32_t prim_type,
    uint32_t prim_count,
    uint32_t vertex_stream_zero_va,
    uint32_t vertex_stride
) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint8_t* vb_ptr = arena.GetHostPointer(vertex_stream_zero_va);
    uint32_t stride = (vertex_stride > 0) ? vertex_stride : active_vb_stride_;
    RenderVerticesInternalLocked(vb_ptr, stride, active_fvf_, nullptr, false, prim_type, 0, prim_count);
}

void D3D9SurfaceRenderer::SubmitD3DXFontText(int32_t x, int32_t y, uint32_t argb_color, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (text.empty()) return;
    frame_texts_.push_back(OverlayTextCommand{x, y, argb_color, text});
}

void D3D9SurfaceRenderer::SetHudStatusLines(const std::vector<std::string>& lines) {
    std::lock_guard<std::mutex> lock(mutex_);
    hud_status_lines_ = lines;
}

void D3D9SurfaceRenderer::RenderFilledRectLocked(float x, float y, float w, float h, uint32_t argb_color) {
#ifdef __ANDROID__
    if (shader_program_ == 0 || white_tex_ == 0) return;
    uint8_t b = static_cast<uint8_t>(argb_color & 0xFFu);
    uint8_t g = static_cast<uint8_t>((argb_color >> 8) & 0xFFu);
    uint8_t r = static_cast<uint8_t>((argb_color >> 16) & 0xFFu);
    uint8_t a = static_cast<uint8_t>((argb_color >> 24) & 0xFFu);

    PackedGpuVertex verts[6] = {
        {x, y, 0.0f, 1.0f, r, g, b, a, 0.0f, 0.0f},
        {x + w, y, 0.0f, 1.0f, r, g, b, a, 1.0f, 0.0f},
        {x, y + h, 0.0f, 1.0f, r, g, b, a, 0.0f, 1.0f},
        {x + w, y, 0.0f, 1.0f, r, g, b, a, 1.0f, 0.0f},
        {x + w, y + h, 0.0f, 1.0f, r, g, b, a, 1.0f, 1.0f},
        {x, y + h, 0.0f, 1.0f, r, g, b, a, 0.0f, 1.0f}
    };

    glUseProgram(shader_program_);
    glUniform1i(u_is_rhw_loc_, 1);
    glUniform2f(u_screen_size_loc_, static_cast<float>(surface_width_), static_cast<float>(surface_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, white_tex_);
    glUniform1i(u_tex_loc_, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, dynamic_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(16));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(20));
    glDrawArrays(GL_TRIANGLES, 0, 6);
#else
    (void)x; (void)y; (void)w; (void)h; (void)argb_color;
#endif
}

void D3D9SurfaceRenderer::RenderOverlayTextLocked(int32_t x, int32_t y, uint32_t argb_color, const std::string& text) {
#ifdef __ANDROID__
    if (shader_program_ == 0 || font_tex_ == 0 || text.empty()) return;

    uint8_t b = static_cast<uint8_t>(argb_color & 0xFFu);
    uint8_t g = static_cast<uint8_t>((argb_color >> 8) & 0xFFu);
    uint8_t r = static_cast<uint8_t>((argb_color >> 16) & 0xFFu);
    uint8_t a = static_cast<uint8_t>((argb_color >> 24) & 0xFFu);
    if (a == 0) a = 255;

    std::vector<PackedGpuVertex> verts;
    verts.reserve(text.size() * 6);

    float pen_x = static_cast<float>(x);
    float pen_y = static_cast<float>(y);
    const float glyph_w = 10.0f;
    const float glyph_h = 14.0f;

    for (char c : text) {
        if (c == '\n') {
            pen_x = static_cast<float>(x);
            pen_y += glyph_h + 4.0f;
            continue;
        }
        if (c < 32 || c > 126) {
            pen_x += glyph_w;
            continue;
        }
        int idx = static_cast<int>(c) - 32;
        float u0 = static_cast<float>((idx % 16) * 8) / 128.0f;
        float v0 = static_cast<float>((idx / 16) * 8) / 64.0f;
        float u1 = u0 + (8.0f / 128.0f);
        float v1 = v0 + (8.0f / 64.0f);

        PackedGpuVertex v_tl{pen_x, pen_y, 0.0f, 1.0f, r, g, b, a, u0, v0};
        PackedGpuVertex v_tr{pen_x + glyph_w, pen_y, 0.0f, 1.0f, r, g, b, a, u1, v0};
        PackedGpuVertex v_bl{pen_x, pen_y + glyph_h, 0.0f, 1.0f, r, g, b, a, u0, v1};
        PackedGpuVertex v_br{pen_x + glyph_w, pen_y + glyph_h, 0.0f, 1.0f, r, g, b, a, u1, v1};

        verts.push_back(v_tl);
        verts.push_back(v_tr);
        verts.push_back(v_bl);
        verts.push_back(v_tr);
        verts.push_back(v_br);
        verts.push_back(v_bl);

        pen_x += glyph_w;
    }

    if (verts.empty()) return;

    glUseProgram(shader_program_);
    glUniform1i(u_is_rhw_loc_, 1);
    glUniform2f(u_screen_size_loc_, static_cast<float>(surface_width_), static_cast<float>(surface_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_tex_);
    glUniform1i(u_tex_loc_, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, dynamic_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(verts.size() * sizeof(PackedGpuVertex)),
        verts.data(),
        GL_STREAM_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(16));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PackedGpuVertex), reinterpret_cast<const void*>(20));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
#else
    (void)x; (void)y; (void)argb_color; (void)text;
#endif
}

void D3D9SurfaceRenderer::OnD3D9EndScene() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_frame_draw_calls_ = current_draw_calls_;
    last_frame_triangles_ = current_triangles_;
}

void D3D9SurfaceRenderer::OnD3D9Present() {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_counter_++;
#ifdef __ANDROID__
    if (EnsureEglCurrentLocked()) {
        glViewport(0, 0, surface_width_, surface_height_);
        if (current_draw_calls_ == 0) {
            float r = static_cast<float>((last_clear_color_ >> 16) & 0xFFu) / 255.0f;
            float g = static_cast<float>((last_clear_color_ >> 8) & 0xFFu) / 255.0f;
            float b = static_cast<float>(last_clear_color_ & 0xFFu) / 255.0f;
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Render subtle horizon & ground grid for the active Direct3D9 viewport while game world streams
            float horizon_y = static_cast<float>(surface_height_) * 0.46f;
            RenderFilledRectLocked(0.0f, 0.0f, static_cast<float>(surface_width_), horizon_y, 0xFF12161Du);
            RenderFilledRectLocked(0.0f, horizon_y, static_cast<float>(surface_width_), static_cast<float>(surface_height_) - horizon_y, 0xFF181D26u);
            RenderFilledRectLocked(0.0f, horizon_y - 1.0f, static_cast<float>(surface_width_), 2.0f, 0xFF2C3545u);
        }

        for (const auto& cmd : frame_texts_) {
            RenderOverlayTextLocked(cmd.x, cmd.y, cmd.argb_color, cmd.text);
        }

        if (!hud_status_lines_.empty()) {
            int32_t box_h = static_cast<int32_t>(hud_status_lines_.size()) * 20 + 16;
            RenderFilledRectLocked(20.0f, 68.0f, 760.0f, static_cast<float>(box_h), 0xB80D0F12u);
            int32_t y_cursor = 76;
            for (size_t i = 0; i < hud_status_lines_.size(); ++i) {
                uint32_t col = (i == 0) ? 0xFFE5E8ECu : 0xFFA9B4C2u;
                RenderOverlayTextLocked(32, y_cursor, col, hud_status_lines_[i]);
                y_cursor += 20;
            }
        }

        eglSwapBuffers(egl_display_, egl_surface_);
    }
#endif
}

D3D9FrameTelemetry D3D9SurfaceRenderer::GetTelemetry() const {
    std::lock_guard<std::mutex> lock(mutex_);
    D3D9FrameTelemetry t;
    t.frame_index = frame_counter_;
    t.draw_calls = last_frame_draw_calls_;
    t.triangles_submitted = last_frame_triangles_;
    t.d3d9_device_va = kGtaSaD3D9DevicePtrVa;
    return t;
}

}  // namespace ventura
