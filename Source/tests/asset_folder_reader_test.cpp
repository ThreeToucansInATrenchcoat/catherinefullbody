#include "asset_pak.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    assert(argc == 4);
    const std::wstring mode = argv[1];
    int initialized = mode.starts_with(L"pak") ? asset_pak_initialize_for_test(argv[2]) : asset_folder_initialize_for_test(argv[2]);
    if (mode == L"invalid") { assert(initialized == -1); return 0; }
    if (initialized != 1) { std::cerr << asset_pak_error() << '\n'; return 1; }
    if (mode == L"badmodule") {
        void* data = nullptr; size_t size = 0;
        assert(!asset_pak_read_alloc("main.image", &data, &size));
        assert(data == nullptr && size == 0);
        return 0;
    }
    if (mode.ends_with(L"samples")) {
        std::ifstream samples(std::filesystem::path(argv[3]), std::ios::binary);
        uint64_t total = 0; uint32_t count = 0;
        assert(samples.read(reinterpret_cast<char*>(&total), sizeof total));
        assert(samples.read(reinterpret_cast<char*>(&count), sizeof count));
        assert(asset_pak_romfs_size() == total && count <= 10000);
        for (unsigned i = 0; i < count; ++i) {
            uint64_t offset = 0; uint32_t length = 0;
            assert(samples.read(reinterpret_cast<char*>(&offset), sizeof offset));
            assert(samples.read(reinterpret_cast<char*>(&length), sizeof length) && length <= 1024 * 1024);
            std::vector<char> expected(length), actual(length);
            assert(samples.read(expected.data(), length));
            assert(asset_pak_read_romfs(actual.data(), offset, length));
            if (actual != expected) {
                auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
                std::cerr << "Sample " << i << " at " << offset << ": mismatch at RomFS "
                          << offset + size_t(mismatch.first - actual.begin()) << " actual="
                          << unsigned(static_cast<unsigned char>(*mismatch.first)) << " expected="
                          << unsigned(static_cast<unsigned char>(*mismatch.second)) << '\n';
                return 1;
            }
        }
        std::cout << count << " real RomFS samples matched through the native asset reader.\n";
        return 0;
    }
    std::ifstream input(std::filesystem::path(argv[3]), std::ios::binary);
    std::vector<char> reference((std::istreambuf_iterator<char>(input)), {});
    assert(!reference.empty() && asset_pak_romfs_size() == reference.size());
    uint64_t random = 47;
    for (unsigned i = 0; i < 2000; ++i) {
        random = random * 6364136223846793005ull + 1;
        size_t offset = random % reference.size();
        size_t count = std::min<size_t>((random >> 32) % 400000, reference.size() - offset);
        std::vector<char> actual(count);
        assert(asset_pak_read_romfs(actual.data(), offset, count));
        assert(std::equal(actual.begin(), actual.end(), reference.begin() + offset));
    }
    std::vector<char> entire(reference.size());
    assert(asset_pak_read_romfs(entire.data(), 0, entire.size()) && entire == reference);
    assert(asset_pak_read_romfs(nullptr, reference.size(), 0));
    assert(!asset_pak_read_romfs(nullptr, 0, 1));
    assert(!asset_pak_read_romfs(entire.data(), reference.size(), 1));
    assert(!asset_pak_read_romfs(entire.data(), UINT64_MAX, 1));
    assert(asset_pak_contains("main.image") && asset_pak_contains("spirv/results.tsv"));
    assert(!asset_pak_contains("../main.image"));
    void* data = nullptr; size_t size = 0;
    assert(asset_pak_read_alloc("main.image", &data, &size) && size == 256);
    for (size_t i = 0; i < size; ++i) assert(static_cast<unsigned char*>(data)[i] == i);
    asset_pak_free(data);
    assert(!asset_pak_read("main.image", entire.data(), 255));
    std::cout << "Native asset reads, boundaries, padding, modules and shader lookup passed.\n";
}
