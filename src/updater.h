#pragma once
#include <windows.h>
#include <string>
#include <functional>
namespace updates {
constexpr UINT Message=WM_APP+8;
struct Release {std::wstring version,body,exeUrl,sumUrl;};
struct Result {Release release;std::wstring error,file;bool manual=false,download=false;};
std::wstring currentVersion();
bool check(HWND owner,bool manual);
bool download(HWND owner,const Release& release);
bool handoff(const std::wstring& file,std::wstring& error);
// Returns -1 for normal startup; update helper mode never loads user settings.
int applyMode(int argc,wchar_t** argv);
bool probe(const std::wstring& output);
void selfTest(const std::wstring& dir,const std::function<void(bool,const char*)>& check);
}
