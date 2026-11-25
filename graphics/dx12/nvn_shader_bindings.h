#pragma once

#include "nvn_shaders.h"
#include "shader_binary.h"

namespace catherine::dx12 {

struct NvnBinding {
    ShaderBinding dxil;
    nx::nvn::NvnMeaning guest;
    nx::nvn::ImageShape image;
};

// Match the active DXIL descriptors to the already decoded NVN shader.
// Storage descriptors need reflection enriched by the harness resources.
HRESULT nvn_shader_bindings(const ShaderBinary& shader, const nx::nvn::ShaderReflection& reflection,
                            std::vector<NvnBinding>& output);

}  // namespace catherine::dx12
