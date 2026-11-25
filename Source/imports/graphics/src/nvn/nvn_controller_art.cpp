// Exact texture identities select original, vector-drawn PC controller help.
// All state is protected by the renderer's GPU mutex, like texture upload state.
#include "nvn_controller_art.h"
#include "controller_art_raster.h"
#include "controller_asset_images.h"
#include "controller_prompt_policy.h"
#include "nvn_shaders.h"
#include "platform.h"
#include "catherine_keyboard.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace nx::nvn {
using namespace nx::vk;
namespace {
struct Identity {
    unsigned kind, w, h;
    const char *sha1;
    std::array<size_t, 4> sample;
    uint64_t quick;
};
const Identity identities[] = {
#include "controller_art_catalog.inc"
};
constexpr uint8_t phone_ps_icons[4][32 * 32] = {
#include "controller_art_phone_ps.inc"
};
struct Stored {
    std::vector<uint8_t> original, replacement;
    uint64_t version = 0;
};
std::array<Stored, std::size(identities)> stored;
struct Configuration {
    bool ps = false, keyboard = false;
    unsigned layout=0,tutorial_layout=0;
    std::array<uint16_t,catherine::kKeyboardBindings> keys=catherine::kDefaultKeyboard;
    std::array<unsigned, 16> physical{};
    uint64_t version = 0;
};
const Configuration &configuration() {
    // Called only while the renderer GPU mutex is held. One atomic map snapshot
    // prevents mixing mappings if settings are changed concurrently.
    static Configuration cached;
    static uint64_t previous_map = 0, previous_keys = 0;
    static bool initialized = false;
    uint64_t map = 0xfedcba9876543210ull;
    bool ps = false;
    auto layout=plat::active_input_layout();
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    map = plat::controller_binding_map();
    layout=plat::prompt_input_layout();
    ps = layout==plat::InputLayout::PlayStation||layout==plat::InputLayout::PS4;
#endif
    const bool keyboard=plat::prompt_input_layout()==plat::InputLayout::KeyboardMouse;
    const uint64_t key_version=plat::keyboard_binding_version();
    const unsigned tutorial_layout=catherine::resolve_tutorial_layout(unsigned(plat::active_input_layout()));
    if (!initialized || map != previous_map || ps != cached.ps || unsigned(layout)!=cached.layout || tutorial_layout!=cached.tutorial_layout || keyboard != cached.keyboard || key_version != previous_keys) {
        cached.layout=unsigned(layout);
        cached.tutorial_layout=tutorial_layout;
        for(unsigned i=0;i<cached.keys.size();++i)cached.keys[i]=uint16_t(plat::keyboard_binding(i));
        previous_keys=key_version;cached.keyboard=keyboard;
        for (unsigned i = 0; i < cached.physical.size(); ++i)
            cached.physical[i] = unsigned((map >> (i * 4)) & 15);
        cached.ps = ps;
        ++cached.version;
        previous_map = map;
        initialized = true;
    }
    return cached;
}
bool supported(const GpuImage &image, size_t size) {
    return image.layers == 1 && image.levels == 1 && image.depth == 1 && !image.rendered && !image.storage &&
           (image.format == VK_FORMAT_BC3_UNORM_BLOCK || image.format == VK_FORMAT_BC3_SRGB_BLOCK) &&
           size == size_t(image.width) * image.height;
}
uint64_t quick(const uint8_t *bytes, const Identity &id) {
    uint64_t hash = 1469598103934665603ull;
    for (auto offset : id.sample)
        for (unsigned k = 0; k < 32; ++k)
            hash = (hash ^ bytes[offset + k]) * 1099511628211ull;
    return hash;
}
struct Patch {
    unsigned button;
    int x, y, w, h;
};
std::vector<Patch> patches(unsigned kind,bool keyboard) {
    std::vector<Patch> p;
    auto add = [&](unsigned b, int x, int y, int w, int h) { p.push_back({b, x, y, w, h}); };
    auto row = [&](int x, int y) {
        for (unsigned i = 0; i < 4; ++i)
            add(std::array<unsigned, 4>{0, 2, 3, 1}[i], x + 40 * i, y, 32, 32);
    };
    if (kind == 5) {
        add(6, 684, 464, 40, 24);
        add(6, 684, 512, 40, 24);
        add(10, 684, 352, 36, 40);
        add(10, 684, 404, 36, 36);
        add(2, 1384, 608, 52, 52);
        add(2, 1384, 680, 52, 52);
    }
    if (kind == 6) {
        add(3, 624, 136, 52, 56);
        add(2, 712, 184, 52, 56);
    }
    if (kind == 7) {
        row(488, 520);
        row(488, 564);
        add(7, 12, 360, 44, 24);
        add(7, 248, 280, 40, 24);
        add(10, 12, 400, 40, 36);
        add(10, 248, 324, 36, 36);
        add(0, 156, 400, 44, 48);
    }
    if (kind == 8) {
        unsigned b[] = {2, 1, 2, 1, 0, 2, 3, 1, 1, 2};
        int x[] = {4, 44, 80, 120, 156, 196, 236, 276, 316, 356};
        for (unsigned i = 0; i < 10; ++i)
            add(b[i], x[i], 140, 32, 32);
        add(3, 588, 112, 36, 36);
        add(0, 628, 112, 36, 36);
        add(0, 692, 260, 32, 32);
        add(0, 732, 260, 32, 32);
        add(3, 692, 300, 32, 32);
        add(3, 732, 300, 32, 32);
        add(0, 668, 424, 32, 32);
        add(0, 668, 464, 32, 32);
        add(2, 720, 364, 32, 32);
        add(2, 720, 412, 32, 32);
        add(10, 252, 344, 40, 40);
        add(10, 416, 344, 40, 40);
    }
    if (kind == 9) {
        row(40, 180);
        row(40, 224);
        // Fixed direction art; these are not remappable Menu/View actions.
        add(16, 316, 632, 48, 44);
        add(17, 316, 676, 48, 44);
    }
    if (kind == 10) {
        row(556, 100);
        row(556, 144);
    }
    if (kind == 11) {
        // The phone has two platform rows of the same four face buttons.
        constexpr int x[] = {212, 252, 288, 328};
        constexpr unsigned top[] = {0, 2, 1, 3};
        constexpr unsigned bottom[] = {1, 0, 2, 3};
        for (unsigned i = 0; i < 4; ++i) {
            add(top[i], x[i], 0, 32, 32);
            add(bottom[i], x[i], 36, 32, 32);
        }
    }
    if(kind==14){for(unsigned i=0;i<4;++i)add(i&1,276+40*i,640,32,36);}
    if (kind == 13) {
        // Question & Answer has its own Confirm badge and two platform rows.
        // L/R are shared labels on this screen and retain their original art.
        add(0, 644, 688, 36, 32);
        add(0, 644, 728, 36, 32);
        if(keyboard){for(int y:{688,728}){add(6,692,y,28,32);add(7,736,y,28,32);}}
    }
    return p;
}

void replace_grab_mark(controller_art::Canvas &canvas, std::vector<uint8_t> &blocks, const Configuration &config) {
    const unsigned button = config.physical[0];

    // Grab uses a separate A mark baked into the puzzle HUD. Keep the badge's
    // original disk and alpha, which also supply the silhouette for its glow.
    constexpr unsigned left = 360, top = 604, width = 40, height = 40;
    constexpr controller_art::Color disk{60, 61, 60, 255}, ink{255, 255, 255, 255};
    canvas.rect(left, top, width, height, disk);
    const auto* supplied=supplied_art::find(config.keyboard?supplied_art::keyboard(config.keys[20]):supplied_art::gamepad(config.layout,button));
    if(supplied)supplied_art::draw(canvas,supplied,left,top,width,height);
    else if(config.keyboard){
        const char* label=catherine::keyboard_badge(config.keys[20]);
        const float height=std::min(23.f,34.f*7/(6*std::strlen(label)-1));
        controller_art::text(canvas,label,380.f-(6*std::strlen(label)-1)*height/14,624.f-height/2,height,ink);
    }else controller_art::mark(canvas, button, config.ps, 380.0f, 624.0f, 40.0f, ink, disk);
    for (unsigned y = top; y < top + height; y += 4) {
        for (unsigned x = left; x < left + width; x += 4) {
            uint8_t encoded[16];
            controller_art::encode_block(&canvas.pixels[(size_t(y) * canvas.w + x) * 4], canvas.w * 4, encoded);
            const size_t offset = (size_t(y / 4) * (canvas.w / 4) + x / 4) * 16;
            if(supplied)std::memcpy(blocks.data()+offset,encoded,16);
            else std::memcpy(blocks.data() + offset + 8, encoded + 8, 8);
        }
    }
}

const std::vector<uint8_t> &replacement(unsigned index, const Configuration &config) {
    auto &state = stored[index];
    const auto &id = identities[index];
    if (state.version == config.version && !state.replacement.empty())
        return state.replacement;
    if (id.kind <= 4) {
        // Diagrams follow actual input, independently of the prompt preference.
        const auto &artwork = supplied_art::tutorial(config.tutorial_layout);
        if(!artwork.empty()) {
            const auto start=artwork.begin()+(id.kind-1)*id.w*id.h;
            state.replacement.assign(start,start+id.w*id.h);
        }else state.replacement=controller_art::encode(controller_art::controller(true,id.kind==2?6:id.kind==3?5:7,id.kind==1));
    } else {
        controller_art::Canvas canvas(id.w, id.h);
        bc_decode(0x44, state.original.data(), id.w, id.h, 1, canvas.pixels.data());
        state.replacement = state.original;
        const auto edits = patches(id.kind,config.keyboard);
        for (const auto &p : edits) {
            if (id.kind == 9 && p.button >= 16) {
                // Reuse this native Config atlas's filled left triangle. Its
                // mirrored copy gives the right hint on every input device.
                for (int y = 0; y < p.h; ++y) for (int x = 0; x < p.w; ++x) {
                    const int source_x = 329 + (p.button == 16 ? x : p.w - 1 - x) * 83 / p.w;
                    const int source_y = 131 + y * 61 / p.h;
                    const auto *pixel = &canvas.pixels[(size_t(source_y) * id.w + source_x) * 4];
                    canvas.put(p.x + x, p.y + y, {pixel[0], pixel[1], pixel[2], pixel[3]});
                }
                continue;
            }
            // The original atlas includes a complete PS L1 mark. Keep its
            // compressed pixels and reuse them in the Switch L slot below.
            const auto* supplied=supplied_art::find(config.keyboard?supplied_art::keyboard(config.keys[p.button]):supplied_art::gamepad(config.layout,config.physical[p.button]));
            if(supplied_art::draw(canvas,supplied,p.x,p.y,p.w,p.h))continue;
            if (id.kind == 5 && p.button == 6 && config.ps && !config.keyboard && config.physical[6] == 6)
                continue;
            if(config.keyboard){
                controller_art::key_glyph(canvas,catherine::keyboard_badge(config.keys[p.button]),p.x,p.y,p.w,p.h);
            } else if (id.kind == 11 && config.ps) {
                if (config.physical[p.button] < 4) continue;
                const float x = p.x + 16.0f, y = p.y + 16.0f;
                canvas.circle(x, y, 12.0f, controller_art::white);
                controller_art::mark(canvas, config.physical[p.button], true, x, y, 21.0f,
                                     controller_art::black);
            } else {
                controller_art::glyph(canvas, config.physical[p.button], config.ps, p.x, p.y, p.w, p.h);
            }
        }
        // Re-encode only the bounded edited blocks; the remainder stays byte-for-byte original.
        for (const auto &p : edits) {
            const unsigned physical = p.button < 16 ? config.physical[p.button] : 0;
            const bool supplied=p.button<16&&supplied_art::find(config.keyboard?supplied_art::keyboard(config.keys[p.button]):supplied_art::gamepad(config.layout,physical));
            if (!supplied && id.kind == 11 && config.ps && !config.keyboard && physical < 4) {
                constexpr unsigned blocks_per_row = 32 / 4;
                constexpr unsigned bytes_per_row = blocks_per_row * 16;
                for (unsigned row = 0; row < blocks_per_row; ++row) {
                    const size_t offset = (size_t(p.y / 4 + row) * (id.w / 4) + p.x / 4) * 16;
                    std::memcpy(state.replacement.data() + offset,
                                phone_ps_icons[physical] + row * bytes_per_row, bytes_per_row);
                }
                continue;
            }
            if (!supplied && id.kind == 5 && p.button == 6 && config.ps && !config.keyboard && config.physical[6] == 6) {
                if (p.y == 512) {
                    for (unsigned y = 0; y < 24; y += 4)
                        std::memcpy(&state.replacement[(size_t((512 + y) / 4) * (id.w / 4) + 684 / 4) * 16],
                                    &state.original[(size_t((464 + y) / 4) * (id.w / 4) + 684 / 4) * 16],
                                    (40 / 4) * 16);
                }
                continue;
            }
            for (unsigned y = unsigned(p.y) & ~3u; y < (unsigned(p.y + p.h + 3) & ~3u); y += 4)
                for (unsigned x = unsigned(p.x) & ~3u; x < (unsigned(p.x + p.w + 3) & ~3u); x += 4)
                    controller_art::encode_block(
                        &canvas.pixels[(size_t(y) * id.w + x) * 4], id.w * 4,
                        &state.replacement[(size_t(y / 4) * (id.w / 4) + x / 4) * 16]);
        }
        if (id.kind == 5 || id.kind == 12) replace_grab_mark(canvas, state.replacement, config);
    }
    state.version = config.version;
    return state.replacement;
}
} // namespace
void controller_art_identify(GpuImage &image, uint8_t *blocks, size_t size) {
    image.controller_art_id = 0;
    image.controller_art_version = 0;
#ifndef CATHERINE_HIGH_REFRESH_BUILD
    return;
#endif
    if (!supported(image, size))
        return;
    for (unsigned i = 0; i < std::size(identities); ++i) {
        const auto &id = identities[i];
        if (image.width != id.w || image.height != id.h || quick(blocks, id) != id.quick)
            continue;
        if (sha1_hex(blocks, size) != id.sha1)
            continue;
        auto &state = stored[i];
        if (state.original.empty())
            state.original.assign(blocks, blocks + size);
        const auto &config = configuration();
        const auto &data = replacement(i, config);
        std::memcpy(blocks, data.data(), size);
        image.controller_art_id = i + 1;
        image.controller_art_version = config.version;
        static bool logged[std::size(identities)]{};
        if (!logged[i]) {
            log("nvn: PC controller artwork recognized %u (%ux%u)", id.kind, id.w, id.h);
            logged[i] = true;
        }
        return;
    }
}
bool controller_art_changed(const GpuImage &image) {
    return image.controller_art_id && !image.rendered && !image.storage &&
           image.controller_art_version != configuration().version;
}
void controller_art_refresh(VkCommandBuffer cb, GpuImage &image) {
    if (!controller_art_changed(image))
        return;
    unsigned index = image.controller_art_id - 1;
    if (index >= std::size(identities))
        return;
    const auto &config = configuration();
    const auto &data = replacement(index, config);
    Upload staging = upload(data.data(), data.size(), 16);
    if (image.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        transition(cb, image, VK_IMAGE_LAYOUT_GENERAL);
    transition(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = staging.offset;
    copy.imageSubresource = {image.aspect, 0, 0, 1};
    copy.imageExtent = {image.width, image.height, 1};
    p_vkCmdCopyBufferToImage(cb, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image.controller_art_version = config.version;
    image.uploaded = true;
    transition(cb, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
} // namespace nx::nvn
