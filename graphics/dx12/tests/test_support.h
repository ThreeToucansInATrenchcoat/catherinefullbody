#pragma once

#include "renderer.h"

#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace catherine::dx12::test {

using Microsoft::WRL::ComPtr;

inline void check(HRESULT hr, const char* operation) {
    if (hr != S_OK) {
        std::fprintf(stderr, "%s returned 0x%08lx\n", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(operation);
    }
}

inline void expect(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

inline ComPtr<ID3D12Resource> buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type,
                                    D3D12_RESOURCE_STATES state,
                                    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                           IID_PPV_ARGS(&result)), "CreateCommittedResource");
    return result;
}

inline void record_readback(const Frame& frame, ID3D12Resource* readback,
                     const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout, ID3D12Resource* sampled = nullptr) {
    auto* commands = frame.commands;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {frame.color, 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = frame.color;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = layout;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (sampled) commands->CopyResource(sampled, frame.color);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commands->ResourceBarrier(1, &barrier);
}

inline void verify_debug_messages(ID3D12InfoQueue* info) {
    const UINT64 count = info->GetNumStoredMessagesAllowedByRetrievalFilter();
    bool clean = true;
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T size = 0;
        expect(SUCCEEDED(info->GetMessage(i, nullptr, &size)) && size >= sizeof(D3D12_MESSAGE),
               "D3D12 info message size");
        std::vector<char> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        check(info->GetMessage(i, message, &size), "D3D12 info message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
            std::fprintf(stderr, "D3D12: %s\n", message->pDescription);
            clean = false;
        }
    }
    expect(clean, "D3D12 debug layer should report zero warnings or errors");
}

}  // namespace catherine::dx12::test
