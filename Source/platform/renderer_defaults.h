#pragma once

static const char *const catherine_renderer_defaults[][2] = {
        {"CATHERINE_Q1_NATIVE_SEMAPHORES", "1"},
        {"CATHERINE_FAST_NVN_DRIVER", "1"},
        {"CATHERINE_NVN_ATTACHMENT_BARRIERS", "1"},
        {"CATHERINE_GUEST_PRECISE_SLEEP", "1"},
        {"CATHERINE_NVN_LIST_RESERVE", "1"},
        {"NX_PASS_PIPELINE_ATTACHMENTS", "1"},
        {"NX_SHADER_SCOPE_TWO_WAY", "1"},
        {"NX_WATCHED_GEOMETRY", "2"},
        {"NX_WATCHED_GEOMETRY_RECYCLE", "1"},
#if defined(__linux__) && !defined(__ANDROID__)
        {"CATHERINE_PARALLEL80_HELPERS", "1"},
#endif
        {"NX_WRITTEN_PAGE_PREFILTER", "1"},
        {"NX_DEDUP_FLAT", "1"},
        {"NX_ASYNC_GPU", "2"},
        {"NX_REPLAY_THREAD", "1"},
        {"NX_DEFERRED_SNAPSHOT", "1"},
        {"NX_GPU_TIMELINE_SYNC", "1"},
        {"NX_CPU_ASYNC", "1"},
        {"NX_CPU_MORPH", "1"},
        {"NX_CPU_SKIN", "1"},
        {"NX_CPU_SKIN_FUSE_VALIDATION", "1"},
        {"NX_CPU_SKIN_NAN_TANGENTS", "1"},
        {"NX_CPU_SKIN_NAN_POSITIONS", "1"},
        {"NX_SHADOW_SNAPSHOT_CACHE", "1"},
        {"NX_CPU_LAZY_TAIL", "1"},
        {"NX_CPU_SIMD_WRITEBACK", "1"},
        {"NX_DESCRIPTOR_BUFFER", "1"},
#if defined(__linux__) && !defined(__ANDROID__)
        {"NX_RING_CACHED", "1"},
#endif
        {"NX_COMPACT_DESCRIPTOR_VALUES", "1"},
        {"NX_UNIFORM_STATIC_EXTENT", "1"},
        {"CATHERINE_SKIN_DEVICE_MIRROR", "1"},
        {"CATHERINE_VULKAN_TEMPLATE_ADMISSION", "1"},
        {"CATHERINE_ASYNC_CPU_SKIN", "0"},
        {"CATHERINE_ZERO_INPLACE_MORPH", "1"},
        {"CATHERINE_VULKAN_DESCRIPTOR_LAYOUTS", "1"},
        {"CATHERINE_VULKAN_DESCRIPTOR_SET2_REUSE", "0"},  // perf 2026-11-22: was 1 (see below)
        {"CATHERINE_SKIN_REPEAT_JOINTS", "0"},
        {"CATHERINE_VULKAN_PREPARED_IMAGE", "1"},
        {"CATHERINE_INACTIVE_C3", "1"},
        {"CATHERINE_MATERIAL_PLAN_LOOKUP", "1"},
        {"CATHERINE_VULKAN_PIPELINE_STATE_BUCKETS", "1"},
        {"CATHERINE_VULKAN_NATIVE_PIPELINE_MEMO", "1"},
        {"CATHERINE_MORPH_FINITE_SCAN", "1"},
        {"CATHERINE_NATIVE_VERTEX_RANGE", "1"},
        {"CATHERINE_VULKAN_SELECTIVE_DYNAMIC_UBO", "1"},
        {"NX_DYNAMIC_UBO", "0"},
        // perf 2026-11-22: the legacy (reference) image-material and texture-binding paths: same descriptors,
        // ~2.5% faster in the Day 5 bar. Set either to 1 to use the native layer.
        {"CATHERINE_VULKAN_IMAGE_MATERIALS", "0"},
        {"CATHERINE_VULKAN_TEXTURE_BINDINGS", "0"},
        // perf 2026-11-22: the set-2 descriptor cache and native submit-resource layer cost more than they save in the
        // Day 5 bar (~3.5% and ~2%); their legacy paths produce the same descriptors and bindings.
        {"CATHERINE_VULKAN_SUBMIT_RESOURCES", "0"},
#ifdef _WIN32
        // perf 2026-11-22 (user request): keep OBS Studio's implicit Vulkan capture layer (graphics-hook64.dll) out of
        // the game; it hooked every present. OBS window/display capture still works. Set to 0 to allow OBS game capture.
        {"DISABLE_VULKAN_OBS_CAPTURE", "1"},
#endif
    };
