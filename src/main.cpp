// 鼠卫兵 0.1 — native Win32, no background service or game injection.
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
    LOpacity, LWindows, Hint, Icon
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
    if(kv.size()!=9||!kv.count(L"path")||!kv.count(L"args")||!kv.count(L"filter"))return false;
    candidate.path=kv[L"path"];candidate.args=kv[L"args"];candidate.filter=kv[L"filter"];
    if(candidate.path.size()>32000||candidate.args.size()>4096||candidate.filter.empty()||candidate.filter.size()>256)return false;
    if(!number(kv[L"width"],160,7680,candidate.width)||!number(kv[L"height"],120,4320,candidate.height)||!number(kv[L"count"],1,20,candidate.count)||!number(kv[L"delay"],500,10000,candidate.delay)||!number(kv[L"columns"],0,20,candidate.columns)||!number(kv[L"opacity"],20,100,candidate.opacity))return false;
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
static void refresh(){
    std::set<HWND> old;for(int i=0;i<ListView_GetItemCount(listWindow);i++)if(ListView_GetCheckState(listWindow,i)){LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=i;ListView_GetItem(listWindow,&item);old.insert((HWND)item.lParam);}
    std::set<HWND> known;for(auto& t:targets)known.insert(t.hwnd);
    S filter=value(Filter);targets.clear();EnumWindows(enumTarget,(LPARAM)&filter);
    auto add=[](const Target& t){if(!valid(t))return;for(auto& x:targets)if(x.hwnd==t.hwnd)return;targets.push_back(t);};
    manualTargets.erase(std::remove_if(manualTargets.begin(),manualTargets.end(),[](const Target& t){return !valid(t);}),manualTargets.end());
    for(auto& t:manualTargets)add(t);for(auto& [h,t]:hidden)add(t);
    loadingList=true;SendMessageW(listWindow,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(listWindow);
    int i=0;for(auto& t:targets){LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=t.title.data();item.lParam=(LPARAM)t.hwnd;ListView_InsertItem(listWindow,&item);
        ListView_SetItemText(listWindow,i,1,t.process.data());S state=IsWindowVisible(t.hwnd)?(IsIconic(t.hwnd)?L"已最小化":L"显示中"):L"已隐藏";ListView_SetItemText(listWindow,i,2,state.data());
        RECT r{};GetClientRect(t.hwnd,&r);S size=std::to_wstring(r.right)+L" × "+std::to_wstring(r.bottom);ListView_SetItemText(listWindow,i,3,size.data());
        S pid=std::to_wstring(t.pid);ListView_SetItemText(listWindow,i,4,pid.data());ListView_SetCheckState(listWindow,i,old.count(t.hwnd)||!known.count(t.hwnd));i++;}
    SendMessageW(listWindow,WM_SETREDRAW,TRUE,0);InvalidateRect(listWindow,nullptr,TRUE);loadingList=false;
    set(LWindows,L"窗口列表 · "+std::to_wstring(targets.size())+L" 个（勾选后操作）");
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
    bool ok=!c.filter.empty()&&c.filter.size()<=256&&c.args.size()<=4096&&number(value(Width),160,7680,c.width)&&number(value(Height),120,4320,c.height)&&number(value(Count),1,20,c.count)&&number(value(Delay),500,10000,c.delay)&&number(value(Columns),0,20,c.columns)&&number(value(Opacity),20,100,c.opacity);
    if(!ok&&notify)MessageBoxW(mainWindow,L"请检查输入：宽 160–7680，高 120–4320；启动数量 1–20；间隔 500–10000 毫秒；列数 0–20；不透明度 20–100%。筛选词不可为空。",L"鼠卫兵 · 输入检查",MB_OK|MB_ICONINFORMATION);return ok;
}
static void persist(){Settings c;if(collect(c,false)){cfg=c;if(!saveSettings(configPath,cfg))status(L"配置保存失败；本次窗口操作仍可使用。请检查磁盘空间和用户目录权限。");}}
static BOOL CALLBACK enumMonitor(HMONITOR h,HDC,LPRECT,LPARAM){MONITORINFOEXW m{};m.cbSize=sizeof(m);if(GetMonitorInfoW(h,&m)){S name=L"屏幕 "+std::to_wstring(monitors.size()+1)+L" · "+std::to_wstring(m.rcWork.right-m.rcWork.left)+L"×"+std::to_wstring(m.rcWork.bottom-m.rcWork.top);if(m.dwFlags&MONITORINFOF_PRIMARY)name+=L"（主屏）";monitors.push_back({h,m.rcWork,name});}return TRUE;}
static void screens(){monitors.clear();EnumDisplayMonitors(nullptr,nullptr,enumMonitor,0);SendMessageW(controls[Screen],CB_RESETCONTENT,0,0);int select=0;auto current=MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST);for(size_t i=0;i<monitors.size();i++){SendMessageW(controls[Screen],CB_ADDSTRING,0,(LPARAM)monitors[i].name.c_str());if(monitors[i].handle==current)select=(int)i;}SendMessageW(controls[Screen],CB_SETCURSEL,select,0);}
static std::vector<Cell> grid(int n,RECT work,int cols,int width,int height){
    std::vector<Cell> cells;if(n<=0||n>256)return cells;
    int aw=work.right-work.left,ah=work.bottom-work.top,gap=8;if(aw<160||ah<120)return cells;
    if(cols==0){double best=-1;for(int c=1;c<=n;c++){int r=(n+c-1)/c;double cw=(aw-gap*(c-1))/(double)c,ch=(ah-gap*(r-1))/(double)r;double scale=std::min(cw/width,ch/height);if(scale>best){best=scale;cols=c;}}}
    cols=std::min(cols,n);if(cols<=0)return cells;int rows=(n+cols-1)/cols;
    int cw=(aw-gap*(cols-1))/cols,ch=(ah-gap*(rows-1))/rows;if(cw<160||ch<120)return cells;
    // Outer bounds fit the work area; exact client aspect is handled by fitClient.
    for(int i=0;i<n;i++)cells.push_back({work.left+(i%cols)*(cw+gap),work.top+(i/cols)*(ch+gap),cw,ch});return cells;
}
static SIZE fitClient(const Target& t,int outerW,int outerH,int desiredW,int desiredH){
    RECT client{},outer{};GetClientRect(t.hwnd,&client);GetWindowRect(t.hwnd,&outer);
    int borderW=std::max<LONG>(0,(outer.right-outer.left)-(client.right-client.left));int borderH=std::max<LONG>(0,(outer.bottom-outer.top)-(client.bottom-client.top));
    int availableW=outerW-borderW,availableH=outerH-borderH;if(availableW<1||availableH<1)return {0,0};
    double scale=std::min(availableW/(double)desiredW,availableH/(double)desiredH);
    return {(LONG)(desiredW*scale)+borderW,(LONG)(desiredH*scale)+borderH};
}
static bool clientResize(const Target& t,int width,int height){
    if(!capture(t))return false;SetLastError(0);
    RECT r{0,0,width,height};DWORD style=(DWORD)GetWindowLongPtrW(t.hwnd,GWL_STYLE),ex=(DWORD)GetWindowLongPtrW(t.hwnd,GWL_EXSTYLE);
    using AdjustDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);using DpiFor=UINT(WINAPI*)(HWND);
    auto user=GetModuleHandleW(L"user32.dll");auto adjust=(AdjustDpi)GetProcAddress(user,"AdjustWindowRectExForDpi");auto forWindow=(DpiFor)GetProcAddress(user,"GetDpiForWindow");
    if(adjust&&forWindow)adjust(&r,style,GetMenu(t.hwnd)!=nullptr,ex,forWindow(t.hwnd));else AdjustWindowRectEx(&r,style,GetMenu(t.hwnd)!=nullptr,ex);
    if(IsIconic(t.hwnd)||IsZoomed(t.hwnd))ShowWindowAsync(t.hwnd,SW_RESTORE);
    bool ok=SetWindowPos(t.hwnd,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)!=0;if(ok)saved[t.hwnd].resized=true;return ok;
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
    auto rows=selected();if(rows.empty()){status(L"先勾选窗口。未找到游戏时，可用“添加窗口”手动选取。");return;}
    Settings c;if(!collect(c))return;cfg=c;int ok=0;DWORD error=0;
    if(action==Arrange){int i=(int)SendMessageW(controls[Screen],CB_GETCURSEL,0,0);if(i<0||i>=(int)monitors.size()){status(L"请先选择有效屏幕。");return;}auto cells=grid((int)rows.size(),monitors[i].work,c.columns,c.width,c.height);if(cells.size()!=rows.size()){status(L"窗口太多或列数不合适，屏幕无法容纳；请减少勾选数量或调整列数。");return;}
        for(size_t k=0;k<rows.size();k++){auto& t=rows[k];if(!capture(t))continue;if(IsIconic(t.hwnd)||IsZoomed(t.hwnd))ShowWindowAsync(t.hwnd,SW_RESTORE);SIZE size=fitClient(t,cells[k].w,cells[k].h,c.width,c.height);
            bool good=size.cx>0&&size.cy>0&&SetWindowPos(t.hwnd,nullptr,cells[k].x,cells[k].y,size.cx,size.cy,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);if(good){saved[t.hwnd].resized=true;ok++;}else error=GetLastError();}
        report(L"自动排列（按所填宽高比例适配屏幕）",ok,(int)rows.size(),error);
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
    bodyFont=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-px(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    for(auto& [id,w]:controls)SendMessageW(w,WM_SETFONT,(WPARAM)(id==Header?titleFont:bodyFont),TRUE);if(statusWindow)SendMessageW(statusWindow,WM_SETFONT,(WPARAM)bodyFont,TRUE);
}
static void layout(){
    RECT r;GetClientRect(mainWindow,&r);int W=MulDiv(r.right,96,dpi),H=MulDiv(r.bottom,96,dpi);int pad=20,space=W-2*pad;
    auto place=[&](int id,int x,int y,int w,int h){MoveWindow(controls[id],px(x),px(y),px(w),px(h),TRUE);};
    place(Icon,20,8,58,58);place(Header,90,15,165,40);place(Subtitle,260,27,space-340,24);place(Help,W-104,21,84,32);
    place(LPath,20,76,92,23);place(Path,116,71,space-184,30);place(Browse,W-100,71,80,30);
    place(LArgs,20,116,92,23);place(Args,116,110,space-96,30);
    place(LCount,20,155,64,23);place(Count,86,150,54,30);place(LDelay,158,155,98,23);place(Delay,258,150,80,30);place(Launch,360,150,126,32);place(Cancel,496,150,84,32);
    place(LFilter,20,202,92,23);place(Filter,116,196,space-392,30);place(Refresh,W-284,196,74,30);place(Pick,W-202,196,182,30);
    place(LWindows,20,239,space-170,24);place(SelectAll,W-177,234,72,28);place(SelectNone,W-97,234,77,28);
    int listHeight=std::max(155,H-475);place(List,20,268,space,listHeight);int y=280+listHeight;
    place(LWidth,20,y+5,55,22);place(Width,76,y,75,30);place(LHeight,161,y+5,45,22);place(Height,210,y,75,30);place(Resize,299,y,118,32);
    place(LColumns,436,y+5,85,22);place(Columns,524,y,55,30);place(LScreen,598,y+5,42,22);place(Screen,645,y,space-625,200);
    y+=44;place(Arrange,20,y,185,34);place(LOpacity,223,y+6,109,23);place(Opacity,335,y+2,65,30);place(Alpha,413,y,91,34);place(Hide,522,y,86,34);place(Show,618,y,86,34);place(Restore,714,y,space-694,34);
    place(Hint,20,y+45,space,26);MoveWindow(statusWindow,0,r.bottom-px(32),r.right,px(32),TRUE);
    if(listWindow){int widths[]={space-470,180,90,122,65};for(int i=0;i<5;i++)ListView_SetColumnWidth(listWindow,i,px(widths[i]));}
}
static void createUi(){
    fonts();auto icon=control(Icon,L"STATIC",L"",SS_ICON|SS_CENTERIMAGE);SendMessageW(icon,STM_SETICON,(WPARAM)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,px(58),px(58),LR_DEFAULTCOLOR),0);label(Header,L"鼠卫兵");SendMessageW(controls[Header],WM_SETFONT,(WPARAM)titleFont,TRUE);label(Subtitle,L"阴阳师窗口管家 · 0.1 预览版");button(Help,L"使用说明");
    label(LPath,L"游戏路径");edit(Path,cfg.path);button(Browse,L"选择…");label(LArgs,L"启动参数");edit(Args,cfg.args);
    label(LCount,L"启动数量");edit(Count,std::to_wstring(cfg.count),true);label(LDelay,L"间隔（毫秒）");edit(Delay,std::to_wstring(cfg.delay),true);button(Launch,L"逐个启动 / 多开");button(Cancel,L"停止队列");EnableWindow(controls[Cancel],FALSE);
    label(LFilter,L"进程筛选");edit(Filter,cfg.filter);button(Refresh,L"刷新");button(Pick,L"添加窗口（3 秒后选取）");
    label(LWindows,L"窗口列表");button(SelectAll,L"全选");button(SelectNone,L"全不选");
    listWindow=control(List,WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,WS_EX_CLIENTEDGE);
    SetWindowTheme(listWindow,L"Explorer",nullptr);
    ListView_SetExtendedListViewStyle(listWindow,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    const wchar_t* names[]={L"窗口标题",L"进程",L"状态",L"客户区尺寸",L"PID"};for(int i=0;i<5;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)names[i];c.cx=px(120);ListView_InsertColumn(listWindow,i,&c);}
    label(LWidth,L"宽（px）");edit(Width,std::to_wstring(cfg.width),true);label(LHeight,L"高（px）");edit(Height,std::to_wstring(cfg.height),true);button(Resize,L"应用客户区尺寸");
    label(LColumns,L"列数（0自动）");edit(Columns,std::to_wstring(cfg.columns),true);label(LScreen,L"屏幕");control(Screen,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
    button(Arrange,L"自动排列 · 适配屏幕");label(LOpacity,L"不透明度（%）");edit(Opacity,std::to_wstring(cfg.opacity),true);button(Alpha,L"应用透明度");button(Hide,L"隐藏");button(Show,L"显示");button(Restore,L"恢复原布局");
    label(Hint,L"筛选词匹配进程名，用分号分隔；仅操作勾选窗口。退出时恢复本工具隐藏的窗口。");
    statusWindow=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,mainWindow,nullptr,instance,nullptr);SendMessageW(statusWindow,WM_SETFONT,(WPARAM)bodyFont,TRUE);
    screens();layout();refresh();status(L"就绪。启动器可能限制多开；若只出现一个实例，请选择游戏实际客户端程序。");
}
static void restoreHidden(){for(auto& [h,t]:hidden)if(valid(t))ShowWindowAsync(h,SW_SHOW);}
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{mainWindow=w;using Dpi=UINT(WINAPI*)(HWND);auto f=(Dpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");if(f)dpi=(int)f(w);createUi();if(smokeMode)SetTimer(w,9,4500,nullptr);return 0;}
    case WM_SIZE:if(statusWindow){layout();InvalidateRect(w,nullptr,TRUE);}return 0;
    case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)lp;p->ptMinTrackSize={px(980),px(720)};return 0;}
    case WM_DPICHANGED:{dpi=HIWORD(wp);fonts();auto r=(RECT*)lp;SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
    case WM_DISPLAYCHANGE:screens();return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);FillRect(dc,&r,background);auto old=SelectObject(dc,cardBrush);auto pen=CreatePen(PS_SOLID,1,RGB(226,232,239));auto oldPen=SelectObject(dc,pen);RoundRect(dc,px(12),px(64),r.right-px(12),px(185),px(14),px(14));RoundRect(dc,px(12),px(188),r.right-px(12),r.bottom-px(39),px(14),px(14));SelectObject(dc,old);SelectObject(dc,oldPen);DeleteObject(pen);EndPaint(w,&p);return 0;}
    case WM_DRAWITEM:drawButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(HWND)lp==controls[Header]?RGB(35,58,100):RGB(64,76,93));bool head=(HWND)lp==controls[Header]||(HWND)lp==controls[Subtitle]||(HWND)lp==controls[Icon]||(HWND)lp==statusWindow;return (LRESULT)(head?background:cardBrush);}
    case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)switch(LOWORD(wp)){
        case Browse:browse();break;case Refresh:refresh();persist();status(L"列表已刷新。");break;
        case SelectAll:case SelectNone:for(int i=0;i<ListView_GetItemCount(listWindow);i++)ListView_SetCheckState(listWindow,i,LOWORD(wp)==SelectAll);break;
        case Launch:beginLaunch();break;case Cancel:KillTimer(w,1);launchRemaining=0;EnableWindow(controls[Launch],TRUE);EnableWindow(controls[Cancel],FALSE);status(L"启动队列已停止；已启动的游戏继续运行。");break;
        case Pick:EnableWindow(controls[Pick],FALSE);SetTimer(w,2,3000,nullptr);status(L"请在 3 秒内切换到要管理的游戏窗口。");break;
        case Resize:case Arrange:case Alpha:case Hide:case Show:case Restore:operate(LOWORD(wp));break;
        case Help:MessageBoxW(w,L"① 选择游戏启动器或实际客户端，填写数量与启动间隔。\n② 点击刷新；找不到窗口时，点“添加窗口”并在 3 秒内切换过去。\n③ 勾选窗口，再调整大小、排列、透明度或隐藏/显示。\n\n列数 0 自动选最佳网格；排列按所填宽高比例缩放到屏幕工作区。\n“恢复原布局”恢复本次会话首次修改前的位置、大小和透明度。\n配置损坏会尝试备份或默认值。隐藏恢复记录会跨重启保留。\n退出时显示由本程序隐藏的窗口。\n\n多开使用正常启动，不绕过客户端自身的实例限制。\n原生全屏游戏可能不接受窗口大小或透明度；请使用窗口模式。\n启动参数可留空。没有常驻服务、键鼠脚本或游戏内存操作。",L"鼠卫兵 · 使用说明",MB_OK|MB_ICONINFORMATION);break;
        }return 0;
    case WM_TIMER:switch(wp){
        case 1:launchOne();break;
        case 2:{KillTimer(w,2);EnableWindow(controls[Pick],TRUE);HWND fg=GetForegroundWindow();Target t=targetOf(fg);if(fg&&fg!=w&&t.pid!=GetCurrentProcessId()&&valid(t)&&!t.title.empty()){manualTargets.push_back(t);refresh();status(L"已添加窗口："+t.title);}else status(L"没有选到有效窗口，请重试并切换到游戏。");break;}
        case 3:KillTimer(w,3);for(auto i=hidden.begin();i!=hidden.end();){if(!valid(i->second)||IsWindowVisible(i->first))i=hidden.erase(i);else ++i;}saveHidden();refresh();break;
        case 9:KillTimer(w,9);DestroyWindow(w);break;
        }return 0;
    case WM_CLOSE:persist();restoreHidden();DestroyWindow(w);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
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
    bool fits=true;for(int n=1;n<=20;n++){RECT work{-1920,-200,0,880};auto cells=grid(n,work,0,1136,640);if(cells.size()!=(size_t)n){fits=false;break;}for(auto cell:cells)if(cell.x<work.left||cell.y<work.top||cell.x+cell.w>work.right||cell.y+cell.h>work.bottom)fits=false;}
    check(fits,"grid fits 1-20 windows including negative monitor origin");
    check(grid(100,{0,0,300,200},20,1136,640).empty(),"impossible layout rejected");
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
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\ShuWeiBing.Manager.01");if(mutex&&GetLastError()==ERROR_ALREADY_EXISTS){HWND old=FindWindowW(L"ShuWeiBing.Window",nullptr);if(old){ShowWindow(old,SW_RESTORE);SetForegroundWindow(old);}CloseHandle(mutex);return 0;}
    wchar_t local[MAX_PATH]{};if(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local)!=S_OK){MessageBoxW(nullptr,L"无法找到用户配置目录。",L"鼠卫兵",MB_OK|MB_ICONERROR);return 1;}
    configDir=S(local)+L"\\ShuWeiBing";CreateDirectoryW(configDir.c_str(),nullptr);configPath=configDir+L"\\settings.dat";
    int source;loadSettings(configPath,cfg,source);if(cfg.path.empty())cfg.path=detectGame();loadHidden();
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    background=CreateSolidBrush(RGB(244,246,250));cardBrush=CreateSolidBrush(RGB(255,255,255));WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=h;wc.lpszClassName=L"ShuWeiBing.Window";wc.lpfnWndProc=proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=background;wc.hIcon=LoadIconW(h,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;RegisterClassExW(&wc);
    auto systemDpi=(UINT(WINAPI*)())GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem");if(systemDpi)dpi=(int)systemDpi();
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"鼠卫兵 · 轻量窗口管家",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(1000),px(750),nullptr,nullptr,h,nullptr);
    if(!w)return 1;DWORD corner=2;DwmSetWindowAttribute(w,33,&corner,sizeof(corner));ShowWindow(w,show);UpdateWindow(w);
    if(!hidden.empty())status(L"找到上次隐藏的 "+std::to_wstring(hidden.size())+L" 个窗口；勾选后点击“显示”即可恢复。");
    else if(source==1)status(L"配置损坏，已恢复有效备份。窗口管理可正常使用。");else if(source==2&&fileExists(configPath))status(L"配置及备份无法读取，已使用默认设置；重新填写后会安全保存。");
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(mutex)CloseHandle(mutex);DeleteObject(bodyFont);DeleteObject(titleFont);DeleteObject(background);DeleteObject(cardBrush);return (int)msg.wParam;
}
