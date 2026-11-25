#include "catherine_settings_win.h"
#include <cassert>
#include <iostream>
int main(int argc,char**){
 auto s=catherine::host_settings();
 if(argc>1){assert(s.button_prompts==4);std::cout<<"Persisted PS5 preference loaded.\n";return 0;}
 assert(s.fps==60&&s.movie_interpolation==0&&s.anti_aliasing==0&&s.window_height==0&&s.button_prompts==0&&s.input_devices[0]=="auto"&&s.input_devices[1]=="none");
 catherine::begin_config_settings();s.button_prompts=4;assert(catherine::store_host_settings(s));assert(catherine::button_prompt_style()==4);assert(catherine::save_config_settings());assert(catherine::end_config_settings());
 catherine::begin_config_settings();s=catherine::host_settings();s.button_prompts=2;assert(catherine::store_host_settings(s));assert(catherine::button_prompt_style()==2);assert(catherine::end_config_settings());assert(catherine::button_prompt_style()==4);
 std::cout<<"Fresh defaults, live prompt updates, saving, and discard restoration passed.\n";
}
