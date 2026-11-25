#include "nvn_draw_state.h"
#include "nvn_image.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace catherine::dx12 {

namespace {

D3D12_COMPARISON_FUNC compare(UINT value) {
    return value >= 1 && value <= 8 ? static_cast<D3D12_COMPARISON_FUNC>(value) : D3D12_COMPARISON_FUNC_ALWAYS;
}

D3D12_STENCIL_OP stencil(UINT value) {
    return value >= 1 && value <= 8 ? static_cast<D3D12_STENCIL_OP>(value) : D3D12_STENCIL_OP_KEEP;
}

D3D12_DEPTH_STENCILOP_DESC stencil_face(const NvnStencilFace& face) {
    return {stencil(face.fail), stencil(face.depth_fail), stencil(face.pass), compare(face.compare)};
}

D3D12_BLEND blend(UINT value, bool alpha) {
    switch (value) {
    case 1: return D3D12_BLEND_ZERO;
    case 2: return D3D12_BLEND_ONE;
    case 3: return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
    case 4: return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    case 5: return D3D12_BLEND_SRC_ALPHA;
    case 6: return D3D12_BLEND_INV_SRC_ALPHA;
    case 7: return D3D12_BLEND_DEST_ALPHA;
    case 8: return D3D12_BLEND_INV_DEST_ALPHA;
    case 9: return alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
    case 10: return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
    case 11: return alpha ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA_SAT;
    case 0x10: return alpha ? D3D12_BLEND_SRC1_ALPHA : D3D12_BLEND_SRC1_COLOR;
    case 0x11: return alpha ? D3D12_BLEND_INV_SRC1_ALPHA : D3D12_BLEND_INV_SRC1_COLOR;
    case 0x12: return D3D12_BLEND_SRC1_ALPHA;
    case 0x13: return D3D12_BLEND_INV_SRC1_ALPHA;
    case 0x61: return D3D12_BLEND_BLEND_FACTOR;
    case 0x62: return D3D12_BLEND_INV_BLEND_FACTOR;
    case 0x63: return alpha ? D3D12_BLEND_BLEND_FACTOR : D3D12_BLEND_ALPHA_FACTOR;
    case 0x64: return alpha ? D3D12_BLEND_INV_BLEND_FACTOR : D3D12_BLEND_INV_ALPHA_FACTOR;
    default: return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP blend_op(UINT value) {
    return value >= 1 && value <= 5 ? static_cast<D3D12_BLEND_OP>(value) : D3D12_BLEND_OP_ADD;
}

constexpr D3D12_LOGIC_OP logic_ops[] = {
    D3D12_LOGIC_OP_CLEAR, D3D12_LOGIC_OP_AND, D3D12_LOGIC_OP_AND_REVERSE, D3D12_LOGIC_OP_COPY,
    D3D12_LOGIC_OP_AND_INVERTED, D3D12_LOGIC_OP_NOOP, D3D12_LOGIC_OP_XOR, D3D12_LOGIC_OP_OR,
    D3D12_LOGIC_OP_NOR, D3D12_LOGIC_OP_EQUIV, D3D12_LOGIC_OP_INVERT, D3D12_LOGIC_OP_OR_REVERSE,
    D3D12_LOGIC_OP_COPY_INVERTED, D3D12_LOGIC_OP_OR_INVERTED, D3D12_LOGIC_OP_NAND, D3D12_LOGIC_OP_SET,
};

constexpr D3D12_TEXTURE_ADDRESS_MODE address_modes[] = {
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
};

}  // namespace

HRESULT nvn_draw_arguments(const NvnDrawArguments& args, GraphicsDraw& draw) noexcept {
    D3D12_PRIMITIVE_TOPOLOGY primitive;
    switch (args.mode) {
    case 0: primitive = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
    case 1: primitive = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
    case 3: primitive = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
    case 4: primitive = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    case 5: primitive = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    default: return E_NOTIMPL;  // Loops, fans and quads need a converted index stream.
    }
    const auto& words = args.words;
    if (words[0] < 0 || (!args.indexed && words[1] < 0) || words[2] < 0 || words[3] < 0) return E_INVALIDARG;
    if (args.indexed != (draw.indices != nullptr)) return E_INVALIDARG;
    draw.topology = primitive;
    draw.count = static_cast<UINT>(words[args.indexed ? 0 : 1]);
    draw.first = args.indexed ? 0 : static_cast<UINT>(words[0]);
    draw.base_vertex = args.indexed ? words[1] : 0;
    draw.first_instance = static_cast<UINT>(words[2]);
    draw.instances = static_cast<UINT>(words[3]);
    return S_OK;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type(D3D12_PRIMITIVE_TOPOLOGY topology) noexcept {
    switch (topology) {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST: case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST: case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    }
}

HRESULT nvn_vertex_input(std::span<const NvnVertexAttribute> attributes, std::span<const NvnVertexStream> streams,
                         std::vector<D3D12_INPUT_ELEMENT_DESC>& input) {
    constexpr UINT max = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
    std::array<bool, max> seen{};
    std::vector<D3D12_INPUT_ELEMENT_DESC> result;
    for (const auto& attr : attributes) {
        if (attr.location >= max || seen[attr.location] || attr.stream >= streams.size() ||
            attr.stream >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return E_INVALIDARG;
        const auto format = nvn_vertex_format(attr.format);
        if (format == DXGI_FORMAT_UNKNOWN) return DXGI_ERROR_UNSUPPORTED;
        const auto& stream = streams[attr.stream];
        if (!stream.stride || attr.offset >= stream.stride) return E_INVALIDARG;
        seen[attr.location] = true;
        result.push_back({"TEXCOORD", attr.location, format, attr.stream, attr.offset,
                          stream.divisor ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                         : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                          stream.divisor});
    }
    input = std::move(result);
    return S_OK;
}

HRESULT nvn_fixed_state(const NvnFixedState& state, D3D12_GRAPHICS_PIPELINE_STATE_DESC& output) noexcept {
    if (state.colors.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT || state.front > 1 ||
        state.logic >= std::size(logic_ops) || !std::isfinite(state.bias_clamp) || !std::isfinite(state.slope_bias))
        return E_INVALIDARG;
    if (state.polygon != 1 && state.polygon != 2) return E_NOTIMPL;
    if (state.cull > 2 || (state.alpha_test && state.alpha_test != 8)) return E_NOTIMPL;

    const bool logic = state.logic != 3;
    if (logic && std::any_of(state.colors.begin(), state.colors.end(), [](const auto& rt) { return rt.blend; }))
        return E_NOTIMPL;
    D3D12_BLEND_DESC blend_state{};
    blend_state.AlphaToCoverageEnable = state.alpha_to_coverage;
    blend_state.IndependentBlendEnable = !logic && state.colors.size() > 1;
    for (size_t i = 0; i < state.colors.size(); ++i) {
        const auto& source = state.colors[i];
        auto& target = blend_state.RenderTarget[i];
        target = {source.blend, logic, blend(source.source, false), blend(source.destination, false),
                  blend_op(source.op), blend(source.source_alpha, true), blend(source.destination_alpha, true),
                  blend_op(source.alpha_op), logic_ops[state.logic], static_cast<UINT8>(source.mask & 15)};
    }
    // The legacy D3D12 descriptor cannot mix independent masks and logic ops.
    if (logic && state.colors.size() > 1) {
        for (const auto& target : state.colors)
            if (target.mask != state.colors[0].mask) return E_NOTIMPL;
    }

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = state.polygon == 1 ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    raster.CullMode = static_cast<D3D12_CULL_MODE>(state.cull + D3D12_CULL_MODE_NONE);
    raster.FrontCounterClockwise = state.front == 1;
    raster.DepthClipEnable = !state.depth_clamp;
    raster.DepthBias = state.bias;
    raster.DepthBiasClamp = state.bias_clamp;
    raster.SlopeScaledDepthBias = state.slope_bias;
    raster.MultisampleEnable = output.SampleDesc.Count > 1;

    D3D12_DEPTH_STENCIL_DESC depth{};
    const bool has_depth = output.DSVFormat != DXGI_FORMAT_UNKNOWN;
    depth.DepthEnable = has_depth && state.depth_test;
    depth.DepthWriteMask = has_depth && state.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = compare(state.depth_compare);
    depth.StencilEnable = has_depth && state.stencil_test;
    depth.StencilReadMask = state.stencil_read;
    depth.StencilWriteMask = state.stencil_write;
    depth.FrontFace = stencil_face(state.front_stencil);
    depth.BackFace = stencil_face(state.back_stencil);

    output.BlendState = blend_state;
    output.RasterizerState = raster;
    output.DepthStencilState = depth;
    return S_OK;
}

HRESULT nvn_sampler(const NvnSampler& s, bool shadow, D3D12_SAMPLER_DESC& output) noexcept {
    if (s.min_filter > 5 || s.mag_filter > 1 || s.reduction > 2 ||
        !std::isfinite(s.min_lod) || !std::isfinite(s.max_lod) || !std::isfinite(s.mip_bias) ||
        !std::isfinite(s.anisotropy) || s.anisotropy < 1 ||
        std::any_of(std::begin(s.wrap), std::end(s.wrap), [](UINT wrap) { return wrap >= std::size(address_modes); }))
        return E_INVALIDARG;
    if (shadow && s.reduction) return E_NOTIMPL;
    const auto min_filter = s.min_filter & 1 ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const auto mag_filter = s.mag_filter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const auto mip_filter = s.min_filter >= 4 ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const auto reduction = shadow ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON
                         : s.reduction == 1 ? D3D12_FILTER_REDUCTION_TYPE_MINIMUM
                         : s.reduction == 2 ? D3D12_FILTER_REDUCTION_TYPE_MAXIMUM
                                            : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    D3D12_SAMPLER_DESC result{};
    result.Filter = s.anisotropy > 1 ? D3D12_ENCODE_ANISOTROPIC_FILTER(reduction)
                                    : D3D12_ENCODE_BASIC_FILTER(min_filter, mag_filter, mip_filter, reduction);
    result.AddressU = address_modes[s.wrap[0]];
    result.AddressV = address_modes[s.wrap[1]];
    result.AddressW = address_modes[s.wrap[2]];
    result.MipLODBias = std::clamp(s.mip_bias, D3D12_MIP_LOD_BIAS_MIN, D3D12_MIP_LOD_BIAS_MAX);
    result.MaxAnisotropy = static_cast<UINT>(std::clamp(s.anisotropy, 1.f, 16.f));
    result.ComparisonFunc = shadow ? compare(s.compare) : D3D12_COMPARISON_FUNC_ALWAYS;
    std::copy(std::begin(s.border), std::end(s.border), result.BorderColor);
    result.MinLOD = s.min_filter > 1 ? (std::max)(0.f, s.min_lod) : 0.f;
    result.MaxLOD = s.min_filter > 1 ? (std::max)(result.MinLOD, s.max_lod) : 0.f;
    output = result;
    return S_OK;
}

}  // namespace catherine::dx12
