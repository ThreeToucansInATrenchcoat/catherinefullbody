#pragma once
#include "block20_owner_bridge.h"
#include <cstring>

namespace nx::nvn {
inline constexpr uint32_t block20_command_magic = 0x42323052u;
struct Block20CommandPacket {
    CatherineBlock20Packet owner{};
    uint64_t address = 0, size = 0;
};

// Called while recording the exact storage bind in the native skin job scope.
// record() owns a deep copy; replay never follows any of the guest pointers.
inline bool block20_capture_command(const Cmd& cmd, Block20CommandPacket& out) {
    if (cmd.op != Cmd::BindStorageBuffer || cmd.i[0] != 5 ||
        cmd.i[1] != 0 || (cmd.a[1] != 1280 && cmd.a[1] != 960))
        return false;
    CatherineBlock20Packet packet{};
    if (!catherine_block20_job_snapshot(&packet) || !packet.valid ||
        packet.palette_address != cmd.a[0] ||
        (cmd.a[1]==960 ? packet.type!=12 : (packet.type!=2&&packet.type!=3)))
        return false;
    out.owner = packet;
    out.address = cmd.a[0];
    out.size = cmd.a[1];
    return true;
}

struct Block20ReplayPacket {
    Block20CommandPacket command{};
    bool valid = false;
};
inline Block20ReplayPacket& block20_replay_packet() {
    static thread_local Block20ReplayPacket packet;
    return packet;
}
inline void block20_clear_replay_packet() { block20_replay_packet() = {}; }

// Every ordinary OR deferred compute palette bind must call this, with nullptr
// for unqualified binds. A stale packet cannot survive a replacement binding.
inline void block20_apply_command(int stage, int index, uint64_t address,
                                  uint64_t size, const uint8_t* blob) {
    if (stage != 5 || index != 0) return;
    auto& replay = block20_replay_packet();
    replay = {};
    if (!blob || (size != 1280 && size != 960)) return;
    Block20CommandPacket packet;
    std::memcpy(&packet, blob, sizeof packet);
    if (!packet.owner.valid || !packet.owner.lifetime ||
        packet.address != address || packet.size != size ||
        packet.owner.palette_address != address ||
        (size==960 ? packet.owner.type!=12 : (packet.owner.type!=2&&packet.owner.type!=3)))
        return;
    replay.command = packet;
    replay.valid = true;
}
inline const CatherineBlock20Packet* block20_bound_owner(uint64_t address,
                                                        uint64_t size) {
    const auto& replay = block20_replay_packet();
    return replay.valid && replay.command.address == address &&
                   replay.command.size == size
               ? &replay.command.owner : nullptr;
}
} // namespace nx::nvn
