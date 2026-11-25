#include "../src/nvn/character_pose_interpolation.h"
#include <cassert>
#include <cstdio>
using namespace catherine::visual;
uint64_t clk(unsigned tick, float t, bool on = true) {
  uint32_t b;
  memcpy(&b, &t, 4);
  return (uint64_t(tick) << 32) | b | (on ? 0x80000000u : 0);
}
int main() {
  uint8_t vertices1[128] = {1, 2, 3}, vertices2[256] = {1, 2, 3};
  assert(model_geometry_identity(vertices1, 128, 32) ==
         model_geometry_identity(vertices2, 256, 32));
  vertices2[4] = 7;
  assert(model_geometry_identity(vertices1, 128, 32) !=
         model_geometry_identity(vertices2, 256, 32));
  ModelInstanceTracker tracker;
  float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  auto left = tracker.resolve(7, t, 1);
  t[3] = 150;
  auto right = tracker.resolve(7, t, 1);
  assert(left != right);
  // Reversed draw order and different transient draw-buffer addresses do not
  // matter.
  t[3] = 145;
  assert(tracker.resolve(7, t, 2) == right);
  assert(tracker.resolve(7, t, 2) == right);
  t[3] = 0;
  assert(tracker.resolve(7, t, 2) == left);
  // A third instance enters view while the first is culled.
  t[3] = 300;
  auto extra = tracker.resolve(7, t, 3);
  assert(extra != left && extra != right);
  t[3] = 130;
  assert(tracker.resolve(7, t, 3) == right);
  t[3] = 0;
  assert(tracker.resolve(7, t, 4) == left);
  float a[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, b[16];
  memcpy(b, a, 64);
  PoseInterpolator h;
  for (int id = 0; id < 1000; id++)
    h.render(id, (uint8_t *)a, 64, 1, clk(1, 0), true);
  b[3] = 150;
  for (int n = 0; n < 8; n++) {
    float t = n / 8.f;
    auto p =
        (const float *)h.render(0, (uint8_t *)b, 64, n + 2, clk(2, t), true);
    assert(fabs(p[3] - 150 * t) < .0001);
    auto shadow = h.render(0, (uint8_t *)b, 64, n + 2, clk(2, t), true);
    assert(memcmp(shadow, p, 64) == 0);
    auto separate =
        (const float *)h.render(1, (uint8_t *)a, 64, n + 2, clk(2, t), true);
    assert(separate[3] == 0);
  }
  assert(b[3] == 150);
  b[7] = -90;
  auto fall =
      (const float *)h.render(0, (uint8_t *)b, 64, 10, clk(3, .5), true);
  assert(fabs(fall[7] + 45) < .0001);
  assert(h.render(0, (uint8_t *)b, 64, 11, clk(4, 0, false), true) ==
         (uint8_t *)b);
  assert(h.render(0, (uint8_t *)b, 64, 12, clk(5, .5)) == (uint8_t *)b);
  PoseInterpolator crowded;
  crowded.render(1, (uint8_t *)a, 64, 1, clk(1, 0), true);
  for (uint64_t id = 2; id < 6000; id++)
    crowded.render(id, (uint8_t *)a, 64, 2, clk(1, .5), true);
  float moved[16];
  memcpy(moved, a, 64);
  moved[3] = 150;
  auto retained = (const float *)crowded.render(1, (uint8_t *)moved, 64, 3,
                                                clk(2, .5), true);
  assert(fabsf(retained[3] - 75) < .0001f);
  puts("PASS: 1000 independently identified models;8 intermediate push/pull "
       "positions;falling translation;shared shadow/color result;immutable "
       "source;30FPS bypass;rig/model filtering.");
}
