#include "controller_tutorial_text.h"
#include "controller_prompt_policy.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char**argv){
 assert(argc==2);unsigned cases=0,files=0;
 for(unsigned active=0;active<=4;++active)for(int preference=0;preference<=4;++preference){
  auto resolved=catherine::resolve_button_prompt_layout(active,preference);
  if(!active)assert(resolved==0);
  else if(!preference)assert(resolved==active);
  else assert((resolved==std::array<unsigned,5>{0,1,3,4,2}[preference]));
  assert(catherine::tutorial::voice_for(catherine::tutorial::Topic::Phone,18556)==18558);
 }
 for(auto& entry:std::filesystem::directory_iterator(argv[1])){
  if(entry.path().extension()!=".bin")continue;
  std::ifstream f(entry.path(),std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});assert(bytes.size()%2==0);std::vector<uint16_t> data(bytes.size()/2);memcpy(data.data(),bytes.data(),bytes.size());auto original=data;
  for(unsigned layout:{0u,1u,3u,4u,2u}){
   catherine::tutorial::Input in;in.layout=layout;auto rewritten=catherine::tutorial::replace(data.data(),in);assert(!rewritten.empty());auto text=catherine::tutorial::plain(reinterpret_cast<const uint16_t*>(rewritten.data()));assert(text.size()>10);assert(data==original);
   auto frozen=rewritten;in.keys[6]='F';in.keys[7]=catherine::kWheelUp;in.keys[2]='P';in.keys[3]=1;
   // Swap the Undo and Square/X/Y physical bindings in this custom profile.
   auto a=(in.mapping>>24)&15,b=(in.mapping>>12)&15;in.mapping^=(a^b)<<24;in.mapping^=(a^b)<<12;
   auto custom=catherine::tutorial::replace(data.data(),in);assert(!custom.empty());assert(rewritten==frozen);auto custom_text=catherine::tutorial::plain(reinterpret_cast<const uint16_t*>(custom.data()));
   auto topic=catherine::tutorial::topic(data[2]);
   if(topic==catherine::tutorial::Topic::Undo){assert(custom_text.find(layout?(layout==2||layout==4?u"Square":layout==3?u"Y button":u"X button"):u"F key")!=std::u16string::npos);}
   if(topic==catherine::tutorial::Topic::Autoplay&&!layout){assert(custom_text.find(u"scroll the mouse wheel up")!=std::u16string::npos);}
   if(topic==catherine::tutorial::Topic::Phone&&!layout)assert(custom_text.find(u"P key")!=std::u16string::npos);
   if(topic==catherine::tutorial::Topic::Drink&&!layout)assert(custom_text.find(u"Left Mouse Button")!=std::u16string::npos);
   if(topic==catherine::tutorial::Topic::Undo)assert(rewritten[2]==10060);
   cases+=2;
  }
  ++files;
 }
 assert(files==19);std::cout<<files<<" original message pages; "<<cases<<" default/custom input cases; immutable snapshots and voice selection passed.\n";
}
