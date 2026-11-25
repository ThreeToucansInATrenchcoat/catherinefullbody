#pragma once
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

// Read-only, bounded sampling at the existing one-second caption timer. No
// developer-loader state, guest calls, actor traversal or retained guest pointer.
namespace catherine::window_area {
struct Area { enum Kind { Unknown, Puzzle, Pub, Landing } kind=Unknown; unsigned stage=0,floor=0,day=0; };
struct Guard { uint64_t address=0,value=0; };
template<class Reader> struct Probe {
 Reader& read; Guard guards[24]{}; unsigned count=0; bool valid=true;
 template<class T> bool value(uint64_t address,T& out) { if(!valid||!read(address,&out,sizeof out)){valid=false;return false;}return true; }
 uint64_t pointer(uint64_t address,bool retain=true) {uint64_t result=0;if(!value(address,result))return 0;if(retain){if(count==24){valid=false;return 0;}guards[count++]={address,result};}return result;}
 unsigned number(uint64_t address) {uint32_t result=0;value(address,result);return result;}
 bool stable() {if(!valid)return false;for(unsigned i=0;i<count;++i){uint64_t result=0;if(!read(guards[i].address,&result,sizeof result)||result!=guards[i].value)return false;}return true;}
};
inline bool address(uint64_t p) {return p>=0x10000ull&&p<0x800000000000ull&&(p&7)==0;}
inline bool guest_code(uint64_t p) {return p>=0x7000000000ull&&p<0x7001000000ull&&(p&3)==0;}
template<class Reader> Area puzzle(Reader& read) {
 Probe<Reader> p{read};Area out;const auto registry=p.pointer(0x7000ccadf0ull);if(!address(registry))return out;
 const auto actor=p.pointer(registry);if(!address(actor))return out;
 const auto actor_vt=p.pointer(actor);if(!guest_code(actor_vt)||p.pointer(actor_vt+0x60,false)!=0x70001bf390ull||p.pointer(actor_vt+0x140,false)!=0x7000151fb0ull)return out;
 const auto holder=p.pointer(0x7000cca5d8ull);if(!address(holder))return out;const auto manager=p.pointer(holder);if(!address(manager))return out;
 const auto stage=p.pointer(manager+0x668);if(!address(stage)||!guest_code(p.pointer(stage)))return out;
 out.stage=p.number(stage+0x18);out.floor=p.number(stage+0x1c);
 if(out.stage<1||out.stage>10||out.floor<1||out.floor>16||!p.stable())return {};
 // Recheck scalar identity as a natural progression/retry can reuse the object.
 if(p.number(stage+0x18)!=out.stage||p.number(stage+0x1c)!=out.floor||!p.valid)return {};
 out.kind=Area::Puzzle;return out;
}
template<class Reader> Area world(Reader& read,bool pub) {
 Probe<Reader> p{read};Area out;const auto holder=p.pointer(pub?0x7000cca2c8ull:0x7000cca408ull);if(!address(holder))return out;
 const auto scene=p.pointer(holder);if(!address(scene))return out;const auto actor=scene+(pub?0x80:0x30);
 if(pub){if(p.pointer(actor)!=0x70009f74b0ull||p.pointer(actor+0x1f0)!=0x70009f7090ull||p.pointer(actor+0x210)!=0x70009f70d8ull)return {};}
 else {const auto type=p.pointer(0x7000cca310ull,false);if(!guest_code(type)||p.pointer(actor)!=type+0x10)return {};}
 // GET_DAY reads the live save/config field at9B60; it is not saved DEV target metadata.
 const auto config_holder=p.pointer(0x7000cc9f60ull);if(address(config_holder)){const auto config=p.pointer(config_holder);if(address(config)){const unsigned day=p.number(config+0x9b60);if(day>=1&&day<=14)out.day=day;}}
 if(!p.stable())return {};out.kind=pub?Area::Pub:Area::Landing;return out;
}
template<class Reader> Area current(Reader& read) {
 const Area level=puzzle(read),pub=world(read,true),landing=world(read,false);
 const unsigned candidates=(level.kind!=Area::Unknown)+(pub.kind!=Area::Unknown)+(landing.kind!=Area::Unknown);
 // A load/teardown overlap is temporarily unlabeled, never an old requested area.
 if(candidates!=1)return {};return level.kind!=Area::Unknown?level:pub.kind!=Area::Unknown?pub:landing;
}
inline std::wstring label(Area area) {
 wchar_t text[96]{};
 switch(area.kind){
 case Area::Puzzle:std::swprintf(text,96,L"Stage %u / Floor %u",area.stage,area.floor);break;
 case Area::Pub:if(area.day)std::swprintf(text,96,L"Stray Sheep - Day %u",area.day);else std::swprintf(text,96,L"Stray Sheep");break;
 case Area::Landing:if(area.day)std::swprintf(text,96,L"Landing - Day %u",area.day);else std::swprintf(text,96,L"Landing");break;
 default:break;
 }return text;
}
}
