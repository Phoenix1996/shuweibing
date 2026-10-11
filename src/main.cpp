// 鼠卫兵 0.2 — native Win32, no background service or game injection.
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

using S = std::wstring;
struct Settings {
    S path, args, filter=L"onmyoji;YysLauncher";
    int width=1136, height=640, count=2, delay=1200, columns=0, opacity=100;
    bool lockRatio=true;
};
struct Target {
    HWND hwnd{}; DWORD pid{}; uint64_t born{}; S title, process;
};
struct Saved {
    Target target; RECT rect{}; LONG_PTR ex{}; BYTE alpha=255; COLORREF color{};
    DWORD flags=LWA_ALPHA; bool hasAlpha=false, resized=false, changedAlpha=false;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
};
struct Monitor { HMONITOR handle; RECT work; S name; };
struct Cell { int x,y,w,h; };
static HWND mainWindow, listWindow, statusWindow;
static HINSTANCE instance;
static HFONT bodyFont, titleFont;
static HBRUSH background, cardBrush;
static std::map<HWND,bool> hover;
static int dpi=96;
static bool detailsOpen=false, sizingFields=false, trayReady=false;
static int ratioWidth=1136, ratioHeight=640;
static const UINT TrayMessage=WM_APP+1;
static UINT taskbarCreated;
static NOTIFYICONDATAW tray{};
static Settings cfg;
static S configDir, configPath;
static bool loadingList=false, testMode=false, smokeMode=false;
static std::vector<Target> targets, manualTargets;
static std::map<HWND,Saved> saved;
static std::map<HWND,Target> hidden;
static std::vector<Monitor> monitors;
static int launchRemaining=0, launchTotal=0;
static S launchPath, launchArgs;
static std::map<int,HWND> controls;
enum {
    Path=100, Browse, Args, Count, Delay, Launch, Cancel, Filter, Refresh,
    Pick, SelectAll, SelectNone, Width, Height, Resize, Columns, Screen, Arrange,
    Opacity, Alpha, Hide, Show, Restore, Help, List, Header, Subtitle,
    LPath, LArgs, LCount, LDelay, LFilter, LWidth, LHeight, LColumns, LScreen,
    LOpacity, LWindows, Hint, Icon, Details, LockRatio, Background
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
    S s=L"SWB-SETTINGS-1\npath="+clean(c.path)+L"\nargs="+clean(c.args)+L"\nfilter="+clean(c.filter)+L"\n";
    s+=L"width="+std::to_wstring(c.width)+L"\nheight="+std::to_wstring(c.height)+L"\ncount="+std::to_wstring(c.count);
    s+=L"\ndelay="+std::to_wstring(c.delay)+L"\ncolumns="+std::to_wstring(c.columns)+L"\nopacity="+std::to_wstring(c.opacity)+L"\n";
    s+=L"lockRatio="+S(c.lockRatio?L"1":L"0")+L"\n";
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
    if((kv.size()!=9&&kv.size()!=10)||!kv.count(L"path")||!kv.count(L"args")||!kv.count(L"filter")||(kv.size()==10&&!kv.count(L"lockRatio")))return false;
    candidate.path=kv[L"path"];candidate.args=kv[L"args"];candidate.filter=kv[L"filter"];
    if(candidate.path.size()>32000||candidate.args.size()>4096||candidate.filter.empty()||candidate.filter.size()>256)return false;
    if(!number(kv[L"width"],160,7680,candidate.width)||!number(kv[L"height"],120,4320,candidate.height)||!number(kv[L"count"],1,20,candidate.count)||!number(kv[L"delay"],500,10000,candidate.delay)||!number(kv[L"columns"],0,20,candidate.columns)||!number(kv[L"opacity"],20,100,candidate.opacity))return false;
    if(kv.count(L"lockRatio")){int lock;if(!number(kv[L"lockRatio"],0,1,lock))return false;candidate.lockRatio=lock!=0;}
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
    std::vector<DiskHidden> rows;for(auto& [h,t]:hidden)if(valid(t))rows.push_back({(uint64_t)(uintptr_t)h,t.born,t.pid,0});
    std::vector<unsigned char> b(8+rows.size()*sizeof(DiskHidden));if(!rows.empty())memcpy(b.data()+8,rows.data(),b.size()-8);uint64_t check=hash(b.data()+8,b.size()-8);memcpy(b.data(),&check,8);return atomicWrite(configDir+L"\\hidden.dat",b.data(),b.size());
}
static void loadHidden(){
    std::vector<unsigned char> b;if(!readBytes(configDir+L"\\hidden.dat",b)||b.size()<8||(b.size()-8)%sizeof(DiskHidden)||b.size()>8+256*sizeof(DiskHidden))return;
    uint64_t check;memcpy(&check,b.data(),8);if(check!=hash(b.data()+8,b.size()-8))return;
    for(size_t at=8;at<b.size();at+=sizeof(DiskHidden)){DiskHidden d;memcpy(&d,b.data()+at,sizeof(d));Target t;t.hwnd=(HWND)(uintptr_t)d.hwnd;t.pid=d.pid;t.born=d.born;if(valid(t)&&!IsWindowVisible(t.hwnd)){t.title=text(t.hwnd);t.process=processName(t.pid);hidden[t.hwnd]=t;}}
}
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
    Saved s;s.target=t;if(!GetWindowRect(t.hwnd,&s.rect)||!GetWindowPlacement(t.hwnd,&s.placement))return false;
    s.ex=GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);
    if(s.ex&WS_EX_LAYERED)s.hasAlpha=GetLayeredWindowAttributes(t.hwnd,&s.color,&s.alpha,&s.flags)!=0;
    saved[t.hwnd]=s;return true;
}
static S winError(DWORD error){
    if(error==ERROR_ACCESS_DENIED)return L"权限不足：若游戏以管理员运行，请让鼠卫兵使用相同权限。";
    return L"Windows 错误 "+std::to_wstring(error)+L"。";
}
static bool collect(Settings& c,bool notify=true){
    c=cfg;c.path=value(Path);c.args=value(Args);c.filter=value(Filter);
    c.columns=0;c.lockRatio=SendMessageW(controls[LockRatio],BM_GETCHECK,0,0)==BST_CHECKED;
    bool ok=!c.filter.empty()&&c.filter.size()<=256&&c.args.size()<=4096&&number(value(Width),160,7680,c.width)&&number(value(Height),120,4320,c.height)&&number(value(Count),1,20,c.count)&&number(value(Delay),500,10000,c.delay)&&number(value(Opacity),20,100,c.opacity);
    if(!ok&&notify)MessageBoxW(mainWindow,L"请检查输入：宽 160–7680，高 120–4320；启动数量 1–20；间隔 500–10000 毫秒；不透明度 20–100%。筛选词不可为空。",L"鼠卫兵 · 输入检查",MB_OK|MB_ICONINFORMATION);return ok;
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
static int pairedDimension(int n,int from,int to){return (int)(((int64_t)n*to+from/2)/from);}
static void sizeChanged(int id){
    if(sizingFields||!controls.count(LockRatio)||SendMessageW(controls[LockRatio],BM_GETCHECK,0,0)!=BST_CHECKED)return;
    int n;if(!number(value(id),id==Width?160:120,id==Width?7680:4320,n))return;
    int paired=pairedDimension(n,id==Width?ratioWidth:ratioHeight,id==Width?ratioHeight:ratioWidth);
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
    bool ok=SetWindowPos(t.hwnd,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)!=0;if(ok)saved[t.hwnd].resized=true;return ok;
}
static bool transparency(const Target& t,int percent){
    if(!capture(t))return false;auto& s=saved[t.hwnd];LONG_PTR ex=GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);
    // Per-pixel layered windows cannot be safely converted with this API.
    if((s.ex&WS_EX_LAYERED)&&!s.hasAlpha){SetLastError(ERROR_NOT_SUPPORTED);return false;}
    if(percent==100&&!(s.ex&WS_EX_LAYERED)){
        SetLastError(0);LONG_PTR previous=SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,ex&~WS_EX_LAYERED);bool ok=previous!=0||GetLastError()==0;if(ok){s.changedAlpha=true;RedrawWindow(t.hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);}return ok;
    }
    if(!(ex&WS_EX_LAYERED)){SetLastError(0);LONG_PTR previous=SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,ex|WS_EX_LAYERED);if(!previous&&GetLastError())return false;}
    DWORD flags=LWA_ALPHA|(s.hasAlpha?(s.flags&LWA_COLORKEY):0);bool ok=SetLayeredWindowAttributes(t.hwnd,s.color,(BYTE)((percent*255+50)/100),flags)!=0;
    if(ok)s.changedAlpha=true;else if(!(ex&WS_EX_LAYERED))SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,ex);return ok;
}
static void report(const S& action,int ok,int total,DWORD error=0){status(action+L"：成功 "+std::to_wstring(ok)+L" / "+std::to_wstring(total)+(ok<total?L"。"+winError(error):L"。"));}
static void operate(int action){
    refresh();auto rows=selected();if(rows.empty()){status(L"未选中游戏窗口；可展开窗口选择或手动添加。");return;}
    Settings c;if(!collect(c))return;cfg=c;int ok=0;DWORD error=0;
    if(action==Arrange){int i=(int)SendMessageW(controls[Screen],CB_GETCURSEL,0,0);if(i<0||i>=(int)monitors.size()){status(L"请先选择有效屏幕。");return;}std::vector<SIZE> sizes;for(auto& t:rows)sizes.push_back(outerSize(t,c.width,c.height));auto cells=stackRight(sizes,monitors[i].work);if(cells.size()!=rows.size()){status(L"当前尺寸无法全部放入屏幕，请调小尺寸或减少选择。");return;}
        for(size_t k=0;k<rows.size();k++){auto& t=rows[k];if(!capture(t))continue;if(IsIconic(t.hwnd)||IsZoomed(t.hwnd))ShowWindowAsync(t.hwnd,SW_RESTORE);
            bool good=SetWindowPos(t.hwnd,nullptr,cells[k].x,cells[k].y,cells[k].w,cells[k].h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)!=0;if(good){saved[t.hwnd].resized=true;ok++;}else error=GetLastError();}
        report(L"右侧纵向排列（保持设定尺寸）",ok,(int)rows.size(),error);
    }else if(action==Hide){
        auto previous=hidden;for(auto& t:rows)if(IsWindowVisible(t.hwnd))hidden[t.hwnd]=t;
        // Journal identities before hiding so a process crash remains recoverable.
        if(!saveHidden()){hidden=previous;status(L"无法保存隐藏恢复记录，已取消隐藏操作。");return;}
        for(auto& t:rows){if(!IsWindowVisible(t.hwnd)){ok++;continue;}SetLastError(0);ShowWindowAsync(t.hwnd,SW_HIDE);ok++;}report(L"已发送隐藏请求",ok,(int)rows.size());
    }else if(action==Show){
        for(auto& t:rows){ShowWindowAsync(t.hwnd,SW_SHOW);ok++;}report(L"已发送显示请求",ok,(int)rows.size());
        // Keep journal until visibility is confirmed or process exits.
    }else if(action==Restore){
        for(auto& t:rows){auto it=saved.find(t.hwnd);if(it==saved.end()||!valid(it->second.target))continue;auto& s=it->second;bool good=true;
            if(s.resized){good=SetWindowPlacement(t.hwnd,&s.placement)!=0;if(good)s.resized=false;}
            if(s.changedAlpha){LONG_PTR ex=GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);SetLastError(0);LONG_PTR prev=SetWindowLongPtrW(t.hwnd,GWL_EXSTYLE,(ex&~WS_EX_LAYERED)|(s.ex&WS_EX_LAYERED));bool alphaGood=prev!=0||GetLastError()==0;if(alphaGood&&s.hasAlpha)alphaGood=SetLayeredWindowAttributes(t.hwnd,s.color,s.alpha,s.flags)!=0;if(alphaGood)s.changedAlpha=false;good=good&&alphaGood;}
            if(good)ok++;else error=GetLastError();}
        report(L"恢复本次会话修改前的布局与透明度",ok,(int)rows.size(),error);
    }else {
        for(auto& t:rows){SetLastError(0);bool good=action==Resize?clientResize(t,c.width,c.height):transparency(t,c.opacity);if(good)ok++;else error=GetLastError();}report(action==Resize?L"已发送客户区尺寸调整请求":L"不透明度调整",ok,(int)rows.size(),error);
    }
    persist();SetTimer(mainWindow,3,350,nullptr);
}
static void launchOne(){
    if(launchRemaining<=0)return;
    S command=L"\""+launchPath+L"\""+(launchArgs.empty()?L"":L" "+launchArgs);std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};S cwd=folderOf(launchPath);
    if(!CreateProcessW(launchPath.c_str(),buffer.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.empty()?nullptr:cwd.c_str(),&si,&pi)){
        DWORD e=GetLastError();launchRemaining=0;KillTimer(mainWindow,1);EnableWindow(controls[Launch],TRUE);EnableWindow(controls[Cancel],FALSE);status(L"启动失败。"+winError(e)+(e==ERROR_ELEVATION_REQUIRED?L"该启动器要求管理员权限，可手动打开后再管理窗口。":L""));return;
    }
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);launchRemaining--;status(L"已启动 "+std::to_wstring(launchTotal-launchRemaining)+L" / "+std::to_wstring(launchTotal)+L"。实际多开取决于游戏客户端是否允许多个实例。");
    if(launchRemaining==0){KillTimer(mainWindow,1);EnableWindow(controls[Launch],TRUE);EnableWindow(controls[Cancel],FALSE);SetTimer(mainWindow,3,1500,nullptr);}
}
static void beginLaunch(){
    Settings c;if(!collect(c))return;DWORD attr=GetFileAttributesW(c.path.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_DIRECTORY)||lower(c.path).size()<4||lower(c.path).substr(c.path.size()-4)!=L".exe"){status(L"请选择存在的游戏客户端或启动器 .exe 文件。");return;}
    cfg=c;persist();launchPath=c.path;launchArgs=c.args;launchRemaining=launchTotal=c.count;EnableWindow(controls[Launch],FALSE);EnableWindow(controls[Cancel],TRUE);SetTimer(mainWindow,1,c.delay,nullptr);launchOne();
}
static void browse(){wchar_t path[32768]{};OPENFILENAMEW o{};o.lStructSize=sizeof(o);o.hwndOwner=mainWindow;o.lpstrFilter=L"游戏或启动器 (*.exe)\0*.exe\0\0";o.lpstrFile=path;o.nMaxFile=32768;o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&o)){set(Path,path);persist();}}
static S detectGame(){
    if(fileExists(L"K:\\Netease\\yys\\launch.exe"))return L"K:\\Netease\\yys\\launch.exe";
    for(auto root:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        HKEY h;if(RegOpenKeyExW(root,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&h)!=ERROR_SUCCESS)continue;
        for(DWORD i=0;;i++){wchar_t name[512];DWORD n=512;if(RegEnumKeyExW(h,i,name,&n,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS)break;HKEY sub;
            if(RegOpenKeyExW(h,name,0,KEY_READ|view,&sub)!=ERROR_SUCCESS)continue;
            wchar_t display[2048]{},location[32768]{};DWORD bytes=sizeof(display);RegQueryValueExW(sub,L"DisplayName",nullptr,nullptr,(BYTE*)display,&bytes);
            if(S(display).find(L"阴阳师")!=S::npos){bytes=sizeof(location);if(RegQueryValueExW(sub,L"InstallLocation",nullptr,nullptr,(BYTE*)location,&bytes)==ERROR_SUCCESS){for(auto exe:{L"\\Launch.exe",L"\\YysLauncher.exe",L"\\onmyoji.exe"}){S path=S(location)+exe;if(fileExists(path)){RegCloseKey(sub);RegCloseKey(h);return path;}}}}
            RegCloseKey(sub);
        }RegCloseKey(h);
    }return L"";
}
static HWND control(int id,const wchar_t* cls,const S& label,DWORD style,DWORD ex=0){
    HWND w=CreateWindowExW(ex,cls,label.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,mainWindow,(HMENU)(INT_PTR)id,instance,nullptr);controls[id]=w;SendMessageW(w,WM_SETFONT,(WPARAM)bodyFont,TRUE);return w;
}
static void label(int id,const S& s){control(id,L"STATIC",s,SS_LEFT);}
static void edit(int id,const S& s,bool numeric=false){auto w=control(id,L"EDIT",s,WS_TABSTOP|ES_AUTOHSCROLL|(numeric?ES_NUMBER:0),WS_EX_CLIENTEDGE);SendMessageW(w,EM_SETLIMITTEXT,numeric?8:(id==Args?4096:(id==Filter?256:32000)),0);}
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
    place(LPath,24,84,54,20);place(Path,82,78,W-174,28);place(Browse,W-84,78,60,28);
    place(LCount,24,124,54,20);place(Count,82,118,46,28);place(LDelay,140,124,82,20);place(Delay,224,118,62,28);place(Launch,W-210,116,118,32);place(Cancel,W-84,116,60,32);
    place(LWindows,24,184,space-100,24);place(Refresh,W-88,178,64,28);
    place(LWidth,24,223,28,20);place(Width,56,217,72,28);place(LHeight,139,223,24,20);place(Height,169,217,72,28);place(LockRatio,254,222,108,24);place(Resize,W-150,216,126,32);
    place(LScreen,24,265,32,20);place(Screen,60,258,W-294,200);place(Arrange,W-218,256,194,34);
    place(LOpacity,24,309,88,20);place(Opacity,116,301,57,28);place(Alpha,182,299,76,32);place(Hide,W-281,299,63,32);place(Show,W-210,299,63,32);place(Restore,W-139,299,115,32);
    for(int id:{LArgs,Args,LFilter,Filter,Pick,SelectAll,SelectNone,List})ShowWindow(controls[id],detailsOpen?SW_SHOW:SW_HIDE);
    if(detailsOpen){
        place(LFilter,24,368,56,20);place(Filter,84,362,W-260,28);place(Pick,W-168,362,144,28);
        place(LArgs,24,407,56,20);place(Args,84,401,W-108,28);
        place(SelectAll,W-159,436,63,26);place(SelectNone,W-88,436,64,26);place(List,24,468,space,std::max(60,H-548));
    }
    place(Hint,24,H-55,space,20);MoveWindow(statusWindow,px(24),r.bottom-px(31),r.right-px(48),px(20),TRUE);
    if(listWindow){int widths[]={space-280,110,70,100};for(int i=0;i<4;i++)ListView_SetColumnWidth(listWindow,i,px(widths[i]));}
}
static void panelSize(){RECT r{0,0,px(580),px(detailsOpen?650:410)};AdjustWindowRectEx(&r,(DWORD)GetWindowLongPtrW(mainWindow,GWL_STYLE),FALSE,0);SetWindowPos(mainWindow,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);layout();InvalidateRect(mainWindow,nullptr,TRUE);}
static void createUi(){
    sizingFields=true;ratioWidth=cfg.width;ratioHeight=cfg.height;
    fonts();auto icon=control(Icon,L"STATIC",L"",SS_ICON|SS_CENTERIMAGE);SendMessageW(icon,STM_SETICON,(WPARAM)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,px(46),px(46),LR_DEFAULTCOLOR),0);label(Header,L"鼠卫兵");SendMessageW(controls[Header],WM_SETFONT,(WPARAM)titleFont,TRUE);label(Subtitle,L"阴阳师窗口管家 · 0.2");button(Background,L"后台");button(Details,L"窗口 / 设置");button(Help,L"说明");
    label(LPath,L"游戏");edit(Path,cfg.path);button(Browse,L"选择");label(LArgs,L"参数");edit(Args,cfg.args);
    label(LCount,L"数量");edit(Count,std::to_wstring(cfg.count),true);label(LDelay,L"间隔 / ms");edit(Delay,std::to_wstring(cfg.delay),true);button(Launch,L"启动 / 多开");button(Cancel,L"停止");EnableWindow(controls[Cancel],FALSE);
    label(LFilter,L"进程");edit(Filter,cfg.filter);button(Refresh,L"刷新");button(Pick,L"添加窗口（3 秒）");
    label(LWindows,L"游戏窗口");button(SelectAll,L"全选");button(SelectNone,L"全不选");
    listWindow=control(List,WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,WS_EX_CLIENTEDGE);
    SetWindowTheme(listWindow,L"Explorer",nullptr);
    ListView_SetExtendedListViewStyle(listWindow,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    const wchar_t* names[]={L"窗口标题",L"进程",L"状态",L"尺寸"};for(int i=0;i<4;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)names[i];c.cx=px(120);ListView_InsertColumn(listWindow,i,&c);}
    label(LWidth,L"宽");edit(Width,std::to_wstring(cfg.width),true);label(LHeight,L"高");edit(Height,std::to_wstring(cfg.height),true);control(LockRatio,L"BUTTON",L"锁定比例",WS_TABSTOP|BS_AUTOCHECKBOX);SendMessageW(controls[LockRatio],BM_SETCHECK,cfg.lockRatio?BST_CHECKED:BST_UNCHECKED,0);button(Resize,L"应用尺寸");
    label(LScreen,L"屏幕");control(Screen,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
    button(Arrange,L"自动排列 · 右侧纵向");label(LOpacity,L"不透明度 %");edit(Opacity,std::to_wstring(cfg.opacity),true);button(Alpha,L"应用");button(Hide,L"隐藏");button(Show,L"显示");button(Restore,L"恢复布局");
    label(Hint,L"右侧向下，排满向左 · 排列保持设定尺寸");
    statusWindow=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_ENDELLIPSIS,0,0,0,0,mainWindow,nullptr,instance,nullptr);SendMessageW(statusWindow,WM_SETFONT,(WPARAM)bodyFont,TRUE);
    sizingFields=false;screens();layout();refresh();status(L"就绪 · 默认选择全部游戏窗口");
}
static void restoreHidden(){for(auto& [h,t]:hidden)if(valid(t))ShowWindowAsync(h,SW_SHOW);}
static bool addTray(){
    if(smokeMode)return false;
    tray.cbSize=sizeof(tray);tray.hWnd=mainWindow;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;tray.uCallbackMessage=TrayMessage;tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wcscpy_s(tray.szTip,L"鼠卫兵 · 双击打开，右键退出");
    trayReady=Shell_NotifyIconW(NIM_ADD,&tray)!=0;if(trayReady){tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}return trayReady;
}
static void openPanel(){ShowWindow(mainWindow,SW_RESTORE);SetForegroundWindow(mainWindow);}
static void trayMenu(){POINT p;GetCursorPos(&p);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,2001,L"打开鼠卫兵");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,2002,L"退出程序");SetForegroundWindow(mainWindow);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,mainWindow,nullptr);DestroyMenu(menu);PostMessageW(mainWindow,WM_NULL,0,0);if(choice==2001)openPanel();else if(choice==2002)SendMessageW(mainWindow,WM_CLOSE,0,0);}
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(taskbarCreated&&msg==taskbarCreated){addTray();return 0;}
    switch(msg){
    case WM_CREATE:{mainWindow=w;using Dpi=UINT(WINAPI*)(HWND);auto f=(Dpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");if(f)dpi=(int)f(w);createUi();if(smokeMode)SetTimer(w,9,4500,nullptr);return 0;}
    case WM_SIZE:if(wp==SIZE_MINIMIZED&&trayReady){ShowWindow(w,SW_HIDE);return 0;}if(statusWindow&&wp!=SIZE_MINIMIZED){layout();InvalidateRect(w,nullptr,TRUE);}return 0;
    case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)lp;RECT r{0,0,px(580),px(detailsOpen?650:410)};AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0);p->ptMinTrackSize={r.right-r.left,r.bottom-r.top};return 0;}
    case WM_DPICHANGED:{dpi=HIWORD(wp);fonts();auto r=(RECT*)lp;SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
    case WM_DISPLAYCHANGE:screens();return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);FillRect(dc,&r,background);auto old=SelectObject(dc,cardBrush);auto pen=CreatePen(PS_SOLID,1,RGB(226,232,239));auto oldPen=SelectObject(dc,pen);RoundRect(dc,px(12),px(68),r.right-px(12),px(160),px(14),px(14));RoundRect(dc,px(12),px(170),r.right-px(12),px(342),px(14),px(14));if(detailsOpen)RoundRect(dc,px(12),px(350),r.right-px(12),r.bottom-px(70),px(14),px(14));SelectObject(dc,old);SelectObject(dc,oldPen);DeleteObject(pen);EndPaint(w,&p);return 0;}
    case WM_DRAWITEM:drawButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(HWND)lp==controls[Header]?RGB(35,58,100):RGB(64,76,93));bool head=(HWND)lp==controls[Header]||(HWND)lp==controls[Subtitle]||(HWND)lp==controls[Icon]||(HWND)lp==controls[Hint]||(HWND)lp==statusWindow;return (LRESULT)(head?background:cardBrush);}
    case WM_NOTIFY:if(((NMHDR*)lp)->hwndFrom==listWindow&&((NMHDR*)lp)->code==LVN_ITEMCHANGED&&!loadingList)summary();return 0;
    case TrayMessage:if(LOWORD(lp)==WM_LBUTTONDBLCLK||LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT)openPanel();else if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==WM_RBUTTONUP)trayMenu();return 0;
    case WM_COMMAND:if(HIWORD(wp)==EN_CHANGE&&(LOWORD(wp)==Width||LOWORD(wp)==Height)){sizeChanged(LOWORD(wp));return 0;}if(HIWORD(wp)==BN_CLICKED)switch(LOWORD(wp)){
        case Browse:browse();break;case Refresh:refresh();persist();status(L"游戏窗口已刷新。");break;
        case Details:detailsOpen=!detailsOpen;set(Details,detailsOpen?L"收起设置":L"窗口 / 设置");panelSize();break;
        case Background:if(trayReady||addTray())ShowWindow(w,SW_MINIMIZE);else status(L"托盘暂时不可用，可用任务栏最小化。");break;
        case LockRatio:if(SendMessageW(controls[LockRatio],BM_GETCHECK,0,0)==BST_CHECKED){int a,b;if(number(value(Width),160,7680,a)&&number(value(Height),120,4320,b)){ratioWidth=a;ratioHeight=b;}}break;
        case SelectAll:case SelectNone:for(int i=0;i<ListView_GetItemCount(listWindow);i++)ListView_SetCheckState(listWindow,i,LOWORD(wp)==SelectAll);break;
        case Launch:beginLaunch();break;case Cancel:KillTimer(w,1);launchRemaining=0;EnableWindow(controls[Launch],TRUE);EnableWindow(controls[Cancel],FALSE);status(L"启动队列已停止；已启动的游戏继续运行。");break;
        case Pick:EnableWindow(controls[Pick],FALSE);SetTimer(w,2,3000,nullptr);status(L"请在 3 秒内切换到要管理的游戏窗口。");break;
        case Resize:case Arrange:case Alpha:case Hide:case Show:case Restore:operate(LOWORD(wp));break;
        case Help:MessageBoxW(w,L"默认选择全部游戏窗口，直接使用常用操作即可。\n“窗口 / 设置”可展开窗口选择、进程筛选与启动参数。\n\n锁定比例：修改宽或高，另一个数值自动跟随；取消锁定后可自定义比例，再勾选锁定。\n自动排列：从屏幕右上角向下排，纵向放不下时向左增加一列。保持填写的尺寸，不自动放大或缩小；整屏放不下时会提示调小尺寸。\n\n最小化或“后台”进入托盘，双击托盘图标或右键“打开”恢复。右键“退出”或窗口关闭按钮真正退出，显示本程序隐藏的游戏窗口。\n\n“恢复布局”恢复本次会话首次修改前的布局与透明度。多开仍受客户端实例限制；建议游戏使用窗口模式。",L"鼠卫兵 · 使用说明",MB_OK|MB_ICONINFORMATION);break;
        }return 0;
    case WM_TIMER:switch(wp){
        case 1:launchOne();break;
        case 2:{KillTimer(w,2);EnableWindow(controls[Pick],TRUE);HWND fg=GetForegroundWindow();Target t=targetOf(fg);if(fg&&fg!=w&&t.pid!=GetCurrentProcessId()&&valid(t)&&!t.title.empty()){manualTargets.push_back(t);refresh();status(L"已添加窗口："+t.title);}else status(L"没有选到有效窗口，请重试并切换到游戏。");break;}
        case 3:KillTimer(w,3);for(auto i=hidden.begin();i!=hidden.end();){if(!valid(i->second)||IsWindowVisible(i->first))i=hidden.erase(i);else ++i;}saveHidden();refresh();break;
        case 9:KillTimer(w,9);DestroyWindow(w);break;
        }return 0;
    case WM_CLOSE:persist();restoreHidden();DestroyWindow(w);return 0;
    case WM_DESTROY:if(trayReady)Shell_NotifyIconW(NIM_DELETE,&tray);trayReady=false;PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,msg,wp,lp);
}
static bool selfTest(const S& dir){
    CreateDirectoryW(dir.c_str(),nullptr);int failed=0,count=0;auto check=[&](bool ok,const char* name){count++;printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)failed++;};
    Settings a;a.path=L"C:\\游戏目录\\Launch.exe";a.args=L"--name \"中文账户\"";auto b=encode(a);Settings c;
    check(decode(b,c)&&c.path==a.path&&c.args==a.args&&c.width==1136,"unicode settings round trip");
    auto damaged=b;damaged.back()^=0x55;check(!decode(damaged,c),"checksum rejects corruption");
    check(!decode(std::vector<unsigned char>(182,0),c),"zero-filled settings rejected");
    check(!decode(std::vector<unsigned char>(1,0),c),"truncated settings rejected");
    auto bad=a;bad.opacity=0;check(!decode(encode(bad),c),"out-of-range settings rejected");
    S p=dir+L"\\test-settings.dat";DeleteFileW(p.c_str());DeleteFileW((p+L".bak").c_str());
    check(saveSettings(p,a),"atomic first save");a.width=1200;check(saveSettings(p,a),"atomic subsequent save and backup");int source;
    check(loadSettings(p,c,source)&&source==0&&c.width==1200,"load current settings");
    unsigned char zero[182]{};atomicWrite(p,zero,sizeof(zero));check(loadSettings(p,c,source)&&source==1&&c.width==1136,"corruption recovers valid backup");
    atomicWrite(p+L".bak",zero,sizeof(zero));check(!loadSettings(p,c,source)&&source==2,"both corrupt returns defaults path");
    bool fits=true;for(int n=1;n<=20;n++){RECT work{-1920,-200,0,880};auto cells=stackRight(std::vector<SIZE>(n,{240,160}),work);if(cells.size()!=(size_t)n){fits=false;break;}for(auto cell:cells)if(cell.x<work.left||cell.y<work.top||cell.x+cell.w>work.right||cell.y+cell.h>work.bottom||cell.w!=240||cell.h!=160)fits=false;}
    check(fits,"right stack fits 1-20 windows without resizing, negative origin");
    auto vertical=stackRight(std::vector<SIZE>(5,{320,240}),{0,0,1920,1080});
    check(vertical.size()==5&&vertical[0].x==1600&&vertical[0].y==0&&vertical[1].x==1600&&vertical[1].y==248&&vertical[3].x==1600&&vertical[4].x==1272&&vertical[4].y==0,"fills right column top-down before extending left");
    auto edge=stackRight(std::vector<SIZE>(3,{320,240}),{0,0,1000,488});check(edge.size()==3&&edge[1].y==248&&edge[2].y==0&&edge[2].x<edge[1].x,"exact vertical fit includes gap");
    check(stackRight(std::vector<SIZE>(2,{320,240}),{0,0,400,400}).empty(),"screen overflow rejected without shrinking");
    auto mixed=stackRight({{300,240},{320,240},{280,300}},{-1000,-200,0,300});check(mixed.size()==3&&mixed[0].x+mixed[0].w==0&&mixed[1].x+mixed[1].w==0&&mixed[2].x+mixed[2].w==-328,"different border widths right-aligned and separate columns");
    check(pairedDimension(568,1136,640)==320&&pairedDimension(320,640,1136)==568,"aspect ratio counterpart rounding");
    int n=0;check(!number(L"99999999",1,20,n)&&!number(L"-1",1,20,n)&&number(L"20",1,20,n),"numeric boundaries");
    check(matches(L"阴阳师-网易游戏",L"onmyoji.exe",L"onmyoji;YysLauncher")&&!matches(L"yys 阴阳师攻略",L"msedge.exe",L"onmyoji;YysLauncher"),"process filters exclude browser title matches");
    // Integration uses only our own synthetic window, never the user's programs.
    HWND fake=CreateWindowExW(0,L"STATIC",L"SWB Self Test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,instance,nullptr);Target t=targetOf(fake);
    check(valid(t),"window identity includes process birth time");auto wrong=t;wrong.born++;check(!valid(wrong),"stale identity rejected");
    check(transparency(t,70)&&transparency(t,100),"native transparency applied and layer removed");
    check(clientResize(t,640,360),"native resize request");RECT actual{};GetClientRect(fake,&actual);check(actual.right==640&&actual.bottom==360,"client dimensions are exact");
    configDir=dir;hidden[t.hwnd]=t;check(saveHidden(),"hidden identity journal saved");ShowWindow(fake,SW_HIDE);hidden.clear();loadHidden();check(hidden.size()==1&&valid(hidden.begin()->second),"hidden window recovered from journal");restoreHidden();
    MSG pending{};while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessageW(&pending);}check(IsWindowVisible(fake)!=0,"owned hidden window restored");
    DestroyWindow(fake);check(!valid(t),"destroyed window rejected");saved.clear();hidden.clear();
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);background=CreateSolidBrush(RGB(244,246,250));cardBrush=CreateSolidBrush(RGB(255,255,255));
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"SWB.TestPanel";wc.lpfnWndProc=proc;RegisterClassW(&wc);
    HWND panel=CreateWindowExW(0,wc.lpszClassName,L"SWB Test Panel",WS_OVERLAPPEDWINDOW,0,0,600,450,nullptr,nullptr,instance,nullptr);panelSize();RECT client{};GetClientRect(panel,&client);
    check(panel&&client.right==px(580)&&client.bottom==px(410)&&!(GetWindowLongPtrW(listWindow,GWL_STYLE)&WS_VISIBLE),"compact panel size and collapsed list");
    RECT footer{};GetWindowRect(statusWindow,&footer);MapWindowPoints(nullptr,panel,(POINT*)&footer,2);check(footer.left==px(24)&&footer.right==client.right-px(24)&&footer.bottom<=client.bottom-px(10),"footer horizontal and bottom padding");
    set(Width,L"568");check(value(Height)==L"320","editing width follows locked aspect ratio");set(Height,L"160");check(value(Width)==L"284","editing height follows locked aspect ratio");
    SendMessageW(controls[LockRatio],BM_SETCHECK,BST_UNCHECKED,0);set(Width,L"500");check(value(Height)==L"160","unlocked dimensions can be edited independently");
    SendMessageW(controls[LockRatio],BM_SETCHECK,BST_CHECKED,0);set(Width,L"7680");Settings oversized;check(value(Height)==std::to_wstring(pairedDimension(7680,1136,640))&&!collect(oversized,false),"locked ratio exceeding supported bounds is rejected");set(Width,L"568");
    detailsOpen=true;panelSize();check((GetWindowLongPtrW(listWindow,GWL_STYLE)&WS_VISIBLE)!=0,"optional window selection expands");detailsOpen=false;panelSize();check(!(GetWindowLongPtrW(listWindow,GWL_STYLE)&WS_VISIBLE),"window list collapses again");check(addTray(),"system tray icon registration");DestroyWindow(panel);check(!trayReady,"tray icon removed on program exit");
    printf("RESULT %d/%d passed\n",count-failed,count);return failed==0;
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int show){
    instance=h;int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool diagnostics=false;
    for(int i=1;i<argc;i++){if(S(argv[i])==L"--self-test")testMode=true;if(S(argv[i])==L"--smoke-test")smokeMode=true;if(S(argv[i])==L"--diagnose")diagnostics=true;}
    if(diagnostics&&argc>2){EnumWindows(enumTarget,(LPARAM)&cfg.filter);S report=L"\uFEFF鼠卫兵窗口识别诊断\r\n筛选："+cfg.filter+L"\r\n窗口数量："+std::to_wstring(targets.size())+L"\r\n";for(auto& t:targets)report+=L"PID="+std::to_wstring(t.pid)+L" | "+t.process+L" | "+t.title+L" | 身份有效="+(valid(t)?L"是":L"否")+L"\r\n";bool ok=atomicWrite(argv[argc-1],report.data(),report.size()*sizeof(wchar_t));LocalFree(argv);return ok?0:1;}
    if(testMode){S dir=argc>2?argv[argc-1]:L".";CreateDirectoryW(dir.c_str(),nullptr);FILE* file;_wfreopen_s(&file,(dir+L"\\test-results.txt").c_str(),L"w",stdout);bool ok=selfTest(dir);fflush(stdout);LocalFree(argv);return ok?0:1;}
    LocalFree(argv);
    // A single manager avoids competing launch queues and lost hidden-window ownership.
    HANDLE mutex=CreateMutexW(nullptr,FALSE,smokeMode?L"Local\\ShuWeiBing.Smoke.02":L"Local\\ShuWeiBing.Manager.01");if(mutex&&GetLastError()==ERROR_ALREADY_EXISTS){HWND old=FindWindowW(L"ShuWeiBing.Window",nullptr);if(old){ShowWindow(old,SW_RESTORE);SetForegroundWindow(old);}CloseHandle(mutex);return 0;}
    wchar_t local[MAX_PATH]{};if(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local)!=S_OK){MessageBoxW(nullptr,L"无法找到用户配置目录。",L"鼠卫兵",MB_OK|MB_ICONERROR);return 1;}
    configDir=S(local)+L"\\ShuWeiBing";CreateDirectoryW(configDir.c_str(),nullptr);configPath=configDir+L"\\settings.dat";
    int source;loadSettings(configPath,cfg,source);if(cfg.path.empty())cfg.path=detectGame();loadHidden();
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    background=CreateSolidBrush(RGB(244,246,250));cardBrush=CreateSolidBrush(RGB(255,255,255));WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=h;wc.lpszClassName=L"ShuWeiBing.Window";wc.lpfnWndProc=proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=background;wc.hIcon=LoadIconW(h,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;RegisterClassExW(&wc);
    auto systemDpi=(UINT(WINAPI*)())GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem");if(systemDpi)dpi=(int)systemDpi();
    RECT initial{0,0,px(580),px(410)};AdjustWindowRectEx(&initial,WS_OVERLAPPEDWINDOW,FALSE,0);
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"鼠卫兵 · 轻量窗口管家",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,initial.right-initial.left,initial.bottom-initial.top,nullptr,nullptr,h,nullptr);
    if(!w)return 1;DWORD corner=2;DwmSetWindowAttribute(w,33,&corner,sizeof(corner));taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");addTray();ShowWindow(w,show);UpdateWindow(w);
    if(!hidden.empty())status(L"找到上次隐藏的 "+std::to_wstring(hidden.size())+L" 个窗口；点击“显示”可恢复。");
    else if(source==1)status(L"配置损坏，已恢复有效备份。窗口管理可正常使用。");else if(source==2&&fileExists(configPath))status(L"配置及备份无法读取，已使用默认设置；重新填写后会安全保存。");
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(mutex)CloseHandle(mutex);DeleteObject(bodyFont);DeleteObject(titleFont);DeleteObject(background);DeleteObject(cardBrush);return (int)msg.wParam;
}
