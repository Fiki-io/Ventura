#pragma once

#include "ventura_types.h"
#include "../core/memory_arena.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#else
struct ANativeWindow;
using GLuint = uint32_t;
#endif

namespace ventura {

struct D3D9FrameTelemetry {
    uint64_t frame_index = 0;
    uint32_t draw_calls = 0;
    uint32_t triangles_submitted = 0;
    uint32_t d3d9_device_va = kGtaSaD3D9DevicePtrVa;
};

struct D3D9VertexBufferResource {
    uint32_t handle_va = 0;
    uint32_t data_va = 0;
    uint32_t size_bytes = 0;
    uint32_t fvf = 0;
    GLuint gl_vbo = 0;
    bool dirty = true;
};

struct D3D9IndexBufferResource {
    uint32_t handle_va = 0;
    uint32_t data_va = 0;
    uint32_t size_bytes = 0;
    uint32_t format = 101;
    GLuint gl_ibo = 0;
    bool dirty = true;
};

struct D3D9TextureResource {
    uint32_t handle_va = 0;
    uint32_t data_va = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint32_t pitch = 0;
    uint32_t byte_size = 0;
    GLuint gl_tex = 0;
    bool dirty = true;
};

struct OverlayTextCommand {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t argb_color = 0xFFFFFFFFu;
    std::string text;
};

class D3D9SurfaceRenderer {
public:
    D3D9SurfaceRenderer();
    ~D3D9SurfaceRenderer();

    // Called from Android UI thread (SurfaceHolder.Callback) - never touches EGL directly
    bool AttachWindow(ANativeWindow* window);
    void DetachWindow();

    // Called from ExecutionWorkerLoop thread before thread exit
    void ShutdownOnWorkerThread();

    void OnD3D9Clear(uint32_t argb_color);
    void OnD3D9BeginScene();
    void OnD3D9EndScene();
    void OnD3D9Present();

    // Real D3D9 Resource Management & Pipeline State
    uint32_t CreateVertexBuffer(MemoryArena& arena, uint32_t length_bytes, uint32_t fvf, uint32_t vtbl_va);
    uint32_t LockVertexBuffer(uint32_t vb_obj_va);
    void UnlockVertexBuffer(uint32_t vb_obj_va);

    uint32_t CreateIndexBuffer(MemoryArena& arena, uint32_t length_bytes, uint32_t format, uint32_t vtbl_va);
    uint32_t LockIndexBuffer(uint32_t ib_obj_va);
    void UnlockIndexBuffer(uint32_t ib_obj_va);

    uint32_t CreateTexture(
        MemoryArena& arena,
        uint32_t width,
        uint32_t height,
        uint32_t format,
        uint32_t vtbl_va
    );
    bool LockTextureRect(uint32_t tex_obj_va, uint32_t* out_pitch, uint32_t* out_bits_va);
    void UnlockTextureRect(const MemoryArena& arena, uint32_t tex_obj_va);

    void SetTransform(uint32_t state_type, const float* matrix16);
    void SetRenderState(uint32_t state_type, uint32_t value);
    void SetFVF(uint32_t fvf);
    void SetStreamSource(uint32_t stream_num, uint32_t vb_obj_va, uint32_t offset_bytes, uint32_t stride);
    void SetIndices(uint32_t ib_obj_va);
    void SetTexture(uint32_t stage, uint32_t tex_obj_va);

    void DrawPrimitive(const MemoryArena& arena, uint32_t prim_type, uint32_t start_vertex, uint32_t prim_count);
    void DrawIndexedPrimitive(
        const MemoryArena& arena,
        uint32_t prim_type,
        int32_t base_vertex_idx,
        uint32_t min_vertex_idx,
        uint32_t num_vertices,
        uint32_t start_index,
        uint32_t prim_count
    );
    void DrawPrimitiveUP(
        const MemoryArena& arena,
        uint32_t prim_type,
        uint32_t prim_count,
        uint32_t vertex_stream_zero_va,
        uint32_t vertex_stride
    );

    void SubmitD3DXFontText(int32_t x, int32_t y, uint32_t argb_color, const std::string& text);
    void SetHudStatusLines(const std::vector<std::string>& lines);

    D3D9FrameTelemetry GetTelemetry() const;

    static void DecompressS3tcToRgba8(
        uint32_t width,
        uint32_t height,
        uint32_t d3d_format,
        const uint8_t* src_bytes,
        uint32_t src_size,
        std::vector<uint8_t>& out_rgba
    );

private:
    bool EnsureEglCurrentLocked();
    void DestroyEglLocked();
    bool CompileShadersAndFontLocked();
    void RenderVerticesInternalLocked(
        const uint8_t* vertex_bytes,
        uint32_t stride,
        uint32_t fvf,
        const uint8_t* index_bytes,
        bool index32,
        uint32_t prim_type,
        uint32_t start_elem,
        uint32_t prim_count
    );
    void RenderOverlayTextLocked(int32_t x, int32_t y, uint32_t argb_color, const std::string& text);
    void RenderFilledRectLocked(float x, float y, float w, float h, uint32_t argb_color);

    ANativeWindow* pending_window_ = nullptr;
    ANativeWindow* active_window_ = nullptr;
    bool window_changed_ = false;

#ifdef __ANDROID__
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
#endif
    int32_t surface_width_ = 1280;
    int32_t surface_height_ = 720;
    GLuint shader_program_ = 0;
    GLuint font_tex_ = 0;
    GLuint white_tex_ = 0;
    GLuint dynamic_vbo_ = 0;
    GLuint dynamic_ibo_ = 0;

    int32_t u_mvp_loc_ = -1;
    int32_t u_is_rhw_loc_ = -1;
    int32_t u_screen_size_loc_ = -1;
    int32_t u_tex_loc_ = -1;

    float world_matrix_[16];
    float view_matrix_[16];
    float proj_matrix_[16];
    float mvp_matrix_[16];
    bool mvp_dirty_ = true;

    uint32_t active_fvf_ = 0x144;
    uint32_t active_vb_va_ = 0;
    uint32_t active_vb_offset_ = 0;
    uint32_t active_vb_stride_ = 28;
    uint32_t active_ib_va_ = 0;
    uint32_t active_tex0_va_ = 0;
    bool alpha_blend_enabled_ = true;
    bool depth_test_enabled_ = true;

    std::unordered_map<uint32_t, D3D9VertexBufferResource> vertex_buffers_;
    std::unordered_map<uint32_t, D3D9IndexBufferResource> index_buffers_;
    std::unordered_map<uint32_t, D3D9TextureResource> textures_;
    std::vector<OverlayTextCommand> frame_texts_;
    std::vector<std::string> hud_status_lines_;
    uint32_t next_com_resource_va_ = 0x60100000u;

    uint32_t last_clear_color_ = 0xFF0D0F12u;
    uint64_t frame_counter_ = 0;
    uint32_t current_draw_calls_ = 0;
    uint32_t current_triangles_ = 0;
    uint32_t last_frame_draw_calls_ = 0;
    uint32_t last_frame_triangles_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace ventura
