#include "../src/nvn/character_pose_coherence.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace catherine::visual;
using Matrix = std::array<float, 16>;
using Palette = std::array<float, 103 * 16>;
static Matrix identity() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
static Matrix pose(float angle, float x = 0, float y = 0, float z = 0) {
  Matrix m = identity();
  float r = angle * 3.14159265358979323846f / 180;
  m[0] = std::cos(r);
  m[1] = -std::sin(r);
  m[4] = std::sin(r);
  m[5] = std::cos(r);
  m[3] = x;
  m[7] = y;
  m[11] = z;
  return m;
}
static Matrix product(const Matrix &a, const Matrix &b) {
  Matrix m;
  multiply_affine(a.data(), b.data(), m.data());
  return m;
}
static Matrix inverse(const Matrix &a) {
  Matrix m;
  assert(inverse_affine(a.data(), m.data()));
  return m;
}
static Palette palette(const Matrix &head) {
  Palette p;
  for (size_t b = 0; b < 103; b++) {
    std::memcpy(p.data() + 16 * b, head.data(), 64);
    p[b * 16 + 15] = float(300 + b);
  }
  return p;
}
static uint64_t clock_at(unsigned tick, float fraction, bool enabled = true) {
  uint32_t bits;
  std::memcpy(&bits, &fraction, 4);
  return (uint64_t(tick) << 32) | bits | (enabled ? 0x80000000u : 0);
}
static const uint8_t *bytes(const Palette &p) {
  return reinterpret_cast<const uint8_t *>(p.data());
}
static const uint8_t *bytes(const Matrix &m) {
  return reinterpret_cast<const uint8_t *>(m.data());
}
static Matrix first(const uint8_t *p) {
  Matrix m;
  std::memcpy(m.data(), p, 64);
  m[12] = m[13] = m[14] = 0;
  m[15] = 1;
  return m;
}
static void close(const Matrix &a, const Matrix &b, float tolerance = .001f) {
  for (unsigned i = 0; i < 16; i++) {
    assert(std::isfinite(a[i]));
    assert(std::fabs(a[i] - b[i]) < tolerance);
  }
}
static void affine_tests() {
  Matrix m = {0, -3, 0, 100, 2, 0, 0, 200, 0, 0, 4, 300, 0, 0, 0, 1};
  Matrix expected = {0, .5f, 0,    -100, -1.f / 3, 0, 0, 100.f / 3,
                     0, 0,   .25f, -75,  0,        0, 0, 1};
  close(inverse(m), expected);
  close(product(m, inverse(m)), identity());
  auto alias = m;
  assert(inverse_affine(alias.data(), alias.data()));
  close(alias, expected);
  auto mirror = pose(180, 1, 2, 3);
  for (unsigned row = 0; row < 3; row++)
    mirror[row * 4] = -mirror[row * 4];
  close(product(mirror, inverse(mirror)), identity());
  auto singular = identity();
  singular[0] = 0;
  Matrix out;
  assert(!inverse_affine(singular.data(), out.data()));
  auto invalid = identity();
  invalid[3] = std::numeric_limits<float>::infinity();
  assert(!inverse_affine(invalid.data(), out.data()));
}
static void coherent_head_turns() {
  // Nonidentity bind pose: P=H*B^-1. The attachment correction must cancel B,
  // including its rotation and translation, rather than treating P as H.
  const Matrix bind = pose(12, 2, 3, 1), inv_bind = inverse(bind);
  const Matrix horn_left = pose(4, -7, 5, 4), horn_right = pose(-4, 7, 5, 4),
               face = pose(110, 0, 2, 8), hair = pose(-110, 0, 7, 0);
  for (float end_angle : {90.f, 180.f, -90.f}) {
    CharacterPoseCoherence cache;
    auto p0 = palette(product(pose(0, 100, 200, 300), inv_bind));
    auto p1 = palette(product(pose(end_angle, 112, 202, 304), inv_bind));
    auto original0 = p0, original1 = p1;
    cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0));
    const float *prior_model = nullptr;
    Matrix saved_model;
    for (unsigned k = 0; k < 8; k++) {
      uint64_t frame = k + 2, clock = clock_at(2, k / 8.f);
      const uint8_t *rendered =
          cache.palette(bytes(p1), sizeof p1, frame, clock);
      const uint8_t *reused = cache.palette(bytes(p1), sizeof p1, frame, clock);
      assert(reused == rendered);
      assert(cache.shared_meshes > 0);
      const Matrix rendered_world = product(first(rendered), bind);
      for (const Matrix *local : {&horn_left, &horn_right, &face, &hair}) {
        const Matrix raw_world =
            product(pose(end_angle, 112, 202, 304), *local);
        auto anchored = cache.head_model(bytes(raw_world), frame, clock);
        assert(anchored);
        const Matrix actual = first(anchored),
                     expected = product(rendered_world, *local);
        close(actual, expected, .002f);
        // Check its local offset and full orientation in rendered head space.
        close(product(inverse(rendered_world), actual), *local, .002f);
        // Earlier uploads retain their storage while later attachments
        // allocate.
        if (prior_model && k == 0)
          close(first(reinterpret_cast<const uint8_t *>(prior_model)),
                saved_model);
        prior_model = reinterpret_cast<const float *>(anchored);
        saved_model = actual;
      }
      prior_model = nullptr;
    }
    assert(p0 == original0 && p1 == original1);
    assert(cache.head_models == 32);
  }
}
static void canonical_and_variants() {
  CharacterPoseCoherence cache;
  auto p0 = palette(pose(0, 100, 200, 300));
  auto p1 = palette(pose(30, 110, 200, 300));
  cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0));
  auto first_result = cache.palette(bytes(p1), sizeof p1, 2, clock_at(2, .5));
  Palette expected;
  std::memcpy(expected.data(), first_result, sizeof expected);
  // A distinct limb pose at the same head origin must never borrow the
  // canonical skeleton. It can belong to a coincident actor or another bind.
  auto variant = p1;
  variant[16 + 3] += 2;
  variant[16 + 15] = std::numeric_limits<float>::quiet_NaN();
  auto result =
      cache.palette(bytes(variant), sizeof variant, 2, clock_at(2, .5));
  assert(result == bytes(variant));
  assert(std::memcmp(first_result, expected.data(), sizeof expected) == 0);
  assert(cache.variant_palettes == 1 && cache.variant_max_error == 2);
  assert(cache.rejected_palettes == 1);
  // Padding alone is diagnosed as a variant but cannot inflate pose error.
  auto padding = p1;
  padding[15] = std::numeric_limits<float>::infinity();
  cache.palette(bytes(padding), sizeof padding, 2, clock_at(2, .5));
  assert(cache.variant_max_error == 2);
  for (unsigned sample = 0; sample < 6; ++sample)
    cache.palette(bytes(padding), sizeof padding, 2, clock_at(2, .5));
  assert(cache.variant_palettes == 8);
  auto ninth = p1;
  ninth[16 + 3] += 100;
  cache.palette(bytes(ninth), sizeof ninth, 2, clock_at(2, .5));
  assert(cache.variant_palettes == 9 && cache.variant_max_error == 2);
  assert(cache.rejected_palettes == 2);
  auto far = pose(0, 1000, 200, 300);
  assert(!cache.head_model(bytes(far), 2, clock_at(2, .5)));
  // Real head meshes include fixed alternate bind orientations over 100
  // degrees.
  auto alternate_basis = pose(140, 110, 200, 300);
  assert(!cache.head_model(bytes(alternate_basis), 2, clock_at(2, .5)));
  assert(cache.ambiguous_head_models == 1);
  // Ambiguity lasts only for the conflicting render frame. It must not
  // discard valid actor identity/history or suppress future attachments.
  auto recovered = cache.palette(bytes(p1), sizeof p1, 3, clock_at(2, .75));
  assert(cache.head_model(bytes(alternate_basis), 3, clock_at(2, .75)));
  Matrix expected_head;
  blend_matrix(p0.data(), p1.data(), .75, expected_head.data());
  close(first(recovered), first(bytes(expected_head)));
  auto nearby = pose(30, 110, 200, 300);
  assert(!cache.head_model(bytes(nearby), 4, clock_at(2, .8)));
  assert(cache.near_head_stale > 0);
  assert(cache.palette(bytes(p1), sizeof p1, 5, clock_at(3, 0, false)) ==
         bytes(p1));
  assert(!cache.head_model(bytes(nearby), 5, clock_at(3, 0, false)));
  assert(cache.palette(bytes(p1), 64, 5, clock_at(3, .5)) == bytes(p1));
}
static void padding_roundoff_and_nonfinite_guard() {
  CharacterPoseCoherence cache;
  auto p = palette(pose(0, 100, 200, 300));
  auto canonical = cache.palette(bytes(p), sizeof p, 1, clock_at(1, 0));
  auto padding = p;
  for (size_t bone = 0; bone < 103; ++bone) {
    padding[16 * bone + 12] = std::numeric_limits<float>::quiet_NaN();
    padding[16 * bone + 13] = std::numeric_limits<float>::infinity();
    padding[16 * bone + 14] = -std::numeric_limits<float>::infinity();
    padding[16 * bone + 15] = -123;
  }
  assert(cache.palette(bytes(padding), sizeof padding, 1, clock_at(1, 0)) ==
         canonical);
  assert(cache.rejected_palettes == 0 && cache.variant_max_error == 0);
  auto roundoff = p;
  roundoff[16 + 3] += .0005f;
  roundoff[32 + 1] += 1e-6f;
  assert(cache.palette(bytes(roundoff), sizeof roundoff, 1, clock_at(1, 0)) ==
         canonical);
  assert(cache.rejected_palettes == 0);
  auto changed_basis = p;
  changed_basis[32 + 1] += 2e-5f;
  assert(cache.palette(bytes(changed_basis), sizeof changed_basis, 1,
                       clock_at(1, 0)) == bytes(changed_basis));
  assert(cache.rejected_palettes == 1);
  auto invalid = p;
  invalid[16 + 1] = std::numeric_limits<float>::quiet_NaN();
  assert(cache.palette(bytes(invalid), sizeof invalid, 1, clock_at(1, 0)) ==
         bytes(invalid));
  assert(cache.rejected_palettes == 2);
  auto model = pose(0, 100, 200, 300);
  assert(!cache.head_model(bytes(model), 1, clock_at(1, 0)));
  CharacterPoseCoherence fresh;
  assert(fresh.palette(bytes(invalid), sizeof invalid, 1, clock_at(1, 0)) ==
         bytes(invalid));
  assert(fresh.rejected_palettes == 1);
  assert(!fresh.head_model(bytes(model), 1, clock_at(1, 0)));
  // Invalid first input must not poison an actor or its interpolation history.
  fresh.palette(bytes(p), sizeof p, 2, clock_at(1, .5));
  assert(fresh.head_model(bytes(model), 2, clock_at(1, .5)));
}

static void source_snapshot_and_rollover() {
  CharacterPoseCoherence cache;
  auto p0 = palette(pose(0, 100, 200, 300));
  auto p1 = palette(pose(30, 110, 200, 300));
  cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0), 11);
  auto canonical = cache.palette(bytes(p1), sizeof p1, 2, clock_at(2, .5), 11);
  Palette captured;
  std::memcpy(captured.data(), canonical, sizeof captured);
  auto updated_in_place = p1;
  updated_in_place[16 + 3] += 2;
  assert(cache.palette(bytes(updated_in_place), sizeof updated_in_place, 2,
                       clock_at(2, .5), 11) == canonical);
  assert(cache.same_source_updates == 1 && cache.rejected_palettes == 0);
  assert(std::memcmp(canonical, captured.data(), sizeof captured) == 0);
  // An in-flight producer update can even expose partially written values;
  // sharing its already validated first snapshot must remain safe.
  updated_in_place[32 + 1] = std::numeric_limits<float>::quiet_NaN();
  assert(cache.palette(bytes(updated_in_place), sizeof updated_in_place, 2,
                       clock_at(2, .5), 11) == canonical);
  auto model = pose(140, 110, 200, 300);
  assert(cache.head_model(bytes(model), 2, clock_at(2, .5)));
  assert(cache.same_source_updates == 2 && cache.rejected_palettes == 0);
  auto other_source = p1;
  other_source[16 + 3] += 2;
  assert(cache.palette(bytes(other_source), sizeof other_source, 2,
                       clock_at(2, .5), 22) == canonical);
  assert(cache.rejected_palettes == 0 && cache.spatial_source_updates == 1);
  assert(cache.head_model(bytes(model), 2, clock_at(2, .5)));
  // Native upload storage rotates. A new key must retain spatial continuity
  // on the next frame and keep history phase.
  auto recovered = cache.palette(bytes(p1), sizeof p1, 3, clock_at(2, .75), 22);
  Matrix expected;
  blend_matrix(p0.data(), p1.data(), .75, expected.data());
  close(first(recovered), first(bytes(expected)));
  assert(cache.head_model(bytes(model), 3, clock_at(2, .75)));
  assert(cache.palette(bytes(other_source), sizeof other_source, 3,
                       clock_at(2, .75), 22) == recovered);
  assert(cache.rejected_palettes == 0);
  // A different storage address with an equal meaningful pose is also safe.
  auto alias = p1;
  alias[15] = std::numeric_limits<float>::quiet_NaN();
  assert(cache.palette(bytes(alias), sizeof alias, 3, clock_at(2, .75), 33) ==
         recovered);
  assert(cache.head_model(bytes(model), 3, clock_at(2, .75)));
}
static void source_preference_and_distant_reuse() {
  CharacterPoseCoherence cache;
  auto a0 = palette(pose(0, 100, 200, 300));
  auto b0 = palette(pose(0, 130, 200, 300));
  cache.palette(bytes(a0), sizeof a0, 1, clock_at(1, 0), 11);
  cache.palette(bytes(b0), sizeof b0, 1, clock_at(1, 0), 22);
  auto a1 = palette(pose(0, 125, 200, 300));
  // B's old position is closer, but the still-near matching producer is A.
  auto a = first(cache.palette(bytes(a1), sizeof a1, 2, clock_at(2, .5), 11));
  close(a, pose(0, 112.5, 200, 300));
  auto b1 = palette(pose(0, 105, 200, 300));
  auto b = first(cache.palette(bytes(b1), sizeof b1, 2, clock_at(2, .5), 22));
  close(b, pose(0, 117.5, 200, 300));
  // The same native storage can be reused for a distant actor even within
  // one replay frame. It must not borrow the first actor's world pose.
  auto distant_same_frame = palette(pose(0, 500, 200, 300));
  auto same_frame_fresh =
      first(cache.palette(bytes(distant_same_frame), sizeof distant_same_frame,
                          2, clock_at(2, .5), 11));
  close(same_frame_fresh, pose(0, 500, 200, 300));
  // Reused producer storage far from its previous actor starts a fresh pose.
  auto distant = palette(pose(0, 900, 200, 300));
  auto fresh = first(
      cache.palette(bytes(distant), sizeof distant, 3, clock_at(3, .5), 11));
  close(fresh, pose(0, 900, 200, 300));
}

static void verified_source_aliases() {
  for (bool padding_only : {false, true}) {
    CharacterPoseCoherence cache;
    auto p0 = palette(pose(0, 100, 200, 300));
    auto initial = cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0), 11);
    auto equal_alias = p0;
    if (padding_only)
      equal_alias[15] = std::numeric_limits<float>::quiet_NaN();
    // Both memcmp equality and meaningful equality establish source lineage.
    assert(cache.palette(bytes(equal_alias), sizeof equal_alias, 1,
                         clock_at(1, 0), 22) == initial);
    auto p1 = palette(pose(30, 110, 200, 300));
    auto canonical =
        cache.palette(bytes(p1), sizeof p1, 2, clock_at(2, .5), 11);
    auto next_producer_pose = palette(pose(31, 113, 200, 300));
    // A known alternate producer has advanced3 units during this replay
    // frame. It still uses the first snapshot; the0.5-unit unknown-key test
    // must not split a verified actor into two instances.
    assert(cache.palette(bytes(next_producer_pose), sizeof next_producer_pose,
                         2, clock_at(2, .5), 22) == canonical);
    assert(cache.alias_source_updates == 1 && cache.rejected_palettes == 0);
    auto model = pose(140, 110, 200, 300);
    assert(cache.head_model(bytes(model), 2, clock_at(2, .5)));
    auto unproven_result =
        cache.palette(bytes(next_producer_pose), sizeof next_producer_pose, 2,
                      clock_at(2, .5), 33);
    assert(unproven_result != canonical);
    close(first(unproven_result), first(bytes(next_producer_pose)));
    // A new native buffer near the same avatar shares its first snapshot.
    // Its late producer update must not force a raw face/body fallback.
    auto coincident = p1;
    coincident[16 + 3] += 2;
    assert(cache.palette(bytes(coincident), sizeof coincident, 2,
                         clock_at(2, .5), 44) == canonical);
    assert(cache.rejected_palettes == 0 && cache.spatial_source_updates == 1);
    assert(cache.head_model(bytes(model), 2, clock_at(2, .5)));
    // Unkeyed legacy callers retain strict conflict/ambiguity protection.
    assert(cache.palette(bytes(coincident), sizeof coincident, 2,
                         clock_at(2, .5), 0) == bytes(coincident));
    assert(cache.rejected_palettes == 1);
    assert(!cache.head_model(bytes(model), 2, clock_at(2, .5)));
    // A proven alias reused for a distant actor is outside the75-unit bound.
    auto distant = palette(pose(0, 500, 200, 300));
    auto distant_result =
        cache.palette(bytes(distant), sizeof distant, 2, clock_at(2, .5), 22);
    assert(distant_result != canonical);
    close(first(distant_result), first(bytes(distant)));
    assert(cache.alias_source_updates == 1);
  }
}
static void source_alias_continuity_and_expiry() {
  CharacterPoseCoherence cache;
  auto p0 = palette(pose(0, 100, 200, 300));
  auto p1 = palette(pose(30, 110, 200, 300));
  cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0), 11);
  cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0), 22);
  auto advanced = palette(pose(31, 113, 200, 300));
  auto model = pose(140, 110, 200, 300);
  // A proven producer can advance3 units for twenty consecutive frames.
  for (uint64_t frame = 2; frame <= 21; ++frame) {
    auto canonical =
        cache.palette(bytes(p1), sizeof p1, frame, clock_at(2, .5), 11);
    assert(cache.palette(bytes(advanced), sizeof advanced, frame,
                         clock_at(2, .5), 22) == canonical);
    assert(cache.head_model(bytes(model), frame, clock_at(2, .5)));
  }
  assert(cache.alias_source_updates == 20 && cache.rejected_palettes == 0);
  // After nine absent frames that key is no longer trusted across3 units.
  // It starts its own pose outside the unknown-native0.5-unit radius.
  for (uint64_t frame = 22; frame <= 30; ++frame)
    cache.palette(bytes(p1), sizeof p1, frame, clock_at(2, .5), 11);
  auto canonical = cache.palette(bytes(p1), sizeof p1, 30, clock_at(2, .5), 11);
  auto returned =
      cache.palette(bytes(advanced), sizeof advanced, 30, clock_at(2, .5), 22);
  assert(returned != canonical);
  close(first(returned), first(bytes(advanced)));
  assert(cache.alias_source_updates == 20 && cache.rejected_palettes == 0);
}
static void advancing_unknown_native_source() {
  CharacterPoseCoherence cache;
  auto p0 = palette(pose(0, 100, 200, 300));
  auto p1 = palette(pose(30, 110, 200, 300));
  cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0), 11);
  auto canonical = cache.palette(bytes(p1), sizeof p1, 2, clock_at(2, .5), 11);
  auto advancing = palette(pose(30.2f, 110.25f, 200, 300));
  assert(cache.palette(bytes(advancing), sizeof advancing, 2, clock_at(2, .5),
                       22) == canonical);
  assert(cache.spatial_source_updates == 1 && cache.rejected_palettes == 0);
  auto model = pose(140, 110, 200, 300);
  assert(cache.head_model(bytes(model), 2, clock_at(2, .5)));
  // The newly accepted native producer now has ordinary source continuity.
  auto farther_update = palette(pose(31, 113, 200, 300));
  assert(cache.palette(bytes(farther_update), sizeof farther_update, 2,
                       clock_at(2, .5), 22) == canonical);
  assert(cache.alias_source_updates == 1);
}

static void actor_separation_and_invalid_head() {
  CharacterPoseCoherence cache;
  auto a0 = palette(pose(0, 100, 200, 300));
  auto b0 = palette(pose(0, 500, 200, 300));
  cache.palette(bytes(a0), sizeof a0, 1, clock_at(1, 0));
  cache.palette(bytes(b0), sizeof b0, 1, clock_at(1, 0));
  auto a1 = palette(pose(45, 110, 200, 300)),
       b1 = palette(pose(-45, 490, 200, 300));
  Matrix rb = first(cache.palette(bytes(b1), sizeof b1, 2, clock_at(2, .5))),
         ra = first(cache.palette(bytes(a1), sizeof a1, 2, clock_at(2, .5)));
  Matrix local = pose(0, 3, 5, 0), ma = product(pose(45, 110, 200, 300), local),
         mb = product(pose(-45, 490, 200, 300), local);
  close(first(cache.head_model(bytes(ma), 2, clock_at(2, .5))),
        product(ra, local));
  close(first(cache.head_model(bytes(mb), 2, clock_at(2, .5))),
        product(rb, local));
  CharacterPoseCoherence invalid;
  auto singular = identity();
  singular[0] = 0;
  auto p = palette(singular);
  invalid.palette(bytes(p), sizeof p, 1, clock_at(1, 0));
  auto m = identity();
  assert(!invalid.head_model(bytes(m), 1, clock_at(1, 0)));
  auto bad = ma;
  bad[3] = std::numeric_limits<float>::quiet_NaN();
  assert(!cache.head_model(bytes(bad), 2, clock_at(2, .5)));
}
static Matrix palette_bone(const uint8_t *p, size_t index) {
  return first(p + index * 64);
}
static Palette head_and_tongue(const Matrix &head, const Matrix &tongue_local) {
  Palette p = palette(head);
  Matrix tongue = product(head, tongue_local);
  std::memcpy(p.data(), tongue.data(), 64);
  return p;
}
static void actual_head_instead_of_tongue() {
  CharacterPoseCoherence cache;
  Matrix first_head = pose(0, 100, 200, 300),
         next_head = pose(30, 110, 202, 304);
  auto p0 = head_and_tongue(first_head, pose(0, 2, 0, 0));
  auto p1 = head_and_tongue(next_head, pose(120, 2, 0, 0));
  Matrix local = pose(110, -4, 3, 5);
  cache.palette(bytes(p0), sizeof p0, 1, clock_at(1, 0), 11);
  auto rendered = cache.palette(bytes(p1), sizeof p1, 2, clock_at(2, .5), 11);
  Matrix native_model = product(next_head, local);
  auto result = cache.head_model(bytes(native_model), 2, clock_at(2, .5));
  assert(result);
  close(first(result), product(palette_bone(rendered, 19), local), .003f);
  // Animated tongue0 differs strongly, but is not the attachment parent.
  Matrix wrong = product(
      product(palette_bone(rendered, 0), inverse(palette_bone(bytes(p1), 0))),
      native_model);
  float maximum = 0;
  for (size_t j = 0; j < 12; j++)
    maximum = std::max(maximum, std::fabs(wrong[j] - first(result)[j]));
  assert(maximum > 1);
}
static void stable_attachment_and_anomaly() {
  CharacterPoseCoherence cache;
  constexpr uint64_t geometry = 0x98452b590ae291abull;
  Matrix local = pose(110, -7, 4, 5), head = pose(0, 100, 200, 300);
  const uint8_t *rendered = nullptr;
  for (uint64_t frame = 1; frame <= 4; frame++) {
    head = pose(float(frame) * 8, 100 + float(frame), 200, 300);
    auto p = palette(head);
    rendered = cache.palette(bytes(p), sizeof p, frame,
                             clock_at(unsigned(frame), .5), 11);
    Matrix model = product(head, local);
    auto result = cache.head_model(bytes(model), frame,
                                   clock_at(unsigned(frame), .5), geometry);
    assert(result);
    close(first(result), product(palette_bone(rendered, 19), local), .003f);
  }
  head = pose(40, 105, 200, 300);
  auto p = palette(head);
  rendered = cache.palette(bytes(p), sizeof p, 5, clock_at(5, .5), 11);
  Matrix next_native = product(pose(43, 109, 201, 300), local);
  auto fixed =
      cache.head_model(bytes(next_native), 5, clock_at(5, .5), geometry);
  assert(fixed);
  Matrix expected = product(palette_bone(rendered, 19), local);
  close(first(fixed), expected, .003f);
  Matrix captured = first(fixed);
  // Later geometry entries/rehashes and scratch allocations cannot overwrite
  // the stable attachment result before its upload consumes it.
  for (uint64_t i = 1; i <= 100; i++) {
    Matrix extra = product(head, pose(0, float(i % 4), 2, 3));
    assert(cache.head_model(bytes(extra), 5, clock_at(5, .5), geometry + i));
  }
  close(first(fixed), captured);
  head = pose(48, 106, 200, 300);
  p = palette(head);
  rendered = cache.palette(bytes(p), sizeof p, 6, clock_at(6, .5), 11);
  Matrix normal = product(head, local);
  fixed = cache.head_model(bytes(normal), 6, clock_at(6, .5), geometry);
  assert(fixed);
  close(first(fixed), product(palette_bone(rendered, 19), local), .003f);
  // 30FPS disables and clears the learned bind. A fresh local after resuming
  //  must be observed anew instead of inheriting the old calibrated geometry.
  assert(cache.palette(bytes(p), sizeof p, 7, clock_at(7, 0, false), 11) ==
         bytes(p));
  assert(!cache.head_model(bytes(normal), 7, clock_at(7, 0, false), geometry));
  rendered = cache.palette(bytes(p), sizeof p, 8, clock_at(8, .5), 11);
  Matrix replacement_local = pose(110, -2, 4, 5),
         replacement = product(head, replacement_local);
  fixed = cache.head_model(bytes(replacement), 8, clock_at(8, .5), geometry);
  assert(fixed);
  close(first(fixed), product(palette_bone(rendered, 19), replacement_local),
        .003f);
}
static void moving_local_never_calibrates() {
  CharacterPoseCoherence cache;
  constexpr uint64_t geometry = 0x2222;
  Matrix head = pose(0, 100, 200, 300);
  auto p = palette(head);
  for (uint64_t frame = 1; frame <= 7; frame++) {
    auto rendered =
        cache.palette(bytes(p), sizeof p, frame, clock_at(1, .5), 11);
    Matrix local = pose(float(frame) * 10, float(frame), 3, 4),
           model = product(head, local);
    // Four calls in one frame are one calibration observation, not four.
    for (unsigned draw = 0; draw < 4; draw++) {
      auto result =
          cache.head_model(bytes(model), frame, clock_at(1, .5), geometry);
      assert(result);
      close(first(result), product(palette_bone(rendered, 19), local), .003f);
    }
  }
  auto rendered = cache.palette(bytes(p), sizeof p, 8, clock_at(1, .5), 11);
  Matrix changed_local = pose(80, 12, 3, 4),
         model = product(head, changed_local);
  auto result = cache.head_model(bytes(model), 8, clock_at(1, .5), geometry);
  assert(result);
  close(first(result), product(palette_bone(rendered, 19), changed_local),
        .003f);
}
static void calibrated_bind_beats_nearest_actor() {
  CharacterPoseCoherence cache;
  constexpr uint64_t geometry = 0x98452b590ae291abull;
  const float separation = 16.77f, near_distance = 10.11f,
              correct_distance = 10.75f;
  const float x =
      (near_distance * near_distance - correct_distance * correct_distance +
       separation * separation) /
      (2 * separation);
  const float y = std::sqrt(near_distance * near_distance - x * x);
  Matrix local = pose(110, x - separation, y, 0),
         calibration_head = pose(0, 90, 200, 300);
  auto calibration = palette(calibration_head);
  for (uint64_t frame = 1; frame <= 4; frame++) {
    cache.palette(bytes(calibration), sizeof calibration, frame, clock_at(1, 0),
                  11);
    Matrix model = product(calibration_head, local);
    assert(cache.head_model(bytes(model), frame, clock_at(1, 0), geometry));
  }
  Matrix head_a = pose(0, 100, 200, 300),
         head_b = pose(0, 100 + separation, 200, 300);
  auto a = palette(head_a), b = palette(head_b);
  auto rendered_a = cache.palette(bytes(a), sizeof a, 5, clock_at(2, .25), 11);
  Matrix head_a_rendered = palette_bone(rendered_a, 19);
  auto rendered_b = cache.palette(bytes(b), sizeof b, 5, clock_at(2, .25), 22);
  Matrix head_b_rendered = palette_bone(rendered_b, 19);
  Matrix first_pass = product(head_a, local),
         second_pass = product(head_b, local);
  auto first_result =
      cache.head_model(bytes(first_pass), 5, clock_at(2, .25), geometry);
  assert(first_result);
  close(first(first_result), product(head_a_rendered, local), .003f);
  const float da =
      std::hypot(second_pass[3] - head_a[3], second_pass[7] - head_a[7]);
  const float db =
      std::hypot(second_pass[3] - head_b[3], second_pass[7] - head_b[7]);
  assert(std::fabs(da - near_distance) < .001f &&
         std::fabs(db - correct_distance) < .001f && da < db);
  auto second_result =
      cache.head_model(bytes(second_pass), 5, clock_at(2, .25), geometry);
  assert(second_result);
  close(first(second_result), product(head_b_rendered, local), .003f);
  // A wrong-nearest selection would render this mesh on actor A, over20u off.
  assert(std::fabs(first(second_result)[3] -
                   product(head_a_rendered, local)[3]) > 20);
}

static void captured_old_anchor_bind_race() {
  // Actual57015/57016 replay float data. That diagnostic build logged tongue0,
  // not authored head19; remap its matrices to our synthetic head slots to
  // reproduce the anchor-agnostic candidate-selection race without claiming
  // these captured matrices themselves represent the authored Head joint.
  constexpr uint64_t geometry = 0x98452b590ae291abull;
  const Matrix reference_head = {-0.047090333f,
                                 0.987642169f,
                                 0.149135903f,
                                 1295.7594f,
                                 0.0165326223f,
                                 -0.148540482f,
                                 0.988750279f,
                                 1444.24939f,
                                 0.998712063f,
                                 0.0490057953f,
                                 -0.00929085258f,
                                 1203.60522f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 1.0f};
  const Matrix reference_model = {-0.000445223559f,
                                  0.969955921f,
                                  0.243066519f,
                                  1302.87146f,
                                  0.000592346885f,
                                  -0.24309887f,
                                  0.969983041f,
                                  1451.54504f,
                                  0.999956727f,
                                  0.000550309662f,
                                  -0.000426119601f,
                                  1207.02356f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  1.0f};
  const Matrix head_a = {-0.0466498882f,
                         0.987639129f,
                         0.149484202f,
                         1279.75732f,
                         0.0159462113f,
                         -0.148904935f,
                         0.988712072f,
                         1447.98022f,
                         0.998766482f,
                         0.0484988056f,
                         -0.00878439005f,
                         1205.50171f,
                         0.0f,
                         0.0f,
                         0.0f,
                         1.0f};
  const Matrix head_b = {-0.0466449037f,
                         0.987580478f,
                         0.149690732f,
                         1263.453f,
                         0.0163607206f,
                         -0.149098933f,
                         0.988661885f,
                         1444.18921f,
                         0.998745143f,
                         0.0485517532f,
                         -0.00916698948f,
                         1204.46033f,
                         0.0f,
                         0.0f,
                         0.0f,
                         1.0f};
  const Matrix model_a = {-8.53008896e-06f,
                          0.969897985f,
                          0.243415073f,
                          1286.87354f,
                          -1.06889302e-05f,
                          -0.243429005f,
                          0.96990782f,
                          1455.27161f,
                          0.999982059f,
                          -4.87532998e-06f,
                          2.95330155e-05f,
                          1208.92126f,
                          0.0f,
                          0.0f,
                          0.0f,
                          1.0f};
  const Matrix model_b = {-8.21725098e-06f,
                          0.969819784f,
                          0.243615061f,
                          1270.57043f,
                          0.000394345581f,
                          -0.243637562f,
                          0.969840884f,
                          1451.48022f,
                          0.999966681f,
                          8.54224782e-05f,
                          -0.00034630351f,
                          1207.87695f,
                          0.0f,
                          0.0f,
                          0.0f,
                          1.0f};
  CharacterPoseCoherence cache;
  auto reference = palette(reference_head);
  Matrix local = product(inverse(reference_head), reference_model);
  for (uint64_t frame = 1; frame <= 4; frame++) {
    cache.palette(bytes(reference), sizeof reference, frame, clock_at(1, 0),
                  11);
    assert(cache.head_model(bytes(reference_model), frame, clock_at(1, 0),
                            geometry));
  }
  auto a = palette(head_a), b = palette(head_b);
  Matrix rendered_a = palette_bone(
      cache.palette(bytes(a), sizeof a, 5, clock_at(2, .5), 11), 19);
  Matrix rendered_b = palette_bone(
      cache.palette(bytes(b), sizeof b, 5, clock_at(2, .5), 22), 19);
  auto first_pass =
      cache.head_model(bytes(model_a), 5, clock_at(2, .5), geometry);
  assert(first_pass);
  close(first(first_pass), product(rendered_a, local), .003f);
  float nearest_a = 0, nearest_b = 0, separation = 0;
  for (size_t i : {3u, 7u, 11u}) {
    nearest_a += (model_b[i] - head_a[i]) * (model_b[i] - head_a[i]);
    nearest_b += (model_b[i] - head_b[i]) * (model_b[i] - head_b[i]);
    separation += (head_b[i] - head_a[i]) * (head_b[i] - head_a[i]);
  }
  assert(std::fabs(std::sqrt(separation) - 16.77f) < .02f);
  assert(nearest_a < nearest_b);
  auto second_pass =
      cache.head_model(bytes(model_b), 5, clock_at(2, .5), geometry);
  assert(second_pass);
  close(first(second_pass), product(rendered_b, local), .003f);
}

int main() {
  affine_tests();
  coherent_head_turns();
  canonical_and_variants();
  padding_roundoff_and_nonfinite_guard();
  source_snapshot_and_rollover();
  source_preference_and_distant_reuse();
  verified_source_aliases();
  source_alias_continuity_and_expiry();
  advancing_unknown_native_source();
  actor_separation_and_invalid_head();
  actual_head_instead_of_tongue();
  stable_attachment_and_anomaly();
  moving_local_never_calibrates();
  calibrated_bind_beats_nearest_actor();
  captured_old_anchor_bind_race();
  puts("PASS: affine/inverse-bind cancellation; rigid horn/face/hair offsets "
       "coherent through 90/180 turns and 110-degree bind bases; canonical "
       "shared palette; conflicting palettes preserved; frame-local ambiguity; "
       "padding/roundoff/nonfinite guards; scratch lifetime; variant "
       "diagnostics ignore "
       "padding;30FPS bypass; distant blocks/stale/singular rejected; "
       "separated actors; source snapshots/rollover/reuse; verified "
       "aliases/expiry; actual head19; calibrated attachment cache; actor "
       "residual selection.");
}
