#ifdef _WIN32
#include "perf_background.h"
#include "gui.h"
#include "dev-start/generated/include/dev_loader_native.h"
#include "dev-start/runtime/dev_level_overlay.h"
#include "gui-tilde-input.h"
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cstdint>
#include <cwctype>
#include <sstream>
#include <deque>
#include <mutex>

namespace {
std::atomic<bool> opened{false}, paused{false}, pause_supported{false};
std::atomic<ULONGLONG> release_until{0};
extern "C" int catherine_dev_enabled(void);
std::atomic<bool> overlay_open{false};
std::atomic<unsigned> overlay_generation{0};
std::atomic<unsigned> overlay_actions{0};
std::mutex overlay_text_lock;
std::deque<unsigned> overlay_text;
bool hidden_test_panel_enabled() {
 char value[4]{};
 return perf_background_window() && GetEnvironmentVariableA("NX_HEADLESS",value,sizeof(value))==1 && value[0]=='1';
}
void open_overlay() {
 if (!catherine_dev_enabled()) {catherine_dev_loader_set_startup_menu(0);return;}
 std::lock_guard<std::mutex> guard(overlay_text_lock);
 overlay_text.clear();
 overlay_actions.store(0,std::memory_order_release);
 overlay_generation.fetch_add(1,std::memory_order_release);
 overlay_open.store(true,std::memory_order_release);
}
unsigned overlay_key(WPARAM key) {
 switch(key) {
 case VK_UP:return 1; case VK_DOWN:return 2;
 case VK_LEFT:return 4; case VK_RIGHT:return 8;
 case VK_RETURN:return 16; case VK_ESCAPE:return 32;
 case VK_PRIOR:return 64; case VK_NEXT:return 128;
 case VK_TAB:return GetKeyState(VK_SHIFT)<0 ? 512 : 256;
 default:return 0;
 }
}
CatherineDevTildeInput tilde_input;
HWND owner_window{}, panel{}, category{}, stage{}, destination{}, search{}, results{}, result_count{};
HWND category_label{}, stage_label{}, search_label{}, details{}, save_note{}, load{}, status{};
HWND karma{}, difficulty{}, route{}, option_labels[3]{};
HFONT font{};
UINT dpi=96;
std::vector<std::wstring> search_keys;
std::vector<CatherineDevTarget> targets;
struct Choice { std::wstring label; int value; };
// Verified native counter0 domain; polarity is intentionally not inferred.
// Slot0 in each control is still Keep earned state, selected on every target change.
std::vector<Choice> options[3]={
 {{L"0",0},{L"32",32},{L"64",64},{L"96",96},{L"128 (native default)",128},
  {L"160",160},{L"192",192},{L"224",224},{L"255",255}},
 {{L"Hard",0},{L"Normal",1},{L"Easy",2},{L"Safety",3}}, {}
};
std::vector<std::pair<int,int>> groups;
std::vector<unsigned> shown;
constexpr UINT kRefresh=0xD17,kRelease=0xD18;

constexpr int kCategory=101,kStage=102,kDestination=103,kKarma=104,kDifficulty=105,kRoute=106,
              kLoad=107,kClose=108,kSearch=109,kResults=110;
const wchar_t *kind_names[]={L"All destinations",L"Puzzles",L"Bar",L"Landings",L"Rooms",L"Earned checkpoints",
                            L"Babel",L"Colosseum",L"Rapunzel"};
std::wstring wide(const char *s,size_t cap) {
 size_t n=0;while(n<cap&&s[n])++n;
 if(!n)return {};
 int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,int(n),nullptr,0);
 if(!count)return L"Invalid text";
 std::wstring out(size_t(count),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,int(n),out.data(),count);return out;
}
void text(HWND h,const wchar_t *s){if(h)SetWindowTextW(h,s);}
void reset(HWND h){SendMessageW(h,CB_RESETCONTENT,0,0);}
void add(HWND h,const wchar_t *s){SendMessageW(h,CB_ADDSTRING,0,LPARAM(s));}
int selection(HWND h){return int(SendMessageW(h,CB_GETCURSEL,0,0));}
const CatherineDevTarget *selected(){
 int row=int(SendMessageW(results,LB_GETCURSEL,0,0));
 return row>=0&&size_t(row)<shown.size()?&targets[shown[size_t(row)]]:nullptr;
}
std::wstring lowercase(std::wstring value) {
 std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
 return value;
}
void layout();
void release_cursor(){if(perf_background_window())return;ClipCursor(nullptr);SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));}
void hide() {
 if(!opened.exchange(false))return;
 catherine_dev_loader_set_startup_menu(0);
 paused.store(false,std::memory_order_release);
 release_until.store(GetTickCount64()+180,std::memory_order_release);
 KillTimer(panel,kRefresh);SetTimer(panel,kRelease,200,nullptr);ShowWindow(panel,SW_HIDE);
 if(IsWindow(owner_window)&&!perf_background_window()){SetActiveWindow(owner_window);SetFocus(owner_window);PostMessageW(owner_window,WM_SETCURSOR,WPARAM(owner_window),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));}
}
void choices(HWND h,unsigned field,bool enabled) {
 reset(h);add(h,L"Keep earned state");
 for(const auto &v:options[field])add(h,v.label.c_str());
 SendMessageW(h,CB_SETCURSEL,0,0);EnableWindow(h,enabled&&!options[field].empty());
}
void describe_selected() {
 const auto *t=selected();
 text(details,t?wide(t->state_description,sizeof(t->state_description)).c_str():L"No destinations match these filters.");
 const unsigned caps=t?t->capabilities:0;
 HWND controls[]={karma,difficulty,route};
 const unsigned capabilities[]={CATHERINE_DEV_CAN_KARMA,CATHERINE_DEV_CAN_DIFFICULTY,CATHERINE_DEV_CAN_ROUTE};
 for(unsigned i=0;i<3;++i){
  const bool available=(caps&capabilities[i])&&!options[i].empty();
  choices(controls[i],i,available);
  ShowWindow(option_labels[i],available?SW_SHOWNA:SW_HIDE);
  ShowWindow(controls[i],available?SW_SHOWNA:SW_HIDE);
 }
 layout();
}
void select_row(int row) {
 SendMessageW(results,LB_SETCURSEL,row,0);
 SendMessageW(destination,CB_SETCURSEL,row,0);
 describe_selected();
}
void fill_destinations() {
 const auto *previous=selected();
 const uint32_t previous_id=previous?previous->id:0;
 wchar_t query[161]{};
 GetWindowTextW(search,query,ARRAYSIZE(query));
 std::wistringstream input(lowercase(query));
 std::vector<std::wstring> terms;
 for(std::wstring term;input>>term;)terms.push_back(std::move(term));
 SendMessageW(results,WM_SETREDRAW,FALSE,0);
 SendMessageW(results,LB_RESETCONTENT,0,0);
 reset(destination);
 shown.clear();
 int row=0;
 const int k=selection(category),g=selection(stage);
 for(unsigned i=0;i<targets.size();++i){
  const auto &t=targets[i];
  if(k>0&&t.kind!=unsigned(k))continue;
  if(g>0&&size_t(g-1)<groups.size()&&groups[size_t(g-1)]!=std::make_pair(t.day,t.stage))continue;
  if(std::any_of(terms.begin(),terms.end(),[&](const std::wstring& term){return search_keys[i].find(term)==std::wstring::npos;}))continue;
  if(t.id==previous_id)row=int(shown.size());
  shown.push_back(i);
  const auto label=wide(t.label,sizeof(t.label));
  const auto line=std::to_wstring(t.id)+L"\t"+label;
  SendMessageW(results,LB_ADDSTRING,0,LPARAM(line.c_str()));
  add(destination,label.c_str());
 }
 select_row(shown.empty()?-1:row);
 text(result_count,(std::to_wstring(shown.size())+(shown.size()==1?L" destination":L" destinations")).c_str());
 SendMessageW(results,WM_SETREDRAW,TRUE,0);
 InvalidateRect(results,nullptr,TRUE);
}
std::wstring group_label(const CatherineDevTarget& t) {
 if(t.kind==CATHERINE_DEV_BABEL)return t.stage==1?L"Single":L"Pair";
 if(t.kind==CATHERINE_DEV_RAPUNZEL){
  const wchar_t* books[]={L"Classic",L"Classic Extra",L"Remix",L"Remix Extra"};
  if(t.stage>=1&&t.stage<=4)return books[t.stage-1];
 }
 if(t.kind==CATHERINE_DEV_PUZZLE&&t.stage>=0)return L"Stage "+std::to_wstring(t.stage);
 if(t.day>=0&&t.stage>=0)return L"Day "+std::to_wstring(t.day)+L" / Stage "+std::to_wstring(t.stage);
 if(t.day>=0)return L"Day "+std::to_wstring(t.day);
 return L"Stage "+std::to_wstring(t.stage);
}
void fill_stages() {
 reset(stage);groups.clear();add(stage,L"All");
 const int kind=selection(category);
 if(kind>0&&kind!=CATHERINE_DEV_COLOSSEUM){
  for(const auto& t:targets){
   if(t.kind!=unsigned(kind)||(t.day<0&&t.stage<0))continue;
   const auto key=std::make_pair(t.day,t.stage);
   if(std::find(groups.begin(),groups.end(),key)!=groups.end())continue;
   groups.push_back(key);add(stage,group_label(t).c_str());
  }
 }
 text(stage_label,kind==CATHERINE_DEV_RAPUNZEL?L"Book":kind==CATHERINE_DEV_BABEL?L"Players":L"Stage / day");
 SendMessageW(stage,CB_SETCURSEL,0,0);EnableWindow(stage,!groups.empty());fill_destinations();
}
void refresh_catalog() {
 targets.clear();search_keys.clear();
 const uint32_t count=std::min(catherine_dev_loader_count(),4096u);
 for(uint32_t i=0;i<count;++i){
  CatherineDevTarget t{};t.version=CATHERINE_DEV_LOADER_VERSION;
  if(catherine_dev_loader_describe(i,&t)&&t.version==CATHERINE_DEV_LOADER_VERSION&&t.kind>=1&&t.kind<ARRAYSIZE(kind_names))targets.push_back(t);
 }
 // ID 5000 remains available to old CLI clients. The picker lists its canonical floor once.
 if(std::any_of(targets.begin(),targets.end(),[](const auto&t){return t.id==10501;}))
  targets.erase(std::remove_if(targets.begin(),targets.end(),[](const auto&t){return t.id==5000;}),targets.end());
 for(const auto& t:targets)
  search_keys.push_back(lowercase(std::to_wstring(t.id)+L" "+kind_names[t.kind]+L" "+wide(t.label,sizeof(t.label))));
 fill_stages();
}
void refresh_status() {
 CatherineDevLoadStatus s{};s.version=CATHERINE_DEV_LOADER_VERSION;catherine_dev_loader_status(&s);
 if(s.version!=CATHERINE_DEV_LOADER_VERSION){text(status,L"Loader version mismatch. Loading is unavailable.");EnableWindow(load,FALSE);return;}
 text(status,wide(s.message,sizeof(s.message)).c_str());
 auto*t=selected();EnableWindow(load,t&&(t->capabilities&CATHERINE_DEV_CAN_LOAD));
}
int option_value(HWND h,unsigned field,int fallback){int n=selection(h);return n>0&&size_t(n-1)<options[field].size()?options[field][size_t(n-1)].value:fallback;}
void request_load() {
 auto*t=selected();if(!t)return;
 CatherineDevLoadStatus s{};s.version=CATHERINE_DEV_LOADER_VERSION;catherine_dev_loader_status(&s);
 if(s.version!=CATHERINE_DEV_LOADER_VERSION||!(t->capabilities&CATHERINE_DEV_CAN_LOAD)){refresh_status();return;}
 CatherineDevLoadRequest r{};r.version=CATHERINE_DEV_LOADER_VERSION;r.target_id=t->id;
 r.karma_enabled=(t->capabilities&CATHERINE_DEV_CAN_KARMA)&&selection(karma)>0;
 r.karma_value=option_value(karma,0,0);
 r.difficulty=(t->capabilities&CATHERINE_DEV_CAN_DIFFICULTY)?option_value(difficulty,1,-1):-1;
 r.route=(t->capabilities&CATHERINE_DEV_CAN_ROUTE)?option_value(route,2,-1):-1;
 if(!catherine_dev_loader_request(&r)){refresh_status();return;}
 refresh_status();
 hide();
}
int px(int logical){return MulDiv(logical,int(dpi),96);}
bool visible_style(HWND h){return (GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=0;}
void layout() {
 if(!status)return;
 RECT client{};GetClientRect(panel,&client);
 const int width=MulDiv(client.right,96,int(dpi)),height=MulDiv(client.bottom,96,int(dpi));
 const int margin=18,gap=12,inner=width-2*margin,half=(inner-gap)/2;
 auto place=[](HWND h,int x,int y,int w,int ht){MoveWindow(h,px(x),px(y),px(std::max(w,1)),px(std::max(ht,1)),TRUE);};
 place(search_label,margin,12,inner,19);
 place(search,margin,33,inner,29);
 place(category_label,margin,73,half,18);place(stage_label,margin+half+gap,73,half,18);
 place(category,margin,93,half,235);place(stage,margin+half+gap,93,inner-half-gap,350);
 place(result_count,margin,134,inner,20);
 const int actions=height-50;
 place(load,width-margin-198,actions,114,34);place(GetDlgItem(panel,kClose),width-margin-74,actions,74,34);
 place(status,margin,actions,inner-212,38);
 place(save_note,margin,actions-29,inner,20);
 HWND controls[]={karma,difficulty,route};
 int count=0;
 for(auto h:controls)if(visible_style(h))++count;
 const int options_top=actions-88;
 if(count){
  const int cell=(inner-(count-1)*gap)/count;
  int x=margin;
  for(int i=0;i<3;++i)if(visible_style(controls[i])){
   place(option_labels[i],x,options_top,cell,18);place(controls[i],x,options_top+20,cell,260);x+=cell+gap;
  }
 }
 const int details_top=count?options_top-46:actions-75;
 place(details,margin,details_top,inner,42);
 place(results,margin,155,inner,details_top-162);
}
HWND control(const wchar_t*cls,const wchar_t*label,DWORD style,int id=0,DWORD ex=0){
 return CreateWindowExW(ex,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,panel,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
}
void set_font(){
 HFONT next=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
     CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 if(!next)return;
 for(HWND child=GetWindow(panel,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
  SendMessageW(child,WM_SETFONT,WPARAM(next),TRUE);
 if(font)DeleteObject(font);
 font=next;
 int tab=36;
 SendMessageW(results,LB_SETTABSTOPS,1,LPARAM(&tab));
 SendMessageW(results,LB_SETITEMHEIGHT,0,px(24));
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
 switch(m){
 case WM_CLOSE:hide();return 0;
 case WM_SIZE:if(panel)layout();return 0;
 case WM_GETMINMAXINFO:{
  auto *info=reinterpret_cast<MINMAXINFO*>(l);
  info->ptMinTrackSize={px(500),px(490)};return 0;
 }
 case WM_DPICHANGED:{
  dpi=HIWORD(w);const RECT& r=*reinterpret_cast<const RECT*>(l);
  SetWindowPos(h,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
  if(panel){set_font();layout();}return 0;
 }
 case WM_ERASEBKGND:{RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(w),&r,GetSysColorBrush(COLOR_BTNFACE));return 1;}
 case WM_TIMER:if(w==kRefresh){refresh_status();return 0;}if(w==kRelease){KillTimer(h,kRelease);if(IsWindow(owner_window))PostMessageW(owner_window,WM_SETCURSOR,WPARAM(owner_window),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));return 0;}break;
 case WM_COMMAND:{
  const int id=LOWORD(w),event=HIWORD(w);
  if(id==kSearch&&event==EN_CHANGE){fill_destinations();refresh_status();return 0;}
  if(id==kResults){
   if(event==LBN_SELCHANGE){select_row(int(SendMessageW(results,LB_GETCURSEL,0,0)));refresh_status();}
   else if(event==LBN_DBLCLK)request_load();
   return 0;
  }
  if(event==CBN_SELCHANGE){
   if(id==kCategory)fill_stages();
   if(id==kStage)fill_destinations();
   if(id==kDestination)select_row(selection(destination));
   refresh_status();return 0;
  }
  if(event==BN_CLICKED){if(id==kClose)hide();if(id==kLoad)request_load();return 0;}break;
 }
 case WM_DESTROY:
  opened.store(false);paused.store(false);panel=nullptr;status=nullptr;
  if(font){DeleteObject(font);font=nullptr;}return 0;
 }
 return DefWindowProcW(h,m,w,l);
}
bool create(HWND owner) {
 owner_window=owner;if(panel)return true;
 WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CatherineDevDirectStart";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
 if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
 using WindowDpi=UINT(WINAPI*)(HWND);
 auto window_dpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
 dpi=window_dpi?window_dpi(owner):96;
 if(!dpi)dpi=96;
 RECT pos{};GetWindowRect(owner,&pos);
 MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
 const auto& work=monitor.rcWork;
 const int width=std::min<int>(px(780),work.right-work.left),height=std::min<int>(px(650),work.bottom-work.top);
 const int x=std::clamp<int>((pos.left+pos.right-width)/2,work.left,work.right-width);
 const int y=std::clamp<int>((pos.top+pos.bottom-height)/2,work.top,work.bottom-height);
 panel=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,L"Level picker",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,wc.hInstance,nullptr);
 if(!panel)return false;
 search_label=control(L"STATIC",L"Search by name or destination ID",0);
 search=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,kSearch,WS_EX_CLIENTEDGE);
 SendMessageW(search,EM_SETLIMITTEXT,160,0);
 category_label=control(L"STATIC",L"Area",0);stage_label=control(L"STATIC",L"Stage / day",0);
 const DWORD combo=WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST;
 category=control(L"COMBOBOX",L"",combo,kCategory);stage=control(L"COMBOBOX",L"",combo,kStage);
 result_count=control(L"STATIC",L"",0);
 results=control(L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS|LBS_USETABSTOPS|LBS_NOINTEGRALHEIGHT,kResults,WS_EX_CLIENTEDGE);
 // Legacy selection messages still address control 103. Its index mirrors the visible list.
 destination=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,kDestination);ShowWindow(destination,SW_HIDE);
 details=control(L"STATIC",L"",SS_NOPREFIX,0);
 HWND *controls[]={&karma,&difficulty,&route};
 const wchar_t *names[]={L"Karma",L"Difficulty",L"Route"};
 for(int i=0;i<3;++i){option_labels[i]=control(L"STATIC",names[i],0);*controls[i]=control(L"COMBOBOX",L"",combo,kKarma+i);}
 save_note=control(L"STATIC",L"Level loads use a temporary save until you restart.",0);
 load=control(L"BUTTON",L"Load level",WS_TABSTOP|BS_DEFPUSHBUTTON,kLoad);
 control(L"BUTTON",L"Close",WS_TABSTOP,kClose);
 status=control(L"STATIC",L"",SS_NOPREFIX,0);
 for(auto*s:kind_names)add(category,s);
 SendMessageW(category,CB_SETCURSEL,0,0);set_font();layout();return true;
}
void toggle(HWND owner){
 if(opened.load()){hide();return;}
 if(!create(owner))return;
 paused.store(false);opened.store(true,std::memory_order_release);release_cursor();
 if(targets.empty())refresh_catalog();
 refresh_status();
 /* This legacy window exists only for the hidden, owned regression driver. */
 SetTimer(panel,kRefresh,250,nullptr);
}
}
extern "C" unsigned catherine_dev_overlay_generation(void) {return overlay_generation.load(std::memory_order_acquire);}
extern "C" int catherine_dev_overlay_visible(void) {
 return overlay_open.load(std::memory_order_acquire);
}
extern "C" void catherine_dev_overlay_close(void) {
 if (!overlay_open.exchange(false,std::memory_order_acq_rel)) return;
 release_until.store(GetTickCount64()+180,std::memory_order_release);
 catherine_dev_loader_set_startup_menu(0);
}
extern "C" unsigned catherine_dev_overlay_actions(void) {
 return overlay_actions.exchange(0,std::memory_order_acq_rel);
}
extern "C" unsigned catherine_dev_overlay_character(void) {
 std::lock_guard<std::mutex> guard(overlay_text_lock);
 if (overlay_text.empty()) return 0;
 unsigned c=overlay_text.front();
 overlay_text.pop_front();
 return c;
}
extern "C" void catherine_dev_overlay_pad(unsigned held) {
 static std::atomic<unsigned> previous{0};
 const unsigned edge=held & ~previous.exchange(held,std::memory_order_acq_rel);
 if (edge && catherine_dev_overlay_visible()) overlay_actions.fetch_or(edge,std::memory_order_release);
}
extern "C" int catherine_dev_gui_pause_requested(){return opened.load(std::memory_order_acquire)&&pause_supported.load(std::memory_order_acquire);}
extern "C" int catherine_dev_gui_input_blocked(){
 if(catherine_dev_overlay_visible() || opened.load(std::memory_order_acquire))return 1;
 auto until=release_until.load(std::memory_order_acquire);if(!until)return 0;
 if(GetTickCount64()<until)return 1;release_until.compare_exchange_strong(until,0);return 0;
}
extern "C" void catherine_dev_gui_set_pause_supported(int supported){pause_supported.store(supported!=0,std::memory_order_release);}
extern "C" void catherine_dev_gui_set_paused(int yes){paused.store(yes!=0,std::memory_order_release);}
extern "C" void catherine_dev_gui_set_choices(unsigned field,const char *const *labels,const int *values,unsigned count){
 if(field>=3||count>32||(!labels&&count)||(!values&&count))return;options[field].clear();
 for(unsigned i=0;i<count;++i)if(labels[i])options[field].push_back({wide(labels[i],128),values[i]});
 if(panel)describe_selected();
}
extern "C" int catherine_dev_gui_window_message(HWND owner,UINT m,WPARAM w,LPARAM l,LRESULT*out){
 if (m==WM_CREATE && catherine_dev_loader_startup_menu()) open_overlay();
 if ((m==WM_KEYDOWN || m==WM_KEYUP) && w==VK_F8 && catherine_dev_enabled()) {
  if (m==WM_KEYDOWN && !(l&(1ll<<30))) {
   if(catherine_dev_overlay_visible()) catherine_dev_overlay_close();
   else open_overlay();
  }
  if(out)*out=0;
  return 1;
 }
 if(catherine_dev_overlay_visible()) {
  if(m==WM_CHAR) {
   if(w==8 || (w>=32 && w<=126)) {
    std::lock_guard<std::mutex> guard(overlay_text_lock);
    if(overlay_text.size()<256)overlay_text.push_back((unsigned)w);
   }
   if(out)*out=0;
   return 1;
  }
  if(m==WM_KEYDOWN || m==WM_KEYUP) {
   if(m==WM_KEYDOWN) {
    const unsigned action=overlay_key(w);
    // Navigation can repeat, activation and Back are press edges.
    if(action && (!(l&(1ll<<30)) || action<=8 || action==64 || action==128))
     overlay_actions.fetch_or(action,std::memory_order_release);
   }
   if(out)*out=0;
   return 1;
  }
 }
 if(!hidden_test_panel_enabled() || !catherine_dev_enabled())return 0;
 const auto decision=tilde_input.event(m,w,l);
 if(decision!=CatherineDevTildeInput::Pass){
  if(decision==CatherineDevTildeInput::Toggle)toggle(owner);
  if(out)*out=0;return 1;
 }
 if(m==WM_DESTROY&&panel){DestroyWindow(panel);}
 return 0;
}
extern "C" int catherine_dev_gui_pretranslate(MSG*m){
 if(!panel||!opened.load()||!m)return 0;
 const auto decision=tilde_input.event(m->message,m->wParam,m->lParam);
 if(decision!=CatherineDevTildeInput::Pass){
  if(decision==CatherineDevTildeInput::Toggle)toggle(owner_window);
  return 1;
 }
 if((m->message==WM_KEYDOWN||m->message==WM_KEYUP)&&m->wParam==VK_ESCAPE){
  if(m->message==WM_KEYDOWN&&!(m->lParam&(1ll<<30)))hide();return 1;
 }
 if(m->message==WM_KEYDOWN&&!(m->lParam&(1ll<<30))){
  if(m->wParam==VK_RETURN&&(m->hwnd==search||m->hwnd==results||m->hwnd==load)){
   request_load();return 1;
  }
  if(m->hwnd==search&&m->wParam==VK_DOWN&&!shown.empty()){SetFocus(results);return 1;}
 }
 return IsDialogMessageW(panel,m)!=0;
}

#else
#include "C:/catherine/recomp/Source/platform/overlay_linux.inc"
#endif
