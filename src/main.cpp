// 鼠卫兵 0.3.5 — native Win32, no background service or game injection.
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdio>
#include "updater.h"
#include "audio.h"

using S = std::wstring;
struct Settings {
    S path, filter=L"onmyoji;YysLauncher";
    int width=1113, height=627, count=2, delay=1200, columns=0, opacity=100;
    bool lockRatio=true, bossEnabled=true, autoUpdates=true;
    int bossKey='H', bossMods=MOD_CONTROL|MOD_ALT;
    int wednesdayReminder=0;
};
struct Target {
    HWND hwnd{}; DWORD pid{}; uint64_t born{}; S title, process;
};
struct Saved {
    Target target; LONG_PTR ex{}; BYTE alpha=255; COLORREF color{}; DWORD flags=LWA_ALPHA; bool hasAlpha=false;
};
struct Monitor { HMONITOR handle; RECT work; S name; };
struct Cell { int x,y,w,h; };
struct LaunchSpec {S executable,args,directory;bool directClient=false;};
static HWND mainWindow, listWindow, statusWindow;
static HINSTANCE instance;
static HFONT bodyFont, titleFont;
static HBRUSH background, cardBrush;
static std::map<HWND,bool> hover;
static int dpi=96;
static bool detailsOpen=false, sizingFields=false, trayReady=false;
static int ratioWidth=16, ratioHeight=9;
static const UINT TrayMessage=WM_APP+1;
static UINT taskbarCreated;
static NOTIFYICONDATAW tray{};
static bool elevated=false, bossRegistered=false, bossActive=false, bossPanelVisible=false, bossBusy=false;
static int bossRegistration=3010, registeredKey=0, registeredMods=0, bossTicks=0;
static std::map<HWND,Target> bossHidden;
static Settings cfg;
static S configDir, configPath;
static bool loadingList=false, testMode=false, smokeMode=false;
static std::vector<Target> targets, manualTargets;
static std::map<HWND,Saved> saved;
static std::map<HWND,Target> hidden;
static std::map<HWND,Target> hiddenCompanions;
static std::vector<Monitor> monitors;
struct Arrangement { std::vector<Target> rows; HMONITOR monitor{}; int ticks=0, stable=0, width=0, height=0; bool resizing=false; };
static Arrangement arrangement;
static int launchRemaining=0, launchTotal=0;
static LaunchSpec launchSpec;
struct StartupWindow {DWORD pid;uint64_t born;HWND hwnd=nullptr;int width=0,height=0;ULONGLONG stableSince=0,lastSeen=0;};
static std::vector<StartupWindow> startupWindows;
static ULONGLONG startupDeadline=0;
static bool updateBusy=false;
static updates::Result* pendingUpdate=nullptr;
static std::vector<Target> closingWindows;
static std::set<HWND> confirmedDialogs;
static ULONGLONG closeDeadline=0;
static std::map<int,HWND> controls;
enum {
    Path=100, Browse, Count, Delay, Launch, Filter, Refresh,
    Pick, SelectAll, SelectNone, Width, Height, Resize, Columns, Screen, Arrange,
    Opacity, Alpha, Hide, Show, Help, List, Header, Subtitle,
    LPath, LCount, LDelay, LFilter, LWidth, LHeight, LColumns, LScreen,
    LOpacity, LWindows, Hint, Icon, Details, LockRatio, Background,
    Admin, LBoss, BossEnabled, BossKey, BossSave, BossDisable, AutoFind, CloseAll, About,
    Mute, Volume, LVolume, ApplyVolume
};
static int px(int n){return MulDiv(n,dpi,96);}
static S text(HWND w){int n=GetWindowTextLengthW(w); S s(n+1,L'\0');GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
static S value(int id){return text(controls[id]);}
static void set(int id,const S& s){SetWindowTextW(controls[id],s.c_str());}
static void status(const S& s){SetWindowTextW(statusWindow,s.c_str());}
static S lower(S s){for(auto& c:s)c=towlower(c);return s;}
static S folderOf(const S& path){auto p=path.find_last_of(L"\\/");return p==S::npos?L"":path.substr(0,p);}
static bool fileExists(const S& p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
static uint64_t hash(const void* p,size_t n){auto b=static_cast<const unsigned char*>(p);uint64_t h=14695981039346656037ULL;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ULL;}return h;}
static bool readBytes(const S& p,std::vector<unsigned char>& out){
    HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};bool ok=GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<=65536;
    if(ok){out.resize((size_t)size.QuadPart);DWORD got{};ok=ReadFile(f,out.data(),(DWORD)out.size(),&got,nullptr)&&got==out.size();}
    CloseHandle(f);return ok;
}
static bool atomicWrite(const S& p,const void* data,size_t n){
    S temp=p+L".tmp";
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD written{};bool ok=WriteFile(f,data,(DWORD)n,&written,nullptr)&&written==n&&FlushFileBuffers(f);
    CloseHandle(f);
    if(ok)ok=MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok)DeleteFileW(temp.c_str());return ok;
}
static bool number(const S& s,int lo,int hi,int& out){
    if(s.empty()||s.size()>8)return false;int n=0;
    for(wchar_t c:s){if(c<L'0'||c>L'9')return false;n=n*10+c-L'0';}
    if(n<lo||n>hi)return false;out=n;return true;
}
static S clean(S s){s.erase(std::remove(s.begin(),s.end(),L'\r'),s.end());s.erase(std::remove(s.begin(),s.end(),L'\n'),s.end());return s;}
static std::vector<unsigned char> encode(const Settings& c){
    S s=L"SWB-SETTINGS-1\npath="+clean(c.path)+L"\nfilter="+clean(c.filter)+L"\n";
    s+=L"width="+std::to_wstring(c.width)+L"\nheight="+std::to_wstring(c.height)+L"\ncount="+std::to_wstring(c.count);
    s+=L"\ndelay="+std::to_wstring(c.delay)+L"\ncolumns="+std::to_wstring(c.columns)+L"\nopacity="+std::to_wstring(c.opacity)+L"\n";
    s+=L"lockRatio="+S(c.lockRatio?L"1":L"0")+L"\n";
    s+=L"bossEnabled="+S(c.bossEnabled?L"1":L"0")+L"\nbossKey="+std::to_wstring(c.bossKey)+L"\nbossMods="+std::to_wstring(c.bossMods)+L"\n";
    s+=L"autoUpdates="+S(c.autoUpdates?L"1":L"0")+L"\n";
    s+=L"wednesdayReminder="+std::to_wstring(c.wednesdayReminder)+L"\n";
    uint64_t check=hash(s.data(),s.size()*sizeof(wchar_t));
    std::vector<unsigned char> b(8+s.size()*sizeof(wchar_t));memcpy(b.data(),&check,8);memcpy(b.data()+8,s.data(),s.size()*sizeof(wchar_t));return b;
}
static bool decode(const std::vector<unsigned char>& b,Settings& c){
    if(b.size()<40||(b.size()-8)%2)return false;
    uint64_t check;memcpy(&check,b.data(),8);if(check!=hash(b.data()+8,b.size()-8))return false;
    S s((const wchar_t*)(b.data()+8),(b.size()-8)/2);
    const S magic=L"SWB-SETTINGS-1\n";if(s.substr(0,magic.size())!=magic)return false;
    std::map<S,S> kv;size_t at=magic.size();
    while(at<s.size()){size_t end=s.find(L'\n',at);if(end==S::npos)return false;size_t eq=s.find(L'=',at);if(eq==S::npos||eq>=end)return false;S key=s.substr(at,eq-at);if(kv.count(key))return false;kv[key]=s.substr(eq+1,end-eq-1);at=end+1;}
    Settings candidate;
    if(kv.size()<8||kv.size()>16||!kv.count(L"path")||!kv.count(L"filter"))return false;
    for(auto& [key,value]:kv)if(key!=L"path"&&key!=L"args"&&key!=L"filter"&&key!=L"width"&&key!=L"height"&&key!=L"count"&&key!=L"delay"&&key!=L"columns"&&key!=L"opacity"&&key!=L"lockRatio"&&key!=L"sharpLaunch"&&key!=L"bossEnabled"&&key!=L"bossKey"&&key!=L"bossMods"&&key!=L"autoUpdates"&&key!=L"wednesdayReminder")return false;
    candidate.path=kv[L"path"];candidate.filter=kv[L"filter"];
    if(candidate.path.size()>32000||candidate.filter.empty()||candidate.filter.size()>256)return false;
    if(!number(kv[L"width"],160,7680,candidate.width)||!number(kv[L"height"],90,4320,candidate.height)||!number(kv[L"count"],1,20,candidate.count)||!number(kv[L"delay"],500,10000,candidate.delay)||!number(kv[L"columns"],0,20,candidate.columns)||!number(kv[L"opacity"],20,100,candidate.opacity))return false;
    if(kv.count(L"lockRatio")){int lock;if(!number(kv[L"lockRatio"],0,1,lock))return false;candidate.lockRatio=lock!=0;}
    // Older config files may contain sharpLaunch=0; startup DPI handling is now always enabled.
    if(kv.count(L"sharpLaunch")){int ignored;if(!number(kv[L"sharpLaunch"],0,1,ignored))return false;}
    if(kv.count(L"autoUpdates")){int enabled;if(!number(kv[L"autoUpdates"],0,1,enabled))return false;candidate.autoUpdates=enabled!=0;}
    if(kv.count(L"wednesdayReminder")&&!number(kv[L"wednesdayReminder"],0,99991231,candidate.wednesdayReminder))return false;
    if(kv.count(L"bossEnabled")||kv.count(L"bossKey")||kv.count(L"bossMods")){
        int enabled;if(!number(kv[L"bossEnabled"],0,1,enabled)||!number(kv[L"bossKey"],1,254,candidate.bossKey)||!number(kv[L"bossMods"],0,7,candidate.bossMods))return false;candidate.bossEnabled=enabled!=0;
    }
    c=candidate;return true;
}
static bool loadSettings(const S& p,Settings& c,int& source){
    std::vector<unsigned char> b;if(readBytes(p,b)&&decode(b,c)){source=0;return true;}
    if(readBytes(p+L".bak",b)&&decode(b,c)){source=1;return true;}source=2;return false;
}
static bool saveSettings(const S& p,const Settings& c){
    std::vector<unsigned char> old;Settings valid;
    if(readBytes(p,old)&&decode(old,valid)&&!atomicWrite(p+L".bak",old.data(),old.size()))return false;
    auto b=encode(c);return atomicWrite(p,b.data(),b.size());
}
static uint64_t birth(DWORD pid){
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return 0;
    FILETIME a,b,c,d;uint64_t n=0;if(GetProcessTimes(h,&a,&b,&c,&d))n=((uint64_t)a.dwHighDateTime<<32)|a.dwLowDateTime;CloseHandle(h);return n;
}
static bool valid(const Target& t){DWORD pid=0;GetWindowThreadProcessId(t.hwnd,&pid);return IsWindow(t.hwnd)&&pid==t.pid&&t.born!=0&&birth(pid)==t.born;}
static S processName(DWORD pid){
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return L"（无法读取）";
    wchar_t b[32768];DWORD n=32768;S s;if(QueryFullProcessImageNameW(h,0,b,&n)){s.assign(b,n);auto p=s.find_last_of(L"\\/");if(p!=S::npos)s=s.substr(p+1);}CloseHandle(h);return s;
}
static Target targetOf(HWND w){Target t;t.hwnd=w;GetWindowThreadProcessId(w,&t.pid);t.born=birth(t.pid);t.title=text(w);t.process=processName(t.pid);return t;}
static bool matches(const S& a,const S& b,const S& filters){
    (void)a;S hay=lower(b), f=lower(filters);size_t at=0;
    while(at<=f.size()){size_t end=f.find(L';',at);if(end==S::npos)end=f.size();S part=f.substr(at,end-at);while(!part.empty()&&iswspace(part.front()))part.erase(part.begin());while(!part.empty()&&iswspace(part.back()))part.pop_back();if(!part.empty()&&hay.find(part)!=S::npos)return true;at=end+1;}return false;
}
struct DiskHidden {uint64_t hwnd,born;uint32_t pid,reserved;};
static bool saveHidden(){
    std::vector<DiskHidden> rows;for(auto& [h,t]:hidden)if(valid(t))rows.push_back({(uint64_t)(uintptr_t)h,t.born,t.pid,0});for(auto& [h,t]:hiddenCompanions)if(valid(t))rows.push_back({(uint64_t)(uintptr_t)h,t.born,t.pid,1});
    std::vector<unsigned char> b(8+rows.size()*sizeof(DiskHidden));if(!rows.empty())memcpy(b.data()+8,rows.data(),b.size()-8);uint64_t check=hash(b.data()+8,b.size()-8);memcpy(b.data(),&check,8);return atomicWrite(configDir+L"\\hidden.dat",b.data(),b.size());
}
static void loadHidden(){
    std::vector<unsigned char> b;if(!readBytes(configDir+L"\\hidden.dat",b)||b.size()<8||(b.size()-8)%sizeof(DiskHidden)||b.size()>8+256*sizeof(DiskHidden))return;
    uint64_t check;memcpy(&check,b.data(),8);if(check!=hash(b.data()+8,b.size()-8))return;
    for(size_t at=8;at<b.size();at+=sizeof(DiskHidden)){DiskHidden d;memcpy(&d,b.data()+at,sizeof(d));Target t;t.hwnd=(HWND)(uintptr_t)d.hwnd;t.pid=d.pid;t.born=d.born;if(valid(t)&&!IsWindowVisible(t.hwnd)){t.title=text(t.hwnd);t.process=processName(t.pid);if(d.reserved)hiddenCompanions[t.hwnd]=t;else hidden[t.hwnd]=t;}}
}
struct CompanionScan {const Target* root;std::vector<Target>* rows;};
static bool ownerIs(HWND window,HWND root){HWND owner=GetWindow(window,GW_OWNER);for(int depth=0;owner&&depth<16;depth++,owner=GetWindow(owner,GW_OWNER))if(owner==root)return true;return false;}
static BOOL CALLBACK scanCompanions(HWND window,LPARAM context){auto scan=(CompanionScan*)context;if(window==scan->root->hwnd||!IsWindowVisible(window))return TRUE;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=scan->root->pid)return TRUE;LONG_PTR ex=GetWindowLongPtrW(window,GWL_EXSTYLE);if(ownerIs(window,scan->root->hwnd)||(ex&WS_EX_TOPMOST)){auto target=targetOf(window);if(valid(target)&&target.born==scan->root->born)scan->rows->push_back(target);}return TRUE;}
static std::vector<Target> companions(const Target& root){std::vector<Target> rows;if(!valid(root))return rows;CompanionScan scan{&root,&rows};EnumWindows(scanCompanions,(LPARAM)&scan);return rows;}
static BOOL CALLBACK enumTarget(HWND w,LPARAM p){
    if(w==mainWindow||GetWindow(w,GW_OWNER)||!GetWindowTextLengthW(w))return TRUE;
    RECT rect{};if((GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)||!GetWindowRect(w,&rect)||rect.right-rect.left<80||rect.bottom-rect.top<60)return TRUE;
    BOOL cloaked=FALSE;DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
    Target t=targetOf(w);if(t.pid==GetCurrentProcessId()||!t.born)return TRUE;
    if(matches(t.title,t.process,*(S*)p))targets.push_back(t);return TRUE;
}
static void summary(){int checked=0;for(int i=0;i<ListView_GetItemCount(listWindow);i++)if(ListView_GetCheckState(listWindow,i))checked++;set(LWindows,L"已选 "+std::to_wstring(checked)+L" / "+std::to_wstring(targets.size())+L" 个游戏窗口");}
static void refresh(){
    std::set<HWND> old;for(int i=0;i<ListView_GetItemCount(listWindow);i++)if(ListView_GetCheckState(listWindow,i)){LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=i;ListView_GetItem(listWindow,&item);old.insert((HWND)item.lParam);}
    std::set<HWND> known;for(auto& t:targets)known.insert(t.hwnd);
    S filter=value(Filter);targets.clear();EnumWindows(enumTarget,(LPARAM)&filter);
    auto add=[](const Target& t){if(!valid(t))return;for(auto& x:targets)if(x.hwnd==t.hwnd)return;targets.push_back(t);};
    manualTargets.erase(std::remove_if(manualTargets.begin(),manualTargets.end(),[](const Target& t){return !valid(t);}),manualTargets.end());
    for(auto& t:manualTargets)add(t);for(auto& [h,t]:hidden)add(t);
    std::sort(targets.begin(),targets.end(),[](const Target& a,const Target& b){return a.born!=b.born?a.born<b.born:(uintptr_t)a.hwnd<(uintptr_t)b.hwnd;});
    loadingList=true;SendMessageW(listWindow,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(listWindow);
    int i=0;for(auto& t:targets){LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=t.title.data();item.lParam=(LPARAM)t.hwnd;ListView_InsertItem(listWindow,&item);
        ListView_SetItemText(listWindow,i,1,t.process.data());S state=IsWindowVisible(t.hwnd)?(IsIconic(t.hwnd)?L"已最小化":L"显示中"):L"已隐藏";ListView_SetItemText(listWindow,i,2,state.data());
        RECT r{};GetClientRect(t.hwnd,&r);S size=std::to_wstring(r.right)+L" × "+std::to_wstring(r.bottom);ListView_SetItemText(listWindow,i,3,size.data());
        ListView_SetCheckState(listWindow,i,old.count(t.hwnd)||!known.count(t.hwnd));i++;}
    SendMessageW(listWindow,WM_SETREDRAW,TRUE,0);InvalidateRect(listWindow,nullptr,TRUE);loadingList=false;
    summary();
}
static std::vector<Target> selected(){
    std::vector<Target> out;for(int i=0;i<(int)targets.size();i++)if(ListView_GetCheckState(listWindow,i)&&valid(targets[i]))out.push_back(targets[i]);return out;
}
static bool capture(const Target& t){
    auto it=saved.find(t.hwnd);if(it!=saved.end()&&valid(it->second.target))return true;
    Saved s;s.target=t;s.ex=GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);
    if(s.ex&WS_EX_LAYERED)s.hasAlpha=GetLayeredWindowAttributes(t.hwnd,&s.color,&s.alpha,&s.flags)!=0;
    saved[t.hwnd]=s;return true;
}
static S winError(DWORD error){
    if(error==ERROR_ACCESS_DENIED)return L"权限不足：若游戏以管理员运行，请让鼠卫兵使用相同权限。";
    return L"Windows 错误 "+std::to_wstring(error)+L"。";
}
static int gameHeight(int width){return (int)(((int64_t)width*9+15)/16);}
static int gameWidth(int height){return (int)((int64_t)height*16/9);}
static bool collect(Settings& c,bool notify=true){
    c=cfg;c.path=value(Path);c.filter=value(Filter);
    c.columns=0;c.lockRatio=true;
    bool ok=!c.filter.empty()&&c.filter.size()<=256&&number(value(Width),160,7680,c.width)&&number(value(Height),90,4320,c.height)&&number(value(Count),1,20,c.count)&&number(value(Delay),500,10000,c.delay)&&number(value(Opacity),20,100,c.opacity);
    if(ok){c.height=gameHeight(c.width);sizingFields=true;set(Height,std::to_wstring(c.height));sizingFields=false;}
    if(!ok&&notify)MessageBoxW(mainWindow,L"请检查输入：宽 160–7680，高 90–4320；启动数量 1–20；间隔 500–10000 毫秒；不透明度 20–100%。筛选词不可为空。",L"鼠卫兵 · 输入检查",MB_OK|MB_ICONINFORMATION);return ok;
}
static void persist(){Settings c;if(collect(c,false)){cfg=c;if(!saveSettings(configPath,cfg))status(L"配置保存失败；本次窗口操作仍可使用。请检查磁盘空间和用户目录权限。");}}
static BOOL CALLBACK enumMonitor(HMONITOR h,HDC,LPRECT,LPARAM){MONITORINFOEXW m{};m.cbSize=sizeof(m);if(GetMonitorInfoW(h,&m)){S name=L"屏幕 "+std::to_wstring(monitors.size()+1)+L" · "+std::to_wstring(m.rcWork.right-m.rcWork.left)+L"×"+std::to_wstring(m.rcWork.bottom-m.rcWork.top);if(m.dwFlags&MONITORINFOF_PRIMARY)name+=L"（主屏）";monitors.push_back({h,m.rcWork,name});}return TRUE;}
static void screens(){monitors.clear();EnumDisplayMonitors(nullptr,nullptr,enumMonitor,0);SendMessageW(controls[Screen],CB_RESETCONTENT,0,0);int select=0;auto current=MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST);for(size_t i=0;i<monitors.size();i++){SendMessageW(controls[Screen],CB_ADDSTRING,0,(LPARAM)monitors[i].name.c_str());if(monitors[i].handle==current)select=(int)i;}SendMessageW(controls[Screen],CB_SETCURSEL,select,0);}
static std::vector<Cell> stackRight(const std::vector<SIZE>& sizes,RECT work){
    std::vector<Cell> cells;if(sizes.empty()||sizes.size()>256)return cells;
    int right=work.right,y=work.top,columnWidth=0;const int gap=8;
    for(auto size:sizes){
        if(size.cx<=0||size.cy<=0||size.cx>work.right-work.left||size.cy>work.bottom-work.top)return {};
        if(y!=work.top&&y+size.cy>work.bottom){right-=columnWidth+gap;y=work.top;columnWidth=0;}
        if(right-size.cx<work.left)return {};
        cells.push_back({right-size.cx,y,(int)size.cx,(int)size.cy});y+=size.cy+gap;columnWidth=std::max(columnWidth,(int)size.cx);
    }return cells;
}
static void stopArrangement(){
    KillTimer(mainWindow,4);arrangement={};
    if(controls.count(Arrange))EnableWindow(controls[Arrange],TRUE);
}
static bool clientResize(const Target& t,int width,int height);
static bool beginArrangement(const std::vector<Target>& rows,HMONITOR monitor,int width=0,int height=0){
    if(!arrangement.rows.empty())return false;
    for(auto& t:rows)if(!valid(t)||!capture(t)){status(L"窗口已关闭或无法读取，取消排列。");return false;}
    arrangement.rows=rows;arrangement.monitor=monitor;
    arrangement.width=width;arrangement.height=height;arrangement.resizing=width>0&&height>0;
    // Restore first, then measure after the target has handled the asynchronous request.
    for(auto& t:rows)if(IsIconic(t.hwnd)||IsZoomed(t.hwnd))ShowWindowAsync(t.hwnd,SW_RESTORE);
    if(arrangement.resizing)for(auto& t:rows){RECT c{};GetClientRect(t.hwnd,&c);if(c.right==width&&c.bottom==height)continue;if(!clientResize(t,width,height)){DWORD e=GetLastError();stopArrangement();status(L"应用尺寸失败。"+winError(e));return false;}}
    EnableWindow(controls[Arrange],FALSE);SetTimer(mainWindow,4,150,nullptr);
    status(arrangement.resizing?L"正在应用尺寸，等待游戏响应后排列…":L"正在排列 · 保持窗口当前大小…");return true;
}
static void arrangementTick(){
    if(arrangement.rows.empty()){stopArrangement();return;}
    if(++arrangement.ticks>(arrangement.resizing?24:12)){bool resizing=arrangement.resizing;stopArrangement();status(resizing?L"游戏未接受设定尺寸，已取消排列；请检查窗口模式或尝试较小尺寸。":L"窗口位置未稳定；客户端可能限制移动，请使用窗口模式后重试。");return;}
    if(arrangement.resizing){
        bool ready=true;for(auto& t:arrangement.rows){RECT c{};if(!valid(t)){stopArrangement();status(L"窗口已关闭，取消排列。");return;}if(IsIconic(t.hwnd)||IsZoomed(t.hwnd)||!GetClientRect(t.hwnd,&c)||abs(c.right-arrangement.width)>1||abs(c.bottom-arrangement.height)>1)ready=false;}
        arrangement.stable=ready?arrangement.stable+1:0;
        if(arrangement.stable<2)return;
        arrangement.resizing=false;arrangement.ticks=0;arrangement.stable=0;
    }
    MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(arrangement.monitor,&info)){stopArrangement();status(L"所选屏幕已断开，请重新选择。");return;}
    std::vector<RECT> rects;std::vector<SIZE> sizes;
    for(auto& t:arrangement.rows){
        RECT r{};
        if(!valid(t)||!GetWindowRect(t.hwnd,&r)){stopArrangement();status(L"窗口已关闭或无法读取，取消排列。");return;}
        if(IsIconic(t.hwnd)||IsZoomed(t.hwnd))return;
        rects.push_back(r);sizes.push_back({r.right-r.left,r.bottom-r.top});
    }
    // Use measured outer bounds, including the game's own frame, in screen pixels.
    auto cells=stackRight(sizes,info.rcWork);
    if(cells.size()!=arrangement.rows.size()){stopArrangement();status(L"当前窗口大小无法全部放入屏幕；请先应用较小尺寸或减少选择。");return;}
    bool settled=true;
    for(size_t k=0;k<cells.size();k++){
        auto& cell=cells[k];auto& r=rects[k];auto& t=arrangement.rows[k];
        if(r.left==cell.x&&r.top==cell.y)continue;
        settled=false;SetLastError(0);
        // Arrangement must never request a resize, including on repeated clicks.
        if(!SetWindowPos(t.hwnd,nullptr,cell.x,cell.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)){
            DWORD error=GetLastError();stopArrangement();status(L"排列失败。"+winError(error));return;
        }
    }
    arrangement.stable=settled?arrangement.stable+1:0;
    if(arrangement.stable>=2){int count=(int)arrangement.rows.size();stopArrangement();status(L"右侧纵向排列完成 · "+std::to_wstring(count)+L" 个窗口，保持当前大小。");refresh();}
}
static int pairedDimension(int n,int from,int to){return (int)(((int64_t)n*to+from/2)/from);}
static void sizeChanged(int id){
    if(sizingFields||!controls.count(LockRatio))return;
    int n;if(!number(value(id),id==Width?160:90,id==Width?7680:4320,n))return;
    int paired=id==Width?gameHeight(n):gameWidth(n);
    sizingFields=true;set(id==Width?Height:Width,std::to_wstring(paired));sizingFields=false;
}
static SIZE outerSize(const Target& t,int width,int height){
    RECT r{0,0,width,height};DWORD style=(DWORD)GetWindowLongPtrW(t.hwnd,GWL_STYLE)&~(WS_MINIMIZE|WS_MAXIMIZE),ex=(DWORD)GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);
    using AdjustDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);using DpiFor=UINT(WINAPI*)(HWND);
    auto user=GetModuleHandleW(L"user32.dll");auto adjust=(AdjustDpi)GetProcAddress(user,"AdjustWindowRectExForDpi");auto forWindow=(DpiFor)GetProcAddress(user,"GetDpiForWindow");
    BOOL ok=adjust&&forWindow?adjust(&r,style,GetMenu(t.hwnd)!=nullptr,ex,forWindow(t.hwnd)):AdjustWindowRectEx(&r,style,GetMenu(t.hwnd)!=nullptr,ex);
    return ok?SIZE{r.right-r.left,r.bottom-r.top}:SIZE{0,0};
}
static bool clientResize(const Target& t,int width,int height){
    if(!capture(t))return false;SetLastError(0);SIZE size=outerSize(t,width,height);if(size.cx<=0||size.cy<=0)return false;
    if(IsIconic(t.hwnd)||IsZoomed(t.hwnd))ShowWindowAsync(t.hwnd,SW_RESTORE);
    return SetWindowPos(t.hwnd,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)!=0;
}
static bool transparency(const Target& t,int percent){
    if(!capture(t))return false;auto& s=saved[t.hwnd];LONG_PTR ex=GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);
    // Per-pixel layered windows cannot be safely converted with this API.
    if((s.ex&WS_EX_LAYERED)&&!s.hasAlpha){SetLastError(ERROR_NOT_SUPPORTED);return false;}
    if(percent==100&&!(s.ex&WS_EX_LAYERED)){
        SetLastError(0);LONG_PTR previous=SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,ex&~WS_EX_LAYERED);bool ok=previous!=0||GetLastError()==0;if(ok)RedrawWindow(t.hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);return ok;
    }
    if(!(ex&WS_EX_LAYERED)){SetLastError(0);LONG_PTR previous=SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,ex|WS_EX_LAYERED);if(!previous&&GetLastError())return false;}
    DWORD flags=LWA_ALPHA|(s.hasAlpha?(s.flags&LWA_COLORKEY):0);bool ok=SetLayeredWindowAttributes(t.hwnd,s.color,(BYTE)((percent*255+50)/100),flags)!=0;
    if(!ok&&!(ex&WS_EX_LAYERED))SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,ex);return ok;
}
static void report(const S& action,int ok,int total,DWORD error=0){status(action+L"：成功 "+std::to_wstring(ok)+L" / "+std::to_wstring(total)+(ok<total?L"。"+winError(error):L"。"));}
static void operate(int action){
    if(action==Arrange&&!arrangement.rows.empty())return;
    if(action!=Arrange)stopArrangement();
    refresh();auto rows=selected();if(rows.empty()){status(L"未选中游戏窗口；可展开窗口选择或手动添加。");return;}
    Settings c;if(!collect(c))return;cfg=c;int ok=0;DWORD error=0;
    if(action==Arrange){int i=(int)SendMessageW(controls[Screen],CB_GETCURSEL,0,0);if(i<0||i>=(int)monitors.size()){status(L"请先选择有效屏幕。");return;}
        beginArrangement(rows,monitors[i].handle,c.width,c.height);
    }else if(action==Hide){
        auto previous=hidden;auto previousCompanions=hiddenCompanions;std::vector<Target> overlays;for(auto& t:rows)if(IsWindowVisible(t.hwnd)){hidden[t.hwnd]=t;auto related=companions(t);for(auto& extra:related){hiddenCompanions[extra.hwnd]=extra;overlays.push_back(extra);}}
        // Journal identities before hiding so a process crash remains recoverable.
        if(!saveHidden()){hidden=previous;hiddenCompanions=previousCompanions;status(L"无法保存隐藏恢复记录，已取消隐藏操作。");return;}
        for(auto& t:overlays)if(valid(t))ShowWindowAsync(t.hwnd,SW_HIDE);for(auto& t:rows){if(!IsWindowVisible(t.hwnd)){ok++;continue;}SetLastError(0);ShowWindowAsync(t.hwnd,SW_HIDE);ok++;}report(L"已发送隐藏请求",ok,(int)rows.size());
    }else if(action==Show){
        for(auto& t:rows){ShowWindowAsync(t.hwnd,SW_SHOW);for(auto& [hwnd,extra]:hiddenCompanions)if(extra.pid==t.pid&&extra.born==t.born&&valid(extra))ShowWindowAsync(hwnd,SW_SHOW);ok++;}report(L"已发送显示请求",ok,(int)rows.size());
        // Keep journal until visibility is confirmed or process exits.
    }else {
        for(auto& t:rows){SetLastError(0);bool good=action==Resize?clientResize(t,c.width,c.height):transparency(t,c.opacity);if(good)ok++;else error=GetLastError();}report(action==Resize?L"已发送客户区尺寸调整请求":L"不透明度调整",ok,(int)rows.size(),error);
    }
    persist();SetTimer(mainWindow,3,350,nullptr);
}
static int sendCloseRequests(const std::vector<Target>& rows,bool confirmed){
    if(!confirmed)return 0;int count=0;std::set<HWND> seen;
    for(auto& t:rows)if(t.pid!=GetCurrentProcessId()&&valid(t)&&seen.insert(t.hwnd).second){if(!IsWindowVisible(t.hwnd))ShowWindowAsync(t.hwnd,SW_SHOW);if(PostMessageW(t.hwnd,WM_CLOSE,0,0))count++;}return count;
}
static S boundedControlText(HWND w){wchar_t buffer[256]{};DWORD_PTR result=0;if(!SendMessageTimeoutW(w,WM_GETTEXT,256,(LPARAM)buffer,SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&result))return {};return buffer;}
static BOOL CALLBACK findExitPrompt(HWND child,LPARAM context){wchar_t kind[32]{};GetClassNameW(child,kind,32);if(S(kind)==L"Static"){S value=boundedControlText(child);if(value==L"确定要离开吗?"||value==L"确定要离开吗？")*(bool*)context=true;}return TRUE;}
static HWND exitConfirmButton(HWND dialog,const Target& game){
    if(!valid(game)||!IsWindowVisible(dialog))return nullptr;DWORD pid=0;GetWindowThreadProcessId(dialog,&pid);if(pid!=game.pid)return nullptr;
    wchar_t kind[32]{};GetClassNameW(dialog,kind,32);if(S(kind)!=L"#32770"||text(dialog)!=L"退出游戏")return nullptr;
    HWND owner=GetWindow(dialog,GW_OWNER);int depth=0;while(owner&&owner!=game.hwnd&&depth++<16)owner=GetWindow(owner,GW_OWNER);if(owner!=game.hwnd)return nullptr;
    bool prompt=false;EnumChildWindows(dialog,findExitPrompt,(LPARAM)&prompt);if(!prompt)return nullptr;HWND button=GetDlgItem(dialog,IDOK);if(!button||!IsWindowVisible(button)||!IsWindowEnabled(button))return nullptr;
    GetClassNameW(button,kind,32);S label=boundedControlText(button);if(S(kind)!=L"Button"||(label!=L"确定"&&label!=L"确定(&O)"&&label!=L"&确定"&&label!=L"OK"&&label!=L"&OK"))return nullptr;return valid(game)?button:nullptr;
}
static BOOL CALLBACK confirmExitDialog(HWND dialog,LPARAM context){auto rows=(const std::vector<Target>*)context;if(confirmedDialogs.count(dialog))return TRUE;for(auto& game:*rows)if(HWND button=exitConfirmButton(dialog,game)){if(PostMessageW(button,BM_CLICK,0,0))confirmedDialogs.insert(dialog);break;}return TRUE;}
static void closeTick(){bool any=false;for(auto& game:closingWindows)if(valid(game))any=true;if(!any||GetTickCount64()>closeDeadline){KillTimer(mainWindow,7);closingWindows.clear();confirmedDialogs.clear();EnableWindow(controls[CloseAll],TRUE);refresh();status(any?L"部分窗口尚未退出；无法识别的确认框请手动处理。":L"关闭完成。");return;}EnumWindows(confirmExitDialog,(LPARAM)&closingWindows);}
static void beginClose(const std::vector<Target>& rows){sendCloseRequests(rows,true);closingWindows=rows;confirmedDialogs.clear();closeDeadline=GetTickCount64()+8000;EnableWindow(controls[CloseAll],FALSE);SetTimer(mainWindow,7,250,nullptr);status(L"正在关闭客户端并确认退出…");}
struct FixtureWindow {DWORD pid;HWND hwnd{};};
static BOOL CALLBACK findFixtureWindow(HWND window,LPARAM context){auto fixture=(FixtureWindow*)context;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==fixture->pid&&text(window)==L"鼠卫兵多开测试窗口"){fixture->hwnd=window;return FALSE;}return TRUE;}
struct CaptionSearch {const wchar_t* caption;bool found=false;};
static BOOL CALLBACK findChildCaption(HWND window,LPARAM context){auto search=(CaptionSearch*)context;if(text(window)==search->caption){search->found=true;return FALSE;}return TRUE;}
static bool childHasCaption(HWND parent,const wchar_t* caption){CaptionSearch search{caption};EnumChildWindows(parent,findChildCaption,(LPARAM)&search);return search.found;}
static void stopStartup(){KillTimer(mainWindow,1);KillTimer(mainWindow,6);launchRemaining=0;startupWindows.clear();EnableWindow(controls[Launch],TRUE);}
static void closeAllGames(){
    refresh();auto rows=targets;if(rows.empty()){status(L"没有可关闭的游戏窗口。");return;}
    S message=L"确认关闭列表中的全部 "+std::to_wstring(rows.size())+L" 个游戏窗口？包括未勾选和已隐藏窗口。\n鼠卫兵会自动确认这些窗口的“退出游戏 / 确定要离开吗？”提示。\n未保存进度可能丢失。\n\n";
    for(size_t i=0;i<rows.size()&&i<8;i++)message+=rows[i].title+L"（"+rows[i].process+L"）\n";
    if(MessageBoxW(mainWindow,message.c_str(),L"鼠卫兵 · 关闭游戏",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    stopArrangement();stopStartup();
    beginClose(rows);
}
static int reminderDay(const SYSTEMTIME& now){return now.wYear*10000+now.wMonth*100+now.wDay;}
static bool reminderDue(const Settings& settings,const SYSTEMTIME& now,int existing){return now.wDayOfWeek==3&&now.wHour>=9&&settings.count+existing>1&&settings.wednesdayReminder!=reminderDay(now);}
static std::vector<Target> extraClients(const std::vector<Target>& rows){std::vector<Target> extras;std::set<std::pair<DWORD,uint64_t>> seen;for(auto& row:rows)if(valid(row)&&seen.insert({row.pid,row.born}).second){if(seen.size()>1)extras.push_back(row);}return extras;}
static void changeAudio(bool toggle){refresh();auto rows=selected();if(rows.empty()){status(L"请先选择需要调整声音的游戏窗口。");return;}int level=100;if(!toggle&&!number(value(Volume),0,100,level)){status(L"音量请输入 0–100。");return;}
    auto chosen=[&](DWORD pid){for(auto& row:rows)if(row.pid==pid&&valid(row))return true;return false;};auto result=audio::change(chosen,toggle,level);
    status(result.changed?L"已调整 "+std::to_wstring(result.changed)+L" 个所选客户端音频会话。":result.shared?L"客户端共用音频会话，无法分别控制；请使用游戏内声音设置。":L"未找到可单独控制的声音；请让客户端播放声音后重试。");
}
static S sharpCompatibility(const S& previous){
    S result;size_t at=0;
    while(at<previous.size()){
        while(at<previous.size()&&iswspace(previous[at]))at++;
        size_t end=at;while(end<previous.size()&&!iswspace(previous[end]))end++;
        S token=previous.substr(at,end-at),key=lower(token);
        if(!token.empty()&&key!=L"~"&&key!=L"dpiaware"&&key!=L"dpiunaware"&&key!=L"gdidpiscaling"&&key!=L"highdpiaware")result+=token+L" ";
        at=end;
    }
    return result+L"HIGHDPIAWARE";
}
static std::vector<wchar_t> launchEnvironment(bool sharp){
    LPWCH raw=GetEnvironmentStringsW();if(!raw)return {};
    std::vector<S> entries;S previous;
    for(auto p=raw;*p;p+=wcslen(p)+1){S entry=p;if(sharp&&lower(entry).rfind(L"__compat_layer=",0)==0)previous=entry.substr(15);else entries.push_back(entry);}
    FreeEnvironmentStringsW(raw);
    if(sharp)entries.push_back(L"__COMPAT_LAYER="+sharpCompatibility(previous));
    std::sort(entries.begin(),entries.end(),[](const S& a,const S& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;});
    std::vector<wchar_t> block;for(auto& entry:entries){block.insert(block.end(),entry.begin(),entry.end());block.push_back(0);}block.push_back(0);if(block.size()==1)block.push_back(0);return block;
}
static bool validBossKey(int key,int mods){
    return key>0&&key<255&&key!=VK_SHIFT&&key!=VK_CONTROL&&key!=VK_MENU&&key!=VK_LWIN&&key!=VK_RWIN&&key!=VK_F12&&(mods&~7)==0&&((mods&(MOD_CONTROL|MOD_ALT))||(key>=VK_F1&&key<=VK_F11));
}
static WORD hotkeyControlValue(int key,int mods){
    BYTE flags=(mods&MOD_CONTROL?HOTKEYF_CONTROL:0)|(mods&MOD_ALT?HOTKEYF_ALT:0)|(mods&MOD_SHIFT?HOTKEYF_SHIFT:0);
    if((MapVirtualKeyW(key,MAPVK_VK_TO_VSC_EX)&0xff00)==0xe000)flags|=HOTKEYF_EXT;
    return MAKEWORD(key,flags);
}
static int hotkeyModifiers(WORD value){
    auto flags=HIBYTE(value);return (flags&HOTKEYF_CONTROL?MOD_CONTROL:0)|(flags&HOTKEYF_ALT?MOD_ALT:0)|(flags&HOTKEYF_SHIFT?MOD_SHIFT:0);
}
static S hotkeyName(int key,int mods){
    S name;if(mods&MOD_CONTROL)name+=L"Ctrl + ";if(mods&MOD_ALT)name+=L"Alt + ";if(mods&MOD_SHIFT)name+=L"Shift + ";
    UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC_EX);LONG bits=(scan&0xff)<<16;if(scan&0xff00)bits|=1<<24;wchar_t label[80]{};
    if(GetKeyNameTextW(bits,label,80))name+=label;else name+=L"键 "+std::to_wstring(key);return name;
}
static bool registerBoss(bool enable,int key,int mods,bool notify=true){
    if(!enable){if(bossRegistered)UnregisterHotKey(mainWindow,bossRegistration);bossRegistered=false;return true;}
    if(!validBossKey(key,mods)){if(notify)status(L"请选择 Ctrl/Alt 组合键或 F1–F11；F12 为系统保留键。");return false;}
    if(bossRegistered&&registeredKey==key&&registeredMods==mods)return true;
    int next=bossRegistration==3010?3011:3010;
    if(!RegisterHotKey(mainWindow,next,mods|MOD_NOREPEAT,key)){if(notify)status(L"老板键已被其他程序占用或系统保留，请换一个组合；原快捷键继续有效。");return false;}
    if(bossRegistered)UnregisterHotKey(mainWindow,bossRegistration);
    bossRegistration=next;bossRegistered=true;registeredKey=key;registeredMods=mods;return true;
}
static void saveBossKey(bool disable=false){
    Settings candidate;if(!collect(candidate))return;WORD chosen=(WORD)SendMessageW(controls[BossKey],HKM_GETHOTKEY,0,0);
    candidate.bossEnabled=!disable&&SendMessageW(controls[BossEnabled],BM_GETCHECK,0,0)==BST_CHECKED;
    if(candidate.bossEnabled){candidate.bossKey=LOBYTE(chosen);candidate.bossMods=hotkeyModifiers(chosen);}
    if(!registerBoss(candidate.bossEnabled,candidate.bossKey,candidate.bossMods))return;
    if(!saveSettings(configPath,candidate)){registerBoss(cfg.bossEnabled,cfg.bossKey,cfg.bossMods,false);status(L"老板键保存失败，已恢复原设置。");return;}
    cfg=candidate;SendMessageW(controls[BossEnabled],BM_SETCHECK,cfg.bossEnabled?BST_CHECKED:BST_UNCHECKED,0);
    status(cfg.bossEnabled?L"老板键已保存："+hotkeyName(cfg.bossKey,cfg.bossMods)+L" · 按一次隐藏，再按恢复。":L"老板键已停用；已隐藏的窗口仍可点击“显示”恢复。");
}
static void bossFinishTick(){
    bool settled=true;
    for(auto it=bossHidden.begin();it!=bossHidden.end();){
        if(!valid(it->second)){it=bossHidden.erase(it);continue;}
        bool visible=IsWindowVisible(it->first)!=0;
        if(!bossActive&&visible){hidden.erase(it->first);hiddenCompanions.erase(it->first);it=bossHidden.erase(it);continue;}
        if(bossActive?visible:!visible)settled=false;
        ++it;
    }
    if(!settled&&++bossTicks<10)return;
    KillTimer(mainWindow,5);bossBusy=false;
    if(!bossActive&&!bossHidden.empty())bossActive=true;
    saveHidden();refresh();
    if(!settled){if(bossPanelVisible){ShowWindow(mainWindow,SW_RESTORE);SetForegroundWindow(mainWindow);}status(L"部分窗口没有响应老板键；可重试，若游戏以管理员运行，请点击“管理员启动”。");}
}
static void toggleBossRows(const std::vector<Target>& rows){
    if(bossBusy)return;stopArrangement();
    if(bossActive){
        for(auto& [hwnd,t]:bossHidden)if(valid(t))ShowWindowAsync(hwnd,SW_SHOWNA);
        bossActive=false;if(bossPanelVisible){ShowWindow(mainWindow,SW_RESTORE);SetForegroundWindow(mainWindow);}status(L"老板键 · 已发送恢复请求。");
    }else{
        std::map<HWND,Target> batch,overlays;for(auto& t:rows)if(valid(t)&&IsWindowVisible(t.hwnd)){batch[t.hwnd]=t;for(auto& extra:companions(t))overlays[extra.hwnd]=extra;}
        if(batch.empty()){status(L"老板键：没有选中的可见游戏窗口。");return;}
        auto previous=hidden;auto previousCompanions=hiddenCompanions;for(auto& [hwnd,t]:batch)hidden[hwnd]=t;for(auto& [hwnd,t]:overlays)hiddenCompanions[hwnd]=t;
        if(!saveHidden()){hidden=previous;hiddenCompanions=previousCompanions;status(L"无法保存恢复记录，已取消老板键隐藏。");return;}
        bossHidden.clear();for(auto& [hwnd,t]:overlays)if(ShowWindowAsync(hwnd,SW_HIDE))bossHidden[hwnd]=t;for(auto& [hwnd,t]:batch){if(ShowWindowAsync(hwnd,SW_HIDE))bossHidden[hwnd]=t;else if(!previous.count(hwnd))hidden.erase(hwnd);}
        if(bossHidden.empty()){saveHidden();status(L"窗口隐藏失败，请检查管理员权限。");return;}
        bossActive=true;bossPanelVisible=IsWindowVisible(mainWindow)!=0&&!IsIconic(mainWindow);
        if(bossPanelVisible)ShowWindow(mainWindow,trayReady?SW_HIDE:SW_MINIMIZE);
        status(L"老板键 · 已发送隐藏请求，再按恢复。");
    }
    bossBusy=true;bossTicks=0;SetTimer(mainWindow,5,150,nullptr);
}
static void toggleBoss(){if(bossActive)toggleBossRows({});else{refresh();toggleBossRows(selected());}}
static bool isElevated(){
    HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    TOKEN_ELEVATION info{};DWORD bytes{};bool result=GetTokenInformation(token,TokenElevation,&info,sizeof(info),&bytes)&&info.TokenIsElevated;CloseHandle(token);return result;
}
static S adminHint(bool admin){return admin?L"已以系统管理员身份运行":L"普通权限 · 建议以系统管理员身份运行";}
static void restartAdmin(){
    if(elevated){status(L"当前已以系统管理员身份运行。");return;}
    Settings next;if(!collect(next))return;
    if(!saveSettings(configPath,next)){status(L"设置保存失败，请稍后重试管理员启动。");return;}cfg=next;
    wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))return;
    S parameters=L"--wait-parent "+std::to_wstring(GetCurrentProcessId()),directory=folderOf(executable);
    SHELLEXECUTEINFOW request{sizeof(request)};request.fMask=SEE_MASK_NOCLOSEPROCESS;request.hwnd=mainWindow;request.lpVerb=L"runas";request.lpFile=executable;request.lpParameters=parameters.c_str();request.lpDirectory=directory.c_str();request.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&request)){DWORD error=GetLastError();status(error==ERROR_CANCELLED?L"已取消管理员启动，当前程序继续运行。":L"管理员启动失败。"+winError(error));return;}
    if(request.hProcess)CloseHandle(request.hProcess);SendMessageW(mainWindow,WM_CLOSE,0,0);
}
static bool resolveLaunch(const S& path,const S& args,LaunchSpec& output,S& error){
    if(!fileExists(path)||lower(path).size()<4||lower(path).substr(path.size()-4)!=L".exe"){error=L"请选择存在的客户端或启动器 .exe 文件。";return false;}
    LaunchSpec candidate{path,args,folderOf(path),false};S name=lower(path.substr(path.find_last_of(L"\\/")+1));
    if(name==L"launch.exe"||name==L"yyslauncher.exe"){
        S client=candidate.directory+L"\\bin\\onmyoji.exe";
        if(!fileExists(client)){error=L"该目录缺少 bin\\onmyoji.exe，不能用此启动器追加多开。请通过“查找”选择阴阳师-MuMu模拟器专版，或先用官方启动器完成安装更新。";return false;}
        candidate.executable=client;candidate.directClient=true;
    }else if(name==L"onmyoji.exe"){
        if(lower(candidate.directory.substr(candidate.directory.find_last_of(L"\\/")+1))==L"bin")candidate.directory=folderOf(candidate.directory);
        candidate.directClient=true;
    }
    output=candidate;return true;
}
static bool spawnClient(const LaunchSpec& spec,bool sharp,PROCESS_INFORMATION& process){
    S command=L"\""+spec.executable+L"\""+(spec.args.empty()?L"":L" "+spec.args);std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);auto environment=launchEnvironment(sharp);
    if(environment.empty()){SetLastError(ERROR_ENVVAR_NOT_FOUND);return false;}
    return CreateProcessW(spec.executable.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,environment.data(),spec.directory.empty()?nullptr:spec.directory.c_str(),&startup,&process)!=0;
}
static void launchOne(){
    if(launchRemaining<=0)return;
    PROCESS_INFORMATION pi{};
    // A private child environment avoids persistent registry or global environment changes.
    if(!spawnClient(launchSpec,true,pi)){
        DWORD e=GetLastError();stopStartup();status(L"启动失败。"+winError(e)+(e==ERROR_ELEVATION_REQUIRED?L"该启动器要求管理员权限，可手动打开后再管理窗口。":L""));return;
    }
    FILETIME born{},exit{},kernel{},user{};uint64_t birth=0;if(GetProcessTimes(pi.hProcess,&born,&exit,&kernel,&user))memcpy(&birth,&born,8);
    startupWindows.push_back({pi.dwProcessId,birth});CloseHandle(pi.hThread);CloseHandle(pi.hProcess);launchRemaining--;status(L"已启动 "+std::to_wstring(launchTotal-launchRemaining)+L" / "+std::to_wstring(launchTotal)+L" 个进程，等待游戏窗口就绪…");
    if(launchRemaining==0){KillTimer(mainWindow,1);SetTimer(mainWindow,3,1500,nullptr);}
}
static bool observeStartup(StartupWindow& row,const Target& t,ULONGLONG now){
    RECT c{};if(!valid(t)||t.pid!=row.pid||t.born!=row.born||!IsWindowVisible(t.hwnd)||(GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)||GetWindow(t.hwnd,GW_OWNER)||!GetClientRect(t.hwnd,&c)||c.right<320||c.bottom<180)return false;
    if(row.hwnd!=t.hwnd||row.width!=c.right||row.height!=c.bottom||now-row.lastSeen>750){row.hwnd=t.hwnd;row.width=c.right;row.height=c.bottom;row.stableSince=now;}row.lastSeen=now;
    return now-row.stableSince>=1000;
}
struct StartupScan {ULONGLONG now;std::set<DWORD> ready;};
static BOOL CALLBACK scanStartup(HWND w,LPARAM context){auto scan=(StartupScan*)context;DWORD pid=0;GetWindowThreadProcessId(w,&pid);for(auto& row:startupWindows)if(row.pid==pid&&observeStartup(row,targetOf(w),scan->now))scan->ready.insert(pid);return TRUE;}
static void startupTick(){
    if(startupWindows.empty()){KillTimer(mainWindow,6);return;}ULONGLONG now=GetTickCount64();
    if(now>startupDeadline){stopStartup();refresh();status(L"等待游戏窗口超时；已启动游戏继续运行，可刷新后手动排列。");return;}
    StartupScan scan{now,{}};EnumWindows(scanStartup,(LPARAM)&scan);
    if(launchRemaining||scan.ready.size()!=startupWindows.size())return;
    for(auto& row:startupWindows){auto t=targetOf(row.hwnd);if(valid(t)&&t.pid==row.pid&&t.born==row.born)manualTargets.push_back(t);}
    stopStartup();refresh();status(L"游戏窗口已就绪，正在应用尺寸并排列…");operate(Arrange);
}
static void beginLaunch(const S& internalTestArgs=L""){
    if(launchRemaining>0||!startupWindows.empty()||!closingWindows.empty())return;Settings c;if(!collect(c))return;LaunchSpec next;S error;
    if(!resolveLaunch(c.path,internalTestArgs,next,error)){status(error);return;}
    SYSTEMTIME now{};GetLocalTime(&now);refresh();std::set<std::pair<DWORD,uint64_t>> live;for(auto& row:targets)if(valid(row))live.insert({row.pid,row.born});
    if(!testMode&&reminderDue(c,now,(int)live.size())){
        c.wednesdayReminder=reminderDay(now);cfg=c;if(!saveSettings(configPath,cfg)){status(L"无法保存本周提醒记录，请检查配置目录权限后重试。");return;}
        S content=L"阴阳师通常在周三更新。建议先只保留一个客户端完成更新，再启动其他客户端。\n本提示每周三 9 点后仅显示一次，并不代表已检测到官方更新。";
        if(!live.empty())content+=L"\n\n当前已打开 "+std::to_wstring(live.size())+L" 个客户端；选择第一项将保留第一个，正常关闭其余客户端并自动确认退出，未保存进度可能丢失。";
        TASKDIALOG_BUTTON buttons[]={{101,L"先只保留一个客户端用于更新"},{102,L"已完成更新，继续多开"}};TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);dialog.hwndParent=mainWindow;dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;dialog.pszWindowTitle=L"鼠卫兵 · 周三更新提醒";dialog.pszMainInstruction=L"先用一个客户端完成游戏更新？";dialog.pszContent=content.c_str();dialog.cButtons=2;dialog.pButtons=buttons;dialog.nDefaultButton=101;int choice=IDCANCEL;
        if(FAILED(TaskDialogIndirect(&dialog,&choice,nullptr,nullptr))||choice==IDCANCEL)return;
        if(choice==101){
            stopArrangement();if(!live.empty()){auto extras=extraClients(targets);if(!extras.empty())beginClose(extras);else status(L"保留当前客户端用于更新；完成后再点击多开。");return;}
            // Updating is a deliberate single-launch flow; normal multi-launch
            // continues to use the independent bin/onmyoji.exe entry point.
            S launcher;for(auto name:{L"Launch.exe",L"YysLauncher.exe"}){S path=next.directory+L"\\"+name;if(fileExists(path)){launcher=path;break;}}
            if(launcher.empty()){status(L"未找到官方启动器，请选择 Launch.exe 或先手动完成游戏更新。");return;}
            LaunchSpec update{launcher,L"",next.directory,false};PROCESS_INFORMATION process{};if(!spawnClient(update,true,process)){status(L"单客户端更新启动失败。"+winError(GetLastError()));return;}CloseHandle(process.hThread);CloseHandle(process.hProcess);status(L"已打开一次官方启动器用于更新；完成后再点击多开。");return;
        }
    }
    stopArrangement();cfg=c;persist();launchSpec=next;startupWindows.clear();startupDeadline=GetTickCount64()+120000;launchRemaining=launchTotal=c.count;EnableWindow(controls[Launch],FALSE);SetTimer(mainWindow,1,c.delay,nullptr);SetTimer(mainWindow,6,250,nullptr);launchOne();
}
static void browse(){wchar_t path[32768]{};OPENFILENAMEW o{};o.lStructSize=sizeof(o);o.hwndOwner=mainWindow;o.lpstrFilter=L"游戏或启动器 (*.exe)\0*.exe\0\0";o.lpstrFile=path;o.nMaxFile=32768;o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&o)){set(Path,path);persist();}}
static const wchar_t* OfficialDownload=L"https://yys.163.com/zmb/";
static void addGamePath(std::vector<S>& paths,const S& path){
    if(paths.size()>=32||!fileExists(path))return;
    for(auto& previous:paths)if(lower(previous)==lower(path))return;paths.push_back(path);
}
static void launchersIn(std::vector<S>& paths,S folder){
    while(!folder.empty()&&(folder.back()==L'\\'||folder.back()==L'/'))folder.pop_back();
    for(auto exe:{L"\\launch.exe",L"\\YysLauncher.exe"})addGamePath(paths,folder+exe);
    if(lower(folder.substr(folder.find_last_of(L"\\/")+1))==L"bin")for(auto exe:{L"\\launch.exe",L"\\YysLauncher.exe"})addGamePath(paths,folderOf(folder)+exe);
}
struct RunningDiscovery {std::vector<S> paths;std::set<DWORD> checked;};
static BOOL CALLBACK discoverRunning(HWND window,LPARAM data){
    if(!GetWindowTextLengthW(window))return TRUE;auto& found=*(RunningDiscovery*)data;DWORD pid{};GetWindowThreadProcessId(window,&pid);if(!found.checked.insert(pid).second)return TRUE;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return TRUE;
    wchar_t path[32768]{};DWORD n=32768;if(QueryFullProcessImageNameW(process,0,path,&n)){
        S image(path,n),name=lower(image.substr(image.find_last_of(L"\\/")+1));
        if(name==L"onmyoji.exe"||name==L"yyslauncher.exe")launchersIn(found.paths,folderOf(image));
    }CloseHandle(process);return TRUE;
}
static std::vector<S> gameCandidates(){
    RunningDiscovery running;EnumWindows(discoverRunning,(LPARAM)&running);auto paths=running.paths;
    addGamePath(paths,cfg.path);addGamePath(paths,L"K:\\Netease\\yys\\launch.exe");
    for(auto root:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        HKEY h;if(RegOpenKeyExW(root,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&h)!=ERROR_SUCCESS)continue;
        for(DWORD i=0;;i++){wchar_t name[512];DWORD n=512;if(RegEnumKeyExW(h,i,name,&n,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS)break;HKEY sub;
            if(RegOpenKeyExW(h,name,0,KEY_READ|view,&sub)!=ERROR_SUCCESS)continue;
            wchar_t display[2048]{},location[32768]{};DWORD bytes=sizeof(display);RegQueryValueExW(sub,L"DisplayName",nullptr,nullptr,(BYTE*)display,&bytes);
            if(S(display).find(L"阴阳师")!=S::npos){bytes=sizeof(location);if(RegQueryValueExW(sub,L"InstallLocation",nullptr,nullptr,(BYTE*)location,&bytes)==ERROR_SUCCESS)launchersIn(paths,location);}
            RegCloseKey(sub);
        }RegCloseKey(h);
    }
    wchar_t drives[512]{};GetLogicalDriveStringsW(512,drives);
    for(auto drive=drives;*drive;drive+=wcslen(drive)+1)if(GetDriveTypeW(drive)==DRIVE_FIXED){
        for(auto relative:{L"Netease\\yys",L"NetEase\\Onmyoji",L"Onmyoji",L"Games\\yys",L"Games\\Onmyoji",L"Program Files\\Netease\\yys",L"Program Files (x86)\\Netease\\yys"})launchersIn(paths,S(drive)+relative);
    }
    return paths;
}
static S detectGame(){auto paths=gameCandidates();return paths.empty()?L"":paths.front();}
struct ScanResult {std::vector<S> paths;bool limited=false;};
static ScanResult* pendingScan=nullptr;
static ScanResult scanGameFolder(const S& root){
    ScanResult result;std::vector<std::pair<S,int>> folders{{root,0}};ULONGLONG deadline=GetTickCount64()+3000;int entries=0;
    for(size_t at=0;at<folders.size();at++){
        if(GetTickCount64()>=deadline||entries>=6000){result.limited=true;break;}
        auto folder=folders[at].first;int depth=folders[at].second;launchersIn(result.paths,folder);if(depth>=3)continue;
        WIN32_FIND_DATAW item{};HANDLE search=FindFirstFileW((folder+L"\\*").c_str(),&item);if(search==INVALID_HANDLE_VALUE)continue;
        do{
            if(++entries>=6000||GetTickCount64()>=deadline){result.limited=true;break;}
            if((item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(item.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&S(item.cFileName)!=L"."&&S(item.cFileName)!=L".."){
                if(folders.size()<256)folders.push_back({folder+L"\\"+item.cFileName,depth+1});else result.limited=true;
            }
        }while(FindNextFileW(search,&item));FindClose(search);
    }return result;
}
static void selectDiscovered(const ScanResult& result){
    if(result.paths.empty()){status(result.limited?L"扫描已达到范围限制；请选择更接近安装位置的文件夹。":L"未找到客户端；可通过“查找 → 官方下载”安装阴阳师-MuMu模拟器专版。");return;}
    int index=0;
    if(result.paths.size()>1){
        HMENU menu=CreatePopupMenu();for(size_t i=0;i<result.paths.size();i++){S name=result.paths[i];for(size_t p=0;(p=name.find(L'&',p))!=S::npos;p+=2)name.insert(p,L"&");AppendMenuW(menu,MF_STRING,4100+(UINT)i,name.c_str());}
        RECT rect{};GetWindowRect(controls[AutoFind],&rect);int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,rect.left,rect.bottom,0,mainWindow,nullptr);DestroyMenu(menu);
        if(command<4100||command>=4100+(int)result.paths.size())return;index=command-4100;
    }
    set(Path,result.paths[index]);persist();status(L"已读取客户端路径："+result.paths[index]+(result.limited?L"（扫描范围有限）":L""));
}
struct ScanRequest {S root;HWND panel;};
static DWORD WINAPI scanWorker(void* data){
    auto request=(ScanRequest*)data;auto result=new ScanResult(scanGameFolder(request->root));HWND panel=request->panel;delete request;
    if(!PostMessageW(panel,WM_APP+2,0,(LPARAM)result))delete result;return 0;
}
static void browseGameFolder(){
    IFileDialog* dialog{};if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){status(L"无法打开文件夹选择，请使用“选择”指定启动器。");return;}
    DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(L"选择阴阳师安装文件夹（扫描至三层子目录）");
    S root;if(SUCCEEDED(dialog->Show(mainWindow))){IShellItem* item{};if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){root=path;CoTaskMemFree(path);}item->Release();}}dialog->Release();if(root.empty())return;
    auto request=new ScanRequest{root,mainWindow};HANDLE worker=CreateThread(nullptr,0,scanWorker,request,0,nullptr);if(!worker){delete request;status(L"扫描无法启动，请重试。");return;}CloseHandle(worker);EnableWindow(controls[AutoFind],FALSE);status(L"正在扫描所选文件夹…");
}
static void discoveryMenu(){
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,4001,L"自动读取：运行中的客户端 / 安装记录 / 常见目录");AppendMenuW(menu,MF_STRING,4002,L"扫描所选文件夹…");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4003,L"官方下载 · 阴阳师-MuMu模拟器专版");
    RECT rect{};GetWindowRect(controls[AutoFind],&rect);int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,rect.left,rect.bottom,0,mainWindow,nullptr);DestroyMenu(menu);
    if(command==4001)selectDiscovered({gameCandidates(),false});else if(command==4002)browseGameFolder();else if(command==4003){
        auto result=(INT_PTR)ShellExecuteW(mainWindow,L"open",OfficialDownload,nullptr,nullptr,SW_SHOWNORMAL);status(result>32?L"已打开官方下载页面，请选择阴阳师-MuMu模拟器专版；安装后再自动查找。":L"无法打开浏览器，请访问 https://yys.163.com/zmb/ 。");
    }
}
static HWND control(int id,const wchar_t* cls,const S& label,DWORD style,DWORD ex=0){
    HWND w=CreateWindowExW(ex,cls,label.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,mainWindow,(HMENU)(INT_PTR)id,instance,nullptr);controls[id]=w;SendMessageW(w,WM_SETFONT,(WPARAM)bodyFont,TRUE);return w;
}
static void label(int id,const S& s){control(id,L"STATIC",s,SS_LEFT);}
static void edit(int id,const S& s,bool numeric=false){auto w=control(id,L"EDIT",s,WS_TABSTOP|ES_AUTOHSCROLL|(numeric?ES_NUMBER:0),WS_EX_CLIENTEDGE);SendMessageW(w,EM_SETLIMITTEXT,numeric?8:(id==Filter?256:32000),0);}
static LRESULT CALLBACK buttonProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_MOUSEMOVE&&!hover[w]){hover[w]=true;TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,w,0};TrackMouseEvent(&t);InvalidateRect(w,nullptr,TRUE);}
    if(msg==WM_MOUSELEAVE){hover[w]=false;InvalidateRect(w,nullptr,TRUE);}
    return DefSubclassProc(w,msg,wp,lp);
}
static void button(int id,const S& s){auto w=control(id,L"BUTTON",s,WS_TABSTOP|BS_OWNERDRAW);SetWindowSubclass(w,buttonProc,1,0);}
static void drawButton(DRAWITEMSTRUCT* d){
    bool primary=d->CtlID==Launch||d->CtlID==Arrange,disabled=(d->itemState&ODS_DISABLED)!=0;
    COLORREF fill=primary?RGB(46,112,107):RGB(236,241,246),ink=primary?RGB(255,255,255):RGB(38,56,78);
    if(d->CtlID==Hide){fill=RGB(249,235,229);ink=RGB(135,68,53);}if(d->CtlID==Show){fill=RGB(225,242,235);ink=RGB(38,99,76);}
    if(disabled){fill=RGB(235,237,240);ink=RGB(153,158,168);}else if(hover[d->hwndItem]||(d->itemState&ODS_SELECTED)){fill=RGB(std::max(0,(int)GetRValue(fill)-9),std::max(0,(int)GetGValue(fill)-9),std::max(0,(int)GetBValue(fill)-9));}
    FillRect(d->hDC,&d->rcItem,cardBrush);HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,fill);auto oldBrush=SelectObject(d->hDC,brush);auto oldPen=SelectObject(d->hDC,pen);
    RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,px(8),px(8));SelectObject(d->hDC,oldBrush);SelectObject(d->hDC,oldPen);DeleteObject(brush);DeleteObject(pen);
    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,ink);auto oldFont=SelectObject(d->hDC,bodyFont);S caption=text(d->hwndItem);RECT r=d->rcItem;DrawTextW(d->hDC,caption.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(d->itemState&ODS_FOCUS){InflateRect(&r,-px(4),-px(4));DrawFocusRect(d->hDC,&r);}SelectObject(d->hDC,oldFont);
}
static void fonts(){
    if(bodyFont)DeleteObject(bodyFont);if(titleFont)DeleteObject(titleFont);
    bodyFont=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-px(23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    for(auto& [id,w]:controls)SendMessageW(w,WM_SETFONT,(WPARAM)(id==Header?titleFont:bodyFont),TRUE);if(statusWindow)SendMessageW(statusWindow,WM_SETFONT,(WPARAM)bodyFont,TRUE);
}
static void layout(){
    RECT r;GetClientRect(mainWindow,&r);int W=MulDiv(r.right,96,dpi),H=MulDiv(r.bottom,96,dpi);int space=W-48;
    auto place=[&](int id,int x,int y,int w,int h){MoveWindow(controls[id],px(x),px(y),px(w),px(h),TRUE);};
    place(Icon,18,9,46,46);place(Header,76,9,148,29);place(Subtitle,78,38,210,20);place(Background,W-259,18,55,30);place(Details,W-196,18,98,30);place(Help,W-90,18,66,30);
    place(LPath,24,82,54,20);place(Path,82,78,W-244,28);place(AutoFind,W-154,78,64,28);place(Browse,W-84,78,60,28);
    place(LCount,24,122,54,20);place(Count,82,118,46,28);place(LDelay,140,122,64,20);place(Delay,210,118,62,28);place(Launch,W-218,116,100,32);place(CloseAll,W-108,116,84,32);
    place(Subtitle,78,38,170,20);place(About,250,38,68,22);
    place(LWindows,24,180,W-250,24);place(SelectAll,W-238,178,63,28);place(SelectNone,W-167,178,71,28);place(Refresh,W-88,178,64,28);
    place(LWidth,24,221,28,20);place(Width,56,217,72,28);place(LHeight,139,221,24,20);place(Height,169,217,72,28);place(LockRatio,254,219,108,24);place(Resize,W-150,216,126,32);
    place(LScreen,24,262,32,20);place(Screen,60,258,W-294,200);place(Arrange,W-218,256,194,34);
    place(LOpacity,24,305,88,20);place(Opacity,116,301,57,28);place(Alpha,182,299,76,32);place(Mute,268,299,132,32);place(Hide,W-158,299,63,32);place(Show,W-87,299,63,32);
    for(int id:{LFilter,Filter,Pick,SelectAll,SelectNone,List,LBoss,BossEnabled,BossKey,BossSave,BossDisable,LVolume,Volume,ApplyVolume})ShowWindow(controls[id],detailsOpen?SW_SHOW:SW_HIDE);
    if(detailsOpen){
        place(LFilter,24,366,56,20);place(Filter,84,362,W-260,28);place(Pick,W-168,362,144,28);
        place(LVolume,24,401,82,20);place(Volume,116,397,57,28);place(ApplyVolume,182,395,130,32);
        place(LBoss,24,450,56,20);place(BossEnabled,84,446,64,28);place(BossKey,156,445,W-364,28);place(BossSave,W-200,443,92,32);place(BossDisable,W-100,443,76,32);place(List,24,485,space,std::max(60,H-565));
    }
    place(Hint,24,H-56,W-202,20);place(Admin,W-170,H-60,146,28);MoveWindow(statusWindow,px(24),r.bottom-px(31),r.right-px(48),px(20),TRUE);
    // Native combo boxes choose their own closed height. Align labels using
    // the actual control bounds rather than the requested dropdown height.
    for(auto pair:{std::pair<int,int>{LPath,Path},{LCount,Count},{LDelay,Delay},{LFilter,Filter},{LWidth,Width},{LHeight,Height},{LScreen,Screen},{LOpacity,Opacity},{LBoss,BossEnabled},{LVolume,Volume}}){RECT label{},field{};if(!GetWindowRect(controls[pair.first],&label)||!GetWindowRect(controls[pair.second],&field)||field.bottom<=field.top)continue;MapWindowPoints(nullptr,mainWindow,(POINT*)&label,2);MapWindowPoints(nullptr,mainWindow,(POINT*)&field,2);MoveWindow(controls[pair.first],label.left,field.top+(field.bottom-field.top-(label.bottom-label.top))/2,label.right-label.left,label.bottom-label.top,TRUE);}
    if(listWindow){RECT table{};GetClientRect(listWindow,&table);int widths[]={std::max(px(80),(int)table.right-px(280)-GetSystemMetricsForDpi(SM_CXVSCROLL,dpi)-2),px(110),px(70),px(100)};for(int i=0;i<4;i++)ListView_SetColumnWidth(listWindow,i,widths[i]);}
}
static void panelSize(){RECT r{0,0,px(580),px(detailsOpen?700:410)},old{};GetWindowRect(mainWindow,&old);DWORD style=(DWORD)GetWindowLongPtrW(mainWindow,GWL_STYLE),ex=(DWORD)GetWindowLongPtrW(mainWindow,GWL_EXSTYLE);using AdjustDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);auto adjust=(AdjustDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi");if(adjust)adjust(&r,style,FALSE,ex,(UINT)dpi);else AdjustWindowRectEx(&r,style,FALSE,ex);MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfoW(MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST),&mi))return;int width=std::min(r.right-r.left,mi.rcWork.right-mi.rcWork.left),height=std::min(r.bottom-r.top,mi.rcWork.bottom-mi.rcWork.top);int x=std::clamp((int)old.left,(int)mi.rcWork.left,(int)mi.rcWork.right-width),y=std::clamp((int)old.top,(int)mi.rcWork.top,(int)mi.rcWork.bottom-height);SetWindowPos(mainWindow,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);RECT actual{};if(GetWindowRect(mainWindow,&actual)){int moveX=actual.left<mi.rcWork.left?mi.rcWork.left-actual.left:actual.right>mi.rcWork.right?mi.rcWork.right-actual.right:0;int moveY=actual.top<mi.rcWork.top?mi.rcWork.top-actual.top:actual.bottom>mi.rcWork.bottom?mi.rcWork.bottom-actual.bottom:0;if(moveX||moveY)SetWindowPos(mainWindow,nullptr,x+moveX,y+moveY,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}layout();InvalidateRect(mainWindow,nullptr,TRUE);}
static void createUi(){
    sizingFields=true;ratioWidth=16;ratioHeight=9;cfg.height=gameHeight(cfg.width);cfg.lockRatio=true;
    fonts();auto icon=control(Icon,L"STATIC",L"",SS_ICON|SS_CENTERIMAGE);SendMessageW(icon,STM_SETICON,(WPARAM)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,px(46),px(46),LR_DEFAULTCOLOR),0);label(Header,L"鼠卫兵");SendMessageW(controls[Header],WM_SETFONT,(WPARAM)titleFont,TRUE);label(Subtitle,L"阴阳师窗口管家 · 0.3.5");button(Background,L"后台");button(Details,L"窗口 / 设置");button(Help,L"说明");
    label(LPath,L"游戏");edit(Path,cfg.path);button(AutoFind,L"查找 ▾");button(Browse,L"选择");
    label(LCount,L"数量");edit(Count,std::to_wstring(cfg.count),true);label(LDelay,L"间隔ms");edit(Delay,std::to_wstring(cfg.delay),true);button(Launch,L"启动 / 多开");button(CloseAll,L"批量关闭");control(About,WC_LINK,L"<a>关于·更新</a>",WS_TABSTOP);
    label(LFilter,L"进程");edit(Filter,cfg.filter);button(Refresh,L"刷新");button(Pick,L"添加窗口（3 秒）");
    label(LWindows,L"游戏窗口");button(SelectAll,L"全选");button(SelectNone,L"全不选");
    listWindow=control(List,WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,WS_EX_CLIENTEDGE);
    SetWindowTheme(listWindow,L"Explorer",nullptr);
    ListView_SetExtendedListViewStyle(listWindow,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    const wchar_t* names[]={L"窗口标题",L"进程",L"状态",L"尺寸"};for(int i=0;i<4;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)names[i];c.cx=px(120);ListView_InsertColumn(listWindow,i,&c);}
    label(LWidth,L"宽");edit(Width,std::to_wstring(cfg.width),true);label(LHeight,L"高");edit(Height,std::to_wstring(cfg.height),true);control(LockRatio,L"BUTTON",L"固定16:9",BS_AUTOCHECKBOX);SendMessageW(controls[LockRatio],BM_SETCHECK,BST_CHECKED,0);EnableWindow(controls[LockRatio],FALSE);button(Resize,L"应用尺寸");
    label(LScreen,L"屏幕");control(Screen,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
    button(Arrange,L"自动排列 · 右侧纵向");label(LOpacity,L"不透明度 %");edit(Opacity,std::to_wstring(cfg.opacity),true);button(Alpha,L"应用");button(Hide,L"隐藏");button(Show,L"显示");
    label(LBoss,L"老板键");control(BossEnabled,L"BUTTON",L"启用",WS_TABSTOP|BS_AUTOCHECKBOX);SendMessageW(controls[BossEnabled],BM_SETCHECK,cfg.bossEnabled?BST_CHECKED:BST_UNCHECKED,0);
    control(BossKey,HOTKEY_CLASSW,L"",WS_TABSTOP);SendMessageW(controls[BossKey],HKM_SETHOTKEY,hotkeyControlValue(cfg.bossKey,cfg.bossMods),0);button(BossSave,L"保存快捷键");button(BossDisable,L"停用");
    label(Hint,adminHint(elevated));control(Admin,L"BUTTON",elevated?L"已是管理员":L"管理员启动",WS_TABSTOP|BS_PUSHBUTTON);EnableWindow(controls[Admin],!elevated);if(!elevated)SendMessageW(controls[Admin],BCM_SETSHIELD,0,TRUE);
    button(Mute,L"静音 / 取消静音");label(LVolume,L"音量 %");edit(Volume,L"100",true);button(ApplyVolume,L"应用所选音量");
    statusWindow=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_ENDELLIPSIS,0,0,0,0,mainWindow,nullptr,instance,nullptr);SendMessageW(statusWindow,WM_SETFONT,(WPARAM)bodyFont,TRUE);
    sizingFields=false;screens();layout();refresh();status(L"就绪 · 默认选择全部游戏窗口");
}
static void restoreHidden(){for(auto& [h,t]:hidden)if(valid(t))ShowWindowAsync(h,SW_SHOW);for(auto& [h,t]:hiddenCompanions)if(valid(t))ShowWindowAsync(h,SW_SHOW);}
static bool addTray(){
    if(smokeMode)return false;
    tray.cbSize=sizeof(tray);tray.hWnd=mainWindow;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;tray.uCallbackMessage=TrayMessage;tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wcscpy_s(tray.szTip,L"鼠卫兵 · 双击打开，右键退出");
    trayReady=Shell_NotifyIconW(NIM_ADD,&tray)!=0;if(trayReady){tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}return trayReady;
}
static void showUpdate(updates::Result* result){
    if(!result)return;
    if(result->release.version.empty()){delete result;return;}
    S message=L"发现新版本 "+result->release.version+L"。\n确认后下载更新，完成时会重启鼠卫兵，游戏继续运行。\n\n"+result->release.body.substr(0,1000);
    if(MessageBoxW(mainWindow,message.c_str(),L"鼠卫兵 · 软件更新",MB_YESNO|MB_ICONINFORMATION)==IDYES){updateBusy=updates::download(mainWindow,result->release);status(updateBusy?L"正在下载更新，完成后自动重启鼠卫兵…":L"无法开始下载，请稍后重试。");}
    delete result;
}
static void checkUpdates(bool manual){if(updateBusy){if(manual)status(L"正在检查或下载更新，请稍候。");return;}updateBusy=updates::check(mainWindow,manual);if(manual)status(updateBusy?L"正在检查 GitHub Release…":L"无法开始检查更新。");}
static void receiveUpdate(updates::Result* result){
    updateBusy=false;if(!result->error.empty()){if(result->manual)status(result->error);delete result;return;}
    if(result->download){S error;if(updates::handoff(result->file,error)){delete result;SendMessageW(mainWindow,WM_CLOSE,0,0);return;}status(error);delete result;return;}
    if(result->release.version.empty()){if(result->manual)status(L"当前已是最新版本。");delete result;return;}
    delete pendingUpdate;pendingUpdate=result;SetWindowTextW(controls[About],L"<a>有更新</a>");
    if(result->manual||(IsWindowVisible(mainWindow)&&GetForegroundWindow()==mainWindow&&startupWindows.empty()&&arrangement.rows.empty())){pendingUpdate=nullptr;showUpdate(result);}
    else if(trayReady){tray.uFlags=NIF_INFO;wcscpy_s(tray.szInfoTitle,L"鼠卫兵有新版本");wcscpy_s(tray.szInfo,L"打开鼠卫兵，查看并确认更新。");tray.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&tray);tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;}
}
static void about(){
    if(pendingUpdate){auto result=pendingUpdate;pendingUpdate=nullptr;showUpdate(result);return;}
    TASKDIALOG_BUTTON buttons[]={{101,L"打开 GitHub 首页"},{102,L"检查更新"},{103,L"查看许可证"}};
    TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);dialog.hwndParent=mainWindow;dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW|(cfg.autoUpdates?TDF_VERIFICATION_FLAG_CHECKED:0);dialog.pszWindowTitle=L"关于鼠卫兵";dialog.pszMainInstruction=L"鼠卫兵 · Phoenix Feng";dialog.pszContent=L"感谢摸鼠小工具激发项目想法，初版页面的功能组织和设计也参考了它。\n主要代码编写、修复与验证由 Codex 协助完成。\n\n独立开源项目，使用 MIT 许可证。";dialog.pszVerificationText=L"启动时自动检查更新";dialog.cButtons=3;dialog.pButtons=buttons;int choice=0;BOOL automatic=cfg.autoUpdates;
    if(SUCCEEDED(TaskDialogIndirect(&dialog,&choice,nullptr,&automatic))){cfg.autoUpdates=automatic!=FALSE;persist();if(choice==101)ShellExecuteW(mainWindow,L"open",L"https://github.com/Phoenix1996/shuweibing",nullptr,nullptr,SW_SHOWNORMAL);if(choice==102)checkUpdates(true);if(choice==103)ShellExecuteW(mainWindow,L"open",L"https://github.com/Phoenix1996/shuweibing/blob/main/NOTICE.md",nullptr,nullptr,SW_SHOWNORMAL);}
}
static void openPanel(){ShowWindow(mainWindow,SW_RESTORE);SetForegroundWindow(mainWindow);if(pendingScan){auto result=pendingScan;pendingScan=nullptr;selectDiscovered(*result);delete result;}if(pendingUpdate&&startupWindows.empty()&&arrangement.rows.empty()){auto result=pendingUpdate;pendingUpdate=nullptr;showUpdate(result);}}
static void trayMenu(){POINT p;GetCursorPos(&p);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,2001,L"打开鼠卫兵");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,2002,L"退出程序");SetForegroundWindow(mainWindow);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,mainWindow,nullptr);DestroyMenu(menu);PostMessageW(mainWindow,WM_NULL,0,0);if(choice==2001)openPanel();else if(choice==2002)SendMessageW(mainWindow,WM_CLOSE,0,0);}
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(taskbarCreated&&msg==taskbarCreated){addTray();return 0;}
    switch(msg){
    case WM_CREATE:{mainWindow=w;using Dpi=UINT(WINAPI*)(HWND);auto f=(Dpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");if(f)dpi=(int)f(w);createUi();if(smokeMode)SetTimer(w,9,4500,nullptr);return 0;}
    case WM_SIZE:if(wp==SIZE_MINIMIZED&&trayReady){ShowWindow(w,SW_HIDE);return 0;}if(statusWindow&&wp!=SIZE_MINIMIZED){layout();InvalidateRect(w,nullptr,TRUE);}return 0;
    case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)lp;RECT r{0,0,px(580),px(detailsOpen?650:410)};using AdjustDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);auto adjust=(AdjustDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi");if(adjust)adjust(&r,WS_OVERLAPPEDWINDOW,FALSE,0,(UINT)dpi);else AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0);MONITORINFO mi{sizeof(mi)};if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&mi)){p->ptMinTrackSize.x=std::min(r.right-r.left,mi.rcWork.right-mi.rcWork.left);p->ptMinTrackSize.y=std::min(r.bottom-r.top,mi.rcWork.bottom-mi.rcWork.top);}else p->ptMinTrackSize={r.right-r.left,r.bottom-r.top};return 0;}
    case WM_DPICHANGED:{dpi=HIWORD(wp);fonts();auto r=(RECT*)lp;SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
    case WM_DISPLAYCHANGE:stopArrangement();screens();return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);FillRect(dc,&r,background);auto old=SelectObject(dc,cardBrush);auto pen=CreatePen(PS_SOLID,1,RGB(226,232,239));auto oldPen=SelectObject(dc,pen);RoundRect(dc,px(12),px(68),r.right-px(12),px(160),px(14),px(14));RoundRect(dc,px(12),px(170),r.right-px(12),px(342),px(14),px(14));if(detailsOpen)RoundRect(dc,px(12),px(350),r.right-px(12),r.bottom-px(70),px(14),px(14));SelectObject(dc,old);SelectObject(dc,oldPen);DeleteObject(pen);EndPaint(w,&p);return 0;}
    case WM_DRAWITEM:drawButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(HWND)lp==controls[Header]?RGB(35,58,100):RGB(64,76,93));bool head=(HWND)lp==controls[Header]||(HWND)lp==controls[Subtitle]||(HWND)lp==controls[Icon]||(HWND)lp==controls[Hint]||(HWND)lp==statusWindow;return (LRESULT)(head?background:cardBrush);}
    case WM_NOTIFY:if(((NMHDR*)lp)->hwndFrom==listWindow&&((NMHDR*)lp)->code==LVN_ITEMCHANGED&&!loadingList)summary();if(((NMHDR*)lp)->hwndFrom==controls[About]&&(((NMHDR*)lp)->code==NM_CLICK||((NMHDR*)lp)->code==NM_RETURN))about();return 0;
    case updates::Message:receiveUpdate((updates::Result*)lp);return 0;
    case WM_APP+2:{auto result=(ScanResult*)lp;EnableWindow(controls[AutoFind],TRUE);if(!IsWindowVisible(w)||IsIconic(w)){delete pendingScan;pendingScan=result;status(L"文件夹扫描已完成，打开面板后选择客户端。");}else{selectDiscovered(*result);delete result;}return 0;}
    case WM_HOTKEY:if(bossRegistered&&(int)wp==bossRegistration&&!(GetForegroundWindow()==w&&GetFocus()==controls[BossKey]))toggleBoss();return 0;
    case TrayMessage:if(LOWORD(lp)==WM_LBUTTONDBLCLK||LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT)openPanel();else if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==WM_RBUTTONUP)trayMenu();return 0;
    case WM_COMMAND:if(HIWORD(wp)==EN_CHANGE&&(LOWORD(wp)==Width||LOWORD(wp)==Height)){sizeChanged(LOWORD(wp));return 0;}if(HIWORD(wp)==BN_CLICKED)switch(LOWORD(wp)){
        case Browse:browse();break;case Refresh:refresh();persist();status(L"游戏窗口已刷新。");break;
        case Admin:restartAdmin();break;case BossSave:saveBossKey();break;case BossDisable:saveBossKey(true);break;
        case AutoFind:discoveryMenu();break;
        case Details:detailsOpen=!detailsOpen;set(Details,detailsOpen?L"收起设置":L"窗口 / 设置");panelSize();break;
        case Background:if(trayReady||addTray())ShowWindow(w,SW_MINIMIZE);else status(L"托盘暂时不可用，可用任务栏最小化。");break;
        case LockRatio:if(SendMessageW(controls[LockRatio],BM_GETCHECK,0,0)==BST_CHECKED){int a,b;if(number(value(Width),160,7680,a)&&number(value(Height),90,4320,b)){ratioWidth=a;ratioHeight=b;}}break;
        case SelectAll:case SelectNone:for(int i=0;i<ListView_GetItemCount(listWindow);i++)ListView_SetCheckState(listWindow,i,LOWORD(wp)==SelectAll);break;
        case Launch:beginLaunch();break;
        case CloseAll:closeAllGames();break;
        case Mute:changeAudio(true);break;case ApplyVolume:changeAudio(false);break;
        case Pick:EnableWindow(controls[Pick],FALSE);SetTimer(w,2,3000,nullptr);status(L"请在 3 秒内切换到要管理的游戏窗口。");break;
        case Resize:case Arrange:case Alpha:case Hide:case Show:operate(LOWORD(wp));break;
        case Help:MessageBoxW(w,L"作者：Phoenix Feng\n鸣谢：摸鼠小工具激发了本项目的想法，初版页面的功能组织和设计也参考了它。\n\n默认选择全部游戏窗口；展开“窗口 / 设置”可选择部分窗口。\n\n客户端：点“查找”读取运行中的客户端、安装记录及常见目录，或扫描所选文件夹。推荐从 https://yys.163.com/zmb/ 下载阴阳师-MuMu模拟器专版。\n\n管理员：底部显示权限状态；普通权限可点“管理员启动”，接受 Windows 提示后重启本程序。取消提示会继续使用当前程序。\n\n老板键：默认 Ctrl + Alt + H，隐藏已选的可见游戏窗口并将面板收起；再次按恢复同一批窗口。此前已隐藏的窗口保持隐藏。展开设置，点击快捷键框按下新组合，再点“保存快捷键”；可停用。支持 Ctrl/Alt/Shift 组合及 F1–F11；占用时提示更换。\n\n固定 16:9，高度向上取整；宽 980 时高为 552。“自动排列”先应用当前尺寸，等待响应后从右上向下排列，排满向左。启动等待最长 120 秒并自动排列。“批量关闭”确认后正常关闭，并自动确认核验过的游戏退出提示；未保存进度可能丢失。\n每周三 9 点后首次多开提醒一次，可先只保留一个客户端更新。\n“静音 / 取消静音”按勾选客户端独立控制声音；展开设置可输入音量百分比。\n\n最小化或“后台”进入托盘，双击或右键“打开”恢复。右键退出或窗口关闭真正退出，显示本程序隐藏的游戏窗口。\n新启动默认启用清晰启动。多开仍受客户端限制；建议游戏使用窗口模式。",L"鼠卫兵 · 使用说明",MB_OK|MB_ICONINFORMATION);break;
        }return 0;
    case WM_TIMER:switch(wp){
        case 1:launchOne();break;
        case 2:{KillTimer(w,2);EnableWindow(controls[Pick],TRUE);HWND fg=GetForegroundWindow();Target t=targetOf(fg);if(fg&&fg!=w&&t.pid!=GetCurrentProcessId()&&valid(t)&&!t.title.empty()){manualTargets.push_back(t);refresh();status(L"已添加窗口："+t.title);}else status(L"没有选到有效窗口，请重试并切换到游戏。");break;}
        case 3:KillTimer(w,3);for(auto i=hidden.begin();i!=hidden.end();){if(!valid(i->second)||IsWindowVisible(i->first))i=hidden.erase(i);else ++i;}for(auto i=hiddenCompanions.begin();i!=hiddenCompanions.end();){if(!valid(i->second)||IsWindowVisible(i->first))i=hiddenCompanions.erase(i);else ++i;}saveHidden();refresh();break;
        case 4:arrangementTick();break;
        case 5:bossFinishTick();break;
        case 6:startupTick();break;
        case 7:closeTick();break;
        case 9:KillTimer(w,9);DestroyWindow(w);break;
        }return 0;
    case WM_CLOSE:stopArrangement();persist();restoreHidden();DestroyWindow(w);return 0;
    case WM_DESTROY:delete pendingScan;pendingScan=nullptr;delete pendingUpdate;pendingUpdate=nullptr;if(bossRegistered)UnregisterHotKey(w,bossRegistration);bossRegistered=false;if(trayReady)Shell_NotifyIconW(NIM_DELETE,&tray);trayReady=false;PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,msg,wp,lp);
}
static bool selfTest(const S& dir,bool ciMode=false){
    CreateDirectoryW(dir.c_str(),nullptr);int failed=0,count=0;auto check=[&](bool ok,const char* name){count++;printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)failed++;};
    Settings a;a.path=L"C:\\游戏目录\\Launch.exe";auto b=encode(a);Settings c;
    check(decode(b,c)&&c.path==a.path&&c.width==1113&&c.height==627,"unicode settings round trip");
    S encodedWithoutArgs((const wchar_t*)(b.data()+8),(b.size()-8)/2);S legacyArgs=encodedWithoutArgs;legacyArgs.insert(legacyArgs.find(L"filter="),L"args=--old-launch-argument\n");std::vector<unsigned char> oldArgs(8+legacyArgs.size()*sizeof(wchar_t));auto oldArgsHash=hash(legacyArgs.data(),legacyArgs.size()*sizeof(wchar_t));memcpy(oldArgs.data(),&oldArgsHash,8);memcpy(oldArgs.data()+8,legacyArgs.data(),legacyArgs.size()*sizeof(wchar_t));
    check(encodedWithoutArgs.find(L"args=")==S::npos&&decode(oldArgs,c),"launch argument setting is removed and old files remain readable");
    auto legacy=[&](int fields){
        S data((const wchar_t*)(b.data()+8),(b.size()-8)/2);
        for(auto line:{L"wednesdayReminder=0\n",L"autoUpdates=1\n",L"bossEnabled=1\n",L"bossKey=72\n",L"bossMods=3\n",fields<11?L"sharpLaunch=1\n":L"",fields<10?L"lockRatio=1\n":L""}){if(!*line)continue;auto pos=data.find(line);if(pos!=S::npos)data.erase(pos,wcslen(line));}
        std::vector<unsigned char> bytes(8+data.size()*2);auto checkSum=hash(data.data(),data.size()*2);memcpy(bytes.data(),&checkSum,8);memcpy(bytes.data()+8,data.data(),data.size()*2);return bytes;
    };
    check(decode(legacy(9),c)&&c.lockRatio&&decode(legacy(10),c)&&decode(legacy(11),c)&&c.bossEnabled&&c.bossKey=='H',"older nine, ten and eleven field settings retain compatibility and default boss key");
    auto customBoss=a;customBoss.bossEnabled=false;customBoss.bossKey=VK_F8;customBoss.bossMods=MOD_CONTROL|MOD_SHIFT;check(decode(encode(customBoss),c)&&!c.bossEnabled&&c.bossKey==VK_F8&&c.bossMods==(MOD_CONTROL|MOD_SHIFT),"custom boss key and disabled state round trip");
    check(validBossKey('H',MOD_CONTROL|MOD_ALT)&&validBossKey(VK_F8,0)&&!validBossKey(VK_F12,MOD_CONTROL)&&!validBossKey('H',0)&&!validBossKey(VK_CONTROL,MOD_CONTROL),"boss key rejects reserved and bare typing keys");
    check(hotkeyModifiers(hotkeyControlValue('H',MOD_CONTROL|MOD_ALT|MOD_SHIFT))==(MOD_CONTROL|MOD_ALT|MOD_SHIFT),"native hotkey modifier mapping round trip");
    S encodedSettings((const wchar_t*)(b.data()+8),(b.size()-8)/2);S legacySharp=encodedSettings;legacySharp.insert(legacySharp.find(L"bossEnabled="),L"sharpLaunch=0\n");std::vector<unsigned char> oldSharp(8+legacySharp.size()*sizeof(wchar_t));auto oldSharpHash=hash(legacySharp.data(),legacySharp.size()*sizeof(wchar_t));memcpy(oldSharp.data(),&oldSharpHash,8);memcpy(oldSharp.data()+8,legacySharp.data(),legacySharp.size()*sizeof(wchar_t));
    check(encodedSettings.find(L"sharpLaunch=")==S::npos&&decode(oldSharp,c),"sharp launch is always on, hidden from settings, and old preference files remain readable");
    auto updateSettings=a;updateSettings.autoUpdates=false;check(decode(encode(updateSettings),c)&&!c.autoUpdates,"automatic update check preference survives settings round trip");
    SYSTEMTIME wednesday{};wednesday.wYear=2026;wednesday.wMonth=10;wednesday.wDay=14;wednesday.wDayOfWeek=3;wednesday.wHour=8;
    check(!reminderDue(a,wednesday,0),"weekly game update reminder stays quiet before Wednesday nine");wednesday.wHour=9;
    check(reminderDue(a,wednesday,0),"weekly game update reminder activates at Wednesday nine for multi-launch");
    auto reminded=a;reminded.wednesdayReminder=reminderDay(wednesday);check(decode(encode(reminded),c)&&!reminderDue(c,wednesday,0),"weekly reminder date persists and suppresses repeats after restart");
    wednesday.wDay=21;check(reminderDue(reminded,wednesday,0),"next Wednesday receives a new reminder");wednesday.wDayOfWeek=4;check(!reminderDue(a,wednesday,2),"other weekdays never show the update reminder");wednesday.wDayOfWeek=3;auto single=a;single.count=1;check(!reminderDue(single,wednesday,0)&&reminderDue(single,wednesday,2),"single launch stays quiet unless multiple clients already exist");
    check(sharpCompatibility(L"~ RUNASADMIN DPIUNAWARE GdiDpiScaling HighDpiAware")==L"RUNASADMIN HIGHDPIAWARE","sharp launch replaces conflicting DPI flags and preserves other compatibility flags");
    auto launchEnv=launchEnvironment(true);bool envGood=launchEnv.size()>2&&launchEnv[launchEnv.size()-1]==0&&launchEnv[launchEnv.size()-2]==0;int layers=0;
    for(auto p=launchEnv.data();envGood&&*p;p+=wcslen(p)+1)if(lower(p).rfind(L"__compat_layer=",0)==0){layers++;if(S(p).find(L"HIGHDPIAWARE")==S::npos)envGood=false;}
    check(envGood&&layers==1,"private Unicode launch environment has exactly one DPI override and double terminator");
    auto damaged=b;damaged.back()^=0x55;check(!decode(damaged,c),"checksum rejects corruption");
    check(!decode(std::vector<unsigned char>(182,0),c),"zero-filled settings rejected");
    check(!decode(std::vector<unsigned char>(1,0),c),"truncated settings rejected");
    auto bad=a;bad.opacity=0;check(!decode(encode(bad),c),"out-of-range settings rejected");
    S p=dir+L"\\test-settings.dat";DeleteFileW(p.c_str());DeleteFileW((p+L".bak").c_str());
    check(saveSettings(p,a),"atomic first save");a.width=1200;check(saveSettings(p,a),"atomic subsequent save and backup");int source;
    check(loadSettings(p,c,source)&&source==0&&c.width==1200,"load current settings");
    unsigned char zero[182]{};atomicWrite(p,zero,sizeof(zero));check(loadSettings(p,c,source)&&source==1&&c.width==1113,"corruption recovers valid backup");
    atomicWrite(p+L".bak",zero,sizeof(zero));check(!loadSettings(p,c,source)&&source==2,"both corrupt returns defaults path");
    bool fits=true;for(int n=1;n<=20;n++){RECT work{-1920,-200,0,880};auto cells=stackRight(std::vector<SIZE>(n,{240,160}),work);if(cells.size()!=(size_t)n){fits=false;break;}for(auto cell:cells)if(cell.x<work.left||cell.y<work.top||cell.x+cell.w>work.right||cell.y+cell.h>work.bottom||cell.w!=240||cell.h!=160)fits=false;}
    check(fits,"right stack fits 1-20 windows without resizing, negative origin");
    auto vertical=stackRight(std::vector<SIZE>(5,{320,240}),{0,0,1920,1080});
    check(vertical.size()==5&&vertical[0].x==1600&&vertical[0].y==0&&vertical[1].x==1600&&vertical[1].y==248&&vertical[3].x==1600&&vertical[4].x==1272&&vertical[4].y==0,"fills right column top-down before extending left");
    auto edge=stackRight(std::vector<SIZE>(3,{320,240}),{0,0,1000,488});check(edge.size()==3&&edge[1].y==248&&edge[2].y==0&&edge[2].x<edge[1].x,"exact vertical fit includes gap");
    check(stackRight(std::vector<SIZE>(2,{320,240}),{0,0,400,400}).empty(),"screen overflow rejected without shrinking");
    auto mixed=stackRight({{300,240},{320,240},{280,300}},{-1000,-200,0,300});check(mixed.size()==3&&mixed[0].x+mixed[0].w==0&&mixed[1].x+mixed[1].w==0&&mixed[2].x+mixed[2].w==-328,"different border widths right-aligned and separate columns");
    check(pairedDimension(980,1113,627)==552&&pairedDimension(552,627,1113)==980,"game default aspect ratio counterpart rounding");
    int n=0;check(!number(L"99999999",1,20,n)&&!number(L"-1",1,20,n)&&number(L"20",1,20,n),"numeric boundaries");
    check(matches(L"阴阳师-网易游戏",L"onmyoji.exe",L"onmyoji;YysLauncher")&&!matches(L"yys 阴阳师攻略",L"msedge.exe",L"onmyoji;YysLauncher"),"process filters exclude browser title matches");
    // Integration uses only our own synthetic window, never the user's programs.
    HWND fake=CreateWindowExW(0,L"STATIC",L"SWB Self Test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,instance,nullptr);Target t=targetOf(fake);
    check(valid(t),"window identity includes process birth time");auto wrong=t;wrong.born++;check(!valid(wrong),"stale identity rejected");
    check(transparency(t,70)&&transparency(t,100),"native transparency applied and layer removed");
    check(clientResize(t,640,360),"native resize request");RECT actual{};GetClientRect(fake,&actual);check(actual.right==640&&actual.bottom==360,"client dimensions are exact");
    HWND overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"STATIC",L"SWB Test Overlay",WS_POPUP|WS_VISIBLE,0,0,80,40,fake,nullptr,instance,nullptr);auto related=companions(t);check(related.size()==1&&related[0].hwnd==overlay,"same-process topmost companion is associated with its game window");
    configDir=dir;hidden[t.hwnd]=t;for(auto& extra:related)hiddenCompanions[extra.hwnd]=extra;check(saveHidden(),"hidden identity journal saved");ShowWindow(fake,SW_HIDE);ShowWindow(overlay,SW_HIDE);hidden.clear();hiddenCompanions.clear();loadHidden();check(hidden.size()==1&&hiddenCompanions.size()==1&&valid(hidden.begin()->second)&&valid(hiddenCompanions.begin()->second),"hidden game and companion recovered from journal");restoreHidden();
    MSG pending{};for(int i=0;i<4;i++){while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessageW(&pending);}Sleep(20);}check(IsWindowVisible(fake)!=0&&IsWindowVisible(overlay)!=0,"hidden game and topmost companion restored together");
    DestroyWindow(overlay);DestroyWindow(fake);check(!valid(t),"destroyed window rejected");saved.clear();hidden.clear();hiddenCompanions.clear();
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_HOTKEY_CLASS|ICC_LINK_CLASS};InitCommonControlsEx(&cc);background=CreateSolidBrush(RGB(244,246,250));cardBrush=CreateSolidBrush(RGB(255,255,255));
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"SWB.TestPanel";wc.lpfnWndProc=proc;RegisterClassW(&wc);
    HWND panel=CreateWindowExW(0,wc.lpszClassName,L"SWB Test Panel",WS_OVERLAPPEDWINDOW,0,0,600,450,nullptr,nullptr,instance,nullptr);panelSize();RECT client{};GetClientRect(panel,&client);
    check(panel&&client.right==px(580)&&client.bottom==px(410)&&!(GetWindowLongPtrW(listWindow,GWL_STYLE)&WS_VISIBLE),"compact panel size and collapsed list");
    check(controls[BossKey]&&value(Hint)==adminHint(false)&&IsWindowEnabled(controls[Admin]),"native boss key control and normal privilege hint are available");
    check(adminHint(true)!=adminHint(false)&&adminHint(true).find(L"管理员")!=S::npos,"administrator hint distinguishes elevated execution");
    RECT footer{};GetWindowRect(statusWindow,&footer);MapWindowPoints(nullptr,panel,(POINT*)&footer,2);check(footer.left==px(24)&&footer.right==client.right-px(24)&&footer.bottom<=client.bottom-px(10),"footer horizontal and bottom padding");
    set(Width,L"980");check(value(Height)==L"552","editing width uses fixed 16:9 with ceiling height");set(Height,L"627");check(value(Width)==L"1114"&&gameHeight(1113)==627,"editing height and legacy 1113 width obey integer 16:9 rules");
    check(!IsWindowEnabled(controls[LockRatio]),"game aspect ratio cannot be unlocked");
    set(Width,L"7680");Settings largest;check(value(Height)==L"4320"&&collect(largest,false),"largest supported 16:9 size is accepted");set(Width,L"7681");check(!collect(largest,false),"size outside supported bounds is rejected");set(Width,L"980");
    // Exercise the same measured geometry and move-only path used by the Arrange button.
    auto monitor=MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(monitor,&mi);
    HWND one=CreateWindowExW(0,L"STATIC",L"Arrange Test 1",WS_OVERLAPPEDWINDOW,mi.rcWork.right+100,mi.rcWork.top,300,180,nullptr,nullptr,instance,nullptr);
    HWND two=CreateWindowExW(WS_EX_CLIENTEDGE,L"STATIC",L"Arrange Test 2",WS_OVERLAPPEDWINDOW,mi.rcWork.right+100,mi.rcWork.top,320,180,nullptr,nullptr,instance,nullptr);
    std::vector<Target> rows{targetOf(one),targetOf(two)};RECT beforeOne{},beforeTwo{},afterOne{},afterTwo{};
    GetWindowRect(one,&beforeOne);GetWindowRect(two,&beforeTwo);
    bool began=beginArrangement(rows,monitor),duplicate=!beginArrangement(rows,monitor);for(int i=0;i<8&&!arrangement.rows.empty();i++)arrangementTick();
    GetWindowRect(one,&afterOne);GetWindowRect(two,&afterTwo);
    check(began&&duplicate&&arrangement.rows.empty()&&IsWindowEnabled(controls[Arrange]),"arrange coalesces duplicate requests and completes");
    check(afterOne.right==mi.rcWork.right&&afterTwo.right==mi.rcWork.right&&afterOne.top==mi.rcWork.top&&afterTwo.top==afterOne.bottom+8,"native actual window bounds align at right edge without overflow");
    bool stable=true;for(int repeat=0;repeat<10;repeat++){
        beginArrangement(rows,monitor);for(int i=0;i<8&&!arrangement.rows.empty();i++)arrangementTick();
        RECT r1{},r2{};GetWindowRect(one,&r1);GetWindowRect(two,&r2);
        if(!EqualRect(&r1,&afterOne)||!EqualRect(&r2,&afterTwo))stable=false;
    }
    check(stable&&afterOne.right-afterOne.left==beforeOne.right-beforeOne.left&&afterOne.bottom-afterOne.top==beforeOne.bottom-beforeOne.top&&afterTwo.right-afterTwo.left==beforeTwo.right-beforeTwo.left&&afterTwo.bottom-afterTwo.top==beforeTwo.bottom-beforeTwo.top,"ten repeated arrangements preserve exact size and position despite different input dimensions");
    SetWindowPos(one,nullptr,mi.rcWork.left,mi.rcWork.top,mi.rcWork.right-mi.rcWork.left+100,180,SWP_NOZORDER|SWP_NOACTIVATE);GetWindowRect(one,&beforeOne);GetWindowRect(two,&beforeTwo);
    beginArrangement(rows,monitor);arrangementTick();GetWindowRect(one,&afterOne);GetWindowRect(two,&afterTwo);
    check(arrangement.rows.empty()&&EqualRect(&beforeOne,&afterOne)&&EqualRect(&beforeTwo,&afterTwo),"oversized measured window cancels before moving or resizing any window");
    beginArrangement({targetOf(two)},monitor);DestroyWindow(two);arrangementTick();check(arrangement.rows.empty(),"destroyed target cancels pending arrangement");
    DestroyWindow(one);saved.clear();
    using ThreadContext=HANDLE(WINAPI*)(HANDLE);auto setContext=(ThreadContext)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetThreadDpiAwarenessContext");
    HANDLE original=setContext?setContext((HANDLE)(INT_PTR)-1):nullptr;
    HWND unaware=CreateWindowExW(0,L"STATIC",L"Arrange DPI unaware test",WS_OVERLAPPEDWINDOW,0,0,300,180,nullptr,nullptr,instance,nullptr);
    if(original)setContext(original);RECT beforeUnaware{},afterUnaware{};GetWindowRect(unaware,&beforeUnaware);
    beginArrangement({targetOf(unaware)},monitor);for(int i=0;i<8&&!arrangement.rows.empty();i++)arrangementTick();GetWindowRect(unaware,&afterUnaware);
    check(original&&arrangement.rows.empty()&&afterUnaware.right==mi.rcWork.right&&afterUnaware.top==mi.rcWork.top&&afterUnaware.right-afterUnaware.left==beforeUnaware.right-beforeUnaware.left&&afterUnaware.bottom-afterUnaware.top==beforeUnaware.bottom-beforeUnaware.top,"DPI unaware target right-aligned in physical pixels with exact size preserved");DestroyWindow(unaware);saved.clear();
    S probe=folderOf(dir)+L"\\dpi-probe.exe";
    if(fileExists(probe)){
        auto runProbe=[&](bool sharp){
            S output=dir+(sharp?L"\\probe-sharp.txt":L"\\probe-normal.txt");S command=L"\""+probe+L"\" \""+output+L"\"";std::vector<wchar_t> args(command.begin(),command.end());args.push_back(0);
            auto environment=launchEnvironment(sharp);STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
            if(!CreateProcessW(probe.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,environment.data(),nullptr,&startup,&child))return false;
            bool done=WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0;if(!done)TerminateProcess(child.hProcess,1);CloseHandle(child.hThread);CloseHandle(child.hProcess);
            std::vector<unsigned char> bytes;if(!done||!readBytes(output,bytes))return false;std::string line(bytes.begin(),bytes.end());
            return sharp?line.rfind("awareness=2 ",0)==0||line.rfind("awareness=1 ",0)==0:line.rfind("awareness=0 ",0)==0;
        };
        check(runProbe(false)&&runProbe(true),"real child process uses same launch environment to switch from DPI unaware to aware");
        S installation=dir+L"\\多开测试 带空格";CreateDirectoryW(installation.c_str(),nullptr);CreateDirectoryW((installation+L"\\bin").c_str(),nullptr);
        unsigned char placeholder=0;atomicWrite(installation+L"\\Launch.exe",&placeholder,1);CopyFileW(probe.c_str(),(installation+L"\\bin\\onmyoji.exe").c_str(),FALSE);
        LaunchSpec resolved;S error;bool resolvedOk=resolveLaunch(installation+L"\\Launch.exe",L"--sample \"中文参数\"",resolved,error);
        check(resolvedOk&&resolved.directClient&&resolved.executable==installation+L"\\bin\\onmyoji.exe"&&resolved.directory==installation&&resolved.args==L"--sample \"中文参数\"","launcher resolves to independent client with root working directory and unchanged arguments");
        LaunchSpec direct;check(resolveLaunch(installation+L"\\bin\\onmyoji.exe",L"",direct,error)&&direct.directory==installation&&direct.directClient,"explicit bin client also uses installation root as working directory");
        CreateDirectoryW((installation+L"\\incomplete").c_str(),nullptr);atomicWrite(installation+L"\\incomplete\\Launch.exe",&placeholder,1);
        check(!resolveLaunch(installation+L"\\incomplete\\Launch.exe",L"",direct,error)&&!error.empty(),"missing independent client rejects launch without falling back to launcher");
        std::vector<PROCESS_INFORMATION> children;std::vector<Target> windows;bool preserved=true,rootDirectory=true;
        // Three starts use the production spawn path; the placeholder Launch.exe cannot run.
        for(int launch=0;launch<3;launch++){
            S record=installation+L"\\instance-"+std::to_wstring(launch)+L".txt";DeleteFileW(record.c_str());
            resolveLaunch(installation+L"\\Launch.exe",L"--hold \""+record+L"\"",resolved,error);PROCESS_INFORMATION child{};
            if(!spawnClient(resolved,true,child)){preserved=false;break;}CloseHandle(child.hThread);child.hThread=nullptr;children.push_back(child);
            std::vector<unsigned char> bytes;ULONGLONG end=GetTickCount64()+4000;bool ready=false;
            while(GetTickCount64()<end){if(readBytes(record,bytes)&&bytes.size()>=8&&bytes.size()%2==0){ready=true;break;}if(WaitForSingleObject(child.hProcess,20)==WAIT_OBJECT_0)break;}
            if(!ready){preserved=false;break;}S contents((const wchar_t*)bytes.data(),bytes.size()/2);auto line=contents.find(L'\n');
            if(line==S::npos){preserved=false;break;}HWND window=(HWND)(uintptr_t)_wcstoui64(contents.c_str()+1,nullptr,10);Target target=targetOf(window);
            if(!valid(target)||target.pid!=child.dwProcessId){preserved=false;break;}windows.push_back(target);if(contents.substr(line+1)!=installation)rootDirectory=false;
            for(auto& previous:windows)if(!valid(previous))preserved=false;
            for(auto& previous:children)if(WaitForSingleObject(previous.hProcess,0)!=WAIT_TIMEOUT)preserved=false;
        }
        check(preserved&&windows.size()==3&&windows[0].pid!=windows[1].pid&&windows[1].pid!=windows[2].pid,"three repeated independent starts keep all earlier process and window identities alive");
        check(rootDirectory&&windows.size()==3,"all independent starts receive correct Unicode working directory");
        for(auto& window:windows)if(valid(window))PostMessageW(window.hwnd,WM_CLOSE,0,0);
        for(auto& child:children){if(WaitForSingleObject(child.hProcess,2000)!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);CloseHandle(child.hProcess);}
    }
    configPath=dir+L"\\manager-settings.dat";
    bool registered=false;int testKey=VK_F6,testMods=MOD_CONTROL|MOD_ALT|MOD_SHIFT;
    for(;testKey<=VK_F11;testKey++)if(registerBoss(true,testKey,testMods,false)){registered=true;break;}
    check(registered,"global boss key registration");cfg.bossKey=testKey;cfg.bossMods=testMods;cfg.bossEnabled=registered;
    int collisionKey='A';bool reserved=false;for(;collisionKey<='Z';collisionKey++)if(RegisterHotKey(panel,3099,testMods|MOD_NOREPEAT,collisionKey)){reserved=true;break;}
    int previousId=bossRegistration;
    check(reserved&&!registerBoss(true,collisionKey,testMods,false)&&bossRegistered&&bossRegistration==previousId&&registeredKey==testKey,"conflicting boss key preserves original registration");
    if(reserved)UnregisterHotKey(panel,3099);
    SendMessageW(controls[BossKey],HKM_SETHOTKEY,hotkeyControlValue(testKey,testMods),0);saveBossKey();Settings loadedBoss;int bossSource=0;
    check(loadSettings(configPath,loadedBoss,bossSource)&&loadedBoss.bossKey==testKey&&loadedBoss.bossMods==testMods,"custom boss key saved through native settings controls");
    HWND visible=CreateWindowExW(0,L"STATIC",L"Boss Visible Test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,320,180,nullptr,nullptr,instance,nullptr);
    HWND prehidden=CreateWindowExW(0,L"STATIC",L"Boss Prehidden Test",WS_OVERLAPPEDWINDOW,0,0,320,180,nullptr,nullptr,instance,nullptr);
    Target vt=targetOf(visible),pt=targetOf(prehidden);hidden[prehidden]=pt;
    auto finishBoss=[&](){ULONGLONG end=GetTickCount64()+2200;while(bossBusy&&GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}bossFinishTick();Sleep(20);}};
    toggleBossRows({vt,pt});finishBoss();
    check(bossActive&&!bossBusy&&!IsWindowVisible(visible)&&bossHidden.size()==1&&bossHidden.count(visible),"boss key hides only visible targets and journals ownership");
    toggleBossRows({pt});finishBoss();
    check(!bossActive&&IsWindowVisible(visible)&&!IsWindowVisible(prehidden)&&hidden.count(prehidden)&&!hidden.count(visible),"boss restore uses original batch and preserves previously hidden windows");
    toggleBossRows({vt});finishBoss();DestroyWindow(visible);toggleBossRows({});finishBoss();check(!bossActive&&bossHidden.empty(),"closed boss target is safely discarded on restore");
    DestroyWindow(prehidden);hidden.clear();bossHidden.clear();saveBossKey(true);check(!bossRegistered&&!cfg.bossEnabled,"boss key disabled and unregistered from settings");
    S scanRoot=dir+L"\\directory-discovery",nested=scanRoot+L"\\Netease",game=nested+L"\\yys";CreateDirectoryW(scanRoot.c_str(),nullptr);CreateDirectoryW(nested.c_str(),nullptr);CreateDirectoryW(game.c_str(),nullptr);CreateDirectoryW((game+L"\\bin").c_str(),nullptr);
    unsigned char dummy=0;atomicWrite(game+L"\\launch.exe",&dummy,1);auto scanned=scanGameFolder(scanRoot);std::vector<S> fromBin;launchersIn(fromBin,game+L"\\bin");
    check(scanned.paths.size()==1&&lower(scanned.paths.front())==lower(game+L"\\launch.exe")&&!scanned.limited,"selected folder scan finds nested launcher");
    check(fromBin.size()==1&&lower(fromBin.front())==lower(game+L"\\launch.exe"),"running executable bin directory maps to parent launcher");
    addGamePath(fromBin,game+L"\\LAUNCH.EXE");addGamePath(fromBin,game+L"\\missing.exe");check(fromBin.size()==1,"directory candidates deduplicate case and ignore missing executables");
    check(scanGameFolder(scanRoot+L"\\missing").paths.empty(),"missing scan folder returns no candidates");
    check(S(OfficialDownload)==L"https://yys.163.com/zmb/","official download uses user supplied URL");
    detailsOpen=true;panelSize();check((GetWindowLongPtrW(listWindow,GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongPtrW(controls[BossKey],GWL_STYLE)&WS_VISIBLE),"window selection and boss key settings expand");
    check(!childHasCaption(panel,L"恢复布局")&&!childHasCaption(panel,L"停止启动")&&!childHasCaption(panel,L"清晰启动 · 避免系统拉伸（新启动生效）")&&!childHasCaption(panel,L"参数"),"removed layout, startup cancel, sharp launch, and argument controls are absent");
    auto centerMatches=[](int labelId,int fieldId){RECT a{},b{};if(!GetWindowRect(controls[labelId],&a)||!GetWindowRect(controls[fieldId],&b))return false;int delta=abs((a.top+a.bottom)-(b.top+b.bottom));if(delta>2)printf("ALIGN ids=%d,%d label=%ld,%ld field=%ld,%ld dpi=%d\n",labelId,fieldId,a.top,a.bottom,b.top,b.bottom,dpi);return delta<=2;};
    check(centerMatches(LPath,Path)&&centerMatches(LCount,Count)&&centerMatches(LDelay,Delay)&&centerMatches(LFilter,Filter)&&centerMatches(LWidth,Width)&&centerMatches(LHeight,Height)&&centerMatches(LScreen,Screen)&&centerMatches(LOpacity,Opacity)&&centerMatches(LBoss,BossEnabled),"labels align vertically with their input controls");
    RECT hideRect{},showRect{},panelClient{};GetClientRect(panel,&panelClient);GetWindowRect(controls[Hide],&hideRect);GetWindowRect(controls[Show],&showRect);MapWindowPoints(nullptr,panel,(POINT*)&hideRect,2);MapWindowPoints(nullptr,panel,(POINT*)&showRect,2);
    check(abs(hideRect.right+px(8)-showRect.left)<=1&&abs(showRect.right-(panelClient.right-px(24)))<=1,"hide and show buttons align to the right edge");
    RECT allRect{},noneRect{},refreshRect{};GetWindowRect(controls[SelectAll],&allRect);GetWindowRect(controls[SelectNone],&noneRect);GetWindowRect(controls[Refresh],&refreshRect);MapWindowPoints(nullptr,panel,(POINT*)&allRect,2);MapWindowPoints(nullptr,panel,(POINT*)&noneRect,2);MapWindowPoints(nullptr,panel,(POINT*)&refreshRect,2);
    check(allRect.top==noneRect.top&&noneRect.top==refreshRect.top&&abs(allRect.right+px(8)-noneRect.left)<=1&&abs(noneRect.right+px(8)-refreshRect.left)<=1,"select controls sit beside refresh above the window list");
    for(int i=0;i<2;i++){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=(LPWSTR)L"完整窗口列表测试";ListView_InsertItem(listWindow,&item);}
    RECT table{},second{};GetClientRect(listWindow,&table);ListView_GetItemRect(listWindow,1,&second,LVIR_BOUNDS);int columns=0;for(int i=0;i<4;i++)columns+=ListView_GetColumnWidth(listWindow,i);
    check(second.bottom<=table.bottom&&columns<=table.right,"expanded table shows two complete rows and every column without horizontal clipping");
    int originalDpi=dpi;dpi=144;fonts();panelSize();RECT panelBounds{};GetWindowRect(panel,&panelBounds);MONITORINFO work{sizeof(work)};GetMonitorInfoW(MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST),&work);
    bool panelInside=panelBounds.top>=work.rcWork.top&&panelBounds.bottom<=work.rcWork.bottom;if(!panelInside)printf("PANEL bounds=%ld,%ld work=%ld,%ld dpi=%d\n",panelBounds.top,panelBounds.bottom,work.rcWork.top,work.rcWork.bottom,dpi);check(panelInside,"expanded panel is repositioned and kept inside work area at 150 percent scale");dpi=originalDpi;fonts();panelSize();
    detailsOpen=false;panelSize();check(!(GetWindowLongPtrW(listWindow,GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongPtrW(controls[BossKey],GWL_STYLE)&WS_VISIBLE),"window list and boss settings collapse again");
    HWND readyWindow=CreateWindowExW(0,L"STATIC",L"Startup Test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,640,400,nullptr,nullptr,instance,nullptr);Target readyTarget=targetOf(readyWindow);clientResize(readyTarget,640,400);StartupWindow startup{readyTarget.pid,readyTarget.born};
    bool initially=!observeStartup(startup,readyTarget,1000);for(ULONGLONG now=1250;now<2000;now+=250)initially=initially&&!observeStartup(startup,readyTarget,now);
    check(initially&&observeStartup(startup,readyTarget,2000),"one-click startup accepts stable windows before applying the configured aspect ratio");
    clientResize(readyTarget,320,180);check(!observeStartup(startup,readyTarget,3500),"startup stability restarts after window size changes");
    startupWindows.push_back(startup);launchRemaining=1;stopStartup();check(startupWindows.empty()&&launchRemaining==0&&valid(readyTarget),"stop startup cancels readiness tracking and leaves launched window alive");
    wchar_t self[MAX_PATH]{};GetModuleFileNameW(instance,self,MAX_PATH);S closeProbe=folderOf(self)+L"\\..\\..\\.build\\dpi-probe.exe",fixturePath=dir+L"\\close-fixture.txt";S command=L"\""+closeProbe+L"\" --close-fixture \""+fixturePath+L"\"";std::vector<wchar_t> args(command.begin(),command.end());args.push_back(0);STARTUPINFOW startupInfo{};startupInfo.cb=sizeof(startupInfo);PROCESS_INFORMATION child{};bool spawned=CreateProcessW(closeProbe.c_str(),args.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startupInfo,&child)!=0;HWND closeWindow=nullptr;
    if(spawned){FixtureWindow fixture{child.dwProcessId};ULONGLONG end=GetTickCount64()+3000;while(!fixture.hwnd&&GetTickCount64()<end){EnumWindows(findFixtureWindow,(LPARAM)&fixture);if(!fixture.hwnd)Sleep(20);}closeWindow=fixture.hwnd;}
    Target closeTarget=targetOf(closeWindow);check(spawned&&closeWindow,"close fixture process starts with a game window");
    check(sendCloseRequests({closeTarget},false)==0&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT,"normal close requires explicit confirmation");auto staleClose=closeTarget;staleClose.born++;check(sendCloseRequests({staleClose},true)==0&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT,"normal close rejects stale process identity");
    check(extraClients({closeTarget,closeTarget}).empty(),"weekly single-client flow preserves one process and ignores duplicate windows");
    auto extras=extraClients({targetOf(panel),closeTarget});check(extras.size()==1&&extras[0].pid==closeTarget.pid,"weekly single-client flow closes only clients after the retained first process");
    int requested=sendCloseRequests({closeTarget,closeTarget},true);HWND exitDialog=nullptr;
    struct ExitSearch {DWORD pid;HWND hwnd;};ExitSearch search{child.dwProcessId,nullptr};
    auto findDialog=[](HWND w,LPARAM data)->BOOL{auto s=(ExitSearch*)data;DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==s->pid&&text(w)==L"退出游戏"){s->hwnd=w;return FALSE;}return TRUE;};
    ULONGLONG dialogUntil=GetTickCount64()+3000;while(!search.hwnd&&GetTickCount64()<dialogUntil){EnumWindows(findDialog,(LPARAM)&search);if(!search.hwnd)Sleep(20);}exitDialog=search.hwnd;
    check(exitDialog&&exitConfirmButton(exitDialog,closeTarget)&&!exitConfirmButton(exitDialog,staleClose),"exit confirmation matches native prompt and live owner identity across processes");
    if(exitDialog)SetWindowTextW(exitDialog,L"其他确认");check(!exitConfirmButton(exitDialog,closeTarget),"unrelated confirmation titles are never accepted");if(exitDialog)SetWindowTextW(exitDialog,L"退出游戏");
    if(exitDialog)SetDlgItemTextW(exitDialog,0xffff,L"其他问题?");check(!exitConfirmButton(exitDialog,closeTarget),"unrelated confirmation body is never accepted");if(exitDialog)SetDlgItemTextW(exitDialog,0xffff,L"确定要离开吗?");
    std::vector<Target> closeRows{closeTarget};confirmedDialogs.clear();EnumWindows(confirmExitDialog,(LPARAM)&closeRows);bool processClosed=spawned&&WaitForSingleObject(child.hProcess,3000)==WAIT_OBJECT_0;
    check(requested==1&&processClosed&&!IsWindow(closeWindow),"confirmed normal close automatically accepts owned exit dialog without process termination");confirmedDialogs.clear();if(spawned){if(!processClosed)TerminateProcess(child.hProcess,1);CloseHandle(child.hThread);CloseHandle(child.hProcess);}
    HWND sized=CreateWindowExW(0,L"STATIC",L"Resize Arrange",WS_OVERLAPPEDWINDOW,0,0,320,200,nullptr,nullptr,instance,nullptr);Target sizedTarget=targetOf(sized);bool repeats=true;
    for(int i=0;i<10;i++){repeats=repeats&&beginArrangement({sizedTarget},monitor,640,360);for(int tick=0;tick<8&&!arrangement.rows.empty();tick++)arrangementTick();RECT c{};GetClientRect(sized,&c);repeats=repeats&&c.right==640&&c.bottom==360&&arrangement.rows.empty();}
    check(repeats,"ten size-and-arrange operations apply the configured size without progressive drift");DestroyWindow(sized);saved.clear();
    updates::selfTest(dir,check);
    audio::selfTest(check);
    // Filter out every real game. Only explicitly tracked synthetic children can be arranged.
    set(Filter,L"__SHUWEIBING_TEST_ONLY__");set(Path,dir+L"\\多开测试 带空格\\Launch.exe");set(Count,L"2");set(Delay,L"500");set(Width,L"640");manualTargets.clear();targets.clear();ULONGLONG started=GetTickCount64();beginLaunch(L"--startup-fixture \""+dir+L"\\one-click.txt\"");
    ULONGLONG until=GetTickCount64()+11000;bool finished=false;
    while(GetTickCount64()<until){while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessageW(&pending);}if(launchRemaining==0&&startupWindows.empty()&&arrangement.rows.empty()){finished=true;break;}Sleep(20);}
    refresh();auto launched=targets;bool exact=finished&&launched.size()==2;
    for(auto& child:launched){RECT c{},r{};GetClientRect(child.hwnd,&c);GetWindowRect(child.hwnd,&r);exact=exact&&valid(child)&&c.right==640&&c.bottom==360&&r.right<=mi.rcWork.right&&r.left>=mi.rcWork.left&&r.top>=mi.rcWork.top&&r.bottom<=mi.rcWork.bottom;}
    check(exact,"one click launches two synthetic logo windows, waits for readiness, then sizes and arranges them");
    check(finished&&GetTickCount64()-started<5500,"ready synthetic windows arrange before the former six-second minimum wait");
    stopStartup();stopArrangement();sendCloseRequests(launched,true);for(auto& child:launched){HANDLE p=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,child.pid);if(p){if(WaitForSingleObject(p,2000)!=WAIT_OBJECT_0)TerminateProcess(p,1);CloseHandle(p);}}manualTargets.clear();
    check(FindResourceW(instance,MAKEINTRESOURCEW(102),RT_RCDATA)&&FindResourceW(instance,MAKEINTRESOURCEW(103),RT_RCDATA),"standalone executable embeds project and bundled library licenses");
    // Hosted runners may lack Explorer; keep the full tray check on desktops.
    if(ciMode&&!FindWindowW(L"Shell_TrayWnd",nullptr))printf("SKIP system tray icon registration: no Explorer shell in CI\n");
    else check(addTray(),"system tray icon registration");
    registerBoss(true,testKey,testMods,false);DestroyWindow(panel);check(!trayReady&&!bossRegistered,"tray and boss key removed on program exit");
    printf("RESULT %d/%d passed\n",count-failed,count);return failed==0;
}
static S geometryReport(const Target& t){
    RECT r{},c{},frame{};GetWindowRect(t.hwnd,&r);GetClientRect(t.hwnd,&c);
    DwmGetWindowAttribute(t.hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame));
    MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(t.hwnd,MONITOR_DEFAULTTONEAREST),&mi);
    auto rect=[](RECT x){return std::to_wstring(x.left)+L","+std::to_wstring(x.top)+L","+std::to_wstring(x.right)+L","+std::to_wstring(x.bottom);};
    auto expected=outerSize(t,c.right,c.bottom);
    using DpiFor=UINT(WINAPI*)(HWND);auto f=(DpiFor)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    using WindowContext=HANDLE(WINAPI*)(HWND);using Awareness=int(WINAPI*)(HANDLE);
    auto getContext=(WindowContext)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowDpiAwarenessContext");
    auto getAwareness=(Awareness)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetAwarenessFromDpiAwarenessContext");
    int awareness=getContext&&getAwareness?getAwareness(getContext(t.hwnd)):-1;
    S path;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,t.pid);
    if(process){wchar_t buffer[32768];DWORD n=32768;if(QueryFullProcessImageNameW(process,0,buffer,&n))path.assign(buffer,n);CloseHandle(process);}
    return L"  路径="+path+L"\r\n  外框="+rect(r)+L" | 客户区="+rect(c)+L" | 可见边框="+rect(frame)+L"\r\n  工作区="+rect(mi.rcWork)+L" | 窗口 DPI="+std::to_wstring(f?f(t.hwnd):96)+L" | DPI 模式="+std::to_wstring(awareness)+L"（0=不感知，1=系统，2=每屏） | 系统估算外框="+std::to_wstring(expected.cx)+L"×"+std::to_wstring(expected.cy)+L"\r\n";
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int show){
    instance=h;int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    int updateResult=updates::applyMode(argc,argv);if(updateResult>=0){LocalFree(argv);return updateResult;}
    if(argc==3&&S(argv[1])==L"--update-probe"){bool ok=updates::probe(argv[2]);LocalFree(argv);return ok?0:1;}
    if(argc==3&&S(argv[1])==L"--audio-probe"){HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);EnumWindows(enumTarget,(LPARAM)&cfg.filter);S report=L"\uFEFF鼠卫兵声音只读检查\r\n";for(auto& t:targets)if(valid(t)){auto result=audio::inspect(t.pid);report+=L"PID="+std::to_wstring(t.pid)+L" · "+t.title+L" · 独立会话="+std::to_wstring(result.matched)+L" · 共享会话="+(result.shared?L"是":L"否")+L"\r\n";}bool ok=atomicWrite(argv[2],report.data(),report.size()*2);if(SUCCEEDED(com))CoUninitialize();LocalFree(argv);return ok?0:1;}
    bool diagnostics=false,discoverReport=false,ciMode=false,skipUpdate=false;int waitParent=0;
    for(int i=1;i<argc;i++){if(S(argv[i])==L"--self-test")testMode=true;if(S(argv[i])==L"--smoke-test")smokeMode=true;if(S(argv[i])==L"--diagnose")diagnostics=true;if(S(argv[i])==L"--discover")discoverReport=true;if(S(argv[i])==L"--wait-parent"&&i+1<argc)number(argv[++i],1,99999999,waitParent);}
    if(discoverReport&&argc>2){S output=L"\uFEFF鼠卫兵自动读取客户端路径与多开入口\r\n";for(auto& path:gameCandidates()){output+=path+L"\r\n";LaunchSpec plan;S error;if(resolveLaunch(path,L"",plan,error))output+=L"  实际启动="+plan.executable+L"\r\n  工作目录="+plan.directory+L"\r\n";else output+=L"  无法追加多开="+error+L"\r\n";}bool ok=atomicWrite(argv[argc-1],output.data(),output.size()*2);LocalFree(argv);return ok?0:1;}
    if(diagnostics&&argc>2){EnumWindows(enumTarget,(LPARAM)&cfg.filter);S report=L"\uFEFF鼠卫兵窗口识别诊断\r\n筛选："+cfg.filter+L"\r\n窗口数量："+std::to_wstring(targets.size())+L"\r\n";for(auto& t:targets)report+=L"PID="+std::to_wstring(t.pid)+L" | "+t.process+L" | "+t.title+L" | 身份有效="+(valid(t)?L"是":L"否")+L"\r\n"+geometryReport(t);bool ok=atomicWrite(argv[argc-1],report.data(),report.size()*sizeof(wchar_t));LocalFree(argv);return ok?0:1;}
    for(int i=1;i<argc;i++)if(S(argv[i])==L"--ci")ciMode=true;
    for(int i=1;i<argc;i++)if(S(argv[i])==L"--skip-update-check")skipUpdate=true;
    if(testMode){S dir=argc>2?argv[argc-1]:L".";CreateDirectoryW(dir.c_str(),nullptr);FILE* file;_wfreopen_s(&file,(dir+L"\\test-results.txt").c_str(),L"w",stdout);bool ok=selfTest(dir,ciMode);fflush(stdout);LocalFree(argv);return ok?0:1;}
    LocalFree(argv);
    if(waitParent){HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,waitParent);if(parent){DWORD result=WaitForSingleObject(parent,10000);CloseHandle(parent);if(result!=WAIT_OBJECT_0){MessageBoxW(nullptr,L"原窗口尚未退出，请退出原鼠卫兵后再以管理员身份运行。",L"鼠卫兵",MB_OK|MB_ICONINFORMATION);return 1;}}}
    elevated=isElevated();
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    // A single manager avoids competing launch queues and lost hidden-window ownership.
    HANDLE mutex=CreateMutexW(nullptr,FALSE,smokeMode?L"Local\\ShuWeiBing.Smoke.02":L"Local\\ShuWeiBing.Manager.01");if(!mutex){MessageBoxW(nullptr,L"无法取得程序运行锁。若已有以管理员身份运行的鼠卫兵，请先退出该程序后再打开。",L"鼠卫兵 · 启动提示",MB_OK|MB_ICONINFORMATION);if(SUCCEEDED(com))CoUninitialize();return 1;}if(GetLastError()==ERROR_ALREADY_EXISTS){HWND old=FindWindowW(L"ShuWeiBing.Window",nullptr);if(old){ShowWindow(old,SW_RESTORE);SetForegroundWindow(old);}CloseHandle(mutex);if(SUCCEEDED(com))CoUninitialize();return 0;}
    wchar_t local[MAX_PATH]{};if(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local)!=S_OK){MessageBoxW(nullptr,L"无法找到用户配置目录。",L"鼠卫兵",MB_OK|MB_ICONERROR);return 1;}
    configDir=S(local)+L"\\ShuWeiBing";CreateDirectoryW(configDir.c_str(),nullptr);configPath=configDir+L"\\settings.dat";
    int source;loadSettings(configPath,cfg,source);if(cfg.path.empty())cfg.path=detectGame();loadHidden();
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_HOTKEY_CLASS|ICC_LINK_CLASS};InitCommonControlsEx(&cc);
    background=CreateSolidBrush(RGB(244,246,250));cardBrush=CreateSolidBrush(RGB(255,255,255));WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=h;wc.lpszClassName=L"ShuWeiBing.Window";wc.lpfnWndProc=proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=background;wc.hIcon=LoadIconW(h,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;RegisterClassExW(&wc);
    auto systemDpi=(UINT(WINAPI*)())GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem");if(systemDpi)dpi=(int)systemDpi();
    RECT initial{0,0,px(580),px(410)};AdjustWindowRectEx(&initial,WS_OVERLAPPEDWINDOW,FALSE,0);
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"鼠卫兵 · 轻量窗口管家",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,initial.right-initial.left,initial.bottom-initial.top,nullptr,nullptr,h,nullptr);
    if(!w)return 1;DWORD corner=2;DwmSetWindowAttribute(w,33,&corner,sizeof(corner));taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");addTray();ShowWindow(w,show);UpdateWindow(w);
    if(!smokeMode&&!skipUpdate&&cfg.autoUpdates)checkUpdates(false);
    if(!hidden.empty())status(L"找到上次隐藏的 "+std::to_wstring(hidden.size())+L" 个窗口；点击“显示”可恢复。");
    else if(source==1)status(L"配置损坏，已恢复有效备份。窗口管理可正常使用。");else if(source==2&&fileExists(configPath))status(L"配置及备份无法读取，已使用默认设置；重新填写后会安全保存。");
    if(!smokeMode&&cfg.bossEnabled&&!registerBoss(true,cfg.bossKey,cfg.bossMods))status(L"老板键暂不可用，请展开设置更换快捷键；其余功能可正常使用。");
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(mutex)CloseHandle(mutex);DeleteObject(bodyFont);DeleteObject(titleFont);DeleteObject(background);DeleteObject(cardBrush);if(SUCCEEDED(com))CoUninitialize();return (int)msg.wParam;
}
