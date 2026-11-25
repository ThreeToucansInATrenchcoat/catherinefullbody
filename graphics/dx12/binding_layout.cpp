#include "binding_layout.h"

#include <utility>

namespace catherine::dx12 {

using Microsoft::WRL::ComPtr;

HRESULT BindingLayout::initialize(ID3D12Device* device, PipelineDomain domain, std::span<const ShaderBinding> bindings) {
    if (!device) return E_POINTER;
    if (signature_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

    std::vector<D3D12_DESCRIPTOR_RANGE> resources, samplers;
    UINT resource_count = 0, sampler_count = 0;
    for (const auto& binding : bindings) {
        if (binding.type < D3D12_DESCRIPTOR_RANGE_TYPE_SRV || binding.type > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ||
            !binding.count || binding.count == UINT_MAX) return E_INVALIDARG;

        const auto end = UINT64(binding.shader_register) + binding.count;
        if (end > UINT64(UINT_MAX) + 1) return E_INVALIDARG;
        const bool sampler = binding.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        auto& table = sampler ? samplers : resources;
        auto& count = sampler ? sampler_count : resource_count;
        if (binding.count >= UINT_MAX - count) return E_INVALIDARG;
        for (const auto& range : table) {
            if (range.RangeType == binding.type && range.RegisterSpace == binding.register_space &&
                UINT64(range.BaseShaderRegister) < end &&
                UINT64(binding.shader_register) < UINT64(range.BaseShaderRegister) + range.NumDescriptors)
                return E_INVALIDARG;
        }
        table.push_back({binding.type, binding.count, binding.shader_register, binding.register_space, count});
        count += binding.count;
    }

    D3D12_ROOT_PARAMETER params[2]{};
    UINT param_count = 0;
    for (const auto* table : {&resources, &samplers}) {
        if (table->empty()) continue;
        auto& parameter = params[param_count++];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {static_cast<UINT>(table->size()), table->data()};
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    const auto flags = domain == PipelineDomain::Graphics ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                                                           : D3D12_ROOT_SIGNATURE_FLAG_NONE;
    const D3D12_ROOT_SIGNATURE_DESC desc{param_count, params, 0, nullptr, flags};
    ComPtr<ID3DBlob> serialized;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr);
    if (FAILED(hr)) return hr;
    hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                     IID_PPV_ARGS(&signature_));
    if (FAILED(hr)) return hr;
    resources_ = std::move(resources);
    samplers_ = std::move(samplers);
    resource_count_ = resource_count;
    sampler_count_ = sampler_count;
    domain_ = domain;
    return S_OK;
}

UINT BindingLayout::offset(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT shader_register, UINT register_space) const noexcept {
    const auto& table = type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ? samplers_ : resources_;
    for (const auto& range : table) {
        if (range.RangeType == type && range.RegisterSpace == register_space &&
            shader_register >= range.BaseShaderRegister) {
            const UINT index = shader_register - range.BaseShaderRegister;
            if (index < range.NumDescriptors) return range.OffsetInDescriptorsFromTableStart + index;
        }
    }
    return missing;
}

HRESULT BindingLayout::bind(ID3D12GraphicsCommandList* commands, D3D12_GPU_DESCRIPTOR_HANDLE resources,
                            D3D12_GPU_DESCRIPTOR_HANDLE samplers) const noexcept {
    if (!commands || !signature_ || (resource_count_ && !resources.ptr) || (sampler_count_ && !samplers.ptr))
        return E_INVALIDARG;
    const UINT sampler_index = resource_count_ ? 1 : 0;
    if (domain_ == PipelineDomain::Graphics) {
        commands->SetGraphicsRootSignature(signature_.Get());
        if (resource_count_) commands->SetGraphicsRootDescriptorTable(0, resources);
        if (sampler_count_) commands->SetGraphicsRootDescriptorTable(sampler_index, samplers);
    } else {
        commands->SetComputeRootSignature(signature_.Get());
        if (resource_count_) commands->SetComputeRootDescriptorTable(0, resources);
        if (sampler_count_) commands->SetComputeRootDescriptorTable(sampler_index, samplers);
    }
    return S_OK;
}

}  // namespace catherine::dx12
