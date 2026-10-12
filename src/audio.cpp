#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "audio.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <audioclient.h>
#include <vector>
#include <cstdio>
namespace audio {
template<class T> struct Ptr {T* p=nullptr;~Ptr(){if(p)p->Release();}T** put(){return &p;}T* operator->()const{return p;}operator bool()const{return p!=nullptr;}};
struct Session {DWORD pid;ISimpleAudioVolume* volume;};
static Result visit(const std::function<bool(DWORD)>& selected,const std::function<void(std::vector<Session>&,Result&)>& apply){
    Result result;Ptr<IMMDeviceEnumerator> enumerator;HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)enumerator.put());if(FAILED(hr)){result.error=hr;return result;}
    Ptr<IMMDeviceCollection> devices;hr=enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,devices.put());if(FAILED(hr)){result.error=hr;return result;}
    UINT count=0;devices->GetCount(&count);std::vector<Session> matches;
    for(UINT d=0;d<count;d++){Ptr<IMMDevice> device;if(FAILED(devices->Item(d,device.put())))continue;Ptr<IAudioSessionManager2> manager;if(FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,(void**)manager.put())))continue;
        Ptr<IAudioSessionEnumerator> sessions;if(FAILED(manager->GetSessionEnumerator(sessions.put())))continue;int n=0;sessions->GetCount(&n);
        for(int i=0;i<n;i++){Ptr<IAudioSessionControl> control;if(FAILED(sessions->GetSession(i,control.put())))continue;Ptr<IAudioSessionControl2> identity;if(FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),(void**)identity.put())))continue;
            DWORD pid=0;hr=identity->GetProcessId(&pid);if(!selected(pid))continue;
            if(hr!=S_OK){if(SUCCEEDED(hr))result.shared=true;continue;}AudioSessionState state;if(FAILED(control->GetState(&state))||state==AudioSessionStateExpired)continue;
            ISimpleAudioVolume* volume=nullptr;if(SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),(void**)&volume)))matches.push_back({pid,volume});
        }
    }
    result.matched=(int)matches.size();apply(matches,result);for(auto& session:matches)session.volume->Release();return result;
}
Result change(const std::function<bool(DWORD)>& selected,bool toggleMute,int level){
    return visit(selected,[&](std::vector<Session>& sessions,Result& result){
        bool allMuted=!sessions.empty();for(auto& session:sessions){BOOL muted=FALSE;HRESULT hr=session.volume->GetMute(&muted);if(FAILED(hr)){result.error=hr;return;}allMuted=allMuted&&muted;}
        for(auto& session:sessions){if(!selected(session.pid))continue;HRESULT hr=toggleMute?session.volume->SetMute(!allMuted,nullptr):session.volume->SetMasterVolume(level/100.0f,nullptr);if(SUCCEEDED(hr))result.changed++;else result.error=hr;}
    });
}
Result inspect(DWORD pid){return visit([&](DWORD current){return current==pid;},[](std::vector<Session>&,Result&){});}
void selfTest(const std::function<void(bool,const char*)>& check){
    struct Apartment {HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment;
    Ptr<IMMDeviceEnumerator> enumerator;Ptr<IMMDevice> device;Ptr<IAudioClient> client;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)enumerator.put());if(SUCCEEDED(hr))hr=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,device.put());if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)client.put());
    WAVEFORMATEX* format=nullptr;if(SUCCEEDED(hr))hr=client->GetMixFormat(&format);GUID session{};if(SUCCEEDED(hr))hr=CoCreateGuid(&session);if(SUCCEEDED(hr))hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,format,&session);if(format)CoTaskMemFree(format);
    if(FAILED(hr)){printf("SKIP per-process audio integration: no usable audio endpoint (0x%08lx)\n",(unsigned long)hr);return;}
    Ptr<ISimpleAudioVolume> volume;hr=client->GetService(__uuidof(ISimpleAudioVolume),(void**)volume.put());if(FAILED(hr)){check(false,"owned audio session provides volume interface");return;}
    BOOL previousMute=FALSE;float previousLevel=1;volume->GetMute(&previousMute);volume->GetMasterVolume(&previousLevel);volume->SetMute(FALSE,nullptr);
    // Never start playback. The initialized silent stream supplies our own session.
    auto own=[](DWORD pid){return pid==GetCurrentProcessId();};auto none=[](DWORD){return false;};
    auto ignored=change(none,true);BOOL muted=TRUE;volume->GetMute(&muted);check(ignored.changed==0&&!muted,"audio operation excludes sessions outside the selected process set");
    auto first=change(own,true);volume->GetMute(&muted);check(first.changed>0&&muted,"native per-process audio mute changes only the owned session");
    auto second=change(own,true);volume->GetMute(&muted);check(second.changed>0&&!muted,"second mute press restores sound for the selected session");
    auto adjusted=change(own,false,35);float level=0;volume->GetMasterVolume(&level);check(adjusted.changed>0&&level>0.349f&&level<0.351f,"native per-process audio volume applies the selected percentage");
    volume->SetMute(previousMute,nullptr);volume->SetMasterVolume(previousLevel,nullptr);
}
}
