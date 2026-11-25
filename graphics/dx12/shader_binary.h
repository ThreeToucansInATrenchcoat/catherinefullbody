#pragma once

#include "binding_layout.h"

#include <array>
#include <cstdint>
#include <filesystem>

namespace catherine::dx12 {

enum class ShaderStage : uint32_t { Vertex, Pixel, Compute };

class ShaderBinary {
public:
    // Reads one .dx12shader file from the offline converter. A failed load
    // preserves the previously loaded shader.
    HRESULT load(const std::filesystem::path& path);
    ShaderStage stage() const noexcept { return stage_; }
    D3D12_SHADER_BYTECODE bytecode() const noexcept { return {code_.data(), code_.size()}; }
    std::span<const ShaderBinding> bindings() const noexcept { return bindings_; }
    const std::array<uint8_t, 32>& spirv_sha256() const noexcept { return spirv_sha256_; }

private:
    ShaderStage stage_ = ShaderStage::Vertex;
    std::array<uint8_t, 32> spirv_sha256_{};
    std::vector<char> code_;
    std::vector<ShaderBinding> bindings_;
};

// Creates a root signature from the active bindings of one compute stage,
// or a vertex stage with an optional pixel stage.
HRESULT initialize_program_layout(BindingLayout& layout, ID3D12Device* device,
                                  std::span<const ShaderBinary* const> stages);

}  // namespace catherine::dx12
