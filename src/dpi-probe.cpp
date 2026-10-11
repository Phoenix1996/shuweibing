// Test fixture: deliberately no DPI manifest, never opens or modifies a game.
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc!=2){LocalFree(argv);return 1;}
    HWND window=CreateWindowExW(0,L"STATIC",L"ShuWeiBing DPI test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,instance,nullptr);
    using Context=HANDLE(WINAPI*)(HWND);using Awareness=int(WINAPI*)(HANDLE);using Dpi=UINT(WINAPI*)(HWND);
    auto user=GetModuleHandleW(L"user32.dll");
    auto context=(Context)GetProcAddress(user,"GetWindowDpiAwarenessContext");
    auto awareness=(Awareness)GetProcAddress(user,"GetAwarenessFromDpiAwarenessContext");
    auto dpi=(Dpi)GetProcAddress(user,"GetDpiForWindow");
    FILE* file=nullptr;if(_wfopen_s(&file,argv[1],L"w")||!file){LocalFree(argv);DestroyWindow(window);return 1;}
    fprintf(file,"awareness=%d dpi=%u\n",awareness(context(window)),dpi(window));fclose(file);DestroyWindow(window);LocalFree(argv);return 0;
}
