// Included inside asset_pak.cpp's private namespace. Folder mode uses the same
// asset API as ZIP mode, while RomFS ranges are read from actual extracted files.
bool folder_reparse(const std::filesystem::path& path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path));
#endif
}

std::filesystem::path folder_path(const State& s, const std::string& name) {
    if (!valid_name(name)) fail("Invalid extracted asset path");
    auto path = s.folder;
    if (folder_reparse(path)) fail("The assets directory must not be a symbolic link or junction");
    for (const auto& part : std::filesystem::path(name)) {
        path /= part;
        if (folder_reparse(path)) fail("Extracted asset path contains a symbolic link or junction");
    }
    return path;
}

void folder_read_file(const std::filesystem::path& path, uint64_t offset, void* output, size_t count) {
    std::ifstream input(path, std::ios::binary);
    if (!input || offset > uint64_t(INT64_MAX)) fail("Cannot open extracted asset: " + path.filename().string());
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input || (count && !input.read(static_cast<char*>(output), static_cast<std::streamsize>(count))))
        fail("Extracted asset is truncated or unreadable: " + path.filename().string());
}

void folder_extract(State& s, const Entry& entry, void* output, size_t count) {
    auto path = folder_path(s, entry.path);
    if (std::filesystem::file_size(path) != entry.size) fail("Extracted asset size changed: " + entry.path);
    folder_read_file(path, 0, output, count);
    if (uint32_t(mz_crc32(0, static_cast<const unsigned char*>(output), count)) != entry.crc)
        fail("Extracted module/shader checksum mismatch: " + entry.path);
}

void folder_read_romfs(State& s, uint64_t offset, void* output, size_t count) {
    auto* bytes = static_cast<unsigned char*>(output);
    std::memset(bytes, 0, count); // RomFS alignment gaps are zero-filled.
    auto it = std::upper_bound(s.ranges.begin(), s.ranges.end(), offset,
        [](uint64_t position, const FolderRange& range) { return position < range.offset; });
    if (it != s.ranges.begin()) --it;
    const uint64_t end = offset + count;
    for (; it != s.ranges.end() && it->offset < end; ++it) {
        const uint64_t first = std::max(offset, it->offset), last = std::min(end, it->offset + it->size);
        if (last <= first) continue;
        const size_t take = static_cast<size_t>(last - first);
        if (it->path.empty()) std::memcpy(bytes + (first - offset), it->metadata.data() + (first - it->offset), take);
        else folder_read_file(it->path, first - it->offset, bytes + (first - offset), take);
    }
}

void open_folder(State& s, const std::filesystem::path& root) {
    s.folder = root;
    auto layout = folder_path(s, ".catherine-layout.bin");
    if (!std::filesystem::exists(layout) || !std::filesystem::exists(folder_path(s, ".catherine-folder.json"))) {
        s.mode = 0;
        return;
    }
    uint64_t length = std::filesystem::file_size(layout);
    if (length < 24 || length > 16 * 1024 * 1024) fail("Invalid extracted asset layout size");
    std::vector<unsigned char> data(static_cast<size_t>(length));
    folder_read_file(layout, 0, data.data(), data.size());
    size_t cursor = 0;
    auto take = [&](size_t n) {
        if (n > data.size() - cursor) fail("Truncated extracted asset layout");
        const unsigned char* p = data.data() + cursor; cursor += n; return p;
    };
    auto number = [&](unsigned n) {
        const auto* p = take(n); uint64_t value = 0;
        for (unsigned i = 0; i < n; ++i) value |= uint64_t(p[i]) << (i * 8);
        return value;
    };
    auto name = [&]() {
        size_t n = static_cast<size_t>(number(2));
        const auto* p = take(n);
        return std::string(reinterpret_cast<const char*>(p), n);
    };
    if (std::memcmp(take(8), "CATHDIR1", 8)) fail("Unknown extracted asset layout version");
    s.romfs_size = number(8);
    if (!s.romfs_size || s.romfs_size > (1ull << 40)) fail("Invalid extracted RomFS size");
    uint64_t files = number(4);
    if (!files || files > 100000) fail("Invalid extracted runtime file count");
    for (uint64_t i = 0; i < files; ++i) {
        auto relative = name(); uint64_t size = number(8); uint32_t crc = uint32_t(number(4));
        auto path = folder_path(s, relative);
        if (!size || size > max_entry || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != size ||
            !s.entries.emplace(relative, Entry{0, size, crc, 0, relative}).second)
            fail("Invalid extracted runtime asset: " + relative);
    }
    uint64_t ranges = number(4), previous_end = 0;
    if (!ranges || ranges > 100000) fail("Invalid extracted RomFS range count");
    for (uint64_t i = 0; i < ranges; ++i) {
        FolderRange range;
        range.offset = number(8); range.size = number(8);
        auto relative = name();
        if (range.offset < previous_end || range.offset > s.romfs_size || range.size > s.romfs_size - range.offset)
            fail("Overlapping or out-of-bounds extracted RomFS ranges");
        previous_end = range.offset + range.size;
        if (relative.empty()) {
            if (range.size > 16 * 1024 * 1024) fail("Oversized extracted RomFS metadata");
            const auto* bytes = take(static_cast<size_t>(range.size));
            range.metadata.assign(bytes, bytes + range.size);
        } else {
            if (!relative.starts_with("romfs/")) fail("Invalid extracted RomFS file path");
            range.path = folder_path(s, relative);
            if (!std::filesystem::is_regular_file(range.path) || std::filesystem::file_size(range.path) != range.size)
                fail("Missing or truncated extracted RomFS file: " + relative);
        }
        s.ranges.push_back(std::move(range));
    }
    if (cursor != data.size()) fail("Unexpected data after extracted asset layout");
    for (const char* required : {"main.image", "subsdk0.image", "subsdk1.image", "sdk.image", "spirv/results.tsv"})
        if (!s.entries.contains(required)) fail(std::string("Missing extracted runtime asset: ") + required);
    std::fprintf(stderr, "assets: loading directly from extracted folder (%zu RomFS ranges)\n", s.ranges.size());
    s.mode = 1;
}
