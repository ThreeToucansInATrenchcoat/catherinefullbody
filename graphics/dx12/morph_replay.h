#pragma once

#include "renderer.h"
#include "shader_binary.h"

#include <cstdint>
#include <span>

namespace catherine::dx12 {

// Validation path for a Catherine morph kernel with 16-byte deltas and
// 32- or 64-byte destination records. Pass a separate 16-byte base stream
// only for shaders with three UAV bindings. Use an offscreen renderer.
// On return the destination is updated only if readback completed.
HRESULT execute_morph(Renderer& renderer, const ShaderBinary& shader, uint32_t vertex_count,
                      uint32_t destination_stride, float weight,
                      std::span<const uint8_t> deltas, std::span<const uint8_t> separate_base,
                      std::span<uint8_t> destination);

}  // namespace catherine::dx12
