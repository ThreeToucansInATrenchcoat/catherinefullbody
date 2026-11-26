#include "catherine_settings_win.h"
#include "config_pc_host.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

extern "C" unsigned catherine_upscaled_render_height(unsigned);
using namespace catherine;
using Game = std::array<unsigned char, 0x9b60>;

static Game native_fixture() {
    Game game{};
    game[0x9b58] = 1;
    game[0x2f7] = 1;
    game[0x2f8] = 1;
    game[0x2fb] = static_cast<unsigned char>(-40);
    return game;
}

static void check_persisted(const HostSettings& s) {
    assert(s.fps == 120 && s.fullscreen && s.window_height == 1440 && s.anti_aliasing == 4);
    assert(s.movie_interpolation == 240 && s.button_prompts == 3);
    assert(s.input_devices[0] == "keyboard" && s.input_devices[1] == "qa:Pad 2");
    assert(s.movement_preset == 1 && s.swap_sticks && s.keyboard[0] == 91);
    assert(s.player2.movement_preset == 2 && s.player2.swap_sticks && s.player2.keyboard[2] == 'T');
    assert(s.keyboard[16] == 0 && s.player2.keyboard[17] == 303);
    assert(s.bindings == 0xfedcba9876543021ull && s.player2.bindings == kIdentityBindings);
    assert(!catherine_dev_enabled());
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string mode = argv[1];
    auto s = host_settings();
    auto game = native_fixture();
    if (mode == "check-upscaling") {
        assert(s.show_fps && s.upscaler==1 && s.upscale_quality==3 && s.fsr_sharpness==80 && s.frame_generation==1 && s.frame_generation_multiplier==4);
        const unsigned heights[]={828,720,630,540};
        for(int quality=0;quality<4;++quality){auto option=s;option.upscale_quality=quality;assert(store_host_settings(option));assert(catherine_upscaled_render_height(1080)==heights[quality]);}
        assert(store_host_settings(s));
        auto invalid=s;invalid.fsr_sharpness=101;assert(!store_host_settings(invalid));
        invalid=s;invalid.upscale_quality=4;assert(!store_host_settings(invalid));
        invalid=s;invalid.frame_generation=2;assert(!store_host_settings(invalid));
        invalid=s;invalid.frame_generation_multiplier=5;assert(!store_host_settings(invalid));
    } else if (mode == "check-graphics-fallback") {
        assert(s.show_fps&&s.upscaler==0&&s.frame_generation==0&&s.frame_generation_multiplier==4&&s.fsr_sharpness==20&&s.upscale_quality==1);
    } else if (mode == "check-migration") {
        check_persisted(s);
        catherine_load_native_config(game.data(), 1);
        assert(game[0x2fb] == static_cast<unsigned char>(-40));
        // Every native option except difficulty/type is inverted or moved.
        for (int offset : {0x300,0x2f4,0x2f5,0x2f6,0x2f9,0x2fa,0x2ff,0x2fe,0x301})
            game[offset] = 1;
        game[0x2fb] = 54;
        game[0x9b58] = 0;
        game[0x9b5c] = 1;
        begin_config_settings();
        assert(catherine_save_native_config(game.data(), 1));
        assert(end_config_settings());
    } else if (mode == "check-roundtrip") {
        check_persisted(s);
        catherine_load_native_config(game.data(), 1);
        for (int offset : {0x300,0x2f4,0x2f5,0x2f6,0x2f9,0x2fa,0x2ff,0x2fe,0x301})
            assert(game[offset] == 1);
        assert(game[0x2f7] == 0 && game[0x2f8] == 0);
        assert(game[0x2fb] == 54 && game[0x9b58] == 0 && game[0x9b5c] == 1);
        Game reloaded = native_fixture();
        reloaded[0x9b58] = 3;
        catherine_load_native_config(reloaded.data(), 0);
        assert(reloaded[0x9b58] == 3 && reloaded[0x9b5c] == 0 && reloaded[0x2fb] == 54);
        begin_config_settings();
        s.fps = 30;
        assert(store_host_settings(s));
        assert(end_config_settings() && host_settings().fps == 120);
        // A title-menu save retains the last chosen campaign defaults.
        game[0x9b58] = 2;
        game[0x9b5c] = 0;
        begin_config_settings();
        assert(catherine_save_native_config(game.data(), 0));
        assert(end_config_settings());
    } else if (mode == "check-invalid") {
        assert(s.fps == 60 && s.input_devices[0] == "auto" && s.input_devices[1] == "none");
        assert(s.keyboard == kDefaultKeyboard && s.bindings == kDefaultBindings);
        assert(catherine_dev_enabled() && s.player2.keyboard[0] == 'B');
        catherine_load_native_config(game.data(), 1);
        assert(game[0x2fb] == static_cast<unsigned char>(-40) && game[0x301] == 1);
    } else if (mode == "check-pre6" || mode == "check-v6") {
        const auto expected = mode == "check-pre6" ? kDefaultBindings : kIdentityBindings;
        assert(s.bindings == expected && s.player2.bindings == expected && catherine_dev_enabled());
        catherine_load_native_config(game.data(), 1);
    } else if (mode == "migrate-install-copy") {
        // The runner provides a private copy of the legacy native system record.
        std::ifstream record("legacy-system.bin", std::ios::binary);
        assert(record.read(reinterpret_cast<char*>(game.data()) + 8, 0x9810));
        catherine_load_native_config(game.data(), 1);
    } else {
        return 2;
    }
    std::cout << mode << " passed\n";
}
