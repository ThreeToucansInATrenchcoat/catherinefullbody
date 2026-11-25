#include "morph_replay.h"
#include "native_replay.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace catherine::dx12 {

namespace {

using Microsoft::WRL::ComPtr;

HRESULT make_buffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE type,
                    D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& buffer) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.MipLevels = desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (type == D3D12_HEAP_TYPE_DEFAULT) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&buffer));
}

}  // namespace

// Execute a CPU-visible NVN morph for validation. Caches and GPU-resident
// resources belong to the future asynchronous replay path.
HRESULT execute_morph(Renderer& renderer, const ShaderBinary& shader, uint32_t vertex_count,
                      uint32_t destination_stride, float weight,
                      std::span<const uint8_t> deltas, std::span<const uint8_t> separate_base,
                      std::span<uint8_t> destination) {
    constexpr UINT cb_bytes = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
    constexpr auto cbv = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    constexpr auto uav = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    const bool separate = !separate_base.empty();
    auto* device = renderer.device();
    if (!device || shader.stage() != ShaderStage::Compute || !vertex_count ||
        vertex_count > 64 * D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
        deltas.size() < size_t(vertex_count) * 16 || deltas.size() % 16 ||
        (separate && separate_base.size() != deltas.size()) ||
        (destination_stride != 32 && destination_stride != 64) ||
        destination.size() < size_t(vertex_count) * destination_stride || destination.size() % destination_stride ||
        deltas.size() > UINT_MAX || destination.size() > UINT_MAX)
        return E_INVALIDARG;

    BindingLayout layout;
    const ShaderBinary* stages[] = {&shader};
    HRESULT hr = initialize_program_layout(layout, device, stages);
    if (FAILED(hr)) return hr;
    const UINT range_count = separate ? 3 : 2;
    if (layout.sampler_count() || layout.resource_count() != range_count + 1 ||
        layout.offset(cbv, 4, 0) == BindingLayout::missing) return E_INVALIDARG;
    for (UINT index = 0; index < range_count; ++index)
        if (layout.offset(uav, index, 1) == BindingLayout::missing) return E_INVALIDARG;

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = layout.root_signature();
    pso.CS = shader.bytecode();
    ComPtr<ID3D12PipelineState> pipeline;
    hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline));
    if (FAILED(hr)) return hr;

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, layout.resource_count(),
                                        D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ComPtr<ID3D12DescriptorHeap> heap;
    hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) return hr;
    const auto descriptor = [&](D3D12_DESCRIPTOR_RANGE_TYPE kind, UINT binding, UINT space) {
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(layout.offset(kind, binding, space)) * device->GetDescriptorHandleIncrementSize(heap_desc.Type);
        return cpu;
    };

    // Register space 1 keeps u0 as deltas even when the separate-base
    // variant uses different NVN storage slots for the two input streams.
    const std::span<const uint8_t> ranges[] = {deltas, separate ? separate_base : destination, destination};
    ComPtr<ID3D12Resource> uniforms, readback;
    hr = make_buffer(device, cb_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, uniforms);
    if (FAILED(hr)) return hr;
    hr = make_buffer(device, destination.size(), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback);
    if (FAILED(hr)) return hr;

    ComPtr<ID3D12Resource> gpu[3];
    Resource constants_resource, readback_resource, storage[3];
    hr = constants_resource.attach(uniforms.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
    if (FAILED(hr)) return hr;
    hr = readback_resource.attach(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    if (FAILED(hr)) return hr;
    const auto uniform_slot = descriptor(cbv, 4, 0);
    DescriptorWrite writes[4]{{cbv, 4, 0, uniform_slot}};
    ResourceAccess accesses[4]{{&constants_resource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER}};
    for (UINT index = 0; index < range_count; ++index) {
        hr = make_buffer(device, ranges[index].size(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, gpu[index]);
        if (FAILED(hr)) return hr;
        hr = storage[index].attach(gpu[index].Get(), D3D12_RESOURCE_STATE_COMMON);
        if (FAILED(hr)) return hr;
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.Buffer.NumElements = static_cast<UINT>(ranges[index].size() / 4);
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        const auto slot = descriptor(uav, index, 1);
        device->CreateUnorderedAccessView(gpu[index].Get(), nullptr, &view, slot);
        writes[index + 1] = {uav, index, 1, slot};
        accesses[index + 1] = {&storage[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    }

    const D3D12_RANGE empty{};
    void* mapped = nullptr;
    hr = uniforms->Map(0, &empty, &mapped);
    if (FAILED(hr)) return hr;
    std::memset(mapped, 0, cb_bytes);
    const uint32_t constants[] = {vertex_count, std::bit_cast<uint32_t>(weight)};
    std::memcpy(mapped, constants, sizeof(constants));
    uniforms->Unmap(0, nullptr);
    const D3D12_CONSTANT_BUFFER_VIEW_DESC constants_view{uniforms->GetGPUVirtualAddress(), cb_bytes};
    device->CreateConstantBufferView(&constants_view, uniform_slot);

    NativeReplay replay(renderer);
    Frame frame;
    hr = replay.begin(frame);
    if (hr != S_OK) return hr;
    for (UINT index = 0; index < range_count; ++index) {
        hr = replay.upload_buffer(storage[index], 0, ranges[index]);
        if (FAILED(hr)) return hr;
    }
    const ComputeDispatch dispatch{
        {&layout, pipeline.Get(), {writes, range_count + 1}, {accesses, range_count + 1}},
        {(vertex_count + 63) / 64, 1, 1},
    };
    hr = replay.dispatch(dispatch);
    if (FAILED(hr)) return hr;
    hr = replay.copy_buffer(readback_resource, 0, storage[range_count - 1], 0, destination.size());
    if (FAILED(hr)) return hr;
    hr = replay.submit();
    if (FAILED(hr)) return hr;
    hr = renderer.wait_idle();
    if (FAILED(hr)) return hr;

    const D3D12_RANGE bytes{0, destination.size()};
    hr = readback->Map(0, &bytes, &mapped);
    if (FAILED(hr)) return hr;
    std::memcpy(destination.data(), mapped, destination.size());
    readback->Unmap(0, &empty);
    return S_OK;
}

}  // namespace catherine::dx12
