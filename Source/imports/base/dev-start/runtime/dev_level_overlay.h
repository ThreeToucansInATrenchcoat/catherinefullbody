#pragma once
#include "../include/dev_overlay_draw.h"
#include "../generated/include/dev_loader_native.h"
#include "../generated/include/pc_dropdown.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace dev_level_picker {
constexpr uint32_t white=0xffffffffu,pink=0xff3698ffu,muted=0xbcb8beffu;
enum Field { Search,Area,Group,Results,Karma,Difficulty,Close,Load,LevelsTab,CutscenesTab,FieldCount };
struct Box {
    float x,y,w,h;
    bool contains(int px,int py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}
};
constexpr Box boxes[]={
    {340,172,594,62},{950,172,301,62},{1267,172,313,62},{340,283,1240,460},
    {340,796,402,60},{762,796,402,60},{1140,925,182,65},{1342,925,238,65},
    {340,78,160,59},{510,78,216,59}
};
constexpr int visible_rows=8;
constexpr const char* areas[]={"All areas","Story puzzles","Bar","Landings","Rooms","Earned checkpoints","Babel","Colosseum","Rapunzel","Cutscenes"};
constexpr const char* scene_types[]={"All cutscenes","Movies","In-game scenes"};
constexpr const char* books[]={"Classic","Classic Extra","Remix","Remix Extra"};
constexpr const char* difficulties[]={"Keep earned","Hard","Normal","Easy","Safety"};
constexpr int karma_values[]={0,0,32,64,96,128,160,192,224,255};
constexpr const char* karma_labels[]={"Keep earned","0","32","64","96","128","160","192","224","255"};
struct Entry {CatherineDevTarget target;std::string key,label;};
struct Stage {int day,stage;std::string label;};
struct Model {
    std::vector<Entry> entries;
    std::vector<int> matches;
    std::vector<Stage> stages;
    std::string query,notice;
    Field focus=Results,popup=Results;
    bool cutscenes=false,replace_query=false;
    int area=0,scene_type=0,group=0,selected=0,scroll=0,karma=0,difficulty=0,popup_row=0,popup_scroll=0;
    int move=0,click=0,back=0,wheel=0;
    unsigned generation=0;
};
static Model model;
static std::mutex frame_lock;
static std::string lower(std::string str) {
    for(auto& c:str)c=char(std::tolower(static_cast<unsigned char>(c)));
    return str;
}
static const CatherineDevTarget* target() {
    return model.selected>=0&&model.selected<int(model.matches.size())?&model.entries[model.matches[model.selected]].target:nullptr;
}
static void select(int index){
    const int n=int(model.matches.size());
    index=n?std::clamp(index,0,n-1):0;
    if(index!=model.selected)model.notice.clear();
    model.selected=index;
    model.scroll=std::clamp(model.scroll,std::max(0,index-visible_rows+1),index);
    model.scroll=std::min(model.scroll,std::max(0,n-visible_rows));
}
static bool scene_matches(const CatherineDevTarget& t){
    const bool movie=t.id>=30000&&t.id<40000;
    return !model.scene_type||(model.scene_type==1)==movie;
}
static void filter(){
    const auto* previous=target();
    const uint32_t previous_id=previous?previous->id:0;
    model.matches.clear();
    const auto query=lower(model.query);
    int next=0;
    for(int i=0;i<int(model.entries.size());++i){
        const auto& entry=model.entries[i];const auto& t=entry.target;
        if((t.kind==CATHERINE_DEV_MOVIE)!=model.cutscenes)continue;
        if(model.cutscenes?!scene_matches(t):(model.area&&int(t.kind)!=model.area))continue;
        if(model.group){
            const Stage& s=model.stages[model.group-1];
            if(t.day!=s.day||(!model.cutscenes&&t.stage!=s.stage))continue;
        }
        size_t pos=0;
        bool match=true;
        while((pos=query.find_first_not_of(' ',pos))!=std::string::npos){
            const auto end=query.find(' ',pos);
            if(entry.key.find(query.substr(pos,end-pos))==std::string::npos){match=false;break;}
            if(end==std::string::npos)break;
            pos=end+1;
        }
        if(!match)continue;
        if(query==std::to_string(t.id)){
            model.matches.assign(1,i);next=0;break;
        }
        if(t.id==previous_id)next=int(model.matches.size());
        model.matches.push_back(i);
    }
    model.selected=next;
    model.scroll=0;select(next);
    model.notice.clear();
}
static void stages(){
    model.group=0;
    model.stages.clear();
    if(model.cutscenes){
        for(const auto& entry:model.entries){
            const auto& t=entry.target;
            if(t.kind!=CATHERINE_DEV_MOVIE||!scene_matches(t))continue;
            if(std::any_of(model.stages.begin(),model.stages.end(),[&](const Stage& s){return s.day==t.day;}))continue;
            const std::string name=t.day<0?"Other scenes":t.day==0?"Prologue":"Day "+std::to_string(t.day);
            model.stages.push_back({t.day,-1,name});
        }
        std::sort(model.stages.begin(),model.stages.end(),[](const Stage& a,const Stage& b){
            if(a.day<0||b.day<0)return a.day>=0&&b.day<0;
            return a.day<b.day;
        });
    }else if(model.area&&model.area!=CATHERINE_DEV_COLOSSEUM){
        for(const auto& entry:model.entries){
            const auto& t=entry.target;
            if(int(t.kind)!=model.area||(t.day<0&&t.stage<0))continue;
            if(std::any_of(model.stages.begin(),model.stages.end(),[&](const Stage& s){return s.day==t.day&&s.stage==t.stage;}))continue;
            std::string name;
            if(t.kind==CATHERINE_DEV_RAPUNZEL&&t.stage>=1&&t.stage<=4)name=books[t.stage-1];
            else if(t.kind==CATHERINE_DEV_BABEL)name=t.stage==1?"Single":"Pair";
            else {
                if(t.day>=0&&t.kind!=CATHERINE_DEV_PUZZLE)name="Day "+std::to_string(t.day);
                if(t.stage>=0){if(!name.empty())name+=" / ";name+="Stage "+std::to_string(t.stage);}
            }
            model.stages.push_back({t.day,t.stage,name});
        }
    }
    filter();
}
static void change_mode(bool cutscenes){
    model.cutscenes=cutscenes;
    model.query.clear();model.notice.clear();model.popup=Results;model.focus=Results;
    stages();
}
static void search_changed(){
    // Exact IDs are shortcuts across both tabs and any active area/day filter.
    const auto first=model.query.find_first_not_of(' '),last=model.query.find_last_not_of(' ');
    if(first!=std::string::npos){
        const auto id=model.query.substr(first,last-first+1);
        if(id.find_first_not_of("0123456789")==std::string::npos){
            for(const auto& entry:model.entries)if(std::to_string(entry.target.id)==id){
                const auto& t=entry.target;
                model.cutscenes=t.kind==CATHERINE_DEV_MOVIE;
                if(model.cutscenes&&!scene_matches(t))model.scene_type=0;
                if(!model.cutscenes&&model.area&&model.area!=int(t.kind))model.area=0;
                model.query=id;stages();return;
            }
        }
    }
    filter();
}
// Conservative Segoe UI glyph advances at the 29px result-row size. Leave a
// gutter before the ID column, including the nine-digit native event IDs.
static std::string row_label(const char* label){
    const std::string s(label);
    float width=0;
    for(size_t i=0;i<s.size();++i){
        const unsigned char c=static_cast<unsigned char>(s[i]);
        if((c&0xc0)==0x80)continue;
        if(c>=0x80)width+=30;
        else if(std::strchr(" ijlI.,:;'|!",c))width+=10;
        else if(std::strchr("MWmw@%&QOG",c))width+=28;
        else if(std::isupper(c))width+=23;
        else width+=18;
        if(width>1000){
            const auto word=s.find_last_of(" -",i);
            return s.substr(0,word!=std::string::npos&&i-word<14?word:i)+"...";
        }
    }
    return s;
}
static void catalog(){
    const uint32_t count=std::min(4096u,catherine_dev_loader_count());
    for(uint32_t i=0;i<count;++i){
        CatherineDevTarget t{};t.version=CATHERINE_DEV_LOADER_VERSION;
        if(!catherine_dev_loader_describe(i,&t)||t.kind<1||t.kind>=sizeof(areas)/sizeof(*areas))continue;
        model.entries.push_back({t,lower(std::to_string(t.id)+" "+areas[t.kind]+" "+t.label),row_label(t.label)});
    }
    if(std::any_of(model.entries.begin(),model.entries.end(),[](const Entry& e){return e.target.id==10501;}))
        model.entries.erase(std::remove_if(model.entries.begin(),model.entries.end(),[](const Entry& e){return e.target.id==5000;}),model.entries.end());
    stages();
}
static bool enabled(Field field){
    const auto* t=target();
    if(field==Group)return !model.stages.empty();
    if(field==Karma)return t&&(t->capabilities&CATHERINE_DEV_CAN_KARMA);
    if(field==Difficulty)return t&&(t->capabilities&CATHERINE_DEV_CAN_DIFFICULTY);
    if(field==Load)return t&&(t->capabilities&CATHERINE_DEV_CAN_LOAD);
    return true;
}
static int option_count(Field field){
    switch(field){
    case Area:return model.cutscenes?int(sizeof(scene_types)/sizeof(*scene_types)):CATHERINE_DEV_MOVIE;
    case Group:return 1+int(model.stages.size());
    case Karma:return int(sizeof(karma_labels)/sizeof(*karma_labels));
    case Difficulty:return int(sizeof(difficulties)/sizeof(*difficulties));
    default:return 0;
    }
}
static const char* option(Field field,int i){
    switch(field){
    case Area:return model.cutscenes?scene_types[i]:areas[i];
    case Group:return i?model.stages[i-1].label.c_str():"All";
    case Karma:return karma_labels[i];
    case Difficulty:return difficulties[i];
    default:return "";
    }
}
static int value(Field field){
    if(field==Area)return model.cutscenes?model.scene_type:model.area;
    if(field==Group)return model.group;
    if(field==Karma)return model.karma;
    return model.difficulty;
}
static CatherineDevicePopup popup_geometry(Field field){
    const Box& box=boxes[field];
    const unsigned count=unsigned(option_count(field));
    const unsigned visible=std::min(count,8u);
    const float height=55.0f*visible+20.0f;
    return {box.x,box.y+box.h+height<1020?box.y+box.h:box.y-height,box.w,count,visible};
}
static void popup_select(int at){
    const auto p=popup_geometry(model.popup);
    model.popup_row=std::clamp(at,0,int(p.count)-1);
    model.popup_scroll=std::clamp(model.popup_scroll,std::max(0,model.popup_row-int(p.visible)+1),model.popup_row);
    model.popup_scroll=std::min(model.popup_scroll,int(p.count-p.visible));
}
static void open_popup(Field field){
    if(!enabled(field))return;
    model.focus=model.popup=field;
    model.popup_scroll=0;
    popup_select(value(field));
}
static void choose(){
    const auto which=model.popup;
    const int selected=model.popup_row;
    model.popup=Results;
    if(which==Area){if(model.cutscenes)model.scene_type=selected;else model.area=selected;stages();}
    else if(which==Group){model.group=selected;filter();}
    else if(which==Karma)model.karma=selected;
    else if(which==Difficulty)model.difficulty=selected;
    model.focus=Results;
}
static void load(){
    const auto* t=target();
    if(!enabled(Load)||!t)return;
    CatherineDevLoadRequest request{};
    request.version=CATHERINE_DEV_LOADER_VERSION;request.target_id=t->id;
    request.karma_enabled=enabled(Karma)&&model.karma!=0;
    request.karma_value=request.karma_enabled?karma_values[model.karma]:0;
    request.difficulty=enabled(Difficulty)?model.difficulty-1:-1;request.route=-1;
    if(catherine_dev_loader_request(&request))catherine_dev_overlay_close();
    else model.notice="This destination could not be loaded.";
}
static void next_focus(int direction){
    int next=model.focus;
    do{next=(next+direction+FieldCount)%FieldCount;}while(!enabled(static_cast<Field>(next)));
    model.focus=static_cast<Field>(next);
}
static void input(){
    const unsigned act=catherine_dev_overlay_actions();
    const int move=catherine_dev_overlay_mouse(2),click=catherine_dev_overlay_mouse(3);
    const int wheel=catherine_dev_overlay_mouse(5),back=catherine_dev_overlay_mouse(6);
    const int x=catherine_dev_overlay_mouse(0),y=catherine_dev_overlay_mouse(1);
    const bool positioned=catherine_dev_overlay_mouse(4)!=0;
    const bool clicked=positioned&&model.click!=click,moved=positioned&&model.move!=move;
    const bool right=positioned&&model.back!=back;
    const int delta=std::clamp(static_cast<int>(static_cast<unsigned>(wheel)-static_cast<unsigned>(model.wheel)),-16,16);
    model.move=move;model.click=click;model.wheel=wheel;model.back=back;
    if((act&32)||right){
        if(model.popup!=Results)model.popup=Results;
        else catherine_dev_overlay_close();
        return;
    }
    bool changed=false;
    for(unsigned ch;(ch=catherine_dev_overlay_character())!=0;){
        if(model.replace_query){model.query.clear();model.replace_query=false;changed=true;}
        if(ch==8){if(!model.query.empty()){model.query.pop_back();changed=true;}}
        else if(ch>=32&&ch<=126&&model.query.size()<80){model.query.push_back(char(ch));changed=true;}
    }
    if(changed){model.focus=Search;model.popup=Results;search_changed();}
    const int step=(act&1)?-1:(act&2)?1:(act&64)?-visible_rows:(act&128)?visible_rows:0;
    if(clicked){
        for(const auto tab:{LevelsTab,CutscenesTab})if(boxes[tab].contains(x,y)){
            if(model.cutscenes!=(tab==CutscenesTab))change_mode(tab==CutscenesTab);
            else model.popup=Results;
            return;
        }
    }
    if(model.popup!=Results){
        const auto p=popup_geometry(model.popup);
        if(step)popup_select(model.popup_row+step);
        const int hit=catherine_dropdown_hit(&p,float(x),float(y));
        if(delta&&positioned&&hit>=0){
            model.popup_scroll=std::clamp(model.popup_scroll-delta,0,int(p.count-p.visible));
            popup_select(model.popup_scroll+hit);
        }
        if((moved||clicked)&&hit>=0)popup_select(model.popup_scroll+hit);
        if((act&16)||(clicked&&hit>=0))choose();
        else if(clicked)model.popup=Results;
        return;
    }
    if((act&256)||(act&8))next_focus(1);
    if((act&512)||(act&4))next_focus(-1);
    const auto n=unsigned(model.matches.size());
    const auto& b=boxes[Results];
    const CatherineDevicePopup list={b.x,b.y,b.w,n,std::min(n,unsigned(visible_rows))};
    const int hit=catherine_dropdown_hit(&list,float(x),float(y));
    if(delta&&positioned&&b.contains(x,y)){
        model.scroll=std::clamp(model.scroll-delta,0,std::max(0,int(n)-visible_rows));
    }
    if(clicked&&hit>=0){model.focus=Results;select(model.scroll+hit);}
    if(clicked){
        if(hit>=0)return;
        for(int f=Search;f<=Load;++f){
            const auto field=static_cast<Field>(f);
            if(!enabled(field)||!boxes[f].contains(x,y))continue;
            model.focus=field;
            if(field==Load)load();
            else if(field==Close)catherine_dev_overlay_close();
            else if(option_count(field))open_popup(field);
            return;
        }
    }
    if(step){
        if(model.focus==Results||model.focus==Search){model.focus=Results;select(model.selected+step);}
        else if(option_count(model.focus))open_popup(model.focus);
        else next_focus(step<0?-1:1);
    }
    if(act&16){
        if(model.focus==Results||model.focus==Load||model.focus==Search)load();
        else if(model.focus==Close)catherine_dev_overlay_close();
        else if(model.focus==LevelsTab||model.focus==CutscenesTab)change_mode(model.focus==CutscenesTab);
        else open_popup(model.focus);
    }
}
static void draw(void* ctx,CatherineOverlayRect rect,CatherineOverlayText text){
    rect(ctx,0,0,1920,1080,0x00000080u);
    rect(ctx,300,64,1320,952,0x19191bf8u);
    for(const auto tab:{LevelsTab,CutscenesTab}){
        const auto& b=boxes[tab];
        const bool active=model.cutscenes==(tab==CutscenesTab);
        text(ctx,b.x+12,b.y+2,37,tab==LevelsTab?"Levels":"Cutscenes",active?white:muted);
        if(active)rect(ctx,b.x,b.y+b.h-4,b.w,4,pink);
        if(model.focus==tab)catherine_dropdown_outline(ctx,rect,b.x,b.y,b.w,b.h,pink);
    }
    const auto box=[&](Field f,const char* label,const char* content){
        const Box& b=boxes[f];
        const uint32_t color=!enabled(f)?muted:model.focus==f?pink:white;
        text(ctx,b.x,b.y-29,23,label,muted);
        rect(ctx,b.x,b.y,b.w,b.h,0x202020ffu);
        catherine_dropdown_outline(ctx,rect,b.x,b.y,b.w,b.h,color);
        text(ctx,b.x+17,b.y+15,28,content,color);
        if(f!=Search)text(ctx,b.x+b.w-43,b.y+15,28,"v",color);
    };
    std::string shown=model.query.size()>32?"..."+model.query.substr(model.query.size()-32):model.query;
    box(Search,"Search",shown.empty()?"Name or destination ID":shown.c_str());
    box(Area,model.cutscenes?"Type":"Area",option(Area,value(Area)));
    box(Group,model.cutscenes?"Day":model.area==CATHERINE_DEV_RAPUNZEL?"Book":model.area==CATHERINE_DEV_BABEL?"Players":"Stage / day",option(Group,model.group));
    const unsigned n=unsigned(model.matches.size());
    char count[48];std::snprintf(count,sizeof count,"%u destination%s",n,n==1?"":"s");
    text(ctx,340,249,23,count,muted);
    const Box& b=boxes[Results];
    const CatherineDevicePopup list={b.x,b.y,b.w,n,std::min(n,unsigned(visible_rows))};
    if(n){
        const int hovered=model.popup==Results&&catherine_dev_overlay_mouse(4)
            ?catherine_dropdown_hit(&list,float(catherine_dev_overlay_mouse(0)),float(catherine_dev_overlay_mouse(1))):-1;
        catherine_dropdown_frame(ctx,rect,&list,unsigned(model.scroll),255);
        for(unsigned row=0;row<list.visible;++row){
            const int index=model.scroll+int(row);
            const auto& entry=model.entries[model.matches[index]];
            const auto& item=entry.target;
            const float y=b.y+20+55*row;
            if(index==model.selected)
                rect(ctx,b.x+2,b.y+10+55*row,b.w-14,55,0xffffff20u);
            const uint32_t color=!(item.capabilities&CATHERINE_DEV_CAN_LOAD)?muted:
                (index==model.selected||int(row)==hovered)?pink:white;
            char id[16];std::snprintf(id,sizeof id,"%u",item.id);
            text(ctx,b.x+20,y,29,entry.label.c_str(),color);text(ctx,b.x+b.w-166,y+2,25,id,color);
        }
    }else text(ctx,b.x+18,b.y+18,28,"No matching destinations",white);
    if(model.cutscenes&&target()){
        const auto* scene=target();
        if(model.entries[model.matches[model.selected]].label!=scene->label)
            text(ctx,340,762,23,scene->label,white);
        text(ctx,340,809,24,scene->state_description,muted);
    }
    if(enabled(Karma))box(Karma,"Karma",option(Karma,model.karma));
    if(enabled(Difficulty))box(Difficulty,"Difficulty",option(Difficulty,model.difficulty));
    const char* note=model.notice.empty()?"Loads use a temporary save until restart.":model.notice.c_str();
    CatherineDevLoadStatus status{};status.version=CATHERINE_DEV_LOADER_VERSION;catherine_dev_loader_status(&status);
    if(model.notice.empty()&&(status.busy||status.phase==CATHERINE_DEV_ERROR))note=status.message;
    std::string message(note);if(message.size()>76)message=message.substr(0,73)+"...";
    text(ctx,340,889,24,message.c_str(),muted);
    text(ctx,340,947,23,"Type to search   /   Enter to load   /   F8 to close",muted);
    for(const auto f:{Close,Load}){
        const Box& button=boxes[f];
        const uint32_t color=!enabled(f)?muted:(f==Load||model.focus==f)?pink:white;
        rect(ctx,button.x,button.y,button.w,button.h,0x202020ffu);
        catherine_dropdown_outline(ctx,rect,button.x,button.y,button.w,button.h,color);
        text(ctx,button.x+26,button.y+16,30,f==Load?(model.cutscenes?"Play scene":"Load map"):"Close",color);
    }
    if(model.popup!=Results){
        const auto p=popup_geometry(model.popup);
        catherine_dropdown_frame(ctx,rect,&p,unsigned(model.popup_scroll),255);
        for(unsigned i=0;i<p.visible;++i){
            const int row=model.popup_scroll+int(i);
            text(ctx,p.left+18,p.top+20+55*i,28,option(model.popup,row),row==model.popup_row?pink:white);
        }
    }
}
}
extern "C" void catherine_dev_overlay_frame(void *ctx,CatherineOverlayRect rect,CatherineOverlayText text){
    using namespace dev_level_picker;
    if(!rect||!text||!catherine_dev_overlay_visible())return;
    std::lock_guard<std::mutex> lock(frame_lock);
    if(!catherine_dev_overlay_visible())return;
    const unsigned generation=catherine_dev_overlay_generation();
    if(model.generation!=generation||model.entries.empty()){
        if(model.entries.empty())catalog();
        model.generation=generation;model.popup=Results;model.focus=Results;model.replace_query=!model.query.empty();
        model.move=catherine_dev_overlay_mouse(2);model.click=catherine_dev_overlay_mouse(3);
        model.wheel=catherine_dev_overlay_mouse(5);model.back=catherine_dev_overlay_mouse(6);
    }
    input();
    if(catherine_dev_overlay_visible())draw(ctx,rect,text);
}
