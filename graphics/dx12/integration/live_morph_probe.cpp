#include "morph_replay.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

using namespace catherine::dx12;

// The isolated NVN caller checks eligibility and the shader fingerprint.
// It compares results with the CPU implementation before publishing them.
extern "C" long catherine_dx12_morph_probe(const char* hash, uint32_t count, uint32_t stride, float weight,
                                          const uint8_t* deltas, const uint8_t* base, uint8_t* output) noexcept {
    try {
        const wchar_t* directory = _wgetenv(L"CATHERINE_DX12_MORPH_PROBE_DIR");
        if (!directory || !*directory || std::strlen(hash) != 16 ||
            std::strspn(hash, "0123456789abcdef") != 16) return E_INVALIDARG;
        static Renderer renderer;
        if (!renderer.device()) {
            RendererOptions options;
            options.width = options.height = 1;
            options.debug = _wgetenv(L"CATHERINE_DX12_MORPH_DEBUG") != nullptr;
            const HRESULT hr = renderer.initialize(options);
            if (FAILED(hr)) return hr;
        }
        ShaderBinary shader;
        const auto name = std::string(hash) + ".dx12shader";
        const HRESULT hr = shader.load(std::filesystem::path(directory) / name);
        if (FAILED(hr)) return hr;
        const size_t input_size = size_t(count) * 16;
        const std::span<const uint8_t> separate = base ? std::span<const uint8_t>{base, input_size}
                                                       : std::span<const uint8_t>{};
        return execute_morph(renderer, shader, count, stride, weight, {deltas, input_size}, separate,
                             {output, size_t(count) * stride});
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
