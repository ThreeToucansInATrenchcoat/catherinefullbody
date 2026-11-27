#include "catherine_settings_win.h"
#include "config_pc_host.h"
#include "catherine_shadow_quality.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

extern "C" int catherine_mode_intro_should_play(unsigned, unsigned);
extern "C" int catherine_title_mouse_enabled();
extern "C" int catherine_native_gpu_skinning_enabled();
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
    if (mode.starts_with("shadow-resolution-")) {
        const int expected = std::stoi(mode.substr(18));
        assert(catherine_shadow_resolution_setting() == expected);
        s.button_prompts = (s.button_prompts + 1) % 5;
        assert(store_host_settings(s));
        assert(catherine_shadow_resolution_setting() == expected);
        assert(catherine_shadow_resolution_for_height(0, 720) == 4096);
        assert(catherine_shadow_resolution_for_height(0, 1080) == 4096);
        assert(catherine_shadow_resolution_for_height(0, 1440) == 8192);
        assert(catherine_shadow_resolution_for_height(0, 2160) == 8192);
        assert(catherine_shadow_resolution_for_height(2048, 2160) == 2048);
        assert(catherine_shadow_resolution_for_height(4096, 720) == 4096);
        assert(catherine_shadow_resolution_with_limit(8192, 4096) == 4096);
        assert(catherine_shadow_resolution_with_limit(8192, 2048) == 2048);
        assert(catherine_shadow_resolution_with_limit(4096, 8192) == 4096);
        return 0;
    }
    if (mode == "native-skinning-startup") {
        assert(catherine_native_gpu_skinning_enabled());
        return 0;
    }
    if (mode == "native-skinning-0" || mode == "native-skinning-1") {
        const bool expected = mode.back() == '1';
        assert(bool(catherine_native_gpu_skinning_enabled()) == expected);
        // An unrelated menu change must preserve the startup renderer choice.
        s.button_prompts = (s.button_prompts + 1) % 5;
        assert(store_host_settings(s));
        assert(bool(catherine_native_gpu_skinning_enabled()) == expected);
        return 0;
    }
    if (mode == "upscale-policy") {
        for(int mode=0;mode<=4;++mode) {
            assert(internal_upscaling_height(mode,1280,720,true,0)==720);
            assert(internal_upscaling_height(mode,3840,2160,false,720)==720);
        }
        assert(internal_upscaling_height(0,3840,2160,true,0)==720);
        assert(internal_upscaling_height(1,1920,1080,true,0)==1080);
        assert(internal_upscaling_height(1,2560,1440,true,0)==1440);
        assert(internal_upscaling_height(1,3840,2160,true,0)==2160);
        assert(internal_upscaling_height(1,1366,768,true,0)==720);
        assert(internal_upscaling_height(1,3440,1440,true,0)==1440);
        assert(internal_upscaling_height(2,3840,2160,true,0)==1080);
        assert(internal_upscaling_height(4,1920,1080,true,0)==1080);
        assert(internal_upscaling_height(1,3840,2160,false,1080)==1080);
        return 0;
    }
    if (mode.rfind("upscale-",0)==0) {
        assert(s.internal_upscaling == mode.back()-'0');
        s.button_prompts=(s.button_prompts+1)%5;
        assert(store_host_settings(s));return 0;
    }
    if (mode == "storage-0" || mode == "storage-1") {
        assert(s.alt_build_mode == (mode == "storage-1"));
        s.button_prompts = (s.button_prompts + 1) % 5;
        assert(store_host_settings(s));
        return 0;
    }
    if (mode.rfind("presentation-", 0) == 0) {
        assert(mode.size() == 15);
        assert(s.vsync == (mode[13] == '1'));
        assert(s.triple_buffering == (mode[14] == '1'));
        // Saving an unrelated setting must retain both INI-only options.
        s.button_prompts = (s.button_prompts + 1) % 5;
        assert(store_host_settings(s));
        return 0;
    }
    if (mode == "cursor-policy") {
        for (const auto& p1 : {"auto", "keyboard", "qa:Pad 1", "disconnected-pad"}) {
            for (const auto& p2 : {"none", "keyboard", "qa:Pad 2"}) {
                s.input_devices = {p1, p2};
                assert(store_host_settings(s));
                const bool expected = std::string(p1) == "auto" || std::string(p1) == "keyboard" || std::string(p2) == "keyboard";
                assert(bool(catherine_title_mouse_enabled()) == expected);
            }
        }
        return 0;
    }
    if (mode == "dev-default-off" || mode == "dev-opt-in") {
        assert(bool(catherine_dev_enabled()) == (mode == "dev-opt-in"));
        assert(store_host_settings(s));
    } else if (mode == "intro-first" || mode == "intro-repeat") {
        const int expected = mode == "intro-first";
        assert(catherine_mode_intro_should_play(102, 10) == expected);
        assert(catherine_mode_intro_should_play(101, 11) == expected);
        assert(catherine_mode_intro_should_play(450, 10) == expected);
        assert(catherine_mode_intro_should_play(102, 10) == 0);
        assert(catherine_mode_intro_should_play(101, 11) == 0);
        assert(catherine_mode_intro_should_play(103, 10) == 1);
        assert(catherine_mode_intro_should_play(101, 10) == 1);
        begin_config_settings();
        s.fps = 30;
        assert(store_host_settings(s));
        assert(end_config_settings());
        assert(store_host_settings(host_settings()));
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
        assert(!s.vsync && !s.triple_buffering && !s.alt_build_mode && s.internal_upscaling == 1);
        assert(s.keyboard == kDefaultKeyboard && s.bindings == kDefaultBindings);
        assert(!catherine_dev_enabled() && s.player2.keyboard[0] == 'B');
        catherine_load_native_config(game.data(), 1);
        assert(game[0x2fb] == static_cast<unsigned char>(-40) && game[0x301] == 1);
    } else if (mode == "check-pre6" || mode == "check-v6") {
        const auto expected = mode == "check-pre6" ? kDefaultBindings : kIdentityBindings;
        assert(s.bindings == expected && s.player2.bindings == expected && !catherine_dev_enabled());
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
