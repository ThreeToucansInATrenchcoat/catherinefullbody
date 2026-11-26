#pragma once

#include <stdint.h>
#include "config_pc_host.h"

extern int catherine_config_pc_is_open(void);

// The optional modes publish a coroutine before their world/player actors exist.
// Its native PuzzleSetting owner uses mode 1 for Babel Pair and 4 for Colosseum
// (f_700011be10). Keep these selectors on individually assigned inputs too.
static inline int catherine_input_two_player_selector_active(void) {
    const uint64_t context=LD64(0x7000cced98ull);
    if(!context)return 0;
    const uint64_t head=LD64(context+0x1eb0ull);
    uint64_t node=head;
    for(unsigned left=256;node&&left;--left) {
        if(LD64(node+0x30ull)==0x700009dfc0ull) {
            const uint64_t work=LD64(node+0x48ull);
            if(work&&LD64(work)==0x70009f6ab8ull) {
                const uint64_t owner=LD64(work+0x10ull);
                if(owner&&LD64(owner)==0x7000aa5be8ull) {
                    const unsigned mode=LD32(owner+0x650ull);
                    if(mode==1||mode==4)return 1;
                }
            }
        }
        node=LD64(node+0x78ull);
        if(node==head)break;
    }
    return 0;
}

// Empty-root optional selectors have no world/player manager. A loaded world,
// including the two native Babel/Colosseum actors, retains gameplay ownership.
static inline int catherine_input_optional_menu_active(void) {
    const uint64_t slot=LD64(0x7000cca5d8ull);
    const uint64_t world=slot?LD64(slot):0;
    if(!world)return 1;
    // The observed optional-mode world type owns P1/P2 at +88/+90. Unknown
    // world types stay in gameplay; never infer a selector from an unknown type.
    if(LD64(world)!=0x7000ad96e0ull)return 0;
    return !LD64(world+0x88)&&!LD64(world+0x90);
}

// The native application's root-job manager owns title/piano (ID 1). Other
// populated root jobs retain gameplay ownership.
static inline int catherine_input_main_menu_active(void) {
    if(catherine_input_two_player_selector_active())return 0;
    const uint64_t slot = LD64(0x7000cc9f98ull);
    if (!slot) return 0;
    const uint64_t app = LD64(slot);
    if (!app) return 0;
    const uint64_t manager = LD64(app + 0x48);
    if (!manager) return 0;
    const uint64_t head = LD64(manager + 0x28);
    if(!head||head==manager+0x28)return catherine_input_optional_menu_active();
    uint64_t node = head;
    // Native lists may use a sentinel or cycle during teardown. Bound traversal
    // and never infer a menu merely from an unknown populated root job list.
    for (unsigned left = 256; node && node != manager + 0x28 && left; --left) {
        const uint64_t task = LD64(node + 0x10);
        if (task && LD32(task + 0xc) == 1) return 1;
        node = LD64(node);
        if (node == head) break;
    }
    return 0;
}

static inline void catherine_input_menu_scope_publish(void) {
    const int menu = catherine_config_pc_is_open() || catherine_input_main_menu_active();
    catherine_config_setting(PC_INPUT_MENU_MODE, menu ? 2 : 1);
}
