#pragma once

#include "native_replay.h"

namespace catherine::dx12 {

// NVN texture target and format values come from the SDK's decoded builders.
struct NvnImageDesc {
    UINT target = 1;
    UINT format = 0x25;
    UINT width = 1, height = 1, depth = 1;
    UINT levels = 1;
    UINT samples = 1;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
};

struct NvnImageFormat {
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT sampled = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT color = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT depth = DXGI_FORMAT_UNKNOWN;
    UINT planes = 1;
    bool decoded_astc = false;
    bool opaque_alpha = false;
};

NvnImageFormat nvn_image_format(UINT format) noexcept;
DXGI_FORMAT nvn_vertex_format(UINT format) noexcept;

// Converts a builder into committed native storage. ASTC uploads must already
// be decoded to RGBA8, and formats without a direct representation fail.
HRESULT create_nvn_image(ID3D12Device* device, const NvnImageDesc& desc, Resource& output,
                          const D3D12_CLEAR_VALUE* optimized_clear = nullptr);
// Untiles the complete 1D/2D texture or array using the shared NVN CPU code.
// BC data stays compressed. ASTC is decoded to RGBA8 before native upload.
HRESULT upload_nvn_tiled_image(NativeReplay& replay, Resource& image, UINT nvn_format, UINT block_height_gobs,
                               std::span<const uint8_t> tiled);

}  // namespace catherine::dx12
