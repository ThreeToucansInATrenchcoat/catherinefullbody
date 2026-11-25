#pragma once

#include "native_replay.h"

#include <array>

namespace catherine::dx12 {

struct NvnDrawArguments {
    UINT mode = 4;
    bool indexed = false;
    // DrawArrays uses firstVertex, count, baseInstance, instances.
    // DrawElements uses count, baseVertex, baseInstance, instances.
    std::array<int32_t, 4> words{};
};

struct NvnVertexAttribute {
    UINT location, format, offset, stream;
};

struct NvnVertexStream {
    UINT stride, divisor;
};

struct NvnSampler {
    UINT min_filter = 0, mag_filter = 0, wrap[3]{7, 7, 7}, compare = 8, reduction = 0;
    float min_lod = 0, max_lod = 1000, mip_bias = 0, anisotropy = 1, border[4]{};
};

struct NvnColorTarget {
    bool blend = false;
    UINT source = 2, destination = 1, op = 1;
    UINT source_alpha = 2, destination_alpha = 1, alpha_op = 1;
    UINT8 mask = 15;
};

struct NvnStencilFace {
    UINT compare = 8, fail = 1, depth_fail = 1, pass = 1;
};

struct NvnFixedState {
    UINT polygon = 2, cull = 0, front = 0, logic = 3, alpha_test = 8;
    bool depth_test = false, depth_write = false, stencil_test = false;
    bool depth_clamp = false, alpha_to_coverage = false;
    UINT depth_compare = 8;
    INT bias = 0;
    float bias_clamp = 0, slope_bias = 0;
    UINT8 stencil_read = 0xff, stencil_write = 0xff;
    NvnStencilFace front_stencil, back_stencil;
    std::span<const NvnColorTarget> colors;
};

HRESULT nvn_draw_arguments(const NvnDrawArguments& arguments, GraphicsDraw& draw) noexcept;
D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type(D3D12_PRIMITIVE_TOPOLOGY topology) noexcept;
HRESULT nvn_vertex_input(std::span<const NvnVertexAttribute> attributes, std::span<const NvnVertexStream> streams,
                         std::vector<D3D12_INPUT_ELEMENT_DESC>& input);
HRESULT nvn_fixed_state(const NvnFixedState& state, D3D12_GRAPHICS_PIPELINE_STATE_DESC& output) noexcept;
HRESULT nvn_sampler(const NvnSampler& sampler, bool shadow, D3D12_SAMPLER_DESC& output) noexcept;

}  // namespace catherine::dx12
