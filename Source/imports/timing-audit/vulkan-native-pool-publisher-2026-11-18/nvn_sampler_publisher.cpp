#include <array>
#include "shared_texture_watch.h"
// NVN sampler states as Vulkan samplers (split from nvn_pipeline.cpp).
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "nvn_pipeline.h"
#include "native_pool_publisher.h"

namespace nx::nvn {

using namespace nx::vk;

namespace {

VkSamplerAddressMode vk_wrap(uint32_t w) {  // NVN: 0 clamp, 1 repeat, 2 mirror clamp, 3 mirror clamp to edge,
    switch (w) {                            // 4 mirror clamp to border, 5 clamp to border, 6 mirrored repeat, 7 edge
        case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case 2:
        case 3:
        case 4: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case 6: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

}  // namespace

// ---- samplers -------------------------------------------------------------------------------------------------------
namespace {
using SamplerCacheKey=ByteKey<sizeof(SamplerInfo)+1>;
auto& sampler_cache() {static std::unordered_map<SamplerCacheKey,VkSampler,ByteKeyHash> cache;return cache;}
}

VkSampler sampler_for(const SamplerInfo& s, bool shadow) {
    // Asked per texture binding of every draw: the key is the state's bytes and the flag (what the std::string key
    // of before held, compared the same way), hashed; no allocation on a hit.
    using Key = ByteKey<sizeof(SamplerInfo) + 1>;
    auto& cache=sampler_cache();
    Key key;
    std::memcpy(key.b, &s, sizeof s);
    key.b[sizeof s] = shadow ? '1' : '0';
    auto& v = cache[key];
    if (v) return v;
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // NVN min filter: 0 nearest, 1 linear, 2 nearest mip nearest, 3 linear mip nearest, 4 nearest mip linear,
    // 5 linear mip linear; mag: 0 nearest, 1 linear.
    uint32_t m = s.min_filter;
    ci.minFilter = (m == 1 || m == 3 || m == 5) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.magFilter = s.mag_filter == 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = (m == 4 || m == 5) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = vk_wrap(s.wrap[0]);
    ci.addressModeV = vk_wrap(s.wrap[1]);
    ci.addressModeW = vk_wrap(s.wrap[2]);
    ci.mipLodBias = s.lod_bias;
    // Games pass lod_min -1000 ("no limit"); Metal wants lodMinClamp >= 0, and a negative one made the glyph
    // sampler return 0 (all text invisible).
    ci.minLod = m <= 1 ? 0.0f : std::max(0.0f, s.lod_min);
    ci.maxLod = m <= 1 ? 0.25f : std::max(ci.minLod, s.lod_max);  // no mipmapping: level 0 only
    ci.anisotropyEnable = s.max_anisotropy > 1.0f;
    ci.maxAnisotropy = std::min(s.max_anisotropy, 16.0f);
    ci.compareEnable = shadow;
    ci.compareOp = shadow ? vk_compare(s.compare_func) : VK_COMPARE_OP_ALWAYS;
    bool white = s.border[3] > 0.5f && s.border[0] > 0.5f;
    ci.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                     : s.border[3] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
                                          : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    p_vkCreateSampler(vk_device(), &ci, nullptr, &v);
    return v;
}

namespace {
VkSamplerCreateInfo original_sampler_values(const SamplerInfo& s,bool shadow) {
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // NVN min filter: 0 nearest, 1 linear, 2 nearest mip nearest, 3 linear mip nearest, 4 nearest mip linear,
    // 5 linear mip linear; mag: 0 nearest, 1 linear.
    uint32_t m = s.min_filter;
    ci.minFilter = (m == 1 || m == 3 || m == 5) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.magFilter = s.mag_filter == 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = (m == 4 || m == 5) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = vk_wrap(s.wrap[0]);
    ci.addressModeV = vk_wrap(s.wrap[1]);
    ci.addressModeW = vk_wrap(s.wrap[2]);
    ci.mipLodBias = s.lod_bias;
    // Games pass lod_min -1000 ("no limit"); Metal wants lodMinClamp >= 0, and a negative one made the glyph
    // sampler return 0 (all text invisible).
    ci.minLod = m <= 1 ? 0.0f : std::max(0.0f, s.lod_min);
    ci.maxLod = m <= 1 ? 0.25f : std::max(ci.minLod, s.lod_max);  // no mipmapping: level 0 only
    ci.anisotropyEnable = s.max_anisotropy > 1.0f;
    ci.maxAnisotropy = std::min(s.max_anisotropy, 16.0f);
    ci.compareEnable = shadow;
    ci.compareOp = shadow ? vk_compare(s.compare_func) : VK_COMPARE_OP_ALWAYS;
    bool white = s.border[3] > 0.5f && s.border[0] > 0.5f;
    ci.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                     : s.border[3] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
                                          : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    return ci;
}
}
void verify_published_sampler(const native_pools::SamplerPlan& plan,bool shadow) {
    if(native_pools::mode()==2) {
        if(!native_pools::same_ci(plan.values[shadow?1:0],original_sampler_values(plan.source,shadow)))fatal("Published native sampler differs from original Vulkan values");
        ++native_pools::census.sampler_compared;
    }
}
VkSampler sampler_for_published(const native_pools::SamplerPlan& plan,bool shadow) {
    const auto index=shadow?1:0;const auto& ci=plan.values[index];verify_published_sampler(plan,shadow);
    ++native_pools::census.sampler_uses;
    // Same cache as the old entry: no duplicate sampler owners or changed identity.
    // The key and every Vulkan creation field were compiled at registration.
    SamplerCacheKey key;std::memcpy(key.b,plan.keys[index].data(),sizeof key.b);auto& v=sampler_cache()[key];
    if(!v)p_vkCreateSampler(vk_device(),&ci,nullptr,&v);return v;
}

// ---- pitch-linear texture contents (moved from nvn_pipeline.cpp) ------------------------------------------------

// Four independent lanes: one multiply chain ran at ~1 byte per cycle (Wonder's 16 MB glyph cache: ~5 ms).
uint64_t hash_memory(const uint8_t* p, uint64_t size) {
    uint64_t h[4] = {0x9E3779B97F4A7C15ull, 1, 2, 3}, n = size / 32;
    const uint64_t* w = reinterpret_cast<const uint64_t*>(p);
    for (uint64_t k = 0; k < n; k++)
        for (int l = 0; l < 4; l++) h[l] = (h[l] ^ w[4 * k + l]) * 0x100000001B3ull + (h[l] >> 29);
    uint64_t r = h[0] ^ h[1] * 3 ^ h[2] * 5 ^ h[3] * 7;
    for (uint64_t k = n * 4; k < size / 8; k++) r = (r ^ w[k]) * 0x100000001B3ull + (r >> 29);
    return r;
}

// A pitch-linear texture the GPU does not write: did the CPU change it since the last upload?
void check_linear_content(Texture& t) {
    // Large pitch-linear textures include the rotating movie buffers. Check each frame so a reused
    // buffer cannot show its previous picture between two newer ones when write watch is unavailable.
    uint64_t frame = g_frame.load(std::memory_order_relaxed) + 1;
    static const uint64_t hash_small = [] {
        const char* e = std::getenv("NX_HASH_SMALL");
        return e ? std::strtoull(e, nullptr, 10) : 0;
    }();
    bool is_small = hash_small && t.storage_size <= hash_small;
    const bool large_linear = t.stride && t.storage_size >= (1u << 20);
    const uint64_t interval = large_linear ? 1 : 4;
    if ((!t.stride && !is_small) || !t.memory || !t.image || t.image->rendered ||
        (t.hashed_submit && t.hashed_submit + interval > frame))
        return;
    t.hashed_submit = frame;
    // Share geometry's reset/epoch broker, including allocation identity and failed-query invalidation.
    static const bool shared_watch=[] {const char* e=std::getenv("CATHERINE_SHARED_TEXTURE_WATCH");return !e || e[0]!='0';}();
    static const bool verify_watch=[] {const char* e=std::getenv("CATHERINE_SHARED_TEXTURE_WATCH_VERIFY");return e && e[0]=='1';}();
    if(large_linear && shared_watch) {
        struct Watch {const Texture* texture=nullptr;uint64_t memory=0,size=0,epoch=0,allocation=0,retry_frame=0;unsigned dirty_streak=0;};
        static std::array<Watch,64> cache;
        static bool failed_verification=false;
        static uint64_t skipped=0,verified=0,last_log=0;
        uint64_t key=uintptr_t(&t);key^=key>>17;key*=0xbf58476d1ce4e5b9ull;key^=key>>29;
        auto& w=cache[key&63];
        if(w.texture!=&t || w.memory!=t.memory || w.size!=t.storage_size) {
            // Rotating movie buffers still belong to the same texture. Keep
            // its dirty backoff, while discarding the old memory checkpoint.
            const auto retry=w.texture==&t?w.retry_frame:0;
            const auto streak=w.texture==&t?w.dirty_streak:0;
            w={&t,t.memory,t.storage_size,0,0,retry,streak};
        }
        bool changed=true;
        const bool deferred=frame<w.retry_frame;
        const bool available=!failed_verification && !deferred && replay::texture_write_watch(reinterpret_cast<const uint8_t*>(t.memory),t.storage_size,w.epoch,w.allocation,changed);
        if(available) {
            if(changed) {if(++w.dirty_streak>=3)w.retry_frame=frame+120;}
            else {w.dirty_streak=0;w.retry_frame=0;}
        }
        // A texture changing every frame (movies) keeps the original hash
        // path without an additional OS query, with occasional rechecks.
        static uint64_t checks=0,unsupported=0,dirty=0,last_status=0;
        ++checks;unsupported+=!deferred&&!available;dirty+=available&&changed;
        if(frame>last_status+600){last_status=frame;log("nvn: shared texture watch: checks=%llu unsupported=%llu dirty=%llu memory=%llx size=%llu",(unsigned long long)checks,(unsigned long long)unsupported,(unsigned long long)dirty,(unsigned long long)t.memory,(unsigned long long)t.storage_size);}
        if(available && !changed && t.content_hash) {
            if(verify_watch) {
                ++verified;
                if(hash_memory(reinterpret_cast<const uint8_t*>(t.memory),t.storage_size)!=t.content_hash) {
                    log("nvn: shared texture watch: exact hash verification mismatch; disabling shortcut");
                    failed_verification=true;
                }
            }
            if(!failed_verification) {
                ++skipped;
                if(frame>last_log+600){last_log=frame;log("nvn: shared texture watch: frame=%llu hashes_avoided=%llu exact_verified=%llu",(unsigned long long)frame,(unsigned long long)skipped,(unsigned long long)verified);}
                return;
            }
        }
    }
    // 1 MB and more (the glyph cache): page protection says whether the CPU wrote it, every frame, at no cost.
    if (large_linear) {
        if (!t.watched && !t.content_hash) t.watched = plat::write_watch_add(t.memory, t.storage_size);
        if (t.watched) {
            if (plat::write_watch_take(t.memory) && t.content_hash) {
                t.dirty = true;
                static uint64_t n = 0;
                if (++n == 1 || n % 100 == 0) log("nvn: watched texture written by the CPU (%llu times)", (unsigned long long)n);
            }
            t.content_hash = 1;  // the first take only arms the watch (the image was just uploaded)
            return;
        }
    }
    uint64_t h = hash_memory(reinterpret_cast<const uint8_t*>(t.memory), t.storage_size);
    if (t.content_hash && h != t.content_hash) {
        t.dirty = true;
        static int logged = 0;
        if (!t.stride && logged++ < 100)
            log("nvn: block-linear texture %llux%u f%x (%llu bytes) changed by the CPU", (unsigned long long)t.width,
                t.height, t.format, (unsigned long long)t.storage_size);
    }
    t.content_hash = h;
}

}  // namespace nx::nvn

