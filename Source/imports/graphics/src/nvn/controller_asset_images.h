#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "controller_art_raster.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <windows.h>
namespace nx::nvn::supplied_art {
struct Image {unsigned w=0,h=0;std::vector<uint8_t> rgba;};
inline std::filesystem::path root(){wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);return n&&n<32768?std::filesystem::path(exe).parent_path()/L"CathAssets":std::filesystem::path{};}
inline const std::vector<uint8_t>& tutorial(unsigned layout){
 static std::array<std::vector<uint8_t>,5> sets;static std::array<bool,5> loaded{};
 if(layout>4)layout=1;
 if(loaded[layout])return sets[layout];loaded[layout]=true;
 static const wchar_t* families[]={L"KeyboardMouse",L"Xbox",L"PS5",L"Switch",L"PS4"};
 std::ifstream input(root()/L"Tutorials"/families[layout]/L"tutorial.bc3",std::ios::binary|std::ios::ate);
 constexpr size_t bytes=4*1024*1024;
 if(input&&input.tellg()==bytes){auto& result=sets[layout];result.resize(bytes);input.seekg(0);if(!input.read(reinterpret_cast<char*>(result.data()),bytes))result.clear();}
 return sets[layout];
}
inline const Image* find(const std::string& name){
 static const auto images=[](){std::map<std::string,Image> result;std::ifstream in(root()/L"prompts.rgba",std::ios::binary);char magic[8]{};uint32_t count=0;in.read(magic,8);in.read(reinterpret_cast<char*>(&count),4);if(!in||memcmp(magic,"CATRIMG1",8)||count>4096)return result;
  for(uint32_t i=0;i<count;++i){uint16_t n=0,w=0,h=0;uint32_t bytes=0;in.read(reinterpret_cast<char*>(&n),2);if(!in||!n||n>512)return std::map<std::string,Image>{};std::string key(n,'\0');in.read(key.data(),n);in.read(reinterpret_cast<char*>(&w),2);in.read(reinterpret_cast<char*>(&h),2);in.read(reinterpret_cast<char*>(&bytes),4);if(!in||!w||!h||w>1024||h>1024||bytes!=uint32_t(w)*h*4)return std::map<std::string,Image>{};Image image{w,h,std::vector<uint8_t>(bytes)};in.read(reinterpret_cast<char*>(image.rgba.data()),bytes);if(!in)return std::map<std::string,Image>{};result.emplace(std::move(key),std::move(image));}return result;}();
 auto i=images.find(name);return i==images.end()?nullptr:&i->second;
}
inline std::string keyboard(unsigned key){
 std::string token;if((key>='A'&&key<='Z')||(key>='0'&&key<='9'))token=char(key);else if(key>=112&&key<=123)token="F"+std::to_string(key-111);else switch(key){
 case 1:token="Mouse_Left";break;case 2:token="Mouse_Right";break;case 4:token="Mouse_Middle";break;case 8:token="BackSpace";break;case 9:token="Tab";break;case 13:token="Enter";break;case 16:token="Shift";break;case 17:token="Crtl";break;case 18:token="Alt";break;case 27:token="Esc";break;case 32:token="Space";break;case 33:token="PageUp";break;case 34:token="PageDown";break;case 35:token="End";break;case 36:token="Home";break;case 37:token="Left";break;case 38:token="Up";break;case 39:token="Right";break;case 40:token="Down";break;case 45:token="Insert";break;case 46:token="Del";break;case 256:token="Mouse_Scroll_Up_Key_Dark";break;case 257:token="Mouse_Scroll_Down_Key_Dark";break;default:return {};}
 return "Keyboard_Mouse/White/T_"+token+"_Key_White.png";
}
inline std::string gamepad(unsigned layout,unsigned physical){
 if(physical>=16)return {};
 const char*const xbox[]={"B_Color","A_Color","Y_Color","X_Color","Left_Stick_Click","Right_Stick_Click","LB","RB","LT","RT","X","Share","Dpad_Left","Dpad_Up","Dpad_Right","Dpad_Down"};
 const char*const sw[]={"A","B","X","Y","L_2D","R_2D","LB","RB","LT","RT","Plus","Minus","Dpad_Left","Dpad_Up","Dpad_Right","Dpad_Down"};
 const char*const ps[]={"Circle_Color","Cross_Color","Triangle_Color","Square_Color","L3","R3","L1","R1","L2","R2","Options","Share","Dpad_Left","Dpad_UP","Dpad_Right","Dpad_Down"};
 if(layout==3)return "SGamepad/Default/T_S_"+std::string(sw[physical])+".png";
 if(layout==2||layout==4){std::string p=layout==4?"P4":"P5";return p+"Gamepad/Default/T_"+p+"_"+ps[physical]+".png";}
 return "XGamepad/Default/T_X_"+std::string(xbox[physical])+".png";
}
inline bool draw(controller_art::Canvas& canvas,const Image* image,int x,int y,int w,int h){
 if(!image)return false;canvas.rect(x,y,w,h,{0,0,0,0});float scale=std::min(float(w)/image->w,float(h)/image->h);int dw=std::max(1,int(image->w*scale)),dh=std::max(1,int(image->h*scale));int left=x+(w-dw)/2,top=y+(h-dh)/2;
 for(int dy=0;dy<dh;++dy)for(int dx=0;dx<dw;++dx){float sx=(dx+.5f)*image->w/dw-.5f,sy=(dy+.5f)*image->h/dh-.5f;int ix=int(std::floor(sx)),iy=int(std::floor(sy));float fx=sx-ix,fy=sy-iy,alpha=0,rgb[3]{};
  for(int j=0;j<2;++j)for(int i=0;i<2;++i){float weight=(i?fx:1-fx)*(j?fy:1-fy);const auto* p=&image->rgba[(size_t(std::clamp(iy+j,0,int(image->h)-1))*image->w+std::clamp(ix+i,0,int(image->w)-1))*4];float a=p[3]*weight;alpha+=a;for(int c=0;c<3;++c)rgb[c]+=p[c]*a;}
  canvas.put(left+dx,top+dy,{uint8_t(alpha>0?std::clamp(rgb[0]/alpha,0.f,255.f):0),uint8_t(alpha>0?std::clamp(rgb[1]/alpha,0.f,255.f):0),uint8_t(alpha>0?std::clamp(rgb[2]/alpha,0.f,255.f):0),uint8_t(std::clamp(alpha,0.f,255.f))});
 }return true;
}
}
