
#include "../src/nvn/character_pose_interpolation.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <limits>
#include <set>
using namespace catherine::visual;
static uint64_t clock_at(unsigned tick, float f, bool enabled = true) {
  uint32_t b;
  memcpy(&b, &f, 4);
  return (uint64_t(tick) << 32) | b | (enabled ? 0x80000000u : 0u);
}
static void baseline_tests() {
  std::vector<float> p(103 * 16);
  for (int i = 0; i < 103; i++) {
    p[i * 16] = p[i * 16 + 5] = p[i * 16 + 10] = 1;
    p[i * 16 + 15] = 1234;
  }
  auto input = p;
  PoseInterpolator h;
  h.render(1, (uint8_t *)p.data(), p.size() * 4, 1, clock_at(1, 0));
  for (int i = 0; i < 103; i++)
    p[i * 16 + 3] = 3;
  std::set<int> positions;
  for (int k = 0; k < 8; k++) {
    auto out = (const float *)h.render(1, (uint8_t *)p.data(), p.size() * 4,
                                       k + 2, clock_at(2, k / 8.f));
    assert(fabsf(out[3] - 3 * k / 8.f) < .00001);
    positions.insert(int(out[3] * 1000));
    assert(out[15] == 1234);
  }
  assert(positions.size() == 8);
  assert(p[3] == 3); // rendering never modifies game-owned data.
  auto repeated =
      h.render(1, (uint8_t *)p.data(), p.size() * 4, 9, clock_at(2, .875f));
  assert(fabsf(((float *)repeated)[3] - 2.625f) < .0001);
  auto at30 =
      h.render(1, (uint8_t *)p.data(), p.size() * 4, 10, clock_at(3, 0, false));
  assert(at30 == (uint8_t *)p.data());
  h.render(1, (uint8_t *)p.data(), p.size() * 4, 11, clock_at(4, 0));
  p[3] = 1000;
  assert(h.render(1, (uint8_t *)p.data(), p.size() * 4, 12, clock_at(5, .5f)) ==
         (uint8_t *)p.data());
  assert(h.render(1, (uint8_t *)p.data(), 64, 13, clock_at(6, .5f)) ==
         (uint8_t *)p.data());
  float a[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0},
        b[16] = {0, -1, 0, 10, 1, 0, 0, 20, 0, 0, 1, 30}, out[16];
  blend_matrix(a, b, .5f, out);
  assert(fabsf(out[0] - .7071068f) < .00001 &&
         fabsf(out[4] - .7071068f) < .00001);
  assert(out[3] == 5 && out[7] == 10 && out[11] == 15);
  for (int col = 0; col < 3; col++)
    assert(fabsf(out[col] * out[col] + out[4 + col] * out[4 + col] +
                 out[8 + col] * out[8 + col] - 1) < .00001);
  b[0] = -1;
  b[1] = 0;
  b[4] = 0;
  b[5] = -1;
  blend_matrix(a, b, .5f, out);
  for (int i = 0; i < 12; i++)
    assert(std::isfinite(out[i]));
  puts("PASS: 8 distinct render poses per30Hz step; shortest-path normalized "
       "rotation; translation; preserved padding; immutable game inputs; 30FPS "
       "bypass; teleport reset; other rigs bypass.");
}

using Matrix = std::array<float, 16>;
using Palette = std::array<float, 103 * 16>;
static Matrix identity() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
static Matrix turn(float degrees, float x = 0, float y = 0, float z = 0) {
  Matrix m = identity();
  float a = degrees * 3.14159265358979323846f / 180;
  m[0] = std::cos(a);
  m[2] = std::sin(a);
  m[8] = -std::sin(a);
  m[10] = std::cos(a);
  m[3] = x;
  m[7] = y;
  m[11] = z;
  return m;
}
static Matrix product(const Matrix &a, const Matrix &b) {
  Matrix m = identity();
  for (size_t r = 0; r < 3; r++) {
    for (size_t c = 0; c < 3; c++) {
      double value = 0;
      for (size_t k = 0; k < 3; k++)
        value += double(a[r * 4 + k]) * b[k * 4 + c];
      m[r * 4 + c] = float(value);
    }
    double value = a[r * 4 + 3];
    for (size_t k = 0; k < 3; k++)
      value += double(a[r * 4 + k]) * b[k * 4 + 3];
    m[r * 4 + 3] = float(value);
  }
  return m;
}
static Matrix bone(const float *p, size_t i = 0) {
  Matrix m;
  std::memcpy(m.data(), p + 16 * i, 64);
  return m;
}
static Matrix local_frame(size_t bone_index) {
  if (bone_index == 15)
    return identity();
  const size_t i = bone_index + 1;
  return turn(float(i % 9) * 3, float(i % 7) * 2, -float(i) * .45f,
              float(i % 5) * 3);
}
static Palette relative_palette(const Matrix &common, unsigned padding) {
  Palette p;
  for (size_t i = 0; i < 103; i++) {
    Matrix local = local_frame(i);
    Matrix m = product(common, local);
    std::memcpy(p.data() + i * 16, m.data(), 64);
    for (size_t j = 12; j < 16; j++) {
      uint32_t bits = 0x7fc00000u + padding + uint32_t(i * 4 + j);
      std::memcpy(p.data() + i * 16 + j, &bits, 4);
    }
  }
  return p;
}
static const uint8_t *bytes(const Palette &p) {
  return reinterpret_cast<const uint8_t *>(p.data());
}
static void close_affine(const Matrix &a, const Matrix &b,
                         float tolerance = .002f) {
  for (size_t j = 0; j < 12; j++) {
    assert(std::isfinite(a[j]));
    assert(std::fabs(a[j] - b[j]) <= tolerance);
  }
}
static void common_frame_turn_tests() {
  for (float angle : {90.f, 180.f, -90.f}) {
    Matrix a = turn(0, 1300, 1400, 1200), b = turn(angle, 1310, 1402, 1198);
    Palette pa = relative_palette(a, 1), pb = relative_palette(b, 500),
            original_a = pa, original_b = pb;
    PoseInterpolator h;
    h.render(21, bytes(pa), sizeof pa, 100, clock_at(1, 0));
    for (unsigned sample = 0; sample <= 8; sample++) {
      float t = sample / 8.f;
      auto result = reinterpret_cast<const float *>(
          h.render(21, bytes(pb), sizeof pb, 101 + sample, clock_at(2, t)));
      Matrix common;
      blend_matrix(a.data(), b.data(), t, common.data());
      for (size_t i = 0; i < 103; i++) {
        Matrix local = local_frame(i);
        close_affine(bone(result, i), product(common, local));
        assert(std::memcmp(result + 16 * i + 12, pb.data() + 16 * i + 12, 16) ==
               0);
        if (sample == 0)
          for (size_t j = 0; j < 12; j++)
            assert(result[16 * i + j] == pa[16 * i + j]);
        if (sample == 8)
          for (size_t j = 0; j < 12; j++)
            assert(result[16 * i + j] == pb[16 * i + j]);
      }
    }
    assert(std::memcmp(pa.data(), original_a.data(), sizeof pa) == 0);
    assert(std::memcmp(pb.data(), original_b.data(), sizeof pb) == 0);
    assert(h.render(21, bytes(pb), sizeof pb, 110, clock_at(3, 0, false)) ==
           bytes(pb));
  }
}
static void bind_pivot_test() {
  Palette a = relative_palette(identity(), 0);
  Matrix start = identity();
  std::memcpy(a.data() + 16, start.data(), 64);
  Palette b = a;
  Matrix pivot = identity();
  pivot[0] = -1;
  pivot[5] = -1;
  pivot[3] = 200;
  std::memcpy(b.data() + 16, pivot.data(), 64);
  PoseInterpolator h;
  h.render(31, bytes(a), sizeof a, 200, clock_at(1, 0));
  auto out = reinterpret_cast<const float *>(
      h.render(31, bytes(b), sizeof b, 201, clock_at(2, .5)));
  // Both skin endpoints preserve bind pivot(100,0,0). Direct world RT blending
  // wrongly moves it to(100,100,0); relative linear skinning keeps it fixed.
  assert(std::fabs(out[16] * 100 + out[19] - 100) < .001f);
  assert(std::fabs(out[20] * 100 + out[23]) < .001f);
  Matrix legacy;
  blend_matrix(a.data() + 16, b.data() + 16, .5, legacy.data());
  assert(std::fabs(legacy[4] * 100 + legacy[7]) > 99);
}
static void guarded_fallback_tests() {
  Palette a = relative_palette(identity(), 0),
          b = relative_palette(turn(0, 2, 0, 0), 1);
  a[15 * 16] = 0; // Noninvertible pelvis frame: finite world-linear fallback.
  PoseInterpolator h;
  h.render(41, bytes(a), sizeof a, 300, clock_at(1, 0));
  auto out = reinterpret_cast<const float *>(
      h.render(41, bytes(b), sizeof b, 301, clock_at(2, .5)));
  for (size_t i = 0; i < 103; i++)
    for (size_t j = 0; j < 12; j++)
      assert(out[i * 16 + j] ==
             float(double(a[i * 16 + j]) * .5 + double(b[i * 16 + j]) * .5));
  a = relative_palette(identity(), 0);
  a[4 * 16 + 1] = std::numeric_limits<float>::quiet_NaN();
  PoseInterpolator bad_previous;
  bad_previous.render(42, bytes(a), sizeof a, 400, clock_at(1, 0));
  assert(bad_previous.render(42, bytes(b), sizeof b, 401, clock_at(2, .5)) ==
         bytes(b));
  auto invalid = b;
  invalid[5 * 16 + 2] = std::numeric_limits<float>::infinity();
  assert(bad_previous.render(42, bytes(invalid), sizeof invalid, 402,
                             clock_at(3, .5)) == bytes(invalid));
}
#if defined(CATHERINE_POSE_DIAGNOSTICS)
static void diagnostic_path_tests() {
  Matrix a = turn(0, 100, 200, 300), b = turn(90, 110, 200, 300);
  Palette pa = relative_palette(a, 0), pb = relative_palette(b, 1);
  for (uint32_t option : {PoseQaLinearPalette, PoseQaLegacyPalette}) {
    {
      std::ofstream setting("pose-options.txt");
      setting << option;
    }
    uint64_t frame = 10000 + option;
    PoseInterpolator h;
    h.render(50, bytes(pa), sizeof pa, frame, clock_at(1, 0));
    auto out = reinterpret_cast<const float *>(
        h.render(50, bytes(pb), sizeof pb, frame + 1, clock_at(2, .5)));
    for (size_t i = 0; i < 103; i++) {
      Matrix expected;
      if (option == PoseQaLegacyPalette)
        blend_matrix(pa.data() + 16 * i, pb.data() + 16 * i, .5,
                     expected.data());
      else
        for (size_t j = 0; j < 12; j++)
          expected[j] =
              float(double(pa[16 * i + j]) * .5 + double(pb[16 * i + j]) * .5);
      close_affine(bone(out, i), expected, .00001f);
    }
    // Palette diagnostics must never change rigid model interpolation.
    PoseInterpolator rigid;
    rigid.render(51, reinterpret_cast<const uint8_t *>(a.data()), 64, frame + 2,
                 clock_at(1, 0), true);
    auto model = reinterpret_cast<const float *>(
        rigid.render(51, reinterpret_cast<const uint8_t *>(b.data()), 64,
                     frame + 3, clock_at(2, .5), true));
    Matrix expected;
    blend_matrix(a.data(), b.data(), .5, expected.data());
    close_affine(bone(model), expected, .00001f);
  }
  std::remove("pose-options.txt");
}
#endif
int main() {
  baseline_tests();
  common_frame_turn_tests();
  bind_pivot_test();
  guarded_fallback_tests();
#if defined(CATHERINE_POSE_DIAGNOSTICS)
  diagnostic_path_tests();
#endif
  puts("PASS: pelvis-frame90/180 turns preserve103 distinct bone offsets; "
       "bind-pivot regression; endpoints/padding; immutable sources; "
       "inverse/nonfinite fallbacks;30FPS bypass; rigid path unchanged.");
}
