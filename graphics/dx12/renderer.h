#pragma once

#include <windows.h>
#include <d3d12.h>

#include <memory>

namespace catherine::dx12 {

struct RendererOptions {
    // Null selects an offscreen output with the dimensions below.
    HWND window = nullptr;
    UINT width = 1280;
    UINT height = 720;
    bool vsync = true;
    bool warp = false;
    bool debug = false;
    // Optimized clear value for the offscreen color buffers. Clear commands
    // still come from the caller.
    float offscreen_clear_color[4] = {0, 0, 0, 1};
};

struct Frame {
    ID3D12GraphicsCommandList* commands = nullptr;
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    UINT width = 0;
    UINT height = 0;
    UINT slot = 0;
};

struct Crop {
    UINT x;
    UINT y;
    UINT width;
    UINT height;
};

class Renderer {
public:
    static constexpr UINT frame_slots = 2;
    static constexpr DXGI_FORMAT color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT;

    Renderer() noexcept;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Serialize all calls on the rendering thread. Enable the optional debug layer
    // before creating any D3D12 device in the process.
    HRESULT initialize(const RendererOptions& options) noexcept;
    ID3D12Device* device() const noexcept;

    // S_FALSE means the window is minimized or occluded. On S_OK the color target,
    // depth target, viewport, and scissor are already bound. Frame fields are
    // borrowed until end_frame or cancel_frame. Restore color to RENDER_TARGET and
    // depth to DEPTH_WRITE before end_frame. Keep the window alive and release
    // any external references to frame targets before the next begin_frame.
    HRESULT begin_frame(Frame& frame) noexcept;
    // Draws mip zero of a native single-sampled RGBA8/BGRA8 UNORM or typeless texture into
    // the full frame, optionally cropping in source pixels. Records at most
    // once per frame. The source is restored to source_state, the color target
    // must be in RENDER_TARGET, and the list's graphics bindings are replaced.
    HRESULT present_texture(ID3D12Resource* source, D3D12_RESOURCE_STATES source_state,
                            const Crop* crop = nullptr) noexcept;
    // Submits the list. In windowed mode this also presents the color target.
    // DXGI_STATUS_OCCLUDED means subsequent begin_frame calls will test visibility.
    HRESULT end_frame(bool* executed = nullptr) noexcept;
    HRESULT cancel_frame() noexcept;

    // Keep resources, descriptors and pipeline objects used by submitted commands
    // alive and unchanged until completion. An open list must be ended or canceled
    // before wait_idle, which waits for all work submitted by this renderer.
    HRESULT wait_idle() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace catherine::dx12
