#include "controller_asset_images.h"
#include "controller_prompt_policy.h"
#include "catherine_keyboard.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(){
 using namespace nx::nvn;controller_art::Canvas canvas(1024,320);unsigned families[]={1,3,4,2};
 for(unsigned row=0;row<4;++row)for(unsigned b=0;b<16;++b){auto name=supplied_art::gamepad(families[row],b);const auto* image=supplied_art::find(name);if(!image){std::cerr<<"Missing "<<name<<"\n";return 1;}assert(supplied_art::draw(canvas,image,b*64,row*64,56,56));}
 for(unsigned b=0;b<16;++b){auto name=supplied_art::keyboard(catherine::kDefaultKeyboard[b]);const auto* image=supplied_art::find(name);if(!image){std::cerr<<"Missing "<<name<<"\n";return 1;}supplied_art::draw(canvas,image,b*64,256,56,56);}
 for(unsigned active=0;active<=4;++active)for(int choice=0;choice<=4;++choice){unsigned expected[]={active,1,3,4,2};assert(catherine::resolve_button_prompt_layout(active,choice)==(active?expected[choice]:0));}
 for(unsigned active=0;active<=4;++active)assert(catherine::resolve_tutorial_layout(active)==active);
 assert(catherine::resolve_tutorial_layout(99)==1);
 for(unsigned active=0;active<=4;++active){const auto& images=supplied_art::tutorial(active);assert(images.size()==4*1024*1024);for(unsigned previous=0;previous<active;++previous)assert(images!=supplied_art::tutorial(previous));}
 assert(&supplied_art::tutorial(99)==&supplied_art::tutorial(1));
 std::ofstream out("Build/asset-setup/prompt-contact.rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(canvas.pixels.data()),canvas.pixels.size());
 controller_art::Canvas phone(384,320);
 for(unsigned row=0;row<5;++row)for(unsigned b=0;b<4;++b){const auto* image=supplied_art::find(row==4?supplied_art::keyboard(catherine::kDefaultKeyboard[b]):supplied_art::gamepad(families[row],b));assert(image);assert(supplied_art::draw(phone,image,16+b*80,16+row*60,32,32));}
 // Phone atlas slots are 32 square. No replacement may touch neighbouring UI.
 for(unsigned y=0;y<phone.h;++y)for(unsigned x=0;x<phone.w;++x)if(phone.pixels[(y*phone.w+x)*4+3]){bool in=false;for(unsigned r=0;r<5;++r)for(unsigned b=0;b<4;++b)in|=x>=16+b*80&&x<48+b*80&&y>=16+r*60&&y<48+r*60;assert(in);}
 auto compressed=controller_art::encode(phone);std::ofstream dds("Build/asset-setup/phone-prompts.bc3",std::ios::binary);dds.write(reinterpret_cast<const char*>(compressed.data()),compressed.size());
 std::ofstream phone_output("Build/asset-setup/phone-prompts.rgba",std::ios::binary);phone_output.write(reinterpret_cast<const char*>(phone.pixels.data()),phone.pixels.size());
 std::cout<<"64 controller icons, 16 keyboard icons, and 25 prompt policy combinations passed.\n";
}

