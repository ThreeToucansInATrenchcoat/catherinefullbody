#include "test_support.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>

using catherine::dx12::Crop;
using catherine::dx12::Frame;
using catherine::dx12::Renderer;
using catherine::dx12::RendererOptions;
using Microsoft::WRL::ComPtr;

namespace {

using catherine::dx12::test::buffer;
using catherine::dx12::test::check;
using catherine::dx12::test::expect;
using catherine::dx12::test::record_readback;
using catherine::dx12::test::verify_debug_messages;

struct Scene {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12Resource> indices;
};

ComPtr<ID3DBlob> compile(const char* source, const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target,
                                  D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
    if (errors) std::fputs(static_cast<const char*>(errors->GetBufferPointer()), stderr);
    check(hr, entry);
    return code;
}

Scene create_scene(ID3D12Device* device) {
    constexpr char hlsl[] = R"(
        cbuffer Draw : register(b0) { float depth; float red; float green; }
        float4 vs(uint vertex : SV_VertexID) : SV_Position {
            float2 points[3] = { float2(-0.6, -0.6), float2(0, 0.6), float2(0.6, -0.6) };
            return float4(points[vertex], depth, 1);
        }
        float4 ps() : SV_Target { return float4(red, green, 0, 1); }
    )";
    const auto vs = compile(hlsl, "vs", "vs_5_0");
    const auto ps = compile(hlsl, "ps", "ps_5_0");

    D3D12_ROOT_PARAMETER params{};
    params.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params.Constants = {0, 0, 3};
    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &params;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr),
          "D3D12SerializeRootSignature");

    Scene scene;
    check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&scene.root)), "CreateRootSignature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = scene.root.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = Renderer::color_format;
    pso.DSVFormat = Renderer::depth_format;
    pso.SampleDesc.Count = 1;
    check(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&scene.pipeline)), "CreateGraphicsPipelineState");

    const uint16_t indices[] = {0, 1, 2};
    scene.indices = buffer(device, sizeof(indices), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const D3D12_RANGE no_reads{0, 0};
    void* mapped = nullptr;
    check(scene.indices->Map(0, &no_reads, &mapped), "map indices");
    std::memcpy(mapped, indices, sizeof(indices));
    scene.indices->Unmap(0, nullptr);
    return scene;
}

void draw(const Frame& frame, const Scene& scene) {
    const float blue[] = {0, 0, 1, 1};
    auto* commands = frame.commands;
    commands->ClearRenderTargetView(frame.rtv, blue, 0, nullptr);
    commands->ClearDepthStencilView(frame.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commands->SetGraphicsRootSignature(scene.root.Get());
    commands->SetPipelineState(scene.pipeline.Get());
    const D3D12_INDEX_BUFFER_VIEW indices{scene.indices->GetGPUVirtualAddress(), 3 * sizeof(uint16_t),
                                          DXGI_FORMAT_R16_UINT};
    commands->IASetIndexBuffer(&indices);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float near_green[] = {0.2f, 0, 1};
    commands->SetGraphicsRoot32BitConstants(0, 3, near_green, 0);
    commands->DrawIndexedInstanced(3, 1, 0, 0, 0);
    const float far_red[] = {0.8f, 1, 0};
    commands->SetGraphicsRoot32BitConstants(0, 3, far_red, 0);
    commands->DrawIndexedInstanced(3, 1, 0, 0, 0);
}

void verify_pixels(ID3D12Resource* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout, UINT64 bytes,
                   bool cropped = false) {
    const D3D12_RANGE reads{0, static_cast<SIZE_T>(bytes)};
    void* mapped = nullptr;
    check(readback->Map(0, &reads, &mapped), "map color readback");
    const auto pixel = [&](UINT x, UINT y) {
        std::array<uint8_t, 4> rgba{};
        const auto offset = layout.Offset + UINT64(y) * layout.Footprint.RowPitch + x * rgba.size();
        std::memcpy(rgba.data(), static_cast<const uint8_t*>(mapped) + offset, rgba.size());
        return rgba;
    };
    const auto center = pixel(layout.Footprint.Width / 2, layout.Footprint.Height / 2);
    const auto corner = pixel(0, 0);
    const D3D12_RANGE no_writes{0, 0};
    readback->Unmap(0, &no_writes);
    expect(center == std::array<uint8_t, 4>{0, 255, 0, 255}, "depth-tested green triangle pixel");
    const std::array<uint8_t, 4> expected_corner = cropped ? std::array<uint8_t, 4>{0, 255, 0, 255}
                                                           : std::array<uint8_t, 4>{0, 0, 255, 255};
    expect(corner == expected_corner, "corner pixel should reflect the selected crop");
}

struct TestWindow {
    HWND hwnd = nullptr;
    ~TestWindow() { if (hwnd) DestroyWindow(hwnd); }
};

void run(bool warp, bool windowed, bool vsync) {
    TestWindow window;
    RendererOptions options;
    options.debug = true;
    options.warp = warp;
    options.vsync = vsync;
    options.width = 79;
    options.height = 55;
    options.offscreen_clear_color[2] = 1.f;
    if (windowed) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"CatherineDirect3D12Smoke";
        expect(RegisterClassW(&wc) != 0, "register smoke window");
        window.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                       L"Catherine Direct3D 12 smoke test", WS_POPUP,
                                       0, 0, 79, 55, nullptr, nullptr, instance, nullptr);
        expect(window.hwnd != nullptr, "create hidden smoke window");
        options.window = window.hwnd;
    }

    Scene scene;
    ComPtr<ID3D12Resource> readback, sampled;
    Renderer renderer;
    Frame frame;
    expect(renderer.begin_frame(frame) == DXGI_ERROR_INVALID_CALL, "reject recording before initialization");
    if (!windowed) {
        auto invalid = options;
        invalid.width = 0;
        expect(renderer.initialize(invalid) == E_INVALIDARG, "reject zero-size offscreen target");
    }
    check(renderer.initialize(options), "initialize renderer");
    expect(renderer.initialize(options) == DXGI_ERROR_INVALID_CALL, "reject duplicate initialization");
    auto* device = renderer.device();
    ComPtr<ID3D12InfoQueue> info;
    check(device->QueryInterface(IID_PPV_ARGS(&info)), "query debug layer info queue");
    scene = create_scene(device);

    check(renderer.begin_frame(frame), "begin canceled frame");
    const float unused[] = {0, 0, 1, 1};
    frame.commands->ClearRenderTargetView(frame.rtv, unused, 0, nullptr);
    Frame rejected;
    expect(renderer.begin_frame(rejected) == DXGI_ERROR_INVALID_CALL && !rejected.commands,
           "reject nested recording");
    expect(renderer.wait_idle() == DXGI_ERROR_INVALID_CALL, "reject idle wait while recording");
    check(renderer.cancel_frame(), "cancel frame");
    expect(renderer.end_frame() == DXGI_ERROR_INVALID_CALL, "reject submit without active frame");

    check(renderer.begin_frame(frame), "begin initial frame");
    expect(frame.width == options.width && frame.height == options.height, "initial dimensions");
    auto desc = frame.color->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT64 bytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
    readback = buffer(device, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_HEAP_PROPERTIES texture_heap{};
    texture_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    texture_heap.CreationNodeMask = texture_heap.VisibleNodeMask = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    check(device->CreateCommittedResource(&texture_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                           IID_PPV_ARGS(&sampled)), "create sampled color");
    draw(frame, scene);
    record_readback(frame, readback.Get(), layout, sampled.Get());
    check(renderer.end_frame(), "end initial frame");
    check(renderer.wait_idle(), "wait for readback");
    verify_pixels(readback.Get(), layout, bytes);

    check(renderer.begin_frame(frame), "begin cropped frame");
    const Crop bad_crop{78, 0, 2, 1};
    expect(renderer.present_texture(sampled.Get(), D3D12_RESOURCE_STATE_COPY_DEST, &bad_crop) == E_INVALIDARG,
           "reject out-of-range crop");
    expect(renderer.present_texture(frame.color, D3D12_RESOURCE_STATE_RENDER_TARGET) == E_INVALIDARG,
           "reject sampling the active render target");
    const Crop crop{36, 31, 7, 7};
    check(renderer.present_texture(sampled.Get(), D3D12_RESOURCE_STATE_COPY_DEST, &crop), "cropped blit");
    expect(renderer.present_texture(sampled.Get(), D3D12_RESOURCE_STATE_COPY_DEST) == DXGI_ERROR_INVALID_CALL,
           "protect in-flight descriptor from repeated writes");
    record_readback(frame, readback.Get(), layout);
    check(renderer.end_frame(), "end cropped frame");
    check(renderer.wait_idle(), "wait for cropped readback");
    verify_pixels(readback.Get(), layout, bytes, true);

    for (unsigned i = 0; i < 6; ++i) {
        if (i == 2 && windowed) {
            expect(SetWindowPos(window.hwnd, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                   "collapse hidden window");
            expect(renderer.begin_frame(frame) == S_FALSE && !frame.commands, "skip a zero-sized window");
            expect(SetWindowPos(window.hwnd, nullptr, 0, 0, 117, 81, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                   "resize hidden window");
        }
        check(renderer.begin_frame(frame), "begin rotating frame");
        const bool resized = windowed && i >= 2;
        expect(frame.width == (resized ? 117u : 79u) && frame.height == (resized ? 81u : 55u),
               "color extent follows resize");
        const auto depth = frame.depth->GetDesc();
        expect(depth.Width == frame.width && depth.Height == frame.height, "matching depth extent");
        // Alternate slots and reuse the source's restored state across GPU submissions.
        check(renderer.present_texture(sampled.Get(), D3D12_RESOURCE_STATE_COPY_DEST), "full texture blit");
        check(renderer.end_frame(), "end rotating frame");
    }
    check(renderer.wait_idle(), "wait for final frame");

    check(renderer.begin_frame(frame), "begin full blit readback");
    const auto final_desc = frame.color->GetDesc();
    device->GetCopyableFootprints(&final_desc, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
    readback = buffer(device, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    check(renderer.present_texture(sampled.Get(), D3D12_RESOURCE_STATE_COPY_DEST), "record full blit readback");
    record_readback(frame, readback.Get(), layout);
    check(renderer.end_frame(), "end full blit readback");
    check(renderer.wait_idle(), "wait for full blit readback");
    verify_pixels(readback.Get(), layout, bytes);
    verify_debug_messages(info.Get());

    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "create adapter inspection factory");
    ComPtr<IDXGIAdapter1> adapter;
    check(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)), "identify rendering adapter");
    DXGI_ADAPTER_DESC1 adapter_desc{};
    check(adapter->GetDesc1(&adapter_desc), "read rendering adapter");
    std::wprintf(L"%ls | %ls | indexed draw, depth and cropped blit verified by RGBA readback\n",
                 adapter_desc.Description, windowed ? L"DXGI presentation and resize" : L"offscreen");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    try {
        run(mode == "--warp", mode == "--window" || mode == "--window-immediate", mode != "--window-immediate");
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Smoke test failed: %s\n", ex.what());
        return 1;
    }
}
