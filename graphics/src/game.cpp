#include <nx/game.h>
#include "platform.h"
namespace nx::game {
const Info kInfo={"catherine","Catherine Full Body",0x0100bf00112c0000ull,"",".",32ull<<20};
void after_init(){} void renderer_start(){} void first_frame(){}
}
#if defined(_WIN32)
extern "C" unsigned graphics_connected_player_count(){return nx::plat::connected_player_count();}
extern "C" void graphics_read_player_pads(uint64_t *buttons,int32_t *sticks,uint32_t *connected) {
    nx::plat::PadState players[2]{};bool live[2]{};
    nx::plat::read_player_pads(players,live);
    for(unsigned i=0;i<2;++i){
        buttons[i]=players[i].buttons;connected[i]=live[i];
        sticks[i*4]=players[i].lx;sticks[i*4+1]=players[i].ly;
        sticks[i*4+2]=players[i].rx;sticks[i*4+3]=players[i].ry;
    }
}
#endif

extern "C" void graphics_read_pad(uint64_t *buttons, int32_t *sticks) {
    nx::plat::PadState state{}; nx::plat::read_pad(state); *buttons=state.buttons;
    sticks[0]=state.lx; sticks[1]=state.ly; sticks[2]=state.rx; sticks[3]=state.ry;
}
