#include "renderer.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <limits>
#include <new>
#include <utility>

namespace catherine::dx12 {

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT buffer_count = Renderer::frame_slots;
constexpr UINT max_extent = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;

D3D12_HEAP_PROPERTIES default_heap() {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    return heap;
}

D3D12_RESOURCE_DESC texture(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    return desc;
}

void transition_color(ID3D12GraphicsCommandList* commands, ID3D12Resource* color,
                      D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                      UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {color, subresource, before, after};
    commands->ResourceBarrier(1, &barrier);
}

HRESULT window_extent(HWND window, UINT& width, UINT& height) {
    if (!IsWindow(window)) return E_HANDLE;
    if (IsIconic(window)) return S_FALSE;
    RECT rect{};
    if (!GetClientRect(window, &rect)) return HRESULT_FROM_WIN32(GetLastError());
    width = static_cast<UINT>(rect.right - rect.left);
    height = static_cast<UINT>(rect.bottom - rect.top);
    return width && height ? S_OK : S_FALSE;
}

HRESULT create_device(IDXGIFactory6* factory, bool warp, ID3D12Device** device) {
    if (warp) {
        ComPtr<IDXGIAdapter> adapter;
        const HRESULT hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if (FAILED(hr)) return hr;
        return D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device));
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                         IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) return DXGI_ERROR_UNSUPPORTED;
        if (FAILED(hr)) return hr;
        DXGI_ADAPTER_DESC1 desc{};
        hr = adapter->GetDesc1(&desc);
        if (FAILED(hr)) return hr;
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device));
        if (SUCCEEDED(hr)) return hr;
        if (hr != DXGI_ERROR_UNSUPPORTED && hr != E_INVALIDARG) return hr;
    }
}

}  // namespace

struct Renderer::Impl {
    struct Slot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> color;
        ComPtr<ID3D12Resource> depth;
        UINT64 fence_value = 0;
    };

    RendererOptions options;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12DescriptorHeap> rtvs;
    ComPtr<ID3D12DescriptorHeap> dsvs;
    ComPtr<ID3D12DescriptorHeap> present_srvs;
    ComPtr<ID3D12PipelineState> present_pipeline;
    ComPtr<ID3D12RootSignature> present_root;
    std::array<Slot, buffer_count> slots;
    ComPtr<ID3D12GraphicsCommandList> commands;
    HANDLE fence_event = nullptr;
    UINT64 last_signal = 0;
    UINT width = 0;
    UINT height = 0;
    UINT active = 0;
    UINT offscreen_index = 0;
    UINT swap_flags = 0;
    HRESULT submission_error = S_OK;
    bool recording = false;
    bool occluded = false;
    bool present_recorded = false;

    ~Impl() {
        if (recording) commands->Close();
        if (last_signal) wait(last_signal);
        if (fence_event) CloseHandle(fence_event);
    }

    HRESULT wait(UINT64 value) {
        if (!value) return S_OK;
        UINT64 completed = fence->GetCompletedValue();
        if (completed == (std::numeric_limits<UINT64>::max)()) return device->GetDeviceRemovedReason();
        if (completed >= value) return S_OK;
        HRESULT hr = fence->SetEventOnCompletion(value, fence_event);
        if (FAILED(hr)) return hr;
        const DWORD status = WaitForSingleObject(fence_event, INFINITE);
        if (status != WAIT_OBJECT_0) {
            return status == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : E_FAIL;
        }
        return device->GetDeviceRemovedReason();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE descriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT index) const {
        ID3D12DescriptorHeap* heap = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? rtvs.Get() : dsvs.Get();
        auto handle = heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * device->GetDescriptorHandleIncrementSize(type);
        return handle;
    }

    HRESULT make_targets(UINT new_width, UINT new_height);
    HRESULT make_present_pipeline();
    HRESULT initialize(const RendererOptions& config);
};

HRESULT Renderer::Impl::initialize(const RendererOptions& config) {
    options = config;
    UINT factory_flags = 0;
    if (options.debug) {
        ComPtr<ID3D12Debug> debug;
        const HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (FAILED(hr)) return hr;
        debug->EnableDebugLayer();
        factory_flags = DXGI_CREATE_FACTORY_DEBUG;
    }

    HRESULT hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    hr = create_device(factory.Get(), options.warp, &device);
    if (FAILED(hr)) return hr;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
    if (FAILED(hr)) return hr;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) return hr;
    fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event) return HRESULT_FROM_WIN32(GetLastError());

    for (auto& slot : slots) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator));
        if (FAILED(hr)) return hr;
    }
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slots[0].allocator.Get(), nullptr,
                                   IID_PPV_ARGS(&commands));
    if (FAILED(hr)) return hr;
    hr = commands->Close();
    if (FAILED(hr)) return hr;

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = buffer_count;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvs));
    if (FAILED(hr)) return hr;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsvs));
    if (FAILED(hr)) return hr;

    if (options.window) {
        BOOL tearing = FALSE;
        if (!options.vsync && SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                                      &tearing, sizeof(tearing))) && tearing) {
            swap_flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        // A minimized window has no drawable size. Defer swap chain creation to begin_frame.
        return IsWindow(options.window) ? S_OK : E_HANDLE;
    }
    return make_targets(options.width, options.height);
}

HRESULT Renderer::Impl::make_targets(UINT new_width, UINT new_height) {
    if (!new_width || !new_height || new_width > max_extent || new_height > max_extent) return E_INVALIDARG;

    HRESULT hr = wait(last_signal);
    if (FAILED(hr)) return hr;
    for (auto& slot : slots) {
        slot.color.Reset();
        slot.depth.Reset();
    }
    width = height = 0;

    if (options.window) {
        if (swapchain) {
            hr = swapchain->ResizeBuffers(buffer_count, new_width, new_height, color_format, swap_flags);
            if (FAILED(hr)) return hr;
        } else {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = new_width;
            desc.Height = new_height;
            desc.Format = color_format;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = buffer_count;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Flags = swap_flags;
            ComPtr<IDXGISwapChain1> created;
            hr = factory->CreateSwapChainForHwnd(queue.Get(), options.window, &desc, nullptr, nullptr, &created);
            if (FAILED(hr)) return hr;
            hr = factory->MakeWindowAssociation(options.window, DXGI_MWA_NO_ALT_ENTER);
            if (FAILED(hr)) return hr;
            hr = created.As(&swapchain);
            if (FAILED(hr)) return hr;
        }
    }

    const auto heap = default_heap();
    const auto color = texture(new_width, new_height, color_format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    const auto depth = texture(new_width, new_height, depth_format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear_color{};
    clear_color.Format = color_format;
    for (UINT channel = 0; channel < 4; ++channel) {
        clear_color.Color[channel] = options.offscreen_clear_color[channel];
    }
    D3D12_CLEAR_VALUE far_depth{};
    far_depth.Format = depth_format;
    far_depth.DepthStencil.Depth = 1.f;

    for (UINT i = 0; i < buffer_count; ++i) {
        auto& slot = slots[i];
        if (swapchain) {
            hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&slot.color));
        } else {
            hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &color,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_color,
                                                  IID_PPV_ARGS(&slot.color));
        }
        if (FAILED(hr)) return hr;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                              &far_depth, IID_PPV_ARGS(&slot.depth));
        if (FAILED(hr)) return hr;
        device->CreateRenderTargetView(slot.color.Get(), nullptr, descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i));
        device->CreateDepthStencilView(slot.depth.Get(), nullptr, descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, i));
    }
    width = new_width;
    height = new_height;
    return S_OK;
}

HRESULT Renderer::Impl::make_present_pipeline() {
    // A single fullscreen triangle with top-left UV origin. Clamp sample
    // positions to texel centers so linear filtering stays inside the crop.
    constexpr char shader[] = R"(
        cbuffer Crop : register(b0) { float4 region; float2 texel; };
        Texture2D<float4> source : register(t0);
        SamplerState linearClamp : register(s0);
        struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD; };
        Vertex vs(uint id : SV_VertexID) {
            Vertex v;
            v.uv = float2((id << 1) & 2, id & 2);
            v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
            return v;
        }
        float4 ps(Vertex v) : SV_Target {
            float2 uv = region.xy + v.uv * region.zw;
            uv = clamp(uv, region.xy + 0.5 * texel, region.xy + region.zw - 0.5 * texel);
            return source.SampleLevel(linearClamp, uv, 0);
        }
    )";
    ComPtr<ID3DBlob> vs, ps;
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "vs", "vs_5_0",
                           D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &vs, nullptr);
    if (FAILED(hr)) return hr;
    hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "ps", "ps_5_0",
                    D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &ps, nullptr);
    if (FAILED(hr)) return hr;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable = {1, &range};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants = {0, 0, 6};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_desc{2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> signature;
    hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr);
    if (FAILED(hr)) return hr;
    ComPtr<ID3D12RootSignature> root;
    hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                     IID_PPV_ARGS(&root));
    if (FAILED(hr)) return hr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = color_format;
    pso.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline));
    if (FAILED(hr)) return hr;

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = buffer_count;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&present_srvs));
    if (FAILED(hr)) return hr;
    present_root = std::move(root);
    present_pipeline = std::move(pipeline);
    return S_OK;
}

Renderer::Renderer() noexcept = default;
Renderer::~Renderer() = default;

HRESULT Renderer::initialize(const RendererOptions& options) noexcept {
    if (impl_) return DXGI_ERROR_INVALID_CALL;
    std::unique_ptr<Impl> instance(new (std::nothrow) Impl);
    if (!instance) return E_OUTOFMEMORY;
    const HRESULT hr = instance->initialize(options);
    if (SUCCEEDED(hr)) impl_ = std::move(instance);
    return hr;
}

ID3D12Device* Renderer::device() const noexcept {
    return impl_ ? impl_->device.Get() : nullptr;
}

HRESULT Renderer::begin_frame(Frame& frame) noexcept {
    frame = {};
    if (!impl_ || impl_->recording) return DXGI_ERROR_INVALID_CALL;
    auto& impl = *impl_;
    if (FAILED(impl.submission_error)) return impl.submission_error;

    if (impl.options.window) {
        UINT width = 0;
        UINT height = 0;
        HRESULT hr = window_extent(impl.options.window, width, height);
        if (hr != S_OK) return hr;
        if (impl.occluded) {
            hr = impl.swapchain->Present(0, DXGI_PRESENT_TEST);
            if (hr == DXGI_STATUS_OCCLUDED) return S_FALSE;
            if (FAILED(hr)) return hr;
            impl.occluded = false;
        }
        if (width != impl.width || height != impl.height) {
            hr = impl.make_targets(width, height);
            if (FAILED(hr)) return hr;
        }
    }

    impl.active = impl.swapchain ? impl.swapchain->GetCurrentBackBufferIndex() : impl.offscreen_index;
    auto& slot = impl.slots[impl.active];
    HRESULT hr = impl.wait(slot.fence_value);
    if (FAILED(hr)) return hr;
    hr = slot.allocator->Reset();
    if (FAILED(hr)) return hr;
    hr = impl.commands->Reset(slot.allocator.Get(), nullptr);
    if (FAILED(hr)) return hr;
    impl.recording = true;
    impl.present_recorded = false;

    if (impl.swapchain) {
        transition_color(impl.commands.Get(), slot.color.Get(), D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    const auto rtv = impl.descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, impl.active);
    const auto dsv = impl.descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, impl.active);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(impl.width), static_cast<float>(impl.height), 0, 1};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(impl.width), static_cast<LONG>(impl.height)};
    impl.commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    impl.commands->RSSetViewports(1, &viewport);
    impl.commands->RSSetScissorRects(1, &scissor);
    frame = {impl.commands.Get(), slot.color.Get(), slot.depth.Get(), rtv, dsv, impl.width, impl.height, impl.active};
    return S_OK;
}

HRESULT Renderer::present_texture(ID3D12Resource* source, D3D12_RESOURCE_STATES source_state,
                                  const Crop* crop) noexcept {
    if (!impl_ || !impl_->recording || impl_->present_recorded) return DXGI_ERROR_INVALID_CALL;
    auto& impl = *impl_;
    const auto& slot = impl.slots[impl.active];
    if (!source || source == slot.color.Get() || source == slot.depth.Get()) return E_INVALIDARG;
    const auto desc = source->GetDesc();
    DXGI_FORMAT view_format = desc.Format;
    if (view_format == DXGI_FORMAT_R8G8B8A8_TYPELESS) view_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (view_format == DXGI_FORMAT_B8G8R8A8_TYPELESS) view_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
        desc.SampleDesc.Count != 1 || (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
        (view_format != DXGI_FORMAT_R8G8B8A8_UNORM && view_format != DXGI_FORMAT_B8G8R8A8_UNORM)) {
        return E_INVALIDARG;
    }
    const UINT width = static_cast<UINT>(desc.Width);
    const Crop region = crop ? *crop : Crop{0, 0, width, desc.Height};
    if (!region.width || !region.height || region.x >= width || region.y >= desc.Height ||
        region.width > width - region.x || region.height > desc.Height - region.y) return E_INVALIDARG;
    ComPtr<ID3D12Device> source_device;
    HRESULT hr = source->GetDevice(IID_PPV_ARGS(&source_device));
    if (FAILED(hr)) return hr;
    if (source_device.Get() != device()) return E_INVALIDARG;
    if (!impl.present_pipeline) {
        hr = impl.make_present_pipeline();
        if (FAILED(hr)) return hr;
    }

    auto cpu = impl.present_srvs->GetCPUDescriptorHandleForHeapStart();
    auto gpu = impl.present_srvs->GetGPUDescriptorHandleForHeapStart();
    const UINT offset = impl.active * device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu.ptr += offset;
    gpu.ptr += offset;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = view_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device()->CreateShaderResourceView(source, &srv, cpu);

    auto* commands = impl.commands.Get();
    const auto rtv = impl.descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, impl.active);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(impl.width), static_cast<float>(impl.height), 0, 1};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(impl.width), static_cast<LONG>(impl.height)};
    commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    constexpr auto sampled = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (source_state != sampled) transition_color(commands, source, source_state, sampled, 0);
    ID3D12DescriptorHeap* heaps[] = {impl.present_srvs.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootSignature(impl.present_root.Get());
    commands->SetPipelineState(impl.present_pipeline.Get());
    commands->SetGraphicsRootDescriptorTable(0, gpu);
    const float inv_w = 1.f / static_cast<float>(width), inv_h = 1.f / static_cast<float>(desc.Height);
    const float mapping[] = {static_cast<float>(region.x) * inv_w, static_cast<float>(region.y) * inv_h,
                             static_cast<float>(region.width) * inv_w, static_cast<float>(region.height) * inv_h,
                             inv_w, inv_h};
    commands->SetGraphicsRoot32BitConstants(1, 6, mapping, 0);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
    if (source_state != sampled) transition_color(commands, source, sampled, source_state, 0);
    impl.present_recorded = true;
    return S_OK;
}

HRESULT Renderer::end_frame(bool* executed) noexcept {
    if (executed) *executed = false;
    if (!impl_ || !impl_->recording) return DXGI_ERROR_INVALID_CALL;
    auto& impl = *impl_;
    auto& slot = impl.slots[impl.active];
    if (impl.swapchain) {
        transition_color(impl.commands.Get(), slot.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PRESENT);
    }
    impl.recording = false;
    HRESULT hr = impl.commands->Close();
    if (FAILED(hr)) return impl.submission_error = hr;

    ID3D12CommandList* lists[] = {impl.commands.Get()};
    impl.queue->ExecuteCommandLists(1, lists);
    if (executed) *executed = true;
    HRESULT present = S_OK;
    if (impl.swapchain) {
        const UINT interval = impl.options.vsync ? 1 : 0;
        const UINT flags = impl.swap_flags ? DXGI_PRESENT_ALLOW_TEARING : 0;
        present = impl.swapchain->Present(interval, flags);
        impl.occluded = present == DXGI_STATUS_OCCLUDED;
    } else {
        impl.offscreen_index = (impl.active + 1) % buffer_count;
    }

    hr = impl.queue->Signal(impl.fence.Get(), impl.last_signal + 1);
    if (FAILED(hr)) return impl.submission_error = hr;
    slot.fence_value = ++impl.last_signal;
    return present;
}

HRESULT Renderer::cancel_frame() noexcept {
    if (!impl_ || !impl_->recording) return DXGI_ERROR_INVALID_CALL;
    impl_->recording = false;
    const HRESULT hr = impl_->commands->Close();
    if (FAILED(hr)) impl_->submission_error = hr;
    return hr;
}

HRESULT Renderer::wait_idle() noexcept {
    if (!impl_ || impl_->recording) return DXGI_ERROR_INVALID_CALL;
    // Even after a list recording error, earlier submissions can be in flight.
    const HRESULT hr = impl_->wait(impl_->last_signal);
    if (FAILED(hr)) return hr;
    return impl_->submission_error;
}

}  // namespace catherine::dx12
