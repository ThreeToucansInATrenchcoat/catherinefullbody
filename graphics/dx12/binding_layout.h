#pragma once

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <span>
#include <vector>

namespace catherine::dx12 {

enum class PipelineDomain { Graphics, Compute };

// The SPIR-V-to-DXIL tool records these D3D12 locations after optimization.
// register_space is the original SPIR-V descriptor set for Catherine shaders.
struct ShaderBinding {
    D3D12_DESCRIPTOR_RANGE_TYPE type;
    UINT shader_register;
    UINT register_space;
    UINT count = 1;
};

class BindingLayout {
public:
    static constexpr UINT missing = UINT_MAX;

    // Pass the union of live bindings for a program. Bindings shared by multiple
    // stages should appear once. Ranges within a table consume consecutive
    // descriptors in input order. The caller owns the storage behind the tables.
    HRESULT initialize(ID3D12Device* device, PipelineDomain domain, std::span<const ShaderBinding> bindings);
    ID3D12RootSignature* root_signature() const noexcept { return signature_.Get(); }
    UINT resource_count() const noexcept { return resource_count_; }
    UINT sampler_count() const noexcept { return sampler_count_; }
    PipelineDomain domain() const noexcept { return domain_; }
    UINT offset(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT shader_register, UINT register_space) const noexcept;

    // Bind the descriptor heaps on the list before calling this method. The
    // corresponding heap handle may be zero when its count is zero.
    HRESULT bind(ID3D12GraphicsCommandList* commands, D3D12_GPU_DESCRIPTOR_HANDLE resources,
                 D3D12_GPU_DESCRIPTOR_HANDLE samplers) const noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> signature_;
    std::vector<D3D12_DESCRIPTOR_RANGE> resources_;
    std::vector<D3D12_DESCRIPTOR_RANGE> samplers_;
    UINT resource_count_ = 0;
    UINT sampler_count_ = 0;
    PipelineDomain domain_ = PipelineDomain::Graphics;
};

}  // namespace catherine::dx12
