#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Original brush lettering for the PC-only piano key, in a 144 x 38 cell. */
typedef struct {
    float x0, y0, x1, y1, r0, r1;
} CatherineMenuStroke;

static void catherine_paint_exit_game(uint8_t *blocks)
{
    static const CatherineMenuStroke strokes[] = {
        /* E */
        {9, 11, 27, 5, 0.4f, 1.5f}, {27, 5, 38, 5, 1.5f, 0.2f},
        {25, 6, 16, 17, 0.5f, 2.7f}, {16, 17, 7, 31, 2.7f, 0.3f},
        {12, 22, 25, 19, 0.4f, 1.3f}, {25, 19, 32, 18, 1.3f, 0.2f},
        {7, 30, 22, 30, 0.4f, 1.4f}, {22, 30, 31, 27, 1.4f, 0.3f},
        /* x */
        {27, 25, 35, 21, 0.3f, 1.4f}, {35, 21, 36, 29, 1.4f, 0.8f},
        {36, 29, 41, 27, 0.8f, 0.3f}, {30, 31, 42, 20, 0.3f, 1.0f},
        /* i */
        {40, 27, 47, 21, 0.3f, 1.3f}, {47, 21, 43, 29, 1.3f, 0.8f},
        {43, 29, 49, 27, 0.8f, 0.3f}, {50, 15, 52, 13, 1.1f, 0.7f},
        /* t */
        {59, 12, 51, 24, 0.5f, 1.8f}, {51, 24, 49, 29, 1.8f, 0.8f},
        {49, 29, 57, 28, 0.8f, 0.8f}, {57, 28, 63, 23, 0.8f, 0.2f},
        {49, 20, 63, 18, 0.5f, 0.6f},
        /* G */
        {95, 7, 90, 5, 0.2f, 1.1f}, {90, 5, 83, 8, 1.1f, 1.5f},
        {83, 8, 75, 14, 1.5f, 2.0f}, {75, 14, 69, 23, 2.0f, 2.2f},
        {69, 23, 70, 29, 2.2f, 1.5f}, {70, 29, 77, 31, 1.5f, 1.4f},
        {77, 31, 86, 27, 1.4f, 1.8f}, {86, 27, 91, 21, 1.8f, 0.8f},
        {79, 23, 94, 20, 0.4f, 1.0f}, {94, 20, 90, 27, 1.0f, 0.4f},
        /* a */
        {103, 21, 98, 22, 0.6f, 1.2f}, {98, 22, 92, 27, 1.2f, 1.5f},
        {92, 27, 93, 30, 1.5f, 0.9f}, {93, 30, 99, 27, 0.9f, 1.1f},
        {99, 27, 104, 21, 1.1f, 0.7f}, {104, 21, 100, 29, 0.7f, 1.0f},
        {100, 29, 106, 27, 1.0f, 0.4f},
        /* m */
        {104, 30, 112, 21, 0.5f, 1.0f}, {112, 21, 114, 21, 1.0f, 0.8f},
        {114, 21, 111, 28, 0.8f, 1.1f}, {111, 28, 121, 21, 1.1f, 0.9f},
        {121, 21, 118, 28, 0.9f, 1.2f}, {118, 28, 126, 21, 1.2f, 1.1f},
        {126, 21, 123, 28, 1.1f, 0.9f}, {123, 28, 129, 26, 0.9f, 0.4f},
        /* e */
        {128, 26, 136, 23, 0.4f, 1.0f}, {136, 23, 136, 20, 1.0f, 0.6f},
        {136, 20, 132, 21, 0.6f, 1.0f}, {132, 21, 128, 26, 1.0f, 1.6f},
        {128, 26, 129, 30, 1.6f, 1.1f}, {129, 30, 137, 28, 1.1f, 0.8f},
        {137, 28, 143, 23, 0.8f, 0.2f},
    };
    enum { scale = 4, width = 144 * scale, height = 38 * scale };
    uint8_t coverage[height][width] = {{0}};
    for (size_t i = 0; i < sizeof(strokes) / sizeof(strokes[0]); ++i) {
        CatherineMenuStroke s = strokes[i];
        float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
        float inverse_length2 = 1.0f / (dx * dx + dy * dy);
        float margin = fmaxf(s.r0, s.r1) + 1.0f;
        int left = (int)fmaxf(0, (fminf(s.x0, s.x1) - margin) * scale);
        int top = (int)fmaxf(0, (fminf(s.y0, s.y1) - margin) * scale);
        int right = (int)fminf(width, (fmaxf(s.x0, s.x1) + margin) * scale);
        int bottom = (int)fminf(height, (fmaxf(s.y0, s.y1) + margin) * scale);
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                float px = ((float)x + 0.5f) / scale - s.x0;
                float py = ((float)y + 0.5f) / scale - s.y0;
                float t = fminf(1, fmaxf(0, (px * dx + py * dy) * inverse_length2));
                float rx = px - t * dx, ry = py - t * dy;
                float radius = s.r0 + (s.r1 - s.r0) * t;
                float edge = (radius - sqrtf(rx * rx + ry * ry)) * scale + 0.5f;
                uint8_t value = (uint8_t)(255 * fminf(1, fmaxf(0, edge)));
                if (value > coverage[y][x]) coverage[y][x] = value;
            }
        }
    }

    /* BC3 with white color endpoints and the full eight-level alpha palette. */
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < 1024; x += 4) {
            uint8_t *block = blocks + y * 1024 + x * 4;
            uint64_t alpha = 0;
            for (int sample = 0; sample < 16; ++sample) {
                unsigned value = x < width ? coverage[y + sample / 4][x + sample % 4] : 0;
                unsigned level = (value * 7 + 127) / 255;
                unsigned code = level == 0 ? 1 : level == 7 ? 0 : 8 - level;
                alpha |= (uint64_t)code << (3 * sample);
            }
            block[0] = 255;
            block[1] = 0;
            for (int byte = 0; byte < 6; ++byte) block[2 + byte] = (uint8_t)(alpha >> (8 * byte));
            memset(block + 8, 0xff, 4);
            memset(block + 12, 0, 4);
        }
    }
}

static void catherine_patch_piano_labels(uint64_t address, uint32_t size)
{
    enum { dds_header = 128, atlas_width = 1024, atlas_height = 4096, row_height = 152 };
    if (!address || size != dds_header + atlas_width * atlas_height ||
        LD32(address) != 0x20534444u || LD32(address + 12) != atlas_height ||
        LD32(address + 16) != atlas_width || LD32(address + 84) != 0x35545844u) {
        return;
    }
    uint8_t *pixels = (uint8_t *)(uintptr_t)(address + dds_header);
    uint32_t signature = 2166136261u;
    for (size_t i = 512 * atlas_width; i < 528 * atlas_width; ++i) {
        signature = (signature ^ pixels[i]) * 16777619u;
    }
    /* Localized E/F/G/I/S titlemenu.dds from the piano model. */
    switch (signature) {
    case 0x37ca186eu:
    case 0xd2c1c143u:
    case 0x8b3bd00eu:
    case 0x648f1bf0u:
    case 0x2ebe9c17u:
        /* Start on BC3 block rows above the tallest localized caption. */
        memcpy(pixels + 452 * atlas_width, pixels + 604 * atlas_width, row_height * atlas_width);
        catherine_paint_exit_game(pixels + 604 * atlas_width);
        break;
    }
}
