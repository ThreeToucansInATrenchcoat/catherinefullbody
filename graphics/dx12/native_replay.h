#pragma once

#include "binding_layout.h"
#include "renderer.h"

#include <span>

namespace catherine::dx12 {

// Tracks an explicitly transitioned resource on one native replay queue.
// The caller provides its initial state at creation.
// All later accesses must use the same replay instance.
class Resource {
public:
    Resource() = default;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    HRESULT attach(ID3D12Resource* resource, D3D12_RESOURCE_STATES initial_state, UINT planes = 0);
    ID3D12Resource* get() const noexcept { return resource_.Get(); }
    UINT subresource_count() const noexcept { return static_cast<UINT>(states_.size()); }

private:
    friend class NativeReplay;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    std::vector<D3D12_RESOURCE_STATES> states_;
    UINT planes_ = 1;
    bool decays_ = false;
    bool fixed_ = false;
};

struct ResourceAccess {
    Resource* resource = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    UINT first_subresource = 0;
    UINT count = 1;
};

// Each binding references a CPU descriptor in a non-shader-visible heap.
// The contents are copied into a frame heap before the GPU can see them.
struct DescriptorWrite {
    D3D12_DESCRIPTOR_RANGE_TYPE type;
    UINT shader_register;
    UINT register_space;
    D3D12_CPU_DESCRIPTOR_HANDLE source;
};

struct PipelineUse {
    const BindingLayout* layout = nullptr;
    ID3D12PipelineState* pipeline = nullptr;
    std::span<const DescriptorWrite> descriptors;
    // Include all resources referenced by the pipeline except render/depth
    // attachments (which are handled by GraphicsDraw and the clear methods).
    std::span<const ResourceAccess> accesses;
};

struct Attachment {
    Resource* resource = nullptr;
    UINT subresource = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
};

struct GraphicsDraw {
    PipelineUse bindings;
    std::span<const Attachment> colors;
    const Attachment* depth = nullptr;
    bool depth_read_only = false;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    // Slots starting at zero. Use zero-sized entries for unused slots.
    std::span<const D3D12_VERTEX_BUFFER_VIEW> vertices;
    const D3D12_INDEX_BUFFER_VIEW* indices = nullptr;
    UINT count = 0;
    UINT instances = 1;
    UINT first = 0;
    INT base_vertex = 0;
    UINT first_instance = 0;
    UINT stencil_reference = 0;
    float blend_factor[4]{};
};

struct ComputeDispatch {
    PipelineUse bindings;
    UINT groups[3]{};
};

struct TextureData {
    std::span<const uint8_t> bytes;
    UINT64 row_pitch = 0;    // zero uses tight rows
    UINT64 slice_pitch = 0;  // zero uses row_pitch * number of block rows
};

class NativeReplay {
public:
    explicit NativeReplay(Renderer& renderer);
    ~NativeReplay();
    NativeReplay(const NativeReplay&) = delete;
    NativeReplay& operator=(const NativeReplay&) = delete;

    // Renderer and Resource objects must outlive any open recording using
    // them. GPU resources and pipeline objects are retained by the frame slot.
    HRESULT begin(Frame& output);
    HRESULT submit();
    HRESULT cancel();
    HRESULT draw(const GraphicsDraw& draw);
    HRESULT dispatch(const ComputeDispatch& dispatch);
    HRESULT clear_color(const Attachment& attachment, const float (&rgba)[4]);
    HRESULT clear_depth(const Attachment& attachment, D3D12_CLEAR_FLAGS flags, float depth, UINT8 stencil);
    HRESULT present(Resource& source, const Crop* crop = nullptr);
    HRESULT upload_texture(Resource& texture, UINT first_subresource, std::span<const TextureData> data);
    HRESULT upload_buffer(Resource& buffer, UINT64 offset, std::span<const uint8_t> data);
    HRESULT copy_buffer(Resource& destination, UINT64 dst_offset, Resource& source, UINT64 src_offset, UINT64 bytes);
    HRESULT resolve_color(Resource& destination, UINT dst_subresource, Resource& source, UINT src_subresource,
                          DXGI_FORMAT view_format);

    // For native transfers recorded by the caller. UAV writes are ordered
    // conservatively when the resource stays in a UAV state.
    HRESULT access(std::span<const ResourceAccess> resources);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace catherine::dx12
