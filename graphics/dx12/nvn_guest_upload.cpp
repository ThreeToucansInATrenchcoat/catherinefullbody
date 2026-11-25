#include "nvn_image.h"

#include "nvn_astc.h"
#include "nvn_tiling.h"

#include <array>

namespace catherine::dx12 {

namespace {

struct Blocks {
    UINT width, height, bytes;
};

Blocks block_shape(UINT format) {
    if (format >= 0x79 && format <= 0x94) {
        constexpr Blocks astc[] = {
            {4, 4, 16}, {5, 4, 16}, {5, 5, 16}, {6, 5, 16}, {6, 6, 16}, {8, 5, 16}, {8, 6, 16},
            {8, 8, 16}, {10, 5, 16}, {10, 6, 16}, {10, 8, 16}, {10, 10, 16}, {12, 10, 16}, {12, 12, 16},
        };
        return astc[(format - 0x79) % std::size(astc)];
    }
    if (format >= 0x41 && format <= 0x50) {
        const bool eight = format == 0x41 || format == 0x42 || format == 0x45 || format == 0x46 ||
                           format == 0x49 || format == 0x4a;
        return {4, 4, eight ? 8u : 16u};
    }
    if (format >= 1 && format <= 4) return {1, 1, 1};
    if ((format >= 5 && format <= 9) || (format >= 0x0d && format <= 0x10) ||
        (format >= 0x39 && format <= 0x3c) || (format >= 0x95 && format <= 0x98)) return {1, 1, 2};
    if ((format >= 0x0a && format <= 0x0c) || (format >= 0x11 && format <= 0x15) ||
        (format >= 0x25 && format <= 0x28) || (format >= 0x3d && format <= 0x40) ||
        format == 0x37 || format == 0x38 || (format >= 0x99 && format <= 0x9c)) return {1, 1, 4};
    if ((format >= 0x16 && format <= 0x18) || (format >= 0x29 && format <= 0x2d)) return {1, 1, 8};
    if (format >= 0x22 && format <= 0x24) return {1, 1, 12};
    if (format >= 0x2e && format <= 0x30) return {1, 1, 16};
    return {};
}

}  // namespace

HRESULT upload_nvn_tiled_image(NativeReplay& replay, Resource& image, UINT nvn_format, UINT block_height_gobs,
                               std::span<const uint8_t> tiled) {
    if (!image.get() || tiled.empty() ||
        (block_height_gobs && (block_height_gobs > 32 || (block_height_gobs & (block_height_gobs - 1)))))
        return E_INVALIDARG;
    const auto desc = image.get()->GetDesc();
    const auto format = nvn_image_format(nvn_format);
    const auto block = block_shape(nvn_format);
    if (format.resource != desc.Format || !block.bytes || desc.SampleDesc.Count != 1 ||
        (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
         desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D)) return DXGI_ERROR_UNSUPPORTED;

    const UINT layers = desc.DepthOrArraySize;
    const UINT mip_levels = desc.MipLevels;
    std::vector<uint8_t> linear;
    const auto levels = nx::nvn::deswizzle_texture(tiled.data(), tiled.size(), static_cast<UINT>(desc.Width),
        desc.Height, layers, mip_levels, block.width, block.height, block.bytes, linear, block_height_gobs);
    if (levels.size() != mip_levels) return E_INVALIDARG;

    std::vector<TextureData> uploads(image.subresource_count());
    std::vector<std::vector<uint8_t>> decoded(format.decoded_astc ? mip_levels : 0);
    for (UINT mip = 0; mip < mip_levels; ++mip) {
        const auto& level = levels[mip];
        const uint8_t* blocks = linear.data() + level.offset;
        if (format.decoded_astc) {
            const size_t layer_bytes = size_t(level.width) * level.height * 4;
            auto& mip_rgba = decoded[mip];
            mip_rgba.resize(layer_bytes * layers);
            nx::nvn::astc_decode_image(blocks, level.width, level.height, layers, block.width, block.height,
                                       nvn_format >= 0x87, mip_rgba.data());
            for (UINT layer = 0; layer < layers; ++layer)
                uploads[layer * mip_levels + mip].bytes = {mip_rgba.data() + layer * layer_bytes, layer_bytes};
        } else {
            const auto layer_bytes = static_cast<size_t>(level.layer_size);
            for (UINT layer = 0; layer < layers; ++layer)
                uploads[layer * mip_levels + mip].bytes = {blocks + layer * layer_bytes, layer_bytes};
        }
    }
    return replay.upload_texture(image, 0, uploads);
}

}  // namespace catherine::dx12
