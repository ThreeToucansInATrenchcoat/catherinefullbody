#include "../src/nvn/interpolated-skin-output.h"
#include <array>
#include <cassert>
#include <cstdio>
using nx::nvn::replay::InterpolatedSkinOutputs;
using nx::nvn::replay::VerifiedSkinOutputs;

static void persistent_outputs_tests() {
    std::array<uint8_t, 256> guest{}, cpu{}, gpu{}, newer{};
    for (size_t i = 0; i < cpu.size(); ++i) {
        cpu[i] = uint8_t(i * 7 + 3);
        gpu[i] = uint8_t(i * 9 + 5);
        newer[i] = uint8_t(i * 11 + 7);
    }
    auto* p = guest.data();
    auto native = [](const uint8_t* bytes, uint64_t) { return bytes; };
    auto cpu_view = [&](const uint8_t* bytes, uint64_t) { return cpu.data() + (bytes - p); };
    auto unavailable = [](const uint8_t*, uint64_t) -> const uint8_t* { return nullptr; };
    VerifiedSkinOutputs outputs;
    outputs.begin_submit();
    outputs.produced(p, 4, 32, cpu.data());
    assert(outputs.contains(p, 12, cpu_view));
    assert(outputs.positions(p, 4, 32, cpu_view));
    assert(!outputs.positions(p, 4, 16, cpu_view));
    outputs.seal_write_back(cpu.data(), p, 128);
    std::memcpy(p, cpu.data(), 128);
    outputs.write_back(cpu.data(), p, 128);
    outputs.begin_submit();
    assert(outputs.contains(p, 12, native)); // Lifetime exceeds the upload submit/frame.
    guest[12] ^= 1; // Normals/padding do not alter position provenance.
    assert(outputs.contains(p, 12, native));
    guest[64] ^= 1; // Validate ALL positions, not just the queried first vertex.
    assert(!outputs.contains(p, 12, native));
    guest[64] ^= 1;
    assert(!outputs.contains(p, 12, native)); // A mismatch permanently expires that proof.

    outputs.produced(p, 4, 32, nullptr); // Known current GPU producer, not readable yet.
    assert(outputs.contains(p, 12, unavailable));
    outputs.seal_write_back(gpu.data(), p, 128);
    outputs.begin_submit();
    assert(!outputs.contains(p, 12, native)); // An unfenced older producer is never guessed.
    std::memcpy(p, gpu.data(), 128);
    outputs.write_back(gpu.data(), p, 128);
    assert(outputs.contains(p, 12, native));
    outputs.overwrite(p + 32, 32);
    assert(outputs.contains(p, 12, native));
    assert(!outputs.contains(p + 32, 12, native));
    assert(outputs.contains(p + 64, 12, native));

    outputs.produced(p, 4, 32, cpu.data());
    outputs.seal_write_back(cpu.data(), p, 128);
    outputs.produced(p, 4, 32, newer.data()); // Same address, newer submitted producer.
    outputs.seal_write_back(newer.data(), p, 128);
    outputs.begin_submit();
    std::memcpy(p, cpu.data(), 128);
    outputs.write_back(cpu.data(), p, 128);
    assert(!outputs.contains(p, 12, native)); // Old fence cannot certify the newer ticket.
    std::memcpy(p, newer.data(), 128);
    outputs.write_back(newer.data(), p, 128);
    assert(outputs.contains(p, 12, native));

    outputs.produced(p, 4, 32, cpu.data());
    outputs.seal_write_back(cpu.data(), p, 128);
    std::memcpy(p, cpu.data(), 128);
    guest[96] ^= 1; // Guest changed an unchanged write-back lane while recording.
    outputs.write_back(cpu.data(), p, 128);
    outputs.begin_submit();
    assert(!outputs.contains(p, 12, native));

    // Splitting an already sealed pending producer must adjust its source
    // ticket, so unaffected suffix vertices can still be verified at the fence.
    outputs.produced(p, 4, 32, gpu.data());
    outputs.seal_write_back(gpu.data(), p, 128);
    outputs.overwrite(p + 32, 32);
    std::memcpy(p, gpu.data(), 128);
    outputs.write_back(gpu.data(), p, 128);
    outputs.begin_submit();
    assert(outputs.contains(p, 12, native));
    assert(!outputs.contains(p + 32, 12, native));
    assert(outputs.contains(p + 64, 12, native));
}

int main() {
    persistent_outputs_tests();
    std::array<uint8_t, 2048> allocation{};
    auto* p = allocation.data();
    InterpolatedSkinOutputs outputs;
    // CPU success and GPU fallback publish through the same dispatch state.
    // A CPU rejection after palette substitution preserves the flag until the
    // fallback succeeds, so there is no false second c3 interpolation.
    outputs.begin_dispatch(1);
    outputs.palette_interpolated();
    outputs.overwrite(p, 320);
    outputs.complete_skin(p, 320, true);
    assert(outputs.contains(p, 12, 1));
    assert(outputs.contains(p + 288, 32, 1));
    assert(!outputs.contains(p + 316, 12, 1));
    // A native/local skin palette must not inherit the prior dispatch's flag.
    outputs.begin_dispatch(1);
    outputs.overwrite(p + 512, 320);
    outputs.complete_skin(p + 512, 320, true);
    assert(!outputs.contains(p + 512, 12, 1));
    assert(outputs.contains(p, 12, 1));
    // Morph/local results remain attachments, including reused skin storage.
    outputs.begin_dispatch(1);
    outputs.overwrite(p + 96, 64);
    outputs.complete_skin(p + 96, 64, false);
    assert(outputs.contains(p, 96, 1));
    assert(!outputs.contains(p + 96, 12, 1));
    assert(outputs.contains(p + 160, 160, 1));
    assert(!outputs.contains(p + 80, 96, 1));
    // An aborted dispatch cannot leak its palette flag into the next one.
    outputs.begin_dispatch(1);
    outputs.palette_interpolated();
    outputs.begin_dispatch(1);
    outputs.complete_skin(p + 1024, 320, true);
    assert(!outputs.contains(p + 1024, 12, 1));
    // A large bound allocation can contain independent native and world parts.
    outputs.begin_dispatch(1);
    outputs.palette_interpolated();
    outputs.complete_skin(p + 512, 320, true);
    assert(outputs.contains(p + 512, 12, 1));
    assert(!outputs.contains(p + 900, 12, 1));
    assert(!outputs.contains(p, allocation.size(), 1));
    // Adjacent production spans join for range queries without filling holes.
    outputs.complete_skin(p + 832, 32, true);
    assert(outputs.contains(p + 512, 352, 1));
    assert(!outputs.contains(p + 512, 353, 1));
    // Both frame and submit boundaries invalidate address reuse.
    assert(!outputs.contains(p + 512, 12, 2));
    outputs.begin_dispatch(2);
    outputs.palette_interpolated();
    outputs.complete_skin(p, 320, true);
    outputs.clear();
    assert(!outputs.contains(p, 12, 2));
    assert(!outputs.contains(nullptr, 12, 2));
    assert(!outputs.contains(p, 0, 2));
    outputs.begin_dispatch(2);
    outputs.palette_interpolated();
    auto* overflow = reinterpret_cast<const uint8_t*>(UINTPTR_MAX - 3);
    outputs.complete_skin(overflow, 12, true);
    assert(!outputs.contains(overflow, 12, 2));
    puts("PASS: substituted world skin only; native/local and morph remain attachments; "
         "exact range coverage; partial overwrite; CPU-to-GPU fallback flag; "
         "aborted dispatch/current-frame resets; verified position persistence across submits; "
         "full-stream validation, normal-only edits, pending GPU fences, writeback tickets and guest races; invalid ranges rejected.");
}
