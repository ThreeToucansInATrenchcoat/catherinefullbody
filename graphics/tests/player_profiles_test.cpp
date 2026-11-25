#include "catherine_settings_win.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstring>
using namespace catherine;
int main(int argc,char**argv){
    if(argc>1&&std::strcmp(argv[1],"legacy")==0){
        const auto saved=host_settings();
        assert(saved.keyboard[0]=='G'&&saved.player2.keyboard==kDefaultKeyboard);
        assert(saved.player2.bindings==kDefaultBindings&&!saved.player2.swap_sticks);
        assert(saved.input_devices[0]=="auto"&&saved.input_devices[1]=="none");
        std::cout<<"Version3 settings preserve P1 and initialize independent default P2\n";return 0;
    }
    if(argc>1){
        const auto saved=host_settings();
        assert(saved.keyboard[0]=='G'&&saved.player2.keyboard[0]=='H');
        assert(saved.player2.swap_sticks&&saved.player2.movement_preset==1);
        assert(saved.input_devices[0]=="keyboard"&&saved.input_devices[1]=="wgi:test-pad");
        std::cout<<"Independent P2 profile and device assignment reload passed\n";return 0;
    }
    auto settings=host_settings();assert(settings.input_devices[0]=="auto"&&settings.input_devices[1]=="none");
    begin_config_settings();settings.fps=240;
    settings.input_devices={"keyboard","wgi:test-pad"};assert(store_host_settings(settings));
    begin_bindings_settings();auto draft=bindings_settings();
    assert(rebind_keyboard(draft.keyboard,0,'G',0));
    assert(rebind_keyboard(draft.player2.keyboard,0,'H',0));
    assert(change_movement_preset(draft.player2.keyboard,0,1));draft.player2.movement_preset=1;draft.player2.swap_sticks=true;
    assert(store_bindings_draft(draft)&&bindings_settings_dirty());
    // Failed input writes publish neither player's profile.
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);
    std::wstring ini(path);ini=ini.substr(0,ini.find_last_of(L'\\')+1)+L"catherine-settings.ini";
    const auto temporary=ini+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    assert(CreateDirectoryW(temporary.c_str(),nullptr));assert(!save_bindings_settings());assert(RemoveDirectoryW(temporary.c_str()));
    assert(host_settings().keyboard[0]==32&&host_settings().player2.keyboard[0]==32&&bindings_settings_dirty());
    assert(save_bindings_settings());
    assert(host_settings().player2.keyboard[0]==32); // P1 Confirm cannot commit P2 edits.
    discard_bindings_settings();begin_bindings_settings(1);draft=bindings_settings();
    const auto p2_version=keyboard_binding_version(1);
    assert(rebind_keyboard(draft.player2.keyboard,0,'H',0));
    assert(change_movement_preset(draft.player2.keyboard,0,1));draft.player2.movement_preset=1;draft.player2.swap_sticks=true;
    assert(store_bindings_draft(draft));
    assert(keyboard_binding(0,1)==32&&keyboard_binding_version(1)==p2_version);
    assert(save_bindings_settings());
    assert(keyboard_binding(0,1)=='H'&&keyboard_binding(0,0)=='G'&&keyboard_binding_version(1)!=p2_version);
    assert(end_config_settings());
    settings=host_settings();assert(settings.fps==-1&&settings.input_devices[0]=="auto"&&settings.input_devices[1]=="none");
    assert(settings.keyboard[0]=='G'&&settings.player2.keyboard[0]=='H'&&settings.player2.swap_sticks);
    begin_bindings_settings(1);draft=bindings_settings();assert(rebind_keyboard(draft.player2.keyboard,0,'J',1));assert(store_bindings_draft(draft));
    assert(keyboard_binding(0,1)=='H');
    discard_bindings_settings();assert(host_settings().player2.keyboard[0]=='H');
    begin_config_settings();settings=host_settings();settings.input_devices={"keyboard","wgi:test-pad"};assert(store_host_settings(settings));assert(save_config_settings());assert(end_config_settings());
    std::cout<<"Two-player binding transactions, rollback, failed-save isolation and assignment draft passed\n";
}
