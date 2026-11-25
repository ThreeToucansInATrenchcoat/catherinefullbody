#include "catherine_settings_win.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
using namespace catherine;
static std::wstring path(){wchar_t p[32768];GetModuleFileNameW(nullptr,p,32768);auto s=std::wstring(p);return s.substr(0,s.find_last_of(L'\\')+1)+L"catherine-settings.ini";}
static std::string text(){std::ifstream in(path().c_str());return {std::istreambuf_iterator<char>(in),{}};}
static uint64_t swap_faces(uint64_t map){return map^uint64_t(1)^uint64_t(1<<4);}
int main(int argc,char**){
    if(argc>1){auto loaded=host_settings();assert(loaded.fps==240&&loaded.movement_preset==1&&loaded.swap_sticks);assert(loaded.keyboard[0]=='G');assert(loaded.keyboard[7]=='R'&&loaded.keyboard[8]=='W');assert(loaded.bindings==swap_faces(kDefaultBindings));std::cout<<"Settings reload passed\n";return 0;}
    auto original=host_settings();assert(original.fps==-1&&original.movement_preset==0&&!original.swap_sticks);
    begin_config_settings();auto main=host_settings();main.fps=120;assert(store_host_settings(main));
    begin_bindings_settings();assert(bindings_settings_active()&&!bindings_settings_dirty());
    auto draft=bindings_settings();assert(rebind_keyboard(draft.keyboard,0,'G',0));
    assert(change_movement_preset(draft.keyboard,0,1));draft.movement_preset=1;draft.swap_sticks=true;draft.bindings=swap_faces(draft.bindings);
    assert(store_bindings_draft(draft));assert(bindings_settings_dirty());assert(host_settings().keyboard[0]==32&&!controller_swap_sticks());
    // A failed disk write leaves both the active input and editable draft untouched.
    const auto temporary=path()+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    assert(CreateDirectoryW(temporary.c_str(),nullptr));assert(!save_bindings_settings());assert(RemoveDirectoryW(temporary.c_str()));
    assert(bindings_settings_dirty()&&host_settings().keyboard[0]==32&&!controller_swap_sticks());
    assert(save_bindings_settings());assert(!bindings_settings_dirty()&&bindings_settings_active());
    assert(host_settings().fps==120&&keyboard_binding(0)=='G'&&controller_swap_sticks());
    auto disk=text();assert(disk.find("Fps=-1")!=std::string::npos);assert(disk.find("MovementPreset=1")!=std::string::npos);assert(disk.find("SwapSticks=1")!=std::string::npos);
    // Reject controller assignments to D-pad even if they form a 16-entry permutation.
    auto invalid=bindings_settings();invalid.bindings^=uint64_t(12)^(uint64_t(12)<<48);assert(!store_bindings_draft(invalid));
    assert(end_config_settings());assert(host_settings().fps==-1&&keyboard_binding(0)=='G'&&controller_swap_sticks());
    assert(!bindings_settings_active());begin_bindings_settings();draft=bindings_settings();assert(rebind_keyboard(draft.keyboard,0,'H',1));assert(store_bindings_draft(draft));assert(bindings_settings_dirty());
    discard_bindings_settings();assert(!bindings_settings_active()&&!bindings_settings_dirty()&&keyboard_binding(0)=='G');
    begin_config_settings();main=host_settings();main.fps=240;assert(store_host_settings(main));assert(save_config_settings());assert(end_config_settings());
    assert(host_settings().fps==240&&keyboard_binding(0)=='G');
    std::cout<<"Independent binding transaction and save-failure tests passed\n";
}

