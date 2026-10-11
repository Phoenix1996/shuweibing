// Test fixture: deliberately no DPI manifest, never opens or modifies a game.
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
static LRESULT CALLBACK holdProc(HWND w,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_TIMER){DestroyWindow(w);return 0;}if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,message,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==3&&std::wstring(argv[1])==L"--hold"){
        WNDCLASSW wc{};wc.lpfnWndProc=holdProc;wc.hInstance=instance;wc.lpszClassName=L"ShuWeiBing.LaunchTest";RegisterClassW(&wc);
        HWND window=CreateWindowExW(0,wc.lpszClassName,L"鼠卫兵多开测试窗口",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,instance,nullptr);
        if(!window){LocalFree(argv);return 1;}
        wchar_t directory[32768]{};GetCurrentDirectoryW(32768,directory);
        std::wstring record=L"\uFEFF"+std::to_wstring((uintptr_t)window)+L"\n"+directory;
        HANDLE file=CreateFileW(argv[2],GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);LocalFree(argv);
        if(file==INVALID_HANDLE_VALUE){DestroyWindow(window);return 1;}DWORD written{};WriteFile(file,record.data(),(DWORD)(record.size()*2),&written,nullptr);CloseHandle(file);
        SetTimer(window,1,15000,nullptr);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
    }
    if(argc!=2){LocalFree(argv);return 1;}
    HWND window=CreateWindowExW(0,L"STATIC",L"ShuWeiBing DPI test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,instance,nullptr);
    using Context=HANDLE(WINAPI*)(HWND);using Awareness=int(WINAPI*)(HANDLE);using Dpi=UINT(WINAPI*)(HWND);
    auto user=GetModuleHandleW(L"user32.dll");
    auto context=(Context)GetProcAddress(user,"GetWindowDpiAwarenessContext");
    auto awareness=(Awareness)GetProcAddress(user,"GetAwarenessFromDpiAwarenessContext");
    auto dpi=(Dpi)GetProcAddress(user,"GetDpiForWindow");
    FILE* file=nullptr;if(_wfopen_s(&file,argv[1],L"w")||!file){LocalFree(argv);DestroyWindow(window);return 1;}
    fprintf(file,"awareness=%d dpi=%u\n",awareness(context(window)),dpi(window));fclose(file);DestroyWindow(window);LocalFree(argv);return 0;
}
