// Original vector controller diagrams and prompt marks. No game artwork or fonts embedded.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace nx::nvn::controller_art {
struct Point {
    float x, y;
};
struct Color {
    uint8_t r, g, b, a;
};
constexpr Color white{240, 242, 245, 255}, black{25, 27, 32, 255}, grey{75, 79, 89, 255},
    pink{255, 25, 120, 255};
struct Canvas {
    unsigned w, h;
    std::vector<uint8_t> pixels;
    Canvas(unsigned width, unsigned height) : w(width), h(height), pixels(size_t(w) * h * 4) {
    }
    void put(int x, int y, Color c) {
        if (x < 0 || y < 0 || unsigned(x) >= w || unsigned(y) >= h)
            return;
        auto *p = &pixels[(size_t(y) * w + x) * 4];
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = c.a;
    }
    void rect(float x, float y, float width, float height, Color c) {
        for (int j = int(y); j < int(y + height); ++j)
            for (int i = int(x); i < int(x + width); ++i)
                put(i, j, c);
    }
    void circle(float x, float y, float radius, Color c) {
        for (int j = int(y - radius); j <= int(y + radius); ++j)
            for (int i = int(x - radius); i <= int(x + radius); ++i)
                if ((i + .5f - x) * (i + .5f - x) + (j + .5f - y) * (j + .5f - y) <= radius * radius)
                    put(i, j, c);
    }
    void line(Point a, Point b, float width, Color c) {
        float dx = b.x - a.x, dy = b.y - a.y;
        unsigned n = unsigned(std::max(std::abs(dx), std::abs(dy))) + 1;
        for (unsigned i = 0; i <= n; ++i)
            circle(a.x + dx * i / n, a.y + dy * i / n, width * .5f, c);
    }
    void poly(std::initializer_list<Point> points, Color c) {
        std::vector<Point> v(points);
        float top = float(h), bottom = 0;
        for (auto p : v) {
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        for (int y = int(top); y <= int(bottom); ++y) {
            std::vector<float> crossings;
            for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
                auto a = v[i], b = v[j];
                if ((a.y > y + .5f) != (b.y > y + .5f))
                    crossings.push_back(a.x + (y + .5f - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            std::sort(crossings.begin(), crossings.end());
            for (size_t i = 0; i + 1 < crossings.size(); i += 2)
                for (int x = int(std::ceil(crossings[i])); x < crossings[i + 1]; ++x)
                    put(x, y, c);
        }
    }
};
inline const char *letter(char c) {
    switch (c) {
    case 'A':
        return "01110100011000111111100011000110001";
    case 'B':
        return "11110100011000111110100011000111110";
    case 'X':
        return "10001100010101000100010101000110001";
    case 'Y':
        return "10001100010101000100001000010000100";
    case 'L':
        return "10000100001000010000100001000011111";
    case 'R':
        return "11110100011000111110101001001010001";
    case 'T':
        return "11111001000010000100001000010000100";
    case '1':
        return "00100011000010000100001000010001110";
    case '2':
        return "01110100010000100010001000100011111";
    case '3':
        return "11110000010000101110000010000111110";
    case 'C': return "01111100001000010000100001000001111";
    case 'D': return "11110100011000110001100011000111110";
    case 'E': return "11111100001000011110100001000011111";
    case 'F': return "11111100001000011110100001000010000";
    case 'G': return "01111100001000010111100011000101111";
    case 'H': return "10001100011000111111100011000110001";
    case 'I': return "11111001000010000100001000010011111";
    case 'J': return "00111000100001000010100101001001100";
    case 'K': return "10001100101010011000101001001010001";
    case 'M': return "10001110111010110101100011000110001";
    case 'N': return "10001110011100110101100111001110001";
    case 'O': return "01110100011000110001100011000101110";
    case 'P': return "11110100011000111110100001000010000";
    case 'Q': return "01110100011000110001101011001001101";
    case 'S': return "01111100001000001110000010000111110";
    case 'U': return "10001100011000110001100011000101110";
    case 'V': return "10001100011000110001100010101000100";
    case 'W': return "10001100011000110101101011101110001";
    case 'Z': return "11111000010001000100010001000011111";
    case '0': return "01110100011001110101110011000101110";
    case '4': return "00010001100101010010111110001000010";
    case '5': return "11111100001000011110000010000111110";
    case '6': return "01110100001000011110100011000101110";
    case '7': return "11111000010001000100010000100001000";
    case '8': return "01110100011000101110100011000101110";
    case '9': return "01110100011000101111000010000101110";
    default:
        return "00000000000000000000000000000000000";
    }
}
inline void text(Canvas &c, const char *s, float x, float y, float height, Color ink) {
    float scale = height / 7;
    for (; *s; ++s, x += 6 * scale) {
        const char *bitmap = letter(*s);
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 5; ++k)
                if (bitmap[r * 5 + k] == '1')
                    c.rect(x + k * scale, y + r * scale, scale, scale, ink);
    }
}
// Platform physical order is explicitly shared with platform.h: B,A,Y,X,LS,RS,LB,RB,LT,RT,Menu,View,L,U,R,D.
inline void mark(Canvas &c, unsigned button, bool ps, float x, float y, float size, Color ink, Color background = black) {
    const float stroke = std::max(1.f, size * .085f), r = size * .39f;
    if (ps && button < 4) {
        if (button == 0) {
            c.circle(x, y, r, ink);
            c.circle(x, y, r - stroke, background);
        } else if (button == 1) {
            c.line({x - r, y - r}, {x + r, y + r}, stroke, ink);
            c.line({x - r, y + r}, {x + r, y - r}, stroke, ink);
        } else if (button == 2) {
            c.line({x, y - r}, {x - r, y + r}, stroke, ink);
            c.line({x - r, y + r}, {x + r, y + r}, stroke, ink);
            c.line({x + r, y + r}, {x, y - r}, stroke, ink);
        } else {
            c.line({x - r, y - r}, {x + r, y - r}, stroke, ink);
            c.line({x + r, y - r}, {x + r, y + r}, stroke, ink);
            c.line({x + r, y + r}, {x - r, y + r}, stroke, ink);
            c.line({x - r, y + r}, {x - r, y - r}, stroke, ink);
        }
    } else if (button < 10) {
        const char *namesXbox[] = {"B", "A", "Y", "X", "L3", "R3", "LB", "RB", "LT", "RT"};
        const char *namesPS[] = {"", "", "", "", "L3", "R3", "L1", "R1", "L2", "R2"};
        const char *label = ps ? namesPS[button] : namesXbox[button];
        float height = size * (std::strlen(label) == 1 ? .8f : .62f);
        float width = (6 * std::strlen(label) - 1) * height / 7;
        text(c, label, x - width / 2, y - height / 2, height, ink);
    } else if (button == 10) {
        for (int i = -1; i <= 1; ++i)
            c.line({x - r, y + i * r * .65f}, {x + r, y + i * r * .65f}, stroke, ink);
    } else if (button == 11) {
        c.line({x - r, y - r}, {x + r * .2f, y - r}, stroke, ink);
        c.line({x - r, y - r}, {x - r, y + r * .2f}, stroke, ink);
        c.line({x - r * .2f, y + r}, {x + r, y + r}, stroke, ink);
        c.line({x + r, y - r * .2f}, {x + r, y + r}, stroke, ink);
    } else {
        Point p[4] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
        auto d = p[(button - 12) % 4];
        Point tip{x + d.x * r, y + d.y * r},
            a{x - d.y * r * .65f - d.x * r * .6f, y + d.x * r * .65f - d.y * r * .6f},
            b{x + d.y * r * .65f - d.x * r * .6f, y - d.x * r * .65f - d.y * r * .6f};
        c.poly({tip, a, b}, ink);
    }
}
inline void glyph(Canvas &c, unsigned button, bool ps, int x, int y, int w, int h) {
    c.rect(float(x), float(y), float(w), float(h), {0, 0, 0, 0});
    float cx = x + w * .5f, cy = y + h * .5f, r = std::min(w, h) * .48f;
    if (button >= 4 && button <= 9 && w >= h) {
        // Two-character shoulder/stick labels need the width of the slot.
        c.rect(float(x), float(y + 1), float(w), float(h - 2), white);
        c.rect(float(x + 2), float(y + 3), float(w - 4), float(h - 6), black);
        mark(c, button, ps, cx, cy, std::min(float(h) * 1.08f, float(w) * .85f), white);
    } else {
        c.circle(cx, cy, r, white);
        c.circle(cx, cy, std::max(0.f, r - 1.5f), black);
        mark(c, button, ps, cx, cy, std::min(w, h) * .70f, white);
    }
}
inline void key_glyph(Canvas& c,const char* label,int x,int y,int w,int h){
    c.rect(x,y,w,h,{0,0,0,0});
    c.rect(x+1,y+2,w-2,h-4,white);c.rect(x+2,y+3,w-4,h-6,black);
    const float height=std::min(float(h)*.6f,float(w-5)*7/std::max(1.f,float(6*std::strlen(label)-1)));
    const float width=(6*std::strlen(label)-1)*height/7;
    text(c,label,x+(w-width)/2,y+(h-height)/2,height,white);
}
inline Canvas controller(bool ps, unsigned selected, bool dpad,
                         const std::array<unsigned, 16> *mapping = nullptr) {
    Canvas c(1024, 1024);
    // Both silhouettes occupy the original tutorial's central transparent rectangle.
    if (ps) {
        c.poly({{186, 222},
                {250, 195},
                {490, 195},
                {554, 222},
                {610, 464},
                {584, 504},
                {545, 496},
                {471, 391},
                {269, 391},
                {195, 496},
                {156, 504},
                {130, 464}},
               white);
        c.poly(
            {{227, 235}, {290, 215}, {450, 215}, {513, 235}, {478, 361}, {440, 407}, {300, 407}, {262, 361}},
            black);
        c.poly({{289, 226}, {451, 226}, {441, 293}, {299, 293}}, grey);
        c.line({285, 222}, {455, 222}, 5, {50, 123, 255, 255});
    } else {
        c.poly({{176, 221},
                {242, 196},
                {498, 196},
                {564, 221},
                {611, 443},
                {601, 481},
                {568, 496},
                {535, 475},
                {467, 391},
                {273, 391},
                {205, 475},
                {172, 496},
                {139, 481},
                {129, 443}},
               grey);
        c.poly({{187, 224},
                {247, 206},
                {493, 206},
                {553, 224},
                {592, 438},
                {576, 474},
                {538, 454},
                {472, 378},
                {268, 378},
                {202, 454},
                {164, 474},
                {148, 438}},
               black);
        c.circle(370, 236, 18, white);
        c.line({360, 226}, {380, 246}, 5, black);
        c.line({380, 226}, {360, 246}, 5, black);
    }
    c.poly({{194, 215}, {261, 197}, {273, 214}, {196, 241}}, black);
    c.poly({{479, 197}, {546, 215}, {544, 241}, {467, 214}}, black);
    // The triggers sit behind the shoulders; draw distinct labels and targets
    // so a remapped action never points at L1/LB when it requires L2/LT.
    c.rect(205, 168, 52, 30, grey);
    c.rect(483, 168, 52, 30, grey);
    text(c, ps ? "L2" : "LT", 219, 175, 16, white);
    text(c, ps ? "R2" : "RT", 497, 175, 16, white);
    text(c, ps ? "L1" : "LB", 207, 210, 16, white);
    text(c, ps ? "R1" : "RB", 494, 210, 16, white);
    Point left = ps ? Point{300, 355} : Point{234, 286}, right{441, 355},
          cross = ps ? Point{229, 292} : Point{302, 355};
    for (auto p : {left, right}) {
        c.circle(p.x, p.y, 39, grey);
        c.circle(p.x, p.y, 32, black);
        c.circle(p.x, p.y, 24, {49, 52, 60, 255});
    }
    c.rect(cross.x - 30, cross.y - 12, 60, 24, grey);
    c.rect(cross.x - 12, cross.y - 30, 24, 60, grey);
    c.rect(cross.x - 28, cross.y - 10, 56, 20, black);
    c.rect(cross.x - 10, cross.y - 28, 20, 56, black);
    for (unsigned b = 0; b < 4; ++b) {
        Point q = b == 0   ? Point{541, 292}
                  : b == 1 ? Point{512, 320}
                  : b == 2 ? Point{512, 264}
                           : Point{483, 292};
        c.circle(q.x, q.y, 20, grey);
        c.circle(q.x, q.y, 17, black);
        mark(c, b, ps, q.x, q.y, 25, white);
    }
    c.circle(333, 307, 13, grey);
    c.circle(407, 307, 13, grey);
    mark(c, 11, ps, 333, 307, 14, white);
    mark(c, 10, ps, 407, 307, 14, white);
    auto highlight = [&](unsigned button) {
        Point hi{};
        float radius = 27;
        if (button < 4) {
            hi = button == 0   ? Point{541, 292}
                 : button == 1 ? Point{512, 320}
                 : button == 2 ? Point{512, 264}
                               : Point{483, 292};
        } else if (button == 4) {
            hi = left;
            radius = 40;
        } else if (button == 5) {
            hi = right;
            radius = 40;
        } else if (button == 6 || button == 7) {
            hi = {button == 6 ? 231.f : 509.f, 220};
            radius = 32;
        } else if (button == 8 || button == 9) {
            hi = {button == 8 ? 231.f : 509.f, 182};
            radius = 29;
        } else if (button == 10) {
            hi = {407, 307};
        } else if (button == 11) {
            hi = {333, 307};
        } else {
            hi = cross;
            radius = 40;
        }
        // Ring is an overlay; keep the underlying control and label visible.
        for (unsigned a = 0; a < 180; ++a) {
            float angle = float(a) * 6.2831853f / 180;
            c.circle(hi.x + std::cos(angle) * radius, hi.y + std::sin(angle) * radius, 4, pink);
        }
    };
    if (dpad && mapping) {
        std::array<bool, 16> highlighted{};
        for (unsigned logical = 12; logical < 16; ++logical) {
            // All four physical directions share one group ring. Other remapped
            // controls get individual rings; duplicate bindings draw only once.
            unsigned button = std::min((*mapping)[logical], 12u);
            if (!highlighted[button]) {
                highlight(button);
                highlighted[button] = true;
            }
        }
    } else {
        highlight(dpad ? 12 : selected);
    }
    return c;
}
inline void encode_block(const uint8_t *rgba, unsigned stride, uint8_t *dst) {
    unsigned lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            auto p = rgba + y * stride + x * 4;
            for (unsigned k = 0; k < 4; ++k) {
                lo[k] = std::min(lo[k], unsigned(p[k]));
                hi[k] = std::max(hi[k], unsigned(p[k]));
            }
        }
    dst[0] = uint8_t(hi[3]);
    dst[1] = uint8_t(lo[3]);
    unsigned alpha[8] = {hi[3], lo[3]};
    if (hi[3] > lo[3])
        for (unsigned i = 2; i < 8; ++i)
            alpha[i] = ((8 - i) * hi[3] + (i - 1) * lo[3]) / 7;
    else {
        for (unsigned i = 2; i < 6; ++i)
            alpha[i] = ((6 - i) * hi[3] + (i - 1) * lo[3]) / 5;
        alpha[6] = 0;
        alpha[7] = 255;
    }
    uint64_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned a = rgba[(i / 4) * stride + (i % 4) * 4 + 3], best = 0, error = 256;
        for (unsigned j = 0; j < 8; ++j) {
            unsigned e = unsigned(std::abs(int(a) - int(alpha[j])));
            if (e < error)
                error = e, best = j;
        }
        bits |= uint64_t(best) << (3 * i);
    }
    for (unsigned i = 0; i < 6; ++i)
        dst[2 + i] = uint8_t(bits >> (8 * i));
    auto rgb565 = [](unsigned r, unsigned g, unsigned b) {
        return uint16_t(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
    };
    uint16_t c0 = rgb565(hi[0], hi[1], hi[2]), c1 = rgb565(lo[0], lo[1], lo[2]);
    if (c0 < c1)
        std::swap(c0, c1);
    std::memcpy(dst + 8, &c0, 2);
    std::memcpy(dst + 10, &c1, 2);
    unsigned rgb[4][3] = {
        {unsigned(c0 >> 11) * 255 / 31, unsigned((c0 >> 5) & 63) * 255 / 63, unsigned(c0 & 31) * 255 / 31},
        {unsigned(c1 >> 11) * 255 / 31, unsigned((c1 >> 5) & 63) * 255 / 63, unsigned(c1 & 31) * 255 / 31}};
    for (unsigned k = 0; k < 3; ++k)
        rgb[2][k] = (2 * rgb[0][k] + rgb[1][k]) / 3, rgb[3][k] = (rgb[0][k] + 2 * rgb[1][k]) / 3;
    uint32_t color = 0;
    for (unsigned i = 0; i < 16; ++i) {
        auto p = rgba + (i / 4) * stride + (i % 4) * 4;
        unsigned best = 0, error = UINT32_MAX;
        for (unsigned j = 0; j < 4; ++j) {
            unsigned e = 0;
            for (unsigned k = 0; k < 3; ++k) {
                int d = int(p[k]) - int(rgb[j][k]);
                e += d * d;
            }
            if (e < error)
                error = e, best = j;
        }
        color |= best << (2 * i);
    }
    std::memcpy(dst + 12, &color, 4);
}
inline std::vector<uint8_t> encode(const Canvas &c) {
    std::vector<uint8_t> out(size_t(c.w) * c.h);
    for (unsigned y = 0; y < c.h; y += 4)
        for (unsigned x = 0; x < c.w; x += 4)
            encode_block(&c.pixels[(size_t(y) * c.w + x) * 4], c.w * 4,
                         &out[(size_t(y / 4) * (c.w / 4) + x / 4) * 16]);
    return out;
}
} // namespace nx::nvn::controller_art
