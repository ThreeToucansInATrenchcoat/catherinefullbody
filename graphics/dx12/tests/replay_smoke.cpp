#include "morph_replay.h"
#include "nvn_draw_state.h"
#include "nvn_image.h"
#include "nvn_shader_bindings.h"
#include "test_support.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

namespace {

using namespace catherine::dx12;
using namespace catherine::dx12::test;

constexpr UINT constant_buffer_size = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
using Rgba = std::array<uint8_t, 4>;
using Vector = std::array<float, 4>;

ComPtr<ID3D12Resource> upload(ID3D12Device* device, size_t capacity, const void* data, size_t bytes) {
    expect(bytes <= capacity, "Upload fits in the resource");
    auto result = buffer(device, capacity, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const D3D12_RANGE empty{};
    void* mapped = nullptr;
    check(result->Map(0, &empty, &mapped), "Map upload");
    std::memset(mapped, 0, capacity);
    std::memcpy(mapped, data, bytes);
    result->Unmap(0, nullptr);
    return result;
}

ComPtr<ID3D12DescriptorHeap> cpu_heap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT size) {
    const D3D12_DESCRIPTOR_HEAP_DESC desc{type, size, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ComPtr<ID3D12DescriptorHeap> heap;
    check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)), "Create CPU descriptor heap");
    return heap;
}

D3D12_CPU_DESCRIPTOR_HANDLE slot(ID3D12Device* device, ID3D12DescriptorHeap* heap, UINT index) {
    const auto desc = heap->GetDesc();
    expect(index < desc.NumDescriptors, "Binding maps to a valid descriptor slot");
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(index) * device->GetDescriptorHandleIncrementSize(desc.Type);
    return handle;
}

void verify_rgba(ID3D12Resource* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint, UINT64 bytes,
                 const std::array<Rgba, 2>& first, const std::array<Rgba, 2>& second,
                 const Vector& c1, const Vector& c3) {
    const D3D12_RANGE readable{0, static_cast<SIZE_T>(bytes)};
    void* mapped = nullptr;
    check(readback->Map(0, &readable, &mapped), "Map game shader output");
    std::array<Rgba, 2> actual{};
    for (size_t i = 0; i < actual.size(); ++i) {
        const auto row = (1 + 2 * i) * footprint.Footprint.Height / 4;
        const auto column = footprint.Footprint.Width / 2;
        std::memcpy(actual[i].data(), static_cast<const uint8_t*>(mapped) + footprint.Offset +
                    row * footprint.Footprint.RowPitch + column * sizeof(Rgba), sizeof(Rgba));
    }
    const D3D12_RANGE empty{};
    readback->Unmap(0, &empty);

    // Evaluate the boot pass tone curve independently for the two input rows.
    for (size_t row = 0; row < actual.size(); ++row) {
        const float source_alpha = first[row][3] / 255.f;
        const float blend_alpha = second[row][3] / 255.f;
        const float contribution = source_alpha <= c1[0] ? c1[1] * blend_alpha / source_alpha : 0.f;
        for (size_t channel = 0; channel < 3; ++channel) {
            const float a = first[row][channel] / 255.f;
            const float b = second[row][channel] / 255.f;
            const float linear = std::fmax(0.f, (contribution * a + b) * c3[0] / (c3[1] + 0.001f) + c1[2]);
            const int expected = static_cast<int>(std::lround(255.f * linear / (linear + 10.f)));
            if (std::abs(int(actual[row][channel]) - expected) > 1) {
                std::fprintf(stderr, "Boot pass row %zu, channel %zu, expected %d, read %u\n",
                             row, channel, expected, unsigned(actual[row][channel]));
                throw std::runtime_error("Game shader output mismatch");
            }
        }
        expect(actual[row][3] == 255, "Boot pass alpha");
    }
}

void replay_boot_pass(Renderer& renderer, const std::filesystem::path& directory, UINT samples) {
    // base/dual-trace.log identifies these stages in the first captured draw.
    ShaderBinary vs, ps;
    check(vs.load(directory / "266d0aa411f67ef5.dx12shader"), "Load boot vertex shader");
    check(ps.load(directory / "0edf8bbc47581c06.dx12shader"), "Load boot pixel shader");
    const auto pixel_contents = ps.spirv_sha256();
    expect(ps.load(directory / "manifest.json") == HRESULT_FROM_WIN32(ERROR_BAD_FORMAT) &&
               ps.spirv_sha256() == pixel_contents && ps.stage() == ShaderStage::Pixel,
           "Reject invalid package and retain the previous shader");
    auto* device = renderer.device();
    BindingLayout layout;
    constexpr auto cbv = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    constexpr auto srv = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    constexpr auto sampler = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    constexpr ShaderBinding bad[] = {{srv, 128, 2, 2}, {srv, 129, 2}};
    expect(layout.initialize(device, PipelineDomain::Graphics, bad) == E_INVALIDARG, "Reject overlapping ranges");
    const ShaderBinary* stages[] = {&vs, &ps};
    check(initialize_program_layout(layout, device, stages), "Create boot pass layout from compiled shaders");
    expect(layout.resource_count() == 4 && layout.sampler_count() == 2 &&
               layout.offset(srv, 129, 2) != BindingLayout::missing && layout.offset(srv, 129, 0) == BindingLayout::missing,
           "Register space and range offsets");
    auto resource_heap = cpu_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, layout.resource_count());
    auto sampler_heap = cpu_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, layout.sampler_count());

    const Vector c1{0.9f, 0.5f, -0.1f, 0}, c3{4.f, 0.32f, 0.3f, 0.5f};
    const auto constants1 = upload(device, constant_buffer_size, c1.data(), sizeof(Vector));
    const auto constants3 = upload(device, constant_buffer_size, c3.data(), sizeof(Vector));
    const D3D12_CONSTANT_BUFFER_VIEW_DESC view1{constants1->GetGPUVirtualAddress(), constant_buffer_size};
    const D3D12_CONSTANT_BUFFER_VIEW_DESC view3{constants3->GetGPUVirtualAddress(), constant_buffer_size};
    device->CreateConstantBufferView(&view1, slot(device, resource_heap.Get(), layout.offset(cbv, 20, 0)));
    device->CreateConstantBufferView(&view3, slot(device, resource_heap.Get(), layout.offset(cbv, 22, 0)));

    const std::array<Rgba, 2> first{{{30, 80, 110, 128}, {160, 20, 50, 240}}};
    const std::array<Rgba, 2> second{{{15, 110, 35, 200}, {140, 45, 210, 100}}};
    Resource image_resource1, image_resource2;
    NvnImageDesc guest_image;
    guest_image.height = 2;
    check(create_nvn_image(device, guest_image, image_resource1), "Create first NVN image");
    check(create_nvn_image(device, guest_image, image_resource2), "Create second NVN image");
    D3D12_SHADER_RESOURCE_VIEW_DESC image_view{};
    image_view.Format = nvn_image_format(guest_image.format).sampled;
    image_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    image_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    image_view.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(image_resource1.get(), &image_view,
                                     slot(device, resource_heap.Get(), layout.offset(srv, 128, 2)));
    device->CreateShaderResourceView(image_resource2.get(), &image_view,
                                     slot(device, resource_heap.Get(), layout.offset(srv, 129, 2)));
    D3D12_SAMPLER_DESC sampler_desc{};
    check(nvn_sampler({}, false, sampler_desc), "Decode the NVN point sampler");
    for (UINT binding : {128u, 129u})
        device->CreateSampler(&sampler_desc, slot(device, sampler_heap.Get(), layout.offset(sampler, binding, 2)));

    struct Vertex { Vector position, uv; };
    // Negative NVN clip Z exercises the translation to D3D12's 0..W range.
    const Vertex vertices[] = {
        {}, {{-1, 1, -0.5f, 1}, {0, 0, 0, 0}}, {{1, 1, -0.5f, 1}, {1, 0, 0, 0}},
        {{-1, -1, -0.5f, 1}, {0, 1, 0, 0}}, {{1, -1, -0.5f, 1}, {1, 1, 0, 0}},
    };
    const auto stream = upload(device, sizeof(vertices), vertices, sizeof(vertices));
    const uint16_t index_data[] = {0, 0, 1, 2, 3};
    const auto index_stream = upload(device, sizeof(index_data), index_data, sizeof(index_data));
    const NvnVertexAttribute attributes[] = {{0, 0x2e, 0, 0}, {1, 0x2e, 16, 0}};
    const NvnVertexStream streams[] = {{sizeof(Vertex), 0}};
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    check(nvn_vertex_input(attributes, streams, elements), "Decode the NVN vertex layout");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = layout.root_signature();
    pso.VS = vs.bytecode();
    pso.PS = ps.bytecode();
    pso.InputLayout = {elements.data(), static_cast<UINT>(elements.size())};
    pso.SampleMask = UINT_MAX;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = Renderer::color_format;
    pso.SampleDesc.Count = samples;
    const NvnColorTarget color_state[] = {{}};
    NvnFixedState fixed;
    fixed.colors = color_state;
    check(nvn_fixed_state(fixed, pso), "Decode NVN pipeline state");
    const D3D12_INDEX_BUFFER_VIEW index_buffer{index_stream->GetGPUVirtualAddress(), sizeof(index_data), DXGI_FORMAT_R16_UINT};
    GraphicsDraw draw;
    draw.indices = &index_buffer;
    check(nvn_draw_arguments({5, true, {4, 1, 0, 1}}, draw), "Decode NVN indexed draw arguments");
    pso.PrimitiveTopologyType = topology_type(draw.topology);
    ComPtr<ID3D12PipelineState> pipeline;
    check(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)), "Create game graphics pipeline");

    Resource constants_resource1, constants_resource3;
    Resource vertex_resource, index_resource, target, multisample;
    check(constants_resource1.attach(constants1.Get(), D3D12_RESOURCE_STATE_GENERIC_READ), "Track the first CBV");
    check(constants_resource3.attach(constants3.Get(), D3D12_RESOURCE_STATE_GENERIC_READ), "Track the second CBV");
    check(vertex_resource.attach(stream.Get(), D3D12_RESOURCE_STATE_GENERIC_READ), "Track the vertex stream");
    check(index_resource.attach(index_stream.Get(), D3D12_RESOURCE_STATE_GENERIC_READ), "Track the index stream");
    const ResourceAccess accesses[] = {
        {&image_resource1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {&image_resource2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {&constants_resource1, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER},
        {&constants_resource3, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER},
        {&vertex_resource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER},
        {&index_resource, D3D12_RESOURCE_STATE_INDEX_BUFFER},
    };
    auto attachment_heap = cpu_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    const DescriptorWrite descriptors[] = {
        {cbv, 20, 0, slot(device, resource_heap.Get(), layout.offset(cbv, 20, 0))},
        {cbv, 22, 0, slot(device, resource_heap.Get(), layout.offset(cbv, 22, 0))},
        {srv, 128, 2, slot(device, resource_heap.Get(), layout.offset(srv, 128, 2))},
        {srv, 129, 2, slot(device, resource_heap.Get(), layout.offset(srv, 129, 2))},
        {sampler, 128, 2, slot(device, sampler_heap.Get(), layout.offset(sampler, 128, 2))},
        {sampler, 129, 2, slot(device, sampler_heap.Get(), layout.offset(sampler, 129, 2))},
    };

    NativeReplay replay(renderer);
    Frame frame;
    check(replay.begin(frame), "Begin cancellable replay");
    check(replay.access({accesses, 1}), "Record a texture transition");
    check(replay.cancel(), "Cancel and roll back the texture state");
    check(replay.begin(frame), "Begin game shader frame");
    const auto color_desc = frame.color->GetDesc();
    D3D12_CLEAR_VALUE black{};
    black.Format = Renderer::color_format;
    black.Color[3] = 1.f;
    NvnImageDesc guest_output;
    guest_output.width = frame.width;
    guest_output.height = frame.height;
    guest_output.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    check(create_nvn_image(device, guest_output, target, &black), "Create the NVN render target");
    Resource* draw_target = &target;
    if (samples > 1) {
        guest_output.samples = samples;
        guest_output.target = 5;
        check(create_nvn_image(device, guest_output, multisample, &black), "Create the NVN MSAA render target");
        draw_target = &multisample;
    }
    const Attachment output{draw_target, 0, attachment_heap->GetCPUDescriptorHandleForHeapStart()};
    D3D12_RENDER_TARGET_VIEW_DESC target_view{};
    target_view.Format = nvn_image_format(guest_output.format).color;
    target_view.ViewDimension = samples > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(draw_target->get(), &target_view, output.descriptor);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device->GetCopyableFootprints(&color_desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto readback = buffer(device, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<uint8_t, 512> tiled{};
    std::memcpy(tiled.data(), first[0].data(), sizeof(Rgba));
    std::memcpy(tiled.data() + 16, first[1].data(), sizeof(Rgba));
    std::array<uint8_t, 11> padded{};
    std::memcpy(padded.data(), second[0].data(), sizeof(Rgba));
    std::memcpy(padded.data() + 7, second[1].data(), sizeof(Rgba));
    const TextureData second_upload{padded, 7};
    const TextureData invalid_upload{padded, 3};
    expect(replay.upload_texture(image_resource2, 0, {&invalid_upload, 1}) == E_INVALIDARG, "Reject short source rows");
    expect(upload_nvn_tiled_image(replay, image_resource1, guest_image.format, 1, {tiled.data(), 16}) == E_INVALIDARG,
           "Reject an incomplete guest texture block");
    check(upload_nvn_tiled_image(replay, image_resource1, guest_image.format, 1, tiled), "Untile and stage NVN texture");
    check(replay.upload_texture(image_resource2, 0, {&second_upload, 1}), "Stage padded guest texture");
    check(replay.clear_color(output, black.Color), "Clear game render target");
    const D3D12_VERTEX_BUFFER_VIEW vertex_buffer{stream->GetGPUVirtualAddress(), sizeof(vertices), sizeof(Vertex)};
    draw.bindings = {&layout, pipeline.Get(), {descriptors, 5}, accesses};
    draw.colors = {&output, 1};
    draw.viewport = {0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height), 0, 1};
    draw.scissor = {0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    draw.vertices = {&vertex_buffer, 1};
    draw.first = 1;
    expect(replay.draw(draw) == E_INVALIDARG, "Reject a missing sampler before recording the draw");
    draw.bindings.descriptors = descriptors;
    check(replay.draw(draw), "Replay game draw with native descriptors and index offsets");
    if (samples > 1)
        check(replay.resolve_color(target, 0, multisample, 0, Renderer::color_format), "Resolve native MSAA");
    check(replay.present(target), "Blit the native game target to the frame output");
    record_readback(frame, readback.Get(), footprint);
    check(replay.submit(), "Submit game draw");
    check(renderer.wait_idle(), "Wait for game draw");
    verify_rgba(readback.Get(), footprint, bytes, first, second, c1, c3);
}

template<size_t words>
void replay_morph(Renderer& renderer, const std::filesystem::path& path, bool separate) {
    ShaderBinary cs;
    check(cs.load(path), "Load morph compute shader");
    constexpr uint32_t active = 69, allocated = 75;
    constexpr float weight = -0.5f;
    std::array<Vector, allocated> deltas{}, separate_base{};
    std::array<std::array<float, words>, allocated> base{};
    for (size_t vertex = 0; vertex < allocated; ++vertex) {
        deltas[vertex] = {float(vertex) / 4, 3, -4, 5};
        separate_base[vertex] = {float(vertex) / 2, -3, 9, 8};
        for (size_t lane = 0; lane < words; ++lane)
            base[vertex][lane] = float(vertex + lane) / 4 - 9;
    }
    auto result = base;
    const std::span<const uint8_t> input{reinterpret_cast<const uint8_t*>(deltas.data()), sizeof(deltas)};
    const std::span<const uint8_t> extra = separate
        ? std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(separate_base.data()), sizeof(separate_base)}
        : std::span<const uint8_t>{};
    const std::span<uint8_t> output{reinterpret_cast<uint8_t*>(result.data()), sizeof(result)};
    check(execute_morph(renderer, cs, active, sizeof(result[0]), weight, input, extra, output), "Execute game morph");

    auto expected = base;
    for (size_t vertex = 0; vertex < active; ++vertex)
        for (size_t channel = 0; channel < 3; ++channel)
            expected[vertex][channel] = (separate ? separate_base[vertex][channel] : base[vertex][channel]) +
                                        weight * deltas[vertex][channel];
    expect(std::memcmp(result.data(), expected.data(), sizeof(expected)) == 0,
           "Game morph output and untouched lanes must match exactly");
}

nx::nvn::ShaderReflection reflect_local_shader(const std::filesystem::path& directory, const std::string& hash) {
    std::ifstream file(directory / (hash + ".spv"), std::ios::binary | std::ios::ate);
    expect(file.is_open(), "Open original NVN shader");
    const auto bytes = static_cast<std::streamoff>(file.tellg());
    expect(bytes >= 20 && bytes % sizeof(uint32_t) == 0, "SPIR-V word count");
    std::vector<uint32_t> words(static_cast<size_t>(bytes) / sizeof(uint32_t));
    file.seekg(0);
    expect(bool(file.read(reinterpret_cast<char*>(words.data()), bytes)), "Read original NVN shader");
    nx::nvn::ShaderReflection reflection;
    expect(nx::nvn::reflect_spirv(words.data(), words.size(), reflection), "Decode NVN shader resources");
    return reflection;
}

void verify_guest_bindings(const std::filesystem::path& cache, const std::filesystem::path& spirv_dir) {
    using nx::nvn::NvnSource;
    const std::string pixel_hash = "0edf8bbc47581c06";
    ShaderBinary shader;
    check(shader.load(cache / (pixel_hash + ".dx12shader")), "Load translated boot pixel shader");
    const auto pixel = reflect_local_shader(spirv_dir, pixel_hash);
    std::vector<NvnBinding> bindings;
    check(nvn_shader_bindings(shader, pixel, bindings), "Resolve NVN pixel shader binding meanings");
    expect(bindings.size() == 6, "Optimized shader has separate SRV and sampler bindings");
    for (const auto& binding : bindings) {
        if (binding.dxil.type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
            const bool material = binding.dxil.shader_register == 22;
            expect(binding.guest.source == (material ? NvnSource::UniformBlock : NvnSource::ConstantData),
                   "Resolve distinct constant buffer sources");
            if (material) expect(binding.guest.index == 0, "Resolve NVN uniform slot");
        } else {
            expect(binding.guest.source == NvnSource::SamplerBinding &&
                       binding.guest.index == static_cast<int>(binding.dxil.shader_register) - 128,
                   "Resolve SRV and sampler to the same guest texture binding");
        }
    }

    const std::string morph_hash = "69a4a203a4c2fb60";
    check(shader.load(cache / (morph_hash + ".dx12shader")), "Load translated separate-base morph shader");
    auto compute = reflect_local_shader(spirv_dir, morph_hash);
    expect(nvn_shader_bindings(shader, compute, bindings) == E_NOTIMPL,
           "Unresolved guest storage metadata must not become an arbitrary buffer");
    std::ifstream table(spirv_dir / "results.tsv", std::ios::binary);
    expect(table.is_open(), "Open NVN shader harness resource table");
    const std::string records{std::istreambuf_iterator<char>(table), {}};
    std::unordered_map<std::string, std::string> metadata;
    nx::nvn::parse_results_tsv(records, metadata);
    std::vector<nx::nvn::HarnessResource> sources;
    expect(nx::nvn::parse_harness_resources(metadata.at(morph_hash), sources) && nx::nvn::apply_harness(compute, sources),
           "Resolve morph shader's storage offsets");
    check(nvn_shader_bindings(shader, compute, bindings), "Map DXIL UAVs to NVN storage slots");
    constexpr int slots[] = {1, 0, 2};
    for (const auto& binding : bindings) {
        if (binding.dxil.type != D3D12_DESCRIPTOR_RANGE_TYPE_UAV) continue;
        expect(binding.guest.source == NvnSource::StorageBuffer && binding.dxil.shader_register < std::size(slots) &&
                   binding.guest.index == slots[binding.dxil.shader_register],
               "Preserve swapped guest inputs for the separate-base morph kernel");
    }
    std::puts("Live DXIL descriptor ranges matched the original NVN shader metadata.");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    bool warp = false;
    std::filesystem::path graphics_metadata;
    for (int arg = 2; arg < argc; ++arg) {
        const std::wstring_view option = argv[arg];
        if (option == L"--warp") warp = true;
        else if (option == L"--spirv" && arg + 1 < argc) graphics_metadata = argv[++arg];
        else return 2;
    }
    if (argc < 2) {
        std::fputs("Usage: catherine_dx12_replay_smoke <translated shader directory> [--warp] [--spirv <dir>]\n", stderr);
        return 2;
    }
    try {
        Renderer renderer;
        RendererOptions options;
        options.width = 63;
        options.height = 41;
        options.warp = warp;
        options.debug = true;
        check(renderer.initialize(options), "Initialize D3D12 replay device");
        ComPtr<ID3D12InfoQueue> info;
        check(renderer.device()->QueryInterface(IID_PPV_ARGS(&info)), "Query D3D12 info queue");
        const std::filesystem::path directory = argv[1];
        if (!graphics_metadata.empty()) verify_guest_bindings(directory, graphics_metadata);
        replay_boot_pass(renderer, directory, 1);
        for (UINT samples : {4u, 8u}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{Renderer::color_format, samples,
                D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0};
            check(renderer.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                                           &quality, sizeof(quality)), "Query MSAA support");
            if (quality.NumQualityLevels) {
                replay_boot_pass(renderer, directory, samples);
                std::printf("Native %ux MSAA color resolve passed pixel readback.\n", samples);
            }
        }
        replay_morph<8>(renderer, directory / "9ae85fe23552c4e8.dx12shader", false);
        replay_morph<16>(renderer, directory / "4843060eb5a152ba.dx12shader", false);
        replay_morph<8>(renderer, directory / "69a4a203a4c2fb60.dx12shader", true);
        replay_morph<16>(renderer, directory / "70053e80370597a3.dx12shader", true);
        verify_debug_messages(info.Get());
        expect(GetModuleHandleW(L"vulkan-1.dll") == nullptr, "Native replay must not load the Vulkan loader");
        std::printf("%s | game boot-pass pixels and four morph kernels verified\n", options.warp ? "WARP" : "Hardware");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "NVN shader replay failed: %s\n", error.what());
        return 1;
    }
}
