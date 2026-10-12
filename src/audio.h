#pragma once
#include <windows.h>
#include <functional>
namespace audio {
struct Result {int matched=0,changed=0;bool shared=false;HRESULT error=S_OK;};
// Match only sessions that Windows attributes to a single selected process.
Result change(const std::function<bool(DWORD)>& selected,bool toggleMute,int volume=-1);
Result inspect(DWORD pid);
void selfTest(const std::function<void(bool,const char*)>& check);
}
