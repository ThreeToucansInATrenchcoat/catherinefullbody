#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "catherine_keyboard.h"

namespace catherine::tutorial {
struct Input {
 unsigned layout=0; // Resolved button-prompt style; keyboard always takes priority.
 uint64_t mapping=0xfedcba9876543201ull;
 std::array<uint16_t,kKeyboardBindings> keys=kDefaultKeyboard;
};
enum class Topic {None,Grab,Undo,Autoplay,Phone,Drink,Menu,Rapunzel};
inline Topic topic(unsigned voice){
 switch(voice){
 case 10054:case 10055:case 10056:return Topic::Grab;
 case 10058:case 10059:case 10060:return Topic::Undo;
 case 10063:case 10064:case 10065:return Topic::Autoplay;
 case 18556:case 18558:return Topic::Phone;
 case 18549:case 18554:return Topic::Drink;
 case 18550:case 18553:case 18555:case 48007:return Topic::Menu;
 case 40560:return Topic::Rapunzel;
 default:return Topic::None;
 }
}
inline std::u16string widen(const char* value){std::u16string s;while(*value)s+=char16_t(uint8_t(*value++));return s;}
inline std::u16string plain(const uint16_t* source){
 std::u16string result;
 for(unsigned i=0;i<1024;++i){unsigned c=source[i];if(c==0xd821)return result;if(c>=0xd800&&c<=0xdfff){i+=(c>>8)&7;continue;}result+=c?char16_t(c):u'\n';}
 return {}; // Bounded, complete messages only.
}
inline bool english(Topic t,const std::u16string& s){
 switch(t){
 case Topic::Grab:return s.starts_with(u"You can grab");
 case Topic::Undo:return s.starts_with(u"If you make a mistake");
 case Topic::Autoplay:return s.starts_with(u"If you can't figure out");
 case Topic::Phone:return s.starts_with(u"Press the X Button")||s.starts_with(u"You can take your phone");
 case Topic::Drink:return s.starts_with(u"Enjoy your time")||s.starts_with(u"Spend some time");
 case Topic::Menu:return s.starts_with(u"Furthermore,")||s.starts_with(u"You can open menus")||s.starts_with(u"If you press START");
 case Topic::Rapunzel:return s.starts_with(u"Press X to find out!");
 default:return false;
 }
}
inline std::u16string key_name(unsigned key){
 if(key>=96&&key<=105)return u"Numpad "+widen(keyboard_key_name('0'+key-96));
 if(key>=124&&key<=135)return u"F"+widen(std::to_string(key-111).c_str());
 // Punctuation bindings must retain their actual key names as well.
 switch(key){case 19:return u"Pause";case 20:return u"Caps Lock";case 44:return u"Print Screen";case 91:return u"Left Windows";case 92:return u"Right Windows";case 93:return u"Menu";case 106:return u"Numpad Multiply";case 107:return u"Numpad Add";case 109:return u"Numpad Subtract";case 110:return u"Numpad Decimal";case 111:return u"Numpad Divide";case 144:return u"Num Lock";case 145:return u"Scroll Lock";case 160:return u"Left Shift";case 161:return u"Right Shift";case 162:return u"Left Ctrl";case 163:return u"Right Ctrl";case 164:return u"Left Alt";case 165:return u"Right Alt";case 0xba:return u"Semicolon";case 0xbb:return u"Equals";case 0xbc:return u"Comma";case 0xbd:return u"Minus";case 0xbe:return u"Period";case 0xbf:return u"Slash";case 0xc0:return u"Backtick";case 0xdb:return u"Left Bracket";case 0xdc:return u"Backslash";case 0xdd:return u"Right Bracket";case 0xde:return u"Apostrophe";}
 return widen(keyboard_key_name(key));
}
inline std::u16string highlight(const std::u16string& s){return std::u16string{char16_t(0xd901),char16_t(4)}+s+std::u16string{char16_t(0xd901),char16_t(1)};}
inline const char* pad_control_name(const Input& in,unsigned action){
 static const char* xbox[]={"B","A","Y","X","Left Stick","Right Stick","L","R","LT","RT","Menu","View","Left","Up","Right","Down"};
 static const char* sw[]={"A","B","X","Y","Left Stick","Right Stick","L","R","ZL","ZR","+","-","Left","Up","Right","Down"};
 static const char* ps[]={"Circle","Cross","Triangle","Square","L3","R3","L","R","L2","R2","Options","Share","Left","Up","Right","Down"};
 unsigned physical=unsigned(in.mapping>>(action*4))&15;
 return in.layout==3?sw[physical]:(in.layout==2||in.layout==4)?ps[physical]:xbox[physical];
}
inline std::u16string control(const Input& in,unsigned action){
 if(!in.layout){unsigned key=in.keys[action];if(key==1)return u"the "+highlight(u"Left Mouse Button");if(key==2)return u"the "+highlight(u"Right Mouse Button");if(key==4)return u"the "+highlight(u"Middle Mouse Button");if(key==5)return highlight(u"Mouse Button 4");if(key==6)return highlight(u"Mouse Button 5");return u"the "+highlight(key_name(key)+u" key");}
 return u"the "+highlight(widen(pad_control_name(in,action))+u" button");
}
inline std::u16string operate(const Input& in,unsigned action){
 if(!in.layout&&in.keys[action]==kWheelUp)return u"scroll the "+highlight(u"mouse wheel up");
 if(!in.layout&&in.keys[action]==kWheelDown)return u"scroll the "+highlight(u"mouse wheel down");
 return u"press "+control(in,action);
}
inline std::u16string sentence(Topic t,const Input& in){
 switch(t){
 case Topic::Grab:{auto movement=in.layout?u"the directional buttons":u"the "+key_name(in.keys[13])+u", "+key_name(in.keys[12])+u", "+key_name(in.keys[15])+u" and "+key_name(in.keys[14])+u" keys";return u"To grab onto a block, "+operate(in,0)+u".\nMove it with "+movement+u".";}
 case Topic::Undo:return u"If you make a mistake moving a block, you can "+operate(in,6)+u" to UNDO it!";
 case Topic::Autoplay:return u"If you can't figure out how to proceed, "+operate(in,7)+u" to enable Autoplay.";
 case Topic::Phone:return u"To open the cellphone and check your mail, "+operate(in,2)+u".";
 case Topic::Drink:return u"Enjoy your time at the bar.\nTo drink alcohol, "+operate(in,3)+u".\nTo get up and walk around, "+operate(in,1)+u".";
 case Topic::Menu:return u"To open the menu, "+operate(in,10)+u".";
 case Topic::Rapunzel:return u"To find out, "+operate(in,0)+u"!";
 default:return {};
 }
}
inline unsigned voice_for(Topic t,unsigned original){
 switch(t){case Topic::Grab:return 10056;case Topic::Undo:return 10060;case Topic::Autoplay:return 10065;case Topic::Phone:return 18558;case Topic::Drink:return 18554;case Topic::Menu:return 18555;case Topic::Rapunzel:return 40560;default:return original;}
}
inline std::u16string replace(const uint16_t* source,const Input& in){
 if(!source||source[0]!=0xd841||source[1]!=0xda61)return {};
 auto t=topic(source[2]);if(t==Topic::None||!english(t,plain(source)))return {};
 std::u16string result={char16_t(0xd841),char16_t(0xda61),char16_t(voice_for(t,source[2])),char16_t(source[3]),char16_t(0xd901),char16_t(1)};
 auto text=sentence(t,in);for(auto c:text)result+=c==u'\n'?char16_t(0):c;
 result+=char16_t(0);result+=char16_t(0xd821);return result;
}
}
