#include "shader_binary.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

namespace catherine::dx12 {

namespace {

constexpr size_t header_size = 48;
constexpr size_t binding_size = 16;
constexpr uint32_t max_bindings = 4096;
constexpr uint32_t max_bytecode = 64 * 1024 * 1024;

uint32_t read_u32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

}  // namespace

HRESULT ShaderBinary::load(const std::filesystem::path& path) {
    constexpr HRESULT invalid = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
    const auto length = file.tellg();
    std::array<uint8_t, header_size> header{};
    if (!file.seekg(0) || !file.read(reinterpret_cast<char*>(header.data()), header.size())) return invalid;
    if (std::memcmp(header.data(), "CDX1", 4) != 0) return invalid;
    const uint32_t shader_stage = read_u32(header.data() + 4);
    const uint32_t count = read_u32(header.data() + 8);
    const uint32_t size = read_u32(header.data() + 12);
    if (shader_stage > static_cast<uint32_t>(ShaderStage::Compute) || count > max_bindings ||
        size < 4 || size > max_bytecode || length != std::streamoff(header_size + count * binding_size + size))
        return invalid;

    std::vector<ShaderBinding> shader_bindings;
    shader_bindings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::array<uint8_t, binding_size> data{};
        if (!file.read(reinterpret_cast<char*>(data.data()), data.size())) return invalid;
        const UINT kind = read_u32(data.data());
        const UINT index = read_u32(data.data() + 4);
        const UINT space = read_u32(data.data() + 8);
        const UINT extent = read_u32(data.data() + 12);
        if (kind > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER || !extent || extent == UINT_MAX ||
            UINT64(index) + extent > UINT64(UINT_MAX) + 1) return invalid;
        shader_bindings.push_back({static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(kind), index, space, extent});
    }

    std::vector<char> code(size);
    if (!file.read(code.data(), size) || std::memcmp(code.data(), "DXBC", 4) != 0) return invalid;
    stage_ = static_cast<ShaderStage>(shader_stage);
    std::copy_n(header.data() + 16, spirv_sha256_.size(), spirv_sha256_.begin());
    bindings_ = std::move(shader_bindings);
    code_ = std::move(code);
    return S_OK;
}

HRESULT initialize_program_layout(BindingLayout& layout, ID3D12Device* device,
                                  std::span<const ShaderBinary* const> stages) {
    bool vertex = false, pixel = false, compute = false;
    std::vector<ShaderBinding> union_bindings;
    for (const auto* stage : stages) {
        if (!stage || !stage->bytecode().BytecodeLength) return E_INVALIDARG;
        switch (stage->stage()) {
        case ShaderStage::Vertex:
            if (vertex) return E_INVALIDARG;
            vertex = true;
            break;
        case ShaderStage::Pixel:
            if (pixel) return E_INVALIDARG;
            pixel = true;
            break;
        case ShaderStage::Compute:
            if (compute) return E_INVALIDARG;
            compute = true;
            break;
        }

        for (const auto& binding : stage->bindings()) {
            const auto same = [&](const ShaderBinding& existing) {
                return binding.type == existing.type && binding.shader_register == existing.shader_register &&
                       binding.register_space == existing.register_space && binding.count == existing.count;
            };
            if (std::none_of(union_bindings.begin(), union_bindings.end(), same)) union_bindings.push_back(binding);
        }
    }
    if ((compute && (vertex || pixel)) || (!compute && !vertex)) return E_INVALIDARG;
    return layout.initialize(device, compute ? PipelineDomain::Compute : PipelineDomain::Graphics, union_bindings);
}

}  // namespace catherine::dx12
