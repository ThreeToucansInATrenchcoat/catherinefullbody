#include "nvn_shader_bindings.h"

#include <utility>

namespace catherine::dx12 {

namespace {

using nx::nvn::DescriptorType;

bool compatible(D3D12_DESCRIPTOR_RANGE_TYPE d3d, DescriptorType nvn) {
    switch (d3d) {
    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return nvn == DescriptorType::UniformBuffer;
    case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
        return nvn == DescriptorType::CombinedImageSampler || nvn == DescriptorType::Sampler;
    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        return nvn == DescriptorType::CombinedImageSampler || nvn == DescriptorType::SampledImage ||
               nvn == DescriptorType::UniformTexelBuffer || nvn == DescriptorType::StorageBuffer;
    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
        return nvn == DescriptorType::StorageBuffer || nvn == DescriptorType::StorageImage ||
               nvn == DescriptorType::StorageTexelBuffer;
    default: return false;
    }
}

}  // namespace

HRESULT nvn_shader_bindings(const ShaderBinary& shader, const nx::nvn::ShaderReflection& reflection,
                            std::vector<NvnBinding>& output) {
    const auto stage = shader.stage() == ShaderStage::Vertex ? nx::nvn::ShaderStage::Vertex
                     : shader.stage() == ShaderStage::Pixel ? nx::nvn::ShaderStage::Fragment
                                                            : nx::nvn::ShaderStage::Compute;
    if (!shader.bytecode().BytecodeLength || reflection.stage != stage) return E_INVALIDARG;

    std::vector<NvnBinding> result;
    result.reserve(shader.bindings().size());
    for (const auto& binding : shader.bindings()) {
        const auto* guest = reflection.find(binding.register_space, binding.shader_register);
        if (!guest || guest->count != binding.count || !compatible(binding.type, guest->type)) return E_INVALIDARG;
        if (guest->nvn.source == nx::nvn::NvnSource::Unknown) return E_NOTIMPL;
        result.push_back({binding, guest->nvn, guest->image});
    }
    output = std::move(result);
    return S_OK;
}

}  // namespace catherine::dx12
