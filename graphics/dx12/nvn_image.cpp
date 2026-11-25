#include "nvn_image.h"

#include <algorithm>
#include <bit>

namespace catherine::dx12 {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT unknown = DXGI_FORMAT_UNKNOWN;
constexpr DXGI_FORMAT direct_formats[] = {
    /* 00 */ unknown,
    /* 01 */ DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT,
    /* 05 */ DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT,
    /* 0a */ DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT,
    /* 0d */ DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT,
    /* 11 */ DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM,
    /* 14 */ DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT,
    /* 16 */ DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_SINT,
    /* 19 */ unknown, unknown, unknown, unknown, unknown, unknown, unknown, unknown, unknown,
    /* 22 */ DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_SINT,
    /* 25 */ DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT,
    /* 29 */ DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM,
    /* 2c */ DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT,
    /* 2e */ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT,
    /* 31 */ unknown, unknown, unknown, unknown, unknown, unknown,
    /* 37 */ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    /* 39 */ unknown, DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM, unknown,
    /* 3d */ DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UINT, DXGI_FORMAT_R11G11B10_FLOAT,
    /* 40 */ DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
    /* 41 */ DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC3_UNORM,
    /* 45 */ DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC2_UNORM_SRGB, DXGI_FORMAT_BC3_UNORM_SRGB,
    /* 49 */ DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_SNORM,
    /* 4d */ DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_BC6H_SF16, DXGI_FORMAT_BC6H_UF16,
};
static_assert(std::size(direct_formats) == 0x51);

NvnImageFormat color(DXGI_FORMAT view, DXGI_FORMAT storage = unknown, bool opaque = false) {
    return {storage == unknown ? view : storage, view, view, unknown, 1, false, opaque};
}

}  // namespace

NvnImageFormat nvn_image_format(UINT format) noexcept {
    if (format >= 0x79 && format <= 0x94) {
        auto decoded = color(format >= 0x87 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
                             DXGI_FORMAT_R8G8B8A8_TYPELESS);
        decoded.decoded_astc = true;
        return decoded;
    }
    switch (format) {
    case 0x25: case 0x37: case 0x38:
        return color(direct_formats[format], DXGI_FORMAT_R8G8B8A8_TYPELESS, format == 0x37);
    case 0x32: return {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, unknown, DXGI_FORMAT_D16_UNORM, 1};
    case 0x33: case 0x35:
        return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, unknown, DXGI_FORMAT_D24_UNORM_S8_UINT, 2};
    case 0x34: return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, unknown, DXGI_FORMAT_D32_FLOAT, 1};
    case 0x36:
        return {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, unknown,
                DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 2};
    case 0x95: return color(DXGI_FORMAT_B5G6R5_UNORM);
    case 0x98: return color(DXGI_FORMAT_B5G5R5A1_UNORM);
    case 0x99: return color(DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_B8G8R8X8_TYPELESS);
    case 0x9a: return color(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_TYPELESS);
    case 0x9b: return color(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, DXGI_FORMAT_B8G8R8X8_TYPELESS);
    case 0x9c: return color(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_TYPELESS);
    default:
        if (format < std::size(direct_formats))
            return color(direct_formats[format], unknown, format == 0x3a || format == 0x41 || format == 0x45);
        return {};
    }
}

DXGI_FORMAT nvn_vertex_format(UINT format) noexcept {
    // Scaled integer vertex attributes require shader-side casts. They are
    // left to the converter instead of being interpreted as normalized data.
    return format < std::size(direct_formats) ? direct_formats[format] : DXGI_FORMAT_UNKNOWN;
}

HRESULT create_nvn_image(ID3D12Device* device, const NvnImageDesc& input, Resource& output,
                          const D3D12_CLEAR_VALUE* optimized_clear) {
    if (!device || output.get()) return E_INVALIDARG;
    const auto format = nvn_image_format(input.format);
    if (format.resource == unknown) return DXGI_ERROR_UNSUPPORTED;

    const bool one_d = input.target == 0 || input.target == 3;
    const bool volume = input.target == 2;
    const bool cube = input.target == 8 || input.target == 9;
    const bool array = input.target == 3 || input.target == 4 || input.target == 6 || input.target == 9;
    if (input.target > 9) return DXGI_ERROR_UNSUPPORTED;  // Buffer textures are buffer views.
    const UINT width = (std::max)(1u, input.width), height = one_d ? 1 : (std::max)(1u, input.height);
    const UINT samples = (std::max)(1u, input.samples);
    UINT depth = volume || array ? (std::max)(1u, input.depth) : 1;
    if (input.target == 3 && depth == 1) depth = (std::max)(1u, input.height);
    if (input.target == 8) depth = 6;
    if (cube && (depth % 6 || width != height)) return E_INVALIDARG;
    if (depth > UINT16_MAX) return E_INVALIDARG;
    const UINT fit = static_cast<UINT>(std::bit_width((std::max)({width, height, volume ? depth : 1u})));
    const UINT levels = (std::min)((std::max)(1u, input.levels), fit);
    if ((samples > 1 && (one_d || volume || cube || levels != 1)) ||
        (format.depth != unknown && (input.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)))
        return E_INVALIDARG;

    const auto surface = format.depth != unknown ? format.depth : format.color;
    if (optimized_clear && optimized_clear->Format != surface) return E_INVALIDARG;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = one_d ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                    : volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(depth);
    desc.MipLevels = static_cast<UINT16>(levels);
    desc.Format = format.resource;
    desc.SampleDesc.Count = samples;
    desc.Flags = input.flags;
    if (format.depth != unknown) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                        optimized_clear, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) return hr;
    return output.attach(resource.Get(), D3D12_RESOURCE_STATE_COMMON, format.planes);
}

}  // namespace catherine::dx12
