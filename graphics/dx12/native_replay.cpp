#include "native_replay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace catherine::dx12 {

using Microsoft::WRL::ComPtr;

HRESULT Resource::attach(ID3D12Resource* resource, D3D12_RESOURCE_STATES initial_state, UINT planes) {
    if (!resource || resource_) return E_INVALIDARG;
    const auto desc = resource->GetDesc();

    D3D12_HEAP_PROPERTIES heap{};
    const HRESULT hr = resource->GetHeapProperties(&heap, nullptr);
    if (FAILED(hr)) return hr;
    ComPtr<ID3D12Device> device;
    const HRESULT owner = resource->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(owner)) return owner;
    UINT count = 1;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
        D3D12_FEATURE_DATA_FORMAT_INFO info{desc.Format, 0};
        const HRESULT result = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info));
        if (FAILED(result)) return result;
        count = info.PlaneCount;
    }
    const UINT layers = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
    const UINT64 size = UINT64(desc.MipLevels) * layers * count;
    if (!size || size > D3D12_REQ_SUBRESOURCES || (planes && planes != count)) return E_INVALIDARG;
    const bool fixed = heap.Type == D3D12_HEAP_TYPE_UPLOAD || heap.Type == D3D12_HEAP_TYPE_READBACK;
    if ((heap.Type == D3D12_HEAP_TYPE_UPLOAD && initial_state != D3D12_RESOURCE_STATE_GENERIC_READ) ||
        (heap.Type == D3D12_HEAP_TYPE_READBACK && initial_state != D3D12_RESOURCE_STATE_COPY_DEST))
        return E_INVALIDARG;
    states_.assign(static_cast<size_t>(size), initial_state);
    resource_ = resource;
    device_ = std::move(device);
    planes_ = count;
    fixed_ = fixed;
    decays_ = !fixed && (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
                         (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS));
    return S_OK;
}

namespace {

constexpr UINT resource_capacity = 16384;
constexpr UINT sampler_capacity = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
constexpr UINT64 upload_page_size = 4ull << 20;
constexpr UINT64 upload_alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

bool fits_product(UINT64 a, UINT64 b) {
    return !b || a <= (std::numeric_limits<UINT64>::max)() / b;
}

bool valid_buffer_range(const Resource& resource, UINT64 offset, UINT64 bytes) {
    if (!resource.get() || !bytes) return false;
    const auto desc = resource.get()->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && offset <= desc.Width && bytes <= desc.Width - offset;
}

bool overlap(const ResourceAccess& a, const ResourceAccess& b) {
    return a.resource == b.resource &&
           UINT64(a.first_subresource) < UINT64(b.first_subresource) + b.count &&
           UINT64(b.first_subresource) < UINT64(a.first_subresource) + a.count;
}

bool valid_viewport(const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor) {
    return std::isfinite(vp.TopLeftX) && std::isfinite(vp.TopLeftY) &&
           std::isfinite(vp.Width) && std::isfinite(vp.Height) && vp.Width > 0 && vp.Height > 0 &&
           vp.MinDepth >= 0 && vp.MinDepth <= vp.MaxDepth && vp.MaxDepth <= 1 &&
           scissor.left <= scissor.right && scissor.top <= scissor.bottom;
}

}  // namespace

struct NativeReplay::Impl {
    struct HeapPage {
        ComPtr<ID3D12DescriptorHeap> resources, samplers;
        UINT resource_cursor = 0, sampler_cursor = 0;
    };

    struct FrameStorage {
        struct UploadPage {
            ComPtr<ID3D12Resource> resource;
            uint8_t* mapped = nullptr;
            UINT64 size = 0, offset = 0;
        };

        std::vector<HeapPage> pages;
        std::vector<UploadPage> uploads;
        size_t active_page = 0;
        std::unordered_map<ID3D12DeviceChild*, ComPtr<ID3D12DeviceChild>> retained;

        void retain(ID3D12DeviceChild* object) { retained.try_emplace(object, object); }
    };

    struct PreparedPipeline {
        ID3D12DescriptorHeap* resource_heap = nullptr;
        ID3D12DescriptorHeap* sampler_heap = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE resources{}, samplers{};
    };

    struct Staging {
        ID3D12Resource* resource = nullptr;
        uint8_t* mapped = nullptr;
        UINT64 offset = 0;
    };

    Renderer& renderer;
    Frame frame{};
    std::array<FrameStorage, Renderer::frame_slots> storage;
    std::unordered_map<Resource*, std::vector<D3D12_RESOURCE_STATES>> journal;

    explicit Impl(Renderer& renderer) : renderer(renderer) {}

    FrameStorage& current() { return storage[frame.slot]; }

    HRESULT prepare(const PipelineUse& use, PipelineDomain domain, PreparedPipeline& prepared);
    void bind(const PipelineUse& use, const PreparedPipeline& prepared);
    HRESULT staging(UINT64 bytes, Staging& result);

    HRESULT depth_accesses(const Attachment& attachment, D3D12_RESOURCE_STATES state,
                            std::vector<ResourceAccess>& result) {
        auto* resource = attachment.resource;
        if (!resource || !resource->get() || !attachment.descriptor.ptr ||
            !(resource->get()->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) return E_INVALIDARG;
        const UINT plane_stride = resource->subresource_count() / resource->planes_;
        if (attachment.subresource >= plane_stride) return E_INVALIDARG;
        for (UINT plane = 0; plane < resource->planes_; ++plane)
            result.push_back({resource, state, attachment.subresource + plane * plane_stride});
        return S_OK;
    }

    HRESULT resource_accesses(std::span<const ResourceAccess> accesses) {
        if (!frame.commands) return DXGI_ERROR_INVALID_CALL;
        for (size_t i = 0; i < accesses.size(); ++i) {
            const ResourceAccess& access = accesses[i];
            if (!access.resource || access.resource->device_.Get() != renderer.device() || !access.count ||
                UINT64(access.first_subresource) + access.count > access.resource->states_.size()) return E_INVALIDARG;
            if (access.resource->fixed_ && (!access.state || (access.resource->states_[0] & access.state) != access.state))
                return E_INVALIDARG;
            for (size_t j = 0; j < i; ++j)
                if (access.state != accesses[j].state && overlap(access, accesses[j])) return E_INVALIDARG;
        }

        for (const auto& access : accesses) {
            Resource& resource = *access.resource;
            auto [it, inserted] = journal.try_emplace(&resource, resource.states_);
            if (inserted) current().retain(resource.resource_.Get());
            if (resource.fixed_) continue;

            auto& states = it->second;
            bool ordered_uav = false;
            const UINT end = access.first_subresource + access.count;
            for (UINT index = access.first_subresource; index < end; ++index) {
                auto& state = states[index];
                if (state == access.state) {
                    ordered_uav |= access.state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    continue;
                }
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition = {resource.resource_.Get(), index, state, access.state};
                frame.commands->ResourceBarrier(1, &barrier);
                state = access.state;
            }
            if (ordered_uav) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = resource.resource_.Get();
                frame.commands->ResourceBarrier(1, &barrier);
            }
        }
        return S_OK;
    }
};

HRESULT NativeReplay::Impl::staging(UINT64 bytes, Staging& result) {
    if (!frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (!bytes || bytes > (std::numeric_limits<SIZE_T>::max)() - upload_alignment) return E_INVALIDARG;
    auto& uploads = current().uploads;
    for (auto& candidate : uploads) {
        const UINT64 start = (candidate.offset + upload_alignment - 1) & ~(upload_alignment - 1);
        if (start <= candidate.size && bytes <= candidate.size - start) {
            result = {candidate.resource.Get(), candidate.mapped + start, start};
            candidate.offset = start + bytes;
            return S_OK;
        }
    }

    FrameStorage::UploadPage page;
    page.size = (std::max)(bytes, upload_page_size);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = page.size;
    desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                             IID_PPV_ARGS(&page.resource));
    if (FAILED(hr)) return hr;
    const D3D12_RANGE no_reads{};
    void* mapped = nullptr;
    hr = page.resource->Map(0, &no_reads, &mapped);
    if (FAILED(hr)) return hr;
    page.mapped = static_cast<uint8_t*>(mapped);
    page.offset = bytes;
    result = {page.resource.Get(), page.mapped, 0};
    uploads.push_back(std::move(page));
    return S_OK;
}

HRESULT NativeReplay::Impl::prepare(const PipelineUse& use, PipelineDomain domain, PreparedPipeline& prepared) {
    if (!frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (!use.layout || !use.layout->root_signature() || !use.pipeline || use.layout->domain() != domain)
        return E_INVALIDARG;

    const UINT resource_count = use.layout->resource_count();
    const UINT sampler_count = use.layout->sampler_count();
    if (resource_count > resource_capacity || sampler_count > sampler_capacity) return E_NOTIMPL;
    if (use.descriptors.size() != resource_count + sampler_count) return E_INVALIDARG;
    if (use.descriptors.empty()) return S_OK;

    std::vector<bool> filled(resource_count + sampler_count);
    for (const auto& descriptor : use.descriptors) {
        const UINT slot = use.layout->offset(descriptor.type, descriptor.shader_register, descriptor.register_space);
        if (slot == BindingLayout::missing || !descriptor.source.ptr) return E_INVALIDARG;
        const UINT offset = descriptor.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ? resource_count + slot : slot;
        if (filled[offset]) return E_INVALIDARG;
        filled[offset] = true;
    }

    auto& slot_storage = current();
    while (slot_storage.active_page < slot_storage.pages.size()) {
        const auto& page = slot_storage.pages[slot_storage.active_page];
        if (resource_count <= resource_capacity - page.resource_cursor &&
            sampler_count <= sampler_capacity - page.sampler_cursor) break;
        ++slot_storage.active_page;
    }
    auto* device = renderer.device();
    if (slot_storage.active_page == slot_storage.pages.size()) {
        HeapPage page;
        D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, resource_capacity,
                                       D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&page.resources));
        if (FAILED(hr)) return hr;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        desc.NumDescriptors = sampler_capacity;
        hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&page.samplers));
        if (FAILED(hr)) return hr;
        slot_storage.pages.push_back(std::move(page));
    }

    auto& page = slot_storage.pages[slot_storage.active_page];
    const UINT resource_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT sampler_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    auto resource_cpu = page.resources->GetCPUDescriptorHandleForHeapStart();
    auto sampler_cpu = page.samplers->GetCPUDescriptorHandleForHeapStart();
    resource_cpu.ptr += SIZE_T(page.resource_cursor) * resource_stride;
    sampler_cpu.ptr += SIZE_T(page.sampler_cursor) * sampler_stride;
    prepared.resource_heap = page.resources.Get();
    prepared.sampler_heap = page.samplers.Get();
    prepared.resources = page.resources->GetGPUDescriptorHandleForHeapStart();
    prepared.samplers = page.samplers->GetGPUDescriptorHandleForHeapStart();
    prepared.resources.ptr += UINT64(page.resource_cursor) * resource_stride;
    prepared.samplers.ptr += UINT64(page.sampler_cursor) * sampler_stride;

    for (const auto& descriptor : use.descriptors) {
        const UINT slot = use.layout->offset(descriptor.type, descriptor.shader_register, descriptor.register_space);
        const bool sampler = descriptor.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        auto destination = sampler ? sampler_cpu : resource_cpu;
        destination.ptr += SIZE_T(slot) * (sampler ? sampler_stride : resource_stride);
        device->CopyDescriptorsSimple(1, destination, descriptor.source,
                                      sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
                                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    page.resource_cursor += resource_count;
    page.sampler_cursor += sampler_count;
    return S_OK;
}

void NativeReplay::Impl::bind(const PipelineUse& use, const PreparedPipeline& prepared) {
    current().retain(use.layout->root_signature());
    current().retain(use.pipeline);
    if (prepared.resource_heap) {
        ID3D12DescriptorHeap* heaps[] = {prepared.resource_heap, prepared.sampler_heap};
        frame.commands->SetDescriptorHeaps(2, heaps);
    }
    use.layout->bind(frame.commands, prepared.resources, prepared.samplers);
    frame.commands->SetPipelineState(use.pipeline);
}

NativeReplay::NativeReplay(Renderer& renderer) : impl_(std::make_unique<Impl>(renderer)) {}

NativeReplay::~NativeReplay() {
    if (impl_->frame.commands) cancel();
    if (impl_->renderer.device()) impl_->renderer.wait_idle();
}

HRESULT NativeReplay::begin(Frame& output) {
    output = {};
    if (impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;

    const HRESULT hr = impl_->renderer.begin_frame(impl_->frame);
    if (hr != S_OK) return hr;
    auto& storage = impl_->current();
    storage.retained.clear();
    storage.active_page = 0;
    for (auto& page : storage.pages) page.resource_cursor = page.sampler_cursor = 0;
    for (auto& page : storage.uploads) page.offset = 0;
    output = impl_->frame;
    return S_OK;
}

HRESULT NativeReplay::submit() {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    bool executed = false;
    const HRESULT hr = impl_->renderer.end_frame(&executed);
    if (executed) {
        for (auto& [resource, states] : impl_->journal) {
            if (resource->decays_) std::fill(states.begin(), states.end(), D3D12_RESOURCE_STATE_COMMON);
            resource->states_.swap(states);
        }
    }
    impl_->journal.clear();
    impl_->frame = {};
    return hr;
}

HRESULT NativeReplay::cancel() {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    const HRESULT hr = impl_->renderer.cancel_frame();
    impl_->current().retained.clear();
    impl_->journal.clear();
    impl_->frame = {};
    return hr;
}

HRESULT NativeReplay::draw(const GraphicsDraw& draw) {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (!draw.count || !draw.instances) return S_OK;
    if (draw.colors.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT ||
        (draw.colors.empty() && !draw.depth) || draw.vertices.size() > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT ||
        draw.topology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED || !valid_viewport(draw.viewport, draw.scissor))
        return E_INVALIDARG;

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
    std::vector<ResourceAccess> accesses(draw.bindings.accesses.begin(), draw.bindings.accesses.end());
    for (size_t i = 0; i < draw.colors.size(); ++i) {
        const auto& attachment = draw.colors[i];
        if (!attachment.descriptor.ptr) return E_INVALIDARG;
        colors[i] = attachment.descriptor;
        accesses.push_back({attachment.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, attachment.subresource});
    }
    if (draw.depth) {
        const auto state = draw.depth_read_only ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
        const HRESULT hr = impl_->depth_accesses(*draw.depth, state, accesses);
        if (FAILED(hr)) return hr;
    }
    Impl::PreparedPipeline prepared;
    HRESULT hr = impl_->prepare(draw.bindings, PipelineDomain::Graphics, prepared);
    if (FAILED(hr)) return hr;
    hr = access(accesses);
    if (FAILED(hr)) return hr;

    impl_->bind(draw.bindings, prepared);
    auto* commands = impl_->frame.commands;
    commands->OMSetRenderTargets(static_cast<UINT>(draw.colors.size()), colors.data(), FALSE,
                                 draw.depth ? &draw.depth->descriptor : nullptr);
    commands->RSSetViewports(1, &draw.viewport);
    commands->RSSetScissorRects(1, &draw.scissor);
    commands->OMSetStencilRef(draw.stencil_reference);
    commands->OMSetBlendFactor(draw.blend_factor);
    commands->IASetPrimitiveTopology(draw.topology);
    if (!draw.vertices.empty())
        commands->IASetVertexBuffers(0, static_cast<UINT>(draw.vertices.size()), draw.vertices.data());
    if (draw.indices) {
        commands->IASetIndexBuffer(draw.indices);
        commands->DrawIndexedInstanced(draw.count, draw.instances, draw.first, draw.base_vertex, draw.first_instance);
    } else {
        commands->DrawInstanced(draw.count, draw.instances, draw.first, draw.first_instance);
    }
    return S_OK;
}

HRESULT NativeReplay::dispatch(const ComputeDispatch& dispatch) {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    for (UINT group : dispatch.groups) {
        if (group > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) return E_INVALIDARG;
    }
    if (!dispatch.groups[0] || !dispatch.groups[1] || !dispatch.groups[2]) return S_OK;
    Impl::PreparedPipeline prepared;
    HRESULT hr = impl_->prepare(dispatch.bindings, PipelineDomain::Compute, prepared);
    if (FAILED(hr)) return hr;
    hr = access(dispatch.bindings.accesses);
    if (FAILED(hr)) return hr;
    impl_->bind(dispatch.bindings, prepared);
    impl_->frame.commands->Dispatch(dispatch.groups[0], dispatch.groups[1], dispatch.groups[2]);
    return S_OK;
}

HRESULT NativeReplay::clear_color(const Attachment& attachment, const float (&rgba)[4]) {
    if (!attachment.descriptor.ptr) return E_INVALIDARG;
    const ResourceAccess use{attachment.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, attachment.subresource};
    const HRESULT hr = access({&use, 1});
    if (FAILED(hr)) return hr;
    impl_->frame.commands->ClearRenderTargetView(attachment.descriptor, rgba, 0, nullptr);
    return S_OK;
}

HRESULT NativeReplay::clear_depth(const Attachment& attachment, D3D12_CLEAR_FLAGS flags, float depth, UINT8 stencil) {
    constexpr auto supported = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
    if (!attachment.descriptor.ptr || !flags || (flags & ~supported) ||
        ((flags & D3D12_CLEAR_FLAG_DEPTH) && !(depth >= 0 && depth <= 1))) return E_INVALIDARG;
    std::vector<ResourceAccess> uses;
    HRESULT hr = impl_->depth_accesses(attachment, D3D12_RESOURCE_STATE_DEPTH_WRITE, uses);
    if (FAILED(hr)) return hr;
    if ((flags & D3D12_CLEAR_FLAG_STENCIL) && uses.size() == 1) return E_INVALIDARG;
    hr = access(uses);
    if (FAILED(hr)) return hr;
    impl_->frame.commands->ClearDepthStencilView(attachment.descriptor, flags, depth, stencil, 0, nullptr);
    return S_OK;
}

HRESULT NativeReplay::present(Resource& source, const Crop* crop) {
    const ResourceAccess use{&source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0};
    const HRESULT hr = access({&use, 1});
    if (FAILED(hr)) return hr;
    return impl_->renderer.present_texture(source.get(), use.state, crop);
}

HRESULT NativeReplay::upload_texture(Resource& texture, UINT first, std::span<const TextureData> data) {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (!texture.get() || texture.device_.Get() != impl_->renderer.device() || data.empty() ||
        first > texture.subresource_count() || data.size() > texture.subresource_count() - first) return E_INVALIDARG;
    const auto desc = texture.get()->GetDesc();
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.SampleDesc.Count != 1) return E_INVALIDARG;

    struct Pitch { UINT64 row, slice; };
    const UINT count = static_cast<UINT>(data.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);
    std::vector<UINT> rows(count);
    std::vector<UINT64> row_bytes(count);
    std::vector<Pitch> source_pitches(count);
    UINT64 bytes = 0;
    impl_->renderer.device()->GetCopyableFootprints(&desc, first, count, 0, layouts.data(), rows.data(),
                                                    row_bytes.data(), &bytes);
    if (!bytes || bytes == (std::numeric_limits<UINT64>::max)()) return E_INVALIDARG;
    for (UINT i = 0; i < count; ++i) {
        const auto& input = data[i];
        const UINT64 row = input.row_pitch ? input.row_pitch : row_bytes[i];
        if (row < row_bytes[i] || !rows[i] || !fits_product(row, rows[i])) return E_INVALIDARG;
        const UINT64 last_row = row * (rows[i] - 1) + row_bytes[i];
        const UINT64 slice = input.slice_pitch ? input.slice_pitch : row * rows[i];
        const UINT depth = layouts[i].Footprint.Depth;
        if (slice < last_row || !depth || !fits_product(slice, depth) ||
            slice * (depth - 1) + last_row > input.bytes.size()) return E_INVALIDARG;
        source_pitches[i] = {row, slice};
    }
    Impl::Staging staging;
    HRESULT hr = impl_->staging(bytes, staging);
    if (FAILED(hr)) return hr;
    const ResourceAccess use{&texture, D3D12_RESOURCE_STATE_COPY_DEST, first, count};
    hr = access({&use, 1});
    if (FAILED(hr)) return hr;
    for (UINT i = 0; i < count; ++i) {
        auto& layout = layouts[i];
        const auto& pitch = source_pitches[i];
        const UINT64 dst_row = layout.Footprint.RowPitch;
        const UINT64 dst_slice = dst_row * rows[i];
        for (UINT z = 0; z < layout.Footprint.Depth; ++z)
            for (UINT y = 0; y < rows[i]; ++y)
                std::memcpy(staging.mapped + layout.Offset + z * dst_slice + y * dst_row,
                            data[i].bytes.data() + z * pitch.slice + y * pitch.row,
                            static_cast<SIZE_T>(row_bytes[i]));

        layout.Offset += staging.offset;
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.pResource = staging.resource;
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = layout;
        to.pResource = texture.get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = first + i;
        impl_->frame.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    return S_OK;
}

HRESULT NativeReplay::upload_buffer(Resource& buffer, UINT64 offset, std::span<const uint8_t> data) {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (!valid_buffer_range(buffer, offset, data.size()) || buffer.device_.Get() != impl_->renderer.device())
        return E_INVALIDARG;
    Impl::Staging staging;
    HRESULT hr = impl_->staging(data.size(), staging);
    if (FAILED(hr)) return hr;
    const ResourceAccess use{&buffer, D3D12_RESOURCE_STATE_COPY_DEST};
    hr = access({&use, 1});
    if (FAILED(hr)) return hr;
    std::memcpy(staging.mapped, data.data(), data.size());
    impl_->frame.commands->CopyBufferRegion(buffer.get(), offset, staging.resource, staging.offset, data.size());
    return S_OK;
}

HRESULT NativeReplay::copy_buffer(Resource& dst, UINT64 dst_offset, Resource& src, UINT64 src_offset, UINT64 bytes) {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (dst.get() == src.get() || !valid_buffer_range(dst, dst_offset, bytes) ||
        !valid_buffer_range(src, src_offset, bytes)) return E_INVALIDARG;
    const ResourceAccess uses[] = {
        {&dst, D3D12_RESOURCE_STATE_COPY_DEST}, {&src, D3D12_RESOURCE_STATE_COPY_SOURCE},
    };
    const HRESULT hr = access(uses);
    if (FAILED(hr)) return hr;
    impl_->frame.commands->CopyBufferRegion(dst.get(), dst_offset, src.get(), src_offset, bytes);
    return S_OK;
}

HRESULT NativeReplay::resolve_color(Resource& dst, UINT dst_subresource, Resource& src, UINT src_subresource,
                                    DXGI_FORMAT view_format) {
    if (!impl_->frame.commands) return DXGI_ERROR_INVALID_CALL;
    if (!dst.get() || !src.get() || dst.get() == src.get() || view_format == DXGI_FORMAT_UNKNOWN ||
        dst_subresource >= dst.subresource_count() || src_subresource >= src.subresource_count())
        return E_INVALIDARG;
    const auto dst_desc = dst.get()->GetDesc(), src_desc = src.get()->GetDesc();
    const UINT mip = dst_subresource % dst_desc.MipLevels;
    if (dst_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        src_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        dst_desc.SampleDesc.Count != 1 || src_desc.SampleDesc.Count == 1 ||
        (dst_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
        (src_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
        (std::max)(1ull, dst_desc.Width >> mip) != src_desc.Width ||
        (std::max)(1u, dst_desc.Height >> mip) != src_desc.Height) return E_INVALIDARG;

    const ResourceAccess uses[] = {
        {&dst, D3D12_RESOURCE_STATE_RESOLVE_DEST, dst_subresource},
        {&src, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, src_subresource},
    };
    const HRESULT hr = access(uses);
    if (FAILED(hr)) return hr;
    impl_->frame.commands->ResolveSubresource(dst.get(), dst_subresource, src.get(), src_subresource, view_format);
    return S_OK;
}

HRESULT NativeReplay::access(std::span<const ResourceAccess> resources) {
    return impl_->resource_accesses(resources);
}

}  // namespace catherine::dx12
