#pragma once
#include "dev_loader_native.h"

/* The checkpoint's native SAVETIME entry creates its first authored scene.
   Observe that scene's own playback or input-ready state. */
static int catherine_dev_world_ready(uint32_t target, int root)
{
    if (root == 13)
        return catherine_dev_loader_target_kind(target) == CATHERINE_DEV_ROOM &&
               catherine_dev_movie_active() && catherine_dev_movie_playing(14);

    if (root == 6) {
        if (target != 15093949 && target != 15936021)
            return 0;
        const uint64_t event = LD64(0x7000d000b8ull);
        return event && LD32(event + 0xc) == 360 && LD32(event + 0x10) == 20 &&
               LD32(event + 0x14) == 1 && (int32_t)LD32(event + 0x1c) > 0;
    }

    if ((root != 22 && root != 23) || catherine_dev_movie_active())
        return 0;
    const int kind = catherine_dev_loader_target_kind(target);
    if (root == 23 && kind != CATHERINE_DEV_BAR)
        return 0;
    if (root == 22 && kind != CATHERINE_DEV_LANDING && kind != CATHERINE_DEV_CHECKPOINT)
        return 0;
    if (target == 15093949 || target == 15936021)
        return 0;
    const uint64_t holder = LD64(0x7000cca2c8ull);
    const uint64_t field = holder ? LD64(holder) : 0;
    if (!field)
        return 0;
    const uint64_t owner = field + 0x80;
    const uint64_t first = LD64(owner + 0x588);
    const uint64_t end = LD64(owner + 0x590);
    const uint64_t active = LD64(owner + 0x5a0);
    return first && end == first + 4 &&
           ((LD32(first) == 1 && active == owner + 0x1f0) ||
            (LD32(first) == 7 && active == owner + 0x318 && LD32(active + 8) == 5));
}
