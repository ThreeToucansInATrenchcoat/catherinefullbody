#pragma once
/* The picker uses the controller dropdown's geometry and hit testing. This
   renderer supplies the same canvas over every presented game/movie frame. */
#include "../../../base/dev-start/include/dev_overlay_draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif (defined(__linux__) && !defined(__ANDROID__))
#include <cairo/cairo.h>
#endif
extern "C" int catherine_dev_overlay_visible(void);

namespace dev_overlay_raster {
constexpr int width=1920,height=1080;
struct Command {float x,y,w,h;uint32_t color;std::string text;};
static std::vector<Command> commands;
static std::vector<unsigned char> pixels;
static uint64_t last_hash=0,version=0;
static void rect(void*,float x,float y,float w,float h,uint32_t color){commands.push_back({x,y,w,h,color,{}});}
static void text(void*,float x,float y,float size,const char* value,uint32_t color){commands.push_back({x,y,0,size,color,value?value:""});}
static void blend(int x,int y,uint32_t color,unsigned coverage=255){
 if(x<0||y<0||x>=width||y>=height)return;
 auto* p=&pixels[(size_t(y)*width+x)*4];
 const unsigned a=(color&255)*coverage/255,inv=255-a;
 p[0]=static_cast<unsigned char>(((color>>24)&255)*a/255+p[0]*inv/255);
 p[1]=static_cast<unsigned char>(((color>>16)&255)*a/255+p[1]*inv/255);
 p[2]=static_cast<unsigned char>(((color>>8)&255)*a/255+p[2]*inv/255);
 p[3]=static_cast<unsigned char>(a+p[3]*inv/255);
}
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
struct Glyph {int w=0,h=0;std::vector<unsigned char> mask;};
static std::unordered_map<std::string,Glyph> glyphs;
#if defined(_WIN32)
static Glyph make_glyph(const std::string& value,int size){
 Glyph result;
 int count=MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,nullptr,0);
 if(count<2)return result;
 std::wstring wide(size_t(count),L'\0');MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,wide.data(),count);
 HDC dc=CreateCompatibleDC(nullptr);
 HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 auto old_font=SelectObject(dc,font);SIZE extent{};GetTextExtentPoint32W(dc,wide.c_str(),count-1,&extent);
 result.w=std::min(width,int(extent.cx)+4);result.h=std::max(size+8,int(extent.cy)+4);
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=result.w;info.bmiHeader.biHeight=-result.h;
 info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
 if(bitmap&&bits){
  auto old_bitmap=SelectObject(dc,bitmap);std::memset(bits,0,size_t(result.w)*result.h*4);
  SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);TextOutW(dc,0,0,wide.c_str(),count-1);GdiFlush();
  result.mask.resize(size_t(result.w)*result.h);const auto* rgba=static_cast<const unsigned char*>(bits);
  for(size_t i=0;i<result.mask.size();++i)result.mask[i]=std::max({rgba[4*i],rgba[4*i+1],rgba[4*i+2]});
  SelectObject(dc,old_bitmap);DeleteObject(bitmap);
 }
 SelectObject(dc,old_font);DeleteObject(font);DeleteDC(dc);return result;
}
#else
#include "../../../../platform/overlay_font_linux.inc"
#endif
#endif
static bool refresh(){
 commands.clear();
 if(!catherine_dev_overlay_visible())return false;
 catherine_dev_overlay_frame(nullptr,rect,text);
 if(!catherine_dev_overlay_visible())return false;
 uint64_t hash=0xcbf29ce484222325ull;
 auto add=[&](const void* p,size_t n){const auto* b=static_cast<const unsigned char*>(p);while(n--){hash^=*b++;hash*=0x100000001b3ull;}};
 for(const auto& cmd:commands){add(&cmd.x,sizeof(float)*4);add(&cmd.color,4);add(cmd.text.data(),cmd.text.size());}
 if(hash==last_hash&&!pixels.empty())return true;
 last_hash=hash;pixels.assign(size_t(width)*height*4,0);
 bool first=true;
 for(const auto& cmd:commands){
  const int x=int(std::floor(cmd.x)),y=int(std::floor(cmd.y));
  if(cmd.text.empty()){
   const int right=std::min(width,int(std::ceil(cmd.x+cmd.w))),bottom=std::min(height,int(std::ceil(cmd.y+cmd.h)));
   if((cmd.color&255)==255||first){
    const unsigned a=cmd.color&255;
    const uint32_t pixel=(((cmd.color>>24)&255)*a/255)|((((cmd.color>>16)&255)*a/255)<<8)|((((cmd.color>>8)&255)*a/255)<<16)|(a<<24);
    for(int py=std::max(0,y);py<bottom;++py){
     auto* row=reinterpret_cast<uint32_t*>(&pixels[(size_t(py)*width+std::max(0,x))*4]);
     if(right>std::max(0,x))std::fill_n(row,right-std::max(0,x),pixel);
    }
   }else for(int py=std::max(0,y);py<bottom;++py)for(int px=std::max(0,x);px<right;++px)blend(px,py,cmd.color);
  }else{
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
   const int size=std::max(8,int(cmd.h));const std::string key=std::to_string(size)+":"+cmd.text;
   auto found=glyphs.find(key);
   if(found==glyphs.end()){
    if(glyphs.size()>2048)glyphs.clear();
    found=glyphs.emplace(key,make_glyph(cmd.text,size)).first;
   }
   const auto& glyph=found->second;
   for(int py=0;py<glyph.h;++py)for(int px=0;px<glyph.w;++px){
    const unsigned mask=glyph.mask.empty()?0:glyph.mask[size_t(py)*glyph.w+px];if(mask)blend(x+px,y+py,cmd.color,mask);
   }
#endif
  }
  first=false;
 }
 ++version;return true;
}
} // namespace dev_overlay_raster
